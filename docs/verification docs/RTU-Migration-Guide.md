# Modbus RTU Migration Guide — From RPi5-in-Loop to ARM-Direct Control

**Date:** March 21, 2026  
**Prerequisite reading:** [VFD-Modbus-RTU-Architecture-Proposal.md](VFD-Modbus-RTU-Architecture-Proposal.md)  
**Audience:** Hardware engineers, firmware developers, and field technicians performing the migration

---

## 1. What Is Modbus RTU?

Modbus RTU (Remote Terminal Unit) is a serial communication protocol used in industrial automation since 1979. It transmits binary data over RS485 wiring — two twisted wires carrying differential signals, noise-immune over distances up to 1200 meters.

Every device on the bus has a unique **unit ID** (1–247). A single **master** (our ARM controller) sends requests; **slaves** (VFD drives, relay modules, sensors) respond. Only one conversation happens at a time — the master asks, one slave answers.

**Frame format:**
```
┌──────────┬───────────┬──────────────────┬───────────┐
│ Unit ID  │ Function  │     Data         │  CRC16    │
│ (1 byte) │ (1 byte)  │  (variable)      │ (2 bytes) │
└──────────┴───────────┴──────────────────┴───────────┘
```

Common function codes:
| Code | Name               | Purpose                          |
|------|--------------------|----------------------------------|
| 0x01 | Read Coils         | Read on/off relay outputs        |
| 0x02 | Read Discrete Inputs | Read on/off sensor inputs     |
| 0x03 | Read Holding Registers | Read 16-bit data values     |
| 0x05 | Write Single Coil  | Turn one relay on or off         |
| 0x06 | Write Single Register | Write one 16-bit value        |
| 0x0F | Write Multiple Coils | Set multiple relays at once    |
| 0x10 | Write Multiple Registers | Write multiple 16-bit values |

Frames are separated by a silent gap of at least 3.5 character times (~2ms at 19200 baud). There is no connection setup, no handshake, no session. Just request → response → done.

---

## 2. What Are RTU Relay Modules?

An **RTU relay module** is a small industrial board that mounts on a DIN rail (or panel) and contains one or more electromechanical or solid-state relays controllable over Modbus RTU. Each relay is mapped to a **coil address** in the Modbus register map, so the master can open or close any relay with a single Modbus command.

### How they work

```
ARM AM2612                      RTU Relay Module (e.g. 8-channel)
┌──────────┐   RS485 bus       ┌─────────────────────────────┐
│  UART3   │ ───── A/B ─────→ │  Unit ID = 10               │
│  MAX485  │                   │                             │
└──────────┘                   │  Coil 0 ──→ Relay 1 (NO/NC)│──→ Contactor / Motor / Heater
                               │  Coil 1 ──→ Relay 2 (NO/NC)│──→ Solenoid Valve
                               │  Coil 2 ──→ Relay 3 (NO/NC)│──→ Alarm Light
                               │  Coil 3 ──→ Relay 4        │──→ ...
                               │  ...                        │
                               │  Discrete Input 0 ← DI1    │←── Door Switch
                               │  Discrete Input 1 ← DI2    │←── Float Level Sensor
                               └─────────────────────────────┘
```

To turn on relay 1 on unit ID 10, the ARM sends:

```
TX: [0x0A] [0x05] [0x00 0x00] [0xFF 0x00] [CRC CRC]
     unit   Write   Coil #0     ON value
     ID=10  Single
            Coil

RX: [0x0A] [0x05] [0x00 0x00] [0xFF 0x00] [CRC CRC]   ← echo confirms success
```

To turn it off, replace `0xFF 0x00` with `0x00 0x00`.

To read back the state of all 8 relays:

```
TX: [0x0A] [0x01] [0x00 0x00] [0x00 0x08] [CRC CRC]
     unit   Read    Start       Count=8
     ID=10  Coils   Coil #0

RX: [0x0A] [0x01] [0x01] [0b_00000011] [CRC CRC]
     unit   Read   1 byte  Relays 1&2 ON, 3-8 OFF
     ID=10  Coils  of data
```

### What they replace

