# Constellation Nova — LP-AM2434 hardware build
# Based on TI MCU+ SDK FreeRTOS task_switch example for AM243x LaunchPad
#
# Build: gmake PROFILE=release
# Requires: CCS_PATH and SYSCFG_PATH env vars set

export MCU_PLUS_SDK_PATH?=C:/ti/mcu_plus_sdk_am243x_12_00_00_26
include $(MCU_PLUS_SDK_PATH)/imports.mak
include $(MCU_PLUS_SDK_PATH)/devconfig/devconfig.mak

CG_TOOL_ROOT=$(CGT_TI_ARM_CLANG_PATH)

CC=$(CG_TOOL_ROOT)/bin/tiarmclang
LNK=$(CG_TOOL_ROOT)/bin/tiarmclang
STRIP=$(CG_TOOL_ROOT)/bin/tiarmstrip
OBJCOPY=$(CG_TOOL_ROOT)/bin/tiarmobjcopy

ifeq ($(OS), Windows_NT)
	PYTHON=python
else
	PYTHON=python3
endif

PROFILE?=release
ConfigName:=$(PROFILE)

OUTNAME:=nova_lp.$(PROFILE).out
COREOUTNAME:=nova_lp.$(PROFILE).optishare.out

BOOTIMAGE_PATH=$(abspath .)
oeconfig?=None

BOOTIMAGE_NAME_MCELF:=nova_lp.$(PROFILE).mcelf
BOOTIMAGE_NAME_MCELF_HS:=nova_lp.$(PROFILE).mcelf.hs
BOOTIMAGE_NAME_HS_FS:=nova_lp.$(PROFILE).mcelf.hs_fs
# Watchdog (R5FSS1_0) ships as a separate signed appimage at OSPI
# offset 0x180000 — sbl_ospi_multi_partition expects each cluster's
# image at its own partition. Bundling both cores into one mcelf
# was tried in 0.A.59/60 and produced wild-PC crashes on R5FSS0_0
# at boot (SBL load-order side-effect). See
# /memories/repo/lp-am2434-watchdog-mcelf-trap.md.
WATCHDOG_BOOTIMAGE_NAME_MCELF:=watchdog.$(PROFILE).mcelf
WATCHDOG_BOOTIMAGE_NAME_HS_FS:=watchdog.$(PROFILE).mcelf.hs_fs
TARGETS := $(BOOTIMAGE_NAME_MCELF) $(WATCHDOG_BOOTIMAGE_NAME_MCELF)
ifeq ($(DEVICE_TYPE), HS)
   TARGETS += $(BOOTIMAGE_NAME_MCELF_HS)
else
   TARGETS += $(BOOTIMAGE_NAME_HS_FS) $(WATCHDOG_BOOTIMAGE_NAME_HS_FS)
endif

FILES_common := \
	main.c \
	lp_engine_shim.c \
	lp_watchdog_client.c \
	nova_states.c \
	nova_modes.c \
	nova_timer.c \
	nova_pwm.c \
	nova_serialshift.c \
	nova_warnings.c \
	nova_failures.c \
	nova_controls.c \
	lwip_smoke.c \
	orbit_client.c \
	lp_rtc.c \
	lp_settings_store.c \
	lp_settings.c \
	lp_remote_outside.c \
	lp_device_config.c \
	lp_ota_task.c \
	nova_fw_update.c \
	nova_ota_broker.c \
	hal_flash.c \
	lp_enet_dma_pause.c \
	lp_orbit_io.c \
	orbit_role.c \
	orbit_state.c \
	orbit_storage.c \
	orbit_modbus_tcp.c \
	orbit_safemode.c \
	orbit_sensor_rtu.c \
	orbit_gdc.c \
	orbit_triton.c \
	mb_rtu.c \
	adc_convert.c \
	nova_protocol.c \
	nova_cobs.c \
	nova_crc16.c \
	ti_drivers_config.c \
	ti_drivers_open_close.c \
	ti_board_config.c \
	ti_board_open_close.c \
	ti_dpl_config.c \
	ti_pinmux_config.c \
	ti_power_clock_config.c \
	ti_enet_config.c \
	ti_enet_dma_init.c \
	ti_enet_init.c \
	ti_enet_open_close.c \
	ti_enet_soc.c \
	ti_enet_lwipif.c \
	test.c \
	test_enet.c \
	test_enet_cpsw.c \
	udp_iperf.c \
	enetextphy.c \
	enetextphy_phymdio_dflt.c \
	dp83867.c \
	dp83869.c \
	generic_phy.c \

