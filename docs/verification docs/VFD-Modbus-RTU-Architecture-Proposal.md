# Modbus RTU VFD Control — AM2612 Architecture Proposal

**Date:** March 21, 2026  
**Context:** Based on findings from VFD speed bouncing investigation and gellertserverd CGI data model analysis  
**Scope:** Moving VFD fan control from RPi5 (current) to ARM controller firmware (proposed)

---

## 1. Current Architecture (RPi5-in-Loop)

```
ARM TM4C129 firmware
    ↓  Serial UART1 (230400 baud)
gellertserverd (C, FastCGI on RPi5)
    ↓  CGI over lighttpd (dirty-flag consumed-on-read)
vfdServer.ts (Node.js on RPi5, port 3002)
    ↓  Modbus TCP over Ethernet
FMBT-21 adapter ($200+) on each drive
    ↓  EFB internal bus
ABB ACS380 VFD → Fan motor
```

### Problems discovered

1. **CGI dirty-flag consumption** — gellertserverd marks data as consumed after each CGI read. With 3 concurrent consumers (browser, iotclient, VFD server), ~50% of responses are missing `MainData` entirely. The VFD server must skip these cycles and hope the next one arrives before the drive's communication timeout.

2. **5-layer failure chain** — A truncated CGI response (1013 bytes, missing `MainData`) was silently interpreted as "fan speed = 0%", sending a STOP command to a running motor every 3–12 seconds. This required a defensive guard (`if (!mainRaw) return`) that masks the underlying fragility.

3. **No control during RPi5 downtime** — If the RPi5 reboots, lighttpd crashes, Node.js hangs, or the SD card corrupts, fan control is lost entirely. Linux boot takes 20–40 seconds; the VFD's FMBT-21 communication timeout is 2 seconds.

4. **Per-drive cost** — Each ACS380 requires an FMBT-21 Modbus TCP adapter (~$200) because the RPi5 communicates over Ethernet. The ACS380 already has a built-in RS485 port with Modbus RTU — the FMBT-21 is only needed to bridge to TCP.

5. **Non-deterministic timing** — JavaScript event loop jitter, HTTP round-trip variability, and CGI cache races make write timing unpredictable (observed 10–200ms variance). The FMBT-21's communication timeout (2.0 seconds) provides margin, but the jitter is unnecessary.

## 2. Proposed Architecture (ARM-Direct RTU)

```
ARM AM2612 firmware (FreeRTOS)
    ↓  UART3 + MAX485 transceiver (Modbus RTU, 19200 baud)
RS485 bus (two wires, daisy-chain, up to 1200m)
    ↓
ACS380 #1 (unit 1) ── ACS380 #2 (unit 2) ── ... more drives
```

The ARM controller talks directly to the VFD drives over Modbus RTU on a dedicated RS485 bus. No RPi5 involvement in the control path.

### Component cost comparison

| Component | Current (per drive) | Proposed (per drive) |
|-----------|-------------------|---------------------|
| FMBT-21 adapter | ~$200 | $0 (not needed) |
| RS485 transceiver | — | ~$1 (one MAX485 total, shared bus) |
| Network switch | — | $0 (not needed) |
| Wiring | Cat5e Ethernet per drive | 2-wire twisted pair, daisy-chain |
| **Total per drive** | **~$200+** | **~$0.50 wire cost** |

For a 4-drive installation: ~$800 saved.

## 3. Hardware Requirements

### AM2612 UART allocation

| UART | Purpose | Baud | Status |
|------|---------|------|--------|
| UART0 | Debug console | 115200 | Existing |
| UART1 | RPi5 bridge (LTX protocol) | 230400 | Existing |
| UART2 | RS485 analog boards (Gellert protocol) | 9600 | Existing |
| **UART3** | **RS485 Modbus RTU (VFD drives)** | **19200** | **New** |

The AM2612 (Sitara family) has 6+ UART peripherals plus PRU-based serial channels — UART3 is readily available.

### New hardware on the controller board

- **1× RS485 transceiver** (MAX485, SN65HVD72, or equivalent, ~$1)
  - Connected to UART3 TX/RX pins
  - Direction control via a spare GPIO (same pattern as existing UART2/RS485 circuit on PC5)
