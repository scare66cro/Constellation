/*
 * hal_pwm.c — PWM HAL for Constellation Nova (TI AM2434 Cortex-R5F)
 *
 * ─── Behavioral contract (must match legacy TM4C firmware exactly) ─────
 *
 * Legacy firmware (Mini_IO/Application/PWM.c):
 *   System clock: 120 MHz
 *   PWM clock   : /32  →  3.75 MHz  (PWMClockSet PWM_SYSCLK_DIV_32)
 *   PWM_PERIOD  : 375 ticks         →  carrier = 3.75 MHz / 375 = 10.000 kHz
 *   Mode        : count-down, local shadow sync (PWM_GEN_MODE_DOWN)
 *   Pulse width : PWM_PERIOD - PwmChannel[i].Output   (polarity inverted
 *                 in software because the opto-isolators invert the signal
 *                 on the way to the equipment)
 *   Four generators (GEN_0..GEN_3) all programmed to the same 10 kHz period,
 *   driving six outputs PWM_OUT_1..PWM_OUT_6 → fan, refrigeration, doors,
 *   burner / cooling.
 *
 * This HAL preserves those semantics byte-for-byte:
 *   - hal_pwm_gen_period_set() accepts LEGACY TICKS (3.75 MHz base).
 *     Internally it rescales to the AM2434 EPWM TBCLK so the waveform
 *     the opto-isolator sees is identical to TM4C.
 *   - hal_pwm_pulse_width_set() accepts LEGACY TICKS.  The existing
 *     "PWM_PERIOD - Output" inversion in legacy PWM_UpdateChannel() is
 *     preserved unchanged; the EPWM action qualifier is configured to
 *     replicate TM4C down-count behaviour (HIGH at period reload, LOW on
 *     CMPA-down), so the pin-level waveform is bit-compatible.
 *
 * ─── AM2434 EPWM mapping (matches pinmux in pinout.c) ──────────────────
 *
 *   Legacy PWM_GEN_0  →  EPWM0   (TM4C PWM_OUT_0/1)
 *   Legacy PWM_GEN_1  →  EPWM1   (TM4C PWM_OUT_2/3)
 *   Legacy PWM_GEN_2  →  EPWM2   (TM4C PWM_OUT_4/5)
 *   Legacy PWM_GEN_3  →  EPWM3   (TM4C PWM_OUT_6/7)
 *
 *   Legacy PWM_OUT_x bits → EPWM CMPA/CMPB (A = even, B = odd):
 *     OUT_0 → EPWM0.A    OUT_1 → EPWM0.B
 *     OUT_2 → EPWM1.A    OUT_3 → EPWM1.B
 *     OUT_4 → EPWM2.A    OUT_5 → EPWM2.B
 *     OUT_6 → EPWM3.A    OUT_7 → EPWM3.B
 *
 * ─── EPWM clock tree (AM2434) ─────────────────────────────────────────
 *
 * EPWM source clock is SYSCLK0 / EPWM_CLKDIV (set in CTRL_MMR).  The Nova
 * board BSP programs it to 200 MHz.  Within each EPWM, TBCLK is further
 * divided by HSPCLKDIV × CLKDIV.  We select HSPCLKDIV=/2, CLKDIV=/1 →
 * TBCLK = 100 MHz.
 *
 *   TBPRD(10 kHz) = 100 MHz / 10 kHz = 10000  (fits comfortably in 16 bits)
 *
 * Conversion from LEGACY ticks → EPWM TBCLK ticks:
 *   ratio = TBCLK_HZ / LEGACY_PWM_TICK_HZ = 100 MHz / 3.75 MHz = 26.666…
 *   We use 64-bit scaling to stay exact to the nearest ns:
 *     tbprd = legacy * TBCLK_HZ / LEGACY_PWM_TICK_HZ
 *   Duty ratio is then preserved to 16-bit EPWM resolution, which gives
 *   ~27× the duty resolution of the TM4C (375 steps → 10000 steps).
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "hal.h"
#include <stdint.h>

#include "driverlib/pwm.h"   /* PWM_GEN_*, PWM_OUT_*, PWM_SYSCLK_DIV_* */

