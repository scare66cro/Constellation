# Factory defaults review — May 2026

Interactive walkthrough of every Level 1 / Level 2 page's persisted
defaults. Captures the user's "what should the factory state be"
decisions before applying them in
[`Nova_Firmware/lp_am2434/lp_settings.c::LpSettings_DataInit()`](../Nova_Firmware/lp_am2434/lp_settings.c).

Format: `field = current → decision`.

## Pass 1 — Level 1

### Q1. plenum (level1/plentemp top section)
- plenum.temp_setpoint        = 46 °F → **keep**
- plenum.humid_setpoint       = 95 %  → **keep**
- plenum.humid_setpoint_ref   = 0     → **keep**
- plenum.burner_temp_setpoint = 75 °F → **keep**
- plenum.burner_threshold     = 50 °F → **keep**

### Q2. temp_alarm (level1/plentemp deviations)
- temp_alarm.low_temp   = 5 °F   → **keep**
- temp_alarm.low_timer  = 10 min → **keep**
- temp_alarm.high_temp  = 5 °F   → **keep**
- temp_alarm.high_timer = 10 min → **keep**

### Q3. ramp_rate (level1/plentemp ramp section)
- ramp_rate.rate_per_day  = 1 °F/day → **keep**
- ramp_rate.update_period = 2        → **keep**
- ramp_rate.temp_diff     = 1 °F     → **keep**
- ramp_rate.target_temp   = 46 °F    → **keep**

### Q4. fan_speed (level1/fanspeed)
- fan_speed.max_speed     = 100 %   → **keep**
- fan_speed.min_speed     = 25 %    → **keep**
- fan_speed.refrig_speed  = 75 %    → **keep**
- fan_speed.recirc_speed  = 50 %    → **keep**
- fan_speed.update_period = 5 hours → **keep**
- fan_speed.temp_diff     = 1 °F    → **keep**
- fan_speed.temp_ref2     = 255 (return-air sentinel) → **keep**
- fan_speed.prev_speed    = 25 %    → **keep**
- fan_speed.update_mode   = 0       → **keep**
- fan_speed.temp_ref1     = 0       → **keep**

### Q5. fan_boost (level1/fanboost)
- fan_boost.mode     = 0  (Off)  → **keep**
- fan_boost.speed    = 80 %      → **keep**
- fan_boost.interval = 12 min    → **keep**
- fan_boost.duration = 20 min    → **keep**
- fan_boost.temp     = 40 °F     → **keep**

### Q6. climacell_times (level1/climacell schedule)
- climacell_times[0..47] = 2 (On) for all 48 half-hour slots → **keep**

### Q7. humid_ctrl (level1/humidifier — 3 humidifier units)
- humid_ctrl[0..2].mode       = 1 (Timer) → **CHANGE to 0 (Manual)** — operator should see humidifiers running out of the box; Timer/Auto are opt-in
- humid_ctrl[0..2].cool_on    = 60 s → **keep** (inherited if user switches to Timer)
- humid_ctrl[0..2].cool_off   = 60 s → **keep**
- humid_ctrl[0..2].recirc_on  = 60 s → **keep**
- humid_ctrl[0..2].recirc_off = 60 s → **keep**
- humid_ctrl[0..2].refrig_on  = 60 s → **keep**
- humid_ctrl[0..2].refrig_off = 60 s → **keep**

### Q8. co2 (level1/co2)
- co2.cycle_or_set = 1200 → **keep**
- co2.duration     = 20 min → **keep**
- co2.min_temp     = 40 °F → **keep**
- co2.max_temp     = 50 °F → **keep**
- co2.fan_speed    = 100 % → **keep**
- co2.door_pos     = 100 % → **keep**
- co2.mode         = 0 (Off) → **keep**

