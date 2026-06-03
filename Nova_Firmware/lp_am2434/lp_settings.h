/*
 * lp_settings.h — Phase E in-memory Settings struct + (de)serializer
 *
 * Holds the LP firmware's mirror of all user-configurable settings.
 * Today only `BasicSetup` is implemented; tags 40-65 (Plenum, FanSpeed,
 * etc.) extend `LpSettings_*` field-by-field as the UI gates demand.
 *
 * Persistence model:
 *   - In-RAM struct is the live source-of-truth for cadence emit.
 *   - LpSettings_Serialize() packs it to a byte blob we hand to
 *     `lp_settings_store::LpSettings_Save()`. The store writes to the
 *     inactive RAM bank, flips active, and trips the dirty flag so
 *     `bridge_uart_task` ships envelope field 32 to the bridge.
 *   - LpSettings_Deserialize() is called from `LpSettings_Restore()`
 *     after the bridge replays `~/.constellation/lp_settings.bin` on
 *     a disconnected→connected edge — restoring user settings across
 *     power cycles.
 *
 * Wire format (settings.proto schema-stable):
 *   The serialize/deserialize functions emit/parse a *protobuf message*
 *   with the same field numbers as `BasicSetupUpdate` (settings.proto).
 *   Reusing the same encoding for both the save blob and the over-wire
 *   envelope means one source-of-truth field map and lets a future
 *   tool (eg. `protoc --decode`) inspect the file directly.
 *
 *   Field numbers we currently round-trip:
 *      1  string  storage_name
 *      2  uint32  temp_type      (0=F, 1=C)
 *      3  uint32  mode
 *      4  string  home_page
 *      5  uint32  system_mode    (0=Potato, 1=Onion)
 *      6  uint32  language
 *      8  uint32  multi_view
 *     10  uint32  local_login
 *     11  uint32  animations
 *
 *   Strings 7 (master_ip) and 9 (login_pw) are tracked in the struct
 *   but persisted lazily — see comments in Settings_Apply for why.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#ifndef LP_SETTINGS_H
#define LP_SETTINGS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define LP_SET_STR_MAX 64

/* In-RAM mirror of Settings.Basic (a subset of legacy AS2 Settings.*). */
typedef struct {
    char     storage_name[LP_SET_STR_MAX];
    char     home_page   [LP_SET_STR_MAX];
    char     master_ip   [LP_SET_STR_MAX];
    char     login_pw    [LP_SET_STR_MAX];
    uint32_t temp_type;
    uint32_t mode;
    uint32_t system_mode;
    uint32_t language;
    uint32_t multi_view;
    uint32_t local_login;
    uint32_t animations;
} LpBasicSetup;

/* Mirrors PlenumSettings (settings.proto field 1, envelope tag 40). */
typedef struct {
    float    temp_setpoint;          /* field 1 */
    uint32_t humid_setpoint;         /* field 2 */
    uint32_t humid_setpoint_ref;     /* field 3 */
    float    burner_temp_setpoint;   /* field 4 */
    float    burner_threshold;       /* field 5 */
} LpPlenum;

/* Mirrors FanSpeedSettings (settings.proto field 2, envelope tag 41). */
typedef struct {
    uint32_t max_speed;       /* field 1 */
    uint32_t min_speed;       /* field 2 */
    uint32_t refrig_speed;    /* field 3 */
    uint32_t recirc_speed;    /* field 4 */
    uint32_t update_period;   /* field 5 */
    float    temp_diff;       /* field 6 */
    uint32_t temp_ref1;       /* field 7 */
    uint32_t temp_ref2;       /* field 8 */
    uint32_t prev_speed;      /* field 9 */
    uint32_t update_mode;     /* field 10 */
} LpFanSpeed;

/* Mirrors FanBoostSettings (settings.proto field 3, envelope tag 42). */
typedef struct {
    uint32_t mode;        /* field 1: 0=Off, 1=Interval, 2=Temp-based */
    uint32_t speed;       /* field 2 */
    uint32_t interval;    /* field 3 */
    uint32_t duration;    /* field 4 */
    float    temp;        /* field 5 */
} LpFanBoost;

/* Mirrors RampRateSettings (settings.proto field 4, envelope tag 43).
 * NOTE: update_period==255 means "Automatically" in firmware; UI shows
 * the string. Value 255 is not a proto3 default so emits normally. */
typedef struct {
    float    rate_per_day;     /* field 1 */
    uint32_t update_period;    /* field 2 (255=Auto) */
    float    temp_diff;        /* field 3 */
    uint32_t temp_ref;         /* field 4 (255=return-air) */
    float    target_temp;      /* field 5 */
} LpRampRate;

/* RefrigStage submessage (RefrigSettings field 2 + 14, envelope tag 44).
 *
 * Wire field numbers per nova_dataexc.c::apply_refrig stage sub-decoder
 * + NovaMsg_SendRefrigSettings: 1=on, 2=off, 3=diagnostic. (Legacy
 * comments in settings.proto warn that these names DID NOT historically
 * match older C names trigger_temp/enabled/delay_minutes — wire numbers
 * always won; new code here uses the wire names directly.) */
#define LP_REFRIG_MAX_STAGES 8
typedef struct {
    uint32_t on;          /* field 1 */
    uint32_t off;         /* field 2 */
    uint32_t diagnostic;  /* field 3 */
} LpRefrigStage;

/* Mirrors RefrigSettings (settings.proto field 5, envelope tag 44).
 * Includes a fixed-size stages[] array because we have no malloc on
 * the LP; count_stages tracks how many slots are populated. */
typedef struct {
    uint32_t      num_stages;          /* field 1 (legacy AS2 mirror) */
    LpRefrigStage stages[LP_REFRIG_MAX_STAGES];   /* field 2 (repeated) */
    uint32_t      stages_count;        /* derived: # populated slots */
    float         p_gain;              /* field 3 */
    float         i_gain;              /* field 4 */
    float         d_gain;              /* field 5 */
    float         u_limit;             /* field 6 */
    uint32_t      mode;                /* field 7 */
    uint32_t      defrost_interval;    /* field 8 */
    uint32_t      defrost_duration;    /* field 9 */
    uint32_t      purge;               /* field 10 */
    uint32_t      purge_threshold;     /* field 11 */
    float         limit;               /* field 12 */
    uint32_t      fail_mode;           /* field 13 */
} LpRefrig;

