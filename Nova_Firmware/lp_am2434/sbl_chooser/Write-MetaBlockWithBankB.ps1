<#
.SYNOPSIS
  Construct a 128 KB OSPI meta-block payload with BOTH FwBootMeta +
  Bank A header (at sec_a = 0x300000) AND Bank B header (at sec_b =
  0x310000), then JTAG-flash it to OSPI 0x300000 on a chosen probe.

.DESCRIPTION
  Used to test the F2c chooser's bank-selection logic without
  modifying firmware. The default behavior makes Bank B "newer" than
  Bank A (sequence=2 vs sequence=1) so the chooser should pick Bank B
  on next boot.

  Layout of the 128 KB payload (covers OSPI 0x300000-0x31FFFF):
    +0x00000  FwBootMeta (128 B)         boot_count=1, reason=0, strikes=1
    +0x00080  Bank A FwBankHeader (136 B) magic=NOVA, seq=1, valid=$BankAValid, active=$BankAActive, version="seed"
    +0x00108  filler 0xFF                 (rest of 0x300000 sector)
    +0x10000  Bank B FwBankHeader (136 B) magic=NOVA, seq=2, valid=$BankBValid, active=$BankBActive, version=$BankBVersion
    +0x10088  filler 0xFF                 (rest of 0x310000 sector)

  Sectors 0x320000-0x33FFFF (part of the same 256 KB erase block as
  0x300000) are erased by the flasher but our payload doesn't touch
  them -- they stay 0xFF.

.PARAMETER Probe
  A | N | P | T

.PARAMETER BankAValid
  Bool. Bank A header `valid` bit. Default $true.

.PARAMETER BankAActive
  Bool. Bank A header `active` bit. Default $false (because Bank B
  becomes active in the standard test).

.PARAMETER BankBValid
  Bool. Bank B header `valid` bit. Default $true.

.PARAMETER BankBActive
  Bool. Bank B header `active` bit. Default $true.

.PARAMETER BankBVersion
  String for Bank B's version[32]. Default "0.A.208-bank-b" so the
  bridge's broker-fleet-probe shows this value when chooser picks B.

.PARAMETER BankBImageSize
  uint32. Bank B image size. Default 524834 (the size of the OTA'd
  firmware currently at OSPI 0x900000). Informational -- chooser
  doesn't validate.

.EXAMPLE
  .\Write-MetaBlockWithBankB.ps1 -Probe A
  # Standard test: Bank A valid+inactive, Bank B valid+active+higher-sequence.
  # Chooser should pick Bank B on next boot. broker-fleet-probe should
  # show active_version="0.A.208-bank-b".

.EXAMPLE
  .\Write-MetaBlockWithBankB.ps1 -Probe A -BankAValid:$false
  # Negative test: Bank A INVALID, Bank B valid+active.
  # Chooser should still pick Bank B (now via the fallback path because
  # Bank A is rejected). Proves rollback logic.
#>
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('A', 'N', 'P', 'T')]
    [string]$Probe,

    [bool]$BankAValid    = $true,
    [bool]$BankAActive   = $false,
    [bool]$BankBValid    = $true,
    [bool]$BankBActive   = $true,
    [string]$BankBVersion   = "0.A.208-bank-b",
    [uint32]$BankBImageSize = 524834
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$LpDir     = Split-Path -Parent $ScriptDir

# --- Build the 128 KB payload --------------------------------------------
# Covers OSPI offsets 0x300000 to 0x31FFFF.
$payloadSize = 0x20000
$payload = New-Object byte[] $payloadSize
# Default-init all bytes to 0xFF (erased flash state).
for ($i = 0; $i -lt $payloadSize; $i++) { $payload[$i] = 0xFF }

# --- FwBootMeta at offset 0x00 (128 bytes) -------------------------------
# Preserve seed: boot_count=1, boot_reason=0, watchdog_strikes=1
[System.BitConverter]::GetBytes([uint32]1).CopyTo($payload, 0x00)  # boot_count
[System.BitConverter]::GetBytes([uint32]0).CopyTo($payload, 0x04)  # boot_reason
[System.BitConverter]::GetBytes([uint32]1).CopyTo($payload, 0x08)  # watchdog_strikes
# bytes 0x0C..0x7F stay 0xFF

# --- Bank A FwBankHeader at offset 0x80 (136 bytes) ----------------------
$aBase = 0x80
$NOVA  = [uint32]0x4E4F5641
[System.BitConverter]::GetBytes($NOVA).CopyTo($payload, $aBase + 0)               # magic
[System.BitConverter]::GetBytes([uint32]0).CopyTo($payload, $aBase + 4)           # image_size
[System.BitConverter]::GetBytes([uint32]0).CopyTo($payload, $aBase + 8)           # image_crc
[System.BitConverter]::GetBytes([uint32]([int]$BankAValid)).CopyTo($payload, $aBase + 12)   # valid
[System.BitConverter]::GetBytes([uint32]([int]$BankAActive)).CopyTo($payload, $aBase + 16)  # active
[System.BitConverter]::GetBytes([uint32]1).CopyTo($payload, $aBase + 20)          # sequence = 1
$aVer = [System.Text.Encoding]::ASCII.GetBytes("seed")
$aVerLen = [Math]::Min($aVer.Length, 31)
# version[32] starts at +24. Zero-init the 32 bytes first (not 0xFF).
for ($i = 0; $i -lt 32; $i++) { $payload[$aBase + 24 + $i] = 0 }
for ($i = 0; $i -lt $aVerLen; $i++) { $payload[$aBase + 24 + $i] = $aVer[$i] }
# reserved[80] at +56 stays 0xFF (from initial fill).

