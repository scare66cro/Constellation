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
static inline void HibernateCalendarSet(struct tm *t) { (void)t; }
static inline void HibernateCalendarGet(struct tm *t) { (void)t; }

#define HIBERNATE_OSC_LOWDRIVE   0
#define HIBERNATE_OSC_HIGHDRIVE  0
#define HIBERNATE_COUNTER_24HR   0x00000001
#define HIBERNATE_COUNTER_RTC    0x00000000

#endif