### Q9. outside_air + cure + cure_limit (level1/outside)
- outside_air.mode             = 1 (Plenum)  → **keep**
- outside_air.differential     = 2 °F        → **keep**
- outside_air.temp_ref         = 255 (return-air sentinel) → **keep**
- outside_air.calc_humid_max   = 80 %        → **keep**
- cure.start_temp        = 60 °F → **keep**
- cure.start_humid       = 70 %  → **keep**
- cure.humid_high_limit  = 85 %  → **keep**
- cure_limit.low_temp_limit  = 35 °F  → **keep**
- cure_limit.high_temp_limit = 110 °F → **CHANGE to 90 °F** — 110 is too high

### Q10. runtime (level1/runclock — 48 half-hour slots)
- runtime[0..47].mode = 3 (Standby) → **keep**

### Q11. email (level1/email)
- email.smtp_server  = "" → **CHANGE to "mail.smtp2go.com"**
- email.smtp_port    = 587 → **CHANGE to 465**
- email.auth_type    = 0 (StartTLS) → **CHANGE to TLS/SSL** (implicit TLS — value to be confirmed against proto enum, likely 1 or 2)
- email.username     = "" → **CHANGE to "agristar.alerts"**
- email.password     = "" → **CHANGE to "4gri*st4r4l3rts"**
- email.from_address = "" → **CHANGE to "agristar.alerts@gellert.com"**
- email.to_address[0] = "" → **CHANGE to "youraccount@gmail.com"** (placeholder example for operator to replace)
- (other to_address slots remain "")

> ⚠ NOTE: baking SMTP credentials into firmware defaults is a security trade-off
> (binary inspection reveals the password). User explicitly approved on 2026-05-02.
> Auth-type enum value to be verified before edit lands in `lp_settings.c`.

### Q12. alert_flags (level1/alerts)
- alert_flags[0..19] = 1 (enabled) → **keep** (all alerts enabled by default; only fire when SMTP is configured)

### Q13. miscellaneous (level1/miscellaneous — heater/cavity/defrost)
- misc.heat_temp_thresh = 10 °F   → **keep**
- misc.cavity_mode      = 1 (Off) → **keep**
- misc.cavity_diff      = -5 °F   → **keep**
- misc.cavity_duty      = 50 %    → **keep**
- misc.defrost_duration = 10 min  → **keep**
- misc.cavity_standby_on = 0 (Off in Standby) → **keep** (operator opts in)
- (un-seeded → 0): refrig_mode, defrost_interval, kb_pref, cavity_target → **keep at 0**

### Q14. lights / preferences / read-only Level 1 pages
- All un-seeded (= 0). No explicit defaults required; operator picks on first use.

---
**Pass 1 (Level 1) — CLOSED 2026-05-02.**
Net firmware changes vs current `lp_settings.c::LpSettings_DataInit()`:
1. `humid_ctrl[0..2].mode`              : 1 → **0** (Manual)
2. `cure_limit.high_temp_limit`         : 110 → **90** °F
3. `email.smtp_server`                  : "" → **"mail.smtp2go.com"**
4. `email.smtp_port`                    : 587 → **465**
5. `email.auth_type`                    : 0 → **TLS/SSL** (verify enum value)
6. `email.username`                     : "" → **"agristar.alerts"**
7. `email.password`                     : "" → **"4gri*st4r4l3rts"**
8. `email.from_address`                 : "" → **"agristar.alerts@gellert.com"**
9. `email.to_address[0]`                : "" → **"youraccount@gmail.com"**

All other Level 1 fields confirmed kept at current value.

## Pass 2 — Level 2

### Q2.1. basic (level2/basic — Basic Setup)
- basic.multi_view     = 6                    → **keep**
- basic.animations     = 1 (on)               → **keep**
- basic.system_mode    = 0 (Potato)           → **keep**
- basic.temp_unit      = 0 (°F)               → **keep**
- basic.home_page      = 0 (System Monitor)   → **keep**
- basic.location       = ""                   → **CHANGE to "Gellert Nova"**
- basic.language       = 0 (English)          → **keep**
- basic.login_pw       = "" (no PIN)          → **keep**
- basic.local_required = 0 (off)              → **keep**

