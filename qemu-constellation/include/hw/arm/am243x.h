/*
 * TI Sitara AM243x SoC - Constellation Platform
 *
 * Common header for AM2434 (Nova) and AM2432 (Orbit/Pulsar/Triton)
 * Cortex-R5F real-time MCUs used in the Agristar Constellation platform.
 *
 * Reference: TI AM243x Technical Reference Manual (SPRUIM2)
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_AM243X_H
#define HW_ARM_AM243X_H

#include "hw/sysbus.h"
#include "qom/object.h"

/* ======================== AM2434 (Nova) Memory Map ======================== */

/*
 * The AM243x has a complex memory map. Key regions for emulation:
 *
 * R5F TCM (Tightly Coupled Memory):
 *   ATCM: 0x00000000  64 KB  (instruction)
 *   BTCM: 0x41010000  64 KB  (data)
 *
 * On-Chip SRAM (OCRAM/MSRAM):
 *   0x70000000  2 MB (AM2434) / 1.5 MB (AM2432)
 *
 * On-Chip Flash (FLASH):
 *   0x60000000  2 MB (AM2434) / 1.5 MB (AM2432)
 *
 * Peripheral region:
 *   0x02000000 – 0x0FFFFFFF  (MCU domain peripherals)
 *   0x20000000 – 0x2FFFFFFF  (main domain peripherals)
 */

/* --- TCM (Tightly Coupled Memory) --- */
#define AM243X_R5F_ATCM_BASE       0x00000000
#define AM243X_R5F_ATCM_SIZE       (64 * 1024)     /* 64 KB */
#define AM243X_R5F_BTCM_BASE       0x41010000
#define AM243X_R5F_BTCM_SIZE       (64 * 1024)     /* 64 KB */

/* --- On-Chip SRAM (MSRAM) --- */
#define AM243X_MSRAM_BASE           0x70000000
#define AM2434_MSRAM_SIZE           (2 * 1024 * 1024)   /* 2 MB */
#define AM2432_MSRAM_SIZE           (1536 * 1024)        /* 1.5 MB */

/* --- On-Chip Flash --- */
#define AM243X_FLASH_BASE           0x60000000
#define AM2434_FLASH_SIZE           (2 * 1024 * 1024)   /* 2 MB */
#define AM2432_FLASH_SIZE           (1536 * 1024)        /* 1.5 MB */

/* --- OSPI/QSPI External Flash Window --- */
#define AM243X_OSPI_DATA_BASE       0x50000000
#define AM243X_OSPI_DATA_SIZE       (64 * 1024 * 1024)  /* 64 MB window */

/* ======================== Peripheral Addresses ======================== */

/* UART (TI 16550-compatible) */
#define AM243X_UART0_BASE           0x02800000
#define AM243X_UART1_BASE           0x02810000
#define AM243X_UART2_BASE           0x02820000
#define AM243X_UART3_BASE           0x02830000
#define AM243X_UART4_BASE           0x02840000
#define AM243X_UART5_BASE           0x02850000
/* AM2434 has UART6-8 (Nova only) */
#define AM243X_UART6_BASE           0x02860000
#define AM243X_UART7_BASE           0x02870000
#define AM243X_UART8_BASE           0x02880000

/* SPI (McSPI) */
#define AM243X_MCSPI0_BASE          0x20100000
#define AM243X_MCSPI1_BASE          0x20110000
#define AM243X_MCSPI2_BASE          0x20120000
#define AM243X_MCSPI3_BASE          0x20130000  /* AM2434 only */
#define AM243X_MCSPI4_BASE          0x20140000  /* AM2434 only */

/* I2C */
#define AM243X_I2C0_BASE            0x20000000
#define AM243X_I2C1_BASE            0x20010000
#define AM243X_I2C2_BASE            0x20020000

/* GPIO */
#define AM243X_GPIO0_BASE           0x00600000
#define AM243X_GPIO1_BASE           0x00601000

