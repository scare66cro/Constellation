# Makefile for Agristar AS2 firmware — GCC cross-compilation for QEMU
#
# Usage:
#   make              — build firmware.bin
#   make clean        — remove build artifacts
#   make flash        — (placeholder) flash to QEMU
#
# This builds the Mini_IO firmware with arm-none-eabi-gcc, using
# the GCC FreeRTOS port, GCC startup, and GCC linker script.

# Toolchain
PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc
LD      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
OBJDUMP = $(PREFIX)objdump
SIZE    = $(PREFIX)size

# Project root
PROJ    = .

# Output
BUILD   = build
TARGET  = firmware

# Linker script
LDSCRIPT = tm4c129e_gcc.ld

# CPU flags for Cortex-M4 with hardware FPU
CPU_FLAGS = \
	-mcpu=cortex-m4 \
	-mthumb \
	-mfloat-abi=hard \
	-mfpu=fpv4-sp-d16

# Defines
DEFS = \
	-Dgcc \
	-DPART_TM4C129ENCPDT \
	-DSNOWFLAKE \
	-DNO_SHA256 \
	-DAPP_BASE=0x00000000 \
	-DQEMU_BUILD

# Include paths
INCS = \
	-I$(PROJ)/Application \
	-I$(PROJ)/Platform \
	-I$(PROJ)/Platform/Crypto \
	-I$(PROJ)/Platform/Drivers \
	-I$(PROJ)/Platform/Drivers/inc \
	-I$(PROJ)/Platform/Filesystems \
	-I$(PROJ)/Platform/Filesystems/HTTP_fs \
	-I$(PROJ)/Platform/Filesystems/KFS \
	-I$(PROJ)/Platform/misc \
	-I$(PROJ)/Platform/Network \
	-I$(PROJ)/Platform/Network/Servers \
	-I$(PROJ)/Platform/Network/axTLS \
	-I$(PROJ)/Platform/Network/axTLS/crypto \
	-I$(PROJ)/Platform/Network/axTLS/ssl \
	-I$(PROJ)/Platform/Network/lwip-1.4.1/src/include \
	-I$(PROJ)/Platform/Network/lwip-1.4.1/src/include/ipv4 \
	-I$(PROJ)/Platform/Network/lwip-1.4.1/src/include/ipv6 \
	-I$(PROJ)/Platform/Network/lwip-1.4.1/ports/tiva-tm4c129/include \
	-I$(PROJ)/Platform/OS \
	-I$(PROJ)/Platform/OS/FreeRTOS/Source/include \
	-I$(PROJ)/Platform/OS/FreeRTOS/Source/portable/GCC/ARM_CM4F \
	-I$(PROJ)/Platform/queues \
	-I$(PROJ)/Platform/utils

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

# Linker flags
LDFLAGS = \
	$(CPU_FLAGS) \
	-T $(LDSCRIPT) \
	-Wl,--gc-sections \
	-Wl,-Map=$(BUILD)/$(TARGET).map \
	--specs=nano.specs \
	--specs=nosys.specs \
	-u _printf_float \
	-lm -lc -lgcc -lnosys

# ============================================================================
# Source files
# ============================================================================

# Application layer
APP_SRCS = \
	Application/Analog_Input.c \
	Application/Controls.c \
	Application/DataExc.c \
	Application/Failures.c \
	Application/LoadLogs.c \
	Application/Modes.c \
	Application/PIDLogs.c \
	Application/PWM.c \
	Application/RS485.c \
	Application/RTC.c \
	Application/SDCard.c \
	Application/SerialShift.c \
	Application/Settings.c \
	Application/States.c \
	Application/StorePostData.c \
	Application/SystemLogs.c \
	Application/ThreadFileReceive.c \
	Application/ThreadMonitor.c \
	Application/ThreadSerialCom.c \
	Application/ThreadSerialShift.c \
	Application/ThreadUIUpdate.c \
	Application/Timer.c \
	Application/UI_Messages.c \
	Application/Usart.c \
	Application/UserLogs.c \
	Application/Warnings.c

# Platform — startup + main
PLATFORM_SRCS = \
	Platform/startup_gcc.c \
	Platform/main.c \
	Platform/issi_flash_port.c \
	Platform/rom_shims.c

