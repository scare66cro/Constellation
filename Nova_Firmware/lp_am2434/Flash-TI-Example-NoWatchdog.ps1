#requires -RunAsAdministrator
<#
.SYNOPSIS
  Flash TI's stock OSPI flash file_io FreeRTOS example, with the watchdog
  partition at 0x180000 INVALIDATED so the SBL doesn't load it. This lets
  TI's example run alone without our DWWD reset loop killing it.

.DESCRIPTION
  Two-step flash via dss.bat (no Flash-LP.ps1 build phase):
    1. Write 16 zeros at 0x180000 — auto-flasher erases the 256-KiB block
       first, then writes 16 zero bytes. Result: SBL reads invalid mcelf
       header at 0x180000, skips loading R5FSS1_0. Our watchdog can't fire.
    2. Write TI's prebuilt example at 0x80000.

  After the warm-reset that follows step 2, R5FSS0_0 boots TI's example
  with no watchdog watching it. UART output should be visible on COM4
  ("APP_OSPI_FILE_WRITE_COUNT: 32", file write counter, etc.).

  This is a destructive test of our watchdog setup — to restore the
  watchdog after the test, run:
    .\Flash-LP.ps1 -Probe N -Force  (without -SkipWatchdog this time)
#>
param(
    [Parameter(Mandatory)][ValidateSet('N')] [string]$Probe
)

$ErrorActionPreference = 'Stop'

$probeMap = @{
    N = @{ Ccxml = 'F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_NOVA.ccxml'; Com = 'COM4' }
}
$probeCfg = $probeMap[$Probe]

# Step 0: prepare a tiny zero file for the watchdog wipe
$zeroFile = 'F:\Constellation\Nova_Firmware\lp_am2434\ti-arm-clang\watchdog_invalidate_16zero.bin'
[byte[]]$zeros = ,0 * 16
[System.IO.File]::WriteAllBytes($zeroFile, $zeros)
Write-Host "Created watchdog-invalidate file: $zeroFile (16 zero bytes)" -ForegroundColor Cyan

# Step 1: invalidate watchdog at 0x180000
$tiExample = 'C:\ti\mcu_plus_sdk_am243x_12_00_00_26\examples\drivers\ospi\ospi_flash_file_io\am243x-lp\r5fss0-0_freertos\ti-arm-clang\ospi_flash_file_io.release.mcelf.hs_fs'
if (-not (Test-Path $tiExample)) {
    throw "TI example mcelf not found: $tiExample"
}

$dssBat  = 'C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat'
$jsPath  = 'F:\Constellation\Nova_Firmware\lp_am2434\ospi_flash\uniflash_run.js'
$logPath = "F:\Constellation\logs\lp_ti_nowatchdog_flash.log"
"" | Set-Content -Path $logPath
$env:LP_CCXML = $probeCfg.Ccxml
Remove-Item Env:UNIFLASH_MANIFEST -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  STEP 1/2: Invalidate watchdog at 0x180000 (16 zero bytes)" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
$env:UNIFLASH_FILE   = ($zeroFile -replace '\\','/')
$env:UNIFLASH_OFFSET = '0x180000'
& $dssBat $jsPath 2>&1 | Tee-Object -FilePath $logPath -Append

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  STEP 2/2: Flash TI example at 0x80000" -ForegroundColor Cyan
Write-Host "  Size: $((Get-Item $tiExample).Length) bytes" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
$env:UNIFLASH_FILE   = ($tiExample -replace '\\','/')
$env:UNIFLASH_OFFSET = '0x80000'
& $dssBat $jsPath 2>&1 | Tee-Object -FilePath $logPath -Append

Write-Host ""
Write-Host "DONE. Step 2 triggers warm reset. Monitor COM4 for TI's example output:" -ForegroundColor Green
Write-Host "  Expected: 'APP_OSPI_FILE_WRITE_COUNT: 32' then 32 file write lines" -ForegroundColor DarkGray
Write-Host "  Or: hang/silence (means TI example also hits the OSPI wedge)" -ForegroundColor DarkGray
Write-Host ""
Write-Host "To restore watchdog + our firmware, run:" -ForegroundColor DarkGray
Write-Host "  .\Flash-LP.ps1 -Probe N -Force          (without -SkipWatchdog)" -ForegroundColor DarkGray
