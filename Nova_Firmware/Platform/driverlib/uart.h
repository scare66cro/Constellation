/*
 * driverlib/uart.h — TivaWare UART shim for AM2434 port
 *
 * Shadows ../Mini_IO/Platform/Drivers/driverlib/uart.h so that
 * Application code like Usart.c compiles without modification.
 * Provides the same function signatures, implemented as inline
 * wrappers around our HAL.
 */

#ifndef DRIVERLIB_UART_H_SHIM
#define DRIVERLIB_UART_H_SHIM

#include <stdint.h>
#include <stdbool.h>
#include "hal.h"

/* ---- Interrupt mask bits (same values as TivaWare) ---- */
#undef UART_INT_RX
#undef UART_INT_RT
#define UART_INT_OE     0x400
#define UART_INT_BE     0x200
#define UART_INT_PE     0x100
#define UART_INT_FE     0x080
#define UART_INT_RT     0x040
#define UART_INT_TX     0x020
#define UART_INT_RX     0x010
#define UART_INT_DSR    0x008
#define UART_INT_DCD    0x004
#define UART_INT_CTS    0x002
#define UART_INT_RI     0x001

/* ---- Config bits ---- */
#define UART_CONFIG_WLEN_8  0x00000060
#define UART_CONFIG_WLEN_7  0x00000040
#define UART_CONFIG_WLEN_6  0x00000020
#define UART_CONFIG_WLEN_5  0x00000000
#define UART_CONFIG_STOP_ONE  0x00000000
#define UART_CONFIG_STOP_TWO  0x00000008
#define UART_CONFIG_PAR_NONE  0x00000000
#define UART_CONFIG_PAR_EVEN  0x00000006
#define UART_CONFIG_PAR_ODD   0x00000002

/* ---- FIFO levels ---- */
#define UART_FIFO_TX1_8   0x00000000
#define UART_FIFO_TX2_8   0x00000001
#define UART_FIFO_TX4_8   0x00000002
#define UART_FIFO_TX6_8   0x00000003
#define UART_FIFO_TX7_8   0x00000004
#define UART_FIFO_RX1_8   0x00000000
#define UART_FIFO_RX2_8   0x00000008
#define UART_FIFO_RX4_8   0x00000010
#define UART_FIFO_RX6_8   0x00000018
#define UART_FIFO_RX7_8   0x00000020

/* ---- Function shims ---- */

static inline void UARTConfigSetExpClk(uint32_t base, uint32_t clk,
                                        uint32_t baud, uint32_t config)
{
    (void)clk; (void)config;
    hal_uart_init((hal_uart_port_t)base, baud);
}

static inline bool UARTCharsAvail(uint32_t base)
{
    return hal_uart_rx_available((hal_uart_port_t)base) ? true : false;
}

static inline int32_t UARTCharGetNonBlocking(uint32_t base)
{
    return (int32_t)hal_uart_get_char((hal_uart_port_t)base);
}

static inline int32_t UARTCharGet(uint32_t base)
{
    while (!hal_uart_rx_available((hal_uart_port_t)base)) {}
    return (int32_t)hal_uart_get_char((hal_uart_port_t)base);
}

static inline bool UARTCharPutNonBlocking(uint32_t base, uint8_t ch)
{
    hal_uart_send_char((hal_uart_port_t)base, ch);
    return true;
}

static inline void UARTCharPut(uint32_t base, uint8_t ch)
{
    hal_uart_send_char((hal_uart_port_t)base, ch);
}

static inline uint32_t UARTIntStatus(uint32_t base, bool masked)
{
    (void)base; (void)masked;
    return UART_INT_RX | UART_INT_RT;  /* always report RX ready for ISR */
}

static inline void UARTIntClear(uint32_t base, uint32_t flags)
{
    (void)base; (void)flags;
}

static inline void UARTRxErrorClear(uint32_t base)
{
    (void)base;
}

static inline void UARTIntEnable(uint32_t base, uint32_t flags)
{
    (void)flags;
    hal_uart_int_enable((hal_uart_port_t)base);
}

static inline void UARTIntDisable(uint32_t base, uint32_t flags)
{
    (void)flags;
    hal_uart_int_disable((hal_uart_port_t)base);
}

static inline void UARTFIFOLevelSet(uint32_t base, uint32_t txLevel,
                                     uint32_t rxLevel)
{
    (void)base; (void)txLevel; (void)rxLevel;
}

static inline uint32_t UARTRxErrorGet(uint32_t base) {
    (void)base; return 0;
}

static inline bool UARTBusy(uint32_t base) {
    (void)base; return false;
}

static inline void UARTBreakCtl(uint32_t base, bool state) {
    (void)base; (void)state;
}

static inline void UARTFIFOEnable(uint32_t base) { (void)base; }
static inline void UARTFIFODisable(uint32_t base) { (void)base; }

static inline void UARTEnable(uint32_t base) { (void)base; }
static inline void UARTDisable(uint32_t base) { (void)base; }

/* TX interrupt mode */
#define UART_TXINT_MODE_FIFO  0x00000000
#define UART_TXINT_MODE_EOT   0x00000010

static inline void UARTTxIntModeSet(uint32_t base, uint32_t mode) {
    (void)base; (void)mode;
}

static inline uint32_t UARTTxIntModeGet(uint32_t base) {
    (void)base; return UART_TXINT_MODE_FIFO;
}

#endif /* DRIVERLIB_UART_H_SHIM */
