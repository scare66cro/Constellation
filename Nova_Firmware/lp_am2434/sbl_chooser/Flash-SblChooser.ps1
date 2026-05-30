<#
.SYNOPSIS
  JTAG-only orchestration for F2c Session 2: back up stock SBL, flash
  sbl_chooser, write seed metadata, optionally invalidate Bank A for
  the negative test.

.DESCRIPTION
  Pure JTAG workflow — no DIP switches, no UART boot mode, no USB
  cycling. Uses the existing flasher_uart + uniflash_run.js
  infrastructure that Flash-LP.ps1 already relies on.

  Steps (each can be skipped via switches if you're re-running a
  subset after a failure):

    1. Verify single XDS110 probe enumerated (invariant #7)
    2. Backup current OSPI sector 0 (TI stock SBL) → backups/sbl_stock_$Probe.bin
       Skip with -SkipBackup if you've already done this.
    3. Flash sbl_chooser.release.tiimage to OSPI 0x000000
       Skip with -SkipSblFlash if you only want to update the seed.
    4. Write seed FwBootMeta + Bank A header at OSPI 0x300000
       Skip with -SkipSeed if you're flashing a fresh SBL without
       perturbing existing metadata.
    5. (Optional) -InvalidateBankA — re-write seed with valid=0 to
       trigger the negative-test fallback path

  When done, prompt the user to power-cycle the LP and connect to UART0
  to observe the boot trace. Doesn't auto-reset because the F2c chooser
  is what we're TESTING — a clean cold boot from OSPI is the only
  meaningful verification.

.PARAMETER Probe
  Probe identifier (A, N, P, T). Selects ccxml + COM port for boot-trace
  hint.

.PARAMETER SkipBackup
  Skip step 2 (pre-flight OSPI sector-0 backup). Use only if you've
  already backed up this board's stock SBL.

.PARAMETER SkipSblFlash
  Skip step 3 (sbl_chooser flash). Useful for re-seeding metadata on a
  board that already has the chooser.

.PARAMETER SkipSeed
  Skip step 4 (seed metadata write). Useful for flashing the SBL only;
  the board will boot in LEGACY mode on first boot (chooser detects no
  metadata + boot_count=0, falls through to 0x080000).

.PARAMETER InvalidateBankA
  Step 4 writes the seed with valid=0 instead of valid=1. SBL should
  then reject Bank A on next boot and fall back to Bank B (if Bank B is
  populated) or Golden (if it exists). Used for the F2c rollback
  negative test.

.PARAMETER SblImagePath
  Override the path to sbl_chooser.release.tiimage. Defaults to the
  expected build output location.

.PARAMETER BackupDir
  Where to store the stock-SBL backup. Defaults to F:\Constellation\backups.

.EXAMPLE
  .\Flash-SblChooser.ps1 -Probe A
  # First-time install on STORAGE: backup, flash chooser, seed valid header.

.EXAMPLE
  .\Flash-SblChooser.ps1 -Probe A -SkipBackup -SkipSblFlash -InvalidateBankA
  # Negative test on STORAGE: just rewrite seed with valid=0 so next
  # boot triggers SBL fallback path.

.EXAMPLE
  .\Flash-SblChooser.ps1 -Probe A -SkipSeed
  # Flash chooser only, leave OSPI metadata alone. First boot will be
  # LEGACY mode (chooser unconditionally boots 0x080000).
#>
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('A', 'N', 'P', 'T')]
    [string]$Probe,

    [switch]$SkipBackup,
    [switch]$SkipSblFlash,
    [switch]$SkipSeed,
    [switch]$InvalidateBankA,

    [string]$SblImagePath = "F:\Constellation\Nova_Firmware\lp_am2434\sbl_chooser\r5fss0-0_nortos\ti-arm-clang\sbl_chooser.release.tiimage",

    [string]$BackupDir = "F:\Constellation\backups"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$LpDir     = Split-Path -Parent $ScriptDir
$RepoRoot  = "F:\Constellation"

