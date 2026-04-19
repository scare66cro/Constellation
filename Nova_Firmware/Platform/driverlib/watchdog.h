/* driverlib/watchdog.h — TivaWare watchdog shim for AM2434 */
#ifndef DRIVERLIB_WATCHDOG_H_SHIM
#define DRIVERLIB_WATCHDOG_H_SHIM

#include <stdint.h>
#include <stdbool.h>

#define WATCHDOG0_BASE  0
#define WATCHDOG1_BASE  0

static inline void WatchdogReloadSet(uint32_t base, uint32_t val)
{ (void)base; (void)val; }
static inline void WatchdogResetEnable(uint32_t base) { (void)base; }
static inline void WatchdogEnable(uint32_t base) { (void)base; }
static inline void WatchdogLock(uint32_t base) { (void)base; }
static inline void WatchdogUnlock(uint32_t base) { (void)base; }
static inline void WatchdogIntClear(uint32_t base) { (void)base; }
static inline void WatchdogIntEnable(uint32_t base) { (void)base; }
static inline void WatchdogStallEnable(uint32_t base) { (void)base; }

#endif
