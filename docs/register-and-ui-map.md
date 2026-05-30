# Register & UI Map â€” single source of truth

**Purpose.** Every settable / readable value in the Constellation system
takes one of two paths:

  - **Modbus HR** on an orbit board â†’ CONTROLLER â†’ bridge â†’ UI, *or*
  - **Proto Envelope** field tag â†’ bridge â†’ UI

This doc keeps **firmware symbol â†” Modbus address â†” proto field â†” UI
page+label** lined up so we never accidentally implement two paths to
the same setting (a problem that bit us hard in Phase-D, see
[/memories/repo/legacyShim-multifield-save-bug.md](../memories/repo/legacyShim-multifield-save-bug.md)).

## Read this before adding a new field

1. Search this doc for the value (Ctrl-F by name or by HR address).
2. If it exists, **extend the existing row** â€” don't add a parallel
   path. Confirm the bridge already routes it; if it doesn't, fix the
   bridge.
3. If it doesn't exist, decide: HR (per-orbit live data, per-orbit
   config) or proto (system-wide settings, telemetry roll-ups).
4. Add the row to the **Hand-curated UI mapping** section below
   *before* writing code.
5. After the firmware change lands, run
   `pwsh scripts/Generate-RegisterMap.ps1` to refresh the auto-extracted
   tables.

## Related docs (do not duplicate)

- [`firmware-equipment-control.md`](firmware-equipment-control.md) â€” DO/DI shift-register bit assignments, equipment state machines
- [`/memories/repo/datacache-cgi-table.md`](../memories/repo/datacache-cgi-table.md) â€” CGI varName â†” msgTag (legacy CSV bridge layer; being deprecated)
- [`/memories/repo/analog-page-migration.md`](../memories/repo/analog-page-migration.md) â€” AnalogBoard tag 26 â†” AnalogAll CSV layout
- [`proto-direct-redesign-plan.md`](proto-direct-redesign-plan.md) â€” overall plan to delete the CSV layer
- [`proto-migration-pattern.md`](proto-migration-pattern.md) â€” per-page recipe

---

## Hand-curated UI mapping

> **Edit this section by hand** when you wire a new value through to a
> Svelte page. The auto-gen block below only knows firmware-side facts;
> it cannot know which UI label maps to which HR address.

### STORAGE orbit (slot 0..N, `dipswitchId` = `(role<<4)|slot`)

| HR addr / proto | Firmware symbol | Proto field | UI page | UI label | R/W |
|---|---|---|---|---|---|
| HR 0..1 | `HR_AO_BASE` | `OrbitStatus.boards[].ao_pct[]` (tag 120) | level2/pwm | "PWM Output %" (per channel) | R/W |
| HR 2..3 | `HR_AO_MODE_BASE` | (per-orbit, no proto roll-up yet) | level2/pwm | "AO Mode (V/I)" | R/W |
| HR 100..147 | `HR_VFD_BASE` | (passthrough; not in UI yet) | â€” | â€” | R/W |
| HR 200..263 | `HR_SENSOR_BASE` | `SensorData.values[]` (tag 13) | level1/sensors | live sensor numbers | R/W (test) |
| HR 40000..40006 | `HR_STATUS_BASE` | `OrbitStatus.boards[].connected/uptime` (tag 120) | level2/ioconfig | orbit table cells | R |

### GDC (Door) orbit

| HR addr / proto | Firmware symbol | Proto field | UI page | UI label | R/W |
|---|---|---|---|---|---|
| HR 300 | `HR_GDC_DOOR_PCT` | (TBD) | level1/door | "Door Setpoint" | R/W |
| HR 310..314 | `HR_GDC_POS_BASE` | (TBD) | level1/door | "Door Live %" | R |
| HR 320..324 | `HR_GDC_STATUS_BASE` | (TBD) | level1/door | door status flags | R |

### TRITON (Refrigeration) orbit