# --- Bank B FwBankHeader at offset 0x10000 (136 bytes) -------------------
$bBase = 0x10000
[System.BitConverter]::GetBytes($NOVA).CopyTo($payload, $bBase + 0)
[System.BitConverter]::GetBytes($BankBImageSize).CopyTo($payload, $bBase + 4)
[System.BitConverter]::GetBytes([uint32]0).CopyTo($payload, $bBase + 8)           # image_crc (chooser doesn't check)
[System.BitConverter]::GetBytes([uint32]([int]$BankBValid)).CopyTo($payload, $bBase + 12)
[System.BitConverter]::GetBytes([uint32]([int]$BankBActive)).CopyTo($payload, $bBase + 16)
[System.BitConverter]::GetBytes([uint32]2).CopyTo($payload, $bBase + 20)          # sequence = 2 (newer than A)
$bVer = [System.Text.Encoding]::ASCII.GetBytes($BankBVersion)
$bVerLen = [Math]::Min($bVer.Length, 31)
for ($i = 0; $i -lt 32; $i++) { $payload[$bBase + 24 + $i] = 0 }
for ($i = 0; $i -lt $bVerLen; $i++) { $payload[$bBase + 24 + $i] = $bVer[$i] }

# --- Write payload to temp file ------------------------------------------
$tmpDir = Join-Path $env:TEMP "constellation-f2c"
if (-not (Test-Path $tmpDir)) { New-Item -ItemType Directory -Path $tmpDir | Out-Null }
$tmpFile = Join-Path $tmpDir "meta_with_bank_b.bin"
[System.IO.File]::WriteAllBytes($tmpFile, $payload)

Write-Host "=== F2c meta-block with Bank B ===" -ForegroundColor Cyan
Write-Host "  Probe:       $Probe"
Write-Host "  Payload:     $payloadSize bytes (covers OSPI 0x300000-0x31FFFF)"
Write-Host "  Temp file:   $tmpFile"
Write-Host ""
Write-Host "  Bank A:      valid=$($BankAValid -as [int])  active=$($BankAActive -as [int])  sequence=1  version='seed'"
Write-Host "  Bank B:      valid=$($BankBValid -as [int])  active=$($BankBActive -as [int])  sequence=2  version='$BankBVersion'  image_size=$BankBImageSize"
Write-Host ""

# --- Probe -> ccxml mapping ----------------------------------------------
$ccxmlMap = @{
    'A' = "$LpDir\AM2434_LP_A.ccxml"
    'N' = "$LpDir\AM2434_LP_NOVA.ccxml"
    'P' = "$LpDir\AM2434_LP_B.ccxml"
    'T' = "$LpDir\AM2434_LP_T.ccxml"
}
$ccxml = $ccxmlMap[$Probe]
if (-not (Test-Path $ccxml)) { throw "ccxml not found: $ccxml" }

# --- Drive uniflash_run.js -----------------------------------------------
$env:LP_CCXML        = $ccxml
$env:UNIFLASH_FILE   = $tmpFile -replace '\\', '/'
$env:UNIFLASH_OFFSET = "0x300000"

$dss = "C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat"
$uniflashScript = "$LpDir\ospi_flash\uniflash_run.js"

Write-Host "=== Running uniflash_run.js ===" -ForegroundColor Cyan
& $dss $uniflashScript 2>&1 | Select-Object -Last 15

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "=== Meta block written ===" -ForegroundColor Green
    Write-Host "Next boot trace expected on $Probe :"
    if ($BankAValid -and $BankBValid) {
        Write-Host "  [SBL] bank=B seq=2 off=0x900000 ..."
        Write-Host "  (chooser picks Bank B because seq=2 > seq=1)"
    } elseif (-not $BankAValid -and $BankBValid) {
        Write-Host "  [SBL] bank=B seq=2 off=0x900000 ..."
        Write-Host "  (chooser FALLS BACK to Bank B because Bank A valid=0 - ROLLBACK PROOF)"
    } elseif ($BankAValid -and -not $BankBValid) {
        Write-Host "  [SBL] bank=A seq=1 off=0x080000 ..."
    } else {
        Write-Host "  [SBL] bank=GOLDEN off=0xC00000 (FATAL - no Golden image flashed)"
    }
} else {
    Write-Host "=== Flash FAILED (exit $LASTEXITCODE) ===" -ForegroundColor Red
    exit $LASTEXITCODE
}
