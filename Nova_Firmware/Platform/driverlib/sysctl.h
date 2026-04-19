/* driverlib/sysctl.h — TivaWare sysctl shim for AM2434 port */
#ifndef DRIVERLIB_SYSCTL_H_SHIM
#define DRIVERLIB_SYSCTL_H_SHIM

#include <stdint.h>
#include <stdbool.h>

/* PLL/oscillator config defines — no-ops on AM2434 */
#define SYSCTL_XTAL_25MHZ      0x00000000
#define SYSCTL_OSC_MAIN         0x00000000
#define SYSCTL_USE_PLL          0x00000000
#define SYSCTL_CFG_VCO_480      0x00000000
#define SYSCTL_MOSC_HIGHFREQ    0x00000000

/* Reset cause bits */
#define SYSCTL_CAUSE_HSRVREQ    0x00001000
#define SYSCTL_CAUSE_HIB        0x00000040
#define SYSCTL_CAUSE_WDOG1      0x00000020
#define SYSCTL_CAUSE_SW         0x00000010
#define SYSCTL_CAUSE_WDOG0      0x00000008
#define SYSCTL_CAUSE_BOR        0x00000004
#define SYSCTL_CAUSE_POR        0x00000002
#define SYSCTL_CAUSE_EXT        0x00000001

/* Peripheral enable IDs — all no-ops */
#define SYSCTL_PERIPH_I2C0      0
#define SYSCTL_PERIPH_UART0     0
#define SYSCTL_PERIPH_UART1     0
#define SYSCTL_PERIPH_UART2     0
#define SYSCTL_PERIPH_SSI0      0
#define SYSCTL_PERIPH_SSI1      0
#define SYSCTL_PERIPH_SSI2      0
#define SYSCTL_PERIPH_GPIOA     0
#define SYSCTL_PERIPH_GPIOB     0
#define SYSCTL_PERIPH_GPIOC     0
#define SYSCTL_PERIPH_GPIOD     0
#define SYSCTL_PERIPH_GPIOE     0
#define SYSCTL_PERIPH_GPIOF     0
#define SYSCTL_PERIPH_GPIOG     0
#define SYSCTL_PERIPH_GPIOH     0
#define SYSCTL_PERIPH_GPIOJ     0
#define SYSCTL_PERIPH_GPIOK     0
#define SYSCTL_PERIPH_GPIOL     0
#define SYSCTL_PERIPH_GPIOM     0
#define SYSCTL_PERIPH_GPION     0
#define SYSCTL_PERIPH_GPIOP     0
#define SYSCTL_PERIPH_GPIOQ     0
#define SYSCTL_PERIPH_PWM0      0
#define SYSCTL_PERIPH_PWM1      0
#define SYSCTL_PERIPH_TIMER0    0
#define SYSCTL_PERIPH_TIMER1    0
#define SYSCTL_PERIPH_TIMER2    0
#define SYSCTL_PERIPH_TIMER3    0
#define SYSCTL_PERIPH_TIMER4    0
#define SYSCTL_PERIPH_TIMER5    0
#define SYSCTL_PERIPH_ADC0      0
#define SYSCTL_PERIPH_ADC1      0
#define SYSCTL_PERIPH_WDOG0     0
#define SYSCTL_PERIPH_WDOG1     0
#define SYSCTL_PERIPH_HIBERNATE 0

static inline uint32_t SysCtlClockFreqSet(uint32_t cfg, uint32_t freq)
{ (void)cfg; (void)freq; return 800000000UL; }

static inline uint32_t SysCtlClockGet(void)
{ return 800000000UL; }

static inline void SysCtlPeripheralEnable(uint32_t periph) { (void)periph; }
static inline bool SysCtlPeripheralReady(uint32_t periph) { (void)periph; return true; }
static inline void SysCtlPeripheralDisable(uint32_t periph) { (void)periph; }
static inline void SysCtlPeripheralReset(uint32_t periph) { (void)periph; }

static inline uint32_t SysCtlResetCauseGet(void) { return 0; }
static inline void SysCtlResetCauseClear(uint32_t cause) { (void)cause; }
static inline void SysCtlMOSCConfigSet(uint32_t cfg) { (void)cfg; }
static inline void SysCtlReset(void) { while(1); }
static inline void SysCtlDelay(uint32_t count) { (void)count; }

#endif
