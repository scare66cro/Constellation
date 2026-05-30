/*
 * Constellation Nova — independent watchdog core (R5FSS1_0)
 *
 * This image runs on the second R5F cluster's first core. It is
 * a NoRTOS bare-loop firmware whose ONLY job is:
 *
 *   1. Spin reading a 64-byte heartbeat struct in MSRAM at
 *      0x701DFFC0 (LP_WD_SHM_ADDR). The main core (R5FSS0_0) writes
 *      this struct from `lp_watchdog_client.c`.
 *   2. While the heartbeat is fresh AND the main core has reported
 *      all required subsystems alive, strobe the DWWD (defense in
 *      depth — see note below).
 *   3. The moment the heartbeat goes stale (counter unchanged for
 *      > 5 s OR required alive bits drop for > 8 s), call
 *      `Sciclient_pmDeviceReset(SystemP_WAIT_FOREVER)` — sends
 *      TISCI_MSG_SYS_RESET to DMSC, which orchestrates a real SoC
 *      warm reset → ROM re-runs → SBL re-evaluates bank A/B and
 *      loads the freshly-saved (or rolled-back) image.
 *
 * Why TISCI and not DWWD-triggered reset:
 *   `Watchdog_configureWarmReset()` is a NO-OP stub on AM243x —
 *   the SDK explicitly says "Reset is not supported on AM64x" in
 *   `source/drivers/watchdog/v1/watchdog_rti.c:309`. So even
 *   though we configure `Watchdog_RESET_ON`, DWWD expiry does NOT
 *   trigger any reset signal. The DWWD config still serves as a
 *   secondary detection mechanism (status flag for post-mortem)
 *   but the real reset is the explicit Sciclient call.
 *
 * Why a separate core: a same-core software watchdog cannot detect
 * BSS corruption that flips its own gate flag, or a runaway loop
 * inside the strobe task itself. Cluster 1 has its own clock,
 * its own reset domain, and its own DWWD/Sciclient channel — no
 * shared resource the main core can scribble to silence the
 * watchdog short of writing the heartbeat struct (which the
 * watchdog verifies for staleness anyway).
 *
 * Full design: docs/lp-am2434-watchdog-design.md
 *
 * IMPORTANT: keep this code TINY and DEPENDENCY-LIGHT. The whole
 * point is that this core never hangs. No FreeRTOS, no Ethernet,
 * no IPC subsystem — just MPU + DWWD + Sciclient + a polled MSRAM
 * read.
 */

#include <stdint.h>
#include <stdbool.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/CacheP.h>
#include <kernel/dpl/SystemP.h>
#include <kernel/dpl/DebugP.h>
#include <drivers/watchdog.h>
#include <drivers/sciclient.h>
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"

#include "../lp_watchdog_ipc.h"

/* ─── Tuning constants ─────────────────────────────────────────────
 * All times in milliseconds. Margins chosen to give the main core
 * comfortable boot grace while still catching real hangs quickly. */
#define WD_POLL_PERIOD_MS          100u   /* 10 Hz polling */
#define WD_BOOT_GRACE_MS           8000u  /* magic-not-yet-set window */
#define WD_COUNTER_STALE_MS        5000u  /* counter unchanged this long → stop strobing */
#define WD_ALIVE_BITS_GRACE_MS     8000u  /* alive bits dropped this long → stop strobing */
#define WD_HEALTHY_LOG_PERIOD_MS   30000u /* heartbeat log every 30 s */

/* ─── Local state ──────────────────────────────────────────────────
 * Lives in cluster-1 TCMA (per syscfg). MSRAM contents are wiped
 * by warm reset so this is the only durable state across a strobe-
 * gate evaluation cycle. */
static uint32_t s_last_seen_counter        = 0u;
static uint32_t s_last_counter_change_ms   = 0u;
static uint32_t s_last_alive_ok_ms         = 0u;
static uint32_t s_boot_ms                  = 0u;
static uint32_t s_wd_counter               = 0u;
static uint32_t s_last_healthy_log_ms      = 0u;
static bool     s_observed_magic           = false;

/* ─── Helpers ──────────────────────────────────────────────────────*/

static inline uint32_t now_ms(void)
{
    /* ClockP_getTimeUsec is bare-metal-safe on R5F. */
    return (uint32_t)(ClockP_getTimeUsec() / 1000u);
}

