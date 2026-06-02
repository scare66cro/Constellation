#requires -RunAsAdministrator
<#
.SYNOPSIS
  Build + OSPI-flash a role-specific Nova LP firmware to one of the two
  bench LP-AM2434 boards (Probe A=COM5/STORAGE, Probe B=COM9/GDC).

.DESCRIPTION
  Wraps Flash-PhaseB-OSPI.ps1 with:
    * per-probe ccxml selection via LP_CCXML env var (S24L0417 / S24L0707)
    * per-probe COM port selection (COM5 / COM9 — the App User UART each
      flasher app prints its menu on)
    * automatic build with CONFIG_NOVA_LP_ROLE / CONFIG_NOVA_LP_IP defines
      so the same source tree produces the right image per board

  Per board mapping (lp-dual-board-rig.md / lan-ip-map.md):
    Probe N -> S24L0707 -> COM9 -> CONTROLLER @ 10.47.27.1
    Probe A -> S24L0417 -> COM5 -> STORAGE    @ 10.47.27.2
    Probe B -> S24L0707 -> COM9 -> GDC        @ 10.47.27.3   (shares hw w/ N)
    Probe P -> S24L0727 -> COM7 -> GDC        @ 10.47.27.3   (Pulsar door controller; will be TRITON @ .4 once GDC bringup is done) [added 2026-05-20]
    Probe T -> S24L0957 -> COM4 -> TRITON     @ 10.47.27.4

  NOTE 2026-05-11: S24L0957 was thought to be bricked from 2026-05-09 to
  2026-05-11, after 0.A.105 watchdog-test WRARGs left the chip refusing
  to boot OSPI. Root cause turned out to be the BOOTMODE DIP switch
  config (LP-AM243 PCB 190A wires the bootmode pins NON-STANDARDLY vs
  TI's reference doc: 1+2 ON = OSPI on this board, not 2+6 ON). With the
  switches corrected the chip boots cleanly and re-flashes through DSS.
  S24L0957 is now reassigned to TRITON (Probe T) per the 2026-05-11
  bench plan; S24L0707 remains the dev CONTROLLER (Probe N). See
  /memories/repo/lp-am243-pcb-190a-bootmode.md.

  IMPORTANT: only one DSS session can attach to one XDS110 at a time.
  Multi-probe DSS routing is broken (silently picks the first-enumerated
  XDS110 regardless of .ccxml serial filter), so the script's xdsdfu -e
  precheck refuses to flash if more than one probe is attached. Unplug
  all but the target probe before each flash.

.PARAMETER Probe
  A, B, N, P, or T — selects which board to flash. See header for mapping.

.PARAMETER Role
  STORAGE | GDC | TRITON. Defaults: A=STORAGE, B=GDC. Override to put
  any role on either board (e.g. -Probe B -Role TRITON).

.PARAMETER Ip
  Dotted-quad IP for the LP. Defaults from role:
    STORAGE -> 10.1.2.200
    GDC     -> 10.1.2.201
    TRITON  -> 10.1.2.202

.PARAMETER SkipBuild
  Reuse the existing nova_lp.release.mcelf.hs_fs (the build was just
  done by hand or by a prior invocation).

.PARAMETER WipeDevCfg
  Erase the OSPI device-config banks at 0x600000/0x610000 BEFORE
  flashing the firmware image. Required when reflashing a board to a
  different role/IP — otherwise the previously-saved bank silently
  overrides the freshly-built compile-time defaults on next boot.
  See /memories/repo/lp-role-change-reflash.md.

.PARAMETER FlashPhyTuning
  ONE-SHOT commissioning step: write the 128-byte OSPI PHY attack-vector
  to phyTuningOffset (0x2000000 on the S25HL512T) and exit. Required on
  a virgin chip or after an OSPI_NV_RECOVER run — without it, the
  prebuilt SBL wedges silently in Flash_norOspiPhyTune at boot (no UART
  output, ROM still firewalls R5, JTAG attach returns -1065 in OSPI boot
  mode). Working chips already have this vector from the original TI
  Uniflash commissioning, so this step is needed ONCE per recovered chip.
  After it succeeds, re-run Flash-LP.ps1 without -FlashPhyTuning for the
  normal app flash. See memories/repo/lp-am2434-ospi-boot-missing-phy-
  tuning.md and docs/lp-am2434-ospi-boot-commissioning.md.

.EXAMPLE
  .\Flash-LP.ps1 -Probe A
  # Build STORAGE @ .200, flash to probe A.

.EXAMPLE
  .\Flash-LP.ps1 -Probe B -Role TRITON -Ip 10.1.2.205
  # Build TRITON @ .205, flash to probe B.

.EXAMPLE
  .\Flash-LP.ps1 -Probe N -FlashPhyTuning
  # Write PHY attack-vector to OSPI 0x2000000 on the recovered chip.
  # Exits after writer reports SUCCESS. Run Flash-LP.ps1 again (without
  # -FlashPhyTuning) to flash the actual app.
#>
param(
    [Parameter(Mandatory)][ValidateSet('A', 'B', 'N', 'P', 'T')] [string]$Probe,
    [ValidateSet('CONTROLLER', 'STORAGE', 'GDC', 'TRITON')] [string]$Role = '',
    [string]$Ip = '',
    [switch]$SkipBuild,
    [switch]$WipeDevCfg,
    [switch]$Force,
    # 2026-05-07: skip the second uniflash invocation that re-writes
    # watchdog.release.mcelf.hs_fs at 0x180000. The bench dsslite session
    # has been hanging consistently at "Step 2: Load auto-flasher" of the
    # watchdog flash (post-main-flash warm-reset leaves the JTAG/probe in
    # a state DSS can't re-attach to immediately). When iterating on
    # main-core code only — which is the case for everything in
    # 0.A.78→present — the watchdog mcelf is unchanged and re-flashing
    # is just churn. Pass `-SkipWatchdog` to end the script cleanly after
    # the main flash + Step 7 warm-reset, so the LP boots into the new
    # firmware with no manual power-cycle.
    [switch]$SkipWatchdog,
    # 2026-05-15: write the OSPI PHY tuning attack-vector at 0x2000000.
    # One-shot commissioning step for recovered/virgin chips — see param
    # help above. Mutually exclusive with the normal flash flow (exits
    # after the writer reports success).
    [switch]$FlashPhyTuning,
    # 2026-06-02: bench parity with OTA. Flash-LP writes the new image to
    # Bank A bytes (OSPI 0x080000) but does NOT touch the metadata block
    # at 0x300000 — so on next boot the F2c SBL chooser still picks
    # whichever bank has the higher `sequence`. If a previous OTA cycle
    # left Bank B at sequence=N+1, Bank B's OLD firmware wins and the
    # board boots the OLD code (we tripped over this on 0.A.222 — fleet
    # was still reporting 0.A.216 after Flash-LP "succeeded").
    #
    # Production OTA doesn't have this problem because the install path
    # calls `NovaFwUpdate_OrbitFinalize` which writes a new Bank header
    # with `sequence = max(A,B) + 1` before the warm reset. The bench
    # parallel is `Write-SeedMetaBlock.ps1`, which writes Bank A with
    # sequence=1 and wipes Bank B's header (chooser picks A as the only
    # candidate). This switch chains that call automatically so a single
    # `Flash-LP.ps1` invocation reliably ends with the LP running the
    # firmware we just wrote.
    #
    # Pass `-NoSeed` to skip the seed step (e.g. when iterating between
    # bench flash + manual rollback test).
    [switch]$NoSeed
)

$ErrorActionPreference = 'Stop'

# Probe -> ccxml + serial + COM port (App User UART of that XDS110).
$probeMap = @{
    'A' = @{ Ccxml = 'F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_A.ccxml';    Serial = 'S24L0417'; Com = 'COM5'; DefaultRole = 'STORAGE'    }
    'B' = @{ Ccxml = 'F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_B.ccxml';    Serial = 'S24L0707'; Com = 'COM9'; DefaultRole = 'GDC'        }
    # Probe N + Probe B physically share S24L0707 (the dev CONTROLLER).
    # Use -Probe N for CONTROLLER role builds, -Probe B for GDC role builds.
    # The script rebuilds with the right CONFIG_NOVA_LP_ROLE each time.
    'N' = @{ Ccxml = 'F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_B.ccxml';    Serial = 'S24L0707'; Com = 'COM9'; DefaultRole = 'CONTROLLER' }
    # 2026-05-20: S24L0727 ("Pulsar") added as Probe P — door controller LP.
    # Default role is GDC for initial bringup; will be re-roled to TRITON
    # (10.47.27.4) once GDC firmware is validated on this hardware. Use
    # -Role TRITON -WipeDevCfg to swap. App UART = COM7 (MI_00), Aux = COM8.
    'P' = @{ Ccxml = 'F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_P.ccxml';    Serial = 'S24L0727'; Com = 'COM7'; DefaultRole = 'GDC'        }
    # 2026-05-11: S24L0957 recovered from suspected-brick (it was just a
    # BOOTMODE DIP misconfig) and reassigned to TRITON.
    'T' = @{ Ccxml = 'F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_T.ccxml';    Serial = 'S24L0957'; Com = 'COM4'; DefaultRole = 'TRITON'     }
}
# Airgapped 10.47.27.0/24 — see docs/Network-Migration-10.47.27.x.md.
$roleMap = @{
    'CONTROLLER' = @{ Id = 0; DefaultIp = '10.47.27.1' }
    'STORAGE'    = @{ Id = 1; DefaultIp = '10.47.27.2' }
    'GDC'        = @{ Id = 2; DefaultIp = '10.47.27.3' }
    'TRITON'     = @{ Id = 3; DefaultIp = '10.47.27.4' }
}

# === Probe-state helpers ===============================================
# After a Flash-LP run the script disables non-target probes and
# re-enables them in its finally{}. But if the user kills/aborts a
# previous run (or hits the COM-busy Disable failure path), some probes
# can stay administratively-disabled across sessions. The next flash
# fails non-obviously: "Requested probe X is NOT enumerated. Plug it in
# and retry." (when in fact it IS plugged in, just PnP-disabled).
#
# Enable-AllKnownProbes is the cure: at the top of every Flash-LP run,
# walk every distinct serial in $probeMap and call Set-Probe -Action
# Enable on it. Idempotent — already-enabled probes log "[keep on]"
# and no-op. Cheap (~5 ms per probe via Get-PnpDevice).
#
# Note: $probeMap has 'B' and 'N' sharing physical serial S24L0707, so
# we dedupe by serial here.
function Enable-AllKnownProbes {
    param([hashtable]$ProbeMap, [string]$SetProbeScript)
    $seenSerials = @{}
    $letters = @()
    foreach ($k in @('A','B','N','P','T')) {
        if (-not $ProbeMap.ContainsKey($k)) { continue }
        $s = $ProbeMap[$k].Serial
        if ($seenSerials.ContainsKey($s)) { continue }
        $seenSerials[$s] = $true
        $letters += $k
    }
    Write-Host "[probe-prep] ensuring all known probes are PnP-enabled: $($letters -join ', ')" -ForegroundColor DarkGray
    foreach ($l in $letters) {
        try {
            & $SetProbeScript -Probe $l -Action Enable -ErrorAction Stop
        } catch {
            Write-Warning "[probe-prep] Enable -Probe $l failed: $_  (continuing — flash-side checks will catch real issues)"
        }
    }
    Start-Sleep -Milliseconds 500   # USB stack settle
}

# === Belt-and-suspenders probe isolation gate =========================
# Defense against the well-documented DSS "wrong-probe trap": the .ccxml
# `<property id="The serial number is">` filter is silently ignored when
# DSS attaches, so DSS routes to whichever XDS110 enumerated first.
# Past flashes have ended up on the wrong board because of this.
#
# This function is called RIGHT BEFORE every DSS invocation in the
# script. Throws on any deviation from "exactly one probe enumerated AND
# that probe is the requested target AND every other known probe is
# administratively disabled (CM_PROB_DISABLED)".
#
# A 2-second sleep is included so the caller doesn't have to remember to
# wait for USB enumeration to settle after Set-Probe.
function Assert-OnlyTargetProbeReachable {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ExpectedSerial,
        [Parameter(Mandatory)][string]$Context
    )
    $xdsdfu = 'C:\ti\ccs2050\ccs\ccs_base\common\uscif\xds110\xdsdfu.exe'
    if (-not (Test-Path $xdsdfu)) {
        Write-Warning "[probe-check:$Context] xdsdfu.exe not found — SKIPPING isolation gate (risky)."
        return
    }
    # @(...) array-subexpression: when xdsdfu enumerates exactly one probe,
    # the Select-String | ForEach pipeline returns a SCALAR string. Without
    # @(), $serials[0] would index character 0 of that string ('S' instead
    # of 'S24L0417') and the next equality check would always fail. This
    # path only matters after Set-Probe -Action Solo has disabled the other
    # probes (the multi-probe path produces an array naturally).
    $serials = @(& $xdsdfu -e 2>&1 | Select-String 'Serial Num:\s*(\S+)' |
                 ForEach-Object { $_.Matches[0].Groups[1].Value })
    if ($serials.Count -ne 1) {
        throw "[probe-check:$Context] FATAL: expected exactly ONE probe ($ExpectedSerial), saw $($serials.Count) ($($serials -join ', ')). DSS would silently route to wrong board. Run Set-Probe -Probe <X> -Action Solo or unplug the others. Abort."
    }
    if ($serials[0] -ne $ExpectedSerial) {
        throw "[probe-check:$Context] FATAL: only probe enumerated is '$($serials[0])', not target '$ExpectedSerial'. DSS would flash the wrong chip. Abort."
    }
    # PnP belt-check: every OTHER known probe must be CM_PROB_DISABLED or
    # absent. Catches races where xdsdfu doesn't see a probe but Windows
    # still has its driver bound and DSS could still attach to it.
    $knownSerials = @('S24L0707','S24L0417','S24L0727','S24L0957')
    foreach ($s in $knownSerials) {
        if ($s -eq $ExpectedSerial) { continue }
        $pnp = Get-PnpDevice -InstanceId "USB\VID_0451&PID_BEF3\$s" -ErrorAction SilentlyContinue
        if ($pnp -and $pnp.Status -eq 'OK') {
            throw "[probe-check:$Context] FATAL: probe $s shows Status=OK in PnP but is invisible to xdsdfu — race risk (DSS could still grab it). Run Set-Probe -Probe <letter-for-$s> -Action Disable. Abort."
        }
    }
    Write-Host "[probe-check:$Context] OK — $ExpectedSerial is the sole reachable XDS110" -ForegroundColor Green
}