| HR addr / proto | Firmware symbol | Proto field | UI page | UI label | R/W |
|---|---|---|---|---|---|
| HR 400..439 | `HR_TRITON_SP_BASE` | `RefrigSettings` (tag 44) | level2/refrig | cut-in/cut-out, min run/off | R/W |
| HR 460..473 | `HR_TRITON_LIVE_SENS_BASE` | `SensorData` (tag 13) | level1/refrig | live evap/cond temps | R |
| HR 480..493 | `HR_TRITON_LIVE_EQ_BASE` | `EquipmentStatus` (tag 11) | level1/refrig | comp state, capacity, demand | R |
| HR 530..541 | `HR_TRITON_ALARM_*` | `WarningReport` (tag 12) | level1/alarms | alarm flags | R/W (ack) |

### System-wide (proto only, no Modbus path)

| Proto tag.field | Firmware source | UI page | UI label | R/W |
|---|---|---|---|---|
| 10 `system_status` | `nova_main` task | level1/dashboard | header bar | R |
| 11 `equipment_status` | EquipmentControl thread | level1/main | equipment grid | R |
| 13 `sensor_data` | sensor pipeline | level1/sensors | sensor pages | R |
| 20 `basic_setup` | `LpSettings.BasicSetup` | level2/basic | storage type, units, dealer | R/W |
| 22 `version_info` | `lp_version.h` | level1/version | "System SW Versions" | R |
| 24 `io_config` | `LpSettings.IoConfig` | level2/ioconfig | equipment grid | R/W |
| 40 `plenum_settings` | `LpSettings.Plenum` | level2/plenum | temp setpoints, PIDs | R/W |
| 64 `pwm_settings` | `LpSettings.AoEquip` | level2/pwm | per-orbit AO assignment | R/W |

> Tags above are pulled from the auto-gen block below; whenever you add
> a row here, cross-check the tag against that table to avoid drift.

---

<!-- AUTO-GEN:BEGIN  do not edit by hand; regenerate via scripts/Generate-RegisterMap.ps1 -->

### Modbus holding registers (auto-extracted from `Nova_Firmware/.../orbit_server/*.h`)

#### orbit_gdc

