#requires -RunAsAdministrator
<#
.SYNOPSIS
  Flash TI's stock ospi_flash_file_io FreeRTOS example to LP-AM2434 OSPI
  bank A (0x80000), bypassing Flash-LP.ps1's build phase entirely.

.DESCRIPTION
  Sets UNIFLASH_FILE to TI's prebuilt mcelf and calls dss.bat directly.
  Use this to test whether TI's stock SDK example does Flash_write
  successfully on the same hardware where our firmware fails. No build,
  no risk of our gmake regenerating the live mcelf.

  The example is prebuilt at:
    C:\ti\mcu_plus_sdk_am243x_12_00_00_26\examples\drivers\ospi\
      ospi_flash_file_io\am243x-lp\r5fss0-0_freertos\ti-arm-clang\
      ospi_flash_file_io.release.mcelf.hs_fs

  After flash, monitor COM4 for the example's UART output:
    "[OSPI Flash] OSPI File IO Test Started ..."
    -> 32 lines of file write counter
    -> "[OSPI Flash] OSPI File IO Test Passed!!"

.PARAMETER Probe
  Currently only N (CONTROLLER) is supported.
#>
param(
    [Parameter(Mandatory)][ValidateSet('N')] [string]$Probe
)

$ErrorActionPreference = 'Stop'

# Probe -> ccxml + COM mapping (same as Flash-LP.ps1)
$probeMap = @{
    N = @{ Ccxml = 'F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_NOVA.ccxml'; Com = 'COM4' }
}
$probeCfg = $probeMap[$Probe]

$tiExample = 'C:\ti\mcu_plus_sdk_am243x_12_00_00_26\examples\drivers\ospi\ospi_flash_file_io\am243x-lp\r5fss0-0_freertos\ti-arm-clang\ospi_flash_file_io.release.mcelf.hs_fs'
if (-not (Test-Path $tiExample)) {
    throw "TI example mcelf not found: $tiExample`nRun: gmake PROFILE=release all in the example dir first"
}

$tiSize = (Get-Item $tiExample).Length
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Flash TI stock ospi_flash_file_io example" -ForegroundColor Cyan
Write-Host "  File: $tiExample" -ForegroundColor Cyan
Write-Host "  Size: $tiSize bytes (NOT our 493 KB firmware)" -ForegroundColor Cyan
Write-Host "  Probe: $Probe ($($probeCfg.Com))" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# Set env vars exactly like Flash-LP.ps1's flash step
$env:LP_CCXML       = $probeCfg.Ccxml
$env:UNIFLASH_FILE  = ($tiExample -replace '\\','/')
$env:UNIFLASH_OFFSET = '0x80000'
Remove-Item Env:UNIFLASH_MANIFEST -ErrorAction SilentlyContinue

$logPath = "F:\Constellation\logs\lp_ti_example_flash.log"
$dssBat  = 'C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat'
$jsPath  = 'F:\Constellation\Nova_Firmware\lp_am2434\ospi_flash\uniflash_run.js'
if (-not (Test-Path $dssBat)) { throw "dss.bat not found at $dssBat" }
if (-not (Test-Path $jsPath)) { throw "uniflash_run.js not found at $jsPath" }

"" | Set-Content -Path $logPath
& $dssBat $jsPath 2>&1 | Tee-Object -FilePath $logPath -Append

Write-Host ""
Write-Host "DONE. Monitor COM4 for TI example UART output." -ForegroundColor Green
Write-Host "      To restore our firmware: re-run Flash-LP.ps1 -Probe N -Force -SkipWatchdog" -ForegroundColor DarkGray
