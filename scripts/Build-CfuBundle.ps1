<#
.SYNOPSIS
    Build a Constellation Firmware Update (.cfu) bundle for the OTA update path.

.DESCRIPTION
    Packages one or more `.mcelf.hs_fs` orbit firmware images plus a
    generated `manifest.json` into a single `.cfu` zip file.  The bridge
    server reads the resulting bundle via `firmwareBundle.ts` when the
    user uploads it on the /level1/version UI page.

    Phase 1 — plain zip, no password.  Phase 2 will add AES encryption.

.PARAMETER OutputPath
    Where to write the resulting .cfu file. Default:
    F:\Constellation\firmware-bundles\constellation-<version>.cfu

.PARAMETER Version
    Bundle version string for the manifest.  Default reads
    `docs/firmware-version-current.md`.

.PARAMETER ControllerImage
    Path to the CONTROLLER role .mcelf.hs_fs.  Omit to skip.

.PARAMETER StorageImage
    Path to the STORAGE role .mcelf.hs_fs.  Omit to skip.

.PARAMETER GdcImage
    Path to the GDC role .mcelf.hs_fs.  Omit to skip.

.PARAMETER TritonImage
    Path to the TRITON role .mcelf.hs_fs.  Omit to skip.

.EXAMPLE
    # Build a single-component .cfu containing the current Nova CONTROLLER build:
    .\scripts\Build-CfuBundle.ps1 `
      -ControllerImage F:\Constellation\Nova_Firmware\lp_am2434\ti-arm-clang\nova_lp.release.mcelf.hs_fs

.EXAMPLE
    # Build a full bench bundle (all four roles, assuming you've built each):
    .\scripts\Build-CfuBundle.ps1 `
      -Version "0.A.190-test" `
      -ControllerImage .\controller-build\nova_lp.release.mcelf.hs_fs `
      -StorageImage    .\storage-build\nova_lp.release.mcelf.hs_fs `
      -OutputPath      F:\Constellation\firmware-bundles\bench-0.A.190.cfu
#>

[CmdletBinding()]
param(
    [string]$OutputPath,
    [string]$Version,
    [string]$ControllerImage,
    [string]$StorageImage,
    [string]$GdcImage,
    [string]$TritonImage
)

$ErrorActionPreference = 'Stop'

# ─── Default version ───────────────────────────────────────────────────
if (-not $Version) {
    $versionFile = 'F:\Constellation\docs\firmware-version-current.md'
    if (Test-Path $versionFile) {
        # File format puts the current version in a `**`0.A.<n>`**` marker
        # somewhere in the doc — grep the whole file rather than only the
        # first line.
        $allText = Get-Content $versionFile -Raw
        if ($allText -match '0\.[A-Z]\.\d+') {
            $Version = $Matches[0]
        }
    }
    if (-not $Version) { $Version = "0.0.0-dev" }
}

# ─── Default output path ───────────────────────────────────────────────
if (-not $OutputPath) {
    $bundleDir = 'F:\Constellation\firmware-bundles'
    if (-not (Test-Path $bundleDir)) {
        New-Item -ItemType Directory -Path $bundleDir | Out-Null
    }
    $OutputPath = Join-Path $bundleDir "constellation-$Version.cfu"
}

# ─── Collect components ────────────────────────────────────────────────
$components = @{}

function Add-Component {
    param(
        [string]$Name, [string]$ImagePath, [int]$Slot, [int]$Role, [string]$Ip, [string]$InnerName
    )
    if (-not $ImagePath) { return }
    if (-not (Test-Path $ImagePath)) {
        throw "Component $Name : file not found: $ImagePath"
    }

    # SAFETY GUARD — validate the binary's build stamp matches the
    # declared role. Flash-LP.ps1 emits .last-build-role.json next to
    # the .mcelf.hs_fs file describing what role+IP the binary was
    # built for. We refuse to package a mismatching binary because
    # pushing it via OTA would silently brick the target (the IP and
    # role get baked into the firmware at gmake time).
    #
    # See memories/repo/cfu-firmware-bundle-design.md "per-role
    # makefile pending" — until that lands and produces role-tagged
    # output filenames, this guard is the only thing protecting us
    # from cross-role packaging.
    $stampPath = Join-Path (Split-Path $ImagePath -Parent) '.last-build-role.json'
    if (Test-Path $stampPath) {
        try {
            $stamp = Get-Content $stampPath -Raw | ConvertFrom-Json
            if ($null -ne $stamp.role -and [int]$stamp.role -ne $Role) {
                $stampRoleName = switch ([int]$stamp.role) {
                    0 { 'CONTROLLER' } 1 { 'STORAGE' } 2 { 'GDC' } 3 { 'TRITON' }
                    default { "role=$($stamp.role)" }
                }
                $expectedRoleName = switch ($Role) {
                    0 { 'CONTROLLER' } 1 { 'STORAGE' } 2 { 'GDC' } 3 { 'TRITON' }
                    default { "role=$Role" }
                }
                throw @"
REFUSING to package component '$Name' as $expectedRoleName: the binary at
  $ImagePath
was last built as $stampRoleName (per $stampPath). Rebuild for $expectedRoleName via
  Flash-LP.ps1 -Probe <N|A|B|T> -Role $expectedRoleName -Ip $Ip -BuildOnly
or use a role-tagged output filename once the per-role makefile lands.
"@
            }
        } catch [System.Management.Automation.RuntimeException] {
            # Stamp present but unreadable — pass-through warning.
            Write-Warning "Could not parse build stamp $stampPath : $($_.Exception.Message)"
        }
    } else {
        Write-Warning "No build stamp at $stampPath; cannot verify '$Name' matches $Role. Continuing."
    }

    $sha = (Get-FileHash -Path $ImagePath -Algorithm SHA256).Hash.ToLower()
    Write-Host "  + $Name -> $InnerName  ($([math]::Round(((Get-Item $ImagePath).Length / 1KB), 1)) KB, sha256=$($sha.Substring(0, 12))...)"
    $components[$Name] = [ordered]@{
        file   = $InnerName
        slot   = $Slot
        role   = $Role
        ip     = $Ip
        sha256 = $sha
    }
    return $ImagePath
}

Write-Host ""
Write-Host "Building .cfu bundle:"
Write-Host "  version: $Version"
Write-Host "  output:  $OutputPath"
Write-Host ""

$sourceFiles = @{}
$p = Add-Component -Name 'controller' -ImagePath $ControllerImage -Slot -1 -Role 0 -Ip '10.47.27.1' -InnerName 'controller.mcelf.hs_fs'
if ($p) { $sourceFiles['controller.mcelf.hs_fs'] = $p }

$p = Add-Component -Name 'storage'    -ImagePath $StorageImage    -Slot  0 -Role 1 -Ip '10.47.27.2' -InnerName 'storage.mcelf.hs_fs'
if ($p) { $sourceFiles['storage.mcelf.hs_fs'] = $p }

$p = Add-Component -Name 'gdc'        -ImagePath $GdcImage        -Slot  1 -Role 2 -Ip '10.47.27.3' -InnerName 'gdc.mcelf.hs_fs'
if ($p) { $sourceFiles['gdc.mcelf.hs_fs'] = $p }

$p = Add-Component -Name 'triton'     -ImagePath $TritonImage     -Slot  2 -Role 3 -Ip '10.47.27.4' -InnerName 'triton.mcelf.hs_fs'
if ($p) { $sourceFiles['triton.mcelf.hs_fs'] = $p }

if ($components.Count -eq 0) {
    throw "No components provided. Pass at least one of -ControllerImage / -StorageImage / -GdcImage / -TritonImage."
}

# ─── Build manifest ────────────────────────────────────────────────────
$manifest = [ordered]@{
    schema     = 'constellation-firmware/v1'
    version    = $Version
    build_date = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    components = $components
}

# ─── Stage everything in a temp dir, then zip ──────────────────────────
$stagingDir = Join-Path $env:TEMP "cfu-build-$(Get-Random)"
New-Item -ItemType Directory -Path $stagingDir | Out-Null

try {
    # Write manifest.json — UTF-8 WITHOUT BOM. PowerShell 5.1's
    # `Set-Content -Encoding utf8` writes a BOM, which Node's JSON.parse
    # chokes on ("Unexpected token '﻿'"). Use the .NET method with an
    # explicit no-BOM UTF8Encoding instance — works on PS 5.1 and 7+.
    $manifestJson = $manifest | ConvertTo-Json -Depth 10
    $manifestPath = Join-Path $stagingDir 'manifest.json'
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($manifestPath, $manifestJson, $utf8NoBom)

    # Copy each component file in with its inner name
    foreach ($innerName in $sourceFiles.Keys) {
        Copy-Item -Path $sourceFiles[$innerName] -Destination (Join-Path $stagingDir $innerName)
    }

    # Zip up the staging dir contents (NOT the dir itself).  PowerShell's
    # Compress-Archive ONLY accepts a `.zip` extension on -DestinationPath
    # (not `.zip.tmp` or `.cfu`), so we zip to a sibling `.zip` and then
    # rename to the requested `.cfu`.
    if (Test-Path $OutputPath) { Remove-Item $OutputPath -Force }
    $zipSource = Get-ChildItem -Path $stagingDir | Select-Object -ExpandProperty FullName
    $tempZipPath = "$OutputPath.zip"
    if (Test-Path $tempZipPath) { Remove-Item $tempZipPath -Force }
    Compress-Archive -Path $zipSource -DestinationPath $tempZipPath -CompressionLevel Optimal
    Move-Item -Path $tempZipPath -Destination $OutputPath

    $finalSize = [math]::Round(((Get-Item $OutputPath).Length / 1KB), 1)
    Write-Host ""
    Write-Host "Built: $OutputPath ($finalSize KB, $($components.Count) component(s))" -ForegroundColor Green
} finally {
    if (Test-Path $stagingDir) {
        Remove-Item -Path $stagingDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
