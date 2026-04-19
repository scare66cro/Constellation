# Makefile for Constellation Nova firmware — AM2434 Cortex-R5F
#
# Usage:
#   make              — build nova_firmware.bin
#   make clean        — remove build artifacts
#   make run          — run in QEMU constellation-nova machine
#
# This builds the Nova firmware by combining:
#   - Platform/  — AM2434 HAL, startup, FreeRTOS (R5F port)
#   - Application/ — shared with Mini_IO (symlinked or copied)
#
# The Application/ source files are referenced from ../Mini_IO/Application/
# so the original TM4C firmware is never modified.

# Toolchain
PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc
LD      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
OBJDUMP = $(PREFIX)objdump
SIZE    = $(PREFIX)size

# Directories
PROJ       = .
# Mini_IO lives in the legacy AgriStar2Testing tree.  Override on the
# command line or via env if it lives somewhere else, e.g.
#   make MINI_IO_DIR=/path/to/Mini_IO
MINI_IO_DIR ?= /mnt/f/Agristar/Agristar/Mini_IO
APP_DIR    = $(MINI_IO_DIR)/Application
PLATFORM   = Platform
BUILD      = build
TARGET     = nova_firmware

# Linker script — use QEMU AMP layout for development (multi-core)
LDSCRIPT   = am2434_qemu_r5f0.ld

# CPU flags for Cortex-R5F with VFPv3-D16
# Note: softfp uses the hardware FPU for all float math (same perf as hard)
# but passes float args in integer registers — required for compatibility
# with newlib nano's varargs (snprintf %.1f etc).
# IMPORTANT: Must use -marm; Thumb mode breaks FreeRTOS context restore
# on QEMU constellation-nova (movs pc,lr doesn't handle T-bit properly).
CPU_FLAGS = \
	-mcpu=cortex-r5 \
	-marm \
	-mfloat-abi=softfp \
	-mfpu=vfpv3-d16

# Defines
DEFS = \
	-Dgcc \
	-DPART_AM2434 \
	-DQEMU_BUILD \
	-DCONSTELLATION_NOVA \
	-DSNOWFLAKE \
	-DNO_SHA256 \
	-DAPP_BASE=0x00000000

# Include paths — Platform first (so our hal.h/system.h/pinout.h win),
# then Application, then Mini_IO Platform dirs for any remaining headers
INCS = \
	-I$(PROJ)/$(PLATFORM) \
	-I$(APP_DIR) \
	-I$(APP_DIR)/../Platform \
	-I$(APP_DIR)/../Platform/Crypto \
	-I$(APP_DIR)/../Platform/Drivers \
	-I$(APP_DIR)/../Platform/Drivers/inc \
	-I$(APP_DIR)/../Platform/Filesystems \
	-I$(APP_DIR)/../Platform/Filesystems/HTTP_fs \
	-I$(APP_DIR)/../Platform/Filesystems/KFS \
	-I$(APP_DIR)/../Platform/misc \
	-I$(APP_DIR)/../Platform/Network \
	-I$(APP_DIR)/../Platform/Network/lwip-1.4.1/src/include \
	-I$(APP_DIR)/../Platform/Network/lwip-1.4.1/src/include/ipv4 \
	-I$(APP_DIR)/../Platform/Network/lwip-1.4.1/ports/tiva-tm4c129/include \
	-I$(APP_DIR)/../Platform/OS \
	-I$(APP_DIR)/../Platform/OS/FreeRTOS/Source/include \
	-I$(APP_DIR)/../Platform/OS/FreeRTOS/Source/portable/GCC/ARM_CR5 \
	-I$(APP_DIR)/../Platform/queues \
	-I$(APP_DIR)/../Platform/utils

# C flags
CFLAGS = \
	$(CPU_FLAGS) \
	$(DEFS) \
	$(INCS) \
	-std=c99 \
	-Wall \
	-Wno-unused-variable \
	-Wno-unused-but-set-variable \
	-Wno-missing-braces \
	-Wno-strict-aliasing \
	-Wno-int-conversion \
	-Wno-incompatible-pointer-types \
	-Wno-pointer-sign \
	-ffunction-sections \
	-fdata-sections \
	-fno-common \
	-Os

# Linker flags — libraries must come AFTER objects
LDFLAGS_PRE = \
	$(CPU_FLAGS) \
	-T $(LDSCRIPT) \
	-Wl,--gc-sections \
	-Wl,--allow-multiple-definition \
	-Wl,-Map=$(BUILD)/$(TARGET).map \
	--specs=nano.specs \
	--specs=nosys.specs \
	-u _printf_float

LDFLAGS_POST = -lm -lc -lgcc -lnosys

# ============================================================================
# Source files
# ============================================================================

