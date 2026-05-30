# LP-AM2434 — Independent Watchdog Core (R5FSS1_0)

> **Status:** in design / scaffold (May 2026). Not yet built or flashed.
>
> **Goal:** turn the second R5F cluster into a dedicated, hardware-
> independent watchdog. If the main core hangs (kernel deadlock, BSS
> corruption, runaway loop, infinite IRQ storm) the watchdog core stops
> seeing fresh heartbeats, lets its DWWD expire, and the SoC resets.
> Combined with the future stage-2 SBL chooser (F2c), this gives
> unattended A/B rollback without a customer power-cycling the panel.

---

## 1. Why a dedicated core (not a same-core strober)

| Failure mode | Same-core software watchdog | Dedicated core + DWWD |
|---|---|---|
| FreeRTOS scheduler hang | DWWD task never runs → reset ✅ | Same outcome ✅ |
| FreeRTOS scheduler runs but high-prio task spins | Strobe task starved → reset ✅ | Same outcome ✅ |
| BSS corruption flips strobe-gate flag to "always strobe" | Reset never fires ❌ | Watchdog sees stale heartbeat → reset ✅ |
| Stuck IRQ at higher priority than strobe task | Strobe task never runs → reset ✅ | Same outcome ✅ |
| Runaway loop *inside* the strobe task itself | Reset never fires ❌ | Heartbeat timestamp stops advancing → reset ✅ |
| Cluster 0 power glitch / cache parity error | May hang strobe ❌ | Cluster 1 unaffected → reset ✅ |
| Watchdog core itself hangs | n/a | Heartbeat unread → main keeps running unmonitored ❌ (mitigated by DWWD on watchdog core too) |

