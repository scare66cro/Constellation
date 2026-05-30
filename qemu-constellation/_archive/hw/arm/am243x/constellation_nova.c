/*
 * Constellation Nova — AM2434 Cortex-R5F Central Controller
 *
 * Machine model for QEMU: wires up dual Cortex-R5F CPU, on-chip flash,
 * MSRAM, UARTs, GPIO stubs, and Ethernet placeholder.  Nova is the
 * real-time brain — runs PID loops, state machines, VFD commands, and
 * polls all Orbit/Pulsar/Triton cards over Modbus TCP.
 *
 * Usage:
 *   qemu-system-arm -machine constellation-nova \
 *     -kernel nova_firmware.bin \
 *     -serial stdio \
 *     -serial tcp::9100,server,nowait \
 *     -serial tcp::9101,server,nowait
 *
 * serial0 = UART0 (debug console)
 * serial1 = UART1 (RPi5 bridge — ^tag=value$CRC! protocol)
 * serial2 = UART2 (RS-485 Port A — VFD Modbus RTU)
 * serial3 = UART3 (RS-485 Port B — legacy sensors / spare)
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/am243x.h"
#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/qdev-clock.h"
#include "hw/misc/unimp.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/char/serial.h"
#include "hw/char/serial-mm.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include <stdio.h>
#include "hw/loader.h"
#include "net/net.h"

/* ======================== Machine State ======================== */

typedef struct ConstellationNovaState {
    MachineState parent_obj;

    ARMCPU *cpu0;           /* R5F core 0 — control engine */
    ARMCPU *cpu1;           /* R5F core 1 — comms bridge */
    ARMCPU *cpu2;           /* R5F core 2 — safety watchdog */
    /* cpu3 reserved (spare / sleep) */

    MemoryRegion atcm;      /* 64 KB instruction TCM */
    MemoryRegion btcm;      /* 64 KB data TCM */
    MemoryRegion msram;     /* 2 MB on-chip SRAM (all cores share) */
    MemoryRegion flash;     /* 2 MB on-chip flash */
    MemoryRegion ospi_flash;/* External QSPI NOR (settings vault) */

    Clock *sysclk;

    /* Tick timer for FreeRTOS (no real DM Timer model yet) */
    QEMUTimer *tick_timer;
    qemu_irq cpu_irq;
    MemoryRegion tick_mmio;
    uint32_t tick_pending;
} ConstellationNovaState;

#define TYPE_CONSTELLATION_NOVA MACHINE_TYPE_NAME("constellation-nova")
OBJECT_DECLARE_SIMPLE_TYPE(ConstellationNovaState, CONSTELLATION_NOVA)

/* ======================== Machine Init ======================== */


/* ======================== Tick Timer ======================== */
/*
 * Minimal 1 kHz tick timer for FreeRTOS.  Raises the CPU IRQ line
 * once per millisecond.  The firmware clears the interrupt by reading
 * offset 0x00 of this device (mapped at TIMER0_BASE).
 */

#define NOVA_TICK_HZ  1000

static uint64_t nova_tick_read(void *opaque, hwaddr addr, unsigned size)
{
    ConstellationNovaState *s = opaque;
    if (addr == 0) {
        uint32_t val = s->tick_pending;
        s->tick_pending = 0;
        qemu_irq_lower(s->cpu_irq);
        return val;
    }
    return 0;
}

static void nova_tick_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    ConstellationNovaState *s = opaque;
    if (addr == 0 && (val & 1)) {
        s->tick_pending = 0;
        qemu_irq_lower(s->cpu_irq);
    }
}

