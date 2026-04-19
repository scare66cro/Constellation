/*
 * Nova Firmware — AM2434 Cortex-R5F Port of AS2 Grain Storage Controller
 *
 * This is the Constellation Nova firmware, ported from the TM4C129 AS2
 * firmware (Mini_IO/).  The Application/ layer is SHARED — referenced
 * directly from ../Mini_IO/Application/ via the Makefile.  The Platform/
 * layer is completely rewritten for the AM2434 Cortex-R5F.
 *
 * The original TM4C firmware in Mini_IO/ is NOT modified.
 *
 * Directory layout:
 *
 *   Nova_Firmware/
 *   ├── README.c              ← this file
 *   ├── Makefile              ← GCC cross-build (references ../Mini_IO/Application/)
 *   ├── am2434_r5f.ld         ← Linker script (ATCM + BTCM + MSRAM + flash)
 *   └── Platform/
 *       ├── main.c            ← Nova entry point (identical thread structure)
 *       ├── startup_r5f.c     ← Cortex-R5F startup + exception vector table
 *       ├── hal.h             ← HAL interface + TivaWare compatibility shims
 *       ├── hal_uart.c        ← UART (16550 register-level, QEMU compatible)
 *       ├── hal_gpio.c        ← GPIO (AM243x register model)
 *       ├── hal_timer.c       ← Timer (R5F PMU cycle counter)
 *       ├── hal_spi.c         ← SPI (stub — McSPI/OSPI TBD)
 *       ├── hal_flash.c       ← QSPI NOR flash (settings vault)
 *       ├── hal_watchdog.c    ← Watchdog (RTI stub)
 *       ├── pinout.c          ← AM2434 pin initialization
 *       ├── pinout.h          ← Pin definitions (same _pin_str structure)
 *       ├── system.h          ← System globals (wraps hal.h)
 *       ├── debug.h/.c        ← Debug printf → UART0
 *       └── FreeRTOSConfig.h  ← FreeRTOS config for R5F @ 800 MHz
 *
 * Key porting decisions:
 *
 *   1. Application/ code compiles UNCHANGED. The hal.h header provides
 *      compatibility macros that map TivaWare calls (UARTCharPut, etc.)
 *      to the Nova HAL functions.
 *
 *   2. The UART driver uses raw 16550 registers (not MCU+ SDK) so it
 *      works on both QEMU and real hardware without SDK dependencies.
 *
 *   3. FreeRTOS needs the ARM_CR5 port (not ARM_CM4F). The Cortex-R5F
 *      has no NVIC or SysTick — it uses VIM + DMTIMER instead.
 *
 *   4. Startup is fundamentally different: Cortex-M uses a vector table
 *      of function pointers; Cortex-R uses branch instructions at fixed
 *      exception offsets (0x00, 0x04, 0x08, etc.).
 *
 *   5. Memory layout: ATCM at 0x0 (vectors + startup), MSRAM at 0x70000000
 *      (code + data), BTCM at 0x41010000 (fast stacks).
 *
 * Build:   make
 * Run:     make run  (requires constellation-nova QEMU machine)
 * Clean:   make clean
 */