In standard wiring practice, each relay or contactor is controlled by a **dedicated output wire** from a PLC or controller board. This requires:
- One GPIO pin per relay on the controller
- One wire run per relay from the controller enclosure to the field device
- One terminal strip position per wire at each end

With RTU relay modules, all of that collapses to **two shared wires** (RS485 A and B) regardless of how many relays or where they are located:

| Aspect | Traditional Wired Relay | RTU Relay Module |
|--------|------------------------|------------------|
| Wiring per relay | 1 dedicated wire | Shared 2-wire RS485 bus |
| Controller GPIO consumed | 1 per relay | 0 (uses UART) |
| Max distance | ~10m (voltage drop) | 1200m (RS485 differential) |
| Expandability | Requires board redesign | Add another module to the bus |
| Feedback/diagnostics | None (open-loop) | Read-back of relay state, input pins, voltage, temperature |
| Cost per channel | ~$2-5 (discrete relay + driver + terminal) | ~$3-5 (amortized module cost) |

### Common RTU relay module options

| Module | Channels | Rating | DIN Rail | Price | Notes |
|--------|----------|--------|----------|-------|-------|
| Waveshare Modbus RTU Relay | 4 or 8 | 10A 250VAC per channel | Yes | $20–35 | Widely available, screw terminals, unit ID set via DIP switches |
| HF HF46F series | 1, 2, 4, 8 | 10A–16A | Yes | $15–30 | Chinese industrial standard, very common |
| Novatek-Electro EM-483 | 4 | 16A 250VAC | Yes | $40–60 | European industrial grade, wider temp range |
| Waveshare 8-ch with optocouplers | 8 | 10A 250VAC | No | $25–30 | Panel mount, optoisolated inputs |

All of these speak standard Modbus RTU and appear on the bus alongside VFD drives without conflict — they just use different unit IDs.

### What RTU relays control in our system

In the Agristar greenhouse environment, RTU relays replace the hardwired contactor control currently handled by dedicated GPIO or analog board outputs:

| Relay Function | What It Switches | Why RTU |
|----------------|-----------------|---------|
| **Fan motor contactor** | Main power contactor enabling/disabling the fan motor | ARM controls fan startup sequence: contactor ON → wait 500ms → VFD START command |
| **Heater contactor** | Heating element power | On/off duty cycle based on temperature setpoint |
| **Vent actuator** | Motorized roof/side vent open/close | Open for cooling, close for heating or storm |
| **Irrigation solenoid** | Water valve for drip or overhead irrigation | Timed zones based on schedule or soil moisture |
| **Lighting contactor** | Supplemental grow-light circuit | Photoperiod control or DLI-based lighting |
| **Alarm output** | Siren, strobe, or beacon | Tripped on high-temperature, power failure, frost |
| **Backup power transfer** | Generator ATS or UPS relay | Switch to backup on mains failure |

Each of these is currently either a GPIO on the analog board or a separate hardwired relay. With RTU modules, they all share the same two-wire bus as the VFD drives and respond to firmware commands with sub-10ms latency.

---

## 3. The Migration — Step by Step

### 3.1 Current System Overview

```
                        ┌────────────────────────────────────┐
                        │           RPi5 (Ubuntu 22)         │
                        │                                    │
Fan Speed  ←──CGI──←──  │  gellertserverd ←──UART1──← ARM   │
Decision                │       ↓ CGI                        │
                        │  vfdServer.ts (Node.js, port 3002) │
                        │       ↓ Modbus TCP                 │
                        └───────┼────────────────────────────┘
                                ↓ Ethernet
                        ┌───────┴────────┐
                        │ Network Switch │
                        ├────────────────┤
                        │  FMBT-21 #1   │──→ ACS380 #1
                        │  FMBT-21 #2   │──→ ACS380 #2
                        │  FMBT-21 #3   │──→ ACS380 #3
                        └────────────────┘

Contactors/Relays: Hardwired from analog board GPIO
Sensors: Analog board → RS485 UART2 → ARM
```

