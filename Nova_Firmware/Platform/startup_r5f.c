/*
 * startup_r5f.c — Cortex-R5F startup code for AM2434 (Constellation Nova)
 *
 * Unlike Cortex-M (vector table with function pointers), Cortex-R uses
 * an exception vector table with branch instructions at fixed offsets:
 *
 *   0x00: Reset
 *   0x04: Undefined Instruction
 *   0x08: Software Interrupt (SVC)
 *   0x0C: Prefetch Abort
 *   0x10: Data Abort
 *   0x14: Reserved
 *   0x18: IRQ
 *   0x1C: FIQ
 *
 * The Reset_Handler initializes TCM, copies .data, zeros .bss, sets up
 * stacks for each exception mode, then calls main().
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>
#include "hal.h"

/* ---- External symbols from linker script ---- */
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;        /* Top of BTCM — supervisor stack */
extern uint32_t _isr_stack_top; /* ISR stack in BTCM */

/* ---- Application entry ---- */
extern int main(void);
extern void __libc_init_array(void);

/* ---- IRQ handler (FreeRTOS will install its own) ---- */
extern void FreeRTOS_IRQ_Handler(void);
extern void FreeRTOS_SVC_Handler(void);

/* ---- Forward declarations ---- */
void Reset_Handler(void);
static void Undefined_Handler(void);
static void Prefetch_Abort_Handler(void);
static void Data_Abort_Handler(void);
static void Default_IRQ_Handler(void);
static void Default_FIQ_Handler(void);

/* ============================================================
 * Exception Vector Table — placed at 0x00000000 in ATCM
 *
 * Each entry is a 32-bit ARM branch instruction (LDR PC, [PC, #24])
 * that jumps to the handler address stored in the literal pool.
 * GCC inline assembly generates these.
 * ============================================================ */

__attribute__((section(".vectors"), naked))
void _vector_table(void)
{
    __asm volatile(
        "LDR PC, =Reset_Handler          \n"   /* 0x00: Reset */
        "LDR PC, =Undefined_Handler      \n"   /* 0x04: Undefined Instruction */
        "LDR PC, =FreeRTOS_SVC_Handler   \n"   /* 0x08: SVC (FreeRTOS) */
        "LDR PC, =Prefetch_Abort_Handler \n"   /* 0x0C: Prefetch Abort */
        "LDR PC, =Data_Abort_Handler     \n"   /* 0x10: Data Abort */
        "NOP                             \n"   /* 0x14: Reserved */
        "LDR PC, =FreeRTOS_IRQ_Handler   \n"   /* 0x18: IRQ (FreeRTOS) */
        "LDR PC, =Default_FIQ_Handler    \n"   /* 0x1C: FIQ */
    );
}

/* ============================================================
 * Reset Handler
 * ============================================================ */

void __attribute__((noreturn)) Reset_Handler(void)
{
    /*
     * Step 1: Set up stacks for each ARM exception mode.
     * The R5F starts in Supervisor mode after reset.
     * We configure stacks in BTCM for fast exception handling.
     */

    /* IRQ mode stack */
    __asm volatile(
        "CPS #0x12          \n"   /* Switch to IRQ mode */
        "LDR SP, =_isr_stack_top \n"
    );

    /* FIQ mode stack (share with IRQ for now) */
    __asm volatile(
        "CPS #0x11          \n"   /* Switch to FIQ mode */
        "LDR SP, =_isr_stack_top \n"
    );

    /* Abort mode stack */
    __asm volatile(
        "CPS #0x17          \n"   /* Switch to Abort mode */
        "LDR SP, =_isr_stack_top \n"
    );

    /* Undefined mode stack */
    __asm volatile(
        "CPS #0x1B          \n"   /* Switch to Undefined mode */
        "LDR SP, =_isr_stack_top \n"
    );

    /* System/User mode stack (used by FreeRTOS tasks) */
    __asm volatile(
        "CPS #0x1F          \n"   /* Switch to System mode */
        "LDR SP, =_estack   \n"
    );

    /* Back to Supervisor mode for startup */
    __asm volatile(
        "CPS #0x13          \n"   /* Supervisor mode */
        "LDR SP, =_estack   \n"
    );

    /*
     * Step 2: Enable VFP/NEON (Cortex-R5F has VFPv3-D16)
     */
    __asm volatile(
        "MRC p15, 0, r0, c1, c0, 2  \n"   /* Read CPACR */
        "ORR r0, r0, #(0xF << 20)   \n"   /* Enable CP10 + CP11 full access */
        "MCR p15, 0, r0, c1, c0, 2  \n"   /* Write CPACR */
        "ISB                         \n"
        "MOV r0, #(1 << 30)         \n"   /* FPEXC.EN bit */
        "VMSR FPEXC, r0             \n"   /* Enable FPU */
        ::: "r0"
    );

    /*
     * Step 3: Zero .bss
     * (On R5F, .data is already in MSRAM from the loader,
     *  no flash→RAM copy needed when running from QEMU -kernel)
     */
    uint32_t *pDst = &_sbss;
    while (pDst < &_ebss) {
        *pDst++ = 0;
    }

    /*
     * Step 4: Call static constructors
     */
    __libc_init_array();

    /*
     * Step 5: Call main()
     */
    main();

    /* Should never return */
    while (1) {
        __asm volatile("WFI");
    }
}

