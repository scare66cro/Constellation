/*
 * startup_gcc.c — GCC startup code for TM4C129ENCPDT
 *
 * Drop-in replacement for startup_ccs.c.
 * Declares the vector table, Reset_Handler that copies .data, zeros .bss,
 * and calls main(). Used for QEMU builds (no bootloader).
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <string.h>

/* ---- External symbols from linker script ---- */
extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

/* ---- Application entry ---- */
extern int main(void);
extern void __libc_init_array(void);

/* ---- Application ISR handlers ---- */
extern void Usart_ISR(void);
extern void lwIPEthernetIntHandler(void);
extern void xPortPendSVHandler(void);
extern void vPortSVCHandler(void);
extern void xPortSysTickHandler(void);
void prvGetRegistersFromStack(uint32_t *pulFaultStackAddress);

/* ---- Forward declarations ---- */
void Reset_Handler(void);
static void NMI_Handler(void);
static void Default_Handler(void);

/* ---- Hard fault handler (GCC) ---- */
void __attribute__((naked)) HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4         \n"
        "ite eq              \n"
        "mrseq r0, msp       \n"
        "mrsne r0, psp       \n"
        "ldr r1, [r0, #24]   \n"
        "b prvGetRegistersFromStack \n"
    );
}

/* ---- Vector table ---- */
__attribute__ ((section(".isr_vector"), used))
void (* const g_pfnVectors[])(void) = {
    (void (*)(void))((uint32_t)&_estack),    /*  0: Initial stack pointer */
    Reset_Handler,                            /*  1: Reset handler */
    NMI_Handler,                              /*  2: NMI handler */
    HardFault_Handler,                        /*  3: Hard fault handler */
    Default_Handler,                          /*  4: MPU fault handler */
    Default_Handler,                          /*  5: Bus fault handler */
    Default_Handler,                          /*  6: Usage fault handler */
    0,                                        /*  7: Reserved */
    0,                                        /*  8: Reserved */
    0,                                        /*  9: Reserved */
    0,                                        /* 10: Reserved */
    vPortSVCHandler,                          /* 11: SVCall handler */
    Default_Handler,                          /* 12: Debug monitor handler */
    0,                                        /* 13: Reserved */
    xPortPendSVHandler,                       /* 14: PendSV handler */
    xPortSysTickHandler,                      /* 15: SysTick handler */

    /* IRQ 0-15: GPIO ports and UARTs */
    Default_Handler,                          /* 16: GPIO Port A */
    Default_Handler,                          /* 17: GPIO Port B */
    Default_Handler,                          /* 18: GPIO Port C */
    Default_Handler,                          /* 19: GPIO Port D */
    Default_Handler,                          /* 20: GPIO Port E */
    Default_Handler,                          /* 21: UART0 */
    Usart_ISR,                                /* 22: UART1 Rx and Tx */
    Default_Handler,                          /* 23: SSI0 */
    Default_Handler,                          /* 24: I2C0 */
    Default_Handler,                          /* 25: PWM Fault */
    Default_Handler,                          /* 26: PWM Generator 0 */
    Default_Handler,                          /* 27: PWM Generator 1 */
    Default_Handler,                          /* 28: PWM Generator 2 */
    Default_Handler,                          /* 29: Quadrature Encoder 0 */
    Default_Handler,                          /* 30: ADC Sequence 0 */
    Default_Handler,                          /* 31: ADC Sequence 1 */
    Default_Handler,                          /* 32: ADC Sequence 2 */
    Default_Handler,                          /* 33: ADC Sequence 3 */
    Default_Handler,                          /* 34: Watchdog timer */
    Default_Handler,                          /* 35: Timer 0 subtimer A */
    Default_Handler,                          /* 36: Timer 0 subtimer B */
    Default_Handler,                          /* 37: Timer 1 subtimer A */
    Default_Handler,                          /* 38: Timer 1 subtimer B */
    Default_Handler,                          /* 39: Timer 2 subtimer A */
    Default_Handler,                          /* 40: Timer 2 subtimer B */
    Default_Handler,                          /* 41: Analog Comparator 0 */
    Default_Handler,                          /* 42: Analog Comparator 1 */
    Default_Handler,                          /* 43: Analog Comparator 2 */
    Default_Handler,                          /* 44: System Control (PLL/OSC/BO) */
    Default_Handler,                          /* 45: FLASH Control */
    Default_Handler,                          /* 46: GPIO Port F */
    Default_Handler,                          /* 47: GPIO Port G */
    Default_Handler,                          /* 48: GPIO Port H */
    Default_Handler,                          /* 49: UART2 */
    Default_Handler,                          /* 50: SSI1 */
    Default_Handler,                          /* 51: Timer 3 subtimer A */
    Default_Handler,                          /* 52: Timer 3 subtimer B */
    Default_Handler,                          /* 53: I2C1 */
    Default_Handler,                          /* 54: CAN0 */
    Default_Handler,                          /* 55: CAN1 */
    lwIPEthernetIntHandler,                   /* 56: Ethernet */
    Default_Handler,                          /* 57: Hibernate */
    Default_Handler,                          /* 58: USB0 */
    Default_Handler,                          /* 59: PWM Generator 3 */
    Default_Handler,                          /* 60: uDMA Software Transfer */
    Default_Handler,                          /* 61: uDMA Error */
    Default_Handler,                          /* 62: ADC1 Sequence 0 */
    Default_Handler,                          /* 63: ADC1 Sequence 1 */
    Default_Handler,                          /* 64: ADC1 Sequence 2 */
    Default_Handler,                          /* 65: ADC1 Sequence 3 */
    Default_Handler,                          /* 66: External Bus Interface 0 */
    Default_Handler,                          /* 67: GPIO Port J */
    Default_Handler,                          /* 68: GPIO Port K */
    Default_Handler,                          /* 69: GPIO Port L */
    Default_Handler,                          /* 70: SSI2 */
    Default_Handler,                          /* 71: SSI3 */
    Default_Handler,                          /* 72: UART3 */
    Default_Handler,                          /* 73: UART4 */
    Default_Handler,                          /* 74: UART5 */
    Default_Handler,                          /* 75: UART6 */
    Default_Handler,                          /* 76: UART7 */
    Default_Handler,                          /* 77: I2C2 */
    Default_Handler,                          /* 78: I2C3 */
    Default_Handler,                          /* 79: Timer 4 subtimer A */
    Default_Handler,                          /* 80: Timer 4 subtimer B */
    Default_Handler,                          /* 81: Timer 5 subtimer A */
    Default_Handler,                          /* 82: Timer 5 subtimer B */
    Default_Handler,                          /* 83: FPU */
    0,                                        /* 84: Reserved */
    0,                                        /* 85: Reserved */
    Default_Handler,                          /* 86: I2C4 */
    Default_Handler,                          /* 87: I2C5 */
    Default_Handler,                          /* 88: GPIO Port M */
    Default_Handler,                          /* 89: GPIO Port N */
    0,                                        /* 90: Reserved */
    Default_Handler,                          /* 91: Tamper */
    Default_Handler,                          /* 92: GPIO Port P (Summary or P0) */
    Default_Handler,                          /* 93: GPIO Port P1 */
    Default_Handler,                          /* 94: GPIO Port P2 */
    Default_Handler,                          /* 95: GPIO Port P3 */
    Default_Handler,                          /* 96: GPIO Port P4 */
    Default_Handler,                          /* 97: GPIO Port P5 */
    Default_Handler,                          /* 98: GPIO Port P6 */
    Default_Handler,                          /* 99: GPIO Port P7 */
    Default_Handler,                          /* 100: GPIO Port Q (Summary or Q0) */
    Default_Handler,                          /* 101: GPIO Port Q1 */
    Default_Handler,                          /* 102: GPIO Port Q2 */
    Default_Handler,                          /* 103: GPIO Port Q3 */
    Default_Handler,                          /* 104: GPIO Port Q4 */
    Default_Handler,                          /* 105: GPIO Port Q5 */
    Default_Handler,                          /* 106: GPIO Port Q6 */
    Default_Handler,                          /* 107: GPIO Port Q7 */
    Default_Handler,                          /* 108: GPIO Port R */
    Default_Handler,                          /* 109: GPIO Port S */
    Default_Handler,                          /* 110: SHA/MD5 */
    Default_Handler,                          /* 111: AES */
    Default_Handler,                          /* 112: DES3DES */
    Default_Handler,                          /* 113: LCD Controller */
    Default_Handler,                          /* 114: Timer 6 subtimer A */
    Default_Handler,                          /* 115: Timer 6 subtimer B */
    Default_Handler,                          /* 116: Timer 7 subtimer A */
    Default_Handler,                          /* 117: Timer 7 subtimer B */
    Default_Handler,                          /* 118: I2C6 */
    Default_Handler,                          /* 119: I2C7 */
    Default_Handler,                          /* 120: HIM Scan Matrix Keyboard */
    Default_Handler,                          /* 121: One Wire */
    Default_Handler,                          /* 122: HIM PS/2 */
    Default_Handler,                          /* 123: HIM LED Sequencer */
    Default_Handler,                          /* 124: HIM Consumer IR */
    Default_Handler,                          /* 125: I2C8 */
    Default_Handler,                          /* 126: I2C9 */
    Default_Handler,                          /* 127: GPIO Port T */
};