$probeCfg = $probeMap[$Probe]
if (-not $Role) { $Role = $probeCfg.DefaultRole }
$roleCfg = $roleMap[$Role]
if (-not $Ip) { $Ip = $roleCfg.DefaultIp }

# IP -> 32-bit MSB-first hex (for CONFIG_NOVA_LP_IP).
$octets = $Ip.Split('.')
if ($octets.Count -ne 4) { throw "Bad IP: $Ip" }
$ipHex = '0x{0:X2}{1:X2}{2:X2}{3:X2}' -f [int]$octets[0], [int]$octets[1], [int]$octets[2], [int]$octets[3]

# --- Resolve alpha version from docs/firmware-version-current.md ------
# The first non-comment line in the doc that matches `0.A.<n>` is the
# alpha number we ship. Falls back to 1 if the doc is missing.
$verDoc = 'F:\Constellation\docs\firmware-version-current.md'
$alphaN = 1
if (Test-Path $verDoc) {
    $m = (Get-Content $verDoc -Raw) | Select-String -Pattern '0\.A\.(\d+)' -AllMatches
    if ($m.Matches.Count -gt 0) { $alphaN = [int]$m.Matches[0].Groups[1].Value }
}
# git short SHA + dirty flag
$gitSha = (& git -C 'F:\Constellation' rev-parse --short=8 HEAD 2>$null)
if ([string]::IsNullOrWhiteSpace($gitSha)) { $gitSha = 'nogit' }
$dirty = ''
$status = (& git -C 'F:\Constellation' status --porcelain 2>$null)
if ($status) { $dirty = '-dirty' }
$fwVersion = "0.A.$alphaN+$gitSha$dirty"
$buildIso  = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ssK')

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Probe $Probe   ->  $($probeCfg.Serial)   ($($probeCfg.Com))" -ForegroundColor Cyan
Write-Host "  Role        ->  $Role  (id=$($roleCfg.Id))" -ForegroundColor Cyan
Write-Host "  IP          ->  $Ip   ($ipHex)" -ForegroundColor Cyan
Write-Host "  Version     ->  $fwVersion" -ForegroundColor Cyan
Write-Host "  Built       ->  $buildIso" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
if ($dirty -and -not $Force -and -not $FlashPhyTuning) {
    # -FlashPhyTuning writes a fixed 128-byte commissioning pattern from
    # the linked SDK lib; there's no firmware version stamp to taint.
    Write-Warning "Working tree is DIRTY. Pass -Force to flash anyway, or commit first."
    throw 'refusing to flash dirty tree (use -Force)'
}

