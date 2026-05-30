/*
 * orbit_state.h — Shared in-RAM state for the orbit-server roles
 *
 * This struct mirrors the orbit-simulator's `OrbitState` (TypeScript)
 * with only the fields the firmware actually needs. It's owned by
 * orbit_state.c and read/written by:
 *   - orbit_modbus_tcp.c via the orbit_storage register-map handlers
 *   - orbit_safemode.c   for the comm-loss watchdog
 *   - orbit_sensor_rtu.c (Phase 2) which writes sensor_block[]
 *   - orbit_vfd_rtu.c    (Phase 2) which fills vfd_regs[]
 *   - main.c             for boot init + uptime tick
 *
 * Concurrency: the lwIP TCP server runs on its own task and may call
 * the register handlers concurrently with the safemode watchdog. We
 * use a single FreeRTOS mutex (s_state_mtx in orbit_state.c) gated by
 * `OrbitState_Lock()` / `OrbitState_Unlock()` rather than per-field
 * atomics, because every transaction touches multiple fields and the
 * Modbus FCs are short. Hold time is bounded by the longest FC
 * handler (FC16 with up to 123 register writes) which is a few µs of
 * work — no priority-inversion concerns.
 *
 * Sensor block memory layout (HR 200..263, 64 regs total):
 *   Each analog board owns 4 contiguous sensor slots (HR 200..203
 *   = board 0, HR 204..207 = board 1, ...). Up to 16 boards.
 *   Values are int16_t in the simulator's "engineering" format:
 *     - Temperature : °C × 10 (e.g. 215 = 21.5 °C)
 *     - Humidity    : %RH × 10
 *     - CO2         : raw ppm
 *     - Pressure    : PSI × 10
 *     - Generic mA  : 0-100 raw
 *   Special: 0x7FFF (SENSOR_VAL_UNDEF) → no sensor / read failure.
 */
#ifndef ORBIT_STATE_H
#define ORBIT_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define ORBIT_DI_COUNT          10U
#define ORBIT_DO_COUNT          10U
#define ORBIT_AO_COUNT           2U
#define ORBIT_DC24V_COUNT        4U
#define ORBIT_SENSOR_BOARDS     16U
#define ORBIT_SENSORS_PER_BOARD  4U
#define ORBIT_SENSOR_BLOCK_SIZE (ORBIT_SENSOR_BOARDS * ORBIT_SENSORS_PER_BOARD)  /* 64 */

#define ORBIT_VFD_DRIVES         3U
#define ORBIT_VFD_REGS_PER_DRIVE 16U
#define ORBIT_VFD_BLOCK_SIZE     (ORBIT_VFD_DRIVES * ORBIT_VFD_REGS_PER_DRIVE)  /* 48 */
/* Width of the per-VFD-slot activity-timestamp array. The proto
 * `OrbitBoardStatus.vfd_activity_secs` is sized 24 to give headroom
 * for future drive expansion; today only slots 0..ORBIT_VFD_DRIVES-1
 * are ever updated. The remaining slots stay at 0 ("never seen"),
 * which the encoder converts to UINT32_MAX on the wire. */
#define ORBIT_VFD_ACTIVITY_SLOTS 24U
/* Operator label width (chars including NUL). Mirrors the
 * proto `OrbitBoardStatus.{output,input}_labels` max_size:24. */
#define ORBIT_LABEL_LEN          24U

#define ORBIT_SENSOR_VAL_UNDEF   ((int16_t)0x7FFF)

/* GDC role state — populated only when OrbitRole_Get() == ORBIT_ROLE_GDC.
 * See orbit_gdc.h for the HR map and cold-boot semantics. */
#define ORBIT_GDC_NUM_ACTUATORS  5U

typedef struct {
    uint16_t pos_x10;          /* current position 0..1000 (×10 of 0..100%) */
    uint16_t target_x10;       /* commanded target 0..1000                  */
    uint8_t  stage;            /* 0 = unassigned, else 1..5                 */
    bool     moving;
    bool     open_switch;      /* virtual limit switch (pos >= 99%)         */
    bool     close_switch;     /* virtual limit switch (pos <= 1%)          */
    bool     calibrated;
    uint16_t open_travel_ms;   /* 0 = uncalibrated (use default_travel_ms)  */
    uint16_t close_travel_ms;  /* 0 = uncalibrated                          */
    uint32_t cal_start_ticks;  /* xTaskGetTickCount() at phase entry        */
} GdcActuator;

typedef struct {
    GdcActuator act[ORBIT_GDC_NUM_ACTUATORS];
    uint16_t    door_pct_x10;          /* 0..1000 commanded fresh-air %     */
    uint16_t    default_travel_ms;     /* 0 = none → uncalibrated doors idle*/
    uint8_t     active_stage_count;    /* derived each tick                 */
    bool        calibrating;
    uint8_t     cal_phase;             /* 0 idle, 1 opening, 2 closing      */
} GdcState;