The cluster-1 placement was chosen specifically because R5FSS1 has its
own clock domain, its own DWWD peripheral, its own TCM, and is reset
independently from R5FSS0. There is no shared resource the main core
can scribble on to disable the watchdog short of explicit IPC writes
to the heartbeat struct (which the watchdog will detect as "frozen
counter" once the main core stops incrementing it).

---

## 2. Heartbeat protocol — shared memory polled

```c
/* lp_watchdog_ipc.h — included by both cores */
typedef struct __attribute__((packed)) {
    /* Magic — watchdog core refuses to strobe until magic == 0x57444748
     * ("WDGH"). Main core writes magic last during boot init so we can't
     * see a half-initialised struct. */
    uint32_t magic;

    /* Monotonic counter — main core increments every heartbeat tick.
     * Watchdog core compares against last-seen value; equal across
     * `stale_threshold_ms` window → DWWD allowed to expire. */
    uint32_t counter;

    /* Liveness bitmap — set by main core's heartbeat task. Watchdog
     * core only allows strobe when ALL required bits are set:
     *   bit 0  ALIVE_MODBUS         — Modbus listener accept loop OK
     *   bit 1  ALIVE_SYSTEMSTATUS   — emitted in last 2 s
     *   bit 2  ALIVE_LWIP_LINK      — netif flags & NETIF_FLAG_LINK_UP
     *   bit 3  ALIVE_ENGINE_TICK    — lp_engine_tick ran in last 5 s
     *   bit 4  ALIVE_OSPI           — OSPI driver responsive
     *   ... future bits as new subsystems join
     */
    uint32_t alive_bits;

    /* Required-alive mask — main core writes this once at boot. Watchdog
     * AND-compares (alive_bits & required_mask) == required_mask. Lets
     * us add/remove subsystems without rebuilding the watchdog core. */
    uint32_t required_mask;

    /* Free-running uptime in milliseconds (main core's xTaskGetTickCount
     * × portTICK_PERIOD_MS). Logged on reset for post-mortem. */
    uint32_t main_uptime_ms;

    /* Watchdog core writes its own heartbeat here so main core's
     * `FwUpdateHealthy_Mark` can verify the watchdog is alive too
     * (otherwise we'd have a silent watchdog with nothing watching the
     * watcher). */
    uint32_t wd_counter;
    uint32_t wd_uptime_ms;

    /* Reserved for future. Keep struct multiple of cache-line size. */
    uint32_t _pad[9];
} LpWatchdogShm;
_Static_assert(sizeof(LpWatchdogShm) == 64, "shm must be 64B (one cache line)");
```

**Why polled, not IpcNotify:**

- Zero IPC subsystem dependency on the watchdog core — it can boot,
  configure DWWD, and start polling in <100 ms with only the bare-
  metal `R5F_addressTranslate` for cluster-1's MSRAM view.
- Watchdog can never get stuck waiting for an IRQ that never fires.
- ~50 ns per read; budget is irrelevant since watchdog polls at 100 Hz.
- Debuggable: pause the watchdog core in CCS, look at MSRAM through
  the JTAG memory window, see exactly what main core last wrote.

**Cache discipline:** main core flushes the heartbeat cacheline after
each write (`CacheP_wb(&shm, sizeof(shm), CacheP_TYPE_ALL)`); watchdog
core invalidates before each read (`CacheP_inv`). Struct is 64 bytes
aligned to a cache line so the flush doesn't touch unrelated data.

---

## 3. Memory layout

AM243x MSRAM is 2 MB at `0x70000000–0x701FFFFF`:

| Region | Owner | Range | Size |
|---|---|---|---|
| SBL | TI ROM | `0x70000000–0x7007FFFF` | 512 KB |
| Main core code/data | R5FSS0_0 | `0x70080000–0x701DFFBF` | ~1.4 MB |
| **Heartbeat shared** | both | `0x701DFFC0–0x701DFFFF` | **64 B** |
| Watchdog core code/data | R5FSS1_0 | `0x701E0000–0x701FFFFF` | 128 KB |

The watchdog core runs from MSRAM (not its TCMA) so it has room for
DWWD config + the polling loop + a small UART debug tap. Cluster-1
TCMA (32 KB) is reserved for the vector table and ISR stack.

The 64 B at `0x701DFFC0` is carved out of the main core's MSRAM
section in the syscfg (region size shrinks from `0x160000` to
`0x15FFC0`). Main core sees it as a fixed-address pointer, not a
linker-allocated variable.

---

## 4. Watchdog core boot sequence

1. SBL loads watchdog `.rprc` to MSRAM `0x701E0000`, releases
   R5FSS1_0 from reset, jumps to entry point.
2. Watchdog core sets up its own MPU (one region: MSRAM RW + cacheable).
3. Configures DWWD on cluster 1: 10 s open window, 1 s closed window,
   reaction = warm reset.
4. Spins reading the shared heartbeat. Until `magic == 0x57444748` it
   strobes DWWD freely (main hasn't booted yet — boot-time grace).
5. Once magic appears: switches to "strict" mode — only strobes when
   all of:
   - `(alive_bits & required_mask) == required_mask`
   - `counter` advanced at least once in the last 5 s
   - `main_uptime_ms` strictly increasing
6. If strict mode fails for `> 8 s`, stop strobing → DWWD expires →
   warm reset.

**Boot-time grace (step 4):** main core takes ~3-5 s to bring up
Ethernet + Modbus listener + emit first SystemStatus. We can't
require strict mode immediately. Grace ends the moment main core
writes the magic — this happens *after* main has cleared its own
"alive" criteria, so there's no window where strict mode is on but
main isn't ready.

---

## 5. "Healthy" milestone — for OTA rollback (F2c integration)

`FwUpdateHealthy_Mark()` is called by main core's heartbeat task once
the following conditions all hold for **30 consecutive seconds**:

| Bit | Source | Why this matters for "the new image works" |
|---|---|---|
| ALIVE_MODBUS | accept-loop heartbeat in `orbit_client.c` | bridge can reach orbits |
| ALIVE_SYSTEMSTATUS | tick counter in `main.c::data_exchange` | bridge sees the firmware |
| ALIVE_LWIP_LINK | `netif->flags & NETIF_FLAG_LINK_UP` | physical Ethernet OK |
| ALIVE_ENGINE_TICK | `lp_engine_tick` last-ran timestamp | the actual control logic is alive |
| ALIVE_WD_PEER | `wd_counter` advanced in last 2 s | watchdog itself is alive (no point being healthy if nobody's watching) |

At 30 s of all-green, `FwBootMeta.watchdog_strikes` is cleared to 0
in OSPI. Until then, on the next warm reset the stage-2 SBL chooser
(F2c, future) will see `strikes >= 1` and after three resets revert
to the previous bank.

`FwUpdateHealthy_Mark` is idempotent — it only writes OSPI when
`strikes != 0`. No flash wear on healthy boots.

---

## 6. Build / flash impact

| Today | After watchdog lands |
|---|---|
| `gmake.exe` builds one `.out`, wraps to `.mcelf.hs_fs` | Builds two `.out` files (main + watchdog), bundles via `--core-img=4:main.out --core-img=6:wd.out`, single `.mcelf.hs_fs` output |
| `Flash-LP.ps1` flashes one image | No script change — UniFlash deploys all cores from one mcelf |
| `Set-Probe.ps1` | No change |
| Boot trace shows one core | Boot trace shows main + watchdog `[WD] ` lines on UART2 (debug tap) |

**Recovery path if watchdog code is broken:** UniFlash with the
previous good `.mcelf.hs_fs` (which had no watchdog core) — the
SBL just doesn't load any image to R5FSS1, that cluster stays in
reset, no DWWD configured, system runs as before.

---

## 7. Failure modes the watchdog deliberately does NOT catch

- **Wrong-but-running firmware** (e.g. PID coefficients corrupt but
  loop is alive). This is observability/alerting territory, not
  watchdog territory.
- **Single-equipment failure** (refrig contactor stuck closed). The
  alarm subsystem owns this.
- **Power-sequencing failures during OTA bank switch**. The custom
  board's brownout detector + hold-up cap (F4) owns this.

The watchdog's job is "the firmware as a whole is alive and serving
its primary purposes". It's the second-to-last line of defense
(last = stage-2 fallback to previous bank).

---

## 8. Where this lives in the foundation plan

This is **F3** in `/memories/repo/foundation-plan.md`. It
unblocks the "healthy milestone" definition that F2c (custom
stage-2 SBL chooser) depends on. F3 ships before F2c because:

- F3 is testable today on the bench — pull Ethernet, watchdog should
  reset; let firmware run normally, watchdog should never reset.
- F2c needs the custom board (F4) to test its bank-switching path
  fully, and depends on F3 for the "is the new image working?" flag.

---

## 9. Doc-trail

- Architecture rationale + protocol: this file.
- Build mechanics + flash recipe: appended to
  [`docs/LP-AM2434-Hardware-Bringup-Plan.md`](LP-AM2434-Hardware-Bringup-Plan.md)
  once step 6 lands.
- Bench verification recipe: lands in
  [`/memories/repo/lp-watchdog-bringup.md`](/memories/repo/lp-watchdog-bringup.md)
  as step 8.
- Foundation index: [`/memories/repo/foundation-plan.md`](/memories/repo/foundation-plan.md)
  F3 row updated when work completes.
