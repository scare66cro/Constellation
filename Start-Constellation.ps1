<#
.SYNOPSIS
  Constellation Dev Stack Launcher
.DESCRIPTION
  Starts the full simulation + Azure web stack in order:
    1. PostgreSQL check       (Docker or local :5432)
    2. Orbit Simulator        (Modbus TCP :5502, API :9010)
    3. Nova QEMU (WSL)        (UART1 :9000, UART2 -> Orbit :5502)
    4. Bridge Server          (HTTP :9001, Serial -> QEMU :9000)
    5. Django Backend API     (HTTP :8000)
    6. React Frontend PWA     (HTTP :3000)
    7. Constellation UI       (Vite :81)

  Usage:
    .\Start-Constellation.ps1              # Start the stack
    .\Start-Constellation.ps1 -Stop        # Stop a running stack
    .\Start-Constellation.ps1 -Restart     # Stop then start
#>
param(
    [switch]$Stop,
    [switch]$Restart
)

$ErrorActionPreference = "Stop"
$BASE = "F:\Agristar\Agristar\Constellation"
$AZURE = "$BASE\Azure"
$WIN_IP = "172.21.16.1"
$PID_FILE = "$BASE\.constellation-pids"

# ── Helper: stop the stack ───────────────────────────────────────────────
function Stop-Constellation {
    Write-Host "Shutting down Constellation stack..." -ForegroundColor Yellow
    if (Test-Path $PID_FILE) {
        $stopped = 0
        Get-Content $PID_FILE | ForEach-Object {
            $p = [int]$_
            if (Get-Process -Id $p -ErrorAction SilentlyContinue) {
                & taskkill /F /T /PID $p 2>$null | Out-Null
                $stopped++
            }
        }
        Remove-Item $PID_FILE -Force
        Write-Host "Stopped $stopped process tree(s)." -ForegroundColor Green
    } else {
        Write-Host "No PID file found — nothing tracked." -ForegroundColor DarkGray
    }
    wsl -d Ubuntu-24.04 -u scare66cro -e bash -c 'pkill -9 qemu-system-arm 2>/dev/null' 2>$null
    Write-Host "Done." -ForegroundColor Green
}

# ── Stop mode ────────────────────────────────────────────────────────────
if ($Stop) {
    Stop-Constellation
    return
}

# ── Restart mode ─────────────────────────────────────────────────────────
if ($Restart) {
    Stop-Constellation
    Start-Sleep -Seconds 2
    Write-Host ""
}