# ─── Probe mapping (matches Flash-LP.ps1) ──────────────────────────────
$probeMap = @{
    'A' = @{ Ccxml = "$LpDir\AM2434_LP_A.ccxml";    Serial = 'S24L0417'; Com = 'COM5'; Role = 'STORAGE'    }
    'N' = @{ Ccxml = "$LpDir\AM2434_LP_NOVA.ccxml"; Serial = 'S24L0707'; Com = 'COM9'; Role = 'CONTROLLER' }
    'P' = @{ Ccxml = "$LpDir\AM2434_LP_B.ccxml";    Serial = 'S24L0727'; Com = 'COM7'; Role = 'GDC'        }
    'T' = @{ Ccxml = "$LpDir\AM2434_LP_T.ccxml";    Serial = 'S24L0957'; Com = 'COM4'; Role = 'TRITON'     }
}
$probeCfg = $probeMap[$Probe]

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  F2c Session 2 — SBL Chooser JTAG-flash" -ForegroundColor Cyan
Write-Host "  Probe $Probe  ($($probeCfg.Serial), $($probeCfg.Role) on $($probeCfg.Com))" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

# ─── Step 1: Single-probe check ────────────────────────────────────────
Write-Host "[step 1] xdsdfu single-probe check (invariant #7)..." -ForegroundColor Yellow
$xdsdfu = "C:\ti\ccs2050\ccs\ccs_base\common\uscif\xds110\xdsdfu.exe"
if (-not (Test-Path $xdsdfu)) {
    throw "xdsdfu.exe not found at $xdsdfu — install CCS or update path."
}
$present = @(& $xdsdfu -e 2>&1 | Select-String 'Serial(?: Num)?:\s*(\S+)' |
             ForEach-Object { $_.Matches[0].Groups[1].Value })
if ($present.Count -ne 1) {
    throw "Expected exactly ONE probe; saw $($present.Count): $($present -join ', '). Unplug all but $($probeCfg.Serial)."
}
if ($present[0] -ne $probeCfg.Serial) {
    throw "Only probe enumerated is '$($present[0])', not target '$($probeCfg.Serial)'. DSS would flash the wrong board."
}
Write-Host "  OK — $($probeCfg.Serial) is the sole enumerated XDS110" -ForegroundColor Green

# ─── Step 2: Backup stock SBL ──────────────────────────────────────────
if ($SkipBackup) {
    Write-Host ""
    Write-Host "[step 2] SKIPPED (-SkipBackup)" -ForegroundColor DarkGray
} else {
    Write-Host ""
    Write-Host "[step 2] Backup current OSPI sector 0 (stock SBL or current chooser)..." -ForegroundColor Yellow
    if (-not (Test-Path $BackupDir)) {
        New-Item -ItemType Directory -Path $BackupDir | Out-Null
    }
    $ts = (Get-Date).ToString('yyyyMMdd-HHmmss')
    $backupFile = Join-Path $BackupDir "sbl_stock_${Probe}_${ts}.bin"

    $env:LP_CCXML    = $probeCfg.Ccxml
    $env:DUMP_OFFSET = "0x000000"
    $env:DUMP_SIZE   = "0x060000"        # 384 KB = SBL region per nova_fw_update.h
    $env:DUMP_FILE   = $backupFile -replace '\\', '/'

    $dss = "C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat"
    $dumpScript = "$LpDir\ospi_flash\dump_ospi_to_file.js"

    & $dss $dumpScript
    if ($LASTEXITCODE -ne 0) {
        throw "Backup dump failed (dss exit $LASTEXITCODE). Aborting before SBL flash."
    }
    if (-not (Test-Path $backupFile) -or (Get-Item $backupFile).Length -ne 0x60000) {
        throw "Backup file missing or wrong size: $backupFile"
    }
    Write-Host "  Backup written: $backupFile" -ForegroundColor Green
    Write-Host "  Recovery: re-flash with .\Flash-SblChooser.ps1 -Probe $Probe -SblImagePath `"$backupFile`" -SkipBackup -SkipSeed" -ForegroundColor DarkGray
}

# ─── Step 3: Flash sbl_chooser ─────────────────────────────────────────
if ($SkipSblFlash) {
    Write-Host ""
    Write-Host "[step 3] SKIPPED (-SkipSblFlash)" -ForegroundColor DarkGray
} else {
    Write-Host ""
    Write-Host "[step 3] Flash sbl_chooser.release.tiimage to OSPI 0x000000..." -ForegroundColor Yellow
    if (-not (Test-Path $SblImagePath)) {
        throw "SBL image not found: $SblImagePath`nBuild it first:`n  cd $LpDir\sbl_chooser\r5fss0-0_nortos\ti-arm-clang`n  gmake -s PROFILE=release all"
    }
    $sblSize = (Get-Item $SblImagePath).Length
    if ($sblSize -ge 0x60000) {
        throw "SBL image is $sblSize bytes (>= 384 KB = 0x60000). It overlaps Bank A at 0x080000. Abort."
    }
    Write-Host "  Image: $SblImagePath ($sblSize bytes)" -ForegroundColor DarkGray

    $env:LP_CCXML        = $probeCfg.Ccxml
    $env:UNIFLASH_FILE   = $SblImagePath -replace '\\', '/'
    $env:UNIFLASH_OFFSET = "0x000000"

    $dss = "C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat"
    $uniflashScript = "$LpDir\ospi_flash\uniflash_run.js"

    & $dss $uniflashScript
    if ($LASTEXITCODE -ne 0) {
        throw "SBL flash failed (dss exit $LASTEXITCODE). Recovery: re-flash stock SBL via -SblImagePath <backup.bin> -SkipSeed."
    }
    Write-Host "  SBL chooser written to OSPI 0x000000" -ForegroundColor Green
}

