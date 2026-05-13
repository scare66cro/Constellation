/*
 * FreeRTOS V7.1.1 — GCC/ARM_CM4F port
 *
 * This is a GCC-compatible rewrite of the CCS/ARM_CM4F FreeRTOS port
 * for the TM4C129ENCPDT. Combines port.c + portasm.asm into one file.
 *
 * FreeRTOS is free software under the GPL v2 with the FreeRTOS exception.
 */

#include "FreeRTOS.h"
#include "task.h"

/* Constants used for hardware setup. */
#define portNVIC_SYSTICK_CTRL_REG           (*((volatile unsigned long *) 0xe000e010))
#define portNVIC_SYSTICK_LOAD_REG           (*((volatile unsigned long *) 0xe000e014))
#define portNVIC_SYSTICK_CURRENT_VALUE_REG  (*((volatile unsigned long *) 0xe000e018))
#define portNVIC_INT_CTRL_REG               (*((volatile unsigned long *) 0xe000ed04))
#define portNVIC_SYSPRI2_REG                (*((volatile unsigned long *) 0xe000ed20))
#define portNVIC_SYSTICK_CLK_BIT            (1UL << 2UL)
#define portNVIC_SYSTICK_INT_BIT            (1UL << 1UL)
#define portNVIC_SYSTICK_ENABLE_BIT         (1UL << 0UL)
#define portNVIC_SYSTICK_COUNT_FLAG_BIT     (1UL << 16UL)
#define portNVIC_PENDSVSET_BIT              (1UL << 28UL)
#define portMIN_INTERRUPT_PRIORITY          (255UL)
#define portNVIC_PENDSV_PRI                 (portMIN_INTERRUPT_PRIORITY << 16UL)
#define portNVIC_SYSTICK_PRI                (portMIN_INTERRUPT_PRIORITY << 24UL)

/* Constants for the FPU regs. */
#define portINITIAL_XPSR                    (0x01000000)
#define portINITIAL_EXEC_RETURN             (0xfffffffd)
#define portINITIAL_CONTROL_IF_UNPRIVILEGED (0x03)
#define portINITIAL_CONTROL_IF_PRIVILEGED   (0x02)

/* Offsets in the SCB for FPU registers. */
#define portFPCCR                           (*((volatile unsigned long *) 0xE000EF34))

/* FreeRTOS kernel variables. */
extern volatile void *volatile pxCurrentTCB;

/* Each task has a separate stack, and the scheduler saves context onto
   the stack. For the Cortex-M4F with FPU: we set FPCCR to lazy stacking,
   so the FPU registers are saved automatically by hardware on exception entry. */

static unsigned long uxCriticalNesting = 0xaaaaaaaa;

/*-----------------------------------------------------------*/

/* Helper: set basepri to configMAX_SYSCALL_INTERRUPT_PRIORITY */
static void prvPortSetBASEPRI(unsigned long ulNewMask)
{
    __asm volatile(
        "msr basepri, %0    \n"
        "isb                \n"
        "dsb                \n"
        :
        : "r" (ulNewMask)
        : "memory"
    );
}

/*-----------------------------------------------------------*/

/*
 * Setup the timer to generate the tick interrupts.
 */
static void prvSetupTimerInterrupt(void)
{
    /* Stop and reset SysTick. */
    portNVIC_SYSTICK_CTRL_REG = 0UL;
    portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;

    /* Configure SysTick to interrupt at the requested rate. */
    portNVIC_SYSTICK_LOAD_REG = (configCPU_CLOCK_HZ / configTICK_RATE_HZ) - 1UL;
    portNVIC_SYSTICK_CTRL_REG = portNVIC_SYSTICK_CLK_BIT |
                                portNVIC_SYSTICK_INT_BIT |
                                portNVIC_SYSTICK_ENABLE_BIT;
}

/*-----------------------------------------------------------*/

/*
 * Initialize the stack of a task to look exactly as if a call to
 * portSAVE_CONTEXT had been called.
 */