### Q2.2. burner (level2/burner)
- burner.on      = 10 % → **keep**
- burner.low     = 25 % → **keep**
- burner.manual  = 75 % → **keep**
- burner.p_gain  = 5.0  → **keep**
- burner.i_gain  = 15.0 → **keep**
- burner.d_gain  = 2.0  → **keep**
- burner.u_limit = 3.0 s → **keep**

### Q2.3. door (level2/door)
- door.actuator_time  = 180 s → **keep**
- door.cool_air_cycle = 10    → **keep**
- door.p_gain  = 5.0  → **keep**
- door.i_gain  = 15.0 → **keep**
- door.d_gain  = 2.0  → **keep**
- door.u_limit = 3.0 s → **keep**

### Q2.4. refrigeration (level2/refrigeration)
- refrig.num_stages = 8 → **keep**
- refrig.stages[0..7].on  = {20,30,40,50,60,70,80,90} → **keep**
- refrig.stages[0..7].off = {10,20,30,40,50,60,70,80} → **keep**
- refrig.p_gain  = 5.0  → **keep**
- refrig.i_gain  = 15.0 → **keep**
- refrig.d_gain  = 2.0  → **keep**
- refrig.u_limit = 3.0 s → **keep**
- refrig.limit   = 27.0 → **keep**
- refrig.fail_mode = 255 (sentinel) → **keep**

### Q2.5. climacell (level2/climacell)
- climacell.efficiency = 90 % → **keep**
- climacell.p_gain  = 5.0  → **keep**
- climacell.i_gain  = 15.0 → **keep**
- climacell.d_gain  = 2.0  → **keep**
- climacell.u_limit = 3.0 s → **keep**

> ⚠ FIRMWARE NOTE (found 2026-05-02): `LpSettings_DataInit()` line ~327 has
> `s_data.alert.flags[i] = i;` — should be `= 1` (enabled). Currently seeds
> flags[0]=0, flags[1]=1, …, flags[19]=19 instead of all = 1. Flag for fix
> during the batch-edit phase.

> ⚠ Q2.1 field-name correction: actual proto field is `basic.storage_name`
> (not `basic.location`). The "Gellert Nova" string lands in `storage_name`.

### Q2.6. failures1 (level2/failures1 — equipment failure timers/modes)
- failure.fan_mode            = 2 (Fail/Shutdown) → **keep**
- failure.fan_timer           = 3 min  → **CHANGE to 1 min**
- failure.heat_timer          = 10 min → **CHANGE to 1 min**
- failure.refrig_timer        = 10 min → **CHANGE to 1 min**
- failure.burner_timer        = 10 min → **CHANGE to 1 min**
- failure.humid_timer         = 10 min → **CHANGE to 1 min**
- failure.climacell_timer     = 10 min → **CHANGE to 1 min**
- failure.refrig_stages_timer = 10 min → **CHANGE to 1 min**
- failure.aux_timer           = 10 min → **CHANGE to 1 min**
- failure.cavity_heat_timer   = 10 min → **CHANGE to 1 min**
- failure.lights_timer        = 1      → **keep**
- failure.lights_units        = 60 (hours) → **keep**
- (other *_mode fields un-seeded = 0 / "log only") → **keep**

### Q2.7. failures2 (level2/failures2 — sensor failure modes)
- failure2.plen_sen_mode  = 1 (Alarm) → **keep**
- failure2.plen_sen_diff  = 2.0 °F    → **keep**
- failure2.out_air_mode   = 1 (Alarm) → **keep**
- failure2.out_humid_mode = 1 (Alarm) → **keep**
- failure2.high_co2_mode  = 1 (Alarm) → **keep**
- failure2.co2_setpt      = 2500 ppm  → **CHANGE to 4000 ppm**
- failure2.low_humid_set  = 80 %      → **keep**

