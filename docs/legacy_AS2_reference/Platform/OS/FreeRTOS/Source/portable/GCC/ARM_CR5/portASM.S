/*
 * FreeRTOS portASM.S — GCC/ARM_CR5 context switch assembly
 *
 * Cortex-R5F context save/restore for FreeRTOS v7.
 *
 * Stack layout from pxPortInitialiseStack (bottom → top):
 *   ulCriticalNesting
 *   R0, R1, R2, ... R12, LR
 *   PC (return address)       ← RFEIA reads this as PC
 *   SPSR                      ← RFEIA reads this as CPSR
 *
 * Tasks run in System mode (0x1F).
 */

    .text
    .arm

    .extern pxCurrentTCB
    .extern ulCriticalNesting
    .extern vTaskSwitchContext
    .extern vPortTickHandler

    .global vPortRestoreTaskContext
    .global FreeRTOS_SVC_Handler
    .global FreeRTOS_IRQ_Handler

/*-----------------------------------------------------------*/

.macro portSAVE_CONTEXT
    stmdb   sp!, {r0-r12, lr}

    ldr     r2, =ulCriticalNesting
    ldr     r1, [r2]
    stmdb   sp!, {r1}

    ldr     r0, =pxCurrentTCB
    ldr     r1, [r0]
    str     sp, [r1]
.endm

.macro portRESTORE_CONTEXT
    /*
     * Load save frame base from TCB into R2 (not SP — we control
     * mode switching explicitly).  Pre-load return addr + SPSR into
     * SVC-mode LR and SPSR before popping general registers.
     * Final "movs pc, lr" in SVC mode atomically restores PC + CPSR.
     */
    ldr     r0, =pxCurrentTCB
    ldr     r1, [r0]
    ldr     r2, [r1]                /* r2 = saved SP (frame base) */

    /* Pop critical nesting (first word) */
    ldr     r0, [r2], #4            /* r0 = crit nesting, r2 → R0 slot */
    ldr     r1, =ulCriticalNesting
    str     r0, [r1]

    /* Pre-load return state from end of frame (R0-R12,LR = 56 bytes) */
    ldr     r0, [r2, #60]           /* r0 = saved CPSR (offset 56+4) */
    ldr     r1, [r2, #56]           /* r1 = return address (offset 56) */

    /* Set up SVC-mode SPSR and LR for the atomic return */
    cps     #0x13
    msr     spsr_cxsf, r0
    mov     lr, r1

    /* Switch to System mode, restore SP + registers from the frame */
    cps     #0x1F
    mov     sp, r2
    ldmia   sp!, {r0-r12}           /* Pop R0-R12 (52 bytes), SP+=52 */
    ldr     lr, [sp]                /* Restore task LR from stack */
    add     sp, sp, #12             /* Skip LR(4) + ret_addr(4) + SPSR(4) */

    /* Atomic return from SVC mode: PC←LR_svc, CPSR←SPSR_svc */
    cps     #0x13
    movs    pc, lr
.endm

/*-----------------------------------------------------------
 * vPortRestoreTaskContext
 *-----------------------------------------------------------*/
vPortRestoreTaskContext:
    portRESTORE_CONTEXT

/*-----------------------------------------------------------
 * FreeRTOS_SVC_Handler (portYIELD)
 *
 * Manual save: avoids SRSDB (unreliable on QEMU Cortex-R5).
 * Same approach as FreeRTOS_IRQ_Handler.
 * Note: LR_svc already points to instruction AFTER SVC — no -4.
 *-----------------------------------------------------------*/
FreeRTOS_SVC_Handler:
    /* Stash scratch on SVC stack */
    stmdb   sp!, {r0, r1}
    mov     r0, lr              /* r0 = return address (no -4 for SVC) */
    mrs     r1, spsr            /* r1 = task CPSR */

    /* Push return state onto System stack */
    cps     #0x1F
    stmdb   sp!, {r0, r1}       /* [return_addr, SPSR] on System stack */

    /* Restore original R0,R1 from SVC stack */
    cps     #0x13
    ldmia   sp!, {r0, r1}
    cps     #0x1F

    portSAVE_CONTEXT
    bl      vTaskSwitchContext
    portRESTORE_CONTEXT

/*-----------------------------------------------------------
 * FreeRTOS_IRQ_Handler (tick interrupt)
 *
 * Manual save: avoids SRSDB (unreliable on QEMU Cortex-R5).
 * Stashes LR_irq and SPSR_irq via R0,R1, pushes return state
 * onto the System stack, then saves/restores in System mode.
 *-----------------------------------------------------------*/
FreeRTOS_IRQ_Handler:
    sub     lr, lr, #4

    /* Stash scratch on IRQ stack */
    stmdb   sp!, {r0, r1}
    mov     r0, lr              /* r0 = return address */
    mrs     r1, spsr            /* r1 = task CPSR */

    /* Push return state onto System stack */
    cps     #0x1F
    stmdb   sp!, {r0, r1}       /* [return_addr, SPSR] on System stack */

    /* Restore original R0,R1 from IRQ stack */
    cps     #0x12
    ldmia   sp!, {r0, r1}
    cps     #0x1F

    portSAVE_CONTEXT

    bl      vPortTickHandler
    bl      vTaskSwitchContext

    portRESTORE_CONTEXT

    .end