**Data path for a fan speed change:**
1. ARM firmware calculates fan speed = 75% based on temperature
2. ARM sends `^FAN=75$CRC!` over UART1 serial to RPi5
3. gellertserverd stores value in shared memory, marks dirty flag
4. VFD server polls `GET /get/data?page=main` every 3 seconds
5. If not consumed by another client, CGI returns `MainData` with index [10] = 75
6. VFD server parses response, converts 75% → Modbus ref 7500
7. VFD server sends Modbus TCP write to FMBT-21 at 10.1.2.157:502
8. FMBT-21 converts TCP frame to internal EFB command
9. ACS380 applies new speed reference

**Failure points:** Steps 3–8 (six layers, any can fail independently).

### 3.2 Target System Overview

```
                           ┌──────────────────────────┐
                           │     ARM AM2612           │
                           │     (FreeRTOS)           │
                           │                          │
                           │  UART1 ←→ RPi5 (HMI)    │
                           │  UART2 ←→ Analog boards  │
                           │  UART3 ←→ RS485 bus      │
                           └────────┼─────────────────┘
                                    │ RS485 (2-wire)
            ┌───────────────────────┼───────────────────────┐
            │                       │                       │
     ┌──────┴──────┐        ┌──────┴──────┐        ┌──────┴──────┐
     │ ACS380 #1   │        │ ACS380 #2   │        │ RTU Relay   │
     │ Unit ID = 1 │        │ Unit ID = 2 │        │ Unit ID = 10│
     │ (VFD drive) │        │ (VFD drive) │        │ (8-channel) │
     └─────────────┘        └─────────────┘        └─────────────┘
                                                     │ Contacts:
                                                     ├─ Heater contactor
                                                     ├─ Vent actuator
                                                     ├─ Irrigation valve
                                                     ├─ Alarm beacon
                                                     └─ ...

RPi5: Pure HMI / logging / cloud — no longer in the control path
```

**Data path for a fan speed change (new):**
1. ARM firmware calculates fan speed = 75%
2. ARM writes Modbus RTU FC16 to ACS380 unit 1 over UART3
3. ACS380 applies new speed reference

**That's it.** Three steps. One firmware function call. No RPi5 involvement.

### 3.3 Phase 1 — Board Hardware Modifications

**Duration:** One board revision cycle  
**Risk:** Low — additive change, no existing circuits modified

#### What to add to the AM2612 controller board

1. **RS485 transceiver IC** — MAX485 or SN65HVD72 (3.3V compatible for AM2612)
   - VCC → 3.3V rail
   - GND → ground plane
   - DI (driver input) → AM2612 UART3 TX pin
   - RO (receiver output) → AM2612 UART3 RX pin
   - DE (driver enable) → GPIO pin (e.g. PA3 or any spare)
   - RE̅ (receiver enable) → tied to DE (half-duplex: when DE=high, transmitting; DE=low, receiving)
   - A, B → screw terminal on board edge

2. **Screw terminal** — 2-position, 3.5mm or 5mm pitch, labeled "RS485 A" / "RS485 B"

3. **Termination resistor** — 120Ω between A and B, behind a solder jumper or 2-pin header
   - Close the jumper only if this board is at the physical end of the RS485 bus
   - Most installations: ARM board at one end, last drive at the other → both ends terminated

4. **Bias resistors** (optional, recommended) — 560Ω pull-up on A, 560Ω pull-down on B
   - Prevents the bus from floating when no device is transmitting
   - Ensures a known idle state (logic "1")

#### Schematic snippet

```
                    AM2612
                 ┌──────────┐
   UART3_TX ─── │ PA1       │
   UART3_RX ─── │ PA0       │         MAX485
   GPIO     ─── │ PA3       │      ┌──────────┐
                 └──────────┘      │          │
                                   │ DI ← TX  │
     3.3V ────────────────── VCC ──│ VCC      │
     GND  ────────────────── GND ──│ GND      │
                                   │ RO → RX  │
                    GPIO PA3 ──────│ DE       │── A ──┬── [120Ω] ──┬── Screw Terminal A
                    GPIO PA3 ──────│ RE̅       │── B ──┴────────────┴── Screw Terminal B
                                   └──────────┘
                                   (DE and RE̅ tied together for half-duplex)
```

#### Bill of materials (per board)