/* Legacy TM4C mapping: PWM_OUT_x index → generator */
#define NUM_EPWM_UNITS          4U
#define EPWM_OUTPUTS_PER_UNIT   2U   /* A and B */
#define NUM_LEGACY_OUTPUTS      (NUM_EPWM_UNITS * EPWM_OUTPUTS_PER_UNIT)

/* ─── Cached state (period/width) kept in LEGACY ticks so that if the
 *    legacy code changes the period mid-run, we rescale CMP correctly. */
static uint32_t s_legacy_period[NUM_EPWM_UNITS] = {
    LEGACY_PWM_PERIOD, LEGACY_PWM_PERIOD, LEGACY_PWM_PERIOD, LEGACY_PWM_PERIOD
};
static uint32_t s_legacy_width[NUM_LEGACY_OUTPUTS];
static uint8_t  s_output_enabled[NUM_LEGACY_OUTPUTS];
static uint8_t  s_gen_enabled[NUM_EPWM_UNITS];

#ifndef QEMU_BUILD
/* ─── AM2434 EPWM register map (TRM §15.3, EPWM type) ──────────────────
 * EPWM instances in CONTROLSS: base 0x23000000, stride 0x00010000.
 * All time-base and action registers are 16-bit. */
#define EPWM0_BASE              0x23000000U
#define EPWM_STRIDE             0x00010000U

#define EPWM_REG16(base, off)   (*(volatile uint16_t *)((base) + (off)))

/* Register byte offsets */
#define EPWM_O_TBCTL            0x0000U
#define EPWM_O_TBSTS            0x0002U
#define EPWM_O_TBCTR            0x0008U
#define EPWM_O_TBPRD            0x000AU
#define EPWM_O_CMPCTL           0x000EU
#define EPWM_O_CMPA             0x0012U
#define EPWM_O_CMPB             0x0014U
#define EPWM_O_AQCTLA           0x0016U
#define EPWM_O_AQCTLB           0x0018U

/* TBCTL bit fields */
#define TBCTL_CTRMODE_DOWN      (0x1U << 0)   /* 01 = count-down (TM4C PWM_GEN_MODE_DOWN) */
#define TBCTL_PHSEN_DISABLE     (0x0U << 2)
#define TBCTL_PRDLD_SHADOW      (0x0U << 3)   /* shadow load on CTR=0 (TM4C _SYNC_LOCAL) */
#define TBCTL_SYNCOSEL_DISABLE  (0x3U << 4)
#define TBCTL_HSPCLKDIV_2       (0x1U << 7)   /* /2  */
#define TBCTL_CLKDIV_1          (0x0U << 10)  /* /1  → TBCLK = SYSCLK/2 = 100 MHz */
#define TBCTL_FREE_RUN          (0x3U << 14)  /* free run in debug */

#define TBCTL_INIT              (TBCTL_CTRMODE_DOWN    | \
                                 TBCTL_PHSEN_DISABLE   | \
                                 TBCTL_PRDLD_SHADOW    | \
                                 TBCTL_SYNCOSEL_DISABLE| \
                                 TBCTL_HSPCLKDIV_2     | \
                                 TBCTL_CLKDIV_1        | \
                                 TBCTL_FREE_RUN)

/* CMPCTL: shadow CMPA/B, load at CTR=0 (matches TM4C SYNC_LOCAL) */
#define CMPCTL_INIT             0x0000U

/* Action Qualifier (AQCTLA/B): replicate TM4C down-count default
 *   ZRO   = set HIGH  (10b)   counter reached 0 / period reload
 *   CAD   = clear LOW (01b)   CMPA match on down-count
 * Other events: no change. */
#define AQCTL_ZRO_SET           (0x2U << 0)
#define AQCTL_CAD_CLR           (0x1U << 6)
#define AQCTL_INIT              (AQCTL_ZRO_SET | AQCTL_CAD_CLR)

/* TBCLK after HSPCLKDIV(/2) × CLKDIV(/1) from 200 MHz source */
#define TBCLK_HZ                100000000U
#endif /* !QEMU_BUILD */

/* ─── Helpers ──────────────────────────────────────────────────────────── */