### Q2.8. auxiliary (level2/auxiliary)
- All aux slots un-seeded (mode=0 / Off) → **keep** — page should stay dark until an aux output is defined on the IO Config page

### Q2.9. ioconfig (level2/ioconfig — Orbit Main board defaults)
**Outputs (DO):**
| DO | Equipment | Status |
|----|-----------|--------|
| DO1  | EQ_REDLIGHT       | **keep** |
| DO2  | EQ_YELLOWLIGHT    | **keep** |
| DO3  | EQ_FAN (greenlight wired in parallel) | **keep** |
| DO4  | EQ_CLIMACELL      | **keep** |
| DO5  | EQ_HUMID_HEAD1    | **keep** |
| DO6  | EQ_HUMID_PUMP1    | **keep** |
| DO7  | EQ_REFRIG_STAGE1  | **keep** |
| DO8  | EQ_HEAT           | **keep** |
| DO9  | EQ_PULSEDOOR_OPEN  → **EQ_LIGHTS1** | **CHANGE** |
| DO10 | EQ_PULSEDOOR_CLOSE → **EQ_LIGHTS2** | **CHANGE** |
- EQ_PULSEDOOR_POWER / OPEN / CLOSE → **disable** (Output=IO_UNDEFINED, Enabled=0) — slots reassigned to bay lights

**Inputs (DI) — proving / safety:**
| DI | Equipment | Status |
|----|-----------|--------|
| DI1  | (currently IO_UNDEFINED) → **EQ_LOW_TEMP**    | **CHANGE** (low-temp safety) |
| DI2  | (currently IO_UNDEFINED) → **EQ_AIR_FLOW**    | **CHANGE** (airflow restriction) |
| DI3  | EQ_FAN              | **keep** |
| DI4  | EQ_CLIMACELL        | **keep** |
| DI5  | EQ_HUMID_HEAD1      | **keep** |
| DI6  | EQ_HUMID_PUMP1 → **IO_UNDEFINED** | **CHANGE** (left empty per operator) |
| DI7  | EQ_REFRIG_STAGE1    | **keep** |
| DI8  | EQ_HEAT             | **keep** |
| DI9  | (currently IO_UNDEFINED) → **EQ_LIGHTS1**     | **CHANGE** |
| DI10 | (currently IO_UNDEFINED) → **EQ_LIGHTS2**     | **CHANGE** |
| DI11 | **HARDCODED EMERGENCY STOP** — not configurable, not assigned to any EQ_* | **NEW SAFETY PATH** |

> ⚠ DI11 emergency stop is a NEW firmware feature — no existing EQ_ESTOP enum.
> Implementation work item: read DI11 raw in the failure-check loop and force
> immediate system shutdown + alarm regardless of any equipment mapping. The
> IO Config UI must hide DI11 (read-only "EMERGENCY STOP" label).
>
> Sensors are NOT on this page — they live on the Analog Boards page.

### Q2.10. analog (level2/analog — sensor board labels)
**Board 0 (DEFAULT_TEMP_BOARD) — temperatures:**
- slot 0  SENSOR_PLENUM_TEMP_1 → **"Plenum Temperature"** (matches System Monitor)
- slot 1  SENSOR_PLENUM_TEMP_2 → **"Plenum Temp 2"**
- slot 2  SENSOR_OUTSIDE_TEMP  → **"Outside Temp"**
- slot 3  SENSOR_RETURN_TEMP   → **"Return Temp"**

**Board 1 (DEFAULT_HUMID_BOARD) — humidity / CO₂:**
- slot 0  SENSOR_OUTSIDE_HUMID → **"Outside Humid"**
- slot 1  SENSOR_PLENUM_HUMID  → **"Plenum Humid"**
- slot 2  SENSOR_RETURN_HUMID  → **"Return Humid"**
- slot 3  SENSOR_CO2           → **"CO2"**

