/*
 * lp_watchdog_client.c — main-core (R5FSS0_0) heartbeat producer
 *
 * Runs as a low-priority FreeRTOS task. Once per second it:
 *
 *   1. Recomputes `alive_bits` from per-subsystem last-ping timestamps
 *      (each producer calls `LpWatchdog_Ping(LP_WD_ALIVE_X)` from its
 *      own loop body — the client just reads the stored timestamps).
 *   2. Increments `counter`.
 *   3. Updates `main_uptime_ms` from FreeRTOS tick count.
 *   4. Cache-flushes the heartbeat struct so cluster-1's view is fresh.
 *
 * On boot, the client first writes `required_mask` (which subsystems
 * MUST be alive for the watchdog to allow strobing) and only at the
 * very end writes `magic = LP_WD_MAGIC`. Until magic appears the
 * watchdog core is in "boot grace" mode and freely strobes its DWWD.
 *
 * Producer hooks — one-line `LpWatchdog_Ping(LP_WD_ALIVE_X);` calls
 * to add later in:
 *   ALIVE_MODBUS       → orbit_modbus_tcp.c accept loop
 *   ALIVE_SYSTEMSTATUS → main.c envelope task after each emit
 *   ALIVE_LWIP_LINK    → checked here directly via netif_default
 *   ALIVE_ENGINE_TICK  → lp_engine_shim.c lp_engine_tick() exit
 *   ALIVE_OSPI         → optional, only flag in required_mask once it
 *                        has a producer.
 *
 * Full design: docs/lp-am2434-watchdog-design.md
 */

#include <stdint.h>
#include <stdbool.h>
#include <FreeRTOS.h>
#include <task.h>
#include <kernel/dpl/CacheP.h>
#include <kernel/dpl/DebugP.h>
#include <lwip/netif.h>

#include "lp_watchdog_ipc.h"

/* ─── Producer ping store ──────────────────────────────────────────
 * Per-bit "last time this subsystem reported alive" in ms (FreeRTOS
 * tick-derived, wraps at 49.7 days). Producers atomically store
 * xTaskGetTickCount() * portTICK_PERIOD_MS via LpWatchdog_Ping.
 *
 * Bit→index mapping is the bit position. We size for 8 bits which
 * is the practical maximum (currently 5 defined). */
#define LP_WD_NUM_BITS  8u
static volatile uint32_t s_last_ping_ms[LP_WD_NUM_BITS] = { 0u };

/* How long a ping is considered "fresh" before its bit drops out of
 * alive_bits. Generous on purpose — the watchdog's own staleness
 * grace window is 5-8 s, this is a tighter check. */
#define LP_WD_FRESH_MS  3000u