/* Decode TM4C PWM_GEN_x value to unit index 0..3.
 * TM4C pwm.h encodes PWM_GEN_0 = 0x40, PWM_GEN_1 = 0x80, _2 = 0xC0, _3 = 0x100.
 * Converting to index 0..3: (gen >> 6) - 1. */
static inline uint32_t gen_to_unit(uint32_t gen)
{
    uint32_t idx = (gen >> 6);
    return (idx == 0U) ? 0U : (idx - 1U);
}

/* Decode TM4C PWM_OUT_x to (unit, is_B).  PWM_OUT_n bit value = 1<<n.
 * OUT_0→gen0.A, OUT_1→gen0.B, OUT_2→gen1.A, etc. */
static inline uint32_t out_to_index(uint32_t out_bit)
{
    /* out_bit is 1<<n; find n */
    uint32_t n = 0;
    uint32_t v = out_bit;
    while (v > 1U && n < NUM_LEGACY_OUTPUTS) { v >>= 1; n++; }
    return n;
}

#ifndef QEMU_BUILD
static inline uint32_t legacy_to_tbclk(uint32_t legacy_ticks)
{
    /* 64-bit to avoid overflow; exact rounding-to-nearest */
    uint64_t num = (uint64_t)legacy_ticks * (uint64_t)TBCLK_HZ + (LEGACY_PWM_TICK_HZ / 2U);
    uint32_t t = (uint32_t)(num / LEGACY_PWM_TICK_HZ);
    if (t > 0xFFFFU) t = 0xFFFFU;
    return t;
}

static inline uint32_t epwm_base_for_unit(uint32_t unit)
{
    return EPWM0_BASE + unit * EPWM_STRIDE;
}
#endif

/* ─── Public API ───────────────────────────────────────────────────────── */

void hal_pwm_init(void)
{
#ifndef QEMU_BUILD
    for (uint32_t u = 0; u < NUM_EPWM_UNITS; u++) {
        uint32_t b = epwm_base_for_unit(u);
        EPWM_REG16(b, EPWM_O_TBCTL)  = TBCTL_INIT;
        EPWM_REG16(b, EPWM_O_CMPCTL) = CMPCTL_INIT;
        EPWM_REG16(b, EPWM_O_TBCTR)  = 0;
        /* Default: 10 kHz carrier (matches legacy PWM_PERIOD=375) */
        EPWM_REG16(b, EPWM_O_TBPRD)  = (uint16_t)legacy_to_tbclk(LEGACY_PWM_PERIOD);
        /* Start with full period in CMP → output low throughout (safe: duty 0
         * on the post-opto equipment side, because legacy inverts in SW). */
        EPWM_REG16(b, EPWM_O_CMPA)   = (uint16_t)legacy_to_tbclk(LEGACY_PWM_PERIOD);
        EPWM_REG16(b, EPWM_O_CMPB)   = (uint16_t)legacy_to_tbclk(LEGACY_PWM_PERIOD);
        EPWM_REG16(b, EPWM_O_AQCTLA) = AQCTL_INIT;
        EPWM_REG16(b, EPWM_O_AQCTLB) = AQCTL_INIT;
    }
#endif
    for (uint32_t u = 0; u < NUM_EPWM_UNITS; u++) s_legacy_period[u] = LEGACY_PWM_PERIOD;
    for (uint32_t i = 0; i < NUM_LEGACY_OUTPUTS; i++) {
        s_legacy_width[i]   = LEGACY_PWM_PERIOD;  /* inverted → equipment duty 0 */
        s_output_enabled[i] = 0;
    }
    for (uint32_t u = 0; u < NUM_EPWM_UNITS; u++) s_gen_enabled[u] = 0;
}

void hal_pwm_clock_set(uint32_t tm4c_div)
{
    /* The AM2434 EPWM clock is set once in hal_pwm_init() via TBCTL.
     * Legacy code only ever passes PWM_SYSCLK_DIV_32 here to get 3.75 MHz.
     * We honour the LEGACY_PWM_TICK_HZ semantic unconditionally; any
     * reprogramming of the TM4C prescaler is a no-op on AM2434. */
    (void)tm4c_div;
}