/* Mirrors BurnerSettings (settings.proto field 6, envelope tag 45). */
typedef struct {
    uint32_t on;        /* field 1 */
    uint32_t low;       /* field 2 */
    float    p_gain;    /* field 3 */
    float    i_gain;    /* field 4 */
    float    d_gain;    /* field 5 */
    float    u_limit;   /* field 6 */
    uint32_t mode;      /* field 7 */
    uint32_t manual;    /* field 8 */
} LpBurner;

/* Mirrors Co2Settings (settings.proto field 7, envelope tag 46). */
typedef struct {
    uint32_t mode;              /* field 1: 0=Off, 1=Timer, 2=CO2 sensor */
    float    min_temp;          /* field 2 */
    float    max_temp;          /* field 3 */
    uint32_t duration_minutes;  /* field 4 */
    uint32_t cycle_or_set;      /* field 5 */
    uint32_t fan_output;        /* field 6 */
    uint32_t door_output;       /* field 7 */
} LpCo2;

/* Mirrors CureSettings (settings.proto field 8, envelope tag 47). */
typedef struct {
    float    start_temp;        /* field 1 */
    uint32_t humid_ref;         /* field 2 */
    float    start_humid;       /* field 3 */
    float    humid_high_limit;  /* field 4 */
} LpCure;

/* Mirrors ClimacellSettings (settings.proto field 9, envelope tag 48). */
typedef struct {
    uint32_t efficiency;   /* field 1 */
    uint32_t altitude;     /* field 2 */
    uint32_t alt_units;    /* field 3 */
    float    p_gain;       /* field 4 */
    float    i_gain;       /* field 5 */
    float    d_gain;       /* field 6 */
    float    u_limit;      /* field 7 */
} LpClimacell;

/* Mirrors ClimacellTimes (settings.proto field 10, envelope tag 49).
 * Holds a 48-entry hourly schedule (one per 30-min slot). */
#define LP_CLIMACELL_HOURS  48
typedef struct {
    uint32_t hourly_efficiency[LP_CLIMACELL_HOURS];   /* field 1 (packed) */
    uint32_t hourly_count;                             /* derived */
} LpClimacellTimes;

/* Mirrors HumidCtrlEntry (settings.proto). 8 fields per humidifier. */
typedef struct {
    uint32_t index;       /* field 1 */
    uint32_t mode;        /* field 2 */
    uint32_t cool_on;     /* field 3 */
    uint32_t cool_off;    /* field 4 */
    uint32_t recirc_on;   /* field 5 */
    uint32_t recirc_off;  /* field 6 */
    uint32_t refrig_on;   /* field 7 */
    uint32_t refrig_off;  /* field 8 */
} LpHumidCtrlEntry;

/* Mirrors HumidCtrlSettings (settings.proto field 11, envelope tag 50).
 * SAVE direction sends one entry; LOAD direction emits all 3. */
#define LP_HUMID_CTRL_MAX  3
typedef struct {
    LpHumidCtrlEntry entries[LP_HUMID_CTRL_MAX];   /* field 1 (repeated) */
    uint32_t         entries_count;                /* derived (always 3 on load) */
} LpHumidCtrl;

/* Mirrors OutsideAirSettings (settings.proto field 12, envelope tag 51). */
typedef struct {
    uint32_t mode;            /* field 1 */
    float    differential;    /* field 2 */
    uint32_t above_below;     /* field 3 */
    uint32_t temp_ref;        /* field 4 */
    uint32_t calc_humid_max;  /* field 5 */
} LpOutsideAir;

/* Mirrors MiscSettings (settings.proto field 13, envelope tag 52). */
typedef struct {
    uint32_t refrig_mode;            /* field 1 */
    uint32_t defrost_interval;       /* field 2 */
    uint32_t defrost_duration;       /* field 3 */
    float    heat_temp_thresh;       /* field 4 */
    uint32_t cavity_target;          /* field 5 */
    uint32_t cavity_mode;            /* field 6 */
    float    cavity_diff;            /* field 7 */
    uint32_t cavity_duty_or_sensor;  /* field 8 */
    uint32_t cavity_standby_on;      /* field 9 */
    uint32_t kb_pref;                /* field 10 */
    uint32_t lights_fail_units;      /* field 11 */
    uint32_t enthalpy_off_pct;       /* field 12 */
} LpMisc;

/* Mirrors FailureSettings (settings.proto field 14, envelope tag 53).
 * 22 flat fields — see settings.proto for per-field semantics. */
typedef struct {
    uint32_t fan_mode;            /* 1  */
    uint32_t fan_timer;           /* 2  */
    uint32_t heat_mode;           /* 3  */
    uint32_t heat_timer;          /* 4  */
    uint32_t refrig_mode;         /* 5  */
    uint32_t refrig_timer;        /* 6  */
    uint32_t refrig_fail_mode;    /* 7  */
    uint32_t burner_mode;         /* 8  */
    uint32_t burner_timer;        /* 9  */
    uint32_t humid_timer;         /* 10 */
    uint32_t climacell_timer;     /* 11 */
    uint32_t lights_mode;         /* 12 */
    uint32_t lights_timer;        /* 13 */
    uint32_t lights_units;        /* 14 */
    uint32_t climacell_mode;      /* 15 */
    uint32_t refrig_stages_mode;  /* 16 */
    uint32_t refrig_stages_timer; /* 17 */
    uint32_t humid_mode;          /* 18 */
    uint32_t aux_mode;            /* 19 */
    uint32_t aux_timer;           /* 20 */
    uint32_t cavity_heat_mode;    /* 21 */
    uint32_t cavity_heat_timer;   /* 22 */
} LpFailure;