# --- Probe-safety precheck ---------------------------------------------
# DSS .ccxml serial filtering silently falls through to the only
# attachable XDS110 when the named one is locked / busy / not present.
# Use Set-Probe.ps1 to disable non-target probes before flashing, then
# re-enable all probes afterward.
$xdsdfu = 'C:\ti\ccs2050\ccs\ccs_base\common\uscif\xds110\xdsdfu.exe'
$setProbe = Join-Path $PSScriptRoot 'Set-Probe.ps1'

# Wrap from here so the finally{} below ALWAYS re-enables probes, even
# if the probe-safety precheck itself aborts (e.g. Disable-PnpDevice
# failed because a COM port was open). Previously the try{} started
# AFTER the precheck, so a precheck abort left probes in a half-disabled
# state and the next flash session couldn't find them.
try {

# Ensure every physically-attached probe is PnP-enabled BEFORE the
# precheck enumerates. Cures the "previous Flash-LP run aborted before
# its finally{} restored siblings" sticky state.
Enable-AllKnownProbes -ProbeMap $probeMap -SetProbeScript $setProbe

if (Test-Path $xdsdfu) {
    $serials = (& $xdsdfu -e 2>&1 | Select-String 'Serial Num:\s*(\S+)' |
                ForEach-Object { $_.Matches[0].Groups[1].Value })
    Write-Host "[probe-check] enumerated XDS110 serials: $($serials -join ', ')" -ForegroundColor Yellow
    if (-not ($serials -contains $probeCfg.Serial)) {
        throw "Requested probe $($probeCfg.Serial) is NOT enumerated. Plug it in and retry."
    }
    if ($serials.Count -gt 1) {
        Write-Host "[probe-check] Multiple probes — running Set-Probe -Probe $Probe -Action Solo" -ForegroundColor Yellow
        & $setProbe -Probe $Probe -Action Solo
        Start-Sleep -Seconds 2  # let Windows USB stack settle after disables
    }
    # Belt-and-suspenders gate. Throws if anything is off (multiple probes
    # somehow still up, wrong serial, race-state PnP), so the rest of the
    # script can trust that DSS will hit the requested chip.
    Assert-OnlyTargetProbeReachable -ExpectedSerial $probeCfg.Serial -Context "pre-build"
} else {
    Write-Warning "[probe-check] xdsdfu.exe not found at $xdsdfu — skipping safety check."
}

# NOTE: the try{} that wraps the rest of this script (and the matching
# finally{} that re-enables all probes) now lives BEFORE the probe-
# safety precheck, so an abort there still triggers the cleanup.
# Don't re-open another try here.

# --- One-shot: PHY tuning attack-vector writer -------------------------
# Recovered (and virgin) S25HL512T chips lack the 128-byte attack vector
# at phyTuningOffset (0x2000000). The prebuilt SBL silently wedges at
# boot when this vector is missing — Flash_norOspiPhyTune can't enable
# PHY mode for the high-speed app read at 0x80000, and the SBL never
# prints its banner. Write the vector once via run_phy_tuning_writer.js,
# then exit — the user then re-runs without -FlashPhyTuning for the
# normal app flash.
#
# The writer is a build-variant of the flasher_uart auto-flasher
# (-DOSPI_PHY_TUNING_WRITER on DEFINES_common). We rebuild it, run it,
# then rebuild the default auto-flasher so subsequent normal flashes
# keep working. See memories/repo/lp-am2434-ospi-boot-missing-phy-
# tuning.md.
if ($FlashPhyTuning) {
    Write-Host "============================================================" -ForegroundColor Magenta
    Write-Host "  PHY TUNING WRITER (one-shot)  ->  probe $Probe ($($probeCfg.Serial))" -ForegroundColor Magenta
    Write-Host "  Writing 128-byte attack-vector to OSPI 0x02000000" -ForegroundColor Magenta
    Write-Host "============================================================" -ForegroundColor Magenta

    $gmake = 'C:\ti\ccs2050\ccs\utils\bin\gmake.exe'
    if (-not (Test-Path $gmake)) { throw "gmake not found at $gmake" }
    $env:MCU_PLUS_SDK_PATH = 'C:/ti/mcu_plus_sdk_am243x_12_00_00_26'
    $env:CCS_PATH          = 'C:/ti/ccs2050/ccs'
    $env:SYSCFG_PATH       = 'C:/ti/sysconfig_1.27.0'

    Push-Location 'F:\Constellation\Nova_Firmware\lp_am2434\flasher_uart\ti-arm-clang'
    try {
        # 1. Rebuild flasher with -DOSPI_PHY_TUNING_WRITER. The main.obj
        #    and final .out must be forced — gmake otherwise treats the
        #    cached objects as up-to-date even though the DEFINES_common
        #    changed.
        Write-Host "[phyw] rebuild flasher with -DOSPI_PHY_TUNING_WRITER" -ForegroundColor Yellow
        Remove-Item -Force 'obj\release\main.obj','sbl_jtag_uniflash.release.out' -ErrorAction SilentlyContinue
        $oldEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            & $gmake PROFILE=release DEFINES_common='-DSOC_AM243X -DOS_NORTOS -DOSPI_PHY_TUNING_WRITER' all 2>&1 | Out-Host
        } finally {
            $ErrorActionPreference = $oldEap
        }
        $writerOut = 'F:\Constellation\Nova_Firmware\lp_am2434\flasher_uart\ti-arm-clang\sbl_jtag_uniflash.release.out'
        if (-not (Test-Path $writerOut)) { throw "[phyw] writer build failed — sbl_jtag_uniflash.release.out missing" }
        $writerAge = ((Get-Date) - (Get-Item $writerOut).LastWriteTime).TotalSeconds
        if ($writerAge -gt 60) { Write-Warning "[phyw] writer .out is $([int]$writerAge)s old — gmake may have no-op'd." }

        # 2. Run the DSS driver (it loads writer_elf, polls
        #    g_ospi_phy_writer.magic_done, exits 0 on success).
        Write-Host "[phyw] running DSS driver (run_phy_tuning_writer.js)" -ForegroundColor Yellow
        $env:LP_CCXML = $probeCfg.Ccxml
        $dssBat = 'C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat'
        $jsPath = 'F:\Constellation\Nova_Firmware\lp_am2434\ospi_flash\run_phy_tuning_writer.js'
        if (-not (Test-Path $dssBat)) { throw "dss.bat not found at $dssBat" }
        if (-not (Test-Path $jsPath)) { throw "run_phy_tuning_writer.js not found at $jsPath" }
        $phywLog = "F:\Constellation\logs\lp_phy_tuning_writer_probe$Probe.log"
        "" | Set-Content -Path $phywLog
        Assert-OnlyTargetProbeReachable -ExpectedSerial $probeCfg.Serial -Context "pre-phyw-flash"
        & $dssBat $jsPath 2>&1 | Tee-Object -FilePath $phywLog -Append
        $phywExit = $LASTEXITCODE
    } finally {
        # 3. ALWAYS rebuild the default auto-flasher, even if the writer
        #    failed — otherwise the normal Flash-LP.ps1 flow is left with
        #    the writer binary as its "auto-flasher" and silently breaks.
        Write-Host "[phyw] restoring default auto-flasher (rebuild without OSPI_PHY_TUNING_WRITER)" -ForegroundColor Yellow
        Remove-Item -Force 'obj\release\main.obj','sbl_jtag_uniflash.release.out' -ErrorAction SilentlyContinue
        $oldEap2 = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            & $gmake PROFILE=release all 2>&1 | Out-Host
        } finally {
            $ErrorActionPreference = $oldEap2
        }
        Pop-Location
    }

    if ($phywExit -ne 0) {
        throw "PHY tuning writer FAILED (exit $phywExit) — see $phywLog"
    }
    Write-Host ""
    Write-Host "DONE.  PHY attack-vector written to OSPI 0x02000000 on $($probeCfg.Serial)." -ForegroundColor Green
    Write-Host "Next step: re-run Flash-LP.ps1 WITHOUT -FlashPhyTuning to flash the app." -ForegroundColor Green
    Write-Host "(The chip should now boot from OSPI after a normal SBL+app flash.)" -ForegroundColor DarkGray
    # Signal the post-try block to exit 0 after the finally has re-enabled probes.
    $script:phyTuningDone = $true
}