void hal_pwm_gen_configure(uint32_t gen, uint32_t config)
{
    /* Legacy always passes MODE_DOWN | NO_SYNC | GEN_SYNC_LOCAL.
     * hal_pwm_init() has already programmed this mode.  Anything else is
     * unsupported on AM2434 and would require a pinmux change anyway. */
    (void)gen; (void)config;
}

void hal_pwm_gen_period_set(uint32_t gen, uint32_t legacy_ticks)
{
    uint32_t u = gen_to_unit(gen);
    if (u >= NUM_EPWM_UNITS || legacy_ticks == 0U) return;
    s_legacy_period[u] = legacy_ticks;
#ifndef QEMU_BUILD
    uint32_t b = epwm_base_for_unit(u);
    uint32_t tbprd = legacy_to_tbclk(legacy_ticks);
    EPWM_REG16(b, EPWM_O_TBPRD) = (uint16_t)tbprd;
    /* Rescale any cached CMP values for outputs belonging to this unit. */
    for (uint32_t i = 0; i < EPWM_OUTPUTS_PER_UNIT; i++) {
        uint32_t idx = u * EPWM_OUTPUTS_PER_UNIT + i;
        uint32_t w = s_legacy_width[idx];
        if (w > legacy_ticks) w = legacy_ticks;
        uint32_t cmp = legacy_to_tbclk(w);
        if (i == 0) EPWM_REG16(b, EPWM_O_CMPA) = (uint16_t)cmp;
        else        EPWM_REG16(b, EPWM_O_CMPB) = (uint16_t)cmp;
    }
#endif
}

void hal_pwm_gen_enable(uint32_t gen, int enable)
{
    uint32_t u = gen_to_unit(gen);
    if (u >= NUM_EPWM_UNITS) return;
    s_gen_enabled[u] = enable ? 1U : 0U;
    /* On AM2434 the EPWM time-base runs whenever TBCTL.CTRMODE != FREEZE;
     * gating the output is handled by hal_pwm_output_state() below. */
}

void hal_pwm_output_state(uint32_t out_mask, int enable)
{
    for (uint32_t n = 0; n < NUM_LEGACY_OUTPUTS; n++) {
        if (out_mask & (1U << n)) {
            s_output_enabled[n] = enable ? 1U : 0U;
#ifndef QEMU_BUILD
            /* When disabling, force the CMP to full period so the output
             * stays LOW (= equipment OFF after opto inversion). When
             * enabling, the last cached width is re-applied. */
            uint32_t u    = n / EPWM_OUTPUTS_PER_UNIT;
            uint32_t isB  = n & 1U;
            uint32_t b    = epwm_base_for_unit(u);
            uint32_t w    = enable ? s_legacy_width[n] : s_legacy_period[u];
            if (w > s_legacy_period[u]) w = s_legacy_period[u];
            uint32_t cmp  = legacy_to_tbclk(w);
            if (isB) EPWM_REG16(b, EPWM_O_CMPB) = (uint16_t)cmp;
            else     EPWM_REG16(b, EPWM_O_CMPA) = (uint16_t)cmp;
#endif
        }
    }
}

void hal_pwm_pulse_width_set(uint32_t out, uint32_t legacy_ticks)
{
    uint32_t n = out_to_index(out);
    if (n >= NUM_LEGACY_OUTPUTS) return;
    uint32_t u = n / EPWM_OUTPUTS_PER_UNIT;
    uint32_t period = s_legacy_period[u];
    if (legacy_ticks > period) legacy_ticks = period;
    s_legacy_width[n] = legacy_ticks;
#ifndef QEMU_BUILD
    if (!s_output_enabled[n]) {
        /* Output currently gated off: cache only, don't drive pin. */
        return;
    }
    uint32_t b   = epwm_base_for_unit(u);
    uint32_t isB = n & 1U;
    uint32_t cmp = legacy_to_tbclk(legacy_ticks);
    if (isB) EPWM_REG16(b, EPWM_O_CMPB) = (uint16_t)cmp;
    else     EPWM_REG16(b, EPWM_O_CMPA) = (uint16_t)cmp;
#endif
}