/* Mirrors FailureSettings2 (settings.proto field 15, envelope tag 54). */
typedef struct {
    uint32_t out_air_mode;     /* 1  */
    uint32_t out_air_timer;    /* 2  */
    uint32_t out_humid_mode;   /* 3  */
    uint32_t out_humid_timer;  /* 4  */
    uint32_t high_co2_mode;    /* 5  */
    uint32_t high_co2_timer;   /* 6  */
    uint32_t co2_setpt;        /* 7  */
    uint32_t low_humid_mode;   /* 8  */
    uint32_t low_humid_timer;  /* 9  */
    uint32_t low_humid_set;    /* 10 */
    uint32_t plen_sen_mode;    /* 11 */
    uint32_t plen_sen_timer;   /* 12 */
    float    plen_sen_diff;    /* 13 */
} LpFailure2;

/* Mirrors TempAlarmSettings (settings.proto field 16, envelope tag 55). */
typedef struct {
    float    low_temp;     /* 1 */
    uint32_t low_timer;    /* 2 */
    float    high_temp;    /* 3 */
    uint32_t high_timer;   /* 4 */
    float    cure_low;     /* 5 */
    float    cure_high;    /* 6 */
} LpTempAlarm;

/* Mirrors DoorSettings (settings.proto field 19, envelope tag 56).
 * Note: SettingsUpdate sub-field for SAVE is 19 (NOT envelope tag). */
typedef struct {
    float    p_gain;             /* 1 */
    float    i_gain;             /* 2 */
    float    d_gain;             /* 3 */
    float    u_limit;             /* 4 */
    uint32_t actuator_time;      /* 5 */
    uint32_t cool_air_cycle;     /* 6 */
    uint32_t manual_pct;         /* 7 — operator target % when remote_off=MANUAL */
    uint32_t manual_timeout_mins;/* 8 — auto-revert to AUTO after N minutes; 0 = stay manual forever */
} LpDoor;

/* Mirrors CureLimitSettings (settings.proto field 17, envelope tag 57). */
typedef struct {
    float    temp_low_limit;   /* 1 */
    float    temp_high_limit;  /* 2 */
} LpCureLimit;

/* Mirrors UserLogSettings (settings.proto field 22, envelope tag 58). */
typedef struct {
    uint32_t interval_minutes;  /* 1 */
    uint32_t enabled;           /* 2 */
} LpUserLog;

/* Mirrors PidSettings (settings.proto field 23, envelope tag 59).
 * Single-equipment PID write; eq_index selects which equipment. */
typedef struct {
    uint32_t eq_index;   /* 1 */
    float    p_gain;     /* 2 */
    float    i_gain;     /* 3 */
    float    d_gain;     /* 4 */
    float    u_limit;    /* 5 */
    uint32_t wrap;       /* 6 */
} LpPid;

/* Mirrors MasterSlaveSettings (settings.proto field 25, envelope tag 60).
 * AS2 only had {mode, master_ip}. Constellation extends this with two
 * peer lists so confirmed-delivery master/slave can validate and
 * fan-out without external state — see masterSlaveSync.ts in the
 * bridge and lp_remote_outside for the consumption side. */
#define LP_MS_PEER_MAX 8
typedef struct {
    uint32_t mode;                          /* 1 — 0=Standalone, 1=Master, 2=Slave */
    char     master_ip[LP_SET_STR_MAX];     /* 2 */
    /* slave_ips (3) — Master: list of slave panel IPs to push to. */
    uint32_t slave_count;
    char     slave_ips[LP_MS_PEER_MAX][LP_SET_STR_MAX];
    /* allowed_masters (4) — Slave: source-IP allowlist for incoming pushes. */
    uint32_t allowed_count;
    char     allowed_masters[LP_MS_PEER_MAX][LP_SET_STR_MAX];
} LpMasterSlave;

/* Mirrors HttpPortSettings (settings.proto field 33, envelope tag 61). */
typedef struct {
    uint32_t port;   /* 1 */
} LpHttpPort;

/* Mirrors PublicAddressSettings (settings.proto field 34, envelope tag 62). */
typedef struct {
    uint32_t oct1;   /* 1 */
    uint32_t oct2;   /* 2 */
    uint32_t oct3;   /* 3 */
    uint32_t oct4;   /* 4 */
} LpPublicAddress;

/* Mirrors SysModeSettings (settings.proto field 36, envelope tag 63).
 * Wire payload is a single SystemMode enum (uint32 varint). */
typedef struct {
    uint32_t mode;   /* 1 */
} LpSysMode;

/* Mirrors PidLogSettings (settings.proto field 37, envelope tag 64). */
typedef struct {
    uint32_t wrap;        /* 1 */
    uint32_t log_doors;   /* 2 */
    uint32_t log_refrig;  /* 3 */
} LpPidLog;

/* Mirrors ServiceInfoUpdate (settings.proto field 31, envelope tag 65). */
typedef struct {
    char dealer_name [LP_SET_STR_MAX];   /* 1 */
    char dealer_phone[LP_SET_STR_MAX];   /* 2 */
    char dealer_email[LP_SET_STR_MAX];   /* 3 */
    char notes       [LP_SET_STR_MAX];   /* 4 */
} LpServiceInfo;

/* Mirrors EmailSettings (settings.proto field 27, envelope tag 66). */
typedef struct {
    char     server    [LP_SET_STR_MAX];   /* 1 */
    uint32_t port;                         /* 2 */
    char     username  [LP_SET_STR_MAX];   /* 3 */
    char     password  [LP_SET_STR_MAX];   /* 4 */
    char     from_addr [LP_SET_STR_MAX];   /* 5 */
    char     to_addr   [LP_SET_STR_MAX];   /* 6 */
    uint32_t enabled;                      /* 7 */
    uint32_t auth_type;                    /* 8 */
    char     display_id[LP_SET_STR_MAX];   /* 9 */
} LpEmail;

/* Mirrors GraphFavoriteSettings (settings.proto field 24, envelope tag 67).
 * Wire layout: packed-repeated uint32 on field 1, count derived. */
#define LP_GRAPH_FAV_MAX  16
typedef struct {
    uint32_t favorites[LP_GRAPH_FAV_MAX];   /* field 1 (packed) */
    uint32_t count;                          /* derived */
} LpGraphFavorites;

/* Mirrors AlertSettings (settings.proto field 28, envelope tag 68).
 * Wire layout: packed-repeated uint32 on field 1, count derived.
 * Slot is meaningful — don't suppress flags=0; the array length carries
 * the alarm-class index. */