if (-not $FlashPhyTuning) {
# ----- the rest of this try{} is the normal build+flash path; skipped when
#       -FlashPhyTuning was the requested operation -------------------------

# --- Build --------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "[build] gmake PROFILE=release CONFIG_NOVA_LP_ROLE=$($roleCfg.Id) CONFIG_NOVA_LP_IP=$ipHex" -ForegroundColor Yellow
    $gmake = 'C:\ti\ccs2050\ccs\utils\bin\gmake.exe'
    if (-not (Test-Path $gmake)) { throw "gmake not found at $gmake" }
    $env:CCS_PATH    = 'C:/ti/ccs2050/ccs'
    $env:SYSCFG_PATH = 'C:/ti/sysconfig_1.27.0'
    Push-Location 'F:\Constellation\Nova_Firmware\lp_am2434\ti-arm-clang'
    try {
        # gmake writes SysConfig + cygwin-rm warnings to stderr; with
        # $ErrorActionPreference = 'Stop' that aborts the script. Run
        # native commands with EAP=Continue and check $LASTEXITCODE.
        $oldEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            # Delete the role-consuming .obj outright — gmake -B
            # mysteriously says "Nothing to be done" when the target is
            # named on the command line, so we force the rebuild by
            # removing the artifact.
            $roleObj = 'obj\release\lp_device_config.obj'
            if (Test-Path $roleObj) { Remove-Item $roleObj -Force }
            # main.obj bakes in LP_FW_VERSION via lp_version.h. Rather
            # than fight nested PowerShell/cmd/gmake shells over how to
            # quote a string CFLAG (e.g. `-DLP_FW_VERSION="..."` gets
            # stripped down to `-DLP_FW_VERSION=0.A.6+test` which the
            # preprocessor parses as a floating-point literal — this
            # has bitten us once already), drop a tiny generated header
            # next to lp_version.h and let it `#include` it. Force a
            # main.obj rebuild so the new header is honoured.
            $injected = 'F:\Constellation\Nova_Firmware\lp_am2434\lp_version_injected.h'
            $injectedContent = @"
/* AUTO-GENERATED by Flash-LP.ps1 — do not edit, do not commit. */
#ifndef LP_FW_VERSION
#define LP_FW_VERSION "$fwVersion"
#endif
"@
            Set-Content -Path $injected -Value $injectedContent -Encoding ASCII
            $mainObj = 'obj\release\main.obj'
            if (Test-Path $mainObj) { Remove-Item $mainObj -Force }
            $otaObj = 'obj\release\lp_ota_task.obj'
            if (Test-Path $otaObj) { Remove-Item $otaObj -Force }
            & $gmake PROFILE=release "CONFIG_NOVA_LP_ROLE=$($roleCfg.Id)" "CONFIG_NOVA_LP_IP=$ipHex" all 2>&1 | Out-Host
            # gmake exits 1 on the cygwin rm cleanup quirk even after a
            # clean build; verify by checking the .hs_fs artifact below.
        } finally {
            $ErrorActionPreference = $oldEap
        }
    } finally {
        Pop-Location
    }
}