/* TRITON role state — populated only when OrbitRole_Get() == ORBIT_ROLE_TRITON.
 * See orbit_triton.h for the HR map, DO channel assignments, cold-boot
 * semantics, and safe-mode policy. */
#define ORBIT_TRITON_NUM_SENSORS    7U   /* sucP, disP, oilP, sucT, disT, llsT, ambT */
#define ORBIT_TRITON_NUM_UNLOADERS  2U
#define ORBIT_TRITON_ALARM_REGS     6U   /* 6×16 = 96 bit headroom (32 used) */

typedef struct {
    /* Per-sensor failure-mode (mode 0=ALARM_ONLY, 1=SAFE_OFF, 2=RUN_THROUGH)
     * + delay-seconds + live trip accumulator and latched flag. */
    uint8_t   mode;
    uint16_t  delay_sec;
    uint32_t  over_ms;       /* time the sensor has been continuously invalid */
    bool      tripped;
} TritonFailureCh;

typedef struct {
    /* Setpoints. Cold-boot all-zero (no operator config yet). */
    bool     enabled;
    uint8_t  refrigerant_type;        /* 0=R22 1=R134A 2=R404A 3=R407A 4=R407C 5=R410A */
    int16_t  cut_in_p_x10;            /* PSI×10 — start when sucP >= this */
    int16_t  cut_out_p_x10;           /* PSI×10 — stop when sucP <= this  */
    uint16_t min_off_time_sec;        /* anti-short-cycle */
    uint16_t min_runtime_sec;
    uint16_t warm_up_sec;
    uint16_t power_fail_min;
    int16_t  low_ambient_cutout_f_x10;
    uint8_t  pump_down_mode;          /* 0=NONE 1=SWITCH 2=REMOTE 3=CONT */
    int16_t  disc_high_unload_p_x10;  /* PSI×10 — head-pressure unload */
    int16_t  suc_low_unload_p_x10;    /* PSI×10 — low-suction unload */
    int16_t  superheat_target_f_x10;
    int16_t  superheat_low_f_x10;     /* alarm threshold */
    uint8_t  unloader_on_pct[ORBIT_TRITON_NUM_UNLOADERS];   /* demand% load */
    uint8_t  unloader_off_pct[ORBIT_TRITON_NUM_UNLOADERS];  /* demand% unload */
    int16_t  unloader_hp_unload_psi_x10[ORBIT_TRITON_NUM_UNLOADERS];
    int16_t  unloader_hp_load_psi_x10[ORBIT_TRITON_NUM_UNLOADERS];
    int16_t  unloader_lp_unload_psi_x10[ORBIT_TRITON_NUM_UNLOADERS];
    int16_t  unloader_lp_load_psi_x10[ORBIT_TRITON_NUM_UNLOADERS];
    uint8_t  unloader_normal;         /* 0=NO (DO=loaded), 1=NC (DO=unloaded) */

    /* Per-sensor failure-mode config + live trip state. Index order
     * matches the documented sensor list (see orbit_triton.h HR map).
     * Cold boot: mode=SAFE_OFF (1), delay=0, no trips. */
    TritonFailureCh failure[ORBIT_TRITON_NUM_SENSORS];

    /* Live sensor values mirrored from sensor_block[0..7]. Sourced from
     * the sensor RTU master once the analog boards are bound; until then
     * they remain undef and the failure machinery latches accordingly. */
    int16_t  sensor_value_x10[ORBIT_TRITON_NUM_SENSORS];
    bool     sensor_valid[ORBIT_TRITON_NUM_SENSORS];

    /* Live equipment state. */
    bool     compressor_on;
    uint8_t  compressor_status;   /* 0..14 — see TritonState.compressorStatus in TS */
    uint8_t  capacity_stage;      /* derived: # of unloaders adding capacity */
    bool     unloader_on[ORBIT_TRITON_NUM_UNLOADERS];
    bool     unloader_hp_forced[ORBIT_TRITON_NUM_UNLOADERS];
    bool     unloader_lp_forced[ORBIT_TRITON_NUM_UNLOADERS];
    uint32_t compressor_runtime_sec;
    uint32_t off_ms;              /* ms since compressor last stopped (ASC gate) */
    uint16_t pumpdown_sec_remaining;
    uint8_t  demand_pct;          /* 0..100 — from analog board ch 4 (pressure) */

    /* Saturation-temp derived (read-only, exposed for SCADA). */
    int16_t  suction_sat_t_f_x10;
    int16_t  superheat_f_x10;

    /* Alarm bitmaps. Indexed by TRITON_ALARM_BITS in orbit_triton.h.
     * Cold boot: all clear. */
    uint16_t alarm_active[ORBIT_TRITON_ALARM_REGS];
    uint16_t alarm_acked[ORBIT_TRITON_ALARM_REGS];

    /* Bitmask of FAIL_* codes whose mode is SAFE_OFF and whose timer
     * has tripped. Non-zero forces compressor off until ack-all. */
    uint32_t safe_off_mask;
} TritonState;

