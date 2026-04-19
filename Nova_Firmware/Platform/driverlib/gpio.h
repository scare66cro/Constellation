/* driverlib/gpio.h — TivaWare GPIO shim for AM2434 port */
#ifndef DRIVERLIB_GPIO_H_SHIM
#define DRIVERLIB_GPIO_H_SHIM

#include <stdint.h>
#include <stdbool.h>

/* Pin bits */
#define GPIO_PIN_0  0x01
#define GPIO_PIN_1  0x02
#define GPIO_PIN_2  0x04
#define GPIO_PIN_3  0x08
#define GPIO_PIN_4  0x10
#define GPIO_PIN_5  0x20
#define GPIO_PIN_6  0x40
#define GPIO_PIN_7  0x80

/* Direction */
#define GPIO_DIR_MODE_IN    0x00
#define GPIO_DIR_MODE_OUT   0x01
#define GPIO_DIR_MODE_HW    0x02

/* Pad config */
#define GPIO_STRENGTH_2MA   0x01
#define GPIO_STRENGTH_4MA   0x02
#define GPIO_STRENGTH_8MA   0x04
#define GPIO_STRENGTH_12MA  0x08
#define GPIO_PIN_TYPE_STD       0x08
#define GPIO_PIN_TYPE_STD_WPU   0x0A
#define GPIO_PIN_TYPE_STD_WPD   0x0C
#define GPIO_PIN_TYPE_OD        0x09
#define GPIO_PIN_TYPE_ANALOG    0x00

/* Port base addresses — no-ops, our HAL uses GPIO module addresses */
#define GPIO_PORTA_BASE 0
#define GPIO_PORTB_BASE 0
#define GPIO_PORTC_BASE 0
#define GPIO_PORTD_BASE 0
#define GPIO_PORTE_BASE 0
#define GPIO_PORTF_BASE 0
#define GPIO_PORTG_BASE 0
#define GPIO_PORTH_BASE 0
#define GPIO_PORTJ_BASE 0
#define GPIO_PORTK_BASE 0
#define GPIO_PORTL_BASE 0
#define GPIO_PORTM_BASE 0
#define GPIO_PORTN_BASE 0
#define GPIO_PORTP_BASE 0
#define GPIO_PORTQ_BASE 0

/* Function stubs */
static inline void GPIOPinWrite(uint32_t port, uint8_t pins, uint8_t val)
{ (void)port; (void)pins; (void)val; }

static inline uint32_t GPIOPinRead(uint32_t port, uint8_t pins)
{ (void)port; (void)pins; return 0; }

static inline void GPIODirModeSet(uint32_t port, uint8_t pins, uint32_t mode)
{ (void)port; (void)pins; (void)mode; }

static inline void GPIOPadConfigSet(uint32_t port, uint8_t pins,
                                     uint32_t strength, uint32_t type)
{ (void)port; (void)pins; (void)strength; (void)type; }

static inline void GPIOPinTypeGPIOInput(uint32_t port, uint8_t pins)
{ (void)port; (void)pins; }

static inline void GPIOPinTypeGPIOOutput(uint32_t port, uint8_t pins)
{ (void)port; (void)pins; }

static inline void GPIOPinTypeUART(uint32_t port, uint8_t pins)
{ (void)port; (void)pins; }

static inline void GPIOPinTypeSSI(uint32_t port, uint8_t pins)
{ (void)port; (void)pins; }

static inline void GPIOPinTypeI2C(uint32_t port, uint8_t pins)
{ (void)port; (void)pins; }

static inline void GPIOPinTypeI2CSCL(uint32_t port, uint8_t pins)
{ (void)port; (void)pins; }

static inline void GPIOPinTypePWM(uint32_t port, uint8_t pins)
{ (void)port; (void)pins; }

static inline void GPIOPinTypeTimer(uint32_t port, uint8_t pins)
{ (void)port; (void)pins; }

static inline void GPIOPinTypeEthernetLED(uint32_t port, uint8_t pins)
{ (void)port; (void)pins; }

static inline void GPIOPinConfigure(uint32_t config)
{ (void)config; }

static inline void GPIOIntEnable(uint32_t port, uint32_t pins)
{ (void)port; (void)pins; }

static inline void GPIOIntDisable(uint32_t port, uint32_t pins)
{ (void)port; (void)pins; }

static inline void GPIOIntClear(uint32_t port, uint32_t pins)
{ (void)port; (void)pins; }

static inline uint32_t GPIOIntStatus(uint32_t port, bool masked)
{ (void)port; (void)masked; return 0; }

#endif