void LpWatchdog_Ping(uint32_t alive_bit)
{
    /* Find the single set bit position. __builtin_ctz is a single
     * instruction on Cortex-R5; safe for non-zero input. */
    if (alive_bit == 0u) return;
    const uint32_t idx = (uint32_t)__builtin_ctz(alive_bit);
    if (idx >= LP_WD_NUM_BITS) return;
    s_last_ping_ms[idx] = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* ─── Heartbeat task ───────────────────────────────────────────────*/

#define LP_WD_HEARTBEAT_PERIOD_MS  1000u

static uint32_t s_counter = 0u;

static uint32_t recompute_alive_bits(uint32_t now_ms)
{
    uint32_t bits = 0u;

    for (uint32_t i = 0u; i < LP_WD_NUM_BITS; i++) {
        const uint32_t last = s_last_ping_ms[i];
        if (last != 0u && (now_ms - last) < LP_WD_FRESH_MS) {
            bits |= (1u << i);
        }
    }

    /* Direct check for ALIVE_LWIP_LINK — we don't need a producer
     * hook because lwIP exposes netif_default's LINK_UP flag
     * unconditionally. */
    extern struct netif *netif_default;
    if (netif_default != NULL && (netif_default->flags & NETIF_FLAG_LINK_UP)) {
        bits |= LP_WD_ALIVE_LWIP_LINK;
    } else {
        bits &= ~LP_WD_ALIVE_LWIP_LINK;
    }

    return bits;
}

static void lp_watchdog_heartbeat_task(void *pvParameters)
{
    (void)pvParameters;

    /* Phase 1: write required_mask BEFORE the magic. Watchdog may
     * read the struct any time but won't enter strict mode until
     * magic == LP_WD_MAGIC. */
    LP_WD_SHM_ADDR->required_mask  = LP_WD_REQUIRED_DEFAULT;
    LP_WD_SHM_ADDR->alive_bits     = 0u;
    LP_WD_SHM_ADDR->counter        = 0u;
    LP_WD_SHM_ADDR->main_uptime_ms = 0u;
    LP_WD_SHM_ADDR->wd_counter     = 0u;
    LP_WD_SHM_ADDR->wd_uptime_ms   = 0u;
    LP_WD_SHM_ADDR->wd_last_reason = 0u;
    /* Magic written LAST so cluster 1 never observes a half-init
     * struct with stale alive_bits. */
    LP_WD_SHM_ADDR->magic          = LP_WD_MAGIC;
    CacheP_wb((void *)LP_WD_SHM_ADDR, sizeof(LpWatchdogShm), CacheP_TYPE_ALL);

    DebugP_log("[WD-CLIENT] heartbeat task started, required=0x%08x\r\n",
               (unsigned)LP_WD_REQUIRED_DEFAULT);

    for (;;) {
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        const uint32_t alive  = recompute_alive_bits(now_ms);

        s_counter++;
        LP_WD_SHM_ADDR->counter        = s_counter;
        LP_WD_SHM_ADDR->alive_bits     = alive;
        LP_WD_SHM_ADDR->main_uptime_ms = now_ms;
        CacheP_wb((void *)LP_WD_SHM_ADDR, sizeof(LpWatchdogShm), CacheP_TYPE_ALL);

        vTaskDelay(pdMS_TO_TICKS(LP_WD_HEARTBEAT_PERIOD_MS));
    }
}

/* Static task buffers — created from main.c once at boot. */
#define LP_WD_TASK_STACK_WORDS  512u
static StaticTask_t s_wd_task_buf;
static StackType_t  s_wd_task_stack[LP_WD_TASK_STACK_WORDS];

/* Public init — called once from main.c's task-creation block. */
TaskHandle_t LpWatchdogClient_Start(void)
{
    return xTaskCreateStatic(lp_watchdog_heartbeat_task,
                             "wd_client",
                             LP_WD_TASK_STACK_WORDS,
                             NULL,
                             /* Priority: tskIDLE_PRIORITY + 1 — must
                              * be higher than the idle task so it
                              * actually runs, but low enough that a
                              * real-time task spinning won't starve
                              * it for the full WD_COUNTER_STALE_MS
                              * window. */
                             tskIDLE_PRIORITY + 1u,
                             s_wd_task_stack,
                             &s_wd_task_buf);
}

/* Returns true if the watchdog core has been heartbeating us back
 * within the last 2 s. Used by FwUpdateHealthy_Mark to verify
 * "the watchdog is alive" before declaring the firmware healthy
 * for OTA rollback purposes. */
bool LpWatchdog_PeerAlive(void)
{
    static uint32_t s_last_seen_wd_ctr = 0u;
    static uint32_t s_last_seen_at_ms  = 0u;
    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    CacheP_inv((void *)LP_WD_SHM_ADDR, sizeof(LpWatchdogShm), CacheP_TYPE_ALL);
    const uint32_t wd_ctr = LP_WD_SHM_ADDR->wd_counter;

    if (wd_ctr != s_last_seen_wd_ctr) {
        s_last_seen_wd_ctr = wd_ctr;
        s_last_seen_at_ms  = now_ms;
        return true;
    }

    return (now_ms - s_last_seen_at_ms) < 2000u;
}