try {

Write-Host ""
Write-Host "  Constellation Dev Stack Launcher" -ForegroundColor Cyan
Write-Host "  ==========================================" -ForegroundColor Cyan
Write-Host ""

# -- Cleanup old Constellation processes ----------------------------------
# Only kill our own processes — never blanket-kill all node.exe instances.
# We track PIDs in a file so a previous crashed session can be cleaned up.
Write-Host "[1/8] Cleaning up previous Constellation session..." -ForegroundColor Yellow
if (Test-Path $PID_FILE) {
    Get-Content $PID_FILE | ForEach-Object {
        $oldPid = [int]$_
        if (Get-Process -Id $oldPid -ErrorAction SilentlyContinue) {
            & taskkill /F /T /PID $oldPid 2>$null | Out-Null
            Write-Host "   Stopped leftover process tree $oldPid" -ForegroundColor DarkGray
        }
    }
    Remove-Item $PID_FILE -Force
}
wsl -d Ubuntu-24.04 -u scare66cro -e bash -c 'pkill -9 qemu-system-arm 2>/dev/null' 2>$null
Start-Sleep -Seconds 2

# Port conflict check — warn if any of our ports are already in use
$requiredPorts = @(5502, 9000, 9001, 9010, 8000, 3000, 81)
foreach ($p in $requiredPorts) {
    $inUse = Get-NetTCPConnection -LocalPort $p -State Listen -ErrorAction SilentlyContinue
    if ($inUse) {
        $ownerPid = $inUse[0].OwningProcess
        $ownerName = (Get-Process -Id $ownerPid -ErrorAction SilentlyContinue).ProcessName
        Write-Host "   WARNING: Port $p already in use by $ownerName (PID $ownerPid)" -ForegroundColor Red
    }
}

# -- 1. PostgreSQL check --------------------------------------------------
Write-Host "[2/8] Checking PostgreSQL (:5432)..." -ForegroundColor Cyan
$pgReady = $false
try {
    $tcp = New-Object System.Net.Sockets.TcpClient
    $tcp.Connect("localhost", 5432)
    $tcp.Close()
    $pgReady = $true
    Write-Host "   PostgreSQL listening on :5432" -ForegroundColor Green
}
catch {
    # Try starting Docker container if Docker is available
    $dockerCmd = Get-Command docker -ErrorAction SilentlyContinue
    if ($dockerCmd) {
        $existing = docker ps -a --filter "name=agristar-postgres" --format "{{.Names}}" 2>$null
        if ($existing -eq "agristar-postgres") {
            docker start agristar-postgres 2>$null | Out-Null
            Write-Host "   Started Docker agristar-postgres container" -ForegroundColor Green
        } else {
            docker run -d --name agristar-postgres `
                -e POSTGRES_DB=agristar_local `
                -e POSTGRES_USER=agristar `
                -e POSTGRES_PASSWORD=localdevpassword `
                -p 5432:5432 postgres:15-alpine 2>$null | Out-Null
            Write-Host "   Created & started Docker agristar-postgres" -ForegroundColor Green
        }
        Start-Sleep -Seconds 3
        $pgReady = $true
    } else {
        Write-Host "   WARNING: PostgreSQL not running and Docker not found" -ForegroundColor Red
        Write-Host "   Django backend will fail without a database" -ForegroundColor Red
    }
}

# -- 2. Orbit Simulator ---------------------------------------------------
Write-Host "[3/8] Starting Orbit Simulator (TCP :5502, API :9010)..." -ForegroundColor Cyan
$orbitSim = Start-Process "powershell.exe" `
    -ArgumentList "-NoProfile -Command `$env:SENSOR_RTU_PORT='0'; `$env:ORBIT_IDS='2,3'; `$env:VFD_RTU_PORT='5520'; cd '$BASE\orbit-simulator'; npx tsx src/index.ts" `
    -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 3

# -- 3. Nova QEMU ----------------------------------------------------------
Write-Host "[4/8] Starting Nova QEMU in WSL (UART1 :9000)..." -ForegroundColor Cyan
$qemu = Start-Process "wsl.exe" -ArgumentList "-d Ubuntu-24.04 -u scare66cro -e bash /mnt/f/Agristar/Agristar/Constellation/qemu-constellation/start_nova_qemu.sh" `
    -WindowStyle Hidden -PassThru

Write-Host "   Waiting for QEMU port 9000..." -ForegroundColor DarkGray
$qemuReady = $false
for ($i = 0; $i -lt 15; $i++) {
    Start-Sleep -Seconds 1
    try { $tcp = New-Object System.Net.Sockets.TcpClient; $tcp.Connect("localhost", 9000); $tcp.Close(); $qemuReady = $true; break } catch {}
}
if ($qemuReady) { Write-Host "   QEMU listening on :9000" -ForegroundColor Green }
else { Write-Host "   WARNING: QEMU port 9000 not open -- check WSL" -ForegroundColor Red }

# -- 4. Bridge Server -----------------------------------------------------
Write-Host "[5/8] Starting Bridge Server (:9001)..." -ForegroundColor Cyan
$bridge = Start-Process "powershell.exe" `
    -ArgumentList "-NoProfile -Command `$env:VFD_ENABLED='true'; `$env:VFD_PORT='5020'; cd '$BASE\constellation-ui\server'; npx tsx src/index.ts" `
    -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 5

# -- 5. Django Backend API ------------------------------------------------
Write-Host "[6/8] Starting Django Backend API (:8000)..." -ForegroundColor Cyan
$django = $null
if ($pgReady) {
    $django = Start-Process "powershell.exe" `
        -ArgumentList "-NoProfile -Command `$env:DJANGO_ENV='development'; `$env:PGHOST='localhost'; `$env:PGDATABASE='agristar_local'; `$env:PGUSER='agristar'; `$env:PGPASSWORD='localdevpassword'; cd '$AZURE\Backend_Gel_Ops'; python manage.py runserver 8000 --noreload" `
        -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 3
    Write-Host "   Django API on :8000" -ForegroundColor Green
} else {
    Write-Host "   SKIPPED — no PostgreSQL available" -ForegroundColor DarkYellow
}

# -- 6. React Frontend PWA ------------------------------------------------
Write-Host "[7/8] Starting React Frontend PWA (:3000)..." -ForegroundColor Cyan
$reactPwa = $null
$pwaSrcDir = "$AZURE\frontend_storage_pwa"
if (Test-Path "$pwaSrcDir\package.json") {
    if (-not (Test-Path "$pwaSrcDir\node_modules")) {
        Write-Host "   Installing npm dependencies (first run)..." -ForegroundColor DarkGray
        Start-Process "powershell.exe" -ArgumentList "-NoProfile -Command cd '$pwaSrcDir'; npm install" `
            -WindowStyle Hidden -Wait
    }
    $reactPwa = Start-Process "powershell.exe" `
        -ArgumentList "-NoProfile -Command `$env:REACT_APP_API_URL='http://localhost:8000'; `$env:REACT_APP_DISABLE_AUTH='true'; `$env:PORT='3000'; cd '$pwaSrcDir'; npx react-scripts start" `
        -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 3
    Write-Host "   React PWA on :3000" -ForegroundColor Green
} else {
    Write-Host "   SKIPPED — frontend_storage_pwa not found" -ForegroundColor DarkYellow
}

