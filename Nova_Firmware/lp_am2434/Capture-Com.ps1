<#
.SYNOPSIS
  Capture serial output from an LP-AM2434 App User UART (COM5 / COM9 / COM4)
  to both the console and a log file. Press Ctrl+C to stop.

.PARAMETER Port
  Serial port name, e.g. COM5 (Probe A), COM9 (Probe N / B), COM4 (Probe T).

.PARAMETER Baud
  Baud rate. Default 115200 (matches LP firmware DebugP_log).

.PARAMETER LogFile
  Where to tee the output. Default:
  F:\Constellation\logs\uart_<port>_<yyyy-MM-dd_HHmmss>.log

.EXAMPLE
  .\Capture-Com.ps1 -Port COM5
  # Opens COM5 at 115200 8N1, prints to console + writes to a timestamped
  # log file under F:\Constellation\logs\.

.EXAMPLE
  .\Capture-Com.ps1 -Port COM5 -LogFile F:\Constellation\logs\ota_test.log
  # Same but writes to a specific filename.
#>
param(
    [Parameter(Mandatory)][string]$Port,
    [int]$Baud = 115200,
    [string]$LogFile
)

$ErrorActionPreference = 'Stop'

if (-not $LogFile) {
    $stamp = (Get-Date).ToString('yyyy-MM-dd_HHmmss')
    $LogFile = "F:\Constellation\logs\uart_${Port}_$stamp.log"
}

$logDir = Split-Path $LogFile -Parent
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }

Write-Host "[Capture-Com] $Port @ $Baud baud" -ForegroundColor Cyan
Write-Host "[Capture-Com] log file -> $LogFile" -ForegroundColor Cyan
Write-Host "[Capture-Com] Press Ctrl+C to stop." -ForegroundColor Yellow
Write-Host "---------------------------------------------------------------" -ForegroundColor DarkGray

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 1000
$sp.NewLine = "`r`n"

try {
    $sp.Open()
} catch {
    Write-Error "Failed to open ${Port}: $_"
    Write-Host "If '$Port is denied' another program (CCS, Tera Term, PuTTY) probably has it open." -ForegroundColor Yellow
    exit 1
}

# Tee stream: console + file. Append timestamp prefix on the file.
$writer = [System.IO.StreamWriter]::new($LogFile, $true)   # $true = append
$writer.AutoFlush = $true

try {
    while ($true) {
        try {
            $line = $sp.ReadLine()
            $ts   = (Get-Date).ToString('HH:mm:ss.fff')
            Write-Host $line
            $writer.WriteLine("[$ts] $line")
        } catch [System.TimeoutException] {
            # No data this second; keep polling.
        } catch {
            Write-Warning "read error: $_"
            Start-Sleep -Milliseconds 200
        }
    }
} finally {
    $writer.Close()
    $sp.Close()
    Write-Host ""
    Write-Host "[Capture-Com] closed $Port. Log saved: $LogFile" -ForegroundColor Cyan
}