/* ============================================================
 * Default exception handlers — loop forever (development)
 * With UART output for crash diagnosis.
 * ============================================================ */

static void __attribute__((used)) Undefined_Handler(void)
{
    const char *msg = "!!! UNDEFINED INSTRUCTION !!!\r\n";
    hal_uart_send(HAL_UART_DEBUG, (const uint8_t *)msg, 30);
    while (1) {}
}

static void __attribute__((used)) Prefetch_Abort_Handler(void)
{
    uint32_t ifar, ifsr, lr;
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(ifsr));
    __asm__ volatile("mov %0, lr" : "=r"(lr));

    static char abuf[128];
    static const char hex[] = "0123456789ABCDEF";
    int pos = 0;
    const char *hdr = "!!! PREFETCH ABORT !!!\r\n";
    for (int i = 0; hdr[i]; i++) abuf[pos++] = hdr[i];
    const char *l1 = "IFAR=";
    for (int i = 0; l1[i]; i++) abuf[pos++] = l1[i];
    for (int i = 28; i >= 0; i -= 4) abuf[pos++] = hex[(ifar >> i) & 0xF];
    abuf[pos++] = ' ';
    const char *l2 = "IFSR=";
    for (int i = 0; l2[i]; i++) abuf[pos++] = l2[i];
    for (int i = 28; i >= 0; i -= 4) abuf[pos++] = hex[(ifsr >> i) & 0xF];
    abuf[pos++] = ' ';
    const char *l3 = "LR=";
    for (int i = 0; l3[i]; i++) abuf[pos++] = l3[i];
    for (int i = 28; i >= 0; i -= 4) abuf[pos++] = hex[(lr >> i) & 0xF];
    abuf[pos++] = '\r';
    abuf[pos++] = '\n';
    hal_uart_send(HAL_UART_DEBUG, (const uint8_t *)abuf, (uint32_t)pos);
    while (1) {}
}

static void __attribute__((used)) Data_Abort_Handler(void)
{
    /* Read DFAR (Data Fault Address Register) and DFSR (Data Fault Status
     * Register) via MRC — these tell us WHAT address faulted and WHY. */
    uint32_t dfar, dfsr, lr;
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));
    __asm__ volatile("mov %0, lr" : "=r"(lr));

    /* Format a minimal crash report.  Can't use debug_printf here
     * (it requires FreeRTOS critical sections which may not work
     * in abort mode), so we format manually into a static buffer. */
    static char abuf[128];
    static const char hex[] = "0123456789ABCDEF";
    int pos = 0;

    /* "!!! DATA ABORT !!!\r\n" */
    const char *hdr = "!!! DATA ABORT !!!\r\n";
    for (int i = 0; hdr[i]; i++) abuf[pos++] = hdr[i];

    /* "DFAR=XXXXXXXX DFSR=XXXXXXXX LR=XXXXXXXX\r\n" */
    const char *lbl1 = "DFAR=";
    for (int i = 0; lbl1[i]; i++) abuf[pos++] = lbl1[i];
    for (int i = 28; i >= 0; i -= 4) abuf[pos++] = hex[(dfar >> i) & 0xF];

    abuf[pos++] = ' ';
    const char *lbl2 = "DFSR=";
    for (int i = 0; lbl2[i]; i++) abuf[pos++] = lbl2[i];
    for (int i = 28; i >= 0; i -= 4) abuf[pos++] = hex[(dfsr >> i) & 0xF];

    abuf[pos++] = ' ';
    const char *lbl3 = "LR=";
    for (int i = 0; lbl3[i]; i++) abuf[pos++] = lbl3[i];
    for (int i = 28; i >= 0; i -= 4) abuf[pos++] = hex[(lr >> i) & 0xF];

    abuf[pos++] = '\r';
    abuf[pos++] = '\n';

    hal_uart_send(HAL_UART_DEBUG, (const uint8_t *)abuf, (uint32_t)pos);
    while (1) {}
}

static void __attribute__((used)) Default_IRQ_Handler(void)
{
    while (1) {}
}

static void __attribute__((used)) Default_FIQ_Handler(void)
{
    while (1) {}
}

/*
 * Weak aliases so the linker doesn't error if FreeRTOS isn't linked yet.
 * Once FreeRTOS is included, its port.c provides the real implementations.
 */
__attribute__((weak)) void FreeRTOS_IRQ_Handler(void)
{
    Default_IRQ_Handler();
}

__attribute__((weak)) void FreeRTOS_SVC_Handler(void)
{
    while (1) {}
}
