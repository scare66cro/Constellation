/* inc/hw_memmap.h — TivaWare memory map shim — base addresses as no-ops */
#ifndef HW_MEMMAP_H_SHIM
#define HW_MEMMAP_H_SHIM

/* These are referenced by Application code but not actually used
   since our HAL abstracts the hardware access. Define them as 0. */
#define UART0_BASE      0
#define UART1_BASE      1
#define UART2_BASE      2
#define UART3_BASE      3
#define SSI0_BASE       0
#define SSI1_BASE       1
#define SSI2_BASE       2
#define I2C0_BASE       0
#define I2C1_BASE       1
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
#define ADC0_BASE       0
#define ADC1_BASE       0
#define TIMER0_BASE     0
#define TIMER1_BASE     0
#define WATCHDOG0_BASE  0
#define WATCHDOG1_BASE  0
#define PWM0_BASE       0
#define FLASH_CTRL_BASE 0
#define SYSCTL_BASE     0
#define HIBERNATE_BASE  0

#endif
