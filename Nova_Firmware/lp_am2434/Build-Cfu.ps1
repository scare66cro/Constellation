#requires -Version 5.1
<#
.SYNOPSIS
    Build a Constellation Firmware Update (.cfu) bundle for OTA testing.

.DESCRIPTION
    Compiles the LP-AM2434 firmware as a UNIVERSAL binary -- same gmake
    pipeline as Flash-LP.ps1, but WITHOUT the `-DCONFIG_NOVA_LP_ROLE` and
    `-DCONFIG_NOVA_LP_IP` flags. The resulting `.hs_fs` carries every
    role's code paths (CONTROLLER + STORAGE + GDC + TRITON dispatch
    branches; main.c picks at runtime via `OrbitRole_Get()` reading
    `lp_device_config.role` from OSPI) and CRITICALLY does NOT contain
    the factory-provisioning override block in `lp_device_config.c:309-339`
    (that block is `#if defined(CONFIG_NOVA_LP_IP) && (CONFIG_NOVA_LP_IP
    != 0U)` -- compiled out when the flag is unset).

    Why this matters for OTA: a CONFIG_NOVA_LP_IP-baked binary, when
    OTA'd onto a board whose stored IP differs, ACTIVELY OVERWRITES the
    target board's OSPI device-config record with the binary's
    compile-time values on first boot. That's how a CONTROLLER-baked
    binary OTA'd onto a STORAGE board makes the STORAGE board come back
    up as CONTROLLER at 10.47.27.1, fighting the real controller for
    the IP. (Bench-confirmed 2026-05-20; see
    memories/repo/cfu-baked-role-overrode-ospi.md.)

    The universal binary has no role-specific bake -- its boot path reads
    OSPI device-config and uses whatever role is persisted there. JTAG
    flashes via Flash-LP.ps1 still pass the flags so the override block
    writes OSPI on initial provisioning. OTA flashes (this script) skip
    them so existing OSPI records are preserved.

    The same .hs_fs is referenced by all four manifest components -- the
    file is identical; only the per-component (role, slot, ip) metadata
    differs so the bridge routes each push to the correct LP via
    FwBankInfo.current_role. The LP-side G1-LP role gate still fires:
    bridge sends `expected_role` from the manifest, LP compares against
    its persisted role, mismatch = reject. (See
    docs/lp-am2434-ota-hardening-plan.md.)

    The bundle is written to `firmware-bundles/constellation-<version>.cfu`.

    Cross-references:
      - docs/lp-am2434-ota-hardening-plan.md
      - constellation-ui/server/src/orbitFleetResolver.ts
      - constellation-ui/server/src/firmwareInstaller.ts

.PARAMETER SkipBuild
    Reuse the existing `nova_lp.release.mcelf.hs_fs` instead of rebuilding.
    Use when iterating on the manifest layout without changing C code.

.PARAMETER Version
    Override the version string. Defaults to whatever
    `docs/firmware-version-current.md` declares (parsed from the first
    `**`0.A.<N>`**` line).

.PARAMETER ControllerIp
    IP of the CONTROLLER board (default 10.47.27.1).

.PARAMETER StorageIp
    IP of the STORAGE board (default 10.47.27.2).

.PARAMETER GdcIp
    IP of the GDC board (default 10.47.27.3).

.PARAMETER TritonIp
    IP of the TRITON board (default 10.47.27.4).

.EXAMPLE
    .\Build-Cfu.ps1
    # Builds 0.A.188 firmware, packs constellation-0.A.188.cfu with
    # 4 components routed to the standard 10.47.27.{1,2,3,4} bench IPs.

.EXAMPLE
    .\Build-Cfu.ps1 -SkipBuild
    # Repacks the bundle from the last build artifact -- useful for
    # iterating on the manifest without re-compiling.