#define LP_ALERT_MAX  32
typedef struct {
    uint32_t flags[LP_ALERT_MAX];   /* field 1 (packed) */
    uint32_t count;                  /* derived */
} LpAlert;

/* Mirrors BayNameSettings (settings.proto field 35, envelope tag 69).
 * Wire layout: repeated string on field 1, max 8 names × 24 chars. */
#define LP_BAYNAME_MAX     8
#define LP_BAYNAME_STRMAX  24
typedef struct {
    char     names[LP_BAYNAME_MAX][LP_BAYNAME_STRMAX];   /* field 1 */
    uint32_t count;                                       /* derived */
} LpBayName;

/* Mirrors AccountSettings (accounts.proto, settings.proto field 38,
 * envelope tag 29).
 * Wire layout per UserAccount submsg: { slot=1, user_id=2 }.
 * Parent: { repeated UserAccount users=1, user_count=2,
 *           bool password_defined=3 }.
 * Cap at 16 accounts × 24-char user_id (legacy AS2 used 8). */
#define LP_ACCOUNT_MAX      16
#define LP_ACCOUNT_STRMAX   24
typedef struct {
    uint32_t slot;                       /* field 1 (force) */
    char     user_id[LP_ACCOUNT_STRMAX]; /* field 2 */
} LpUserAccount;
typedef struct {
    LpUserAccount users[LP_ACCOUNT_MAX];  /* field 1 (repeated submsg) */
    uint32_t      count;                   /* field 2 */
    uint32_t      password_defined;        /* field 3 (bool 0/1) */
} LpAccount;

/* Mirrors IoConfig (io.proto, settings.proto field 39, envelope tag 24).
 * Equipment-indexed map of port id assignments. PID = board*12+pin+1.
 * Cap at 16 boards × 12 pins = 192 entries (multi-orbit ceiling).
 * Both arrays are length-authoritative — output_count and input_count
 * track how many entries the UI actually sent. */
#define LP_IO_MAP_MAX  192
typedef struct {
    uint32_t output_map[LP_IO_MAP_MAX];   /* field 1 (packed) */
    uint32_t input_map[LP_IO_MAP_MAX];    /* field 2 (packed) */
    uint32_t board_count;                  /* field 3 */
    uint32_t output_count;                 /* derived */
    uint32_t input_count;                  /* derived */
} LpIoConfig;

/* IoEntry — one per equipment slot (io.proto IoEntry, also envelope tag
 * 25 IoDefinition.entries[]). Mirrors AS2 `Settings.EquipIo[i]` layout.
 *
 * Population strategy:
 *   - At factory-reset / first boot we seed the static defaults below
 *     (mode/io_type/renamable from AS2 EquipDefine() — name stays empty
 *     and the UI falls back to its i18n label until the user renames).
 *   - The IoNameUpdate proto message (settings update sfield 40, see
 *     lp_settings.c::LpSettings_ApplyIoName) writes one entry's name in
 *     place. Renamability is enforced from the same defaults table.
 *   - LpSettings_BuildIoDefinitionBody emits all populated entries on
 *     the cadence so UI hydrates names + metadata via the proto stream.
 *
 * Capacity: 64 covers the AS2 EQUIPMENT_IO enum (EQ_TOTAL_IO = 58) with
 * headroom. Wire format is repeated IoEntry, so a tighter cap on count
 * would just zero-suppress the unused slots — we keep the slot index
 * field-1 force-encoded so index 0 is decodable. */
#define LP_IO_ENTRIES_MAX 64
typedef struct {
    uint32_t index;                          /* field 1 (force-encoded — 0 valid) */
    char     name[LP_SET_STR_MAX];           /* field 2 */
    uint32_t mode;                           /* field 3 (SYSTEM_MODE) */
    uint32_t io_pin;                         /* field 4 (legacy EquipIo.Output) */
    uint32_t renamable;                      /* field 5 (bool) */
    uint32_t visible;                        /* field 6 (bool) */
    uint32_t io_type;                        /* field 7 (IO_OPTION enum, force-encoded) */
    uint32_t populated;                      /* derived: 1 once seeded or applied */
} LpIoEntry;

typedef struct {
    LpIoEntry entries[LP_IO_ENTRIES_MAX];
    uint32_t  count;                          /* highest populated index + 1 */
} LpIoDefinition;

/* RemoteOff (operator panel-switch override) — eq-indexed.
 *
 * Phase-E1 LP-side mirror of legacy AS2 `Settings.RemoteOff[]`. Indexed
 * by EQUIPMENT_IO eq_index (NOT by the 20-entry RO_* table — the
 * legacy code conflates them and the bridge's BUTTON_TO_RO map already
 * mirrors that, so eq_index == ro_index for the slots the UI cares
 * about). One byte per slot, value range:
 *   0 = REMOTE_AUTO    (controller decides)
 *   1 = REMOTE_OFF     (forced off)
 *   2 = REMOTE_MANUAL  (forced on)
 *   3 = REMOTE_SYSSTOP (latching emergency stop, set only by SystemCmd 5)
 *
 * Persistence: top-level save-blob field 79; on-wire the same data
 * rides out inside EquipmentStatus (envelope tag 11 / field 11). */
typedef struct {
    uint8_t state[LP_IO_ENTRIES_MAX];
} LpRemoteOff;

/* Mirrors LoadMonitorSettings (settings.proto field 20, OSPI blob field 70).
 * — the "70" is internal to LpSettings_Serialize's save format, NOT
 * envelope tag 70 (LogRecord) on the UART wire.
 * 15 fields — 4 strings + 9 uint32s + 2 floats. Largest single page after
 * Email; needs a dedicated scratch buffer in Serialize for the same
 * reason. */
typedef struct {
    char     bay1_label[LP_SET_STR_MAX];   /* 1 */
    char     bay2_label[LP_SET_STR_MAX];   /* 2 */
    uint32_t bay_count;                    /* 3 */
    uint32_t enabled;                      /* 4 */
    char     units      [LP_SET_STR_MAX];  /* 5 */
    uint32_t sensor_type;                  /* 6 */
    char     pipe_label [LP_SET_STR_MAX];  /* 7 */
    uint32_t pipe_count;                   /* 8 */
    uint32_t interval;                     /* 9 */
    uint32_t auto_start;                   /* 10 */
    uint32_t threshold;                    /* 11 */
    uint32_t mode;                         /* 12 */
    float    low_limit;                    /* 13 */
    float    high_limit;                   /* 14 */
    uint32_t total_count;                  /* 15 */
} LpLoadMonitor;