# --- Optional: wipe device-config banks at 0x600000 (256 KB block covers
# both 64 KB ping-pong banks). Required when changing role/IP on a board
# that was previously flashed with a different role — the saved bank
# would otherwise silently override the new compile-time defaults.
if ($WipeDevCfg) {
    Write-Host "[wipe] erasing OSPI device-config banks at 0x600000 (256 KB block)" -ForegroundColor Yellow
    $blank = 'F:\Constellation\logs\erase_64k.bin'
    if (-not (Test-Path $blank)) {
        $b = New-Object byte[] 65536
        for ($i = 0; $i -lt 65536; $i++) { $b[$i] = 0xFF }
        [System.IO.File]::WriteAllBytes($blank, $b)
    }
    $env:LP_CCXML        = $probeCfg.Ccxml
    $env:UNIFLASH_FILE   = ($blank -replace '\\','/')
    $env:UNIFLASH_OFFSET = '0x600000'
    $dssBat  = 'C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat'
    $jsPath  = 'F:\Constellation\Nova_Firmware\lp_am2434\ospi_flash\uniflash_run.js'
    Assert-OnlyTargetProbeReachable -ExpectedSerial $probeCfg.Serial -Context "pre-wipe-devcfg"
    & $dssBat $jsPath 2>&1 | Tee-Object -FilePath "F:\Constellation\logs\lp_wipe_devcfg_probe$Probe.log"
    if ($LASTEXITCODE -ne 0) { throw "WipeDevCfg failed (exit $LASTEXITCODE) — see logs\lp_wipe_devcfg_probe$Probe.log" }
    Write-Host "[wipe] device-config banks erased" -ForegroundColor Green
}