# -- 7. Constellation UI Dev Server ---------------------------------------
Write-Host "[8/8] Starting Constellation UI Dev Server (:81)..." -ForegroundColor Cyan
$ui = Start-Process "powershell.exe" `
    -ArgumentList "-NoProfile -Command cd '$BASE\constellation-ui'; npx vite dev --port 81" `
    -WindowStyle Hidden -PassThru
Start-Sleep -Seconds 3

# -- Summary ---------------------------------------------------------------
Write-Host ""
Write-Host "  ==========================================" -ForegroundColor Green
Write-Host "  Constellation Stack Running" -ForegroundColor Green
Write-Host "  ==========================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Constellation UI  http://localhost:81" -ForegroundColor White
Write-Host "  Bridge Server     http://localhost:9001" -ForegroundColor White
Write-Host "  Orbit API         http://localhost:9010/api/status" -ForegroundColor White
if ($django) {
Write-Host "  Django Backend    http://localhost:8000" -ForegroundColor White
}
if ($reactPwa) {
Write-Host "  React PWA         http://localhost:3000" -ForegroundColor White
}
Write-Host "  QEMU UART log     wsl cat /tmp/nova_ui.log" -ForegroundColor DarkGray
Write-Host ""

# Save PIDs for cleanup on next launch or shutdown
$allPids = @()
if ($orbitSim)  { $allPids += $orbitSim.Id }
if ($qemu)      { $allPids += $qemu.Id }
if ($bridge)    { $allPids += $bridge.Id }
if ($django)    { $allPids += $django.Id }
if ($reactPwa)  { $allPids += $reactPwa.Id }
if ($ui)        { $allPids += $ui.Id }
$allPids | Out-File -FilePath $PID_FILE -Encoding ASCII

# Open the UI in the default browser
Start-Process "http://localhost:81"

Write-Host "  To stop:  .\Start-Constellation.ps1 -Stop" -ForegroundColor DarkGray
Write-Host ""

}
catch {
    Write-Host ""
    Write-Host "ERROR: $_" -ForegroundColor Red
    Write-Host $_.ScriptStackTrace -ForegroundColor DarkGray
}