# ─── Step 4: Seed metadata ─────────────────────────────────────────────
if ($SkipSeed) {
    Write-Host ""
    Write-Host "[step 4] SKIPPED (-SkipSeed) — board will boot in LEGACY mode (chooser unconditionally boots 0x080000)" -ForegroundColor DarkGray
} else {
    Write-Host ""
    $seedMode = if ($InvalidateBankA) { "INVALIDATE Bank A (negative test)" } else { "seed Bank A valid=1" }
    Write-Host "[step 4] Write seed FwBootMeta + Bank A header — $seedMode" -ForegroundColor Yellow

    $seedArgs = @{ Probe = $Probe; Valid = (-not $InvalidateBankA) }
    & "$ScriptDir\Write-SeedMetaBlock.ps1" @seedArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Seed metadata write failed (exit $LASTEXITCODE)."
    }
}

# ─── Done — prompt user for verification ───────────────────────────────
Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "  F2c Session 2 JTAG steps complete." -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Next: power-cycle the LP (USB unplug + replug) so it boots from OSPI,"
Write-Host "then connect to UART0 on $($probeCfg.Com) @ 115200 to observe the boot trace."
Write-Host ""
Write-Host "Expected trace (healthy first boot, valid=1 seed):" -ForegroundColor Cyan
Write-Host "  Starting OSPI Bootloader ..." -ForegroundColor Gray
Write-Host "  [SBL] bank=A seq=1 off=0x080000 boots=2 strikes=2 reason=0" -ForegroundColor Gray
Write-Host "  Image loading done, switching to application ..." -ForegroundColor Gray
Write-Host "  [BB] LpDeviceConfig_Init ok" -ForegroundColor Gray
Write-Host "  ..."
Write-Host ""
if ($InvalidateBankA) {
    Write-Host "Expected trace (negative test, valid=0 seed):" -ForegroundColor Yellow
    Write-Host "  [SBL] bank=GOLDEN off=0xC00000 ..." -ForegroundColor Gray
    Write-Host "  (if no Golden image exists, SBL will spin in FATAL — that's the failure mode without Golden write)" -ForegroundColor DarkYellow
    Write-Host ""
    Write-Host "Recovery: .\Flash-SblChooser.ps1 -Probe $Probe -SkipBackup -SkipSblFlash" -ForegroundColor Cyan
    Write-Host "  (re-runs step 4 with valid=1, restoring the seed)" -ForegroundColor DarkGray
    Write-Host ""
}
Write-Host "To monitor UART0 from this PowerShell:"
Write-Host "  cd $LpDir; .\Capture-Com.ps1 -Port $($probeCfg.Com)" -ForegroundColor Cyan
Write-Host ""