/* RuntimeEntry submessage (RuntimeSettings field 1, OSPI blob field 71).
 * — "71" is internal to the LP's OSPI save blob, NOT envelope tag 71
 * (ActivityEvent).
 * Defined in equipment.proto — { uint32 slot=1, uint32 mode=2 }.
 * NOTE proto has slot force-encoded (slot 0 is meaningful) — we mirror
 * that by using a dedicated emit_uint32_force helper for slot. */
typedef struct {
    uint32_t slot;   /* field 1 */
    uint32_t mode;   /* field 2 */
} LpRuntimeEntry;

/* Mirrors RuntimeSettings (OSPI blob field 71). 48 half-hour slots × mode.
 * Wire-authoritative repeated submsg on field 1, same pattern as Refrig
 * stages. settings.options caps `entries` at max_count:32, but legacy
 * AS2 covers 48 half-hour slots — keep 48 for a future-proof schedule. */
#define LP_RUNTIME_MAX_ENTRIES  48
typedef struct {
    LpRuntimeEntry entries[LP_RUNTIME_MAX_ENTRIES];
    uint32_t       count;
} LpRuntime;

/* AuxRule submessage (AuxProgramSettings field 6, envelope tag 72).
 * 7 fields — { type, io_index, state, op, sensor_val(float), and_or, ref_index }. */
typedef struct {
    uint32_t type;        /* 1 */
    uint32_t io_index;    /* 2 */
    uint32_t state;       /* 3 */
    uint32_t op;          /* 4 */
    float    sensor_val;  /* 5 */
    uint32_t and_or;      /* 6 */
    uint32_t ref_index;   /* 7 */
} LpAuxRule;

/* Mirrors AuxProgramSettings (envelope tag 72) — splice-by-aux_index.
 * SAVE direction: UI sends ONE aux channel at a time; LOAD direction:
 * emit all populated channels. Up to 8 aux channels (matches PwmChannel
 * fan-out and legacy AUX_PROGRAM_COUNT). Each channel may carry up to 4
 * AuxRule conditions (settings.options max_count:4). */
#define LP_AUX_PROGRAM_MAX  8
#define LP_AUX_RULES_MAX    4
typedef struct {
    uint32_t  aux_index;     /* 1 */
    uint32_t  eq_index;      /* 2 */
    uint32_t  duty_cycle;    /* 3 */
    uint32_t  period;        /* 4 */
    uint32_t  units;         /* 5 */
    LpAuxRule rules[LP_AUX_RULES_MAX];   /* 6 — wire-authoritative per-channel */
    uint32_t  rules_count;
    uint32_t  populated;     /* derived: 1 once channel has been written */
} LpAuxProgram;

typedef struct {
    LpAuxProgram channels[LP_AUX_PROGRAM_MAX];
} LpAuxProgramSet;

/* SensorConfig submessage (AnalogBoardSettings field 4, envelope tag 73).
 * 4 fields — { slot, label, offset(float), disabled }. settings.options
 * caps sensors-per-board at 4. */
typedef struct {
    uint32_t slot;       /* 1 (force-encoded — slot 0 is valid) */
    char     label[LP_SET_STR_MAX];   /* 2 */
    float    offset;     /* 3 */
    uint32_t disabled;   /* 4 */
    uint32_t populated;  /* derived */
} LpSensorConfig;

/* Mirrors AnalogBoardSettings (envelope tag 73) — splice-by-address.
 * SAVE: UI sends one board; LOAD: emit per-board. Up to 8 boards
 * (legacy SettingsTypes ANALOG_BOARD_COUNT_MAX = 8). */
#define LP_ANALOG_BOARD_MAX  8
#define LP_SENSORS_PER_BOARD 4
typedef struct {
    uint32_t       address;                                   /* 1 (force) */
    char           label[LP_SET_STR_MAX];                     /* 2 */
    uint32_t       disabled;                                  /* 3 */
    LpSensorConfig sensors[LP_SENSORS_PER_BOARD];             /* 4 */
    uint32_t       sensors_count;
    uint32_t       populated;                                  /* derived */
} LpAnalogBoard;

typedef struct {
    LpAnalogBoard boards[LP_ANALOG_BOARD_MAX];
} LpAnalogBoardSet;

/* PwmChannel submessage (PwmChannelSettings field 1, envelope tag 74).
 * 4 fields — { index, enabled, port, duty }. settings.options caps
 * channels at 8. */
typedef struct {
    uint32_t index;      /* 1 (force) */
    uint32_t enabled;    /* 2 */
    uint32_t port;       /* 3 */
    uint32_t duty;       /* 4 */
    uint32_t populated;  /* derived */
} LpPwmChannelEntry;

#define LP_PWM_CHANNEL_MAX  8
typedef struct {
    LpPwmChannelEntry channels[LP_PWM_CHANNEL_MAX];
} LpPwmChannelSet;

/* OrbitRole assignment table — operator picks "Storage / Door / Refrig"
 * per orbit slot from the Level 2 IO Config page. The CONTROLLER LP
 * persists the table to OSPI (via the LpSettings ping-pong save path)
 * and emits each board's `role` in OrbitStatus (envelope tag 120,
 * OrbitBoardStatus.role = field 10) so the bridge / UI sees the
 * assignment survive reboot.
 *
 * Wire shape (top-level save-blob field 78, envelope-side currently
 * not transmitted as a settings page — bridge already knows roles via
 * OrbitStatus push).  Inner = repeated submsg, one per assigned slot:
 *   1  uint32  slot          (force-varint — slot 0 is valid)
 *   2  uint32  role          (force-varint — 0=UNASSIGNED is valid)
 *   3  uint32  zone_id       (suppressed when 0)
 *   4  int32   legacy_slot   (suppressed when -1 / zigzag)
 *   5  uint32  refrig_stage  (suppressed when 0)
 *
 * Sized at 16 to match the OrbitRoleAssign protocol allowance even
 * though ORBIT_CLIENT_MAX_ORBITS is 5 today — keeps the wire format
 * stable when the orbit count grows. */