| Part | Qty | Unit Cost | Total |
|------|-----|-----------|-------|
| MAX485ESA (SOIC-8) | 1 | $0.50 | $0.50 |
| 120Ω 0805 resistor | 1 | $0.01 | $0.01 |
| 560Ω 0805 resistors (bias) | 2 | $0.01 | $0.02 |
| 2-pos screw terminal 3.5mm | 1 | $0.30 | $0.30 |
| 2-pin header (termination jumper) | 1 | $0.05 | $0.05 |
| **Total** | | | **$0.88** |

### 3.4 Phase 2 — Firmware: Modbus RTU Master

**Duration:** ~1 week firmware development + 1 week testing  
**Risk:** Low — new task, does not modify any existing UART0/1/2 code

#### File structure (new files within existing Mini_IO project)

```
Mini_IO/
  Application/
    ModbusRTU/
      modbus_rtu.h        — Public API (mb_transaction, mb_crc16, etc.)
      modbus_rtu.c        — Frame building, CRC, UART3 I/O, response parsing
      modbus_rtu_task.c   — FreeRTOS task: poll loop for all RTU devices
      modbus_rtu_config.h — Unit IDs, register maps, poll intervals
```

#### UART3 initialization

Clone the existing UART2 initialization from [RS485.c](../Mini_IO/Application/RS485.c) and change the peripheral base, pins, and baud rate:

```c
void ModbusRTU_Init(void) {
    // Enable UART3 peripheral clock
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART3);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);  // UART3 pins

    // Configure UART3 pins (PA0=RX, PA1=TX on TM4C129 — check pin mux)
    GPIOPinConfigure(GPIO_PA0_U3RX);
    GPIOPinConfigure(GPIO_PA1_U3TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    // Configure UART3: 19200 baud, 8 data bits, even parity, 1 stop bit
    UARTConfigSetExpClk(UART3_BASE, g_ui32SysClock, 19200,
        UART_CONFIG_WLEN_8 | UART_CONFIG_PAR_EVEN | UART_CONFIG_STOP_ONE);

    // Configure RS485 direction GPIO
    GPIOPinTypeGPIOOutput(GPIO_PORTA_BASE, GPIO_PIN_3);  // DE/RE̅
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_3, 0);        // Default: receive mode
}
```

#### CRC16 calculation

Standard Modbus CRC16 — identical across all implementations:

```c
uint16_t mb_crc16(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}
```

#### Transaction function

```c
// Send a Modbus RTU request and wait for the response.
// Returns 0 on success, -1 on timeout, -2 on CRC error.
int mb_transaction(uint8_t unit_id, uint8_t func, uint16_t addr,
                   uint16_t count_or_value, uint16_t *result_buf) {
    uint8_t tx[256], rx[256];
    uint16_t tx_len;

    // Build request frame
    tx_len = mb_build_request(tx, unit_id, func, addr, count_or_value);

    // Switch to transmit mode
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_3, GPIO_PIN_3);  // DE=HIGH

    // Send frame
    for (uint16_t i = 0; i < tx_len; i++) {
        UARTCharPut(UART3_BASE, tx[i]);
    }

    // Wait for TX complete, then switch to receive mode
    while (UARTBusy(UART3_BASE)) {}
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_3, 0);  // DE=LOW

    // Wait for response with timeout
    uint16_t rx_len = mb_receive_response(rx, sizeof(rx), RTU_TIMEOUT_MS);
    if (rx_len == 0) return -1;  // timeout

    // Validate CRC
    uint16_t crc = mb_crc16(rx, rx_len - 2);
    uint16_t rx_crc = rx[rx_len-2] | (rx[rx_len-1] << 8);
    if (crc != rx_crc) return -2;  // CRC mismatch

    // Extract data into result buffer
    mb_parse_response(rx, rx_len, func, result_buf);
    return 0;
}
```

#### FreeRTOS poll task