| HR addr | Symbol | Comment | Source |
|---:|---|---|---|
| 300 | `HR_GDC_DOOR_PCT` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:75](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L75) |
| 300 | `HR_GDC_BASE` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:74](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L74) |
| 301 | `HR_GDC_DEFAULT_TRAVEL_MS` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:76](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L76) |
| 302 | `HR_GDC_ACTIVE_STAGE_COUNT` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:77](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L77) |
| 303 | `HR_GDC_CALIBRATING` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:78](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L78) |
| 304 | `HR_GDC_CAL_PHASE` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:79](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L79) |
| 305 | `HR_GDC_CAL_REQUEST` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:80](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L80) |
| 310 | `HR_GDC_POS_BASE` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:81](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L81) |
| 315 | `HR_GDC_TARGET_BASE` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:82](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L82) |
| 320 | `HR_GDC_STATUS_BASE` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:83](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L83) |
| 325 | `HR_GDC_STAGE_BASE` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:84](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L84) |
| 330 | `HR_GDC_OPEN_MS_BASE` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:85](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L85) |
| 335 | `HR_GDC_CLOSE_MS_BASE` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:86](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L86) |
| 340 | `HR_GDC_END` | exclusive | [Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h:87](Nova_Firmware/lp_am2434/orbit_server/orbit_gdc.h#L87) |

#### orbit_triton

| HR addr | Symbol | Comment | Source |
|---:|---|---|---|
| 400 | `HR_TRITON_SP_BASE` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h:175](Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h#L175) |
| 440 | `HR_TRITON_FAIL_BASE` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h:177](Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h#L177) |
| 440 | `HR_TRITON_SP_END` | exclusive | [Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h:176](Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h#L176) |
| 454 | `HR_TRITON_FAIL_END` | exclusive | [Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h:178](Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h#L178) |
| 460 | `HR_TRITON_LIVE_SENS_BASE` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h:179](Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h#L179) |
| 474 | `HR_TRITON_LIVE_SENS_END` | exclusive | [Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h:180](Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h#L180) |
| 480 | `HR_TRITON_LIVE_EQ_BASE` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h:181](Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h#L181) |
| 494 | `HR_TRITON_LIVE_EQ_END` | exclusive | [Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h:182](Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h#L182) |
| 530 | `HR_TRITON_ALARM_ACTIVE_BASE` | 6 regs | [Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h:183](Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h#L183) |
| 536 | `HR_TRITON_ALARM_ACKED_BASE` | 6 regs | [Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h:184](Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h#L184) |
| 542 | `HR_TRITON_ACK_CMD` |  | [Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h:185](Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h#L185) |
| 543 | `HR_TRITON_END` | exclusive | [Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h:186](Nova_Firmware/lp_am2434/orbit_server/orbit_triton.h#L186) |

### Envelope proto tags (auto-extracted from `proto/agristar/envelope.proto`)

| Tag | Field name | Message type | Line |
|---:|---|---|---:|
| 1 | `protocol_version` | `uint32` | 32 |
| 2 | `seq` | `uint32` | 33 |
| 10 | `system_status` | `SystemStatus` | 37 |
| 11 | `equipment_status` | `EquipmentStatus` | 38 |
| 12 | `warning_report` | `WarningReport` | 39 |
| 13 | `sensor_data` | `SensorData` | 40 |
| 14 | `runtimes` | `Runtimes` | 41 |
| 15 | `humid_modes` | `HumidModes` | 42 |
| 16 | `aux_switches` | `AuxSwitches` | 43 |
| 17 | `data_load_status` | `DataLoadStatus` | 44 |
| 18 | `fan_runtime` | `FanRuntime` | 45 |
| 19 | `network_config` | `NetworkConfig` | 46 |
| 20 | `basic_setup` | `BasicSetup` | 49 |
| 21 | `date_time` | `DateTime` | 50 |
| 22 | `version_info` | `VersionInfo` | 51 |
| 23 | `service_info` | `ServiceInfo` | 52 |
| 24 | `io_config` | `IoConfig` | 53 |
| 25 | `io_definition` | `IoDefinition` | 54 |
| 26 | `analog_board` | `AnalogBoard` | 55 |
| 27 | `available_io` | `AvailableIo` | 56 |
| 28 | `sensor_labels` | `SensorLabels` | 57 |
| 29 | `account_settings` | `AccountSettings` | 58 |
| 30 | `log_chunk` | `LogChunk` | 61 |
| 31 | `password_response` | `PasswordResponse` | 62 |
| 40 | `plenum_settings` | `PlenumSettings` | 65 |
| 41 | `fan_speed_settings` | `FanSpeedSettings` | 66 |
| 42 | `fan_boost_settings` | `FanBoostSettings` | 67 |
| 43 | `ramp_rate_settings` | `RampRateSettings` | 68 |
| 44 | `refrig_settings` | `RefrigSettings` | 69 |
| 45 | `burner_settings` | `BurnerSettings` | 70 |
| 46 | `co2_settings` | `Co2Settings` | 71 |
| 47 | `cure_settings` | `CureSettings` | 72 |
| 48 | `climacell_settings` | `ClimacellSettings` | 73 |
| 49 | `climacell_times` | `ClimacellTimes` | 74 |
| 50 | `humid_ctrl_settings` | `HumidCtrlSettings` | 75 |
| 51 | `outside_air_settings` | `OutsideAirSettings` | 76 |
| 52 | `misc_settings` | `MiscSettings` | 77 |
| 53 | `failure_settings` | `FailureSettings` | 78 |
| 54 | `failure_settings2` | `FailureSettings2` | 79 |
| 55 | `temp_alarm_settings` | `TempAlarmSettings` | 80 |
| 56 | `door_settings` | `DoorSettings` | 81 |
| 57 | `load_monitor_settings` | `LoadMonitorSettings` | 82 |
| 58 | `aux_program_settings` | `AuxProgramSettings` | 83 |
| 59 | `user_log_settings` | `UserLogSettings` | 84 |
| 60 | `pid_settings` | `PidSettings` | 85 |
| 61 | `graph_fav_settings` | `GraphFavoriteSettings` | 86 |
| 62 | `email_settings` | `EmailSettings` | 87 |
| 63 | `alert_settings` | `AlertSettings` | 88 |
| 64 | `pwm_settings` | `PwmChannelSettings` | 89 |
| 65 | `network_nodes` | `NetworkNodeSettings` | 90 |
| 66 | `master_slave_settings` | `MasterSlaveSettings` | 91 |
| 67 | `pid_log_settings` | `PidLogSettings` | 92 |
| 68 | `cure_limit_settings` | `CureLimitSettings` | 93 |
| 69 | `http_port_settings` | `HttpPortSettings` | 94 |
| 70 | `log_record` | `LogRecord` | 102 |
| 71 | `activity_event` | `ActivityEvent` | 103 |
| 72 | `pid_log_record` | `PidLogRecord` | 104 |
| 73 | `load_log_record` | `LoadLogRecord` | 105 |
| 74 | `public_address_settings` | `PublicAddressSettings` | 96 |
| 75 | `sys_mode_settings` | `SysModeSettings` | 97 |
| 76 | `bay_name_settings` | `BayNameSettings` | 98 |
| 77 | `runtime_settings` | `RuntimeSettings` | 99 |
| 80 | `equipment_cmd` | `EquipmentCmd` | 108 |
| 81 | `refrig_diag_cmd` | `RefrigDiagCmd` | 109 |
| 82 | `system_cmd` | `SystemCmd` | 110 |
| 83 | `log_query` | `LogQuery` | 111 |
| 84 | `password_auth` | `PasswordAuth` | 112 |
| 85 | `network_node_cmd` | `NetworkNodeCmd` | 113 |
| 86 | `io_name_update` | `IoNameUpdate` | 114 |
| 90 | `settings_update` | `SettingsUpdate` | 117 |
| 100 | `ack` | `Ack` | 120 |
| 101 | `heartbeat` | `Heartbeat` | 121 |
| 102 | `data_request` | `DataRequest` | 122 |
| 110 | `fw_begin_update` | `FwBeginUpdate` | 126 |
| 111 | `fw_data_chunk` | `FwDataChunk` | 127 |
| 112 | `fw_finalize_update` | `FwFinalizeUpdate` | 128 |
| 113 | `fw_activate_bank` | `FwActivateBank` | 129 |
| 114 | `fw_update_status` | `FwUpdateStatus` | 131 |
| 115 | `fw_bank_info` | `FwBankInfo` | 132 |
| 120 | `orbit_status` | `OrbitStatus` | 136 |
| 121 | `orbit_discovery` | `OrbitDiscovery` | 137 |
| 122 | `orbit_role_assign` | `OrbitRoleAssign` | 139 |
| 123 | `ao_equip_assign` | `AoEquipAssign` | 140 |
| 124 | `orbit_sensor_bank` | `OrbitSensorBank` | 142 |
| 125 | `triton_reg_write` | `TritonRegWrite` | 144 |
| 200 | `vfd_status` | `VfdStatus` | 149 |

<!-- AUTO-GEN:END -->

---

## When this doc is wrong

If the auto-extracted block says one thing and the UI uses a different
HR address / proto field, **the UI is the bug** (the firmware is the
source of truth). Either:

- the UI page is using a stale legacy CSV path (fix it per
  `proto-migration-pattern.md`), or
- a new field was added to firmware without updating the
  hand-curated section above (add the row).

Never patch the UI to match a "wrong" register address â€” chase it back
to firmware first.
