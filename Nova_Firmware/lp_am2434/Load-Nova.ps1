<#
.SYNOPSIS
  Load Nova firmware to LP-AM2434 via JTAG (XDS110) — no DIP switch changes needed.
.DESCRIPTION
  Uses TI CCS Debug Server Scripting (DSS) to:
    1. Initialize DMSC (board config for HS-FS device)
    2. Load nova_lp.release.out to R5F core 0
    3. Run the firmware

  The LP can stay on OSPI boot mode (SW4 = 0100 0100) permanently.
  Firmware runs from RAM until next power cycle. For OSPI persistence,
  use the SBL flash writer instead.

.EXAMPLE
  .\Load-Nova.ps1              # Build + load
  .\Load-Nova.ps1 -SkipBuild   # Load only (use existing .out)
#>
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$BASE = Split-Path -Parent $MyInvocation.MyCommand.Path
$FW_ROOT = Split-Path -Parent $BASE
$DSS = "C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat"
$LOAD_JS = "$BASE\load_nova.js"
$OUT_FILE = "$BASE\ti-arm-clang\nova_lp.release.out"

# Verify DSS exists
if (-not (Test-Path $DSS)) {
    Write-Host "ERROR: CCS DSS not found at $DSS" -ForegroundColor Red
    Write-Host "Install CCS or update the path." -ForegroundColor Red
    exit 1
}

# Build if needed
if (-not $SkipBuild) {
    Write-Host "Building Nova firmware..." -ForegroundColor Cyan
    $makeDir = "$BASE\ti-arm-clang"
    $gmake = "C:\ti\ccs2050\ccs\utils\bin\gmake.exe"
    if (-not (Test-Path $gmake)) {
        Write-Host "ERROR: gmake not found at $gmake" -ForegroundColor Red
        exit 1
    }
    Push-Location $makeDir
    & $gmake -j8 all 2>&1
    if ($LASTEXITCODE -ne 0) {
        Pop-Location
        Write-Host "ERROR: Build failed" -ForegroundColor Red
        exit 1
    }
    Pop-Location
    Write-Host "Build OK" -ForegroundColor Green
}

# Verify .out exists
if (-not (Test-Path $OUT_FILE)) {
    Write-Host "ERROR: $OUT_FILE not found. Run build first." -ForegroundColor Red
    exit 1
}

Write-Host "" -ForegroundColor White
Write-Host "Loading Nova firmware via JTAG..." -ForegroundColor Cyan
Write-Host "  ELF: $OUT_FILE" -ForegroundColor DarkGray
Write-Host "  Target: LP-AM2434 (XDS110)" -ForegroundColor DarkGray
Write-Host ""

# Run the CCS DSS script
& $DSS $LOAD_JS

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Firmware loaded and running." -ForegroundColor Green
    Write-Host "UART2 bridge should be active on J1-3/J1-4." -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "ERROR: DSS returned exit code $LASTEXITCODE" -ForegroundColor Red
    Write-Host "Check that the LP is connected via USB and no other debugger is attached." -ForegroundColor Red
}