```c
// Device table — populated at startup via discovery scan
static struct {
    uint8_t  unit_id;
    uint8_t  type;       // DEVICE_VFD, DEVICE_RELAY, DEVICE_ANALOG_IO
    uint16_t status[16]; // Last-read registers
    uint32_t errors;     // Consecutive error count
} g_rtu_devices[MAX_RTU_DEVICES];

void vModbusRTUTask(void *pvParameters) {
    ModbusRTU_Init();
    modbus_discover_devices();  // Scan unit IDs 1–32

    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        for (int i = 0; i < g_device_count; i++) {
            switch (g_rtu_devices[i].type) {

            case DEVICE_VFD:
                // Write control word + speed reference
                rtu_vfd_write_control(g_rtu_devices[i].unit_id);
                // Read status (speed, current, fault word)
                rtu_vfd_read_status(g_rtu_devices[i].unit_id,
                                    g_rtu_devices[i].status);
                break;

            case DEVICE_RELAY:
                // Write coil states from internal control logic
                rtu_relay_write_coils(g_rtu_devices[i].unit_id);
                // Read back actual coil states + discrete inputs
                rtu_relay_read_state(g_rtu_devices[i].unit_id,
                                     g_rtu_devices[i].status);
                break;

            case DEVICE_ANALOG_IO:
                // Read analog input registers (temperature, humidity, etc.)
                rtu_analog_read(g_rtu_devices[i].unit_id,
                                g_rtu_devices[i].status);
                break;
            }
        }

        // 5 Hz control loop — every 200ms
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(200));
    }
}
```

#### Safety interlocks (firmware, not RPi5)

These run inside the FreeRTOS task and cannot be affected by RPi5 state:

```c
// Called every 200ms inside the poll loop
void safety_check(void) {
    // Over-temperature emergency stop
    if (g_temperature > TEMP_EMERGENCY_LIMIT) {
        for (int i = 0; i < g_device_count; i++) {
            if (g_rtu_devices[i].type == DEVICE_VFD)
                rtu_vfd_emergency_stop(g_rtu_devices[i].unit_id);
        }
        rtu_relay_set_coil(RELAY_UNIT_ID, COIL_ALARM, true);
    }

    // Communication loss — if a drive hasn't responded in 5 cycles
    for (int i = 0; i < g_device_count; i++) {
        if (g_rtu_devices[i].errors > 5) {
            flag_device_fault(g_rtu_devices[i].unit_id);
            // Report over UART1 to RPi5 for logging/alerting
            send_serial_tag("VFD%d_COMFAULT=1", g_rtu_devices[i].unit_id);
        }
    }
}
```

### 3.5 Phase 3 — RPi5 Software Changes

**Duration:** ~3 days  
**Risk:** Low — removing code, not adding complexity

#### 3.5.1 New serial tags (ARM → RPi5 over UART1)

The ARM firmware exposes VFD and relay state as standard serial tags, making them appear in gellertserverd's CGI output alongside temperature/humidity/mode data:

| Tag | Example Value | Description |
|-----|--------------|-------------|
| `VFD1_SPD` | `1480` | Drive 1 actual speed in RPM |
| `VFD1_CUR` | `3.2` | Drive 1 current in amps |
| `VFD1_FLT` | `0` | Drive 1 fault code (0 = none) |
| `VFD1_RUN` | `1` | Drive 1 running (1) or stopped (0) |
| `VFD2_SPD` | `0` | Drive 2 actual speed |
| `RLY_STATE` | `0x03` | Relay module coil states (bitmask) |
| `RLY_DI` | `0x01` | Relay module discrete input states |

These tags flow through the existing serial → gellertserverd → CGI pipeline. The browser, iotclient, and cloud all see VFD/relay data without any VFD-specific code.

#### 3.5.2 Remove VFD server components

1. Stop and disable the VFD service:
   ```bash
   systemctl stop agristar-vfd
   systemctl disable agristar-vfd
   ```

2. Remove VFD proxy from lighttpd config:
   ```
   # DELETE this block from lighttpd.conf:
   # $HTTP["url"] =~ "^/vfd/" {
   #     proxy.server = ( "" => (( "host" => "127.0.0.1", "port" => 3002 )))
   # }
   ```

3. Remove VFD server files from deployment:
   ```
   /home/gellert/Gellert/vfd-server/vfdServer.js   ← delete
   /home/gellert/Gellert/vfd-server/vfdClient.js    ← delete
   /etc/systemd/system/agristar-vfd.service          ← delete
   ```

#### 3.5.3 Update bridge server tag parser

Add VFD tag names to the bridge server's serial tag list so they are forwarded over WebSocket to the UI:

```typescript
// In bridge server tag parser, add:
case 'VFD1_SPD':
case 'VFD1_CUR':
case 'VFD1_FLT':
case 'VFD1_RUN':
case 'VFD2_SPD':
case 'RLY_STATE':
case 'RLY_DI':
    this.state[tag] = value;
    this.broadcastUpdate(tag, value);
    break;
```

### 3.6 Phase 4 — Field Wiring and Drive Reconfiguration

**Duration:** ~2 hours per installation (drives + relay module)  
**Risk:** Medium — involves power equipment, follow lockout/tagout procedures

#### 3.6.1 RS485 bus wiring

Use shielded twisted pair cable (e.g. Belden 9841 or equivalent, 24 AWG):

```
AM2612 board                                                     Last device on bus
┌─────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│ TERM A ─┼──A──┤ ACS380#1 ├──A──┤ ACS380#2 ├──A──┤ RTU Relay├──A──┤ ACS380#3 │
│ TERM B ─┼──B──┤ T30/T31  ├──B──┤ T30/T31  ├──B──┤ A/B term ├──B──┤ T30/T31  │
│ [120Ω]  │     └──────────┘     └──────────┘     └──────────┘     │ [120Ω]   │
└─────────┘                                                        └──────────┘
   ↑ terminated                                                       ↑ terminated
```

**Wiring rules:**
- **Daisy-chain topology only** — no star wiring, no stubs longer than 30cm
- **Termination** at both ends of the bus: 120Ω at the ARM board, 120Ω at the last device
- **Shield grounded at one end only** (ARM board end) to prevent ground loops
- **Maximum 32 devices** on one bus segment (standard RS485 driver capability)
- **Maximum 1200m** total bus length at 19200 baud
- Keep RS485 wiring away from power cables (at least 20cm separation or use separate conduit)

#### 3.6.2 ACS380 drive reconfiguration

For each drive, access the parameter menu via the keypad or ABB DriveComposer:

| Parameter | Name | Old Value | New Value | Description |
|-----------|------|-----------|-----------|-------------|
| 51.01 | Comm module type | EFB (FMBT-21) | Standard (built-in RS485) | Switch from TCP adapter to native RS485 |
| 51.21 | Protocol | Modbus TCP | Modbus RTU | Communication protocol selection |
| 51.22 | Station address | N/A | 1 (or 2, 3...) | Unique unit ID on the RS485 bus |
| 51.23 | Baud rate | N/A | 19200 | Must match ARM UART3 config |
| 51.24 | Parity | N/A | Even | Standard Modbus default (8E1) |
| 51.25 | Comm timeout | 2.0s | 2.0s | Time to fault if no valid message received |
| 51.26 | Timeout action | Fault | Last speed / Fault | What to do on communication loss (site-specific) |

After parameter changes, power-cycle the drive.

#### 3.6.3 RTU relay module setup

1. Mount relay module on DIN rail in the control enclosure
2. Power the module (typically 12V or 24V DC — check module specs)
3. Set the unit ID via DIP switches on the module (e.g. ID = 10)
4. Connect RS485 A and B to the daisy-chain bus
5. Wire relay NO/NC contacts to the load contactors:

```
RTU Relay Module              Field Wiring
┌──────────────┐
│  Relay 1 COM ├──── L1 (from breaker)
│  Relay 1 NO  ├──── Heater contactor coil A1
│              │     Contactor coil A2 ──── N
│              │
│  Relay 2 COM ├──── L1 (from breaker)
│  Relay 2 NO  ├──── Vent actuator open
│              │
│  Relay 3 COM ├──── 24VDC+
│  Relay 3 NO  ├──── Irrigation solenoid +
│              │     Solenoid – ──── 24VDC GND
│              │
│  DI1         ├──── Door limit switch (dry contact)
│  DI2         ├──── High-temp cutout switch
└──────────────┘
```

**Important:** For loads exceeding the relay module's contact rating (typically 10A), use the relay module to switch a larger contactor — do not connect heavy loads directly to the module contacts.

### 3.7 Phase 5 — Verification and Commissioning

#### Pre-power checklist