$image = 'F:\Constellation\Nova_Firmware\lp_am2434\ti-arm-clang\nova_lp.release.mcelf.hs_fs'
if (-not (Test-Path $image)) { throw "Image not produced: $image" }
$ageSec = ((Get-Date) - (Get-Item $image).LastWriteTime).TotalSeconds

# === Hardened: hard-fail on stale build artifact =====================
# Was a soft warning. The "Image is Ns old" warning turned into silent
# stale flashes when gmake errors got swallowed by Out-Host (see
# memories/repo/flash-lp-out-host-trap.md). The case that bit us
# 2026-05-20: a compile error in lwip_smoke.c → gmake errored → image
# stayed at the previous role's binary → that stale binary got flashed
# onto the chip, which then booted up claiming the wrong role/IP.
#
# Now: if the user asked for a build (didn't pass -SkipBuild), the
# artifact MUST be fresh (< 60 s old). If older, gmake silently no-op'd
# — scroll up to find the compile/link error. Pass -SkipBuild to flash
# an old artifact intentionally (the build stamp guard below validates
# that the stale artifact's role/IP matches the request).
if (-not $SkipBuild -and $ageSec -gt 60) {
    throw "[build] FATAL: $image is $([int]$ageSec)s old, but a build was requested. gmake silently no-op'd — scroll up to find the compile/link error. Refusing to flash a stale binary. (Pass -SkipBuild to flash this artifact intentionally; the build stamp guard will confirm role/IP match.)"
}

# --- Image-stamp guard (prevents the -SkipBuild role/IP mix-up trap) ---
# `nova_lp.release.mcelf.hs_fs` bakes in the role/IP via -DCONFIG_NOVA_LP_ROLE
# / -DCONFIG_NOVA_LP_IP from lp_device_config.obj. Whoever ran the last
# build determines which board the binary will work on. If you flash a
# CONTROLLER-baked binary onto STORAGE (probe A), STORAGE will boot
# claiming role=CONTROLLER + IP=10.47.27.1, fight CONTROLLER for the
# address, and never come up at .2.
#
# We write a stamp next to the image at the end of every build that
# records the role+IP it was compiled for. On every flash we check
# the stamp — if it doesn't match the requested probe, we either rebuild
# (default) or refuse if -SkipBuild was used.
$stampPath = 'F:\Constellation\Nova_Firmware\lp_am2434\ti-arm-clang\.last-build-role.json'
# Compare structurally — PowerShell hashtable→JSON key order is not stable
# across versions and a string compare would false-mismatch identical content.
$wantRole = [int]$roleCfg.Id
$wantIp   = $ipHex
$wantStamp = "{`"role`":$wantRole,`"ip`":`"$wantIp`"}"
if ($SkipBuild) {
    if (-not (Test-Path $stampPath)) {
        throw "[stamp] -SkipBuild requested but no build stamp found at $stampPath. Run once without -SkipBuild to bake the correct role/IP."
    }
    $haveObj = $null
    try { $haveObj = Get-Content $stampPath -Raw | ConvertFrom-Json } catch { }
    if (-not $haveObj -or $haveObj.role -ne $wantRole -or $haveObj.ip -ne $wantIp) {
        $haveStr = (Get-Content $stampPath -Raw).Trim()
        throw "[stamp] Image was last built for $haveStr but you asked to flash role=$wantRole ip=$wantIp on probe $Probe. Re-run WITHOUT -SkipBuild to rebuild for this role/IP, or this probe will end up with the wrong role/IP and silently brick the LAN."
    }
    Write-Host "[stamp] image matches requested role/IP (role=$wantRole ip=$wantIp) — safe to flash with -SkipBuild" -ForegroundColor Green
} else {
    # Build path will overwrite the stamp regardless of what was on disk.
    # Use a fixed key order so the file is also greppable/diffable.
    Set-Content -Path $stampPath -Value $wantStamp -Encoding ASCII
    Write-Host "[stamp] wrote build stamp: $wantStamp" -ForegroundColor DarkGray
}

Write-Host "[image] $image  ($((Get-Item $image).Length) B)" -ForegroundColor Yellow