portSTACK_TYPE *pxPortInitialiseStack(portSTACK_TYPE *pxTopOfStack,
                                       pdTASK_CODE pxCode,
                                       void *pvParameters)
{
    /* Simulate the stack frame as it would be created by a context switch. */
    pxTopOfStack--;
    *pxTopOfStack = portINITIAL_XPSR;   /* xPSR */
    pxTopOfStack--;
    *pxTopOfStack = (portSTACK_TYPE) pxCode; /* PC */
    pxTopOfStack--;
    *pxTopOfStack = 0;                   /* LR */
    pxTopOfStack -= 5;                   /* R12, R3, R2, R1 */
    *pxTopOfStack = (portSTACK_TYPE) pvParameters; /* R0 */

    /* A save context on Cortex-M4 pushes: (hardware) xPSR, PC, LR, R12, R3-R0.
       Then (software) R4-R11 + LR (EXC_RETURN). */
    pxTopOfStack--;
    *pxTopOfStack = portINITIAL_EXEC_RETURN; /* EXC_RETURN */
    pxTopOfStack -= 8; /* R11, R10, R9, R8, R7, R6, R5, R4 */

    return pxTopOfStack;
}

/*-----------------------------------------------------------*/

void vPortYield(void)
{
    /* Set the PendSV to request a context switch. */
    portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;

    /* Barriers are used to ensure branch is taken. */
    __asm volatile("dsb");
    __asm volatile("isb");
}

/*-----------------------------------------------------------*/

void vPortYieldFromISR(void)
{
    portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;
}

/*-----------------------------------------------------------*/

void vPortEnterCritical(void)
{
    portDISABLE_INTERRUPTS();
    uxCriticalNesting++;

    /* Don't allow nesting from interrupts. */
    if (uxCriticalNesting == 1) {
        /* First nesting level = task context. That's expected. */
    }
}

/*-----------------------------------------------------------*/

void vPortExitCritical(void)
{
    uxCriticalNesting--;
    if (uxCriticalNesting == 0) {
        portENABLE_INTERRUPTS();
    }
}

/*-----------------------------------------------------------*/

void vPortSetInterruptMask(void)
{
    prvPortSetBASEPRI(configMAX_SYSCALL_INTERRUPT_PRIORITY);
}

/*-----------------------------------------------------------*/

void vPortClearInterruptMask(void)
{
    prvPortSetBASEPRI(0);
}

/*-----------------------------------------------------------*/

/*
 * Start the first task — triggers the first context switch via SVC.
 */
void __attribute__((naked)) vPortStartFirstTask(void)
{
    __asm volatile(
        "ldr r0, =0xE000ED08  \n" /* Use the NVIC offset register to locate stack. */
        "ldr r0, [r0]         \n"
        "ldr r0, [r0]         \n" /* Get the initial stack pointer from vector table. */
        "msr msp, r0          \n" /* Set MSP back to start of stack. */
        "cpsie i              \n" /* Enable interrupts. */
        "cpsie f              \n"
        "dsb                  \n"
        "isb                  \n"
        "svc 0                \n" /* System call to start first task. */
        "nop                  \n"
    );
}

/*-----------------------------------------------------------*/

void __attribute__((naked)) vPortSVCHandler(void)
{
    __asm volatile(
        "ldr r3, =pxCurrentTCB  \n" /* Restore the context. */
        "ldr r1, [r3]           \n" /* Get the pxCurrentTCB address. */
        "ldr r0, [r1]           \n" /* Get the top of stack from the first TCB member. */
        "ldmia r0!, {r4-r11, r14} \n" /* Pop the core registers. */
        "msr psp, r0            \n" /* Set the process stack pointer. */
        "isb                    \n"
        "mov r0, #0             \n"
        "msr basepri, r0        \n"
        "bx r14                 \n"
    );
}

/*-----------------------------------------------------------*/