**Board 2 and beyond:** labels left empty (operator names per-pile at commissioning).

**All sensors:** offset=0.0, disabled=false, display_unit=0, eng_min=0, eng_max=0 → **keep**

### Q2.11. pid (level2/pid — master PID view)
- View-only aggregator of burner / door / refrig / climacell PIDs already covered (5/15/2/3 across the board) → **no separate defaults**

### Q2.12. pwm (level2/pwm — orbit AO equipment assignment)
- AoEquip[slot=0][ch=0] = 0 (UNUSED) → **CHANGE to 1 (AO_EQUIP_FAN_SPEED)** — Main orbit AO1 = Fan
- AoEquip[slot=0][ch=1] = 0 (UNUSED) → **CHANGE to 3 (AO_EQUIP_REFRIG)** — Main orbit AO2 = Refrigeration
- AoEquip[slot 1..N][*] = 0 (UNUSED) → **keep** (additional orbits unassigned until commissioning)

> Enum reference (`Nova_Firmware/Platform/nova_fan_output.h::ao_equip_t`):
> 0=UNUSED, 1=FAN_SPEED, 2=DOORS, 3=REFRIG, 4=BURNER

### Q2.13. tcpip (level2/tcpip — network)
- network.dhcp_enabled = 1 (DHCP on) → **keep**
- network.static_ip / netmask / gateway / dns / hostname = "" → **keep**
- network.http_port    = 80 → **keep**

### Q2.14. master (level2/master — multi-controller master/slave)
- master.is_master  = 0 → **keep** (standalone)
- master.master_ip  = "" → **keep**
- master.slave_count = 0, master.slaves[] = [] → **keep**
- (master distributes plenum temp/humid to slaves — deeper review deferred)

### Q2.15. accounts (level2/accounts)
- accounts.users[] = [] → **CHANGE — seed one default account: username `default`, password `GELLERT`** (offline/non-internet panels need a working login on first boot)
- (factory PW hardcoded in firmware: keep as-is for now — review later)

> **Future direction (deferred):** when a panel is internet-connected, the
> Accounts page should authenticate against Django on the Azure web app
> so user/panel mapping has a single source of truth. Offline panels keep
> the local `default:GELLERT` account.

### Q2.16. remote (level2/remote — multi-Nova LAN discovery + groups)
- Discovered Nova list: empty → **keep** (auto-populates from same-subnet broadcast)
- Groups: none → **keep** (operator defines at commissioning — picks which panels at each site appear in the group view)

### Q2.17. service (level2/service)
- No persistent settings — action buttons / install fields only → **no defaults**

### Q2.18. fans (level2/fans)
- No additional persistent settings beyond fanspeed/ioconfig/failures1 → **no defaults**

> **Future work item:** a separate VFD-Modbus page is planned. When the
> operator enables VFD RTU control, the system fan-output path switches
> to Modbus RTU and the alarm set should swap to VFD-specific alarms
> (drive fault, comm loss, overcurrent, etc.) instead of the generic
> airflow-loss alarm. Currently VFD RTU is bridge-side env-driven only
> (`VFD_ENABLED` in `constellation-ui/server/src/index.ts`) — no Nova
> persistent setting yet.

### Q2.19. settings (level2/settings — save/restore + logging)
- user_log.interval_minutes = 60 → **CHANGE to 15 min**
- user_log.enabled          = 1 (wrap on full) → **keep**
- pid_log.wrap              = 1 → **keep**

### Q2.20. orbit-sensors (level2/orbit-sensors)
- Per-orbit sensor bank labels & enable flags → **keep empty** (operator labels at commissioning)
- Board 0/1 fixed slot conventions covered in Q2.10
- Board 2+ uses the per-slot sensor-type dropdown (`getSensorType` in analog page):
  0/3=Temperature, 1=Humidity, 2=CO2 #1, 4=Return Temp #1, 5=Return Temp #2,
  6=Return Humid #1, 7=Return Humid #2, 8=CO2 #2, 9=Pile Temperature,
  10=Pile Humidity, 11=Static Pressure → **no firmware seed**, operator picks per slot

