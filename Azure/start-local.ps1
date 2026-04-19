# Agristar Local Development Stack — Quick Start Script
# Run this from PowerShell to start everything locally
#
# Prerequisites:
#   1. Docker Desktop installed
#   2. VS Code with Azurite extension
#   3. Python 3.12+ with pip
#   4. Node.js 18+

$ErrorActionPreference = "Stop"
$AzureDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "═══════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "  Agristar Azure Stack — Local Development                 " -ForegroundColor Cyan
Write-Host "═══════════════════════════════════════════════════════════" -ForegroundColor Cyan

# ═══════════════════════════════════════════════════════════════════
# Step 1: PostgreSQL
# ═══════════════════════════════════════════════════════════════════
Write-Host "`n[1/4] PostgreSQL Database" -ForegroundColor Yellow

$dockerAvailable = $null -ne (Get-Command docker -ErrorAction SilentlyContinue)

if ($dockerAvailable) {
    $existing = docker ps -a --filter "name=agristar-postgres" --format "{{.Names}}" 2>$null
    if ($existing -eq "agristar-postgres") {
        $running = docker ps --filter "name=agristar-postgres" --format "{{.Names}}" 2>$null
        if ($running) {
            Write-Host "  ✓ PostgreSQL already running" -ForegroundColor Green
        } else {
            docker start agristar-postgres | Out-Null
            Write-Host "  ✓ PostgreSQL container started" -ForegroundColor Green
        }
    } else {
        Write-Host "  → Creating PostgreSQL container..." -ForegroundColor Gray
        docker run -d --name agristar-postgres `
            -e POSTGRES_DB=agristar_local `
            -e POSTGRES_USER=agristar `
            -e POSTGRES_PASSWORD=localdevpassword `
            -p 5432:5432 `
            postgres:15-alpine | Out-Null
        Write-Host "  ✓ PostgreSQL container created" -ForegroundColor Green
        Start-Sleep -Seconds 3
    }
} else {
    Write-Host "  ✗ Docker not found!" -ForegroundColor Red
    Write-Host "    Install Docker Desktop or PostgreSQL manually" -ForegroundColor Gray
    exit 1
}

# ═══════════════════════════════════════════════════════════════════
# Step 2: Azurite (Azure Storage Emulator)
# ═══════════════════════════════════════════════════════════════════
Write-Host "`n[2/4] Azurite (Azure Storage Emulator)" -ForegroundColor Yellow
Write-Host "  → In VS Code: Press Ctrl+Shift+P → 'Azurite: Start'" -ForegroundColor Cyan
Write-Host "  → Or click the Azurite button in the status bar" -ForegroundColor Cyan
Write-Host "  ✓ Ready (start manually in VS Code)" -ForegroundColor Green

# ═══════════════════════════════════════════════════════════════════
# Step 3: Environment Variables
# ═══════════════════════════════════════════════════════════════════
Write-Host "`n[3/4] Setting environment variables..." -ForegroundColor Yellow
$env:DJANGO_ENV = "development"
$env:PGHOST = "localhost"
$env:PGDATABASE = "agristar_local"
$env:PGUSER = "agristar"
$env:PGPASSWORD = "localdevpassword"
$env:SERVER_HOST = "localhost"
$env:SERVER_PORT = "8000"
$env:SERVER_SCHEME = "http"
Write-Host "  ✓ Environment configured" -ForegroundColor Green

# ═══════════════════════════════════════════════════════════════════
# Step 4: Instructions
# ═══════════════════════════════════════════════════════════════════
Write-Host "`n[4/4] Ready to start servers" -ForegroundColor Yellow
Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║  Open 2 new terminals and run:                          ║" -ForegroundColor Cyan
Write-Host "╠══════════════════════════════════════════════════════════╣" -ForegroundColor Cyan
Write-Host "║                                                          ║" -ForegroundColor Cyan
Write-Host "║  Terminal 1 (Backend):                                   ║" -ForegroundColor Cyan
Write-Host "║    cd $AzureDir\Backend_Gel_Ops" -ForegroundColor White
Write-Host "║    pip install -r requirements.txt                       ║" -ForegroundColor White
Write-Host "║    python manage.py migrate                              ║" -ForegroundColor White
Write-Host "║    python manage.py runserver 8000                       ║" -ForegroundColor White
Write-Host "║                                                          ║" -ForegroundColor Cyan
Write-Host "║  Terminal 2 (Frontend):                                  ║" -ForegroundColor Cyan
Write-Host "║    cd $AzureDir\frontend_storage_pwa" -ForegroundColor White
Write-Host "║    `$env:REACT_APP_API_URL='http://localhost:8000'       ║" -ForegroundColor White
Write-Host "║    `$env:REACT_APP_DISABLE_AUTH='true'                   ║" -ForegroundColor White
Write-Host "║    npm install                                           ║" -ForegroundColor White
Write-Host "║    npm start                                             ║" -ForegroundColor White
Write-Host "║                                                          ║" -ForegroundColor Cyan
Write-Host "╠══════════════════════════════════════════════════════════╣" -ForegroundColor Cyan
Write-Host "║  URLs:                                                   ║" -ForegroundColor Cyan
Write-Host "║    Frontend: http://localhost:3000                       ║" -ForegroundColor Green
Write-Host "║    Backend:  http://localhost:8000                       ║" -ForegroundColor Green
Write-Host "╚══════════════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# Option to open terminals automatically
$response = Read-Host "Open new terminals for backend and frontend? (y/n)"
if ($response -eq 'y') {
    # Start backend terminal
    Start-Process powershell -ArgumentList "-NoExit", "-Command", @"
        cd '$AzureDir\Backend_Gel_Ops'
        `$env:DJANGO_ENV = 'development'
        `$env:PGHOST = 'localhost'
        `$env:PGDATABASE = 'agristar_local'
        `$env:PGUSER = 'agristar'
        `$env:PGPASSWORD = 'localdevpassword'
        Write-Host 'Backend terminal ready. Run:' -ForegroundColor Yellow
        Write-Host '  pip install -r requirements.txt' -ForegroundColor Cyan
        Write-Host '  python manage.py migrate' -ForegroundColor Cyan
        Write-Host '  python manage.py runserver 8000' -ForegroundColor Cyan
"@

    # Start frontend terminal
    Start-Process powershell -ArgumentList "-NoExit", "-Command", @"
        cd '$AzureDir\frontend_storage_pwa'
        `$env:REACT_APP_API_URL = 'http://localhost:8000'
        `$env:REACT_APP_DISABLE_AUTH = 'true'
        Write-Host 'Frontend terminal ready. Run:' -ForegroundColor Yellow
        Write-Host '  npm install' -ForegroundColor Cyan
        Write-Host '  npm start' -ForegroundColor Cyan
"@

    Write-Host "`nNew terminals opened!" -ForegroundColor Green
}
