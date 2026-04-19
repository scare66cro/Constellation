/* inc/hw_ints.h — TivaWare interrupt number shim for AM2434 */
#ifndef HW_INTS_H_SHIM
#define HW_INTS_H_SHIM

/* TM4C interrupt numbers — Application code references INT_UARTx, INT_TIMERx, etc.
   On AM2434 R5F these are meaningless (we use VIM IRQ numbers) but we
   provide them so the code compiles.  The actual interrupt setup is in HAL. */

#define INT_UART0           21
#define INT_UART1           22
#define INT_UART2           49
#define INT_UART3           75
#define INT_UART4           76
#define INT_UART5           77
#define INT_UART6           78
#define INT_UART7           79

#define INT_TIMER0A         35
#define INT_TIMER0B         36
#define INT_TIMER1A         37
#define INT_TIMER1B         38
#define INT_TIMER2A         39
#define INT_TIMER2B         40
#define INT_TIMER3A         51
#define INT_TIMER3B         52
#define INT_TIMER4A         86
#define INT_TIMER4B         87
#define INT_TIMER5A         108
#define INT_TIMER5B         109

#define INT_SSI0            23
#define INT_SSI1            50
#define INT_SSI2            73
#define INT_SSI3            74

#define INT_I2C0            24
#define INT_I2C1            53
#define INT_I2C2            68
#define INT_I2C3            69

#define INT_ADC0SS0         30
#define INT_ADC0SS1         31
#define INT_ADC0SS2         32
#define INT_ADC0SS3         33
#define INT_ADC1SS0         64
#define INT_ADC1SS1         65
#define INT_ADC1SS2         66
#define INT_ADC1SS3         67

#define INT_WATCHDOG        34

#define FAULT_NMI           2
#define FAULT_HARD          3
#define FAULT_MPU           4
#define FAULT_BUS           5
#define FAULT_USAGE         6
#define FAULT_SVCALL        11
#define FAULT_DEBUG         12
#define FAULT_PENDSV        14
#define FAULT_SYSTICK       15

#define NUM_INTERRUPTS      240

#endif
