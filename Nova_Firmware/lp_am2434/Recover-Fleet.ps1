<#
.SYNOPSIS
  Recovery script — re-flash 3 fleet LPs with current 0.A.197 firmware.

.DESCRIPTION
  Bench 2026-05-26 left .2 STORAGE and .3 GDC wedged with Bank-A
  overwritten (data correct, but lp_ota_task self-aborted on a stale-
  CRC sanity check — see 0.A.197 changelog). .1 CONTROLLER also
  needs to be re-flashed with the 0.A.197 LP fix so future installs
  don't hit the same wedge.

  This script re-flashes all 3 in sequence:
    1. .1 CONTROLLER via Probe N (S24L0707) — restores broker
    2. .2 STORAGE    via Probe A (S24L0417) — recovers wedge
    3. .3 GDC        via Probe P (S24L0727) — recovers wedge

  Each flash takes ~5-8 min (full build per flash because Flash-LP.ps1
  bakes role+IP at compile time). Total ~20-25 min.

.PREREQUISITES
  - All 4 XDS110 probes plugged in and enumerated (`xdsdfu -e`).
  - NO COM port captures open (COM4/5/7/9) — Set-Probe needs to
    disable probes one at a time, and Windows refuses to disable a
    USB Composite Device with an open CDC child.
  - Bridge running on the Pi5 is fine — Flash-LP doesn't need it
    stopped, but be aware the bridge will see UART silence briefly
    when the controller resets.

.NOTES
  After this script completes successfully, fleet should probe as:
    .1 CONTROLLER  0.A.197  bank A  role=0
    .2 STORAGE     0.A.197  bank A  role=1
    .3 GDC         0.A.197  bank A  role=2
    .4 TRITON      0.A.192  bank A  role=3   (intentionally left —
                                              not wedged, no urgent
                                              need to bump)

  TRITON can be flashed too if you want a uniform fleet:
    .\Flash-LP.ps1 -Probe T -Role TRITON -IP 10.47.27.4 -Force

  If any step fails:
    - JTAG comm error -261: unplug + replug that probe's USB cable
      (probe firmware glitch), then re-run from that step.
    - DSS hangs at "[probe-check:pre-app-flash] OK": kill any stuck
      `java.exe`, then power-cycle the controller, then re-run.
#>
[CmdletBinding()]
param(
    [switch]$SkipController,
    [switch]$SkipStorage,
    [switch]$SkipGdc,
    [switch]$IncludeTriton
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

function Invoke-Flash {
    param(
        [Parameter(Mandatory)] [string] $Probe,
        [Parameter(Mandatory)] [string] $Role,
        [Parameter(Mandatory)] [string] $IP,
        [Parameter(Mandatory)] [string] $Label
    )

    Write-Host ""
    Write-Host ("=" * 60) -ForegroundColor Cyan
    Write-Host "  Flash: $Label  (Probe $Probe -> $IP)" -ForegroundColor Cyan
    Write-Host ("=" * 60) -ForegroundColor Cyan
    Write-Host ""

    # 2026-05-27: -SkipWatchdog no longer needed. Flash-LP.ps1 now
    # inserts a 10-s wait + java.exe cleanup between the main flash
    # and the watchdog flash, which fixed the "Step 2 wedge" that
    # made the second uniflash invocation hang. Re-flashing the
    # watchdog every recovery keeps the watchdog image fresh for any
    # mid-cycle changes.
    & ".\Flash-LP.ps1" -Probe $Probe -Role $Role -IP $IP -Force
    if ($LASTEXITCODE -ne 0) {
        throw "Flash-LP failed for $Label (Probe $Probe). Stopping."
    }

    Write-Host ""
    Write-Host "[OK] $Label flashed. Waiting 15s for boot + network..." `
                -ForegroundColor Green
    Start-Sleep -Seconds 15
}

# ─── Step 1: .1 CONTROLLER (Probe N) ──────────────────────────────────────
if (-not $SkipController) {
    Invoke-Flash -Probe 'N' -Role 'CONTROLLER' -IP '10.47.27.1' `
                 -Label '.1 CONTROLLER'
} else {
    Write-Host "[SKIP] .1 CONTROLLER" -ForegroundColor Yellow
}

# ─── Step 2: .2 STORAGE (Probe A) ─────────────────────────────────────────
if (-not $SkipStorage) {
    Invoke-Flash -Probe 'A' -Role 'STORAGE' -IP '10.47.27.2' `
                 -Label '.2 STORAGE'
} else {
    Write-Host "[SKIP] .2 STORAGE" -ForegroundColor Yellow
}

# ─── Step 3: .3 GDC (Probe P) ─────────────────────────────────────────────
if (-not $SkipGdc) {
    Invoke-Flash -Probe 'P' -Role 'GDC' -IP '10.47.27.3' `
                 -Label '.3 GDC'
} else {
    Write-Host "[SKIP] .3 GDC" -ForegroundColor Yellow
}

# ─── Optional: .4 TRITON (Probe T) ────────────────────────────────────────
if ($IncludeTriton) {
    Invoke-Flash -Probe 'T' -Role 'TRITON' -IP '10.47.27.4' `
                 -Label '.4 TRITON'
}

Write-Host ""
Write-Host ("=" * 60) -ForegroundColor Green
Write-Host "  Fleet recovery complete." -ForegroundColor Green
Write-Host ("=" * 60) -ForegroundColor Green
Write-Host ""
Write-Host "Verify with:" -ForegroundColor Yellow
Write-Host "  ssh gellert@10.47.27.108 'cd /home/gellert/Gellert/constellation/constellation-ui/server && npx tsx src/probe_fleet.ts 10.47.27.1 10.47.27.2 10.47.27.3 10.47.27.4'"
