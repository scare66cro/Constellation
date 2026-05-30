/* nova_pwm.c
 *
 * Nova-native implementation of legacy Application/PWM.c.
 *
 * MIGRATION STATUS (Phase 5 of legacy → Nova-native):
 *   Replaces docs/legacy_AS2_reference/Application/PWM.c.  PWM.h is
 *   unchanged so all callers (Controls, States, nova_failures, the
 *   nova_thread_overrides PWM mirror, etc.) link without source
 *   changes.  Owns the PwmChannel[] global.
 *
 * Differences vs legacy (TM4C → AM2434):
 *   • No TivaWare driverlib calls.  All hardware access goes through
 *     hal_pwm_* directly.
 *   • PWM_Init is a thin wrapper over hal_pwm_init() (already called
 *     once in Platform/main.c during boot).  We tolerate a second call
 *     here because legacy code unconditionally invokes PWM_Init from
 *     ThreadUIUpdate — hal_pwm_init() is idempotent.
 *
 * Behaviour preserved bit-for-bit:
 *   • PwmChannel[i].Output seeded to PWM_MIN_VALUE.
 *   • Channel-index remap PWM_OUT_3↔4 and PWM_OUT_5↔6 (legacy comment:
 *     "outputs 3 & 4, and 5 & 6 are reversed to make the physical
 *     orientation on the expansion boards the same as the main/mini
 *     board").
 *   • Inverted duty-cycle write `PWM_PERIOD - PwmChannel[].Output`
 *     (compensates the opto-isolator inversion downstream).
 *   • SysConfig warning raised via WarningsSet(WARN_SYSCONFIG_EQ,
 *     FM_ALARM, NA, …) when an enabled PWM channel has an undefined
 *     or out-of-range channel index.
 */

#include <stdint.h>
#include <stdbool.h>

#include "hal.h"
#include "PWM.h"
#include "Settings.h"
#include "States.h"
#include "Warnings.h"

/* ─── Public global (declared extern in PWM.h) ────────────────────── */
PWM_INFO PwmChannel[PWM_TOTAL_EQ];

/* ─── Channel remap table (legacy comment quoted in header) ───────── */
static const uint32_t kPwmOutput[6] = {
    PWM_OUT_1, PWM_OUT_2, PWM_OUT_4, PWM_OUT_3, PWM_OUT_6, PWM_OUT_5
};

/* ─────────────────────────────────────────────────────────────────── */

void PWM_Init(void)
{
    /* Bring up the EPWM peripheral.  hal_pwm_init() is idempotent and
     * has already run once in Platform/main.c — calling it again here
     * matches legacy semantics where PWM_Init was the only entry. */
    hal_pwm_init();

    /* Match legacy: divider /32 (3.75 MHz tick), 10 kHz period, all 4
     * generators down-mode, all 6 outputs enabled.  hal_pwm_init() does
     * the heavy lifting; these calls are kept for parity in case a
     * future HAL rev separates the two responsibilities. */
    hal_pwm_clock_set(0 /* PWM_SYSCLK_DIV_32 */);
    hal_pwm_gen_configure(0, 0);
    hal_pwm_gen_configure(1, 0);
    hal_pwm_gen_configure(2, 0);
    hal_pwm_gen_configure(3, 0);
    hal_pwm_gen_period_set(0, PWM_PERIOD);
    hal_pwm_gen_period_set(1, PWM_PERIOD);
    hal_pwm_gen_period_set(2, PWM_PERIOD);
    hal_pwm_gen_period_set(3, PWM_PERIOD);
    hal_pwm_gen_enable(0, 1);
    hal_pwm_gen_enable(1, 1);
    hal_pwm_gen_enable(2, 1);
    hal_pwm_gen_enable(3, 1);
    hal_pwm_output_state(PWM_OUT_1_BIT | PWM_OUT_2_BIT | PWM_OUT_3_BIT |
                         PWM_OUT_4_BIT | PWM_OUT_5_BIT | PWM_OUT_6_BIT, 1);

    hal_pwm_pulse_width_set(0, PWM_MIN_VALUE);
    hal_pwm_pulse_width_set(1, PWM_MIN_VALUE);
    hal_pwm_pulse_width_set(2, PWM_MIN_VALUE);
    hal_pwm_pulse_width_set(3, PWM_MIN_VALUE);
    hal_pwm_pulse_width_set(4, PWM_MIN_VALUE);
    hal_pwm_pulse_width_set(5, PWM_MIN_VALUE);

    int i;
    for (i = 0; i < PWM_TOTAL_EQ; i++) {
        PwmChannel[i].Output = PWM_MIN_VALUE;
    }
}

void PWM_UpdateChannel(PWM_EQUIPMENT eqIndex)
{
    if (!Settings.PWM[eqIndex].Enabled) return;

    if (Settings.PWM[eqIndex].Channel == PWM_UNDEFINED
        || Settings.PWM[eqIndex].Channel > 5) {
        WarningsSet(WARN_SYSCONFIG_EQ, FM_ALARM, NA,
                    Settings.PWM[eqIndex].SysConfigWarnIoIndex);
        return;
    }

    /* Inverted because the opto-isolators invert the signal downstream. */
    hal_pwm_pulse_width_set(kPwmOutput[Settings.PWM[eqIndex].Channel],
                            PWM_PERIOD - PwmChannel[eqIndex].Output);
}

/*** End of file ***/