#define LP_ORBIT_ROLE_MAX  16
typedef struct {
    uint8_t  role;          /* 0=UNASSIGNED 1=STORAGE 2=DOOR 3=REFRIG */
    uint8_t  zone_id;       /* 0..15 */
    int8_t   legacy_slot;   /* -1 = standalone */
    uint8_t  refrig_stage;  /* 0..7 (REFRIG only) */
    uint8_t  populated;     /* derived: 1 if operator has assigned this slot */
} LpOrbitRoleEntry;

typedef struct {
    LpOrbitRoleEntry slots[LP_ORBIT_ROLE_MAX];
} LpOrbitRoleSet;

/* Per-AO equipment program — operator picks (Level 2 PWM page) which
 * equipment a given orbit AO drives. Persisted in OSPI as field 80 of
 * the LpSettings save blob. Up to 16 slots × 2 channels — every slot
 * implicitly UNUSED (0) until the operator assigns it. */
#define LP_AO_EQUIP_SLOT_MAX  LP_ORBIT_ROLE_MAX
#define LP_AO_EQUIP_CH_MAX    2
typedef struct {
    /* equip[slot][ch] = ao_equip_t value:
     *   0 = AO_EQUIP_UNUSED      (default)
     *   1 = AO_EQUIP_FAN_SPEED
     *   2 = AO_EQUIP_DOORS
     *   3 = AO_EQUIP_REFRIG
     *   4 = AO_EQUIP_BURNER
     * Mirrors ao_equip_t in Nova_Firmware/Platform/nova_fan_output.h. */
    uint8_t equip[LP_AO_EQUIP_SLOT_MAX][LP_AO_EQUIP_CH_MAX];
} LpAoEquipSet;

/* Top-level Settings aggregate. As more pages land (Climacell, Humid,
 * ...), add nested structs here and extend LpSettings_Serialize. */
typedef struct {
    LpBasicSetup     basic;
    LpPlenum         plenum;
    LpFanSpeed       fan_speed;
    LpFanBoost       fan_boost;
    LpRampRate       ramp_rate;
    LpRefrig         refrig;
    LpBurner         burner;
    LpCo2            co2;
    LpCure           cure;
    LpClimacell      climacell;
    LpClimacellTimes climacell_times;
    LpHumidCtrl      humid_ctrl;
    LpOutsideAir     outside_air;
    LpMisc           misc;
    LpFailure        failure;
    LpFailure2       failure2;
    LpTempAlarm      temp_alarm;
    LpDoor           door;
    LpCureLimit      cure_limit;
    LpUserLog        user_log;
    LpPid            pid;
    LpMasterSlave    master_slave;
    LpHttpPort       http_port;
    LpPublicAddress  public_address;
    LpSysMode        sys_mode;
    LpPidLog         pid_log;
    LpServiceInfo    service_info;
    LpEmail          email;
    LpGraphFavorites graph_favorites;
    LpAlert          alert;
    LpBayName        bay_name;
    LpLoadMonitor    load_monitor;
    LpRuntime        runtime;
    LpAuxProgramSet  aux_program;
    LpAnalogBoardSet analog_board;
    LpPwmChannelSet  pwm_channel;
    LpAccount        account;
    LpIoConfig       io_config;
    LpIoDefinition   io_definition;
    LpOrbitRoleSet   orbit_role;
    LpAoEquipSet     ao_equip;
    LpRemoteOff      remote_off;
} LpSettingsData;

/* Initialize the struct to factory defaults. Called from main() at
 * boot, BEFORE LpSettings_Init() so Restore() can overwrite. */
void           LpSettings_DataInit(void);

/* Read-only accessor for cadence emit and decode handlers. */
const LpSettingsData *LpSettings_DataGet(void);

/* Apply one decoded BasicSetupUpdate sub-msg (settings.proto field 32
 * payload) to the in-RAM struct. Returns true if anything changed. */
bool           LpSettings_ApplyBasicSetup(const uint8_t *payload, size_t len);

/* Serialize the whole struct to a save blob. Buffer must hold at
 * least LP_SETTINGS_BLOB_MAX bytes (see lp_settings_store.h). Returns
 * the number of bytes written, or 0 on overflow. */
size_t         LpSettings_Serialize(uint8_t *buf, size_t bufsize);

/* Deserialize a save blob (produced by LpSettings_Serialize) into the
 * in-RAM struct. Returns true on success. Called from
 * lp_settings_store::LpSettings_Restore() via a registered callback. */
bool           LpSettings_Deserialize(const uint8_t *buf, size_t len);

/* Build the on-wire BasicSetup envelope body (the inner sub-msg of
 * envelope field 20). Mirrors Settings.basic; 0/empty fields are
 * suppressed per proto3 to keep cadence emits short. Returns inner
 * length, or 0 on buffer overflow. */
size_t         LpSettings_BuildBasicSetupBody(uint8_t *buf, size_t bufsize);