static const MemoryRegionOps nova_tick_ops = {
    .read = nova_tick_read,
    .write = nova_tick_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void nova_tick_callback(void *opaque)
{
    ConstellationNovaState *s = opaque;
    s->tick_pending++;
    qemu_irq_raise(s->cpu_irq);

    /* Re-arm: 1kHz tick (1ms = 1,000,000 ns) */
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    timer_mod_ns(s->tick_timer, now + 1000000);

}


static void constellation_nova_init(MachineState *machine)
{
    ConstellationNovaState *s = CONSTELLATION_NOVA(machine);
    MemoryRegion *sys_mem = get_system_memory();
    const AM243xSoCInfo *soc = &am2434_info;

    /* System clock — 800 MHz for R5F */
    s->sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(s->sysclk, 800000000);

    /* ---- Memory regions ---- */

    /*
     * QEMU flat layout: firmware linker script puts everything at 0x0
     * in a contiguous 2 MB region.  Create one big RAM block at 0x0
     * that covers ATCM + code + data + BSS + heap.
     */
    memory_region_init_ram(&s->msram, NULL, "nova.ram",
                           soc->msram_size, &error_fatal);
    memory_region_add_subregion(sys_mem, 0, &s->msram);

    /* On-chip flash — firmware + config + VFD profiles */
    memory_region_init_rom(&s->flash, NULL, "nova.flash",
                           soc->flash_size, &error_fatal);
    memory_region_add_subregion(sys_mem, AM243X_FLASH_BASE, &s->flash);

    /* External QSPI NOR flash — settings vault (8 MB default).
     *
     * If the NOVA_OSPI_FILE environment variable is set, back the region
     * with that file so settings written by the firmware (ping-pong
     * banking in nova_settings_store.c) survive across QEMU restarts.
     * Otherwise fall back to anonymous RAM (volatile — settings reset
     * to defaults every boot). */
    {
        const char *ospi_path = getenv("NOVA_OSPI_FILE");
        if (ospi_path && ospi_path[0]) {
            memory_region_init_ram_from_file(&s->ospi_flash, NULL,
                                             "nova.ospi",
                                             8 * 1024 * 1024,
                                             4096,
                                             RAM_SHARED,
                                             ospi_path, 0,
                                             &error_fatal);
        } else {
            memory_region_init_ram(&s->ospi_flash, NULL, "nova.ospi",
                                   8 * 1024 * 1024, &error_fatal);
        }
    }
    memory_region_add_subregion(sys_mem, AM243X_OSPI_DATA_BASE,
                                &s->ospi_flash);

    /* ---- CPUs ---- */

    /*
     * AMP architecture — 3 independent R5F cores, each with its own
     * firmware image loaded at a different offset in shared MSRAM:
     *
     *   R5F-0: 0x00000000 – 0x0017FFFF (1536 KB) — Control engine
     *   R5F-1: 0x00180000 – 0x001BFFFF (256 KB)  — Comms bridge
     *   R5F-2: 0x001C0000 – 0x001DFFFF (128 KB)  — Safety watchdog
     *   IPC:   0x001E0000 – 0x001FFFFF (128 KB)  — Shared memory
     *
     * Each core's PC starts at the base of its partition.
     * SP is set to the top of its partition.
     */

    /* R5F-0 — control engine (primary) */
    s->cpu0 = ARM_CPU(object_new(machine->cpu_type));
    object_property_set_link(OBJECT(s->cpu0), "memory",
                             OBJECT(sys_mem), &error_fatal);
    qdev_realize(DEVICE(s->cpu0), NULL, &error_fatal);
    s->cpu0->env.regs[13] = 0x00180000;  /* SP at top of R5F-0 partition */
    s->cpu0->env.regs[15] = 0x00000000;  /* PC at base */

    /* R5F-1 — comms bridge */
    s->cpu1 = ARM_CPU(object_new(machine->cpu_type));
    object_property_set_link(OBJECT(s->cpu1), "memory",
                             OBJECT(sys_mem), &error_fatal);
    object_property_set_bool(OBJECT(s->cpu1), "start-powered-off",
                             true, &error_fatal);
    qdev_realize(DEVICE(s->cpu1), NULL, &error_fatal);
    s->cpu1->env.regs[13] = 0x001C0000;  /* SP at top of R5F-1 partition */
    s->cpu1->env.regs[15] = 0x00180000;  /* PC at base */

    /* R5F-2 — safety watchdog */
    s->cpu2 = ARM_CPU(object_new(machine->cpu_type));
    object_property_set_link(OBJECT(s->cpu2), "memory",
                             OBJECT(sys_mem), &error_fatal);
    object_property_set_bool(OBJECT(s->cpu2), "start-powered-off",
                             true, &error_fatal);
    qdev_realize(DEVICE(s->cpu2), NULL, &error_fatal);
    s->cpu2->env.regs[13] = 0x001E0000;  /* SP at top of R5F-2 partition */
    s->cpu2->env.regs[15] = 0x001C0000;  /* PC at base */

    /* ---- UARTs (16550-compatible) ---- */

    /*
     * AM243x UARTs are TI 16550-like. QEMU's serial_mm_init()
     * provides a functional 16550 model. We wire up the first
     * 4 UARTs for Nova:
     *
     *   UART0: debug console (serial0)
     *   UART1: RPi5 bridge (serial1)
     *   UART2: RS-485 Port A — VFDs (serial2)
     *   UART3: RS-485 Port B — spare (serial3)
     */

    /* UART0 — debug console */
    if (serial_hd(0)) {
        serial_mm_init(sys_mem, AM243X_UART0_BASE, 2,
                       NULL,
                       115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);
    } else {
        create_unimplemented_device("nova.uart0",
                                    AM243X_UART0_BASE, 0x1000);
    }

    /* UART1 — RPi5 bridge */
    if (serial_hd(1)) {
        serial_mm_init(sys_mem, AM243X_UART1_BASE, 2,
                       NULL,
                       230400, serial_hd(1), DEVICE_LITTLE_ENDIAN);
    } else {
        create_unimplemented_device("nova.uart1",
                                    AM243X_UART1_BASE, 0x1000);
    }

    /* UART2 — RS-485 Port A (VFD bus) */
    if (serial_hd(2)) {
        serial_mm_init(sys_mem, AM243X_UART2_BASE, 2,
                       NULL,
                       19200, serial_hd(2), DEVICE_LITTLE_ENDIAN);
    } else {
        create_unimplemented_device("nova.uart2",
                                    AM243X_UART2_BASE, 0x1000);
    }

    /* UART3 — RS-485 Port B (legacy / spare) */
    if (serial_hd(3)) {
        serial_mm_init(sys_mem, AM243X_UART3_BASE, 2,
                       NULL,
                       9600, serial_hd(3), DEVICE_LITTLE_ENDIAN);
    } else {
        create_unimplemented_device("nova.uart3",
                                    AM243X_UART3_BASE, 0x1000);
    }

    /* ---- Unimplemented peripheral stubs ---- */
    /* These log access but don't crash — filled in as firmware develops */

    create_unimplemented_device("nova.cpsw",
                                AM243X_CPSW_BASE, AM243X_CPSW_SIZE);
    create_unimplemented_device("nova.pruicss0",
                                AM243X_PRUICSS0_BASE, AM243X_PRUICSS0_SIZE);
    create_unimplemented_device("nova.pruicss1",
                                AM243X_PRUICSS1_BASE, AM243X_PRUICSS1_SIZE);
    create_unimplemented_device("nova.ctrl_mmr",
                                AM243X_CTRL_MMR_BASE, AM243X_CTRL_MMR_SIZE);
    create_unimplemented_device("nova.gpio0",
                                AM243X_GPIO0_BASE, 0x1000);
    create_unimplemented_device("nova.gpio1",
                                AM243X_GPIO1_BASE, 0x1000);
    create_unimplemented_device("nova.i2c0",
                                AM243X_I2C0_BASE, 0x1000);
    create_unimplemented_device("nova.i2c1",
                                AM243X_I2C1_BASE, 0x1000);
    create_unimplemented_device("nova.mcspi0",
                                AM243X_MCSPI0_BASE, 0x1000);
    create_unimplemented_device("nova.mcspi1",
                                AM243X_MCSPI1_BASE, 0x1000);
    /* Tick timer (replaces unimplemented timer0 stub) */
    s->cpu_irq = qdev_get_gpio_in(DEVICE(s->cpu0), ARM_CPU_IRQ);
    memory_region_init_io(&s->tick_mmio, NULL, &nova_tick_ops, s,
                          "nova.tick", 0x1000);
    memory_region_add_subregion(sys_mem, AM243X_TIMER0_BASE,
                                &s->tick_mmio);
    s->tick_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                 nova_tick_callback, s);
    
    timer_mod_ns(s->tick_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
    create_unimplemented_device("nova.timer1",
                                AM243X_TIMER1_BASE, 0x1000);
    create_unimplemented_device("nova.adc0",
                                AM243X_ADC0_BASE, 0x1000);
    create_unimplemented_device("nova.rti0",
                                AM243X_RTI0_BASE, 0x1000);
    create_unimplemented_device("nova.rti1",
                                AM243X_RTI1_BASE, 0x1000);
    create_unimplemented_device("nova.ospi_ctrl",
                                AM243X_OSPI_CTRL_BASE, 0x1000);
    create_unimplemented_device("nova.mailbox",
                                AM243X_MAILBOX_BASE, 0x1000);

    /* ---- Firmware loading ---- */
    /*
     * Load per-core firmware images into their respective MSRAM partitions.
     *
     * -kernel: R5F-0 control firmware → 0x00000000
     *
     * Secondary core images are loaded via generic-loader or custom
     * properties. For now, we support loading them from environment
     * or extra machine properties.  The Makefile will produce:
     *   nova_firmware.bin     → R5F-0 at 0x00000000
     *   nova_comms.bin        → R5F-1 at 0x00180000
     *   nova_watchdog.bin     → R5F-2 at 0x001C0000
     */
    if (machine->kernel_filename) {
        load_image_targphys(machine->kernel_filename,
                            0, 0x00180000);  /* R5F-0 partition */
    }

    /* Secondary images loaded via -device loader,file=...,addr=... in QEMU CLI.
     * Power on secondary cores after images are loaded. */
    if (s->cpu1) {
        cpu_set_pc(CPU(s->cpu1), 0x00180000);
    }
    if (s->cpu2) {
        cpu_set_pc(CPU(s->cpu2), 0x001C0000);
    }
}

static void constellation_nova_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Agristar Constellation Nova (AM2434 4× Cortex-R5F AMP)";
    mc->init = constellation_nova_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-r5f");
    mc->default_ram_size = AM2434_MSRAM_SIZE;
    mc->min_cpus = 1;
    mc->max_cpus = 4;   /* R5F-0 control, R5F-1 comms, R5F-2 watchdog, R5F-3 spare */
}

static const TypeInfo constellation_nova_info = {
    .name = TYPE_CONSTELLATION_NOVA,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(ConstellationNovaState),
    .class_init = constellation_nova_class_init,
};

static void constellation_nova_register_types(void)
{
    type_register_static(&constellation_nova_info);
}

type_init(constellation_nova_register_types)