- [ ] RS485 bus wiring: A-to-A, B-to-B continuity confirmed
- [ ] Termination resistors installed at both bus ends (120Ω measured)
- [ ] No A-B cross-wiring (differential polarity correct)
- [ ] Drive unit IDs set and unique (no duplicates)
- [ ] Relay module unit ID set and not conflicting with drives
- [ ] All relay module contact wiring verified against wiring diagram
- [ ] ARM firmware flashed with Modbus RTU task enabled

#### Startup sequence

1. **Power ARM board only** (drives and relays unpowered)
   - Verify UART3 initialization in debug console (UART0)
   - Confirm discovery scan runs and reports "0 devices found" (expected)

2. **Power one VFD drive**
   - ARM should discover unit ID 1 within 2 seconds
   - Verify status read: speed=0, current=0, fault=0, running=0
   - Send a 10% speed command from the firmware and confirm drive spins up
   - Send STOP and confirm drive ramps down

3. **Power relay module**
   - ARM should discover relay module at its unit ID
   - Toggle each coil individually and verify relay click / field device activation
   - Read back discrete inputs: verify door switch, float sensor, etc.

4. **Power remaining drives**
   - Confirm all drives discovered and reporting status
   - Run a full operational cycle (heat → cool → vent) and verify all outputs respond

5. **Verify RPi5 display**
   - Confirm `VFD1_SPD`, `VFD1_CUR`, `RLY_STATE` etc. appear in CGI and UI
   - Confirm iotclient receives VFD data for cloud logging
   - Confirm alarm tags trigger notifications

#### Soak test protocol

- Run system for 24 hours under normal operational conditions
- Monitor via RPi5 logs: verify no `VFD_COMFAULT` tags
- Confirm drive speed tracking matches setpoint (±1%)
- Verify relay state readback matches commanded state at all times
- Check ARM debug console for any UART3 CRC errors or timeouts

---

## 4. Bus Addressing Plan

Reserve address ranges by device type to keep the installation organized:

| Address Range | Device Type | Example |
|---------------|-------------|---------|
| 1–8 | VFD drives | ACS380 fan #1 = 1, fan #2 = 2 |
| 10–19 | Relay modules | 8-ch relay module = 10, expansion = 11 |
| 20–29 | Analog I/O modules | 4–20mA input module = 20 |
| 30–39 | Sensors with RTU | RTU temperature/humidity probe = 30 |
| 40–49 | Reserved for future | — |

This leaves room for expansion without renumbering existing devices.

---

## 5. Rollback Plan

If the RTU migration encounters issues at any phase, revert to the current system:

1. **Reconnect FMBT-21 adapters** to drives and re-run Ethernet cables
2. **Reconfigure drives** back to Modbus TCP (parameter 51.21)
3. **Re-enable VFD server**: `systemctl enable agristar-vfd && systemctl start agristar-vfd`
4. **Restore lighttpd proxy block** for `/vfd/`

The RPi5-based VFD server code remains in source control and can be redeployed at any time. The ARM firmware RTU task can be disabled via a build flag (`#define ENABLE_MODBUS_RTU 0`) without affecting any other firmware functionality.

---

## 6. Summary — Before and After

| | Before (Current) | After (Migration Complete) |
|-|-------------------|---------------------------|
| **Fan speed control path** | ARM → UART1 → gellertserverd → CGI → vfdServer.ts → Modbus TCP → FMBT-21 → drive | ARM → UART3 → RS485 → drive |
| **Relay/contactor control** | Analog board GPIO → hardwired | ARM → UART3 → RS485 → RTU relay module → contactor |
| **Layers in control path** | 6 software + 2 hardware | 1 firmware + 1 hardware |
| **Control path uptime** | Requires RPi5 Linux + lighttpd + Node.js + network | Requires ARM only |
| **Response to RPi5 failure** | Control lost | Control unaffected |
| **Cost per VFD drive** | ~$200 (FMBT-21) | ~$0.50 (wire) |
| **Relay expandability** | Board redesign required | Add module to bus |
| **Maximum bus length** | 100m (Ethernet) | 1200m (RS485) |
| **Worst-case control latency** | 3 seconds (CGI poll interval) | 200ms (firmware poll loop) |
| **Diagnostic feedback** | HTTP poll, subject to CGI contention | Register read, deterministic |
