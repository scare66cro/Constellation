/* driverlib/pwm.h — TivaWare PWM shim for AM2434 */
#ifndef DRIVERLIB_PWM_H_SHIM
#define DRIVERLIB_PWM_H_SHIM

#include <stdint.h>

/* PWM base */
#define PWM0_BASE               0
#define PWM1_BASE               0

/* Generator defines */
#define PWM_GEN_0               0x00000040
#define PWM_GEN_1               0x00000080
#define PWM_GEN_2               0x000000C0
#define PWM_GEN_3               0x00000100

/* Output bits */
#define PWM_OUT_0               0x00000001
#define PWM_OUT_1               0x00000002
#define PWM_OUT_2               0x00000004
#define PWM_OUT_3               0x00000008
#define PWM_OUT_4               0x00000010
#define PWM_OUT_5               0x00000020
#define PWM_OUT_6               0x00000040
#define PWM_OUT_7               0x00000080
#define PWM_OUT_0_BIT           0x00000001
#define PWM_OUT_1_BIT           0x00000002
#define PWM_OUT_2_BIT           0x00000004
#define PWM_OUT_3_BIT           0x00000008
#define PWM_OUT_4_BIT           0x00000010
#define PWM_OUT_5_BIT           0x00000020
#define PWM_OUT_6_BIT           0x00000040
#define PWM_OUT_7_BIT           0x00000080

/* Config */
#define PWM_GEN_MODE_DOWN       0x00000000
#define PWM_GEN_MODE_UP_DOWN    0x00000002
#define PWM_GEN_MODE_NO_SYNC    0x00000000
#define PWM_GEN_MODE_SYNC       0x00000038
#define PWM_GEN_MODE_GEN_NO_SYNC  0x00000000
#define PWM_GEN_MODE_GEN_SYNC_LOCAL 0x00000038
#define PWM_GEN_MODE_GEN_SYNC_GLOBAL 0x00000078

#define PWM_SYSCLK_DIV_1        0x00000000
#define PWM_SYSCLK_DIV_2        0x00000001
#define PWM_SYSCLK_DIV_4        0x00000002
#define PWM_SYSCLK_DIV_8        0x00000003
#define PWM_SYSCLK_DIV_16       0x00000004
#define PWM_SYSCLK_DIV_32       0x00000005
#define PWM_SYSCLK_DIV_64       0x00000006

/* ─── Forwarders to hal_pwm.c (AM2434 EPWM driver) ────────────────────
 * Full implementation and behavioural contract: Nova_Firmware/Platform/hal_pwm.c
 * Preserves TM4C timing: 10 kHz carrier, count-down, legacy 3.75 MHz tick units. */
extern void hal_pwm_clock_set(uint32_t tm4c_div);
extern void hal_pwm_gen_configure(uint32_t gen, uint32_t config);
extern void hal_pwm_gen_period_set(uint32_t gen, uint32_t legacy_ticks);
extern void hal_pwm_gen_enable(uint32_t gen, int enable);
extern void hal_pwm_output_state(uint32_t out_mask, int enable);
extern void hal_pwm_pulse_width_set(uint32_t out, uint32_t legacy_ticks);

static inline void PWMClockSet(uint32_t b, uint32_t c)                 { (void)b; hal_pwm_clock_set(c); }
static inline void PWMGenConfigure(uint32_t b, uint32_t g, uint32_t c) { (void)b; hal_pwm_gen_configure(g, c); }
static inline void PWMGenPeriodSet(uint32_t b, uint32_t g, uint32_t p) { (void)b; hal_pwm_gen_period_set(g, p); }
static inline uint32_t PWMGenPeriodGet(uint32_t b, uint32_t g)         { (void)b; (void)g; return 0; }
static inline void PWMPulseWidthSet(uint32_t b, uint32_t o, uint32_t w){ (void)b; hal_pwm_pulse_width_set(o, w); }
static inline uint32_t PWMPulseWidthGet(uint32_t b, uint32_t o)        { (void)b; (void)o; return 0; }
static inline void PWMOutputState(uint32_t b, uint32_t m, int e)       { (void)b; hal_pwm_output_state(m, e); }
static inline void PWMGenEnable(uint32_t b, uint32_t g)                { (void)b; hal_pwm_gen_enable(g, 1); }
static inline void PWMGenDisable(uint32_t b, uint32_t g)               { (void)b; hal_pwm_gen_enable(g, 0); }

#endif
