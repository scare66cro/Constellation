/*
 * orbit_safemode.h — Communication-loss watchdog & safe-mode enforcement
 *
 * Mirrors orbit-simulator/src/orbitSimulator.ts:1770-2010.
 *
 * The watchdog runs as a 1-Hz FreeRTOS task. Each tick:
 *   1. Read OrbitState.last_poll_ticks and compute elapsed.
 *   2. If elapsed > ORBIT_COMM_LOSS_MS, set comm_lost = safe_mode = true
 *      and call the role-appropriate enforce_*() helper.
 *   3. While comm_lost stays true, keep enforcing every tick (the
 *      sim does the same, so any stray Modbus write that lands during
 *      comm-loss is overwritten back to the safe value within ≤1 s).
 *   4. On the comm_lost true→false edge (master came back), clear
 *      safe_mode but DO NOT touch outputs — the master is now in
 *      charge again and will issue its desired DO/AO state.
 *
 * Phase 1 covers STORAGE only. GDC and TRITON enforcement land
 * alongside their respective register-map modules.
 *
 * Also bumps `OrbitState.uptime_sec` once per second so HR 40005/40006
 * (uptime LSW/MSW) reflect real wall time without requiring a separate
 * timer task.
 */
#ifndef ORBIT_SAFEMODE_H
#define ORBIT_SAFEMODE_H

#define ORBIT_COMM_LOSS_MS  5000U   /* matches sim's 5000 ms threshold */
#define ORBIT_WATCHDOG_HZ   1U

void orbit_safemode_task(void *args);

#endif /* ORBIT_SAFEMODE_H */