# Nova Platform layer (new — AM2434 specific)
NOVA_PLATFORM_SRCS = \
	$(PLATFORM)/startup_r5f.c \
	$(PLATFORM)/main.c \
	$(PLATFORM)/nova_debug.c \
	$(PLATFORM)/pinout.c \
	$(PLATFORM)/hal_uart.c \
	$(PLATFORM)/hal_gpio.c \
	$(PLATFORM)/hal_timer.c \
	$(PLATFORM)/hal_spi.c \
	$(PLATFORM)/hal_flash.c \
	$(PLATFORM)/hal_flash_compat.c \
	$(PLATFORM)/hal_adc.c \
	$(PLATFORM)/hal_watchdog.c \
	$(PLATFORM)/hal_modbus_tcp.c \
	$(PLATFORM)/hal_orbit.c \
	$(PLATFORM)/hal_pwm.c \
	$(PLATFORM)/nova_thread_overrides.c \
	$(PLATFORM)/nova_hw_stubs.c \
	$(PLATFORM)/nova_cobs.c \
	$(PLATFORM)/nova_crc16.c \
	$(PLATFORM)/nova_protocol.c \
	$(PLATFORM)/nova_messages.c \
	$(PLATFORM)/nova_dataexc.c \
	$(PLATFORM)/nova_fw_update.c \
	$(PLATFORM)/nova_settings_store.c

# Application layer — SHARED from Mini_IO (not copied, referenced directly)
APP_SRCS = \
	$(APP_DIR)/Analog_Input.c \
	$(APP_DIR)/Controls.c \
	$(APP_DIR)/Failures.c \
	$(APP_DIR)/LoadLogs.c \
	$(APP_DIR)/Modes.c \
	$(APP_DIR)/PIDLogs.c \
	$(APP_DIR)/PWM.c \
	$(APP_DIR)/RS485.c \
	$(APP_DIR)/RTC.c \
	$(APP_DIR)/SDCard.c \
	$(APP_DIR)/SerialShift.c \
	$(APP_DIR)/Settings.c \
	$(APP_DIR)/States.c \
	$(APP_DIR)/StorePostData.c \
	$(APP_DIR)/SystemLogs.c \
	$(APP_DIR)/ThreadFileReceive.c \
	$(APP_DIR)/ThreadMonitor.c \
	$(APP_DIR)/ThreadSerialCom.c \
	$(APP_DIR)/ThreadUIUpdate.c \
	$(APP_DIR)/Timer.c \
	$(APP_DIR)/Usart.c \
	$(APP_DIR)/UserLogs.c \
	$(APP_DIR)/Warnings.c

# FreeRTOS — Cortex-R5 port (NOT Cortex-M4F)
# Note: The ARM_CR5 port must be obtained from FreeRTOS distribution
# and placed in the appropriate directory, or use MCU+ SDK's FreeRTOS.
FREERTOS_SRCS = \
	$(APP_DIR)/../Platform/OS/FreeRTOS/Source/tasks.c \
	$(APP_DIR)/../Platform/OS/FreeRTOS/Source/queue.c \
	$(APP_DIR)/../Platform/OS/FreeRTOS/Source/list.c \
	$(APP_DIR)/../Platform/OS/FreeRTOS/Source/croutine.c \
	$(APP_DIR)/../Platform/OS/FreeRTOS/Source/timers.c \
	$(APP_DIR)/../Platform/OS/FreeRTOS/Source/portable/MemMang/heap_2.c \
	$(APP_DIR)/../Platform/OS/FreeRTOS/Source/portable/GCC/ARM_CR5/port.c

FREERTOS_ASM = \
	$(APP_DIR)/../Platform/OS/FreeRTOS/Source/portable/GCC/ARM_CR5/portASM.S

# Shared platform utilities (crypto, filesystem, queues — mostly portable)
SHARED_PLATFORM_SRCS = \
	$(APP_DIR)/../Platform/Crypto/crc.c \
	$(APP_DIR)/../Platform/Filesystems/KFS/kfs.c \
	$(APP_DIR)/../Platform/Filesystems/KFS/kfs_port.c \
	$(APP_DIR)/../Platform/queues/circular_queue.c \
	$(APP_DIR)/../Platform/queues/data_queue.c \
	$(APP_DIR)/../Platform/misc/tools.c \
	$(APP_DIR)/../Platform/utils/ringbuf.c \
	$(APP_DIR)/../Platform/utils/ustdlib.c

# All sources
SRCS = $(NOVA_PLATFORM_SRCS) $(APP_SRCS) $(FREERTOS_SRCS) $(SHARED_PLATFORM_SRCS)
ASM_SRCS = $(FREERTOS_ASM)

# Object files (put everything in build/ with directory structure flattened)
OBJS = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRCS)))
OBJS += $(patsubst %.S,$(BUILD)/%.o,$(notdir $(ASM_SRCS)))
OBJS := $(OBJS)

# VPATH for source lookup
VPATH = $(sort $(dir $(SRCS)))

# ============================================================================
# Build rules
# ============================================================================

.PHONY: all clean run size

all: $(BUILD)/$(TARGET).bin $(BUILD)/nova_comms.bin $(BUILD)/nova_watchdog.bin

$(BUILD)/$(TARGET).elf: $(OBJS)
	@echo "[LD] $@"
	@$(LD) $(LDFLAGS_PRE) -o $@ $^ $(LDFLAGS_POST)

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	@echo "[BIN] $@"
	@$(OBJCOPY) -O binary $< $@
	@$(SIZE) $<