FILES_PATH_common = \
	.. \
	generated \
	../../Platform \
	../enet \
	../enet/extPhyMgmt \
	../orbit_server \

INCLUDES_common := \
	-I${CG_TOOL_ROOT}/include/c \
	-I${MCU_PLUS_SDK_PATH}/source \
	-I${MCU_PLUS_SDK_PATH}/source/board/ethphy/enet/rtos_drivers/include \
	-I${MCU_PLUS_SDK_PATH}/source/board/ethphy/port \
	-I${MCU_PLUS_SDK_PATH}/source/kernel/freertos/FreeRTOS-Kernel/include \
	-I${MCU_PLUS_SDK_PATH}/source/kernel/freertos/portable/TI_ARM_CLANG/ARM_CR5F \
	-I${MCU_PLUS_SDK_PATH}/source/kernel/freertos/config/am243x/r5f \
	-I${MCU_PLUS_SDK_PATH}/source/networking/enet \
	-I${MCU_PLUS_SDK_PATH}/source/networking/enet/core/utils/include \
	-I${MCU_PLUS_SDK_PATH}/source/networking/enet/core/utils/V3 \
	-I${MCU_PLUS_SDK_PATH}/source/networking/enet/core \
	-I${MCU_PLUS_SDK_PATH}/source/networking/enet/core/include \
	-I${MCU_PLUS_SDK_PATH}/source/networking/enet/core/include/phy \
	-I${MCU_PLUS_SDK_PATH}/source/networking/enet/core/include/core \
	-I${MCU_PLUS_SDK_PATH}/source/networking/enet/soc/k3/am64x_am243x \
	-I${MCU_PLUS_SDK_PATH}/source/networking/enet/hw_include \
	-I${MCU_PLUS_SDK_PATH}/source/networking/enet/hw_include/mdio/V4 \
	-I${MCU_PLUS_SDK_PATH}/source/networking/lwip/lwip-stack/src/include \
	-I${MCU_PLUS_SDK_PATH}/source/networking/lwip/lwip-port/include \
	-I${MCU_PLUS_SDK_PATH}/source/networking/lwip/lwip-port/freertos/include \
	-I${MCU_PLUS_SDK_PATH}/source/networking/enet/core/lwipif/inc \
	-I${MCU_PLUS_SDK_PATH}/source/networking/lwip/lwip-stack/contrib \
	-I${MCU_PLUS_SDK_PATH}/source/networking/lwip/lwip-config/am243x \
	-I../enet \
	-I../enet/extPhyMgmt \
	-Igenerated \
	-I../../Platform \
	-I../../Platform/include/legacy \
	-I../orbit_server \

DEFINES_common := \
	-DSOC_AM243X \
	-DENET_ENABLE_PER_CPSW=1 \
	-DOS_FREERTOS \

# Per-build role + IP override (set by Flash-LP.ps1 or `gmake CONFIG_NOVA_LP_ROLE=N CONFIG_NOVA_LP_IP=0xHEX`).
# When unset, the firmware boots with the compile-time defaults from lp_device_config.c
# (CONTROLLER, IP=0). When set, OSPI ping-pong overrides them on the next save.
ifdef CONFIG_NOVA_LP_ROLE
DEFINES_common += -DCONFIG_NOVA_LP_ROLE=$(CONFIG_NOVA_LP_ROLE)
endif
ifdef CONFIG_NOVA_LP_IP
DEFINES_common += -DCONFIG_NOVA_LP_IP=$(CONFIG_NOVA_LP_IP)
endif

CFLAGS_common := \
	-mcpu=cortex-r5 \
	-mfloat-abi=hard \
	-mfpu=vfpv3-d16 \
	-mthumb \
	-Wall \
	-Werror \
	-g \
	-Wno-gnu-variable-sized-type-not-at-end \
	-Wno-unused-function \
	-Wno-missing-braces \
	-Wno-logical-not-parentheses \
	-Wno-switch \
	-Wno-unused-but-set-variable \
	-Wno-implicit-const-int-float-conversion \
	-Wno-parentheses \

CFLAGS_debug := \
	-D_DEBUG_=1 \

CFLAGS_release := \
	-Os \

LNK_FILES_common = \
	generated/linker.cmd \

