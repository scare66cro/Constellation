/* driverlib/pin_map.h — TivaWare pin mux defines — all no-ops on AM2434 */
#ifndef DRIVERLIB_PIN_MAP_H_SHIM
#define DRIVERLIB_PIN_MAP_H_SHIM

/* Pin mux configuration values — Application code passes these to GPIOPinConfigure()
   which is a no-op on AM2434. Define them so compilation succeeds. */
#define GPIO_PA0_U0RX       0x00000001
#define GPIO_PA1_U0TX       0x00000401
#define GPIO_PA2_SSI0CLK    0x00000802
#define GPIO_PA4_SSI0XDAT0  0x00001002
#define GPIO_PA5_SSI0XDAT1  0x00001402
#define GPIO_PA6_U2RX       0x00001801
#define GPIO_PA7_U2TX       0x00001C01
#define GPIO_PB0_U1RX       0x00010001
#define GPIO_PB1_U1TX       0x00010401
#define GPIO_PB2_I2C0SCL    0x00010802
#define GPIO_PB3_I2C0SDA    0x00010C02
#define GPIO_PB5_SSI1CLK    0x0001140F
#define GPIO_PD0_SSI2XDAT1  0x0003000F
#define GPIO_PD1_SSI2XDAT0  0x0003040F
#define GPIO_PD3_SSI2CLK    0x00030C0F
#define GPIO_PE4_SSI1XDAT0  0x0004100F
#define GPIO_PE5_SSI1XDAT1  0x0004140F
#define GPIO_PF0_EN0LED0    0x00050005
#define GPIO_PF4_EN0LED1    0x00051005
#define GPIO_PL4_T0CCP0     0x000B1007

/* PWM pin mux defines */
#define GPIO_PF1_M0PWM1     0x00050106
#define GPIO_PF2_M0PWM2     0x00050206
#define GPIO_PF3_M0PWM3     0x00050306
#define GPIO_PG0_M0PWM4     0x00060006
#define GPIO_PG1_M0PWM5     0x00060106
#define GPIO_PK4_M0PWM6     0x00090406

/* RTC pin mux */
#define GPIO_PP3_RTCCLK     0x000E0307

#endif
