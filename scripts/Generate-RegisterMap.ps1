<#
.SYNOPSIS
  Regenerate the auto-extracted section of docs/register-and-ui-map.md.

.DESCRIPTION
  Walks the Nova firmware tree for `#define HR_* <addr>` lines and the
  proto envelope for top-level field tags, then rewrites the markdown
  block bounded by the AUTO-GEN markers in docs/register-and-ui-map.md.
  The hand-curated UI columns above/below the auto-gen block are
  preserved.

  Run after touching:
    - Nova_Firmware/lp_am2434/orbit_server/orbit_*.h
    - proto/agristar/envelope.proto

.NOTES
  Read-only against firmware + proto. Only edits docs/register-and-ui-map.md.
#>

[CmdletBinding()]
param(
    [string]$Doc = 'F:\Constellation\docs\register-and-ui-map.md'
)

$ErrorActionPreference = 'Stop'

# --- 1. Scan HR_* defines ----------------------------------------------
$root = 'F:\Constellation\Nova_Firmware\lp_am2434\orbit_server'
$hrFiles = Get-ChildItem -Path $root -Recurse -Include '*.h' -File

# Resolve "BASE + N" expressions: build a symbol -> numeric map by
# making two passes (literals first, then expressions referencing them).
$symMap = @{}
$rows   = @()  # each row: @{ Symbol; Addr; File; Line; Comment; Group }

foreach ($f in $hrFiles) {
    $lines = Get-Content -LiteralPath $f.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        # Match: #define HR_FOO_BAR  123U  /* optional comment */
        if ($line -match '^\s*#define\s+(HR_[A-Z0-9_]+)\s+(.+?)\s*(?:/\*\s*(.+?)\s*\*/|//\s*(.*))?\s*$') {
            $sym = $Matches[1]
            $expr = ($Matches[2] -replace 'U(?![A-Z0-9_])','').Trim()
            $cmt = if ($Matches[3]) { $Matches[3] } elseif ($Matches[4]) { $Matches[4] } else { '' }

            # Try numeric literal first
            $addr = $null
            if ($expr -match '^\d+$') {
                $addr = [int]$expr
                $symMap[$sym] = $addr
            }
            # else: defer to second pass
            $rel = (Resolve-Path -LiteralPath $f.FullName -Relative:$false).Path -replace [regex]::Escape('F:\Constellation\'),''
            $rows += [pscustomobject]@{
                Symbol = $sym
                Expr   = $expr
                Addr   = $addr
                File   = ($rel -replace '\\','/')
                Line   = $i + 1
                Comment = $cmt
                Group  = (Split-Path $f.BaseName -Leaf)
            }
        }
    }
}

# Second pass: resolve simple "BASE + 5" / "BASE - 1" / bare symbol references
foreach ($r in $rows) {
    if ($null -ne $r.Addr) { continue }
    if ($r.Expr -match '^([A-Z0-9_]+)\s*([+\-])?\s*(\d+)?$') {
        $base = $Matches[1]; $op = $Matches[2]; $delta = $Matches[3]
        if ($symMap.ContainsKey($base)) {
            $val = $symMap[$base]
            if ($op -eq '+') { $val += [int]$delta }
            elseif ($op -eq '-') { $val -= [int]$delta }
            $r.Addr = $val
            $symMap[$r.Symbol] = $val
        }
    }
}

# --- 2. Scan proto envelope tags ---------------------------------------
$envelopeProto = 'F:\Constellation\proto\agristar\envelope.proto'
$envFields = @()
if (Test-Path $envelopeProto) {
    $inEnv = $false
    $lines = Get-Content -LiteralPath $envelopeProto
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $l = $lines[$i]
        if ($l -match '^\s*message\s+Envelope\b') { $inEnv = $true; continue }
        if ($inEnv -and $l -match '^\s*\}') { break }
        if (-not $inEnv) { continue }
        # oneof field: `<Type> <name> = <tag>;`
        if ($l -match '^\s*(\w+)\s+(\w+)\s*=\s*(\d+)\s*;') {
            $envFields += [pscustomobject]@{
                Tag   = [int]$Matches[3]
                Name  = $Matches[2]
                Type  = $Matches[1]
                Line  = $i + 1
            }
        }
    }
}

# --- 3. Render the auto-gen block --------------------------------------
$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine('<!-- AUTO-GEN:BEGIN  do not edit by hand; regenerate via scripts/Generate-RegisterMap.ps1 -->')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('### Modbus holding registers (auto-extracted from `Nova_Firmware/.../orbit_server/*.h`)')
[void]$sb.AppendLine('')

$byGroup = $rows | Group-Object Group | Sort-Object Name
foreach ($g in $byGroup) {
    [void]$sb.AppendLine("#### $($g.Name)")
    [void]$sb.AppendLine('')
    [void]$sb.AppendLine('| HR addr | Symbol | Comment | Source |')
    [void]$sb.AppendLine('|---:|---|---|---|')
    foreach ($r in ($g.Group | Sort-Object { if ($_.Addr) { $_.Addr } else { 999999 } })) {
        $addrStr = if ($null -ne $r.Addr) { $r.Addr } else { '`' + $r.Expr + '`' }
        $cmt = ($r.Comment -replace '\|','\|')
        [void]$sb.AppendLine("| $addrStr | ``$($r.Symbol)`` | $cmt | [$($r.File):$($r.Line)]($($r.File)#L$($r.Line)) |")
    }
    [void]$sb.AppendLine('')
}

[void]$sb.AppendLine('### Envelope proto tags (auto-extracted from `proto/agristar/envelope.proto`)')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('| Tag | Field name | Message type | Line |')
[void]$sb.AppendLine('|---:|---|---|---:|')
foreach ($f in ($envFields | Sort-Object Tag)) {
    [void]$sb.AppendLine("| $($f.Tag) | ``$($f.Name)`` | ``$($f.Type)`` | $($f.Line) |")
}
[void]$sb.AppendLine('')
[void]$sb.AppendLine('<!-- AUTO-GEN:END -->')

$autoBlock = $sb.ToString().TrimEnd()

# --- 4. Splice into the doc, preserving hand-curated content -----------
if (-not (Test-Path $Doc)) {
    throw "Target doc does not exist: $Doc -- create it with the AUTO-GEN markers first."
}
$existing = Get-Content -LiteralPath $Doc -Raw
if ($existing -notmatch '<!-- AUTO-GEN:BEGIN' -or $existing -notmatch '<!-- AUTO-GEN:END -->') {
    throw "Could not find AUTO-GEN markers in $Doc."
}
$prefix, $rest = $existing -split '<!-- AUTO-GEN:BEGIN', 2
$_, $suffix     = $rest -split '<!-- AUTO-GEN:END -->', 2
$final = $prefix + $autoBlock + $suffix
Set-Content -LiteralPath $Doc -Value $final -Encoding UTF8 -NoNewline

Write-Host "[ok] Updated auto-gen block in $Doc"
Write-Host "     - HR symbols: $($rows.Count)"
Write-Host "     - Envelope fields: $($envFields.Count)"
