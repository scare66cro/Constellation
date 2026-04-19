/* driverlib/interrupt.h — TivaWare interrupt shim for AM2434 port */
#ifndef DRIVERLIB_INTERRUPT_H_SHIM
#define DRIVERLIB_INTERRUPT_H_SHIM

#include <stdint.h>
#include <stdbool.h>

static inline void IntMasterEnable(void)  {}
static inline bool IntMasterDisable(void) { return false; }
static inline void IntEnable(uint32_t irq)  { (void)irq; }
static inline void IntDisable(uint32_t irq) { (void)irq; }
static inline void IntPrioritySet(uint32_t irq, uint8_t prio) { (void)irq; (void)prio; }
static inline void IntRegister(uint32_t irq, void(*handler)(void)) { (void)irq; (void)handler; }

#endif