# --- XIP manifest -------------------------------------------------------
# Since v0.A.24 the build splits .text + .rodata into a separate XIP
# image (nova_lp.release.mcelf_xip) that must be flashed at the
# OSPI offsets encoded in its PT_LOAD p_paddr fields, and the much
# smaller hs_fs is flashed at 0x80000. Build-XipManifest.ps1 walks the
# XIP ELF and writes a manifest text file consumed by uniflash_run.js.
#
# For backward compatibility (and during development), if the XIP file
# is empty (only stub PHs, no real XIP segments), fall back to the
# legacy single-file flash path. See lp-am2434-xip-bringup.md.
$xipElf = 'F:\Constellation\Nova_Firmware\lp_am2434\ti-arm-clang\nova_lp.release.mcelf_xip'
$useManifest = $false
if (Test-Path $xipElf) {
    $xipBytes = (Get-Item $xipElf).Length
    # Empty XIP container is ~176 bytes (just the ELF + note segment +
    # zero-length PT_LOAD). A real XIP build is hundreds of KB.
    if ($xipBytes -gt 4096) {
        Write-Host "[xip] mcelf_xip = $xipBytes bytes - building flash manifest" -ForegroundColor Yellow
        $xipOutDir = 'F:\Constellation\Nova_Firmware\lp_am2434\ti-arm-clang\xip_segments'
        # Wipe stale segment .bins so a shrunk build doesn't leave leftovers
        if (Test-Path $xipOutDir) {
            Remove-Item -Path (Join-Path $xipOutDir 'xip_seg*.bin') -Force -ErrorAction SilentlyContinue
        }
        $manifestPath = & "$PSScriptRoot\ospi_flash\Build-XipManifest.ps1" `
            -XipElf $xipElf -HsFs $image -OutDir $xipOutDir
        $useManifest = $true
    } else {
        Write-Host "[xip] mcelf_xip is empty stub ($xipBytes B) - using legacy single-file flash" -ForegroundColor DarkGray
    }
} else {
    Write-Host "[xip] mcelf_xip not found - using legacy single-file flash" -ForegroundColor DarkGray
}

# --- Flash --------------------------------------------------------------
Write-Host "[flash] LP_CCXML=$($probeCfg.Ccxml)  ComPort=$($probeCfg.Com)" -ForegroundColor Yellow
$env:LP_CCXML = $probeCfg.Ccxml
if ($useManifest) {
    $env:UNIFLASH_MANIFEST = $manifestPath
    # Clear single-file vars so uniflash_run.js takes the manifest path.
    Remove-Item Env:UNIFLASH_FILE -ErrorAction SilentlyContinue
    Remove-Item Env:UNIFLASH_OFFSET -ErrorAction SilentlyContinue
} else {
    Remove-Item Env:UNIFLASH_MANIFEST -ErrorAction SilentlyContinue
    $env:UNIFLASH_FILE   = ($image -replace '\\','/')
    $env:UNIFLASH_OFFSET = '0x80000'
}
$logPath = "F:\Constellation\logs\lp_ospi_flash_probe$Probe.log"
$dssBat  = 'C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat'
$jsPath  = 'F:\Constellation\Nova_Firmware\lp_am2434\ospi_flash\uniflash_run.js'
if (-not (Test-Path $dssBat)) { throw "dss.bat not found at $dssBat" }
if (-not (Test-Path $jsPath)) { throw "uniflash_run.js not found at $jsPath" }
"" | Set-Content -Path $logPath
Assert-OnlyTargetProbeReachable -ExpectedSerial $probeCfg.Serial -Context "pre-app-flash"
& $dssBat $jsPath 2>&1 | Tee-Object -FilePath $logPath -Append

$flashExitCode = $LASTEXITCODE

# --- Watchdog (R5FSS1_0) flash at 0x180000 -----------------------------
# sbl_ospi_multi_partition loads each cluster's image from its own
# OSPI partition. R5FSS1_0 lives at 0x180000. The watchdog mcelf is
# always built alongside the main one (same makefile target list); if
# it isn't present, fall back gracefully — main image still boots
# fine without it (SBL just leaves R5FSS1 in reset).
$wdImage = 'F:\Constellation\Nova_Firmware\lp_am2434\ti-arm-clang\watchdog.release.mcelf.hs_fs'
if ($SkipWatchdog -and $flashExitCode -eq 0) {
    Write-Host "[wd-flash] -SkipWatchdog set — leaving watchdog at 0x180000 intact" -ForegroundColor DarkGray
} elseif ($flashExitCode -eq 0 -and (Test-Path $wdImage)) {
    Write-Host ""
    # 2026-05-27: wait + java cleanup BEFORE the second DSS invocation.
    # The previous uniflash_run.js fired Step 7 MAGIC_REBOOT which warm-
    # resets the SoC; the SoC then re-runs ROM+SBL+boots the just-flashed
    # main image in ~5 s. If we launch the next dss.bat (watchdog flash)
    # immediately, the second DSS attaches JTAG to a SoC that is mid-boot,
    # which wedges Step 2 ("Load auto-flasher") consistently. Documented
    # bench symptom 2026-05-07 + reproduced 2026-05-27. Mitigation:
    # (1) kill any leftover java.exe from the first DSS session that
    # didn't exit cleanly, (2) wait 10 s for the warm-reset to complete
    # and the SBL to load the new main image (Step 7 banner says ~5 s,
    # giving 2× margin), (3) only THEN attempt the second JTAG attach.
    Write-Host "[wd-flash] cleaning DSS Java + waiting 10 s for SoC warm-reset to complete..." `
                -ForegroundColor DarkYellow
    Get-Process java -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 10
    Write-Host "[wd-flash] $wdImage  ($((Get-Item $wdImage).Length) B) -> 0x180000" -ForegroundColor Yellow
    Remove-Item Env:UNIFLASH_MANIFEST -ErrorAction SilentlyContinue
    $env:UNIFLASH_FILE   = ($wdImage -replace '\\','/')
    $env:UNIFLASH_OFFSET = '0x180000'
    $wdLogPath = "F:\Constellation\logs\lp_ospi_flash_probe${Probe}_watchdog.log"
    "" | Set-Content -Path $wdLogPath
    Assert-OnlyTargetProbeReachable -ExpectedSerial $probeCfg.Serial -Context "pre-wd-flash"
    & $dssBat $jsPath 2>&1 | Tee-Object -FilePath $wdLogPath -Append
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "[wd-flash] FAILED (exit $LASTEXITCODE) — see $wdLogPath. Main image is already flashed; watchdog core will not run until re-flashed."
        $flashExitCode = $LASTEXITCODE
    } else {
        Write-Host "[wd-flash] watchdog (R5FSS1_0) installed at 0x180000" -ForegroundColor Green
    }
} elseif ($flashExitCode -eq 0) {
    Write-Host "[wd-flash] watchdog image not found at $wdImage — skipping (main core only)" -ForegroundColor DarkGray
}

}  # end of `if (-not $FlashPhyTuning)` — normal build+flash path

} finally {
    # Always re-enable every probe, regardless of whether THIS run was
    # the one that disabled them. Idempotent: Set-Probe -Action Enable
    # on an already-enabled probe is a no-op. Running unconditionally
    # protects against the case where a prior session left things in a
    # half-disabled state.
    #
    # Probe letters covered: 'A' (S24L0417 storage), 'B' / 'N' (both
    # alias S24L0707 controller/GDC — Set-Probe is idempotent), 'P'
    # (S24L0727 GDC, added 2026-05-20), 'T' (S24L0957 triton). Missing
    # 'P' was a 2026-05-21 bug that left it disabled across role swaps.
    Write-Host "[probe-check] Re-enabling all probes (finally)..." -ForegroundColor Yellow
    foreach ($p in @('N','A','B','P','T')) {
        try {
            & $setProbe -Probe $p -Action Enable -ErrorAction SilentlyContinue
        } catch {
            # Don't let cleanup failures mask the original error.
            Write-Warning "[probe-check] finally re-enable -Probe $p failed: $_"
        }
    }
}

