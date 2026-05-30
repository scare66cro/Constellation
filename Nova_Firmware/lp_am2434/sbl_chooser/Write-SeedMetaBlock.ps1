<#
.SYNOPSIS
  Constructs the F2c seed metadata block (FwBootMeta + Bank A FwBankHeader)
  and JTAG-flashes it to OSPI offset 0x300000 via the existing uniflash_run.js
  infrastructure. No DIP switches, no UART boot, no USB cycling -- pure JTAG.

.DESCRIPTION
  Sequence:
    1. Build a 256-byte .bin in-memory:
         [0..127]    FwBootMeta   (boot_count=1, boot_reason=0, strikes=1)
         [128..255]  FwBankHeader (magic=NOVA, sequence=1, valid=1, active=1,
                                   image_size=0, image_crc=0, version="seed")
    2. Write to a temp file
    3. Call uniflash_run.js with UNIFLASH_FILE=<temp>, UNIFLASH_OFFSET=0x300000
    4. Flasher's Flash_write does erase-then-program on the 64 KB sector.
       The remaining ~63.7 KB of the sector stays 0xFF (matches the post-
       erase state -- FwBootMeta and Bank A header are the only populated
       fields).

  After this runs, SblBankSelect_Choose on next boot will:
    - Read FwBootMeta: boots=2 (after SBL's increment), strikes=2
    - Read Bank A header: magic=NOVA, valid=1, sequence=1
    - pick_bank() picks Bank A (the only candidate)
    - Override appImageOffset to 0x080000
    - Boot trace: [SBL] bank=A seq=1 off=0x080000 boots=2 strikes=2 reason=0

  Then Session 1's heartbeat hook clears strikes to 0 after 30 s of all-alive.

.PARAMETER Probe
  Probe identifier (A, N, P, T). Sets LP_CCXML to the matching ccxml.

.PARAMETER Valid
  Set the Bank A `valid` field. Default is $true (seed a healthy boot).
  Pass -Valid:$false to seed a BROKEN Bank A header for the negative-test
  fallback path (SBL should then pick Bank B if it exists, else Golden).

.PARAMETER ImageSize
  Optional. Bank A image size in bytes. Default 0 -- informational only;
  the SBL chooser does not CRC-verify, so a placeholder value is fine
  for boot-time correctness.

.PARAMETER ImageCrc
  Optional. Bank A image CRC32. Default 0 -- same reasoning as ImageSize.

.PARAMETER Version
  Optional. 32-byte (null-terminated) version string. Default "seed".

.EXAMPLE
  .\Write-SeedMetaBlock.ps1 -Probe A
  # Seed a healthy Bank A header on STORAGE board.

.EXAMPLE
  .\Write-SeedMetaBlock.ps1 -Probe A -Valid:$false
  # Negative test: seed Bank A with valid=0 to trigger SBL fallback to Bank B.
#>
param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('A', 'N', 'P', 'T')]
    [string]$Probe,

    [bool]$Valid = $true,

    [uint32]$ImageSize = 0,

    [uint32]$ImageCrc  = 0,

    [string]$Version = "seed"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$LpDir     = Split-Path -Parent $ScriptDir

# --- Build the 256-byte seed block --------------------------------------
$seed = New-Object byte[] 256

# FwBootMeta at offset 0 (128 bytes)
#   uint32 boot_count        @ +0  = 1
#   uint32 boot_reason       @ +4  = 0  (normal boot)
#   uint32 watchdog_strikes  @ +8  = 1  (SBL would bump from 0 → 1 on first boot;
#                                        seeding to 1 means the next real boot
#                                        bumps to 2, which is still < 3 so no
#                                        fallback. Session 1 clears to 0 after
#                                        30 s healthy.)
#   uint8  reserved[116]     @ +12 = 0xFF (post-erase default)
[System.BitConverter]::GetBytes([uint32]1).CopyTo($seed, 0)
[System.BitConverter]::GetBytes([uint32]0).CopyTo($seed, 4)
[System.BitConverter]::GetBytes([uint32]1).CopyTo($seed, 8)
for ($i = 12; $i -lt 128; $i++) { $seed[$i] = 0xFF }

# FwBankHeader at offset 0x80 (128 bytes)
#   uint32 magic            @ +0   = 0x4E4F5641 ("NOVA")
#   uint32 image_size       @ +4   = $ImageSize
#   uint32 image_crc        @ +8   = $ImageCrc
#   uint32 valid            @ +12  = $Valid ? 1 : 0
#   uint32 active           @ +16  = 1
#   uint32 sequence         @ +20  = 1
#   char   version[32]      @ +24  = $Version (null-padded)
#   uint8  reserved[80]     @ +56  = 0xFF
$hdrBase = 0x80
[System.BitConverter]::GetBytes([uint32]0x4E4F5641).CopyTo($seed, $hdrBase + 0)
[System.BitConverter]::GetBytes($ImageSize).CopyTo(           $seed, $hdrBase + 4)
[System.BitConverter]::GetBytes($ImageCrc).CopyTo(            $seed, $hdrBase + 8)
[System.BitConverter]::GetBytes([uint32]($(if ($Valid) {1} else {0}))).CopyTo($seed, $hdrBase + 12)
[System.BitConverter]::GetBytes([uint32]1).CopyTo(            $seed, $hdrBase + 16)
[System.BitConverter]::GetBytes([uint32]1).CopyTo(            $seed, $hdrBase + 20)

