/*
 * startup_r5f_minimal.c — Minimal Cortex-R5F startup for secondary cores
 *
 * Used by R5F-1 (comms) and R5F-2 (watchdog). No FreeRTOS, no VFP,
 * just enough to zero BSS, set up stacks, and call core_main().
 *
 * Each secondary core image defines its own core_main() entry point.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

/* ---- External symbols from linker script ---- */
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;
extern uint32_t _isr_stack_top;

/* ---- Application entry (defined per-core) ---- */
extern int core_main(void);

/* ---- Forward declarations ---- */
void Reset_Handler(void);
static void Default_Handler(void);

/* ============================================================
 * Exception Vector Table
 * ============================================================ */

__attribute__((section(".vectors"), naked))
void _vector_table(void)
{
    __asm volatile(
        "LDR PC, =Reset_Handler     \n"   /* 0x00: Reset */
        "LDR PC, =Default_Handler   \n"   /* 0x04: Undefined */
        "LDR PC, =Default_Handler   \n"   /* 0x08: SVC */
        "LDR PC, =Default_Handler   \n"   /* 0x0C: Prefetch Abort */
        "LDR PC, =Default_Handler   \n"   /* 0x10: Data Abort */
        "NOP                        \n"   /* 0x14: Reserved */
        "LDR PC, =Default_Handler   \n"   /* 0x18: IRQ */
        "LDR PC, =Default_Handler   \n"   /* 0x1C: FIQ */
    );
}

/* ============================================================
 * Reset Handler — minimal
 * ============================================================ */

void __attribute__((noreturn)) Reset_Handler(void)
{
    /* Set up Supervisor stack */
    __asm volatile(
        "CPS #0x13          \n"   /* Supervisor mode */
        "LDR SP, =_estack   \n"
    );

    /* IRQ mode stack (minimal) */
    __asm volatile(
        "CPS #0x12          \n"   /* IRQ mode */
        "LDR SP, =_isr_stack_top \n"
    );

    /* Back to Supervisor */
    __asm volatile(
        "CPS #0x13          \n"
        "LDR SP, =_estack   \n"
    );

    /* Zero BSS */
    uint32_t *p = &_sbss;
    while (p < &_ebss) {
        *p++ = 0;
    }

    /* Call core-specific main */
    core_main();

    /* Should never return */
    while (1) {
        __asm volatile("WFI");
    }
}

/* ============================================================
 * Default exception handler — spin
 * ============================================================ */

static void __attribute__((used)) Default_Handler(void)
{
    while (1) {}
}
