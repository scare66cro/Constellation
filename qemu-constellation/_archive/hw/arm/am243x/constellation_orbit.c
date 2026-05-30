/*
 * Constellation Orbit — AM2432 Cortex-R5F I/O Card
 *
 * Machine model for QEMU: wires up dual Cortex-R5F CPU, on-chip flash,
 * MSRAM, UARTs, and peripheral stubs.  Orbit is the remote I/O card
 * that runs a Modbus TCP server, responding to Nova's polls.
 *
 * The same machine model is used for all three I/O card types:
 *   - Orbit:  relay outputs (DRV8908) + sensor inputs
 *   - Pulsar: stepper outputs (TMC2240) + position tracking
 *   - Triton: compressor staging (DRV8908) + pressure safety
 *
 * They share identical hardware (AM2432 + DP83826 PHYs). The firmware
 * variant is selected by a build flag, not different silicon.
 *
 * Usage:
 *   qemu-system-arm -machine constellation-orbit \
 *     -kernel orbit_firmware.bin \
 *     -serial stdio
 *
 * serial0 = UART0 (debug console)
 * serial1 = UART1 (optional — RS-485 sensors on some Orbit variants)
 *
 * Networking:
 *   Modbus TCP server on port 502 — Nova polls this card.
 *   In emulation, we use QEMU user-mode networking (-nic user)
 *   with port forwarding to expose the Modbus server.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/am243x.h"
#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/boards.h"
#include "hw/qdev-clock.h"
#include "hw/misc/unimp.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/char/serial.h"
#include "hw/char/serial-mm.h"
#include "hw/irq.h"
#include "net/net.h"

/* ======================== Machine State ======================== */

typedef struct ConstellationOrbitState {
    MachineState parent_obj;

    ARMCPU *cpu0;           /* R5F core 0 — Modbus server + I/O */

    MemoryRegion atcm;      /* 64 KB instruction TCM */
    MemoryRegion btcm;      /* 64 KB data TCM */
    MemoryRegion msram;     /* 1.5 MB on-chip SRAM */
    MemoryRegion flash;     /* 1.5 MB on-chip flash */

    Clock *sysclk;
} ConstellationOrbitState;

#define TYPE_CONSTELLATION_ORBIT MACHINE_TYPE_NAME("constellation-orbit")
OBJECT_DECLARE_SIMPLE_TYPE(ConstellationOrbitState, CONSTELLATION_ORBIT)

/* ======================== Machine Init ======================== */

static void constellation_orbit_init(MachineState *machine)
{
    ConstellationOrbitState *s = CONSTELLATION_ORBIT(machine);
    MemoryRegion *sys_mem = get_system_memory();
    const AM243xSoCInfo *soc = &am2432_info;

    /* System clock — 800 MHz for R5F */
    s->sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(s->sysclk, 800000000);

    /* ---- Memory regions ---- */

    /* ATCM — firmware boots here */
    memory_region_init_ram(&s->atcm, NULL, "orbit.atcm",
                           AM243X_R5F_ATCM_SIZE, &error_fatal);
    memory_region_add_subregion(sys_mem, AM243X_R5F_ATCM_BASE, &s->atcm);

    /* BTCM — data TCM */
    memory_region_init_ram(&s->btcm, NULL, "orbit.btcm",
                           AM243X_R5F_BTCM_SIZE, &error_fatal);
    memory_region_add_subregion(sys_mem, AM243X_R5F_BTCM_BASE, &s->btcm);

    /* MSRAM — 1.5 MB for AM2432 */
    memory_region_init_ram(&s->msram, NULL, "orbit.msram",
                           soc->msram_size, &error_fatal);
    memory_region_add_subregion(sys_mem, AM243X_MSRAM_BASE, &s->msram);

    /* On-chip flash — 1.5 MB */
    memory_region_init_rom(&s->flash, NULL, "orbit.flash",
                           soc->flash_size, &error_fatal);
    memory_region_add_subregion(sys_mem, AM243X_FLASH_BASE, &s->flash);

    /* ---- CPU ---- */
    s->cpu0 = ARM_CPU(object_new(machine->cpu_type));
    object_property_set_link(OBJECT(s->cpu0), "memory",
                             OBJECT(sys_mem), &error_fatal);
    qdev_realize(DEVICE(s->cpu0), NULL, &error_fatal);

    /* ---- UARTs ---- */
    /* Orbit has 6 UARTs but we only wire the first 2 */

    /* UART0 — debug console */
    if (serial_hd(0)) {
        serial_mm_init(sys_mem, AM243X_UART0_BASE, 2,
                       qdev_get_gpio_in(DEVICE(s->cpu0), AM243X_IRQ_UART0),
                       115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);
    }

    /* UART1 — optional RS-485 sensor bus */
    if (serial_hd(1)) {
        serial_mm_init(sys_mem, AM243X_UART1_BASE, 2,
                       qdev_get_gpio_in(DEVICE(s->cpu0), AM243X_IRQ_UART1),
                       9600, serial_hd(1), DEVICE_LITTLE_ENDIAN);
    }

    /* ---- Unimplemented peripheral stubs ---- */

    create_unimplemented_device("orbit.cpsw",
                                AM243X_CPSW_BASE, AM243X_CPSW_SIZE);
    create_unimplemented_device("orbit.pruicss0",
                                AM243X_PRUICSS0_BASE, AM243X_PRUICSS0_SIZE);
    create_unimplemented_device("orbit.ctrl_mmr",
                                AM243X_CTRL_MMR_BASE, AM243X_CTRL_MMR_SIZE);
    create_unimplemented_device("orbit.gpio0",
                                AM243X_GPIO0_BASE, 0x1000);
    create_unimplemented_device("orbit.gpio1",
                                AM243X_GPIO1_BASE, 0x1000);
    create_unimplemented_device("orbit.i2c0",
                                AM243X_I2C0_BASE, 0x1000);
    create_unimplemented_device("orbit.mcspi0",
                                AM243X_MCSPI0_BASE, 0x1000);
    create_unimplemented_device("orbit.mcspi1",
                                AM243X_MCSPI1_BASE, 0x1000);
    create_unimplemented_device("orbit.timer0",
                                AM243X_TIMER0_BASE, 0x1000);
    create_unimplemented_device("orbit.timer1",
                                AM243X_TIMER1_BASE, 0x1000);
    create_unimplemented_device("orbit.adc0",
                                AM243X_ADC0_BASE, 0x1000);
    create_unimplemented_device("orbit.rti0",
                                AM243X_RTI0_BASE, 0x1000);
    create_unimplemented_device("orbit.mailbox",
                                AM243X_MAILBOX_BASE, 0x1000);

    /* ---- Firmware loading ---- */
    armv7m_load_kernel(s->cpu0, machine->kernel_filename,
                       0, AM243X_R5F_ATCM_SIZE);
}

static void constellation_orbit_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Agristar Constellation Orbit (AM2432 Cortex-R5F)";
    mc->init = constellation_orbit_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-r5f");
    mc->default_ram_size = AM2432_MSRAM_SIZE;
    mc->min_cpus = 1;
    mc->max_cpus = 1;
}

static const TypeInfo constellation_orbit_info = {
    .name = TYPE_CONSTELLATION_ORBIT,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(ConstellationOrbitState),
    .class_init = constellation_orbit_class_init,
};

static void constellation_orbit_register_types(void)
{
    type_register_static(&constellation_orbit_info);
}

type_init(constellation_orbit_register_types)
