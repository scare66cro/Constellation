# Flash-PhaseB-OSPI.ps1
#
# Drives the SBL JTAG Uniflash flow end-to-end with no manual steps.
# Default behaviour: write F:/Constellation/Nova_Firmware/lp_am2434/ti-arm-clang/nova_lp.release.mcelf.hs_fs
# to OSPI offset 0x80000 (the application slot the existing OSPI SBL loads from).
#
# Sequence:
#   1. Open COM4 @ 115200, start a background tee to a log file.
#   2. dss.bat uniflash_start.js   -> boots flasher, returns when CPU is running it.
#   3. Read UART until the menu appears.
#   4. Send '2' (Write+Verify) -> answer filename prompt -> answer offset prompt.
#   5. Capture the "loadRaw(0xXXXXXXXX, 0, ..." prompt, extract the target address.
#   6. dss.bat uniflash_loadraw.js (env: UNIFLASH_ADDR, UNIFLASH_FILE) -> pushes file bytes.
#   7. Send '1' to UART -> wait for "Flashing success" / "Flashing FAIL".
#   8. Send 'x' to exit cleanly.
#   9. Close COM4.
#
# Iteration loop after this works the first time:
#   build -> Flash-PhaseB-OSPI.ps1 -> power-on-reset (or run load_nova.js)
# No physical USB-C cycle required between iterations because the flasher
# never opens CPSW (avoids the lp-am2434-cpsw-reflash-trap).

[CmdletBinding()]
param(
    [string]$File   = 'F:\Constellation\Nova_Firmware\lp_am2434\ti-arm-clang\nova_lp.release.mcelf.hs_fs',
    [string]$Offset = '0x80000',
    [string]$ComPort = 'COM4',
    [int]$BaudRate = 115200,
    [string]$LogPath = 'F:\Constellation\logs\lp_ospi_flash.log',
    [string]$DssBat = 'C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat',
    [string]$ScriptDir = 'F:\Constellation\Nova_Firmware\lp_am2434\ospi_flash'
)

$ErrorActionPreference = 'Stop'

function Test-Required {
    if (-not (Test-Path $File))   { throw "Image not found: $File" }
    if (-not (Test-Path $DssBat)) { throw "DSS not found: $DssBat" }
    if (-not (Test-Path (Join-Path $ScriptDir 'uniflash_start.js'))) {
        throw "Script not found: $ScriptDir\uniflash_start.js"
    }
}

function Open-Com {
    $p = New-Object System.IO.Ports.SerialPort $ComPort, $BaudRate, 'None', 8, 'One'
    $p.NewLine     = "`n"
    $p.ReadTimeout = 200
    $p.WriteTimeout = 2000
    $p.Open()
    return $p
}

function Write-Log([string]$line) {
    $stamp = Get-Date -Format 'HH:mm:ss.fff'
    "$stamp | $line" | Tee-Object -FilePath $LogPath -Append | Out-Host
}

# Read UART until $pattern (regex) is seen on a line, OR timeout. Returns the matching line, or $null.
function Wait-ForLine([System.IO.Ports.SerialPort]$port, [string]$pattern, [int]$timeoutSec = 60) {
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    $buf = New-Object System.Text.StringBuilder
    while ((Get-Date) -lt $deadline) {
        try {
            while ($port.BytesToRead -gt 0) {
                $ch = [char]$port.ReadByte()
                if ($ch -eq "`n" -or $ch -eq "`r") {
                    if ($buf.Length -gt 0) {
                        $line = $buf.ToString()
                        [void]$buf.Clear()
                        Write-Log "<< $line"
                        if ($line -match $pattern) { return $line }
                    }
                } else {
                    [void]$buf.Append($ch)
                }
            }
            # The flasher prompts ("Enter Choice: ") may not be newline-terminated.
            # Detect when buffer matches the pattern itself.
            if ($buf.Length -gt 0) {
                $partial = $buf.ToString()
                if ($partial -match $pattern) {
                    Write-Log "<< $partial   (no newline)"
                    [void]$buf.Clear()
                    return $partial
                }
            }
            Start-Sleep -Milliseconds 50
        } catch [System.TimeoutException] { }
    }
    if ($buf.Length -gt 0) { Write-Log "<< [partial-on-timeout] $($buf.ToString())" }
    return $null
}

function Send-Text([System.IO.Ports.SerialPort]$port, [string]$text) {
    Write-Log ">> $text"
    $port.Write($text + "`n")
    Start-Sleep -Milliseconds 200
}

