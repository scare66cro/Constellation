/* utils/uartstdio.h — TivaWare UART stdio shim for AM2434 */
#ifndef UTILS_UARTSTDIO_H_SHIM
#define UTILS_UARTSTDIO_H_SHIM

#include <stdint.h>

/*
 * On TM4C, UARTStdioConfig() selects which UART UARTprintf() writes to.
 * On AM2434 we don't use this — debug output goes through debug_printf()
 * which writes to HAL_UART_DEBUG.  These stubs let the code compile.
 */
static inline void UARTStdioConfig(uint32_t port, uint32_t baud, uint32_t clk) {
    (void)port; (void)baud; (void)clk;
}

static inline int UARTwrite(const char *buf, uint32_t len) {
    (void)buf; (void)len;
    return (int)len;
}

static inline void UARTprintf(const char *fmt, ...) {
    (void)fmt;
}

static inline const char *UARTgets(char *buf, uint32_t len) {
    (void)len;
    buf[0] = '\0';
    return buf;
}

static inline void UARTFlushTx(int drain) {
    (void)drain;
}

static inline int UARTRxBytesAvail(void) {
    return 0;
}

#endif