# Drivers (TivaWare driverlib)
DRIVERLIB_SRCS = \
	Platform/Drivers/driverlib/adc.c \
	Platform/Drivers/driverlib/can.c \
	Platform/Drivers/driverlib/comp.c \
	Platform/Drivers/driverlib/cpu.c \
	Platform/Drivers/driverlib/crc.c \
	Platform/Drivers/driverlib/eeprom.c \
	Platform/Drivers/driverlib/emac.c \
	Platform/Drivers/driverlib/flash.c \
	Platform/Drivers/driverlib/fpu.c \
	Platform/Drivers/driverlib/gpio.c \
	Platform/Drivers/driverlib/hibernate.c \
	Platform/Drivers/driverlib/i2c.c \
	Platform/Drivers/driverlib/interrupt.c \
	Platform/Drivers/driverlib/pwm.c \
	Platform/Drivers/driverlib/ssi.c \
	Platform/Drivers/driverlib/sw_crc.c \
	Platform/Drivers/driverlib/sysctl.c \
	Platform/Drivers/driverlib/sysexc.c \
	Platform/Drivers/driverlib/systick.c \
	Platform/Drivers/driverlib/timer.c \
	Platform/Drivers/driverlib/uart.c \
	Platform/Drivers/driverlib/udma.c \
	Platform/Drivers/driverlib/watchdog.c

# Platform drivers (non-driverlib)
DRIVERS_SRCS = \
	Platform/Drivers/flash.c \
	Platform/Drivers/issi_flash.c \
	Platform/Drivers/mc_rtc.c \
	Platform/Drivers/micron_flash.c \
	Platform/Drivers/pinout.c \
	Platform/Drivers/sound.c \
	Platform/Drivers/tiva_flash.c

# Crypto
CRYPTO_SRCS = \
	Platform/Crypto/aes.c \
	Platform/Crypto/crc.c \
	Platform/Crypto/des3.c \
	Platform/Crypto/hmac.c \
	Platform/Crypto/md5.c \
	Platform/Crypto/misc.c \
	Platform/Crypto/sha.c

# Filesystems
FS_SRCS = \
	Platform/Filesystems/HTTP_fs/http_fs.c \
	Platform/Filesystems/KFS/kfs.c \
	Platform/Filesystems/KFS/kfs_port.c

# Queues
QUEUE_SRCS = \
	Platform/queues/circular_queue.c \
	Platform/queues/data_queue.c \
	Platform/queues/pbuf_queue.c

# Misc
MISC_SRCS = \
	Platform/misc/debug.c \
	Platform/misc/net_params.c \
	Platform/misc/tools.c

# Utils (TivaWare utilities)
UTILS_SRCS = \
	Platform/utils/cmdline.c \
	Platform/utils/cpu_usage.c \
	Platform/utils/flash_pb.c \
	Platform/utils/lwiplib.c \
	Platform/utils/random.c \
	Platform/utils/ringbuf.c \
	Platform/utils/uartstdio.c \
	Platform/utils/ustdlib.c

# FreeRTOS — GCC port
FREERTOS_SRCS = \
	Platform/OS/FreeRTOS/Source/tasks.c \
	Platform/OS/FreeRTOS/Source/queue.c \
	Platform/OS/FreeRTOS/Source/list.c \
	Platform/OS/FreeRTOS/Source/croutine.c \
	Platform/OS/FreeRTOS/Source/timers.c \
	Platform/OS/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c \
	Platform/OS/FreeRTOS/Source/portable/MemMang/heap_2.c

# Network — servers (not used by application, excluded)
NET_SERVERS_SRCS =

# Network — axTLS
AXTLS_SRCS = \
	Platform/Network/axTLS/crypto/PORT_byte_swap.c \
	Platform/Network/axTLS/crypto/aes.c \
	Platform/Network/axTLS/crypto/bigint.c \
	Platform/Network/axTLS/crypto/crypto_misc.c \
	Platform/Network/axTLS/crypto/hmac.c \
	Platform/Network/axTLS/crypto/md5.c \
	Platform/Network/axTLS/crypto/rc4.c \
	Platform/Network/axTLS/crypto/rsa.c \
	Platform/Network/axTLS/crypto/sha1.c \
	Platform/Network/axTLS/crypto/sha256.c \
	Platform/Network/axTLS/crypto/sha384.c \
	Platform/Network/axTLS/crypto/sha512.c \
	Platform/Network/axTLS/ssl/asn1.c \
	Platform/Network/axTLS/ssl/gen_cert.c \
	Platform/Network/axTLS/ssl/loader.c \
	Platform/Network/axTLS/ssl/openssl.c \
	Platform/Network/axTLS/ssl/os_port.c \
	Platform/Network/axTLS/ssl/p12.c \
	Platform/Network/axTLS/ssl/tls1.c \
	Platform/Network/axTLS/ssl/tls1_clnt.c \
	Platform/Network/axTLS/ssl/tls1_svr.c \
	Platform/Network/axTLS/ssl/x509.c