$(BUILD)/%.o: %.c
	@mkdir -p $(BUILD)
	@echo "[CC] $(notdir $<)"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.S
	@mkdir -p $(BUILD)
	@echo "[AS] $(notdir $<)"
	@$(AS) $(CFLAGS) -c -o $@ $<

# ============================================================================
# R5F-1: Communications Bridge (bare-metal, no FreeRTOS)
# ============================================================================

COMMS_SRCS = $(PLATFORM)/startup_r5f_minimal.c $(PLATFORM)/nova_comms_core.c
COMMS_OBJS = $(BUILD)/startup_r5f_minimal_comms.o $(BUILD)/nova_comms_core.o
COMMS_LDSCRIPT = am2434_qemu_r5f1.ld
COMMS_LDFLAGS = $(CPU_FLAGS) -T $(COMMS_LDSCRIPT) -Wl,--gc-sections \
	-Wl,-Map=$(BUILD)/nova_comms.map --specs=nano.specs --specs=nosys.specs -lm -lc -lgcc -lnosys

$(BUILD)/startup_r5f_minimal_comms.o: $(PLATFORM)/startup_r5f_minimal.c
	@mkdir -p $(BUILD)
	@echo "[CC] startup_r5f_minimal.c (comms)"
	@$(CC) $(CPU_FLAGS) -I$(PLATFORM) -std=c99 -Os -ffunction-sections -fdata-sections -fno-common -c -o $@ $<

$(BUILD)/nova_comms_core.o: $(PLATFORM)/nova_comms_core.c
	@mkdir -p $(BUILD)
	@echo "[CC] nova_comms_core.c"
	@$(CC) $(CPU_FLAGS) -I$(PLATFORM) -std=c99 -Os -ffunction-sections -fdata-sections -fno-common -c -o $@ $<

$(BUILD)/nova_comms.elf: $(COMMS_OBJS)
	@echo "[LD] $@ (R5F-1 comms)"
	@$(LD) $(COMMS_LDFLAGS) -o $@ $^

$(BUILD)/nova_comms.bin: $(BUILD)/nova_comms.elf
	@echo "[BIN] $@"
	@$(OBJCOPY) -O binary $< $@
	@$(SIZE) $<

# ============================================================================
# R5F-2: Safety Watchdog (bare-metal, no FreeRTOS)
# ============================================================================

WDG_SRCS = $(PLATFORM)/startup_r5f_minimal.c $(PLATFORM)/nova_watchdog_core.c
WDG_OBJS = $(BUILD)/startup_r5f_minimal_wdg.o $(BUILD)/nova_watchdog_core.o
WDG_LDSCRIPT = am2434_qemu_r5f2.ld
WDG_LDFLAGS = $(CPU_FLAGS) -T $(WDG_LDSCRIPT) -Wl,--gc-sections \
	-Wl,-Map=$(BUILD)/nova_watchdog.map --specs=nano.specs --specs=nosys.specs -lm -lc -lgcc -lnosys

$(BUILD)/startup_r5f_minimal_wdg.o: $(PLATFORM)/startup_r5f_minimal.c
	@mkdir -p $(BUILD)
	@echo "[CC] startup_r5f_minimal.c (watchdog)"
	@$(CC) $(CPU_FLAGS) -I$(PLATFORM) -std=c99 -Os -ffunction-sections -fdata-sections -fno-common -c -o $@ $<

$(BUILD)/nova_watchdog_core.o: $(PLATFORM)/nova_watchdog_core.c
	@mkdir -p $(BUILD)
	@echo "[CC] nova_watchdog_core.c"
	@$(CC) $(CPU_FLAGS) -I$(PLATFORM) -std=c99 -Os -ffunction-sections -fdata-sections -fno-common -c -o $@ $<

$(BUILD)/nova_watchdog.elf: $(WDG_OBJS)
	@echo "[LD] $@ (R5F-2 watchdog)"
	@$(LD) $(WDG_LDFLAGS) -o $@ $^

$(BUILD)/nova_watchdog.bin: $(BUILD)/nova_watchdog.elf
	@echo "[BIN] $@"
	@$(OBJCOPY) -O binary $< $@
	@$(SIZE) $<

clean:
	@if exist $(BUILD) rmdir /s /q $(BUILD)

size: $(BUILD)/$(TARGET).elf
	@$(SIZE) -A $<

# Run in QEMU — multi-core AMP
# R5F-0 via -kernel, R5F-1 and R5F-2 via generic-loader
QEMU ?= qemu-system-arm
run: $(BUILD)/$(TARGET).bin $(BUILD)/nova_comms.bin $(BUILD)/nova_watchdog.bin
	$(QEMU) -machine constellation-nova \
		-kernel $(BUILD)/$(TARGET).bin \
		-device loader,file=$(BUILD)/nova_comms.bin,addr=0x00180000,cpu-num=1 \
		-device loader,file=$(BUILD)/nova_watchdog.bin,addr=0x001C0000,cpu-num=2 \
		-serial stdio \
		-serial tcp::9100,server,nowait \
		-serial tcp::9101,server,nowait \
		-serial tcp::9102,server,nowait \
		-nographic
