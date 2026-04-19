# Azure Cloud Integration Plan — Agristar Constellation

## Current State

Agristar has an existing Azure tenant/subscription used by the AS2 mobile app. It hosts:
- Azure IoT Hub (device connectivity for AS2 controllers)
- Log storage (operational data from deployed AS2 systems)

Constellation is **not yet deployed** to Azure. This document captures the integration plan for when we're ready.

---

## Phase 0 — Remote UI Access via Azure Relay (Near-Term)

**Goal**: Access the Constellation UI from anywhere without opening router ports.

This is independent of the full IoT Hub integration — it can be done first with minimal Azure resources.

### How It Works

Each Nova's bridge server opens an **outbound** WebSocket tunnel to Azure Relay.
Azure proxies inbound HTTP/WS requests back through that tunnel.
No inbound ports, no VPN, no port forwarding on the customer's router.

```
Browser (anywhere)
    │
    ▼
Azure Relay Hybrid Connection   ← HTTPS, standard port 443
    │
    ▼  (outbound WS tunnel from Nova)
Bridge server (:9001) on the RPi5/Nova
    │
    ▼
ARM firmware (serial)
```

### Azure Resources Needed

| Resource | Purpose | Cost |
|----------|---------|------|
| Relay namespace | Hosts Hybrid Connections | ~$10/month |
| 1 Hybrid Connection per Nova | Routes traffic to the right device | Included in namespace |
| Static Web App (optional) | Host the SvelteKit UI in the cloud | Free tier |

### Implementation (Bridge Server Side)

- New file: `server/src/azureRelay.ts` — uses `hyco-ws` to open relay listener
- Config via env var `AZURE_RELAY_CONNECTION_STRING` or `.sim-config/azureRelay.json`
- The `novaId` (persistent UUID, already implemented) becomes the Hybrid Connection entity name
- Auto-reconnect with exponential backoff
- All existing REST/WebSocket routes work unchanged through the tunnel

### Implementation (Cloud Side)

- **Option A** — Direct relay URL: user browses to `https://{namespace}.servicebus.windows.net/{novaId}/`
- **Option B** — Azure Static Web App + proxy Function: deploy SvelteKit UI to Azure, a thin Azure Function routes `/iot/*` through the relay to the selected Nova
- The panel dropdown and group system (already built) work unchanged — `novaId` routes to the right device

### Multi-Nova Routing

- Each Nova registers itself with the relay namespace on startup
- A small Azure Function maintains a registry: `novaId → relay endpoint + panelName`
- The cloud-hosted UI queries the registry to populate the panel dropdown
- Selecting a panel routes all traffic through that Nova's relay tunnel
- Works with the existing site group system (groups store `novaId`, not IP)

### Status

- [x] Persistent `novaId` (UUID) per Nova — already implemented in bridge server
- [x] `GET /iot/identity` endpoint — returns `{ novaId, panelName }`
- [x] Site groups use `novaId` — survive DHCP IP changes
- [ ] `azureRelay.ts` — bridge-side relay listener (ready to build when connection string available)
- [ ] Azure Relay namespace + Hybrid Connection created
- [ ] Azure Static Web App deployment (optional)
- [ ] Nova registry Azure Function (optional, for multi-site fleet view)

---

## Phase 1 — Exploration (Pre-Deployment)

**Goal**: Map the existing Azure infrastructure before adding Constellation resources.

Tasks:
- [ ] Inventory all resources in the existing subscription (IoT Hub, storage accounts, App Services, databases, etc.)
- [ ] Understand how AS2 mobile app connects (device connection strings, message routing rules, consumer groups)
- [ ] Document the existing log data schema — what fields, what frequency, what retention
- [ ] Identify what can be reused (same IoT Hub? same storage?) vs. what needs separate Constellation resources
- [ ] Review Azure AD / Entra ID setup — service principals, app registrations, RBAC roles

---

## Phase 2 — Architecture Design

### 2.1 RPi5 as Cloud Gateway

The RPi5 is the only internet-facing component in a Constellation system. It connects to Nova via UART1 (serial bridge) and to the site network via Ethernet. The RPi5 runs the cloud agent that relays data to Azure.

```
Nova (AM2434)                    RPi5                              Azure
─────────────                    ────                              ─────
R5F-0 control ──UART1──► Bridge server ──► Cloud agent ──TLS──► IoT Hub
                         (localhost:9001)   (Node.js)            │
                              │                                   ├── Device Twin
                              │                                   ├── D2C telemetry
                              └── Web UI (:81)                    ├── C2D commands
                                                                  └── Blob Storage
```

### 2.2 Data Flowing Up (Device-to-Cloud)