# Network — lwIP 1.4.1
LWIP_SRCS = \
	Platform/Network/lwip-1.4.1/src/api/api_lib.c \
	Platform/Network/lwip-1.4.1/src/api/api_msg.c \
	Platform/Network/lwip-1.4.1/src/api/err.c \
	Platform/Network/lwip-1.4.1/src/api/netbuf.c \
	Platform/Network/lwip-1.4.1/src/api/netdb.c \
	Platform/Network/lwip-1.4.1/src/api/netifapi.c \
	Platform/Network/lwip-1.4.1/src/api/sockets.c \
	Platform/Network/lwip-1.4.1/src/api/tcpip.c \
	Platform/Network/lwip-1.4.1/src/core/def.c \
	Platform/Network/lwip-1.4.1/src/core/dhcp.c \
	Platform/Network/lwip-1.4.1/src/core/dns.c \
	Platform/Network/lwip-1.4.1/src/core/init.c \
	Platform/Network/lwip-1.4.1/src/core/mem.c \
	Platform/Network/lwip-1.4.1/src/core/memp.c \
	Platform/Network/lwip-1.4.1/src/core/netif.c \
	Platform/Network/lwip-1.4.1/src/core/pbuf.c \
	Platform/Network/lwip-1.4.1/src/core/raw.c \
	Platform/Network/lwip-1.4.1/src/core/stats.c \
	Platform/Network/lwip-1.4.1/src/core/sys.c \
	Platform/Network/lwip-1.4.1/src/core/tcp.c \
	Platform/Network/lwip-1.4.1/src/core/tcp_in.c \
	Platform/Network/lwip-1.4.1/src/core/tcp_out.c \
	Platform/Network/lwip-1.4.1/src/core/timers.c \
	Platform/Network/lwip-1.4.1/src/core/udp.c \
	Platform/Network/lwip-1.4.1/src/core/ipv4/autoip.c \
	Platform/Network/lwip-1.4.1/src/core/ipv4/icmp.c \
	Platform/Network/lwip-1.4.1/src/core/ipv4/igmp.c \
	Platform/Network/lwip-1.4.1/src/core/ipv4/inet.c \
	Platform/Network/lwip-1.4.1/src/core/ipv4/inet_chksum.c \
	Platform/Network/lwip-1.4.1/src/core/ipv4/ip.c \
	Platform/Network/lwip-1.4.1/src/core/ipv4/ip_addr.c \
	Platform/Network/lwip-1.4.1/src/core/ipv4/ip_frag.c \
	Platform/Network/lwip-1.4.1/src/core/snmp/asn1_dec.c \
	Platform/Network/lwip-1.4.1/src/core/snmp/asn1_enc.c \
	Platform/Network/lwip-1.4.1/src/core/snmp/mib2.c \
	Platform/Network/lwip-1.4.1/src/core/snmp/mib_structs.c \
	Platform/Network/lwip-1.4.1/src/core/snmp/msg_in.c \
	Platform/Network/lwip-1.4.1/src/core/snmp/msg_out.c \
	Platform/Network/lwip-1.4.1/src/netif/etharp.c \
	Platform/Network/lwip-1.4.1/ports/tiva-tm4c129/netif/tiva-tm4c129.c \
	Platform/Network/lwip-1.4.1/ports/tiva-tm4c129/perf.c \
	Platform/Network/lwip-1.4.1/ports/tiva-tm4c129/sys_arch.c

# All sources
SRCS = \
	$(APP_SRCS) \
	$(PLATFORM_SRCS) \
	$(DRIVERLIB_SRCS) \
	$(DRIVERS_SRCS) \
	$(CRYPTO_SRCS) \
	$(FS_SRCS) \
	$(QUEUE_SRCS) \
	$(MISC_SRCS) \
	$(UTILS_SRCS) \
	$(FREERTOS_SRCS) \
	$(NET_SERVERS_SRCS) \
	$(AXTLS_SRCS) \
	$(LWIP_SRCS)

# Object files
OBJS = $(addprefix $(BUILD)/,$(SRCS:.c=.o))
DEPS = $(OBJS:.o=.d)

# ============================================================================
# Rules
# ============================================================================

.PHONY: all clean size

all: $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).elf size

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	@echo "  LD    $@"
	@$(LD) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	@echo "  BIN   $@"
	@$(OBJCOPY) -O binary $< $@

size: $(BUILD)/$(TARGET).elf
	@echo "  SIZE"
	@$(SIZE) $<

# Compile C files
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD)

-include $(DEPS)