# version[32] -- null-terminated string, null-padded
$verBytes = [System.Text.Encoding]::ASCII.GetBytes($Version)
$verLen   = [Math]::Min($verBytes.Length, 31)  # leave room for null
for ($i = 0; $i -lt $verLen; $i++) { $seed[$hdrBase + 24 + $i] = $verBytes[$i] }
$seed[$hdrBase + 24 + $verLen] = 0  # null terminator
# Rest of version[32] stays 0 (initialized as such -- note: not 0xFF)

# reserved[80] @ +56 = 0xFF
for ($i = 56; $i -lt 128; $i++) { $seed[$hdrBase + $i] = 0xFF }

# --- Write to temp file ------------------------------------------------
$tmpDir  = Join-Path $env:TEMP "constellation-f2c"
if (-not (Test-Path $tmpDir)) { New-Item -ItemType Directory -Path $tmpDir | Out-Null }
$tmpFile = Join-Path $tmpDir "seed_meta_block.bin"
[System.IO.File]::WriteAllBytes($tmpFile, $seed)

$validStr = if ($Valid) { "VALID" } else { "INVALID" }
Write-Host "=== F2c seed metadata block ===" -ForegroundColor Cyan
Write-Host "  Probe:      $Probe"
Write-Host "  Block size: 256 bytes (FwBootMeta + Bank A FwBankHeader)"
Write-Host "  Bank A:     $validStr (valid=$($Valid -as [int]), magic=NOVA, sequence=1, active=1)"
Write-Host "  Image size: $ImageSize bytes"
Write-Host "  Image CRC:  0x$($ImageCrc.ToString('X8'))"
Write-Host "  Version:    `"$Version`""
Write-Host "  OSPI offset: 0x300000"
Write-Host "  Temp file:   $tmpFile"
Write-Host ""

# --- Probe to ccxml mapping --------------------------------------------
$ccxmlMap = @{
    'A' = "$LpDir\AM2434_LP_A.ccxml"     # STORAGE  S24L0417
    'N' = "$LpDir\AM2434_LP_NOVA.ccxml"  # CONTROLLER S24L0707
    'P' = "$LpDir\AM2434_LP_B.ccxml"     # GDC      S24L0727 (legacy name LP_B)
    'T' = "$LpDir\AM2434_LP_T.ccxml"     # TRITON   S24L0957
}
$ccxml = $ccxmlMap[$Probe]
if (-not (Test-Path $ccxml)) {
    Write-Host "[ERROR] ccxml not found: $ccxml" -ForegroundColor Red
    exit 1
}

# --- xdsdfu single-probe check (invariant #7) --------------------------
$xdsdfu = "C:\ti\ccs2050\ccs\ccs_base\common\uscif\xds110\xdsdfu.exe"
if (Test-Path $xdsdfu) {
    $present = @(& $xdsdfu -e 2>&1 | Select-String 'Serial(?: Num)?:\s*(\S+)' | ForEach-Object {
        $_.Matches[0].Groups[1].Value
    })
    if ($present.Count -gt 1) {
        Write-Host "[ERROR] Multiple XDS110 probes attached. Unplug all but the target." -ForegroundColor Red
        Write-Host "        Present: $($present -join ', ')" -ForegroundColor Yellow
        exit 1
    }
    if ($present.Count -eq 1) {
        Write-Host "  Single probe enumerated: $($present[0])" -ForegroundColor Green
    }
}

# --- Drive uniflash_run.js ---------------------------------------------
$env:LP_CCXML        = $ccxml
$env:UNIFLASH_FILE   = $tmpFile -replace '\\', '/'   # DSS Java wants forward slashes
$env:UNIFLASH_OFFSET = "0x300000"

$dss = "C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat"
$uniflashScript = "$LpDir\ospi_flash\uniflash_run.js"

Write-Host "=== Running uniflash_run.js ===" -ForegroundColor Cyan
Write-Host "  ccxml:   $ccxml"
Write-Host "  file:    $env:UNIFLASH_FILE"
Write-Host "  offset:  $env:UNIFLASH_OFFSET"
Write-Host ""

& $dss $uniflashScript
$dssExit = $LASTEXITCODE

if ($dssExit -eq 0) {
    Write-Host ""
    Write-Host "=== Seed metadata written ===" -ForegroundColor Green
    Write-Host "Next boot trace should show: [SBL] bank=A seq=1 off=0x080000 boots=2 strikes=2 reason=0"
    if (-not $Valid) {
        Write-Host "  (NEGATIVE TEST -- valid=0, so SBL should reject Bank A and fall back to Bank B if it exists, else Golden)" -ForegroundColor Yellow
    }
} else {
    Write-Host ""
    Write-Host "=== Seed write FAILED (dss exit $dssExit) ===" -ForegroundColor Red
    Write-Host "Check the DSS output above. Common issues:"
    Write-Host "  - Wrong probe / multiple probes attached"
    Write-Host "  - LP not in a JTAG-debuggable state (try power-cycling the LP USB cable)"
    Write-Host "  - uniflash_run.js timeout (flasher app didn't reach poll loop)"
    exit $dssExit
}
