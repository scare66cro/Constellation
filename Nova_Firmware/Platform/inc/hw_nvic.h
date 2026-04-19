/* inc/hw_nvic.h — TivaWare NVIC register shim for AM2434 R5F */
#ifndef HW_NVIC_H_SHIM
#define HW_NVIC_H_SHIM

/* Cortex-M NVIC offsets — referenced by some Application code.
   On R5F these are meaningless but we provide them to compile. */
#define NVIC_INT_CTRL       0xE000ED04
#define NVIC_PENDSVSET      0x10000000
#define NVIC_APINT          0xE000ED0C
#define NVIC_APINT_VECTKEY  0x05FA0000
#define NVIC_APINT_SYSRESETREQ 0x00000004
#define NVIC_ST_CTRL        0xE000E010
#define NVIC_ST_RELOAD      0xE000E014
#define NVIC_ST_CURRENT     0xE000E018

#define NVIC_VTABLE         0xE000ED08

#endif