void __attribute__((naked)) xPortPendSVHandler(void)
{
    __asm volatile(
        "mrs r0, psp            \n"
        "isb                    \n"
        "                       \n"
        "ldr r3, =pxCurrentTCB  \n" /* Get the address of pxCurrentTCB. */
        "ldr r2, [r3]           \n"
        "                       \n"
        "tst r14, #0x10         \n" /* Is the task using the FPU? */
        "it eq                  \n"
        "vstmdbeq r0!, {s16-s31}\n" /* Push high FPU registers. */
        "                       \n"
        "stmdb r0!, {r4-r11, r14}\n" /* Save remaining core regs. */
        "str r0, [r2]           \n" /* Save new top of stack. */
        "                       \n"
        "stmdb sp!, {r3}        \n"
        "mov r0, %0             \n"
        "msr basepri, r0        \n"
        "bl vTaskSwitchContext   \n"
        "mov r0, #0             \n"
        "msr basepri, r0        \n"
        "ldmia sp!, {r3}        \n"
        "                       \n"
        "ldr r1, [r3]           \n" /* New pxCurrentTCB. */
        "ldr r0, [r1]           \n" /* First item in new TCB = top of stack. */
        "                       \n"
        "ldmia r0!, {r4-r11, r14}\n" /* Pop core regs. */
        "                       \n"
        "tst r14, #0x10         \n" /* FPU? */
        "it eq                  \n"
        "vldmiaeq r0!, {s16-s31}\n" /* Pop high FPU regs. */
        "                       \n"
        "msr psp, r0            \n"
        "isb                    \n"
        "bx r14                 \n"
        :
        : "i" (configMAX_SYSCALL_INTERRUPT_PRIORITY)
        : "memory"
    );
}

/*-----------------------------------------------------------*/

void __attribute__((naked)) vPortEnableVFP(void)
{
    __asm volatile(
        "ldr.w r0, =0xE000ED88 \n" /* CPACR */
        "ldr r1, [r0]          \n"
        "orr r1, r1, #(0xF << 20) \n" /* Enable CP10 + CP11, full access. */
        "str r1, [r0]          \n"
        "bx r14                \n"
    );
}

/*-----------------------------------------------------------*/

extern void vPortEnableVFP(void);

portBASE_TYPE xPortStartScheduler(void)
{
    /* Set PendSV and SysTick priorities to lowest. */
    portNVIC_SYSPRI2_REG |= portNVIC_PENDSV_PRI;
    portNVIC_SYSPRI2_REG |= portNVIC_SYSTICK_PRI;

    /* Enable the FPU. */
    vPortEnableVFP();

    /* Set lazy stacking for FPU registers. */
    portFPCCR |= (0x3UL << 30UL); /* ASPEN | LSPEN */

    /* Initialize the critical nesting counter. */
    uxCriticalNesting = 0;

    /* Start the timer that generates the tick ISR. */
    prvSetupTimerInterrupt();

    /* Start the first task. */
    vPortStartFirstTask();

    /* Should never get here. */
    return 0;
}

/*-----------------------------------------------------------*/

void vPortEndScheduler(void)
{
    /* Not implemented. */
}

/*-----------------------------------------------------------*/

void xPortSysTickHandler(void)
{
    /* If using preemption, also force a context switch (matching CCS port). */
#if configUSE_PREEMPTION == 1
    portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT;
#endif

    /* Set interrupt mask before entering FreeRTOS API. */
    unsigned long ulPreviousMask;
    ulPreviousMask = portSET_INTERRUPT_MASK_FROM_ISR();
    {
        xTaskIncrementTick();

        /* Maintain uptime counters (matching the CCS port). */
        static unsigned int msCounter = 0;
        extern unsigned int uptime_sec;
        extern unsigned int uptime_ms;
        uptime_ms = xTaskGetTickCount() / portTICK_RATE_MS;

        if (++msCounter >= 1000U)
        {
            ++uptime_sec;
            msCounter = 0;
        }

        /* File-system timer (10ms resolution) */
        extern void kfs_timer(void);
        kfs_timer();
    }
    portCLEAR_INTERRUPT_MASK_FROM_ISR(ulPreviousMask);
}