#>
param(
    [switch]$SkipBuild,
    [string]$Version = '',
    [string]$ControllerIp = '10.47.27.1',
    [string]$StorageIp    = '10.47.27.2',
    [string]$GdcIp        = '10.47.27.3',
    [string]$TritonIp     = '10.47.27.4',
    # Emit a .cfu with ONLY the storage component (slot=0, role=1, ip=$StorageIp).
    # Used during the first-light dry-run after JTAG-flashing one board to
    # the new firmware -- keeps the install loop from trying to route to
    # boards still on the old firmware (which would noMatch the resolver).
    [switch]$StorageOnly,
    # Emit a .cfu with ONLY the controller component (slot=-1, role=0, ip=$ControllerIp).
    # Used to bench-test the controller self-update path (nova_fw_update.c)
    # in isolation from the orbit OTA path (lp_ota_task.c) when orbit boards
    # are wedged or unavailable. Install goes straight to controller-self-update.
    [switch]$ControllerOnly,
    # Emit a .cfu with ONLY the triton component (slot=2, role=3, ip=$TritonIp).
    # Used when one of the other orbits is offline and would otherwise abort
    # the full-fleet install at the broker fleet-probe gate. Mirrors
    # -StorageOnly. Added 2026-05-31 during OrbitFinalize integration bench
    # (GDC offline blocked full-fleet round 2).
    [switch]$TritonOnly,
    # F2c manufacturing-bundle flag -- also stage the SBL chooser binary into
    # the .cfu zip and add an `sbl_chooser` manifest entry. The runtime
    # OTA installer does NOT auto-flash the SBL today (separate design
    # decision tracked in docs/lp-am2434-f2c-sbl-chooser-design.md §8.3);
    # this is for the manufacturing pipeline that uses Commission-LP.ps1
    # at the workstation to flash fresh boards. The manifest entry is also
    # useful for cross-checking the deployed SBL version against the
    # built-against version at install time. SBL chooser binary is
    # expected at sbl_chooser/r5fss0-0_nortos/ti-arm-clang/sbl_chooser.release.tiimage.
    [switch]$IncludeSbl
)
$ErrorActionPreference = 'Stop'

$RepoRoot   = 'F:\Constellation'
$BuildDir   = "$RepoRoot\Nova_Firmware\lp_am2434\ti-arm-clang"
$ObjFile    = "$BuildDir\nova_lp.release.mcelf.hs_fs"
$BundlesDir = "$RepoRoot\firmware-bundles"

# ─── Resolve target version ─────────────────────────────────────────────
if (-not $Version) {
    $verDoc = Get-Content "$RepoRoot\docs\firmware-version-current.md" -Raw
    $m = [regex]::Match($verDoc, '\*\*`(0\.A\.\d+)`\*\*')
    if (-not $m.Success) {
        throw "Could not parse current version from docs/firmware-version-current.md"
    }
    $Version = $m.Groups[1].Value
}

# Append a git-short-sha and -dirty if applicable, matching Flash-LP.ps1.
$gitSha = (& git -C $RepoRoot rev-parse --short=8 HEAD).Trim()
$gitDirty = $false
$dirtyCheck = (& git -C $RepoRoot status --porcelain 2>$null)
if ($dirtyCheck) { $gitDirty = $true }
$fwVersion = if ($gitDirty) { "$Version+$gitSha-dirty" } else { "$Version+$gitSha" }

Write-Host "=== Build-Cfu ===" -ForegroundColor Cyan
Write-Host "Target version : $fwVersion"
Write-Host "Bundle dir     : $BundlesDir"
Write-Host ""

# ─── Build the firmware (optional skip) ─────────────────────────────────
if (-not $SkipBuild) {
    Write-Host "[build] gmake PROFILE=release (UNIVERSAL - no CONFIG_NOVA_LP_ROLE/IP flags)" -ForegroundColor Yellow
    $gmake = 'C:\ti\ccs2050\ccs\utils\bin\gmake.exe'
    if (-not (Test-Path $gmake)) { throw "gmake not found at $gmake" }
    $env:CCS_PATH    = 'C:/ti/ccs2050/ccs'
    $env:SYSCFG_PATH = 'C:/ti/sysconfig_1.27.0'

    # Generate lp_version_injected.h with the bumped version (matches
    # Flash-LP.ps1's pattern -- see that file's comment for why we use a
    # generated header rather than a -D flag).
    $injected = "$RepoRoot\Nova_Firmware\lp_am2434\lp_version_injected.h"
    $injectedContent = @"
/* AUTO-GENERATED by Build-Cfu.ps1 -- do not edit, do not commit. */
#ifndef LP_FW_VERSION
#define LP_FW_VERSION "$fwVersion"
#endif
"@
    Set-Content -Path $injected -Value $injectedContent -Encoding ASCII

    Push-Location $BuildDir
    try {
        # Force-rebuild role/IP/version/OTA-baking objects.
        foreach ($obj in @('lp_device_config.obj', 'main.obj', 'lp_ota_task.obj')) {
            $p = "obj\release\$obj"
            if (Test-Path $p) { Remove-Item $p -Force }
        }
        $oldEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            # No CONFIG_NOVA_LP_ROLE / CONFIG_NOVA_LP_IP flags -- see
            # script header for rationale. Universal binary, OSPI-driven role.
            & $gmake PROFILE=release all 2>&1 | Out-Host
        } finally {
            $ErrorActionPreference = $oldEap
        }
    } finally {
        Pop-Location
    }
}