/*-----------------------------------------------------------*/

void __attribute__((noreturn)) Reset_Handler(void)
{
    uint32_t *pSrc, *pDst;

    /* Enable the FPU (Cortex-M4F).
     * Set CP10 and CP11 to Full Access in the CPACR register.
     * CPACR is at address 0xE000ED88. */
    volatile uint32_t *cpacr = (volatile uint32_t *)0xE000ED88;
    *cpacr |= (0xF << 20);  /* Set CP10, CP11 to full access (bits 23:20) */
    __asm volatile("dsb");
    __asm volatile("isb");

    /* Copy .data initializers from flash to SRAM */
    pSrc = &_sidata;
    pDst = &_sdata;
    while (pDst < &_edata) {
        *pDst++ = *pSrc++;
    }

    /* Zero fill .bss */
    pDst = &_sbss;
    while (pDst < &_ebss) {
        *pDst++ = 0;
    }

    /* Call static constructors / init_array */
    __libc_init_array();

    /* Call the application's entry point */
    main();

    /* Should never return */
    while (1) {}
}

/*-----------------------------------------------------------*/

static void NMI_Handler(void)
{
    while (1) {}
}

/*-----------------------------------------------------------*/

static void Default_Handler(void)
{
    while (1) {}
}

/*-----------------------------------------------------------*/