LIBS_PATH_common = \
	-Wl,-igenerated \
	-Wl,-i${MCU_PLUS_SDK_PATH}/source/kernel/freertos/lib \
	-Wl,-i${MCU_PLUS_SDK_PATH}/source/drivers/lib \
	-Wl,-i${MCU_PLUS_SDK_PATH}/source/board/lib \
	-Wl,-i${MCU_PLUS_SDK_PATH}/source/networking/enet/lib \
	-Wl,-i${MCU_PLUS_SDK_PATH}/source/networking/lwip/lib \
	-Wl,-i${CG_TOOL_ROOT}/lib \

LIBS_common = \
	-lfreertos.am243x.r5f.ti-arm-clang.${ConfigName}.lib \
	-ldrivers.am243x.r5f.ti-arm-clang.freertos.${ConfigName}.lib \
	-lenet-cpsw.am243x.r5f.ti-arm-clang.freertos.${ConfigName}.lib \
	-llwipif-cpsw-freertos.am243x.r5f.ti-arm-clang.${ConfigName}.lib \
	-llwip-freertos.am243x.r5f.ti-arm-clang.${ConfigName}.lib \
	-llwip-contrib-freertos.am243x.r5f.ti-arm-clang.${ConfigName}.lib \
	-lboard.am243x.r5f.ti-arm-clang.freertos.${ConfigName}.lib \
	-llibc.a \
	-llibsysbm.a \

LFLAGS_common = \
	-Wl,--diag_suppress=10063 \
	-Wl,--ram_model \
	-Wl,--reread_libs \
	-Wl,--gen_xml_func_hash \
	-Wl,--zero_init=on \
	-Wl,--use_memset=fast \
	-Wl,--use_memcpy=fast \

LIBS_NAME = \
	freertos.am243x.r5f.ti-arm-clang.${ConfigName}.lib \
	drivers.am243x.r5f.ti-arm-clang.freertos.${ConfigName}.lib \
	enet-cpsw.am243x.r5f.ti-arm-clang.freertos.${ConfigName}.lib \
	lwipif-cpsw-freertos.am243x.r5f.ti-arm-clang.${ConfigName}.lib \
	lwip-freertos.am243x.r5f.ti-arm-clang.${ConfigName}.lib \
	lwip-contrib-freertos.am243x.r5f.ti-arm-clang.${ConfigName}.lib \
	board.am243x.r5f.ti-arm-clang.freertos.${ConfigName}.lib \
	libc.a \
	libsysbm.a \

LIBS_PATH_NAME = \
	generated \
	${MCU_PLUS_SDK_PATH}/source/kernel/freertos/lib \
	${MCU_PLUS_SDK_PATH}/source/drivers/lib \
	${MCU_PLUS_SDK_PATH}/source/board/lib \
	${MCU_PLUS_SDK_PATH}/source/networking/enet/lib \
	${MCU_PLUS_SDK_PATH}/source/networking/lwip/lib \
	${CG_TOOL_ROOT}/lib \

FILES := $(FILES_common) $(FILES_$(PROFILE))
ASMFILES := $(ASMFILES_common) $(ASMFILES_$(PROFILE))
FILES_PATH := $(FILES_PATH_common) $(FILES_PATH_$(PROFILE))
CFLAGS := $(CFLAGS_common) $(CFLAGS_$(PROFILE)) $(CFLAGS_EXTRA)
DEFINES := $(DEFINES_common) $(DEFINES_$(PROFILE))
INCLUDES := $(INCLUDES_common) $(INCLUDE_$(PROFILE))
LIBS := $(LIBS_common) $(LIBS_$(PROFILE))
LIBS_PATH := $(LIBS_PATH_common) $(LIBS_PATH_$(PROFILE))
LFLAGS := $(LFLAGS_common) $(LFLAGS_$(PROFILE))
LNKOPTFLAGS := $(LNKOPTFLAGS_common) $(LNKOPTFLAGS_$(PROFILE))
LNK_FILES := $(LNK_FILES_common) $(LNK_FILES_$(PROFILE))

OBJDIR := obj/$(PROFILE)/
OBJS := $(FILES:%.c=%.obj)
OBJS += $(ASMFILES:%.S=%.obj)
DEPS := $(FILES:%.c=%.d)

vpath %.obj $(OBJDIR)
vpath %.c $(FILES_PATH)
vpath %.S $(FILES_PATH)
vpath %.lib $(LIBS_PATH_NAME)
vpath %.a $(LIBS_PATH_NAME)

$(OBJDIR)/%.obj %.obj: %.c
	@echo  Compiling: $(OUTNAME): $<
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) -MMD -o $(OBJDIR)/$@ $<

$(OBJDIR)/%.obj %.obj: %.S
	@echo  Compiling: $(OUTNAME): $<
	$(CC) -c $(CFLAGS) $(INCLUDES) $(DEFINES) -o $(OBJDIR)/$@ $<

