#requires -RunAsAdministrator
<#
.SYNOPSIS
  Disable / enable an XDS110 probe at the USB level so DSS only sees one.

.DESCRIPTION
  The DSS .ccxml `<property id="The serial number is">` filter is NOT
  reliably honored — when both XDS110 probes are plugged in, DSS will
  silently grab whichever one it locks first (typically the lower
  serial). The only workaround proven to work is making the unwanted
  probe invisible to libusb. Two ways:
    1. Physically unplug the USB cable.
    2. Disable the parent USB Composite Device in Windows.

  This script does (2) — same effect, no cable wrangling.

.PARAMETER Probe
  N | A | B | P | T   (matches Flash-LP.ps1 mapping)
    N -> S24L0707  (CONTROLLER, shares hw w/ B)
    A -> S24L0417  (STORAGE)
    B -> S24L0707  (GDC, shares hw w/ N)
    P -> S24L0727  (Pulsar door controller — GDC initially, TRITON later) [added 2026-05-20]
    T -> S24L0957  (TRITON)  [2026-05-11 reassignment after recovery]

.PARAMETER Action
  Disable | Enable | Solo
    Disable — disable just the named probe.
    Enable  — enable just the named probe.
    Solo    — disable ALL XDS110 probes EXCEPT the named one.

.EXAMPLE
  .\Set-Probe.ps1 -Probe N -Action Solo
  # Leaves only NOVA's XDS110 visible to DSS / xdsdfu.

.EXAMPLE
  .\Set-Probe.ps1 -Probe A -Action Enable
  # Re-enables STORAGE's XDS110.
#>
param(
    [Parameter(Mandatory)][ValidateSet('N','A','B','P','T')] [string]$Probe,
    [Parameter(Mandatory)][ValidateSet('Disable','Enable','Solo')] [string]$Action
)

$ErrorActionPreference = 'Stop'

# Matches Flash-LP.ps1 probeMap. N+B intentionally share S24L0707
# (one physical board, two firmware roles via -Role).
$probeSerial = @{
    'N' = 'S24L0707'
    'A' = 'S24L0417'
    'B' = 'S24L0707'
    'P' = 'S24L0727'   # Pulsar door controller — GDC initially, TRITON later
    'T' = 'S24L0957'
}

$wantSerial = $probeSerial[$Probe]

$all = Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId -match '^USB\\VID_0451&PID_BEF3\\S24L\d+$'
}

if (-not $all) { throw "No XDS110 USB Composite Devices enumerated." }

if ($Action -eq 'Solo') {
    foreach ($d in $all) {
        $serial = ($d.InstanceId -split '\\')[-1]
        if ($serial -eq $wantSerial) {
            if ($d.Status -ne 'OK') {
                Write-Host "[enable]  $serial" -ForegroundColor Green
                Enable-PnpDevice -InstanceId $d.InstanceId -Confirm:$false
            } else {
                Write-Host "[keep on] $serial (already enabled)" -ForegroundColor Green
            }
        } else {
            if ($d.Status -eq 'OK') {
                Write-Host "[disable] $serial" -ForegroundColor Yellow
                Disable-PnpDevice -InstanceId $d.InstanceId -Confirm:$false
            } else {
                Write-Host "[keep off] $serial (already disabled)" -ForegroundColor DarkGray
            }
        }
    }
} else {
    $target = $all | Where-Object { ($_.InstanceId -split '\\')[-1] -eq $wantSerial }
    if (-not $target) {
        # Soft-warn instead of throw so callers (e.g. Flash-LP.ps1's
        # finally-block "re-enable all probes" sweep) don't blow up when
        # only a subset of the rig's probes are physically connected.
        Write-Warning "Probe $wantSerial not currently enumerated (cable unplugged?) - skipping ${Action}."
        return
    }
    if ($Action -eq 'Disable') {
        Write-Host "[disable] $wantSerial" -ForegroundColor Yellow
        Disable-PnpDevice -InstanceId $target.InstanceId -Confirm:$false
    } else {
        Write-Host "[enable]  $wantSerial" -ForegroundColor Green
        Enable-PnpDevice -InstanceId $target.InstanceId -Confirm:$false
    }
}

Start-Sleep -Seconds 2

Write-Host ""
Write-Host "[verify] xdsdfu -e :" -ForegroundColor Cyan
$xdsdfu = 'C:\ti\ccs2050\ccs\ccs_base\common\uscif\xds110\xdsdfu.exe'
if (Test-Path $xdsdfu) {
    & $xdsdfu -e 2>&1 | Select-String 'Serial|Found' | ForEach-Object { Write-Host "  $_" }
}
