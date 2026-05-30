<#
.SYNOPSIS
  One-button commissioning of a fresh / newly-allocated LP board with
  the F2c SBL chooser + Nova app + per-board role/IP. JTAG-only — no
  DIP switches, no UART boot mode required.

.DESCRIPTION
  The full "first install" pipeline for a new board going from
  "stock TI SBL" to "F2c-enabled production firmware":

    1. Flash-SblChooser.ps1 — back up stock SBL, install F2c chooser,
       seed Bank A metadata at OSPI 0x300000
    2. Flash-LP.ps1         — build (if needed) + flash the Nova app
       with the requested role baked in, write per-board OSPI device-
       config at 0x600000

  After this script finishes, power-cycle the LP. The boot trace
  should show:

    Starting OSPI Bootloader ...
    [SBL] bank=A seq=1 off=0x080000 boots=N strikes=K reason=0
    Image loading done, switching to application ...
    [BB] LpDeviceConfig_Init ok
    [BB] NovaFwUpdate_Init ok
    [BB] LP role = <ROLE>   ip = <IP>
    ...

  Use this script for:
    - Initial commissioning of a board fresh from manufacturing
    - Migrating an existing-bench board from TI stock SBL → F2c chooser
    - Re-commissioning after a probe-routing accident (the SBL stays
      put; only the app + device-config need re-flashing)

  Don't use this for routine app-only updates — use Flash-LP.ps1
  directly for that (the SBL chooser persists across app re-flashes).

.PARAMETER Probe
  Probe identifier (A, N, P, T). See Flash-LP.ps1 / Flash-SblChooser.ps1
  for the probe → serial → COM mapping.

.PARAMETER Role
  CONTROLLER / STORAGE / GDC / TRITON. Passed through to Flash-LP.ps1.
  Optional — defaults to the probe's standard role per the bench map.

.PARAMETER Ip
  Per-board IP (10.47.27.x). Passed through to Flash-LP.ps1. Optional
  — defaults from the role.

.PARAMETER SkipSbl
  Skip the Flash-SblChooser.ps1 step. Use only if you've already
  installed the F2c chooser on this board and just want to re-flash
  the app + role/IP.

.PARAMETER SkipApp
  Skip the Flash-LP.ps1 step. Use if you only want to install the SBL
  chooser without disturbing the running app.

.PARAMETER WipeDevCfg
  Passed through to Flash-LP.ps1. Erases OSPI device-config banks
  before the role/IP write so a fresh board / wrong-probe-recovery
  board doesn't boot with stale identity.

.PARAMETER SkipBuild
  Passed through to Flash-LP.ps1. Re-uses the already-built
  nova_lp.release.mcelf.hs_fs instead of rebuilding.

.PARAMETER SkipBackup
  Passed through to Flash-SblChooser.ps1. Skips the pre-flight stock-SBL
  backup. Use only if you've already imaged this board's stock SBL.

.EXAMPLE
  .\Commission-LP.ps1 -Probe A
  # Full first-install of STORAGE on Probe A: back up stock SBL,
  # install F2c chooser, seed Bank A header, build + flash Nova app
  # with role=STORAGE / ip=10.47.27.2, write device-config.

.EXAMPLE
  .\Commission-LP.ps1 -Probe N -WipeDevCfg
  # Re-commission CONTROLLER after probe-routing accident. WipeDevCfg
  # erases the OSPI device-config so the role/IP can't conflict.

.EXAMPLE
  .\Commission-LP.ps1 -Probe T -SkipSbl
  # Just re-flash the app + role on TRITON. Use this if F2c chooser
  # is already installed and you only need to update the app payload.
#>
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('A', 'N', 'P', 'T')]
    [string]$Probe,

    [ValidateSet('CONTROLLER', 'STORAGE', 'GDC', 'TRITON')]
    [string]$Role = '',

    [string]$Ip = '',

    [switch]$SkipSbl,
    [switch]$SkipApp,
    [switch]$WipeDevCfg,
    [switch]$SkipBuild,
    [switch]$SkipBackup
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$LpDir     = Split-Path -Parent $ScriptDir