function Invoke-Dss([string]$jsName, [hashtable]$envVars = @{}) {
    $jsPath = Join-Path $ScriptDir $jsName
    Write-Log "[dss] $jsPath"
    foreach ($k in $envVars.Keys) {
        Write-Log "[env] $k=$($envVars[$k])"
        Set-Item -Path "Env:$k" -Value $envVars[$k]
    }
    # NOTE: dss.bat may write to stderr / exit non-zero with
    # "Invalid CIO command" when it disconnects from a target that is
    # still running (the flasher uses semihosting CIO for DebugP_log).
    # That's cosmetic - capture and log but do NOT throw. Verify via UART.
    $tmpOut = [System.IO.Path]::GetTempFileName()
    $tmpErr = [System.IO.Path]::GetTempFileName()
    $proc = Start-Process -FilePath $DssBat -ArgumentList @("`"$jsPath`"") `
                          -NoNewWindow -Wait -PassThru `
                          -RedirectStandardOutput $tmpOut `
                          -RedirectStandardError  $tmpErr
    Get-Content $tmpOut | ForEach-Object { Write-Log "[dss] $_" }
    Get-Content $tmpErr | ForEach-Object { Write-Log "[dss-err] $_" }
    Remove-Item $tmpOut, $tmpErr -Force -ErrorAction SilentlyContinue
    if ($proc.ExitCode -ne 0) {
        Write-Log "[dss] WARN exit=$($proc.ExitCode) (ignored - will verify via UART)"
    }
}

# ---- main -----------------------------------------------------------------
Test-Required
"" | Set-Content -Path $LogPath
Write-Log "=== Flash-PhaseB-OSPI ==="
Write-Log "File   : $File"
Write-Log "Offset : $Offset"
Write-Log "Port   : $ComPort @ $BaudRate"

# 1) Boot the flasher first (CPU-side). DSS holds COM4-free, so do this BEFORE opening COM4.
Invoke-Dss 'uniflash_start.js'

# 2) Open serial and drive the menu.
$port = Open-Com
try {
    # Wait for "Enter Choice:" prompt (menu appears after flasher banner).
    if (-not (Wait-ForLine $port 'Enter Choice' 30)) {
        throw "Did not see flasher menu prompt within 30 s. Is the flasher really running on R5F0-0?"
    }

    # 3) Choose option 2 (Write + Verify).
    Send-Text $port '2'

    # 4) Filename prompt.
    if (-not (Wait-ForLine $port 'Enter file name' 15)) { throw "No filename prompt" }
    # Flasher expects forward slashes; convert.
    $fileFwd = $File -replace '\\','/'
    Send-Text $port $fileFwd

    # 5) Offset prompt.
    if (-not (Wait-ForLine $port 'Enter flash offset' 15)) { throw "No offset prompt" }
    Send-Text $port $Offset

    # 6) Wait for the loadRaw(...) instruction line so we can extract the target buffer address.
    $loadRawLine = Wait-ForLine $port 'loadRaw\(0x[0-9A-Fa-f]+' 15
    if (-not $loadRawLine) { throw "Did not see loadRaw(...) prompt" }
    if ($loadRawLine -notmatch 'loadRaw\((0x[0-9A-Fa-f]+)') { throw "Could not parse loadRaw addr from: $loadRawLine" }
    $rawAddr = $Matches[1]
    Write-Log "[parsed] loadRaw target addr = $rawAddr"

    # 7) Push the file via a fresh DSS attach.
    Invoke-Dss 'uniflash_loadraw.js' @{ UNIFLASH_ADDR = $rawAddr; UNIFLASH_FILE = $fileFwd }

    # 8) Tell the flasher to proceed.
    Send-Text $port '1'

    # 9) Wait for outcome (writes can take a while for a multi-MB image).
    $result = Wait-ForLine $port '(Flashing success|Flashing FAIL|FAILED|ERROR)' 240
    if (-not $result) { throw "No result line from flasher within 240 s" }
    if ($result -notmatch 'success') { throw "Flasher reported failure: $result" }
    Write-Log "[OK] $result"

    # 10) Re-prompt for menu, then exit.
    [void](Wait-ForLine $port 'Enter Choice' 15)
    Send-Text $port 'x'
    [void](Wait-ForLine $port 'Application exited' 5)

    Write-Log "=== DONE - Phase B image is now in OSPI at $Offset ==="
}
finally {
    if ($port -and $port.IsOpen) { $port.Close() }
}
