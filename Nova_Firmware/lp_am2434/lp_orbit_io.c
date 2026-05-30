/*
 * lp_orbit_io.c — see lp_orbit_io.h.
 *
 * Pin table format: for each channel, base_addr=0 means "not wired";
 * the channel becomes a no-op for both push and latch. To wire a new
 * channel:
 *   1. Add a GPIO instance to example.syscfg with $name = "CONFIG_GDC_DO_<N>"
 *      (or DI_<N>), pinDir = "OUTPUT" / "INPUT", pu_pd = "pd", and
 *      $assign = "<padname>".
 *   2. Rebuild — generated/ti_drivers_config.h will gain
 *      CONFIG_GDC_DO_<N>_BASE_ADDR + CONFIG_GDC_DO_<N>_PIN.
 *   3. Replace the IO_PIN_NONE entry below with IO_PIN(GDC_DO_<N>).
 *
 * Address translation: SysConfig's CSL_GPIOx_BASE constants are SoC
 * absolute. R5F sees them through the AddrTranslateP MMU layer. The
 * SysConfig-generated Drivers_open() already calls
 * AddrTranslateP_getLocalAddr() on its own pre-write, so by the time
 * this module runs the registered base-addr macros are still SoC-
 * absolute. We must translate every time we touch them.
 */

#include "lp_orbit_io.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <drivers/gpio.h>
#include <kernel/dpl/AddrTranslateP.h>

/* Pull in the SysConfig-generated channel macros. Channels not present
 * in the syscfg simply won't have their macros defined → the IO_PIN()
 * helper picks the IO_PIN_NONE sentinel for those slots. */
#include "ti_drivers_config.h"

/* --- table types ------------------------------------------------------ */

typedef struct {
    uint32_t base_addr;   /* SoC-absolute GPIO bank base (CSL_GPIOx_BASE),
                             0 = unwired (skip). */
    uint16_t pin;         /* GPIO pin index within the bank. */
} LpOrbitIoPin;

#define IO_PIN_NONE  { 0u, 0u }

/* IO_PIN(NAME) — expands to a populated entry for syscfg name
 * CONFIG_<NAME>. Wrap in an #ifdef so missing instances compile away
 * cleanly to IO_PIN_NONE. */
#define _IO_PIN_DEFINED(NAME)  { CONFIG_ ## NAME ## _BASE_ADDR, \
                                 CONFIG_ ## NAME ## _PIN }
#define IO_PIN(NAME)           _IO_PIN_DEFINED(NAME)

/* --- pin tables ------------------------------------------------------- */
/* Slots 0..9 = DO[0]..DO[9].
 * GDC mapping (orbit_gdc.c):
 *   DO[0]/DO[1] = door 1 open/close
 *   DO[2]/DO[3] = door 2 open/close
 *   DO[4]/DO[5] = door 3 open/close
 *   DO[6]/DO[7] = door 4 open/close
 *   DO[8]/DO[9] = door 5 open/close
 *
 * Triton mapping (orbit_triton.c):
 *   See TRITON_DO_* enum in orbit_triton.h.
 */
static const LpOrbitIoPin s_do_pins[ORBIT_DO_COUNT] = {
#ifdef CONFIG_GDC_DO_0_PIN
    IO_PIN(GDC_DO_0),     /* onboard USR LED — door 1 OPEN contactor */
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DO_1_PIN
    IO_PIN(GDC_DO_1),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DO_2_PIN
    IO_PIN(GDC_DO_2),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DO_3_PIN
    IO_PIN(GDC_DO_3),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DO_4_PIN
    IO_PIN(GDC_DO_4),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DO_5_PIN
    IO_PIN(GDC_DO_5),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DO_6_PIN
    IO_PIN(GDC_DO_6),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DO_7_PIN
    IO_PIN(GDC_DO_7),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DO_8_PIN
    IO_PIN(GDC_DO_8),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DO_9_PIN
    IO_PIN(GDC_DO_9),
#else
    IO_PIN_NONE,
#endif
};

static const LpOrbitIoPin s_di_pins[ORBIT_DI_COUNT] = {
#ifdef CONFIG_GDC_DI_0_PIN
    IO_PIN(GDC_DI_0),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DI_1_PIN
    IO_PIN(GDC_DI_1),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DI_2_PIN
    IO_PIN(GDC_DI_2),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DI_3_PIN
    IO_PIN(GDC_DI_3),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DI_4_PIN
    IO_PIN(GDC_DI_4),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DI_5_PIN
    IO_PIN(GDC_DI_5),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DI_6_PIN
    IO_PIN(GDC_DI_6),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DI_7_PIN
    IO_PIN(GDC_DI_7),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DI_8_PIN
    IO_PIN(GDC_DI_8),
#else
    IO_PIN_NONE,
#endif
#ifdef CONFIG_GDC_DI_9_PIN
    IO_PIN(GDC_DI_9),
#else
    IO_PIN_NONE,
#endif
};

/* --- helpers ---------------------------------------------------------- */

static inline uint32_t local_addr(uint32_t soc_addr)
{
    return (uint32_t)AddrTranslateP_getLocalAddr(soc_addr);
}

/* --- public API ------------------------------------------------------- */

void LpOrbitIo_Init(void)
{
    /* Force every wired DO low. Provides a clean post-boot state and
     * doubles as a "are the pins really programmed?" smoke check.
     * SysConfig's Drivers_open() already drove them low at init, but
     * cycling here makes the dependency explicit.
     *
     * NOTE: must NOT call DebugP_log here — runs pre-scheduler, and
     * DebugP_log takes a FreeRTOS mutex (would fault). Caller logs
     * via bb_uart0_puts. */
    for (size_t i = 0; i < ORBIT_DO_COUNT; ++i) {
        const LpOrbitIoPin *p = &s_do_pins[i];
        if (p->base_addr == 0u) continue;
        GPIO_pinWriteLow(local_addr(p->base_addr), p->pin);
    }
}

void LpOrbitIo_PushOutputs(const OrbitStateData *s)
{
    for (size_t i = 0; i < ORBIT_DO_COUNT; ++i) {
        const LpOrbitIoPin *p = &s_do_pins[i];
        if (p->base_addr == 0u) continue;
        const uint32_t base = local_addr(p->base_addr);
        if (s->do_[i]) GPIO_pinWriteHigh(base, p->pin);
        else           GPIO_pinWriteLow (base, p->pin);
    }
}

void LpOrbitIo_LatchInputs(OrbitStateData *s)
{
    for (size_t i = 0; i < ORBIT_DI_COUNT; ++i) {
        const LpOrbitIoPin *p = &s_di_pins[i];
        if (p->base_addr == 0u) continue;
        s->di[i] = (GPIO_pinRead(local_addr(p->base_addr), p->pin) != 0u);
    }
}