Write-Host ""
Write-Host "================================================================" -ForegroundColor Magenta
Write-Host "  F2c Commission-LP — full first-install pipeline" -ForegroundColor Magenta
Write-Host "  Probe $Probe    Role $($Role ? $Role : '(default)')    IP $($Ip ? $Ip : '(default)')" -ForegroundColor Magenta
Write-Host "================================================================" -ForegroundColor Magenta
Write-Host ""

# ─── Stage 1: SBL chooser ──────────────────────────────────────────────
if ($SkipSbl) {
    Write-Host "[stage 1] SKIPPED (-SkipSbl) — assuming F2c chooser is already installed" -ForegroundColor DarkGray
} else {
    Write-Host "[stage 1] Flash-SblChooser.ps1 — backup, flash chooser, seed metadata" -ForegroundColor Cyan
    Write-Host ""
    $sblArgs = @{ Probe = $Probe }
    if ($SkipBackup) { $sblArgs.SkipBackup = $true }

    & "$ScriptDir\Flash-SblChooser.ps1" @sblArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Flash-SblChooser.ps1 failed (exit $LASTEXITCODE). Commissioning ABORTED before app flash to keep the board in a known state."
    }
    Write-Host ""
    Write-Host "[stage 1] SBL chooser installed." -ForegroundColor Green
}

# ─── Stage 2: Nova app + role/IP ───────────────────────────────────────
if ($SkipApp) {
    Write-Host ""
    Write-Host "[stage 2] SKIPPED (-SkipApp) — leaving running app + device-config alone" -ForegroundColor DarkGray
} else {
    Write-Host ""
    Write-Host "[stage 2] Flash-LP.ps1 — build (if needed) + flash app + role/IP" -ForegroundColor Cyan
    Write-Host ""
    $appArgs = @{ Probe = $Probe }
    if ($Role)       { $appArgs.Role        = $Role }
    if ($Ip)         { $appArgs.Ip          = $Ip }
    if ($SkipBuild)  { $appArgs.SkipBuild   = $true }
    if ($WipeDevCfg) { $appArgs.WipeDevCfg  = $true }

    & "$LpDir\Flash-LP.ps1" @appArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Flash-LP.ps1 failed (exit $LASTEXITCODE). The SBL chooser is installed but the app flash didn't complete. Recovery: re-run with -SkipSbl to retry the app flash only."
    }
    Write-Host ""
    Write-Host "[stage 2] Nova app + role/IP flashed." -ForegroundColor Green
}

# ─── Done ──────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "================================================================" -ForegroundColor Green
Write-Host "  Commission-LP COMPLETE — Probe $Probe" -ForegroundColor Green
Write-Host "================================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Power-cycle the LP (USB unplug + replug) so it cold-boots from OSPI."
Write-Host "  2. Watch UART0 boot trace:"

$probeComMap = @{ 'A' = 'COM5'; 'N' = 'COM9'; 'P' = 'COM7'; 'T' = 'COM4' }
$com = $probeComMap[$Probe]
Write-Host "     cd $LpDir; .\Capture-Com.ps1 -Port $com" -ForegroundColor Cyan
Write-Host ""
Write-Host "  3. Confirm trace shows BOTH:"
Write-Host "     [SBL] bank=A seq=1 off=0x080000 ...     (F2c chooser picked Bank A)" -ForegroundColor Gray
Write-Host "     [BB] LP role = <expected role>          (Nova app started with correct identity)" -ForegroundColor Gray
Write-Host ""
Write-Host "  4. After 30s of all-alive, Session 1's heartbeat hook clears strikes=0."
Write-Host "     Subsequent boots will show strikes=1 (the SBL increment that gets cleared again)."
Write-Host ""