| Data | Source | Frequency | IoT Hub Message Type |
|------|--------|-----------|---------------------|
| Live sensor readings | SensorData protobuf | Every 60s (configurable) | D2C telemetry |
| Equipment status | EquipmentStatus protobuf | On change | D2C telemetry |
| Alarm/warning events | WarningReport protobuf | On change | D2C telemetry (high priority) |
| Activity events | ActivityEvent protobuf | On occurrence | D2C telemetry |
| Firmware version | VersionInfo protobuf | On connect + daily | Device twin reported properties |
| Zone/orbit config | OrbitDiscovery protobuf | On connect + on change | Device twin reported properties |
| System health | Heartbeat, uptime, comm errors | Every 5 min | Device twin reported properties |

### 2.3 Data Flowing Down (Cloud-to-Device)

| Data | Target | Mechanism |
|------|--------|-----------|
| Settings updates | Nova firmware (via bridge) | C2D message → bridge → serial |
| Firmware images | Nova OTA update | Blob Storage URL in C2D → RPi5 downloads → bridge pushes chunks |
| Remote commands | Equipment overrides, system commands | C2D direct method |
| Time sync | RTC correction | Device twin desired property |
| Configuration push | IO config, zone assignments | Device twin desired property |

### 2.4 Device Identity

Each Constellation system registers as a single IoT Hub device:
- **Device ID**: `constellation-{site_id}-{unit_id}` (e.g., `constellation-boise-01`)
- **Auth**: X.509 certificate (preferred) or SAS token
- **Device twin**: stores reported properties (versions, sensor snapshot, orbit config) and desired properties (settings, OTA target version)

### 2.5 Remote System Information (Fleet Dashboard)

What the cloud dashboard needs to show across all deployed Constellation units:

| Feature | Data Source | Storage |
|---------|------------|---------|
| **Live sensor data** | D2C telemetry → IoT Hub → Stream Analytics or Function | Time-series DB (ADX, Cosmos, or Table Storage) |
| **Alarm/event history** | D2C telemetry (high priority) | Cosmos DB or SQL |
| **Firmware versions per site** | Device twin reported properties | IoT Hub query |
| **Configuration snapshots** | Device twin reported properties | IoT Hub query |
| **Equipment runtime hours** | Periodic D2C telemetry | Time-series aggregate |

---

## Phase 3 — Implementation

### 3.1 Cloud Agent (RPi5 Service)

A lightweight Node.js service running on the RPi5 alongside the bridge server:

```
Constellation/
  constellation-cloud-agent/     ← NEW
    src/
      index.ts                   — Entry point, IoT Hub connection
      telemetryForwarder.ts      — Subscribe to bridge WS, forward to IoT Hub
      twinManager.ts             — Sync device twin ↔ bridge settings
      otaHandler.ts              — Receive firmware URL from cloud, trigger bridge OTA
      config.ts                  — Connection string, device ID, intervals
    package.json
    tsconfig.json
```

The agent subscribes to the bridge server's WebSocket feed (same one the UI uses) and selectively forwards events to IoT Hub. It does NOT duplicate the bridge — it's a thin relay layer.

### 3.2 Azure Resources Needed

| Resource | Purpose | Tier |
|----------|---------|------|
| IoT Hub | Device connectivity, twin, D2C/C2D | S1 (or existing) |
| Blob Storage | Firmware images for OTA, log archives | Hot tier |
| Cosmos DB or Azure SQL | Alarm history, activity logs, sensor time-series | Serverless |
| App Service or Static Web App | Fleet management dashboard | Free/B1 |
| Azure Functions (optional) | Message routing, alert triggers, data transforms | Consumption |

### 3.3 Security

- RPi5 ↔ Azure: TLS 1.2+ only, X.509 device cert or SAS with 24h rotation
- No inbound ports on RPi5 from internet — all communication is outbound to IoT Hub
- Firmware images in Blob Storage signed with Ed25519 — Nova verifies before flashing
- Azure AD / Entra ID for dashboard access — no shared passwords
- Device provisioning via DPS (Device Provisioning Service) for zero-touch enrollment

---

## Open Questions

- [ ] Should Constellation devices share the existing AS2 IoT Hub or get a separate one?
- [ ] What's the AS2 mobile app's data schema? Can Constellation reuse it or does it need a new schema?
- [ ] What retention period for sensor time-series data? (affects storage cost)
- [ ] Is there an existing fleet dashboard for AS2, or is the mobile app the only interface?
- [ ] Azure region preference? (latency for Canadian/US grain storage sites)
- [ ] Budget constraints for Azure resources?
