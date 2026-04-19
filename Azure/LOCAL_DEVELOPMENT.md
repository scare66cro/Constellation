# Agristar Azure Stack — Local Development

This folder contains the Azure-hosted services for Agristar. You can run everything **locally** without touching production.

## Prerequisites

### 1. VS Code Extensions (Required)

Install these extensions in VS Code:
- **Azurite** — Local Azure Storage emulator (Blob, Queue, Table)
- **Azure Functions** — Run Azure Functions locally

### 2. PostgreSQL Database

**Option A (Recommended): Docker**
```powershell
docker run -d --name agristar-postgres `
  -e POSTGRES_DB=agristar_local `
  -e POSTGRES_USER=agristar `
  -e POSTGRES_PASSWORD=localdevpassword `
  -p 5432:5432 `
  postgres:15-alpine
```

**Option B: Native Install**
Download from https://www.postgresql.org/download/windows/

### 3. Azure Functions Core Tools
```powershell
npm install -g azure-functions-core-tools@4 --unsafe-perm true
```

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    LOCAL DEVELOPMENT STACK                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐     ┌──────────────────┐     ┌─────────────┐ │
│  │   React PWA  │────▶│  Django Backend  │────▶│  PostgreSQL │ │
│  │  port 3000   │     │    port 8000     │     │  port 5432  │ │
│  └──────────────┘     └──────────────────┘     └─────────────┘ │
│                                                                  │
│  (Azure Functions IoT Hub handler not needed for local dev)     │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Quick Start (Docker)

```powershell
cd F:\Agristar\Agristar\Constellation\azure

# Start the full stack
docker-compose -f docker-compose.local.yml up -d

# View logs
docker-compose -f docker-compose.local.yml logs -f

# Stop everything
docker-compose -f docker-compose.local.yml down
```

Then open:
- **Frontend**: http://localhost:3000
- **Backend API**: http://localhost:8000
- **Admin**: http://localhost:8000/admin

## Quick Start (Without Docker)

### 1. Start PostgreSQL

You can use Docker just for the database:
```powershell
docker run -d --name agristar-postgres `
  -e POSTGRES_DB=agristar_local `
  -e POSTGRES_USER=agristar `
  -e POSTGRES_PASSWORD=localdevpassword `
  -p 5432:5432 `
  postgres:15-alpine
```

Or install PostgreSQL locally and create the database:
```sql
CREATE DATABASE agristar_local;
CREATE USER agristar WITH PASSWORD 'localdevpassword';
GRANT ALL PRIVILEGES ON DATABASE agristar_local TO agristar;
```

### 2. Start Django Backend

```powershell
cd F:\Agristar\Agristar\Constellation\azure\Backend_Gel_Ops

# Create virtual environment (first time only)
python -m venv venv
.\venv\Scripts\Activate.ps1

# Install dependencies
pip install -r requirements.txt

# Set environment variables
$env:DJANGO_ENV = "development"
$env:PGHOST = "localhost"
$env:PGDATABASE = "agristar_local"
$env:PGUSER = "agristar"
$env:PGPASSWORD = "localdevpassword"

# Run migrations
python manage.py migrate

# Create superuser (first time only)
python manage.py createsuperuser

# Start server
python manage.py runserver 8000
```

### 3. Start React Frontend

```powershell
cd F:\Agristar\Agristar\Constellation\azure\frontend_storage_pwa

# Install dependencies (first time only)
npm install

# Set environment variables
$env:REACT_APP_API_URL = "http://localhost:8000"
$env:REACT_APP_DISABLE_AUTH = "true"

# Start dev server
npm start
```

## Environment Variables

### Django Backend (`Backend_Gel_Ops`)

| Variable | Default | Description |
|----------|---------|-------------|
| `DJANGO_ENV` | `development` | Set to `development` for local |
| `PGHOST` | `localhost` | Database host |
| `PGDATABASE` | `agristar_local` | Database name |
| `PGUSER` | `agristar` | Database user |
| `PGPASSWORD` | `localdevpassword` | Database password |
| `SERVER_HOST` | `localhost` | API host for CORS |
| `SERVER_PORT` | `8000` | API port |
| `SERVER_SCHEME` | `http` | http or https |

### React Frontend (`frontend_storage_pwa`)

| Variable | Default | Description |
|----------|---------|-------------|
| `REACT_APP_API_URL` | `http://localhost:8000` | Backend API URL |
| `REACT_APP_DISABLE_AUTH` | `true` | Skip Azure AD auth locally |

## Folder Structure

```
azure/
├── AgristarCloud/           # Azure Functions (IoT Hub handler)
│   └── IoTHub_EventHub/     # Event Hub trigger function
├── Backend_Gel_Ops/         # Django REST API backend
│   ├── gellert_apps/        # Django apps
│   │   ├── api/             # Main API endpoints
│   │   └── user_account_app/# User management
│   └── gellert_project/     # Django settings
├── frontend_storage_pwa/    # React PWA frontend
│   └── src/                 # React source code
├── docker-compose.local.yml # Local dev orchestration
└── LOCAL_DEVELOPMENT.md     # This file
```

## Connecting to Constellation Simulators

The local Django backend can receive data from the Constellation bridge server:

```powershell
# In the Constellation bridge server, set:
$env:CLOUD_API_URL = "http://localhost:8000/api"


# Then the bridge will POST controller data to the local Django backend
# instead of Azure IoT Hub
```

## Troubleshooting

### Database connection refused
```powershell
# Check if PostgreSQL is running
docker ps | Select-String postgres

# Or check Windows service
Get-Service postgresql*
```

### CORS errors in browser
Make sure `SERVER_HOST=localhost` is set in Django and the frontend is calling `http://localhost:8000`.

### Auth redirect loop
Set `REACT_APP_DISABLE_AUTH=true` to bypass Azure AD authentication locally.

## Not Needed for Local Development

- **AgristarCloud/** — Azure Functions for IoT Hub. Only needed when receiving data from real controllers via Azure IoT Hub.
- Azure AD / SAML authentication — Disabled locally via `REACT_APP_DISABLE_AUTH=true`
- SSL certificates — Not needed for local http://localhost
