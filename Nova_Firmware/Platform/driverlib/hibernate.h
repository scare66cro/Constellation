/* driverlib/hibernate.h — TivaWare hibernate shim — not used on AM2434 */
#ifndef DRIVERLIB_HIBERNATE_H_SHIM
#define DRIVERLIB_HIBERNATE_H_SHIM

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

static inline void HibernateEnableExpClk(uint32_t clk) { (void)clk; }
static inline void HibernateRTCEnable(void) {}
static inline void HibernateRTCSet(uint32_t val) { (void)val; }
static inline uint32_t HibernateRTCGet(void) { return 0; }
static inline void HibernateRTCMatchSet(uint32_t idx, uint32_t val) { (void)idx; (void)val; }
static inline uint32_t HibernateRTCMatchGet(uint32_t idx) { (void)idx; return 0; }
static inline void HibernateClockConfig(uint32_t cfg) { (void)cfg; }
static inline void HibernateRTCTrimSet(uint32_t val) { (void)val; }
static inline uint32_t HibernateRTCTrimGet(void) { return 0; }
static inline uint32_t HibernateIntStatus(bool masked) { (void)masked; return 0; }
static inline void HibernateIntClear(uint32_t flags) { (void)flags; }
static inline void HibernateCounterMode(uint32_t mode) { (void)mode; }

/* Calendar (wall-clock) — implemented in Platform/hal_rtc.c.
 *
 * On AM2434 (no battery-backed RTC), wall time is maintained by
 * `hal_timer_get_ms()` deltas from a baseline `time_t` recorded by
 * `HibernateCalendarSet()`. The baseline is set either by:
 *   (a) the bridge auto-pushing host wall-clock at Nova-connect, or
 *   (b) an operator setting the date/time on the Svelte UI.
 * Until something sets it, `HibernateCalendarGet()` returns an
 * obviously-invalid date (year 1900) so the legacy `WARN_DATETIME`
 * path triggers normally. */
extern void HibernateCalendarSet(struct tm *t);
extern void HibernateCalendarGet(struct tm *t);

#define HIBERNATE_OSC_LOWDRIVE   0
#define HIBERNATE_OSC_HIGHDRIVE  0
#define HIBERNATE_COUNTER_24HR   0x00000001
#define HIBERNATE_COUNTER_RTC    0x00000000

#endif
