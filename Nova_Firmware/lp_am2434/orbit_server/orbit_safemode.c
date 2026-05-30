/*
 * orbit_safemode.c — Comm-loss watchdog & STORAGE safe-mode enforcement.
 *
 * Concurrency: this task always operates while holding OrbitState_Lock.
 * The Modbus TCP handlers do the same, so a 1-Hz tick interleaves
 * cleanly with bursts of register reads/writes.
 */

#include "orbit_safemode.h"
#include "orbit_state.h"
#include "orbit_role.h"
#include "orbit_gdc.h"
#include "orbit_triton.h"

#include <FreeRTOS.h>
#include <task.h>
#include <kernel/dpl/DebugP.h>

/* STORAGE safe-mode policy — see orbit-simulator enforceSafeMode():
 *   - DO[3]   Climacell        OFF
 *   - DO[4]   Humidifier head  OFF
 *   - DO[5]   Humidifier pump  OFF
 *   - DO[8]   Door close       OFF
 *   - DO[9]   Door open        OFF
 *   - DO[1]   Yellow light     ON  (visible safe-mode indicator)
 *   - DO[2]   Fan              UNCHANGED (ventilation safety)
 *   - DC24V[*]                 OFF
 *   - AO[*]                    0
 */
static void enforce_storage(OrbitStateData *s)
{
    s->do_[3] = false;
    s->do_[4] = false;
    s->do_[5] = false;
    s->do_[8] = false;
    s->do_[9] = false;
    s->do_[1] = true;
    for (int i = 0; i < ORBIT_DC24V_COUNT; i++) s->dc24v[i] = false;
    s->ao_pct[0] = 0;
    s->ao_pct[1] = 0;
}

static void enforce_role(OrbitRole role, OrbitStateData *s)
{
    switch (role) {
        case ORBIT_ROLE_STORAGE: enforce_storage(s); break;
        case ORBIT_ROLE_GDC:     (void)s; OrbitGdc_EnforceSafeMode();    break;
        case ORBIT_ROLE_TRITON:  (void)s; OrbitTriton_EnforceSafeMode(); break;
        default: break;
    }
}

void orbit_safemode_task(void *args)
{
    (void)args;
    const OrbitRole role = OrbitRole_Get();
    const TickType_t period = pdMS_TO_TICKS(1000U / ORBIT_WATCHDOG_HZ);

    DebugP_log("[SAFEMODE] watchdog starting (role=%s, timeout=%u ms)\r\n",
               OrbitRole_Name(role), (unsigned)ORBIT_COMM_LOSS_MS);

    for (;;) {
        vTaskDelay(period);

        OrbitState_Lock();
        OrbitStateData *s = OrbitState_Get();

        /* Bump uptime regardless of comm state. */
        s->uptime_sec += 1;

        TickType_t now = xTaskGetTickCount();
        TickType_t elapsed_ticks = now - s->last_poll_ticks;
        uint32_t   elapsed_ms    = (uint32_t)(elapsed_ticks * portTICK_PERIOD_MS);

        bool was_lost = s->comm_lost;
        s->comm_lost  = (elapsed_ms > ORBIT_COMM_LOSS_MS);

        if (s->comm_lost) {
            if (!was_lost) {
                DebugP_log("[SAFEMODE] comm lost (no poll for %u ms) — entering safe mode\r\n",
                           (unsigned)elapsed_ms);
                s->safe_mode = true;
            }
            enforce_role(role, s);
        } else if (was_lost) {
            DebugP_log("[SAFEMODE] comm restored — clearing safe mode\r\n");
            s->safe_mode = false;
            /* Do NOT clobber outputs — the master is back in charge. */
        }

        OrbitState_Unlock();
    }
}
