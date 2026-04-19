/*
 * hal.h — Hardware Abstraction Layer for Constellation firmware
 *
 * This header provides the common API that Application/ code calls.
 * The TM4C implementation lives in Mini_IO/Platform/; the AM2434
 * implementation lives here in Nova_Firmware/Platform/.
 *
 * Application code should include "hal.h" instead of driverlib headers.
 * During the transition period, the old driverlib calls are shimmed
 * through compatibility macros below.
 *
 * Copyright (c) 2026 Agristar
 */

#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stdbool.h>

/* ======================== System ======================== */

extern unsigned int system_clock_speed;
extern unsigned char system_mac[6];
extern unsigned int uptime_sec;
extern unsigned int uptime_ms;
extern unsigned int reset_cause;
extern unsigned int net_up;

/* ======================== GPIO ======================== */

/*
 * Pin descriptor — same structure as TM4C _pin_str so Application
 * code that references pin structs compiles without changes.
 * The fields are reinterpreted for AM2434 GPIO.
 */
typedef struct {
    unsigned long pin;          /* bit mask (TM4C) or pin number (AM2434) */
    unsigned long port;         /* port base address (TM4C) or port index (AM2434) */
    unsigned long periph;       /* peripheral enable (TM4C) or module ID (AM2434) */
    unsigned long strength;     /* drive strength */
    unsigned long type;         /* pad type */
    unsigned int  initial_state;
} _pin_str;

/* GPIO operations — same signatures as TM4C pinout.c */
void set_output(_pin_str pin, unsigned int state);
unsigned int read_input(_pin_str pin);
void configure_output_pin(_pin_str pin);

/* ======================== UART ======================== */

/* UART port identifiers */
typedef enum {
    HAL_UART_DEBUG = 0,     /* UART0 — debug console */
    HAL_UART_UI    = 1,     /* UART1 — RPi5 bridge (was Lantronix on TM4C) */
    HAL_UART_RS485A = 2,    /* UART2 — RS-485 VFD bus (was analog boards) */
    HAL_UART_RS485B = 3,    /* UART3 — RS-485 spare */
    HAL_UART_COUNT
} hal_uart_port_t;

void    hal_uart_init(hal_uart_port_t port, uint32_t baud);
void    hal_uart_send_char(hal_uart_port_t port, uint8_t ch);
void    hal_uart_send(hal_uart_port_t port, const uint8_t *data, uint32_t len);
int     hal_uart_rx_available(hal_uart_port_t port);
uint8_t hal_uart_get_char(hal_uart_port_t port);
void    hal_uart_int_enable(hal_uart_port_t port);
void    hal_uart_int_disable(hal_uart_port_t port);
void    hal_uart_flush_rx(hal_uart_port_t port);

/*
 * TivaWare compatibility — port IDs for Application code.
 * The actual function shims are in Platform/driverlib/ shim headers.
 */
#define LTX_UART        HAL_UART_UI
#define LTX_UART_INT    0
#define AUX_UART        HAL_UART_RS485A
#define AUX_UART_INT    0

/* ======================== SPI ======================== */

typedef enum {
    HAL_SPI_FLASH = 0,      /* SPI0 — external NOR flash (TM4C) / QSPI (AM2434) */
    HAL_SPI_SHIFT = 1,      /* Bit-banged shift registers (CPLD) */
    HAL_SPI_COUNT
} hal_spi_port_t;

void     hal_spi_init(hal_spi_port_t port, uint32_t speed);
uint8_t  hal_spi_transfer(hal_spi_port_t port, uint8_t tx);
void     hal_spi_cs_assert(hal_spi_port_t port);
void     hal_spi_cs_deassert(hal_spi_port_t port);

int  spi_lock(unsigned char block);
void spi_unlock(void);
void CreateSPILocks(void);

/* TivaWare SSI compatibility — shims are in Platform/driverlib/ssi.h */
#define SD_BASE         HAL_SPI_FLASH

/* ======================== Timer ======================== */

void    hal_timer_init(void);
void    hal_timer_delay_ms(uint32_t ms);
uint32_t hal_timer_get_ms(void);
uint32_t hal_timer_get_us(void);

/* ======================== Watchdog ======================== */

void    hal_watchdog_init(void);
void    hal_watchdog_feed(void);

/* TM4C compatibility — Application's Timer.c provides WatchdogInternal_Init()
   and WatchdogExternal_Init() which call driverlib/watchdog.h stubs. */

/* ======================== ADC ======================== */

void     hal_adc_init(void);
uint16_t hal_adc_read(uint8_t channel);

/* ======================== PWM (EPWM on AM2434) ========================
 *
 * Preserves legacy TM4C PWM semantics exactly so Application/PWM.c runs
 * unchanged:
 *   - 10 kHz carrier frequency (same as TM4C 120 MHz / 32 / 375)
 *   - Period/pulse values are in LEGACY TICKS at 3.75 MHz (LEGACY_PWM_PERIOD = 375)
 *   - Count-down mode, output HIGH on period reload, LOW on CMPA-down
 *     (so "PWM_PERIOD - Output" inversion in legacy code still produces
 *      the correct equipment-side duty through the opto-isolator)
 *
 * On QEMU: all functions are no-ops (no EPWM peripheral modelled).
 * On AM2434: programs EPWM0..EPWM3 as TM4C PWM_GEN_0..PWM_GEN_3 equivalents.
 */
#define LEGACY_PWM_TICK_HZ      3750000U   /* TM4C PWM clock: 120 MHz / 32 */
#define LEGACY_PWM_PERIOD_HZ    10000U     /* Target carrier: 10 kHz */
#define LEGACY_PWM_PERIOD       375U       /* LEGACY_PWM_TICK_HZ / LEGACY_PWM_PERIOD_HZ */

void hal_pwm_init(void);
void hal_pwm_clock_set(uint32_t tm4c_div);                 /* PWM_SYSCLK_DIV_32 etc. */
void hal_pwm_gen_configure(uint32_t gen, uint32_t config); /* PWM_GEN_0..3 + mode bits */
void hal_pwm_gen_period_set(uint32_t gen, uint32_t legacy_ticks);
void hal_pwm_gen_enable(uint32_t gen, int enable);
void hal_pwm_output_state(uint32_t out_mask, int enable);
void hal_pwm_pulse_width_set(uint32_t out, uint32_t legacy_ticks);

/* ======================== Flash (QSPI Settings Vault) ======================== */

void    hal_flash_init(void);
int     hal_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);
int     hal_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len);
int     hal_flash_erase_sector(uint32_t addr);

/* ======================== I2C (RTC) ======================== */

void     hal_i2c_init(void);
int      hal_i2c_write(uint8_t addr, const uint8_t *data, uint32_t len);
int      hal_i2c_read(uint8_t addr, uint8_t *data, uint32_t len);

/* ======================== Debug ======================== */

void debug_printf(const char *fmt, ...);

/*
 * TivaWare compatibility -- shim functions are in Platform driverlib
 * and inc shim headers, not as macros here.
 */
/* UARTStdioConfig shim is in Platform/utils/uartstdio.h */

/* ======================== Pin Setup ======================== */

void PinoutSet(void);

#endif /* HAL_H */