all: $(TARGETS)

SYSCFG_GEN_FILES=generated/ti_drivers_config.c generated/ti_drivers_config.h
SYSCFG_GEN_FILES+=generated/ti_drivers_open_close.c generated/ti_drivers_open_close.h
SYSCFG_GEN_FILES+=generated/ti_dpl_config.c generated/ti_dpl_config.h
SYSCFG_GEN_FILES+=generated/ti_pinmux_config.c generated/ti_power_clock_config.c
SYSCFG_GEN_FILES+=generated/ti_board_config.c generated/ti_board_config.h
SYSCFG_GEN_FILES+=generated/ti_board_open_close.c generated/ti_board_open_close.h
SYSCFG_GEN_FILES+=generated/ti_enet_config.c generated/ti_enet_config.h
SYSCFG_GEN_FILES+=generated/ti_enet_dma_init.c generated/ti_enet_dma_init.h
SYSCFG_GEN_FILES+=generated/ti_enet_init.c
SYSCFG_GEN_FILES+=generated/ti_enet_open_close.c generated/ti_enet_open_close.h
SYSCFG_GEN_FILES+=generated/ti_enet_soc.c
SYSCFG_GEN_FILES+=generated/ti_enet_lwipif.c generated/ti_enet_lwipif.h

SYSTEM_FLAG ?= false

ifeq ($(SYSTEM_FLAG), false)
	SYSTEM_COMMAND := syscfg $(SYSCFG_GEN_FILES) $(OBJS) $(LNK_FILES) $(LIBS_NAME)
else
	SYSTEM_COMMAND := $(OBJS) $(LNK_FILES) $(LIBS_NAME)
endif

$(OUTNAME): $(SYSTEM_COMMAND)
	@echo  .
	@echo  Linking: $@ ...
	$(LNK) $(LNKOPTFLAGS) $(LFLAGS) $(LIBS_PATH) -Wl,-m=$(basename $@).map -o $@ $(addprefix $(OBJDIR), $(OBJS)) $(LIBS) $(LNK_FILES)
	@echo  Linking: $@ Done !!!
	@echo  .

clean:
	@echo  Cleaning: $(OUTNAME) ...
	$(RMDIR) $(OBJDIR)
	$(RM) $(OUTNAME)
	$(RM) $(BOOTIMAGE_NAME_MCELF)
	$(RM) $(BOOTIMAGE_NAME_HS_FS)
	$(RM) $(BOOTIMAGE_NAME_MCELF_HS)
	$(RM) $(WATCHDOG_BOOTIMAGE_NAME_MCELF)
	$(RM) $(WATCHDOG_BOOTIMAGE_NAME_HS_FS)
	$(RMDIR) generated/
	-$(MAKE) -C $(WATCHDOG_DIR) clean PROFILE=$(PROFILE)

scrub:
	@echo  Scrubing: $(OUTNAME) ...
	$(RMDIR) obj
ifeq ($(OS),Windows_NT)
	$(RM) \*.out
	$(RM) \*.map
	$(RM) \*.mcelf*
else
	$(RM) *.out
	$(RM) *.map
	$(RM) *.mcelf*
endif
	$(RMDIR) generated

$(OBJS): | $(OBJDIR)

$(OBJDIR):
	$(MKDIR) $@


.NOTPARALLEL:

.INTERMEDIATE: syscfg
$(SYSCFG_GEN_FILES): syscfg

ifeq ($(SYSTEM_FLAG), false)
syscfg: ../example.syscfg
	@echo Generating SysConfig files ...
	$(SYSCFG_NODE) $(SYSCFG_CLI_PATH)/dist/cli.js --product $(SYSCFG_SDKPRODUCT) --context r5fss0-0 --part ALX --package ALX --output generated/ ../example.syscfg
	@echo Patching DP83869 PHY config: enable TX clock shift @ 2000 ps
	$(PYTHON) ../patch_phy_cfg.py generated/ti_board_config.c
	@echo Patching ti_enet_open_close.c: add CPSW DMA-handle accessors for OTA-time pause
	$(PYTHON) ../patch_enet_accessor.py generated/ti_enet_open_close.c
endif

#
# Boot image generation
#
ifeq ($(OS),Windows_NT)
EXE_EXT=.exe
endif

BOOTIMAGE_CORE_ID_r5fss0-0 = 4
BOOTIMAGE_CORE_ID_r5fss1-0 = 6
SBL_RUN_ADDRESS=0x70000000
SBL_DEV_ID=55

