/*
 *  Copyright (C) 2018-2026 Texas Instruments Incorporated
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"
#include <drivers/sciclient.h>
#include <drivers/bootloader/soc/bootloader_soc.h>
#include <drivers/bootloader.h>
#include <drivers/bootloader/bootloader_flash.h>
#include <board/flash.h>
#include <kernel/dpl/ClockP.h>
#include "sbl_bank_select.h"  /* F2c bank selection */

/* This buffer needs to be defined for OSPI boot in case of HS device for
 * image decryption and authentication
 * Incase of am243x-lp only 256kb is available in RAM, so the encrypted image should be
 * less than 256kb.
 */
uint8_t gAppimage[0x40000] __attribute__ ((section (".app"), aligned (4096)));

/* call this API to stop the booting process and spin, do that you can connect
 * debugger, load symbols and then make the 'loop' variable as 0 to continue execution
 * with debugger connected.
 */
void loop_forever(void)
{
    volatile uint32_t loop = 1;
    while(loop)
        ;
}

int main(void)
{
    int32_t status;

    Bootloader_profileReset();

    Bootloader_socWaitForFWBoot();

#ifndef DISABLE_WARM_REST_WA
    /* Warm Reset Workaround to prevent CPSW register lockup */
    if (!Bootloader_socIsMCUResetIsoEnabled())
    {
        Bootloader_socResetWorkaround();
    }
#endif

    Bootloader_profileAddProfilePoint("SYSFW init");

    if (!Bootloader_socIsMCUResetIsoEnabled())
    {
        /* Update devGrp to ALL to initialize MCU domain when reset isolation is
        not enabled */
        Sciclient_BoardCfgPrms_t boardCfgPrms_pm =
            {
                .boardConfigLow = (uint32_t)0,
                .boardConfigHigh = 0,
                .boardConfigSize = 0,
                .devGrp = DEVGRP_ALL,
            };

        status = Sciclient_boardCfgPm(&boardCfgPrms_pm);

        Sciclient_BoardCfgPrms_t boardCfgPrms_rm =
        {
            .boardConfigLow = (uint32_t)0,
            .boardConfigHigh = 0,
            .boardConfigSize = 0,
            .devGrp = DEVGRP_ALL,
        };

        status = Sciclient_boardCfgRm(&boardCfgPrms_rm);

        /* Enable MCU PLL. MCU PLL will not be enabled by DMSC when devGrp is set
        to Main in boardCfg */
        Bootloader_enableMCUPLL();
    }

    System_init();
    Bootloader_profileAddProfilePoint("System_init");

    Bootloader_socOpenFirewalls();

    Bootloader_socNotifyFirewallOpen();


    Drivers_open();
    Bootloader_profileAddProfilePoint("Drivers_open");

    #if 0
    DebugP_log("\r\n");
    DebugP_log("Starting OSPI Bootloader ... \r\n");
    #endif

    status = Board_driversOpen();
    DebugP_assert(status == SystemP_SUCCESS);
    Bootloader_profileAddProfilePoint("Board_driversOpen");

    status = Sciclient_getVersionCheck(1);
    Bootloader_profileAddProfilePoint("Sciclient Get Version");
    if(SystemP_SUCCESS == status)
    {
        Bootloader_BootImageInfo bootImageInfo;
        Bootloader_Params bootParams;
        Bootloader_Handle bootHandle;
        #ifdef ENC_BOOT
        Bootloader_Config *bootConfig;
        #endif

        Bootloader_Params_init(&bootParams);
        Bootloader_BootImageInfo_init(&bootImageInfo);

        bootHandle = Bootloader_open(CONFIG_BOOTLOADER_FLASH0, &bootParams);
        if(bootHandle != NULL)
        {
            /* Initialize PRU Cores if applicable */
            Bootloader_Config *cfg = (Bootloader_Config *)bootHandle;
            #ifdef ENC_BOOT
            bootConfig = (Bootloader_Config *)bootHandle;
            bootConfig->scratchMemPtr = gAppimage;
            #else
            cfg->enableScratchMem = 0U;
            #endif

            /* ─── F2c bank selection ─────────────────────────────────
             * Read FwBootMeta + Bank A/B FwBankHeader from OSPI,
             * increment boot_count + watchdog_strikes, decide which
             * bank to boot, and override the bootloader's flash offset
             * to point at that bank's image. The Bootloader_open() call
             * above wired in the SysConfig default (0x080000, matching
             * Bank A for the migration-free first flash). We patch
             * Bootloader_FlashArgs::appImageOffset AND curOffset before
             * parseAndLoadMultiCoreELF reads the image.
             *
             * Why both: Bootloader_open() runs Flash_imgOpen() which
             * sets `curOffset = appImageOffset` at that moment (see
             * bootloader_flash.c line 57 in the SDK). The first
             * Flash_imgRead from parseAndLoadMultiCoreELF reads at
             * `curOffset`, NOT `appImageOffset`. If we only patch
             * appImageOffset, the very first read goes to the old
             * SysConfig offset (0x080000); subsequent Seek-based
             * reads use the new appImageOffset, but the initial
             * X.509 cert + image-header parse fails because it
             * starts from the wrong place. Discovered 2026-06-01
             * after Bank B boot failure on STORAGE — the chooser
             * picked Bank B correctly but the bootloader was still
             * reading the first chunk from 0x080000 because
             * Flash_imgOpen had latched the old value.
             *
             * Detailed algorithm in docs/lp-am2434-f2c-sbl-chooser-design.md §4.
             * Session 1's heartbeat hook in lp_watchdog_client.c clears
             * the strike counter once the app has held all required alive
             * bits for 30 s — that's what makes the strike-based rollback
             * actually work. Without that, every boot would accumulate
             * strikes and we'd fall back after 3 boots even on a healthy
             * board. */
            {
                SblBankSelection sel = {0};
                int32_t          sel_status = SblBankSelect_Choose(
                                                  gFlashHandle[CONFIG_FLASH0],
                                                  &sel);
                SblBankSelect_LogSelection(&sel);

                if (sel_status == SystemP_SUCCESS)
                {
                    Bootloader_FlashArgs *fa =
                        (Bootloader_FlashArgs *)cfg->args;
                    fa->appImageOffset = sel.flash_off;
                    /* CRITICAL: also patch curOffset. Flash_imgOpen
                     * (called inside Bootloader_open above) has
                     * already set curOffset = (old) appImageOffset,
                     * so we must re-sync it or the first read goes
                     * to the wrong place. */
                    fa->curOffset      = sel.flash_off;
                }
                else
                {
                    /* Read failed — fall back to whatever SysConfig
                     * baked in (Bank A at 0x080000). SblBankSelect_Choose
                     * already left sel.bank = LEGACY in this path so
                     * the boot trace tells the operator. */
                    DebugP_log("[SBL] WARN: bank select read failed, "
                               "using SysConfig default offset\r\n");
                }
            }

            if(TRUE == cfg->initICSSCores)
            {
                status = Bootloader_socEnableICSSCores(BOOTLOADER_ICSS_CORE_DEFAULT_FREQUENCY);
                DebugP_assert(status == SystemP_SUCCESS);
            }

            status = Bootloader_parseAndLoadMultiCoreELF(bootHandle, &bootImageInfo);
            
            Bootloader_profileAddProfilePoint("CPU load");
            Bootloader_profileUpdateAppimageSize(Bootloader_getMulticoreImageSize(bootHandle));
            Bootloader_profileUpdateMediaAndClk(BOOTLOADER_MEDIA_FLASH, OSPI_getInputClk(gOspiHandle[CONFIG_OSPI0]));

            #if 1
            if( status == SystemP_SUCCESS)
            {
                /* Enable Phy and Phy pipeline for XIP execution */
                if( OSPI_isPhyEnable(gOspiHandle[CONFIG_OSPI0]) )
                {
                    status = OSPI_enablePhy(gOspiHandle[CONFIG_OSPI0]);
                    DebugP_assert(status == SystemP_SUCCESS);

                    status = OSPI_enablePhyPipeline(gOspiHandle[CONFIG_OSPI0]);
                    DebugP_assert(status == SystemP_SUCCESS);
                }
                /* Enable Dac mode */
                status = OSPI_enableDacMode(gOspiHandle[CONFIG_OSPI0]);
                DebugP_assert(status == SystemP_SUCCESS);
            }
            #endif

            if(status == SystemP_SUCCESS)
            {
                /* Print SBL Profiling logs to UART as other cores may use the UART for logging */
                Bootloader_profileAddProfilePoint("SBL End");
                Bootloader_profilePrintProfileLog();
                DebugP_log("Image loading done, switching to application ...\r\n");
                UART_flushTxFifo(gUartHandle[CONFIG_UART0]);
            }

            /* Run CPUs */
            /* Do not run M4 when MCU domain is reset isolated */
            if (!Bootloader_socIsMCUResetIsoEnabled())
            {
                if(status == SystemP_SUCCESS && (TRUE == Bootloader_isCorePresent(bootHandle, CSL_CORE_ID_M4FSS0_0)))
                {
                    status = Bootloader_runCpu(bootHandle, &bootImageInfo.cpuInfo[CSL_CORE_ID_M4FSS0_0]);
                }
            }
            if(status == SystemP_SUCCESS && (TRUE == Bootloader_isCorePresent(bootHandle, CSL_CORE_ID_R5FSS1_0)))
            {
                status = Bootloader_runCpu(bootHandle, &bootImageInfo.cpuInfo[CSL_CORE_ID_R5FSS1_0]);
            }
            /*Checks the core variant(Dual/Quad) */
            if((Bootloader_socIsR5FSSDual(BOOTLOADER_R5FSS1)) && status == SystemP_SUCCESS && (TRUE == Bootloader_isCorePresent(bootHandle, CSL_CORE_ID_R5FSS1_1)))
            {
                status = Bootloader_runCpu(bootHandle, &bootImageInfo.cpuInfo[CSL_CORE_ID_R5FSS1_1]);
            }
            if(status == SystemP_SUCCESS)
            {
                /* Reset self cluster, both Core0 and Core 1. Init RAMs and run the app  */
                status = Bootloader_runSelfCpu(bootHandle, &bootImageInfo);
            }
            /* it should not return here, if it does, then there was some error */
            Bootloader_close(bootHandle);
        }
    }
    if(status != SystemP_SUCCESS )
    {
        DebugP_log("Some tests have failed!!\r\n");
    }
    Drivers_close();
    System_deinit();

    return 0;
}