if (-not (Test-Path $ObjFile)) {
    throw "Build artifact not found: $ObjFile (build failed or -SkipBuild used with no prior build)"
}

# ─── Stage assets into a working directory ──────────────────────────────
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) "constellation-cfu-$(Get-Random)"
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

try {
    $stagedHsFs = Join-Path $tmp 'nova_lp.release.mcelf.hs_fs'
    Copy-Item $ObjFile $stagedHsFs -Force

    # sha256 (lowercase hex) -- must match what loadBundle() verifies.
    $sha = (Get-FileHash $stagedHsFs -Algorithm SHA256).Hash.ToLowerInvariant()
    $size = (Get-Item $stagedHsFs).Length
    Write-Host ""
    Write-Host "[stage] $stagedHsFs"
    Write-Host "        size   = $size bytes"
    Write-Host "        sha256 = $sha"

    # ─── Generate manifest with 4 role entries ─────────────────────────
    # All four components reference the SAME file -- the LP role check is
    # against OSPI-persisted role, not against the binary's compile-time
    # default. See orbitFleetResolver.ts for the routing logic that uses
    # the role field to pick the right destination IP.
    $buildDate = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
    if ($StorageOnly) {
        $manifestObj = [ordered]@{
            schema     = 'constellation-firmware/v1'
            version    = $fwVersion
            build_date = $buildDate
            components = [ordered]@{
                storage = [ordered]@{
                    file   = 'nova_lp.release.mcelf.hs_fs'
                    slot   = 0
                    role   = 1   # ORBIT_ROLE_STORAGE
                    ip     = $StorageIp
                    sha256 = $sha
                }
            }
        }
    } elseif ($ControllerOnly) {
        $manifestObj = [ordered]@{
            schema     = 'constellation-firmware/v1'
            version    = $fwVersion
            build_date = $buildDate
            components = [ordered]@{
                controller = [ordered]@{
                    file   = 'nova_lp.release.mcelf.hs_fs'
                    slot   = -1
                    role   = 0   # ORBIT_ROLE_CONTROLLER
                    ip     = $ControllerIp
                    sha256 = $sha
                }
            }
        }
    } elseif ($TritonOnly) {
        $manifestObj = [ordered]@{
            schema     = 'constellation-firmware/v1'
            version    = $fwVersion
            build_date = $buildDate
            components = [ordered]@{
                triton = [ordered]@{
                    file   = 'nova_lp.release.mcelf.hs_fs'
                    slot   = 2
                    role   = 3   # ORBIT_ROLE_TRITON
                    ip     = $TritonIp
                    sha256 = $sha
                }
            }
        }
    } else {
        $manifestObj = [ordered]@{
            schema     = 'constellation-firmware/v1'
            version    = $fwVersion
            build_date = $buildDate
            components = [ordered]@{
                controller = [ordered]@{
                    file   = 'nova_lp.release.mcelf.hs_fs'
                    slot   = -1
                    role   = 0   # ORBIT_ROLE_CONTROLLER
                    ip     = $ControllerIp
                    sha256 = $sha
                }
                storage = [ordered]@{
                    file   = 'nova_lp.release.mcelf.hs_fs'
                    slot   = 0
                    role   = 1   # ORBIT_ROLE_STORAGE
                    ip     = $StorageIp
                    sha256 = $sha
                }
                gdc = [ordered]@{
                    file   = 'nova_lp.release.mcelf.hs_fs'
                    slot   = 1
                    role   = 2   # ORBIT_ROLE_GDC
                    ip     = $GdcIp
                    sha256 = $sha
                }
                triton = [ordered]@{
                    file   = 'nova_lp.release.mcelf.hs_fs'
                    slot   = 2
                    role   = 3   # ORBIT_ROLE_TRITON
                    ip     = $TritonIp
                    sha256 = $sha
                }
            }
        }
    }
    # ─── F2c: optional SBL chooser inclusion (manufacturing bundles) ───
    if ($IncludeSbl) {
        $sblBuild = "$RepoRoot\Nova_Firmware\lp_am2434\sbl_chooser\r5fss0-0_nortos\ti-arm-clang\sbl_chooser.release.hs_fs.tiimage"
        if (-not (Test-Path $sblBuild)) {
            throw "IncludeSbl requested but sbl_chooser.release.hs_fs.tiimage not found at $sblBuild. Build it first: cd $RepoRoot\Nova_Firmware\lp_am2434\sbl_chooser\r5fss0-0_nortos\ti-arm-clang ; gmake -s PROFILE=release all"
        }
        $stagedSbl = Join-Path $tmp 'sbl_chooser.release.hs_fs.tiimage'
        Copy-Item $sblBuild $stagedSbl -Force
        $sblSha  = (Get-FileHash $stagedSbl -Algorithm SHA256).Hash.ToLowerInvariant()
        $sblSize = (Get-Item $stagedSbl).Length
        Write-Host ""
        Write-Host "[stage] $stagedSbl  (F2c SBL chooser)" -ForegroundColor Magenta
        Write-Host "        size   = $sblSize bytes"
        Write-Host "        sha256 = $sblSha"

        # Add to manifest as a non-orbit component. Today's runtime OTA
        # installer skips manifest entries without slot/role/ip fields
        # (treated as a Pi5-style optional component, per Phase 1 manifest
        # contract in firmwareBundle.ts). For manufacturing use, the
        # Commission-LP.ps1 workstation script reads this entry, extracts
        # the binary, and JTAG-flashes via Flash-SblChooser.ps1.
        $manifestObj.components['sbl_chooser'] = [ordered]@{
            file   = 'sbl_chooser.release.hs_fs.tiimage'
            kind   = 'sbl_chooser'   # tag so the installer can recognize + skip
            target = 'ospi_offset_0x000000'
            sha256 = $sblSha
        }
    }

    $manifestPath = Join-Path $tmp 'manifest.json'
    ($manifestObj | ConvertTo-Json -Depth 10) | Set-Content -Path $manifestPath -Encoding ASCII

    Write-Host ""
    Write-Host "[manifest] $manifestPath"
    Get-Content $manifestPath | Out-Host

    # ─── Pack the .cfu zip ──────────────────────────────────────────────
    if (-not (Test-Path $BundlesDir)) {
        New-Item -ItemType Directory -Force -Path $BundlesDir | Out-Null
    }
    $suffix  = if ($StorageOnly) { '-storage-only' }
               elseif ($ControllerOnly) { '-controller-only' }
               elseif ($TritonOnly) { '-triton-only' }
               else { '' }
    $cfuPath = Join-Path $BundlesDir "constellation-$Version$suffix.cfu"
    if (Test-Path $cfuPath) { Remove-Item $cfuPath -Force }

    # Compress-Archive refuses any extension other than .zip, so produce
    # constellation-<ver>.zip in a temp location, then rename to .cfu
    # (adm-zip on the bridge side doesn't care about the extension -- only
    # firmwareBundle.ts's path.extname() gate cares, and that gate accepts
    # `.cfu` per CFU_EXTENSION).
    $tmpZip = Join-Path $BundlesDir "constellation-$Version$suffix.zip"
    if (Test-Path $tmpZip) { Remove-Item $tmpZip -Force }
    Compress-Archive -Path "$tmp\*" -DestinationPath $tmpZip -CompressionLevel Optimal
    Move-Item -LiteralPath $tmpZip -Destination $cfuPath -Force

    Write-Host ""
    Write-Host "=== Done ===" -ForegroundColor Cyan
    Write-Host "Bundle: $cfuPath" -ForegroundColor Green
    Write-Host "Size  : $((Get-Item $cfuPath).Length) bytes"
    if ($IncludeSbl) {
        Write-Host "F2c   : sbl_chooser.release.tiimage included (manufacturing bundle)" -ForegroundColor Magenta
        Write-Host "        Runtime OTA installer will SKIP this component." -ForegroundColor DarkGray
        Write-Host "        Use Commission-LP.ps1 at the workstation to flash SBL chooser." -ForegroundColor DarkGray
    }
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Yellow
    Write-Host "  1. (Manufacturing only -- for IncludeSbl bundles)"
    Write-Host "       cd Nova_Firmware\lp_am2434\sbl_chooser"
    Write-Host "       .\Commission-LP.ps1 -Probe A   # repeat for N, P, T"
    Write-Host "  2. For routine OTA: upload .cfu via the UI → Program → Level 1 → Software Upgrade,"
    Write-Host "     then click Install. The bridge will route per the manifest's role/ip fields."
    Write-Host "  3. Watch the bridge log for the fleet-snapshot line and"
    Write-Host "     per-component routing decisions."

} finally {
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
}