/* Plenum (envelope tag 40 emit, SettingsUpdate field 1 RX). */
bool           LpSettings_ApplyPlenum(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildPlenumBody(uint8_t *buf, size_t bufsize);

/* FanSpeed (envelope tag 41 emit, SettingsUpdate field 2 RX). */
bool           LpSettings_ApplyFanSpeed(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildFanSpeedBody(uint8_t *buf, size_t bufsize);

/* FanBoost (envelope tag 42 emit, SettingsUpdate field 3 RX). */
bool           LpSettings_ApplyFanBoost(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildFanBoostBody(uint8_t *buf, size_t bufsize);

/* RampRate (envelope tag 43 emit, SettingsUpdate field 4 RX). */
bool           LpSettings_ApplyRampRate(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildRampRateBody(uint8_t *buf, size_t bufsize);

/* Refrig (envelope tag 44 emit, SettingsUpdate field 5 RX).
 * Includes nested repeated `stages` (RefrigStage submessages). */
bool           LpSettings_ApplyRefrig(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildRefrigBody(uint8_t *buf, size_t bufsize);

/* CMD_CLEAR_DIAG handler (envelope-82 SystemCmd cmd_type=6).
 * Mirrors legacy CtrlRefrigDiagClear(): zeroes RefrigStage[i].diagnostic
 * across all populated stages so the operator's "Clear" button on the
 * door-diagnostic row (DoorDiagRow.svelte) actually wipes the latched
 * diag state. Returns true if anything changed (caller should persist). */
bool           LpSettings_ClearRefrigDiag(void);

bool           LpSettings_ApplyBurner(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildBurnerBody(uint8_t *buf, size_t bufsize);

bool           LpSettings_ApplyCo2(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildCo2Body(uint8_t *buf, size_t bufsize);

bool           LpSettings_ApplyCure(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildCureBody(uint8_t *buf, size_t bufsize);

bool           LpSettings_ApplyClimacell(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildClimacellBody(uint8_t *buf, size_t bufsize);

bool           LpSettings_ApplyClimacellTimes(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildClimacellTimesBody(uint8_t *buf, size_t bufsize);

bool           LpSettings_ApplyHumidCtrl(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildHumidCtrlBody(uint8_t *buf, size_t bufsize);

bool           LpSettings_ApplyOutsideAir(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildOutsideAirBody(uint8_t *buf, size_t bufsize);

bool           LpSettings_ApplyMisc(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildMiscBody(uint8_t *buf, size_t bufsize);

bool           LpSettings_ApplyFailure(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildFailureBody(uint8_t *buf, size_t bufsize);

bool           LpSettings_ApplyFailure2(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildFailure2Body(uint8_t *buf, size_t bufsize);

bool           LpSettings_ApplyTempAlarm(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildTempAlarmBody(uint8_t *buf, size_t bufsize);

bool           LpSettings_ApplyDoor(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildDoorBody(uint8_t *buf, size_t bufsize);

/* CureLimit (envelope tag 57 emit, SettingsUpdate field 17 RX). */
bool           LpSettings_ApplyCureLimit(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildCureLimitBody(uint8_t *buf, size_t bufsize);

/* UserLog (envelope tag 58 emit, SettingsUpdate field 22 RX). */
bool           LpSettings_ApplyUserLog(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildUserLogBody(uint8_t *buf, size_t bufsize);

/* Pid (envelope tag 59 emit, SettingsUpdate field 23 RX). */
bool           LpSettings_ApplyPid(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildPidBody(uint8_t *buf, size_t bufsize);

/* MasterSlave (envelope tag 60 emit, SettingsUpdate field 25 RX). */
bool           LpSettings_ApplyMasterSlave(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildMasterSlaveBody(uint8_t *buf, size_t bufsize);

/* HttpPort (envelope tag 61 emit, SettingsUpdate field 33 RX). */
bool           LpSettings_ApplyHttpPort(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildHttpPortBody(uint8_t *buf, size_t bufsize);

/* PublicAddress (envelope tag 62 emit, SettingsUpdate field 34 RX). */
bool           LpSettings_ApplyPublicAddress(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildPublicAddressBody(uint8_t *buf, size_t bufsize);

/* SysMode (envelope tag 63 emit, SettingsUpdate field 36 RX). */
bool           LpSettings_ApplySysMode(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildSysModeBody(uint8_t *buf, size_t bufsize);

/* PidLog (envelope tag 64 emit, SettingsUpdate field 37 RX). */
bool           LpSettings_ApplyPidLog(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildPidLogBody(uint8_t *buf, size_t bufsize);

/* ServiceInfo (envelope tag 65 emit, SettingsUpdate field 31 RX). */
bool           LpSettings_ApplyServiceInfo(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildServiceInfoBody(uint8_t *buf, size_t bufsize);

/* Email (envelope tag 66 emit, SettingsUpdate field 27 RX). */
bool           LpSettings_ApplyEmail(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildEmailBody(uint8_t *buf, size_t bufsize);

/* GraphFavorites (envelope tag 67 emit, SettingsUpdate field 24 RX). */
bool           LpSettings_ApplyGraphFavorites(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildGraphFavoritesBody(uint8_t *buf, size_t bufsize);

/* Alert (envelope tag 68 emit, SettingsUpdate field 28 RX). */
bool           LpSettings_ApplyAlert(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildAlertBody(uint8_t *buf, size_t bufsize);

/* BayName (envelope tag 69 emit, SettingsUpdate field 35 RX). */
bool           LpSettings_ApplyBayName(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildBayNameBody(uint8_t *buf, size_t bufsize);

/* LoadMonitor (OSPI blob field 70 emit, SettingsUpdate field 20 RX). */
bool           LpSettings_ApplyLoadMonitor(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildLoadMonitorBody(uint8_t *buf, size_t bufsize);

/* Runtime (OSPI blob field 71 emit, SettingsUpdate field 18 RX).
 * Wire-authoritative repeated submsg of RuntimeEntry on field 1
 * (same pattern as RefrigStage). */
bool           LpSettings_ApplyRuntime(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildRuntimeBody(uint8_t *buf, size_t bufsize);

/* AuxProgram (envelope tag 72 emit, SettingsUpdate field 21 RX).
 * Splice-by-aux_index: SAVE sends one channel; LOAD emits all
 * populated channels back-to-back. */
bool           LpSettings_ApplyAuxProgram(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildAuxProgramBody(uint8_t *buf, size_t bufsize);

/* AnalogBoard (envelope tag 73 emit, SettingsUpdate field 29 RX).
 * Splice-by-address: SAVE sends one board; LOAD emits all populated. */
bool           LpSettings_ApplyAnalogBoard(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildAnalogBoardBody(uint8_t *buf, size_t bufsize);

/* PwmChannel (envelope tag 74 emit, SettingsUpdate field 30 RX).
 * Splice-by-index per nested channel; SAVE typically sends one. */
bool           LpSettings_ApplyPwmChannel(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildPwmChannelBody(uint8_t *buf, size_t bufsize);

/* Account (envelope tag 29 emit, SettingsUpdate field 38 RX).
 * Wire-authoritative: any non-empty payload that mentions field 1
 * REPLACES the whole user list. password_defined (field 3) is sticky
 * across saves that omit it. */
bool           LpSettings_ApplyAccount(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildAccountBody(uint8_t *buf, size_t bufsize);

/* IoConfig (envelope tag 24 emit, SettingsUpdate field 39 RX).
 * Wire-authoritative: any payload with field 1 or 2 REPLACES that
 * whole half (output or input). Both halves are packed varint arrays. */
bool           LpSettings_ApplyIoConfig(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildIoConfigBody(uint8_t *buf, size_t bufsize);

/* CMD_RESET_IO_CONFIG handler (envelope-82 SystemCmd cmd_type=8).
 * Clears every operator-assigned port, re-applies the cold-boot
 * soft default (EQ_POWER → DI2), and re-pins the hardware-only
 * inputs (DI1 = EQ_AUX_LOW_LIMIT, DI11 = EQ_ESTOP). Operator-
 * renamed equipment names in io_definition.entries[] are preserved
 * (the legacy AS2 EquipIoInit() never touched names either). The
 * caller persists s_data to OSPI. */
void           LpSettings_ResetIoConfig(void);

/* IoDefinition (envelope tag 25 emit) + IoNameUpdate (SettingsUpdate
 * field 40 RX, see io.proto IoNameUpdate { uint32 index, string new_name }).
 *
 * Apply path: decodes a single rename event and updates entries[idx].name
 * if the entry's `renamable` flag is set. Returns true if anything
 * changed. Out-of-range indices and non-renamable entries are silently
 * rejected (matches AS2 StoreIoName semantics).
 *
 * Build path: emits all populated entries (one IoEntry sub-msg each)
 * inside the IoDefinition body. Names that are empty get suppressed
 * per proto3 — UI then falls back to its i18n default for that slot. */
bool           LpSettings_ApplyIoName(const uint8_t *payload, size_t len);
size_t         LpSettings_BuildIoDefinitionBody(uint8_t *buf, size_t bufsize);

/* Restore the IoDefinition table from a save blob (top-level field 77).
 * Internal — called only by LpSettings_Deserialize. The wire shape is
 * identical to the LpSettings_BuildIoDefinitionBody output. Re-seeds
 * defaults afterwards so firmware-added slots get their metadata. */
bool           LpSettings_ApplyIoDefinitionBlob(const uint8_t *buf, size_t len);

/* OrbitRole table (top-level save-blob field 78).
 *
 * Persistence-only: the bridge issues role assignments via envelope
 * tag 122 (OrbitRoleAssign) which `bridge_rx_callback` in main.c
 * decodes and applies via `LpSettings_SetOrbitRole` below. There is
 * no envelope-side broadcast of the whole table — each slot's `role`
 * rides out in OrbitBoardStatus.role (envelope tag 120, field 10).
 *
 * Returns true if anything changed (so the caller can decide to
 * Serialize+Save). Out-of-range slot is silently rejected. */
bool           LpSettings_SetOrbitRole(uint32_t slot,
                                       uint32_t role,
                                       uint32_t zone_id,
                                       int32_t  legacy_slot,
                                       uint32_t refrig_stage);

/* Read-only accessor used by build_orbit_board to populate
 * OrbitBoardStatus.role / zone_id / legacy_slot / refrig_stage. */
const LpOrbitRoleEntry *LpSettings_GetOrbitRole(uint32_t slot);

/* Save-blob (de)serialization for top-level field 78. */
size_t         LpSettings_BuildOrbitRoleBody(uint8_t *buf, size_t bufsize);
bool           LpSettings_ApplyOrbitRoleBlob(const uint8_t *buf, size_t len);

/* AoEquip table (top-level save-blob field 80).
 *
 * Persistence-only: the bridge issues per-AO assignments via envelope
 * tag 123 (AoEquipAssign) which `bridge_rx_callback` in main.c
 * decodes and applies via `LpSettings_SetAoEquip`. The table rides
 * back out to the UI inside OrbitBoardStatus.ao_equip (orbit.proto
 * field 15) emitted by `build_orbit_board`.
 *
 * Returns true on actual change. Out-of-range slot/channel is
 * silently rejected. equip values >255 are clamped (table is uint8). */
bool           LpSettings_SetAoEquip(uint32_t slot,
                                     uint32_t channel,
                                     uint32_t equip);

/* Read-only accessor for build_orbit_board (proto field 15).
 * Returns 0 (AO_EQUIP_UNUSED) for any out-of-range slot/channel. */
uint8_t        LpSettings_GetAoEquip(uint32_t slot, uint32_t channel);

/* Save-blob (de)serialization for top-level field 80. */
size_t         LpSettings_BuildAoEquipBody(uint8_t *buf, size_t bufsize);
bool           LpSettings_ApplyAoEquipBlob(const uint8_t *buf, size_t len);

/* RemoteOff (Phase E1) — equipment manual-override table.
 *
 * Set path: bridge sends EquipmentCmd (envelope tag 80) on operator
 * Auto/Off/Manual click; main.c::bridge_rx_callback decodes
 * { eq_index, new_state } and calls LpSettings_SetRemoteOff. Returns
 * true on actual change so the caller decides whether to persist.
 *
 * Read path: build_equipment_status_envelope() (also in main.c) walks
 * io_definition.entries[] and emits EquipState submsgs carrying
 * remote_off + a synthetic output_on derived from it.
 *
 * Persistence: top-level save-blob field 79 (packed bytes). */
bool           LpSettings_SetRemoteOff(uint32_t eq_index, uint8_t new_state);
uint8_t        LpSettings_GetRemoteOff(uint32_t eq_index);
size_t         LpSettings_BuildRemoteOffBody(uint8_t *buf, size_t bufsize);
bool           LpSettings_ApplyRemoteOffBlob(const uint8_t *buf, size_t len);

/* Setter for the live plenum temperature setpoint. Used by the
 * lp_engine_tick reverse-sync to capture SetRamp()'s in-tick
 * modifications to Settings.Plenum.TempSet back into the wire/UI
 * store so the bridge broadcasts the ramped value and OSPI persists
 * it on the next periodic flush. RAM-only update — does NOT trigger
 * an immediate OSPI save (the periodic flusher batches writes). */
void           LpSettings_SetPlenumTempSetpoint(float v);

#endif /* LP_SETTINGS_H */
