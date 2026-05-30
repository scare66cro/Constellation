# Agristar Bridge Server

WebSocket + REST + protobuf-stream bridge between the SvelteKit UI and
the LP-AM2434 CONTROLLER firmware. Replaces the legacy C-based
`gellertserverd`. Runs as `agristar-bridge.service` on the rpi5.

## Architecture

```
â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”  ws/http :80 (lighttpd) â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”  UART2  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚  Svelte UI   â”‚ â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â–¸ â”‚  Bridge Server   â”‚ â”€â”€â”€â”€â”€â”€â–¸ â”‚  CONTROLLER LP â”‚
â”‚  (panel /    â”‚  /iot/* /proto/*        â”‚  (npx tsx        â”‚ 921600  â”‚  (AM2434)      â”‚
â”‚   browser)   â”‚                         â”‚   src/index.ts)  â”‚         â”‚                â”‚
â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜                         â”‚                  â”‚         â””â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”˜
                                         â”‚  dataCache.ts    â”‚                 â”‚ Modbus TCP :502
                                         â”‚  novaSerialBridgeâ”‚                 â”‚ on 10.47.27.x
                                         â”‚  protoStream.ts  â”‚                 â–¼
                                         â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜         â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
                                                                      â”‚  STORAGE / GDC â”‚
                                                                      â”‚  / TRITON LPs  â”‚
                                                                      â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
```

There is no longer a software ARM simulator, no `serialBridge.ts`, no
`USE_NOVA` switch, no mock mode. The bridge speaks COBS+CRC+protobuf to a
real LP-AM2434 board over UART2 in **both** dev and production.

See [`docs/Simulator-to-Production-Transition.md`](../../docs/Simulator-to-Production-Transition.md)
for the full transport / persistence / build story.

---

## Local development against a real LP board

The bridge is meant to run on the rpi5 next to the CONTROLLER LP
(/dev/ttyAMA0). For local hacking you have two options:

### Option 1 â€” edit on Windows, deploy via scp

The bridge runs `tsx` directly on the rpi5 (no build step), so any
edit to `server/src/*.ts` just needs a file copy:

```powershell
scp F:\Constellation\constellation-ui\server\src\<file>.ts `
    gellert@10.47.27.108:/home/gellert/Gellert/constellation/constellation-ui/server/src/
ssh gellert@10.47.27.108 "sudo systemctl reset-failed agristar-bridge && sudo systemctl restart agristar-bridge"
```

Bridge logs:

```bash
bash F:\Constellation\_get_bridge_log.sh
```

### Option 2 â€” open a tunnel and hit the rpi5 bridge from the dev PC

```powershell
ssh -L 9001:127.0.0.1:9001 gellert@10.47.27.108
# in another window:
curl http://localhost:9001/iot/health
curl http://localhost:9001/iot/orbits
```

`http://localhost:9001/proto/stream` accepts the proto-direct WS
subscription (`{action:'subscribe', tags:[120,124,...]}`).

---

## Environment / runtime

| Variable        | Default              | Notes                                                          |
|-----------------|----------------------|----------------------------------------------------------------|
| `PORT`          | `9001`               | HTTP/WebSocket listen port                                     |
| `SERIAL_PORT`   | `/dev/ttyAMA0`       | UART device (set in the systemd unit on the rpi5)              |
| `SERIAL_BAUD`   | `921600`             | Must match LP firmware UART2 config                            |
| `SVELTE_BUILD`  | _(unset)_            | Optional path for static-serving the SvelteKit build           |

Service unit on the rpi5:

```
WorkingDirectory=/home/gellert/Gellert/constellation/constellation-ui/server
ExecStart=/usr/bin/npx tsx src/index.ts
Restart=on-failure
```

Note the systemd start-rate-limit: rapid restarts can be blocked with
`Start request repeated too quickly`. Always reset first:

```bash
sudo systemctl reset-failed agristar-bridge
sudo systemctl restart agristar-bridge
```

---

## REST surface (`/iot/*`)

The legacy REST endpoints are still served for the SvelteKit pages that
have not been migrated to `/proto/stream`. New pages SHOULD bind to
proto stores and call `writeProto(TAG.X, draft)` instead of POSTing
under `/iot/*` â€” see
[`docs/proto-direct-redesign-plan.md`](../../docs/proto-direct-redesign-plan.md)
and the `agristar-principles` user-memory note.

| Method | Path                       | Description                                      |
|--------|----------------------------|--------------------------------------------------|
| GET    | `/iot/health`              | Bridge + UART health snapshot                    |
| GET    | `/iot/orbits`              | Discovered orbit boards (slot/role/connected)    |
| GET    | `/iot/orbits/sensor-banks` | Latest raw HR-200 banks per slot                 |
| GET    | `/iot/triton/<slot>`       | Triton telemetry / setpoints                     |
| POST   | `/iot/triton/<slot>/...`   | Triton manual / setpoints / safety control       |
| GET    | `/iot/<page>`              | Legacy CSV page data (basic, analog, pid, etc.)  |
| POST   | `/iot/PostSave.jsp`        | Legacy save shim (translates to SettingsUpdate)  |
| POST   | `/iot/button`              | Forward a button press to firmware               |

## Proto-direct surface (`/proto/*`)

| Path                                | Description                                                         |
|-------------------------------------|---------------------------------------------------------------------|
| `WS  /proto/stream`                 | Subscribe to envelope tags; bridge broadcasts raw protobuf frames   |
| `POST /proto/write/:settingsField`  | Send a `SettingsUpdate` payload (UI helper: `writeProto`)           |

Wire format on `/proto/stream`: each frame is
`[u16 LE tag][u32 LE len][payload]`.

## Codebase landmarks

| File                                      | Responsibility                                          |
|-------------------------------------------|---------------------------------------------------------|
| `src/index.ts`                            | Express + WS bootstrap                                  |
| `src/novaSerialBridge.ts`                 | UART transport, COBS+CRC framing, command/Ack queue     |
| `src/novaDataStore.ts`                    | Decodes envelope tags, raw-message cache, `update` event|
| `src/novaAdapter.ts`                      | Bridges proto messages into `dataCache` (CSV-shape)     |
| `src/dataCache.ts`                        | Legacy CGI-name â†’ string store for `/iot/*`             |
| `src/legacyShim.ts`                       | Translates POST forms into `SettingsUpdate` envelopes   |
| `src/protoStream.ts`                      | `/proto/stream` WS server                               |
| `src/apiRoutes.ts`                        | All `/iot/*` REST handlers                              |

See also:

- [`/memories/repo/novaadapter-rpi5-deploy-gap.md`](../../memories/repo/novaadapter-rpi5-deploy-gap.md)
  â€” `deploy.sh` does NOT push `server/src/*.ts`; manual scp is required.
- [`/memories/repo/data-path-rules.md`](../../memories/repo/data-path-rules.md)
  â€” the bridge MUST be a transparent passthrough (no unit conversion,
  no fix-ups, no synthetic sensor values).
- [`docs/firmware-bridge-protocol.md`](../../docs/firmware-bridge-protocol.md)
  â€” full COBS+CRC+protobuf wire-protocol invariants.
