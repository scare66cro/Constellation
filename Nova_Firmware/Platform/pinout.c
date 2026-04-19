/*
 * pinout.c — AM2434 Nova pin configuration
 *
 * Replaces Mini_IO/Platform/Drivers/pinout.c.
 * Initializes all GPIO pins, UARTs, SPI, I2C, ADC.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "pinout.h"
#include "hal.h"
#include "debug.h"

void PinoutSet(void)
{
    /*
     * On AM2434, pin muxing is typically configured by TI SysConfig
     * tool which generates a board_config.c.  For QEMU development,
     * we just initialize the HAL drivers directly.
     */

    /* UARTs */
    hal_uart_init(HAL_UART_DEBUG,  115200);   /* UART0: debug console */
    hal_uart_init(HAL_UART_UI,     230400);   /* UART1: RPi5 bridge */
    hal_uart_init(HAL_UART_RS485A, 19200);    /* UART2: VFD RS-485 bus */
    hal_uart_init(HAL_UART_RS485B, 9600);     /* UART3: spare RS-485 */

    /* RS-485 direction pins (DE = low = receive mode) */
    configure_output_pin(AUX_DIR);
    configure_output_pin(RS485B_DIR);

    /* Watchdog heartbeat output */
    configure_output_pin(WD_CPLD);

    /* Status LEDs */
    configure_output_pin(LED_STATUS);
    configure_output_pin(LED_COMMS);

    /* Shift register pins (legacy compat — used by SerialShift.c) */
    configure_output_pin(SS_MOSI);
    configure_output_pin(SS_SCLK);
    configure_output_pin(SS_RCLK);
    configure_output_pin(SS_CS0);
    configure_output_pin(SS_CS1);
    configure_output_pin(SS_CS2);
    /* SS_MISO is input — configure_input_pin handled by HAL */

    /* QSPI flash (settings vault) */
    hal_flash_init();

    /* Timer (cycle counter) */
    hal_timer_init();

    debug_printf("PinoutSet: AM2434 Nova pins configured\r\n");
}