- **1× 2-pin screw terminal** for RS485 A/B bus connection
- **120Ω termination resistor** (jumper-selectable for end-of-bus)

No other hardware changes required. The existing Ethernet, UART0–2, SPI, I2C, and all other peripherals remain unchanged.

### Drive-side wiring

The ACS380 has a built-in RS485 port (terminals 30/31 on the base unit, no adapter needed):

```
AM2612 board                          ACS380 drives
┌──────────┐                    ┌─────────┐    ┌─────────┐    ┌─────────┐
│  UART3   │  RS485 A ─────────│ T30 (A) │────│ T30 (A) │────│ T30 (A) │
│  MAX485  │  RS485 B ─────────│ T31 (B) │────│ T31 (B) │────│ T31 (B) │
└──────────┘                    └─────────┘    └─────────┘    └─────────┘
                                 Unit ID=1      Unit ID=2      Unit ID=3
                                                               [120Ω term]
```

Drive configuration (set via ACS380 keypad or startup commissioning):
- Parameter 51.21 (Fieldbus comm protocol) = Modbus RTU
- Parameter 51.22 (Station address) = 1, 2, 3, etc.
- Parameter 51.23 (Baud rate) = 19200
- Parameter 51.24 (Parity) = Even (standard Modbus default)

## 4. Firmware Implementation

### Modbus RTU master — estimated scope

The entire Modbus RTU master implementation is ~200 lines of C:

**Frame format (request):**
```
[Unit ID] [Function Code] [Register Addr] [Register Count/Value] [CRC16]
 1 byte      1 byte          2 bytes           2+ bytes          2 bytes
```

**Core functions needed:**

```c
// Build a Modbus RTU request frame
uint16_t mb_build_request(uint8_t *buf, uint8_t unit_id, uint8_t func,
                          uint16_t reg_addr, uint16_t reg_count);

// Validate a Modbus RTU response (check CRC, unit ID, function code)
int mb_validate_response(const uint8_t *buf, uint16_t len, uint8_t unit_id);

// CRC16 calculation (standard Modbus polynomial 0xA001)
uint16_t mb_crc16(const uint8_t *data, uint16_t len);

// Send request and wait for response (with timeout)
int mb_transaction(uint8_t unit_id, uint8_t func, uint16_t addr,
                   uint16_t count, uint16_t *result);
```

**FreeRTOS task:**

```c
void vModbusRTUTask(void *pvParameters) {
    // Init UART3 at 19200, 8E1
    // Init RS485 direction GPIO

    for (;;) {
        for (each discovered drive) {
            // 1. Write CW + speed ref (FC16, registers 0x0001-0x0002)
            mb_transaction(unit_id, 0x10, 0x0001, 2, cw_and_ref);

            // 2. Read status registers (FC03)
            mb_transaction(unit_id, 0x03, status_addr, count, &status_buf);

            // 3. Update internal drive state (speed, current, faults)
            update_drive_snapshot(unit_id, &status_buf);
        }

        vTaskDelay(pdMS_TO_TICKS(200));  // 5 Hz control loop
    }
}
```

### Integration with existing firmware

The fan speed decision already happens inside the ARM firmware — it calculates the percentage based on temperature setpoints, PID loops, and mode logic. Currently, that percentage is sent over UART1 to gellertserverd as a serial tag (`^FAN=75$CRC!`), which the CGI exposes as `MainData[10]`, which our VFD server scrapes and forwards to Modbus TCP.

With RTU on the ARM, the firmware simply takes its internal `fanSpeedPercent` variable and writes it directly:

```c
uint16_t cw = (fanSpeedPercent > 0) ? 0x047F : 0x040E;  // START or STOP
uint16_t ref = fanSpeedPercent * 100;                     // 0–10000 scale
uint16_t regs[2] = { cw, ref };
mb_transaction(drive_unit_id, 0x10, 0x0001, 2, regs);
```

No serial protocol. No CGI. No HTTP. No JavaScript. One function call.

### Drive discovery

