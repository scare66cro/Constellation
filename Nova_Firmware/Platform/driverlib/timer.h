/* driverlib/timer.h — TivaWare timer shim for AM2434 */
#ifndef DRIVERLIB_TIMER_H_SHIM
#define DRIVERLIB_TIMER_H_SHIM

#include <stdint.h>
#include "hal.h"

/* Timer configuration values — Application code may reference these defines */
#define TIMER_CFG_ONE_SHOT      0x00000021
#define TIMER_CFG_ONE_SHOT_UP   0x00000031
#define TIMER_CFG_PERIODIC      0x00000022
#define TIMER_CFG_PERIODIC_UP   0x00000032
#define TIMER_CFG_SPLIT_PAIR    0x04000000
#define TIMER_CFG_A_ONE_SHOT    0x00000021
#define TIMER_CFG_A_PERIODIC    0x00000022
#define TIMER_CFG_A_CAP_COUNT   0x00000003
#define TIMER_CFG_A_CAP_TIME    0x00000007
#define TIMER_CFG_B_ONE_SHOT    0x00002100
#define TIMER_CFG_B_PERIODIC    0x00002200

#define TIMER_TIMA_TIMEOUT      0x00000001
#define TIMER_TIMB_TIMEOUT      0x00000100
#define TIMER_BOTH              0x00000300
#define TIMER_A                 0x000000FF
#define TIMER_B                 0x0000FF00

/* Base addresses — Application references TIMER0_BASE, etc. */
#ifndef TIMER0_BASE
#define TIMER0_BASE             0
#define TIMER1_BASE             0
#define TIMER2_BASE             0
#define TIMER3_BASE             0
#define TIMER4_BASE             0
#define TIMER5_BASE             0
#endif

/* All timer functions are no-ops on AM2434 — we use the R5F PMU timer */
static inline void TimerConfigure(uint32_t b, uint32_t c) { (void)b; (void)c; }
static inline void TimerLoadSet(uint32_t b, uint32_t t, uint32_t v) { (void)b; (void)t; (void)v; }
static inline uint32_t TimerLoadGet(uint32_t b, uint32_t t) { (void)b; (void)t; return 0; }
static inline uint32_t TimerValueGet(uint32_t b, uint32_t t) { (void)b; (void)t; return 0; }
static inline void TimerEnable(uint32_t b, uint32_t t) { (void)b; (void)t; }
static inline void TimerDisable(uint32_t b, uint32_t t) { (void)b; (void)t; }
static inline void TimerIntEnable(uint32_t b, uint32_t f) { (void)b; (void)f; }
static inline void TimerIntDisable(uint32_t b, uint32_t f) { (void)b; (void)f; }
static inline void TimerIntClear(uint32_t b, uint32_t f) { (void)b; (void)f; }
static inline uint32_t TimerIntStatus(uint32_t b, int m) { (void)b; (void)m; return 0; }
static inline void TimerControlEvent(uint32_t b, uint32_t t, uint32_t e) { (void)b; (void)t; (void)e; }
static inline void TimerPrescaleSet(uint32_t b, uint32_t t, uint32_t v) { (void)b; (void)t; (void)v; }
static inline void TimerIntRegister(uint32_t b, uint32_t t, void (*fn)(void)) { (void)b; (void)t; (void)fn; }
static inline void TimerIntUnregister(uint32_t b, uint32_t t) { (void)b; (void)t; }

#endif
