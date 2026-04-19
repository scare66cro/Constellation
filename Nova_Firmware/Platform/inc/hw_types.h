/* inc/hw_types.h — TivaWare hardware types shim */
#ifndef HW_TYPES_H_SHIM
#define HW_TYPES_H_SHIM

#include <stdint.h>

#define HWREG(x)    (*((volatile uint32_t *)(x)))
#define HWREGH(x)   (*((volatile uint16_t *)(x)))
#define HWREGB(x)   (*((volatile uint8_t *)(x)))

#define HWREGBITW(addr, bit)  HWREG(addr)
#define HWREGBITH(addr, bit)  HWREGH(addr)
#define HWREGBITB(addr, bit)  HWREGB(addr)

#endif