/* Snapshot the heartbeat struct under cache-invalidate so we don't
 * read stale values from the L1 D-cache. We copy field-by-field
 * into locals (struct copy could in theory break atomicity for
 * 32-bit fields on some compilers; field-by-field is defensive). */
static void shm_snapshot(uint32_t *out_magic,
                         uint32_t *out_counter,
                         uint32_t *out_alive,
                         uint32_t *out_required,
                         uint32_t *out_uptime)
{
    CacheP_inv((void *)LP_WD_SHM_ADDR, sizeof(LpWatchdogShm), CacheP_TYPE_ALL);
    *out_magic    = LP_WD_SHM_ADDR->magic;
    *out_counter  = LP_WD_SHM_ADDR->counter;
    *out_alive    = LP_WD_SHM_ADDR->alive_bits;
    *out_required = LP_WD_SHM_ADDR->required_mask;
    *out_uptime   = LP_WD_SHM_ADDR->main_uptime_ms;
}

static void shm_post_wd(uint32_t reason)
{
    LP_WD_SHM_ADDR->wd_counter     = ++s_wd_counter;
    LP_WD_SHM_ADDR->wd_uptime_ms   = now_ms() - s_boot_ms;
    LP_WD_SHM_ADDR->wd_last_reason = reason;
    CacheP_wb((void *)LP_WD_SHM_ADDR, sizeof(LpWatchdogShm), CacheP_TYPE_ALL);
}

/* ─── Strobe gate decision ─────────────────────────────────────────
 *
 * Returns the reason code (0 = strobe is allowed).
 *
 * Decision tree (in order):
 *   1. Boot grace (magic never observed) AND we're inside boot
 *      grace window → STROBE (return 0). Main core hasn't finished
 *      coming up yet.
 *   2. Magic was observed previously but is gone now → MAGIC_LOST.
 *      Either main core hung during init or BSS got wiped.
 *   3. counter has not advanced in WD_COUNTER_STALE_MS → COUNTER_FROZEN.
 *   4. (alive_bits & required) != required for > WD_ALIVE_BITS_GRACE_MS
 *      → ALIVE_BITS_DROPPED. Main is alive enough to write counter
 *      but a critical subsystem failed.
 *   5. main_uptime_ms unchanged for > WD_COUNTER_STALE_MS → MAIN_UPTIME_STALL
 *      (defense-in-depth — catches the case where main wraps counter
 *      back to its previous value through corruption). */
static uint32_t evaluate_strobe(uint32_t now)
{
    uint32_t magic, counter, alive, required, uptime;
    shm_snapshot(&magic, &counter, &alive, &required, &uptime);

    /* Boot grace: until magic appears, freely strobe. */
    if (!s_observed_magic) {
        if (magic == LP_WD_MAGIC) {
            s_observed_magic = true;
            s_last_seen_counter      = counter;
            s_last_counter_change_ms = now;
            s_last_alive_ok_ms       = now;
            DebugP_log("[WD] magic observed at +%u ms — entering strict mode\r\n",
                       (unsigned)(now - s_boot_ms));
        } else if ((now - s_boot_ms) < WD_BOOT_GRACE_MS) {
            return LP_WD_REASON_NONE;  /* still in grace */
        } else {
            /* Grace expired with no magic — main core never finished
             * boot init. Stop strobing → reset. */
            return LP_WD_REASON_MAGIC_LOST;
        }
    }

    /* Strict mode from here on. */
    if (magic != LP_WD_MAGIC) {
        return LP_WD_REASON_MAGIC_LOST;
    }

    if (counter != s_last_seen_counter) {
        s_last_seen_counter      = counter;
        s_last_counter_change_ms = now;
    } else if ((now - s_last_counter_change_ms) > WD_COUNTER_STALE_MS) {
        return LP_WD_REASON_COUNTER_FROZEN;
    }

    if ((alive & required) == required) {
        s_last_alive_ok_ms = now;
    } else if ((now - s_last_alive_ok_ms) > WD_ALIVE_BITS_GRACE_MS) {
        return LP_WD_REASON_ALIVE_BITS_DROPPED;
    }

    /* Track main_uptime separately: if it stops advancing we're
     * looking at a frozen scheduler even if counter wraps. */
    static uint32_t s_last_main_uptime = 0u;
    static uint32_t s_last_main_uptime_change_ms = 0u;
    if (uptime != s_last_main_uptime) {
        s_last_main_uptime = uptime;
        s_last_main_uptime_change_ms = now;
    } else if ((now - s_last_main_uptime_change_ms) > WD_COUNTER_STALE_MS) {
        return LP_WD_REASON_MAIN_UPTIME_STALL;
    }

    return LP_WD_REASON_NONE;
}