### Q2.21. iotclient (level2/iotclient — Azure cloud client)
- iot.endpoint / device_id / sas_token = "" → **keep** (operator pastes tokens at commissioning)
- iot.enabled = 0 (off) → **keep**
- iot.poll_interval defaults → **keep**

### Q2.22. download (level2/download — software upgrade)
- upgrade.last_version = "" → **keep**
- upgrade.auto_check   = 0  (off) → **keep** (USB upgrade is the primary path)
- upgrade.update_url   = "" → **keep**

> **Future direction:** keep USB upgrade as-is. Add OTA via *push* from
> two sources: (1) admins on the Azure web app initiate per-panel
> firmware pushes, (2) the local Pi 5 may push firmware to its bonded
> Nova(s). No always-on poll-for-update pattern. Plan in
> `docs/LP-AM2434-OTA-Update-Plan.md`.

### Q2.23. Read-only Level 2 pages
- `level2/log`, `level2/pidlog`, `level2/table`, `level2/graph` — runtime views, no persistent settings → **no defaults**

---
**Pass 2 (Level 2) — CLOSED 2026-05-02.**

Net firmware changes vs current `lp_settings.c::LpSettings_DataInit()`:

1. `basic.storage_name`                    → **"Gellert Nova"**
2. `failure.fan_timer`                     : 3  → **1 min**
3. `failure.heat_timer`                    : 10 → **1 min**
4. `failure.refrig_timer`                  : 10 → **1 min**
5. `failure.burner_timer`                  : 10 → **1 min**
6. `failure.humid_timer`                   : 10 → **1 min**
7. `failure.climacell_timer`               : 10 → **1 min**
8. `failure.refrig_stages_timer`           : 10 → **1 min**
9. `failure.aux_timer`                     : 10 → **1 min**
10. `failure.cavity_heat_timer`            : 10 → **1 min**
11. `failure2.co2_setpt`                   : 2500 → **4000 ppm**
12. **IO Config DO9** EQ_PULSEDOOR_OPEN  → **EQ_LIGHTS1**
13. **IO Config DO10** EQ_PULSEDOOR_CLOSE → **EQ_LIGHTS2**
14. **IO Config disable** EQ_PULSEDOOR_POWER/OPEN/CLOSE
15. **IO Config DI1** = **EQ_LOW_TEMP** (was IO_UNDEFINED)
16. **IO Config DI2** = **EQ_AIR_FLOW** (was IO_UNDEFINED)
17. **IO Config DI6** EQ_HUMID_PUMP1 → **IO_UNDEFINED**
18. **IO Config DI9** = **EQ_LIGHTS1** (was IO_UNDEFINED)
19. **IO Config DI10** = **EQ_LIGHTS2** (was IO_UNDEFINED)
20. **NEW: DI11 hardcoded EMERGENCY STOP** safety path (firmware feature work item)
21. **Sensor labels Board 0**: slot 0 "Plenum Temperature", slot 1 "Plenum Temp 2", slot 2 "Outside Temp", slot 3 "Return Temp"
22. **Sensor labels Board 1**: slot 0 "Outside Humid", slot 1 "Plenum Humid", slot 2 "Return Humid", slot 3 "CO2"
23. `AoEquip[0][0]`                        : 0 → **1 (FAN_SPEED)**
24. `AoEquip[0][1]`                        : 0 → **3 (REFRIG)**
25. **NEW account**: username `default`, password `GELLERT`
26. `user_log.interval_minutes`            : 60 → **15 min**

Firmware bugs found during review (fix during batch-edit):
- `lp_settings.c` ~line 327: `s_data.alert.flags[i] = i;` should be `= 1;`

All other Level 2 fields confirmed kept at current value.