void prvGetRegistersFromStack(uint32_t *pulFaultStackAddress)
{
    /* These are volatile to try and prevent the compiler/linker optimising them
    away as the variables never actually get used. */
    volatile uint32_t r0;
    volatile uint32_t r1;
    volatile uint32_t r2;
    volatile uint32_t r3;
    volatile uint32_t r12;
    volatile uint32_t lr;  /* Link register. */
    volatile uint32_t pc;  /* Program counter. */
    volatile uint32_t psr; /* Program status register. */

    r0  = pulFaultStackAddress[0];
    r1  = pulFaultStackAddress[1];
    r2  = pulFaultStackAddress[2];
    r3  = pulFaultStackAddress[3];
    r12 = pulFaultStackAddress[4];
    lr  = pulFaultStackAddress[5];
    pc  = pulFaultStackAddress[6];
    psr = pulFaultStackAddress[7];

    (void)r0; (void)r1; (void)r2; (void)r3;
    (void)r12; (void)psr;

    /* Write fault info to QEMU debug registers at SYSCTL+0xFCx */
    volatile uint32_t *sysctl_dbg_pc   = (volatile uint32_t *)0x400FEFC4;
    volatile uint32_t *sysctl_dbg_lr   = (volatile uint32_t *)0x400FEFC8;
    volatile uint32_t *sysctl_dbg_cfsr = (volatile uint32_t *)0x400FEFCC;
    volatile uint32_t *sysctl_dbg_bfar = (volatile uint32_t *)0x400FEFD0;

    *sysctl_dbg_pc   = pc;
    *sysctl_dbg_lr   = lr;
    *sysctl_dbg_cfsr = *(volatile uint32_t *)0xE000ED28; /* CFSR */
    *sysctl_dbg_bfar = *(volatile uint32_t *)0xE000ED38; /* BFAR */

    /* When the following line is hit, the variables contain the register values. */
    for (;;) {}
}