# Independent dual-core watchdog (R5FSS1_0). Built by recursive sub-
# make in Nova_Firmware/lp_am2434/watchdog_core/ti-arm-clang/. The
# resulting watchdog.<profile>.out is bundled into the same multi-
# core ELF as the main core. See docs/lp-am2434-watchdog-design.md.
WATCHDOG_DIR     = ../watchdog_core/ti-arm-clang
WATCHDOG_OUTNAME = watchdog.$(PROFILE).out
WATCHDOG_OUT     = $(WATCHDOG_DIR)/$(WATCHDOG_OUTNAME)

APP_IMAGE_SIGN_CMD = $(MCU_PLUS_SDK_PATH)/source/security/security_common/tools/boot/signing/appimage_x509_cert_gen.py
MCELF_IMAGE_GEN = $(MCU_PLUS_SDK_PATH)/tools/boot/multicore-elf/genimage_am64x.py

.PHONY: watchdog
watchdog:
	@echo  Building watchdog core (R5FSS1_0) ...
	$(MAKE) -C $(WATCHDOG_DIR) PROFILE=$(PROFILE)

$(WATCHDOG_OUT): watchdog ;

$(BOOTIMAGE_NAME_MCELF): $(OUTNAME)
	@echo  Boot MulticoreELF image (main core): $(BOOTIMAGE_PATH)/$(BOOTIMAGE_NAME_MCELF) ...
	$(PYTHON) $(MCELF_IMAGE_GEN) \
		--core-img=$(BOOTIMAGE_CORE_ID_r5fss0-0):$(OUTNAME) \
		--output=$(BOOTIMAGE_NAME_MCELF) --xip=$(MCELF_XIP_RANGE)
	@echo  Boot MulticoreELF ELF image: $(BOOTIMAGE_PATH)/$(BOOTIMAGE_NAME_MCELF) Done !!!
	@echo  .

$(WATCHDOG_BOOTIMAGE_NAME_MCELF): $(WATCHDOG_OUT)
	@echo  Boot MulticoreELF image (watchdog core): $(BOOTIMAGE_PATH)/$(WATCHDOG_BOOTIMAGE_NAME_MCELF) ...
	$(PYTHON) $(MCELF_IMAGE_GEN) \
		--core-img=$(BOOTIMAGE_CORE_ID_r5fss1-0):$(WATCHDOG_OUT) \
		--output=$(WATCHDOG_BOOTIMAGE_NAME_MCELF) --xip=$(MCELF_XIP_RANGE)
	@echo  Boot MulticoreELF ELF image: $(BOOTIMAGE_PATH)/$(WATCHDOG_BOOTIMAGE_NAME_MCELF) Done !!!
	@echo  .

ifeq ($(DEVICE_TYPE), HS)
	$(PYTHON) $(APP_IMAGE_SIGN_CMD) --bin $(BOOTIMAGE_NAME_MCELF) --authtype 1 --key $(APP_SIGNING_KEY) --output $(BOOTIMAGE_NAME_MCELF_HS)
	$(RM) $(BOOTIMAGE_NAME_MCELF)
	@echo  Boot image: $(BOOTIMAGE_PATH)/$(BOOTIMAGE_NAME_MCELF_HS) Done !!!
	@echo  .
else
$(BOOTIMAGE_NAME_HS_FS): $(BOOTIMAGE_NAME_MCELF)
	$(PYTHON) $(APP_IMAGE_SIGN_CMD) --bin $(BOOTIMAGE_NAME_MCELF) --authtype 1 --key $(APP_SIGNING_KEY) --output $(BOOTIMAGE_NAME_HS_FS)
	@echo  Boot image: $(BOOTIMAGE_PATH)/$(BOOTIMAGE_NAME_HS_FS) Done !!!
	@echo  .

$(WATCHDOG_BOOTIMAGE_NAME_HS_FS): $(WATCHDOG_BOOTIMAGE_NAME_MCELF)
	$(PYTHON) $(APP_IMAGE_SIGN_CMD) --bin $(WATCHDOG_BOOTIMAGE_NAME_MCELF) --authtype 1 --key $(APP_SIGNING_KEY) --output $(WATCHDOG_BOOTIMAGE_NAME_HS_FS)
	@echo  Boot image: $(BOOTIMAGE_PATH)/$(WATCHDOG_BOOTIMAGE_NAME_HS_FS) Done !!!
	@echo  .
endif

-include $(addprefix $(OBJDIR)/, $(DEPS))