if ($script:phyTuningDone) {
    # -FlashPhyTuning one-shot path completed successfully. Probes have
    # been re-enabled by the finally{} above. No manifest/deployment-log
    # entry — those describe app flashes, not commissioning steps.
    exit 0
}

if ($flashExitCode -eq 0) {
    # --- Flash manifest + append-only deployment log -------------------
    $manifest = [ordered]@{
        flashedAt   = $buildIso
        probe       = $Probe
        probeSerial = $probeCfg.Serial
        role        = $Role
        roleId      = $roleCfg.Id
        ip          = $Ip
        version     = $fwVersion
        gitSha      = $gitSha
        dirty       = [bool]$dirty
        image       = $image
        imageBytes  = (Get-Item $image).Length
    }
    $manifestPath = "$image.flash-manifest.json"
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -Path $manifestPath -Encoding UTF8
    Write-Host "[manifest] $manifestPath" -ForegroundColor Yellow

    $deployedDoc = 'F:\Constellation\docs\firmware-deployed.md'
    if (Test-Path $deployedDoc) {
        $row = "| {0} | {1} | {2} | {3} | {4} | {5} |" -f `
            $buildIso, $Probe, $Role, $Ip, $fwVersion, $(if ($dirty) { 'YES' } else { 'no' })
        Add-Content -Path $deployedDoc -Value $row
        Write-Host "[log] appended to $deployedDoc" -ForegroundColor Yellow
    }

    # --- Seed F2c metadata so SBL chooser picks the bank we just flashed --
    # See the -NoSeed param help for the rationale. Skips when -NoSeed is
    # set (e.g. rollback negative-tests where we want the chooser to fall
    # back to the OTHER bank). Also skipped if the seed script is missing
    # — older checkouts predate the F2c work.
    if (-not $NoSeed) {
        $seedScript = "$PSScriptRoot\sbl_chooser\Write-SeedMetaBlock.ps1"
        if (Test-Path $seedScript) {
            Write-Host ""
            Write-Host "[seed] F2c metadata seed (so chooser picks the just-flashed Bank A)" -ForegroundColor Yellow
            try {
                # Write-SeedMetaBlock asserts only one probe is enumerated
                # (matches Flash-LP's xdsdfu -e precheck). The finally{}
                # above already re-enabled every probe, so re-solo the
                # target before invoking, then re-enable everything after.
                & $setProbe -Probe $Probe -Action Solo -ErrorAction Stop | Out-Host
                & $seedScript -Probe $Probe | Out-Host
                Write-Host "[seed] F2c metadata seeded; LP will boot the new firmware on this warm reset." -ForegroundColor Green
            } catch {
                Write-Warning "[seed] Write-SeedMetaBlock failed: $_"
                Write-Warning "[seed] The new firmware is on Bank A but the chooser may still boot Bank B."
                Write-Warning "[seed] Run sbl_chooser\Write-SeedMetaBlock.ps1 -Probe $Probe manually to fix."
            } finally {
                foreach ($p in @('N','A','B','P','T')) {
                    try { & $setProbe -Probe $p -Action Enable -ErrorAction SilentlyContinue | Out-Null } catch { }
                }
            }
        } else {
            Write-Host "[seed] (skipped — $seedScript not found)" -ForegroundColor DarkGray
        }
    } else {
        Write-Host "[seed] (skipped — -NoSeed)" -ForegroundColor DarkGray
    }

    Write-Host ""
    Write-Host "DONE.  Probe $Probe now holds $Role @ $Ip ($fwVersion) in OSPI." -ForegroundColor Green
    if (-not $NoSeed) {
        Write-Host "Bank A seeded; SBL chooser will boot the new firmware in ~5 s." -ForegroundColor Green
    } else {
        Write-Host "Auto-flasher triggered SoC warm reset; chooser picks whichever bank has higher sequence." -ForegroundColor Yellow
    }
    Write-Host "UART banner on $($probeCfg.Com); bridge connection on UART2." -ForegroundColor DarkGray
} else {
    Write-Host ""
    Write-Host "FAILED — see F:\Constellation\logs\lp_ospi_flash_probe$Probe.log" -ForegroundColor Red
    exit 1
}
