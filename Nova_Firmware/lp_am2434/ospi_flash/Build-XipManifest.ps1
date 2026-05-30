<#
.SYNOPSIS
  Parse a TI MulticoreELF XIP container, extract each PT_LOAD segment to
  its own .bin file, and write a manifest line list:
      <flash_offset_hex> <abs_path>
  ...for consumption by uniflash_run.js (UNIFLASH_MANIFEST mode).

.DESCRIPTION
  Companion to the OSPI XIP migration. The TI flow is:
    - app builds nova_lp.release.mcelf.hs_fs   (non-XIP segments + cert)
    - app builds nova_lp.release.mcelf_xip     (XIP-range PT_LOAD segments)
  TI's uart_uniflash.py walks the second file's PT_LOAD entries and
  flashes each at p_paddr & 0x07FFFFFF (mask the OSPI XIP base
  0x60000000 to get the in-flash byte offset).

  This script does the same in PowerShell so Flash-LP.ps1 can drive
  multi-segment flashing through our existing JTAG auto-flasher rather
  than depending on TI's XMODEM-over-UART tool.

  Mirrors uart_uniflash.py:send_mcelf_xip() - see TI SDK
  tools/boot/uart_uniflash.py:358.

.PARAMETER XipElf
  Path to nova_lp.release.mcelf_xip.

.PARAMETER HsFs
  Path to nova_lp.release.mcelf.hs_fs (flashed at 0x80000).

.PARAMETER OutDir
  Directory to drop per-segment .bin files + manifest.txt into.

.OUTPUTS
  Writes manifest.txt (one entry per line). Returns the manifest path.
#>
param(
    [Parameter(Mandatory)] [string] $XipElf,
    [Parameter(Mandatory)] [string] $HsFs,
    [Parameter(Mandatory)] [string] $OutDir
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $XipElf)) { throw "XipElf not found: $XipElf" }
if (-not (Test-Path $HsFs))   { throw "HsFs not found:   $HsFs" }
$null = New-Item -ItemType Directory -Force -Path $OutDir

$bytes = [System.IO.File]::ReadAllBytes($XipElf)
if ($bytes.Length -lt 52) { throw "XipElf too small to be ELF: $($bytes.Length) bytes" }

# ELF magic
if ($bytes[0] -ne 0x7F -or $bytes[1] -ne 0x45 -or $bytes[2] -ne 0x4C -or $bytes[3] -ne 0x46) {
    throw "Not an ELF file (bad magic): $XipElf"
}
# 32-bit only (AM2434 R5F)
if ($bytes[4] -ne 1) { throw "ELFCLASS != 32 (got $($bytes[4]))" }
if ($bytes[5] -ne 1) { throw "ELFDATA != little-endian (got $($bytes[5]))" }

function Get-U32 {
    param([byte[]] $b, [int] $o)
    # PowerShell's -shl on byte values does NOT zero-extend the way C does;
    # the OR'd composition silently returns only the low byte. Using
    # [BitConverter] avoids the trap. (Discovered while building this.)
    return [System.BitConverter]::ToUInt32($b, $o)
}
function Get-U16 {
    param([byte[]] $b, [int] $o)
    return [System.BitConverter]::ToUInt16($b, $o)
}

# ELF32 header layout (offsets):
#   e_phoff     @ 28  (program header table file offset)
#   e_phentsize @ 42  (size of one program header entry)
#   e_phnum     @ 44  (number of program header entries)
$ePhoff     = Get-U32 $bytes 28
$ePhentsize = Get-U16 $bytes 42
$ePhnum     = Get-U16 $bytes 44

if ($ePhentsize -ne 32) {
    throw "Unexpected e_phentsize ($ePhentsize), expected 32 for ELF32"
}

Write-Host "[xip-manifest] $XipElf : $ePhnum program headers @ offset 0x$($ePhoff.ToString('X'))" -ForegroundColor DarkGray

# ELF32 program header layout (offsets within entry):
#   p_type   @  0  (1 = PT_LOAD)
#   p_offset @  4
#   p_vaddr  @  8
#   p_paddr  @ 12
#   p_filesz @ 16
#   p_memsz  @ 20
$manifestLines = New-Object System.Collections.Generic.List[string]
$segIdx = 0

for ($i = 0; $i -lt $ePhnum; $i++) {
    $base = [int]$ePhoff + $i * 32
    $pType   = Get-U32 $bytes $base
    if ($pType -ne 1) { continue }   # only PT_LOAD
    $pOffset = Get-U32 $bytes ($base + 4)
    $pVaddr  = Get-U32 $bytes ($base + 8)
    $pPaddr  = Get-U32 $bytes ($base + 12)
    $pFilesz = Get-U32 $bytes ($base + 16)
    if ($pFilesz -eq 0) { continue }  # skip BSS-only segments

    # Mask to OSPI byte offset (TI: p_paddr & 0x7ffffff). This converts
    # the memory-mapped XIP address (e.g. 0x60100000) into the flash
    # offset to write to (e.g. 0x100000).
    $flashOffset = $pPaddr -band 0x07FFFFFF

    $segPath = Join-Path $OutDir ("xip_seg{0:D2}_v{1:X8}_p{2:X8}.bin" -f $segIdx, $pVaddr, $pPaddr)
    $segBytes = New-Object byte[] $pFilesz
    [System.Array]::Copy($bytes, [int]$pOffset, $segBytes, 0, [int]$pFilesz)
    [System.IO.File]::WriteAllBytes($segPath, $segBytes)

    $line = "0x{0:X} {1}" -f $flashOffset, ($segPath -replace '\\','/')
    $manifestLines.Add($line)
    Write-Host ("  PT_LOAD #{0,2}  vaddr=0x{1:X8}  paddr=0x{2:X8}  filesz={3,8}  -> flash 0x{4:X}" -f
        $segIdx, $pVaddr, $pPaddr, $pFilesz, $flashOffset) -ForegroundColor DarkGray
    $segIdx++
}

if ($manifestLines.Count -eq 0) {
    throw "No PT_LOAD segments with non-zero filesz in $XipElf - nothing to flash. (Did syscfg keep section3.load_memory=MSRAM?)"
}

# Append the non-XIP image at its conventional offset.
$manifestLines.Add("0x80000 " + ($HsFs -replace '\\','/'))
Write-Host ("  hs_fs       paddr=N/A         size={0,8}  -> flash 0x80000" -f (Get-Item $HsFs).Length) -ForegroundColor DarkGray

$manifestPath = Join-Path $OutDir 'manifest.txt'
$header = @(
    "# Constellation XIP flash manifest",
    "# Generated $(Get-Date -Format o)",
    "# Format: <offset_hex> <abs_path>",
    "# Consumed by uniflash_run.js when UNIFLASH_MANIFEST is set."
)
[System.IO.File]::WriteAllLines($manifestPath, ($header + $manifestLines))
Write-Host "[xip-manifest] wrote $manifestPath ($($manifestLines.Count) entries)" -ForegroundColor Green
return $manifestPath
