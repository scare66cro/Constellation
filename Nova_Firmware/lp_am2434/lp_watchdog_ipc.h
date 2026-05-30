/*
 * lp_watchdog_ipc.h — shared heartbeat protocol between R5FSS0_0
 * (main core) and R5FSS1_0 (independent watchdog core).
 *
 * **Both cores include this header verbatim.** Wire layout MUST stay
 * binary-compatible across builds — this struct lives at a fixed
 * MSRAM address (`LP_WD_SHM_ADDR` below) and is poked through a raw
 * pointer cast on each side. Field order, sizes, and struct size
 * are part of the contract.
 *
 * Full architectural notes:
 *   docs/lp-am2434-watchdog-design.md
 *
 * Quick-reference rules (also in the design doc):
 *   - Magic byte gates strict mode. Watchdog freely strobes until
 *     main core writes magic — covers boot grace (~3-5 s).
 *   - Main core writes `magic` LAST during init so watchdog can't
 *     observe a half-initialised struct.
 *   - Counter is a free-running monotonic uint32 — 49.7 days of
 *     wrap-free operation at 1 Hz. Watchdog uses delta, not absolute.
 *   - Cache discipline: main does CacheP_wb after each write, watchdog
 *     does CacheP_inv before each read. Struct is 64-byte aligned
 *     (one ARM cache line on Cortex-R5F) so flushes don't touch
 *     unrelated data.
 */

#ifndef LP_WATCHDOG_IPC_H
#define LP_WATCHDOG_IPC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Shared memory placement ───────────────────────────────────────
 *
 * AM243x MSRAM is 2 MB at 0x70000000-0x701FFFFF.
 *   0x70000000-0x7007FFFF (512 KB)  reserved for SBL
 *   0x70080000-0x701DFFBF (~1.4 MB) main core code/data (syscfg)
 *   0x701DFFC0-0x701DFFFF (64 B)    THIS struct
 *   0x701E0000-0x701FFFFF (128 KB)  watchdog core code/data
 *
 * The main core's MSRAM region in example.syscfg is sized 0x15FFC0
 * (was 0x160000) to carve out the 64 B for the heartbeat struct.
 * Watchdog core has its own example.syscfg with MSRAM region starting
 * at 0x701E0000.
 */
#define LP_WD_SHM_ADDR  ((volatile LpWatchdogShm *)0x701DFFC0u)

/* "WDGH" little-endian. Watchdog core refuses to enter strict mode
 * until it observes this exact value. */
#define LP_WD_MAGIC     0x57444748u

/* ─── Liveness bitmap (alive_bits / required_mask) ──────────────────
 *
 * Main core sets bits in `alive_bits` from its heartbeat task as each
 * subsystem reports OK. Watchdog core's strict-mode strobe gate is:
 *
 *     (alive_bits & required_mask) == required_mask
 *
 * `required_mask` is set ONCE by main core at boot — lets us add new
 * subsystems (additional bits) without rebuilding the watchdog image.
 */
#define LP_WD_ALIVE_MODBUS         (1u << 0)  /* orbit Modbus accept loop */
#define LP_WD_ALIVE_SYSTEMSTATUS   (1u << 1)  /* SystemStatus envelope <2 s */
#define LP_WD_ALIVE_LWIP_LINK      (1u << 2)  /* netif LINK_UP */
#define LP_WD_ALIVE_ENGINE_TICK    (1u << 3)  /* lp_engine_tick <5 s */
#define LP_WD_ALIVE_OSPI           (1u << 4)  /* OSPI driver responsive */
/* Bits 5..15 reserved for future subsystems. */

#define LP_WD_REQUIRED_DEFAULT \
    (LP_WD_ALIVE_MODBUS | LP_WD_ALIVE_SYSTEMSTATUS | \
     LP_WD_ALIVE_LWIP_LINK | LP_WD_ALIVE_ENGINE_TICK)

/* ─── The struct ────────────────────────────────────────────────────
 *
 * Exactly 64 bytes — one Cortex-R5F cache line. Padding at the end
 * keeps the size stable across additions.
 */
typedef struct {
    /* Written LAST by main core during boot init.
     * Watchdog gates strict mode on `magic == LP_WD_MAGIC`. */
    uint32_t magic;                 /* offset 0 */

    /* Free-running counter, incremented by main heartbeat task
     * (1 Hz). Watchdog tracks delta between samples. Wrap is fine
     * — watchdog only checks "did it advance?". */
    uint32_t counter;               /* offset 4 */

    /* Bitmap of currently-alive subsystems. Main heartbeat task
     * recomputes this every tick from the per-subsystem
     * timestamps it tracks. */
    uint32_t alive_bits;            /* offset 8 */

    /* Bits in `alive_bits` that MUST be set for strict-mode strobe.
     * Written once by main at boot. */
    uint32_t required_mask;         /* offset 12 */

    /* Main core's xTaskGetTickCount * portTICK_PERIOD_MS. Watchdog
     * verifies strict monotonic increase as a second freshness
     * check (catches the case where main wrote `counter` but the
     * value happened to repeat after a wrap). */
    uint32_t main_uptime_ms;        /* offset 16 */

    /* Watchdog core writes its own counter + uptime here so the
     * main core can verify "the watchdog is alive too" before
     * declaring itself healthy for OTA rollback purposes. */
    uint32_t wd_counter;            /* offset 20 */
    uint32_t wd_uptime_ms;          /* offset 24 */

    /* Watchdog records the last reason it almost-or-did expire.
     * Cleared to 0 on warm reset. Useful for post-mortem if the
     * board reboots itself unexpectedly. */
    uint32_t wd_last_reason;        /* offset 28 */

    /* Padding to 64 B. Reserved for future fields; don't repurpose
     * without bumping a protocol version. */
    uint32_t _reserved[8];          /* offset 32..63 */
} LpWatchdogShm;

/* Compile-time guarantee against accidental layout drift. If this
 * fails, someone added a field without removing matching padding. */
#ifdef __STDC_VERSION__
#if __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(LpWatchdogShm) == 64,
               "LpWatchdogShm must be exactly 64 B (one cache line)");
#endif
#endif

/* Reasons the watchdog can record in `wd_last_reason`. Most won't be
 * observable post-reset (the warm reset wipes MSRAM contents on
 * AM243x), but a logged copy lives in the watchdog's local TCM and
 * is dumped to UART2 on reset detection. */
#define LP_WD_REASON_NONE              0u
#define LP_WD_REASON_COUNTER_FROZEN    1u  /* counter unchanged > 5 s */
#define LP_WD_REASON_ALIVE_BITS_DROPPED 2u /* required mask not met > 8 s */
#define LP_WD_REASON_MAIN_UPTIME_STALL 3u  /* main_uptime_ms not advancing */
#define LP_WD_REASON_MAGIC_LOST        4u  /* magic was set, then cleared */

#ifdef __cplusplus
}
#endif

#endif /* LP_WATCHDOG_IPC_H */