typedef struct {
    /* Identity (read once from device config at boot). */
    uint8_t  unit_id;          /* Modbus unit ID expected on the wire */
    uint16_t board_id;         /* Mirrors orbit-simulator state.id */

    /* Digital I/O. */
    bool     di[ORBIT_DI_COUNT];
    bool     do_[ORBIT_DO_COUNT];   /* underscore: `do` is a C keyword */
    bool     dc24v[ORBIT_DC24V_COUNT];
    bool     e_stop;

    /* Analog outputs as percent (0..100). The hardware DAC scale to
     * 4-20 mA happens in the AO write handler. */
    uint16_t ao_pct[ORBIT_AO_COUNT];
    /* AO mode: 0 = voltage, 1 = current. */
    uint8_t  ao_mode[ORBIT_AO_COUNT];

    /* Sensor block (HR 200..263). Filled by orbit_sensor_rtu (Phase 2)
     * or by stub init values in dev. */
    int16_t  sensor_block[ORBIT_SENSOR_BLOCK_SIZE];

    /* VFD pass-through registers (HR 100..147). Filled by
     * orbit_vfd_rtu (Phase 2). */
    uint16_t vfd_regs[ORBIT_VFD_BLOCK_SIZE];

    /* Status registers (HR 40000..40006 in the sim). */
    uint16_t cpu_temp_x10;     /* °C × 10 */
    uint32_t uptime_sec;       /* seconds since boot */
    bool     comm_lost;        /* watchdog timed out */
    bool     safe_mode;        /* safe-mode active */
    bool     sensor_activity;  /* set whenever HR 200+ is read */

    /* Last-poll timestamp, FreeRTOS ticks. Watchdog compares against
     * configurable timeout (default 5 s). */
    uint32_t last_poll_ticks;

    /* Per-sensor-board last successful poll, expressed in board uptime
     * seconds. Set by orbit_sensor_rtu.c on every parse-success path.
     * Cold boot = 0 ("never seen"); the OrbitBoardStatus encoder maps
     * 0 → UINT32_MAX on the wire so the bridge can distinguish
     * "absent" from "0 s ago". */
    uint32_t sensor_last_ok_secs[ORBIT_SENSOR_BOARDS];

    /* Per-VFD-slot last activity timestamp (board uptime sec). Today
     * the only path that touches this is the Modbus TCP slave's VFD
     * HR window (HR 100..147) — i.e. activity-from-bridge counts as
     * "alive". When the orbit_vfd_rtu master lands it will overwrite
     * with real RTU-success times. Slots ORBIT_VFD_DRIVES..23 stay 0
     * (no hardware drives); encoder maps 0 → UINT32_MAX. */
    uint32_t vfd_last_ok_secs[ORBIT_VFD_ACTIVITY_SLOTS];

    /* Operator-assigned labels for the 10 DO / 10 DI channels.
     * Cold-boot empty (zero-filled). Bridge-write path (FC16 to a
     * dedicated HR string region) is a follow-up — see
     * /memories/repo/proto-orbit-iostatus.md. Empty string at slot N
     * means "no label set"; UI falls back to "DO N" / "DI N". */
    char do_label[ORBIT_DO_COUNT][ORBIT_LABEL_LEN];
    char di_label[ORBIT_DI_COUNT][ORBIT_LABEL_LEN];

    /* GDC role state — only meaningful when role == ORBIT_ROLE_GDC.
     * Zeroed on cold boot (all actuators uncalibrated, stage 0). */
    GdcState gdc;

    /* TRITON role state — only meaningful when role == ORBIT_ROLE_TRITON.
     * Cold boot: enabled=false, all setpoints=0, all failure modes=SAFE_OFF
     * with delay=0, all alarms cleared, compressor + unloaders OFF. The
     * bridge programs setpoints + failure modes via FC16 before commanding
     * a run. See orbit_triton.h for the full HR map. */
    TritonState triton;
} OrbitStateData;

/* Boot-time init. Call AFTER LpDeviceConfig_Init() so unit_id/board_id
 * pick up provisioned values, but BEFORE the scheduler starts. */
void OrbitState_Init(void);

/* Acquire/release the shared mutex. Hold for the duration of any
 * multi-field read or write; do not hold across blocking calls. */
void OrbitState_Lock(void);
void OrbitState_Unlock(void);

/* Direct access. Caller MUST hold the lock. */
OrbitStateData *OrbitState_Get(void);

/* Touch the watchdog from a Modbus handler (already-locked OK). */
void OrbitState_TouchPoll(uint32_t now_ticks);

/* Return the board uptime in seconds (mirrors HR[40005..40006]).
 * Used by activity-timestamp paths (sensor RTU master, VFD HR
 * slave handler) to stamp `sensor_last_ok_secs[]` /
 * `vfd_last_ok_secs[]`. Caller does NOT need to hold the lock —
 * the value is a single 32-bit read incremented by main.c's
 * uptime-tick task. */
uint32_t OrbitState_GetUptimeSecs(void);

#endif /* ORBIT_STATE_H */