/* Timer (DMTIMER) */
#define AM243X_TIMER0_BASE          0x02400000
#define AM243X_TIMER1_BASE          0x02410000
#define AM243X_TIMER2_BASE          0x02420000
#define AM243X_TIMER3_BASE          0x02430000

/* Watchdog */
#define AM243X_RTI0_BASE            0x02F00000  /* RTI/WWDT for R5F-0 */
#define AM243X_RTI1_BASE            0x02F10000  /* RTI/WWDT for R5F-1 */

/* ADC (12-bit, 4.6 Msps) */
#define AM243X_ADC0_BASE            0x02500000

/* CPSW (Common Platform Switch — Ethernet) */
#define AM243X_CPSW_BASE            0x08000000
#define AM243X_CPSW_SIZE            (256 * 1024)

/* PRU-ICSS (Industrial Communication Subsystem) */
#define AM243X_PRUICSS0_BASE        0x30000000
#define AM243X_PRUICSS0_SIZE        (512 * 1024)
#define AM243X_PRUICSS1_BASE        0x30080000
#define AM243X_PRUICSS1_SIZE        (512 * 1024)

/* OSPI Controller */
#define AM243X_OSPI_CTRL_BASE       0x0FC40000

/* System Control (CTRL_MMR) */
#define AM243X_CTRL_MMR_BASE        0x43000000
#define AM243X_CTRL_MMR_SIZE        (32 * 1024)

/* Mailbox (inter-core IPC) */
#define AM243X_MAILBOX_BASE         0x29000000

/* ======================== Interrupt Numbers ======================== */
/* Minimal set needed for emulation — from AM243x TRM interrupt map */
#define AM243X_IRQ_UART0            210
#define AM243X_IRQ_UART1            211
#define AM243X_IRQ_UART2            212
#define AM243X_IRQ_UART3            213
#define AM243X_IRQ_UART4            214
#define AM243X_IRQ_UART5            215
#define AM243X_IRQ_TIMER0           152
#define AM243X_IRQ_TIMER1           153
#define AM243X_IRQ_TIMER2           154
#define AM243X_IRQ_TIMER3           155
#define AM243X_IRQ_GPIO0            120
#define AM243X_IRQ_GPIO1            121
#define AM243X_IRQ_ADC0             130
#define AM243X_IRQ_MCSPI0           160
#define AM243X_IRQ_MCSPI1           161
#define AM243X_IRQ_I2C0             193
#define AM243X_IRQ_I2C1             194
#define AM243X_IRQ_RTI0             128
#define AM243X_IRQ_RTI1             129

/* ======================== SoC Variant Info ======================== */

typedef enum {
    AM243X_VARIANT_AM2434 = 0,  /* Nova: 2 MB SRAM, 2 MB flash, 9 UART, 5 SPI */
    AM243X_VARIANT_AM2432 = 1,  /* Orbit/Pulsar/Triton: 1.5 MB SRAM, 1.5 MB flash, 6 UART, 3 SPI */
} AM243xVariant;

typedef struct {
    AM243xVariant variant;
    uint32_t msram_size;
    uint32_t flash_size;
    int num_uarts;
    int num_spis;
    int num_gpio_pins;  /* approximate usable */
    const char *name;
} AM243xSoCInfo;

static const AM243xSoCInfo am2434_info = {
    .variant = AM243X_VARIANT_AM2434,
    .msram_size = AM2434_MSRAM_SIZE,
    .flash_size = AM2434_FLASH_SIZE,
    .num_uarts = 9,
    .num_spis = 5,
    .num_gpio_pins = 144,
    .name = "AM2434",
};

static const AM243xSoCInfo am2432_info = {
    .variant = AM243X_VARIANT_AM2432,
    .msram_size = AM2432_MSRAM_SIZE,
    .flash_size = AM2432_FLASH_SIZE,
    .num_uarts = 6,
    .num_spis = 3,
    .num_gpio_pins = 100,
    .name = "AM2432",
};

#endif /* HW_ARM_AM243X_H */