/* ─── Entry point ──────────────────────────────────────────────────*/

void watchdog_main(void *args)
{
    (void)args;

    s_boot_ms = now_ms();
    DebugP_log("[WD] watchdog core boot at +%u ms\r\n", (unsigned)s_boot_ms);
    DebugP_log("[WD] heartbeat shm @ %p (%u B)\r\n",
               (void *)LP_WD_SHM_ADDR, (unsigned)sizeof(LpWatchdogShm));
    DebugP_log("[WD] DWWD configured for reset-on-expire (10 s window)\r\n");

    /* Strobe once immediately — driver opens with the timer running. */
    Watchdog_clear(gWatchdogHandle[CONFIG_WDT0]);

    for (;;) {
        const uint32_t now = now_ms();
        const uint32_t reason = evaluate_strobe(now);

        if (reason == LP_WD_REASON_NONE) {
            Watchdog_clear(gWatchdogHandle[CONFIG_WDT0]);
        } else {
            /* Stop strobing AND issue a TISCI system reset directly via
             * DMSC. The original implementation relied on letting the
             * DWWD expire to trigger an SoC reset, but on AM243x the
             * SDK driver's `Watchdog_configureWarmReset` is a no-op
             * stub (`source/drivers/watchdog/v1/soc/am64x_am243x/
             * watchdog_soc.c:53` literally `return;`). Comment in
             * driver source: "Reset is not supported on AM64x". DWWD
             * expiry sets a status flag but asserts no reset signal.
             *
             * Without an explicit Sciclient call, the entire watchdog
             * reset chain is a NO-OP — main core hangs, watchdog
             * detects, stops strobing, DWWD expires, NOTHING HAPPENS.
             * Field OTA / stuck-customer-panel recovery silently fails.
             *
             * `Sciclient_pmDeviceReset(SystemP_WAIT_FOREVER)` sends
             * TISCI_MSG_SYS_RESET to DMSC; DMSC orchestrates a real
             * SoC warm reset that re-runs ROM/SBL → bank A/B
             * arbitration → loads the freshly-saved (or rolled-back)
             * image. Same path as the bridge's `POST /iot/system/
             * reboot` (memories/repo/lp-am2434-remote-reset-solved.md).
             *
             * Sciclient is initialized in this core's Drivers_open()
             * (Sciclient_init(CSL_CORE_ID_R5FSS1_0)). */
            shm_post_wd(reason);
            DebugP_log("[WD] STROBE STOPPED reason=%u — issuing TISCI reset\r\n",
                       (unsigned)reason);
            int32_t rc = Sciclient_pmDeviceReset(SystemP_WAIT_FOREVER);
            /* Should not return — DMSC tears down the SoC mid-call.
             * If it does return (DMSC unhappy / TISCI rejected), log
             * once and spin so a JTAG operator sees the failure mode
             * instead of a wedge that looks like nothing happened. */
            DebugP_log("[WD] Sciclient_pmDeviceReset returned rc=%d (UNEXPECTED) — spinning\r\n",
                       (int)rc);
            for (;;) {
                ClockP_usleep(100000);  /* 100 ms idle */
            }
        }

        /* Heartbeat back to main core every poll. */
        shm_post_wd(LP_WD_REASON_NONE);

        /* Periodic life-sign on the boot UART so a human watching
         * the trace knows the watchdog is alive. */
        if ((now - s_last_healthy_log_ms) >= WD_HEALTHY_LOG_PERIOD_MS) {
            s_last_healthy_log_ms = now;
            DebugP_log("[WD] healthy +%u s, ctr=%u, wd_ctr=%u\r\n",
                       (unsigned)((now - s_boot_ms) / 1000u),
                       (unsigned)s_last_seen_counter,
                       (unsigned)s_wd_counter);
        }

        ClockP_usleep(WD_POLL_PERIOD_MS * 1000u);
    }
}

int main(void)
{
    System_init();
    Board_init();
    Drivers_open();
    Board_driversOpen();

    watchdog_main(NULL);

    /* Unreachable. */
    Board_driversClose();
    Drivers_close();
    Board_deinit();
    System_deinit();
    return 0;
}