On startup, the firmware scans unit IDs 1–8 by reading the ACS380's device identification register (FC03, register 0x0064). Drives that respond are added to the poll list. This mirrors the scan logic already in `vfdClient.ts` but runs in ~10 lines of C.

## 5. What the RPi5 Does Instead

The RPi5 remains the display, logging, and cloud connectivity layer:

| Function | How |
|----------|-----|
| **VFD status display** | ARM sends drive state (speed, current, faults) over UART1 using existing serial tag protocol. The UI reads it like any other sensor. |
| **VFD configuration** | User changes drive parameters via the UI → POST to bridge → serial tag to ARM → ARM writes RTU register |
| **Fault alerts** | ARM detects drive faults immediately (200ms poll cycle) → sends alarm tag over UART1 → gellertserverd raises alarm |
| **Cloud telemetry** | iotclient reads VFD data from CGI like any other variable |
| **History logging** | GellertFileSystem.out logs VFD data points alongside temperature/humidity |

The RPi5 never talks directly to the VFD drives. It gets all VFD information through the ARM, same as it gets temperature, humidity, and mode data today.

## 6. Reliability Comparison

| Failure scenario | Current (RPi5-in-loop) | Proposed (ARM-direct RTU) |
|-----------------|----------------------|--------------------------|
| RPi5 reboots | Fan control lost for 20–40s | No impact — ARM keeps writing CW+ref |
| SD card corrupts | Fan control lost permanently until service | No impact |
| lighttpd crashes | CGI unavailable → last speed held (with our fix) | No impact |
| Node.js hangs | Modbus writes stop → drive comm timeout (2s) | No impact |
| CGI returns truncated | Must skip cycle and hope next one works | N/A — no CGI in control path |
| ARM watchdog reset | All control lost for <1s, auto-recovers | All control lost for <1s, auto-recovers |
| RS485 wire break | N/A (using Ethernet) | Drive enters fault mode per its timeout setting |
| Ethernet cable disconnected | Modbus TCP connection lost → drive timeout | N/A (not using Ethernet for drives) |

## 7. Migration Path

### Phase 1: Hardware (board revision)
- Add MAX485 transceiver and screw terminal to AM2612 board
- Route UART3 pins to transceiver
- Add direction control GPIO

### Phase 2: Firmware
- Implement Modbus RTU master module (~200 lines of C)
- Add FreeRTOS task for drive polling (clone pattern from RS485.c)
- Add serial tags for VFD status reporting to RPi5 (e.g. `^VFD1_SPD=1480$`, `^VFD1_CUR=3.2$`, `^VFD1_FLT=0$`)
- Add serial command handler for VFD configuration from UI

### Phase 3: RPi5 software
- Remove `vfdServer.ts` and `vfdClient.ts` from production deployment
- Add VFD serial tags to bridge server's tag parser
- Update UI to read VFD data from standard bridge WebSocket (no more `/vfd/*` proxy)
- Remove FMBT-21 and Ethernet cabling to drives

### Phase 4: Decommission
- Remove `/vfd` proxy block from lighttpd.conf
- Remove `agristar-vfd.service` from systemd
- Remove FMBT-21 adapters from drives
- Reconfigure ACS380 drives from Modbus TCP to Modbus RTU (parameter 51.21)

## 8. Summary

| Aspect | Current | Proposed |
|--------|---------|----------|
| **Control path** | ARM → serial → CGI → Node.js → Modbus TCP → FMBT-21 → drive | ARM → RS485 → drive |
| **Latency** | 200ms–3s (CGI poll + HTTP round-trip) | <50ms (direct UART) |
| **Reliability** | Depends on RPi5 + 5 software layers | Depends on ARM only |
| **Cost per drive** | ~$200 (FMBT-21) | ~$0.50 (wire) |
| **New firmware code** | 0 | ~200 lines C |
| **New hardware** | 0 | 1× MAX485 + GPIO + terminal |
| **Wiring** | Cat5e per drive | 2-wire daisy-chain |
| **Drive config** | Modbus TCP (FMBT-21 Standard profile) | Modbus RTU (ACS380 native) |
| **Industrial suitability** | Consumer SBC in control loop | Dedicated MCU, deterministic timing |
