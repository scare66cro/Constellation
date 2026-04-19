/**
 * nova_dataexc.c — Constellation data exchange (push + request/response)
 *
 * Replaces DataExc.c and the entire RTS/ACK/REPOST state machine.
 *
 * TX scheduling:
 *   - Every 1s: SystemStatus, EquipmentStatus
 *   - Every 5s: Heartbeat
 *   - On change: Warnings, Settings (flagged by application code)
 *   - On connect: All settings (triggered by DataRequest from bridge)
 *
 * RX dispatch:
 *   - Decode the Envelope's oneof field number
 *   - Dispatch to the appropriate handler
 *   - Send ACK back to bridge
 *
 * This file also contains the command handlers that map bridge requests
 * to firmware Store* functions. For the initial implementation, handlers
 * call the existing Store* functions by constructing the PostValue[] arrays
 * they expect. This will be refactored to direct struct writes once the
 * migration is validated.
 *
 * ─── apply_*() decoder invariants (see docs/firmware-bridge-protocol.md) ──
 *
 *   1. Bridge handler in `constellation-ui/server/src/index.ts` is the
 *      paired encoder for every apply_*() here. Wire layout MUST match.
 *
 *   2. Repeated-submessage decoders (apply_pwm_settings, apply_aux_program,
 *      apply_climacell_times, apply_refrig stages/defrosts) clear the
 *      destination array up front so a save is a full replacement, not
 *      an overlay onto stale entries.
 *
 *   3. Counters used to walk repeated submessages MUST be function-local.
 *      A `static` counter survives across saves and silently lands the
 *      next save's first entry in the slot after the previous save's
 *      last entry. The Aux Program rule list bug was this exact pattern.
 *
 *   4. The bridge force-encodes any field where 0 is a meaningful value
 *      (Mode=OFF, index=0, threshold=0, etc.). The decoders here can
 *      rely on those fields always being present in a save message.
 *
 *   5. Cross-coupled fields: some settings live in struct A but are
 *      saved via the page that owns struct B. Example: field 11 of
 *      RefrigSettings writes Settings.Co2.Purge.RefrigThresh, not a
 *      RefrigSettings field. Search "cross-coupled" comments for the
 *      full list.
 */
#include "nova_dataexc.h"
#include "nova_protocol.h"
#include "nova_messages.h"
#include "hal_orbit.h"
#include "debug.h"

#include "Settings.h"
#include "Controls.h"
#include "Analog_Input.h"
#include "Modes.h"
#include "PWM.h"
#include "RTC.h"
#include "SerialShift.h"
#include "States.h"
#include "Warnings.h"
#include "nova_fw_update.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ─── State ───────────────────────────────────────────────────────────── */

static NovaConnState s_state = NOVA_STATE_DISCONNECTED;
static uint32_t s_tick_count = 0;

/* Tick intervals (in 50ms ticks) */
#define TICK_STATUS_INTERVAL    20   /* 1s = 20 × 50ms */
#define TICK_HEARTBEAT_INTERVAL 100  /* 5s = 100 × 50ms */

/* Log record interval — derived from Settings.Log.User.Interval (in minutes).
 * Default 15 minutes = 15 * 60 * 20 = 18000 ticks. Recalculated when settings change. */
static uint32_t s_log_interval_ticks = 15 * 60 * 20;   /* default 15 min */
static uint32_t s_log_tick_counter = 0;

/* ─── Log record ring buffer ──────────────────────────────────────────── */
/* Survives RPi5 reboots: firmware keeps encoding log records into the ring.
 * When the serial link is up, the drain loop sends buffered records.
 * At 15-min intervals, 16 slots covers 4 hours of RPi5 downtime.           */
#define LOG_RING_SLOTS   16
#define LOG_RING_BUFSIZE 512   /* max encoded inner payload per record */

typedef struct {
    uint8_t  data[LOG_RING_BUFSIZE];
    size_t   len;       /* 0 = slot is empty */
} LogRingSlot;

static LogRingSlot s_log_ring[LOG_RING_SLOTS];
static uint32_t s_log_ring_wr = 0;   /* next slot to write */
static uint32_t s_log_ring_rd = 0;   /* next slot to read/send */
static uint32_t s_log_sequence = 0;  /* monotonic counter embedded in LogRecord */

/* How many records are buffered? */
static uint32_t log_ring_count(void)
{
    return (s_log_ring_wr - s_log_ring_rd) % LOG_RING_SLOTS;
}

/* ─── End ring buffer ─────────────────────────────────────────────────── */

/* Equipment state snapshot for activity event detection */
#define ACTIVITY_EQUIP_SLOTS 60   /* must be >= EQ_TOTAL_IO */
static uint8_t s_prev_equip_state[ACTIVITY_EQUIP_SLOTS];
static uint8_t s_prev_mode = 0xFF;   /* invalid initial to force first-change event */

/* Warning state snapshot for activity event detection */
#define ACTIVITY_WARN_SLOTS 64    /* must be >= NUM_WARNINGS */
static uint8_t s_prev_warn_state[ACTIVITY_WARN_SLOTS];

/* Change notification flags (set by application, cleared after send) */
volatile bool NovaDataExc_SettingsChanged = false;
volatile bool NovaDataExc_WarningsChanged = false;
volatile bool NovaDataExc_EquipChanged    = false;
volatile bool NovaDataExc_IoConfigChanged = false;

/* Strong override of the weak hook in Mini_IO/Application/Warnings.c.
 * Called from WarningsSet() / WarningsClear() so the next data-exchange
 * tick re-broadcasts the warning report to the bridge. */
void WarningsOnChanged(void)
{
    NovaDataExc_WarningsChanged = true;
}

/* ─── Protobuf field decoding helpers ─────────────────────────────────── */
/* Minimal protobuf decoder for extracting fields from incoming messages.
 * This is used to extract command parameters from bridge messages.
 * Will be replaced with nanopb decode calls once codegen is integrated. */

typedef struct {
    const uint8_t *buf;
    size_t pos;
    size_t len;
} PbDecoder;

static void pd_init(PbDecoder *d, const uint8_t *buf, size_t len)
{
    d->buf = buf;
    d->pos = 0;
    d->len = len;
}

static bool pd_has_data(const PbDecoder *d)
{
    return d->pos < d->len;
}

static uint64_t pd_varint(PbDecoder *d)
{
    uint64_t val = 0;
    int shift = 0;
    while (d->pos < d->len) {
        uint8_t byte = d->buf[d->pos++];
        val |= (uint64_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
        if (shift >= 64) break;
    }
    return val;
}

/* Read a tag: returns field number, sets wire_type. Returns 0 at end. */
static uint32_t pd_tag(PbDecoder *d, uint8_t *wire_type)
{
    if (!pd_has_data(d)) return 0;
    uint64_t tag = pd_varint(d);
    *wire_type = (uint8_t)(tag & 0x07);
    return (uint32_t)(tag >> 3);
}

/* Skip a field value based on wire type */
static void pd_skip(PbDecoder *d, uint8_t wire_type)
{
    switch (wire_type) {
        case 0: pd_varint(d); break;
        case 1: d->pos += 8; break;
        case 2: { uint64_t len = pd_varint(d); d->pos += (size_t)len; } break;
        case 5: d->pos += 4; break;
        default: d->pos = d->len; break;
    }
}

static uint32_t pd_uint32(PbDecoder *d)
{
    return (uint32_t)pd_varint(d);
}

static float pd_float(PbDecoder *d)
{
    float val = 0.0f;
    if (d->pos + 4 <= d->len) {
        uint32_t bits;
        memcpy(&bits, d->buf + d->pos, 4);
        memcpy(&val, &bits, 4);
        d->pos += 4;
    }
    return val;
}

/* Get a length-delimited field (string or submessage). Returns pointer and length. */
static const uint8_t* pd_bytes(PbDecoder *d, size_t *out_len)
{
    uint64_t len = pd_varint(d);
    const uint8_t *ptr = d->buf + d->pos;
    *out_len = (size_t)len;
    d->pos += (size_t)len;
    return ptr;
}

/* Copy a protobuf string field into a C buffer with safe truncation */
static void pd_string_into(PbDecoder *d, char *dst, size_t dst_max)
{
    size_t len;
    const uint8_t *ptr = pd_bytes(d, &len);
    if (len >= dst_max) len = dst_max - 1;
    memcpy(dst, ptr, len);
    dst[len] = '\0';
}

/* Read an int32 field (for atoi-compatible values stored as varint) */
static int32_t pd_int32(PbDecoder *d)
{
    return (int32_t)pd_varint(d);
}

/* ─── Forward declarations for handlers ───────────────────────────────── */
static void handle_equipment_cmd(const uint8_t *data, size_t len, uint32_t seq);
static void handle_system_cmd(const uint8_t *data, size_t len, uint32_t seq);
static void handle_settings_update(const uint8_t *data, size_t len, uint32_t seq);
static void handle_data_request(const uint8_t *data, size_t len, uint32_t seq);
static void handle_password_auth(const uint8_t *data, size_t len, uint32_t seq);
static void handle_io_name_update(const uint8_t *data, size_t len, uint32_t seq);
static void handle_log_query(const uint8_t *data, size_t len, uint32_t seq);
static void handle_network_node_cmd(const uint8_t *data, size_t len, uint32_t seq);
static void handle_refrig_diag_cmd(const uint8_t *data, size_t len, uint32_t seq);
static void handle_fw_begin_update(const uint8_t *data, size_t len, uint32_t seq);
static void handle_fw_data_chunk(const uint8_t *data, size_t len, uint32_t seq);
static void handle_fw_finalize(const uint8_t *data, size_t len, uint32_t seq);
static void handle_fw_activate(const uint8_t *data, size_t len, uint32_t seq);
static void handle_orbit_role_assign(const uint8_t *data, size_t len, uint32_t seq);

/* ─── Forward declarations for settings sub-handlers ──────────────────── */
static void apply_plenum(const uint8_t *d, size_t len);
static void apply_fan_speed(const uint8_t *d, size_t len);
static void apply_fan_boost(const uint8_t *d, size_t len);
static void apply_ramp_rate(const uint8_t *d, size_t len);
static void apply_refrig(const uint8_t *d, size_t len);
static void apply_burner(const uint8_t *d, size_t len);
static void apply_co2(const uint8_t *d, size_t len);
static void apply_cure(const uint8_t *d, size_t len);
static void apply_climacell(const uint8_t *d, size_t len);
static void apply_climacell_times(const uint8_t *d, size_t len);
static void apply_humid_ctrl(const uint8_t *d, size_t len);
static void apply_outside_air(const uint8_t *d, size_t len);
static void apply_misc(const uint8_t *d, size_t len);
static void apply_failures(const uint8_t *d, size_t len);
static void apply_failures2(const uint8_t *d, size_t len);
static void apply_temp_alarms(const uint8_t *d, size_t len);
static void apply_cure_limits(const uint8_t *d, size_t len);
static void apply_door(const uint8_t *d, size_t len);
static void apply_date_time(const uint8_t *d, size_t len);
static void apply_email(const uint8_t *d, size_t len);
static void apply_basic_setup(const uint8_t *d, size_t len);
static void apply_service_info(const uint8_t *d, size_t len);
static void apply_runtimes(const uint8_t *d, size_t len);
static void apply_accounts(const uint8_t *d, size_t len);
static void apply_alert_setup(const uint8_t *d, size_t len);
static void apply_io_config(const uint8_t *d, size_t len);
static void apply_log_settings(const uint8_t *d, size_t len);
static void apply_pid_settings(const uint8_t *d, size_t len);
static void apply_pwm_settings(const uint8_t *d, size_t len);
static void apply_master_slave(const uint8_t *d, size_t len);
static void apply_aux_program(const uint8_t *d, size_t len);
static void apply_analog_board(const uint8_t *d, size_t len);
static void apply_graph_favorites(const uint8_t *d, size_t len);

/* Stub for OrbitRoleAssign (MSG_ORBIT_ROLE_ASSIGN dispatcher path).
 * Role assignment is currently a no-op on Nova — orbit roles are
 * resolved via Settings.h Modbus mapping at boot.  Stub keeps the
 * dispatch table linkable without dragging in unused logic. */
static void handle_orbit_role_assign(const uint8_t *data, size_t len, uint32_t seq)
{
    (void)data; (void)len; (void)seq;
}

/* ─── Extern declarations for application functions ───────────────────── */
/* ClearAlarms declared in Timer.h (included via PWM.h) */
/* RequestSettingsSave declared in Settings.h */
extern void GetFactoryDefault(void);
extern void ClearIntervalTimers(void);
extern void EquipIoInit(void);
extern void EquipPwmInit(void);
/* SetDateTimeStr declared in RTC.h */
extern int  UserAcctAuth(char *id, char *pw);
extern void StoreSettings(void);
extern void CtrlFan(int speed);
extern void CtrlRefrigDiagClear(void);
extern void WarningsClear(void);
extern char CurrentMode;
extern char SystemState;
extern float PlenumTempAvg;
extern float StartTemp;
extern uint32_t UptimeSeconds;
/* XTimerVal and IntervalTimer declared in Timer.h (included via PWM.h) */
/* PIDCtrl is declared in Controls.h (included above) as:
   extern PID_CTRL PIDCtrl[NUM_PID_EQUIP]; */
extern void ReadAnalogBoards(char ReadType);
extern void NetworkNodeDelete(char *id);
extern void DiscoverAnalogBoardsRequest(unsigned int request);
/* SetIoConfig declared in SerialShift.h (included via Settings.h) */

/* ─── Command dispatch ────────────────────────────────────────────────── */

/* Extract the Envelope oneof field and dispatch to handler.
 * Envelope fields: 1=protocol_version, 2=seq, then oneof msg (field 10-102) */
static void dispatch_envelope(const uint8_t *payload, size_t payload_len)
{
    PbDecoder d;
    pd_init(&d, payload, payload_len);

    uint32_t seq = 0;
    uint32_t msg_field = 0;
    const uint8_t *msg_data = NULL;
    size_t msg_len = 0;

    /* Parse Envelope header fields */
    while (pd_has_data(&d)) {
        uint8_t wire_type;
        uint32_t field = pd_tag(&d, &wire_type);
        if (field == 0) break;

        switch (field) {
            case 1: /* protocol_version */
                pd_varint(&d);
                break;
            case 2: /* seq */
                seq = pd_uint32(&d);
                break;
            default:
                if (wire_type == 2) {
                    /* This is the oneof msg — length-delimited submessage */
                    msg_field = field;
                    msg_data = pd_bytes(&d, &msg_len);
                } else {
                    pd_skip(&d, wire_type);
                }
                break;
        }
    }

    if (msg_field == 0 || msg_data == NULL) return;

    /* Dispatch based on the oneof field number */
    switch (msg_field) {
        case MSG_EQUIPMENT_CMD:
            handle_equipment_cmd(msg_data, msg_len, seq);
            break;
        case MSG_SYSTEM_CMD:
            handle_system_cmd(msg_data, msg_len, seq);
            break;
        case MSG_SETTINGS_UPDATE:
            handle_settings_update(msg_data, msg_len, seq);
            break;
        case MSG_DATA_REQUEST:
            handle_data_request(msg_data, msg_len, seq);
            break;
        case MSG_PASSWORD_AUTH:
            handle_password_auth(msg_data, msg_len, seq);
            break;
        case MSG_IO_NAME_UPDATE:
            handle_io_name_update(msg_data, msg_len, seq);
            break;
        case MSG_REFRIG_DIAG_CMD:
            handle_refrig_diag_cmd(msg_data, msg_len, seq);
            break;
        case MSG_LOG_QUERY:
            handle_log_query(msg_data, msg_len, seq);
            break;
        case MSG_NETWORK_NODE_CMD:
            handle_network_node_cmd(msg_data, msg_len, seq);
            break;
        case MSG_FW_BEGIN_UPDATE:
            handle_fw_begin_update(msg_data, msg_len, seq);
            break;
        case MSG_FW_DATA_CHUNK:
            handle_fw_data_chunk(msg_data, msg_len, seq);
            break;
        case MSG_FW_FINALIZE_UPDATE:
            handle_fw_finalize(msg_data, msg_len, seq);
            break;
        case MSG_FW_ACTIVATE_BANK:
            handle_fw_activate(msg_data, msg_len, seq);
            break;
        case MSG_ORBIT_ROLE_ASSIGN:
            handle_orbit_role_assign(msg_data, msg_len, seq);
            break;
        default:
            NovaMsg_SendAck(seq, 5 /* ACK_UNKNOWN_CMD */);
            break;
    }
}

/* ─── Command handlers ────────────────────────────────────────────────── */

static void handle_equipment_cmd(const uint8_t *data, size_t len, uint32_t seq)
{
    PbDecoder d;
    pd_init(&d, data, len);

    uint32_t eq_index = 0;
    uint32_t new_state = 0;

    while (pd_has_data(&d)) {
        uint8_t wt;
        uint32_t field = pd_tag(&d, &wt);
        if (field == 0) break;
        switch (field) {
            case 1: eq_index  = pd_uint32(&d); break;
            case 2: new_state = pd_uint32(&d); break;
            default: pd_skip(&d, wt); break;
        }
    }

    /* Apply equipment toggle — same as StoreRemoteOff */
    if (eq_index < NUM_REMOTE_OFF) {
        Settings.RemoteOff[eq_index] = (char)new_state;
        NovaDataExc_EquipChanged = true;
    }

    NovaMsg_SendAck(seq, 0 /* ACK_OK */);
}

static void handle_system_cmd(const uint8_t *data, size_t len, uint32_t seq)
{
    PbDecoder d;
    pd_init(&d, data, len);

    uint32_t cmd_type = 0;
    uint32_t param = 0;

    while (pd_has_data(&d)) {
        uint8_t wt;
        uint32_t field = pd_tag(&d, &wt);
        if (field == 0) break;
        switch (field) {
            case 1: cmd_type = pd_uint32(&d); break;
            case 2: param    = pd_uint32(&d); break;
            default: pd_skip(&d, wt); break;
        }
    }

    /* Dispatch system commands — mirrors UI_ActionPosts array and SystemCmdType enum */
    switch (cmd_type) {
        case 1: /* CMD_CLEAR_ALARM */
            ClearAlarms(0);
            break;
        case 2: /* CMD_SET_DEFAULT — operator "Save Settings" */
            /* SR_REQUEST=1 enqueues the save; SR_CLEAR=0 would silently
             * cancel it.  Type 0 = ACTIVE_SETTINGS (operator save). */
            RequestSettingsSave(1 /*SR_REQUEST*/, 0 /*ACTIVE_SETTINGS*/);
            break;
        case 3: /* CMD_PANEL_DEFAULT — snapshot as panel defaults */
            RequestSettingsSave(1 /*SR_REQUEST*/, 1 /*PANEL slot*/);
            break;
        case 4: /* CMD_FACTORY_DEFAULT */
            GetFactoryDefault();
            /* Persist the reset so it survives reboot — mirrors
             * simulator's FactoryDefault=Restore behavior. */
            RequestSettingsSave(1 /*SR_REQUEST*/, 0 /*ACTIVE_SETTINGS*/);
            break;
        case 5: /* CMD_SYSTEM_STOP — Emergency system stop (latching).
                  * Sets all equipment to REMOTE_SYSSTOP except bay lights
                  * (lights persist their toggle state — acts as a latching
                  * three-way switch to prevent greening potatoes). */
            for (int i = 0; i < NUM_REMOTE_OFF; i++) {
                if (i == RO_LIGHTS1 || i == RO_LIGHTS2) continue;
                Settings.RemoteOff[i] = 3;  /* REMOTE_SYSSTOP */
            }
            NovaDataExc_EquipChanged = true;
            debug_printf("[SysCmd] SYSTEM_STOP: all equipment forced off (latching)\r\n");
            break;
        case 6: /* CMD_CLEAR_DIAG */
            CtrlRefrigDiagClear();
            break;
        case 7: /* CMD_FIND_BOARD */
            DiscoverAnalogBoardsRequest(AB_DISCOVER);
            break;
        case 8: /* CMD_RESET_IO_CONFIG */
            EquipIoInit();
            NovaDataExc_IoConfigChanged = true;
            break;
        case 9: /* CMD_RESET_PWM_CONFIG */
            EquipPwmInit();
            break;
        case 10: /* CMD_DELETE_NODES */
            for (int i = 0; i < NUM_NETWORK_NODES; i++) {
                char idx[4];
                snprintf(idx, sizeof(idx), "%d", i);
                NetworkNodeDelete(idx);
            }
            break;
        case 35: /* CMD_SD_CARD_INIT */
            /* SD card init — trigger re-init */
            break;
        case 42: /* CMD_NEXT_BOARD */
            /* Handled via data request for analog boards */
            if (param < ANALOG_BOARDS_PER_SYSTEM)
                NovaMsg_SendAnalogBoard(param);
            break;
        case 43: /* CMD_PREV_BOARD */
            if (param < ANALOG_BOARDS_PER_SYSTEM)
                NovaMsg_SendAnalogBoard(param);
            break;
        case 44: /* CMD_SAME_BOARD */
            if (param < ANALOG_BOARDS_PER_SYSTEM)
                NovaMsg_SendAnalogBoard(param);
            break;
        default:
            NovaMsg_SendAck(seq, 5 /* ACK_UNKNOWN_CMD */);
            return;
    }

    NovaMsg_SendAck(seq, 0 /* ACK_OK */);
}

static void handle_data_request(const uint8_t *data, size_t len, uint32_t seq)
{
    PbDecoder d;
    pd_init(&d, data, len);

    uint32_t request_type = 0;
    uint32_t param = 0;

    while (pd_has_data(&d)) {
        uint8_t wt;
        uint32_t field = pd_tag(&d, &wt);
        if (field == 0) break;
        switch (field) {
            case 1: request_type = pd_uint32(&d); break;
            case 2: param        = pd_uint32(&d); break;
            default: pd_skip(&d, wt); break;
        }
    }

    switch (request_type) {
        case 0: /* REQ_ALL_SETTINGS */
            s_state = NOVA_STATE_CONNECTED;
            NovaMsg_SendAck(seq, 0);
            NovaMsg_SendAllSettings();
            NovaMsg_SendFwBankInfo();
            NovaFwUpdate_ConfirmBoot();
            break;
        case 1: /* REQ_SYSTEM_STATUS */
            NovaMsg_SendSystemStatus();
            break;
        case 2: /* REQ_EQUIPMENT_STATUS */
            NovaMsg_SendEquipmentStatus();
            break;
        case 3: /* REQ_WARNINGS */
            NovaMsg_SendWarnings();
            break;
        case 4: /* REQ_IO_CONFIG */
            NovaMsg_SendIoConfig();
            NovaMsg_SendIoDefinition();
            break;
        case 5: /* REQ_SENSOR_DATA */
            NovaMsg_SendSensorData();
            break;
        case 6: /* REQ_ANALOG_BOARD */
            NovaMsg_SendAnalogBoard(param);
            break;
        case 7: /* REQ_AUX_PROGRAM */
            NovaMsg_SendAuxProgram();
            break;
        case 8: /* REQ_ORBIT_DISCOVERY */
            NovaMsg_SendOrbitDiscovery();
            break;
        default:
            NovaMsg_SendAck(seq, 5 /* ACK_UNKNOWN_CMD */);
            break;
    }
}

static void handle_settings_update(const uint8_t *data, size_t len, uint32_t seq)
{
    /* SettingsUpdate is a oneof — the field number tells us which settings type */
    PbDecoder d;
    pd_init(&d, data, len);

    while (pd_has_data(&d)) {
        uint8_t wt;
        uint32_t field = pd_tag(&d, &wt);
        if (field == 0) break;

        if (wt == 2) {
            /* Length-delimited submessage — field number = settings type */
            size_t sub_len;
            const uint8_t *sub_data = pd_bytes(&d, &sub_len);

            switch (field) {
                case  1: apply_plenum(sub_data, sub_len); break;
                case  2: apply_fan_speed(sub_data, sub_len); break;
                case  3: apply_fan_boost(sub_data, sub_len); break;
                case  4: apply_ramp_rate(sub_data, sub_len); break;
                case  5: apply_refrig(sub_data, sub_len); break;
                case  6: apply_burner(sub_data, sub_len); break;
                case  7: apply_co2(sub_data, sub_len); break;
                case  8: apply_cure(sub_data, sub_len); break;
                case  9: apply_climacell(sub_data, sub_len); break;
                case 10: apply_climacell_times(sub_data, sub_len); break;
                case 11: apply_humid_ctrl(sub_data, sub_len); break;
                case 12: apply_outside_air(sub_data, sub_len); break;
                case 13: apply_misc(sub_data, sub_len); break;
                case 14: apply_failures(sub_data, sub_len); break;
                case 15: apply_failures2(sub_data, sub_len); break;
                case 16: apply_temp_alarms(sub_data, sub_len); break;
                case 17: apply_cure_limits(sub_data, sub_len); break;
                case 19: apply_door(sub_data, sub_len); break;
                case 26: apply_date_time(sub_data, sub_len); break;
                case 27: apply_email(sub_data, sub_len); break;
                case 31: apply_service_info(sub_data, sub_len); break;
                case 32: apply_basic_setup(sub_data, sub_len); break;
                case 33: apply_runtimes(sub_data, sub_len); break;
                case 34: apply_accounts(sub_data, sub_len); break;
                case 35: apply_alert_setup(sub_data, sub_len); break;
                case 36: apply_io_config(sub_data, sub_len); break;
                case 37: apply_log_settings(sub_data, sub_len); break;
                case 38: apply_pid_settings(sub_data, sub_len); break;
                case 39: apply_pwm_settings(sub_data, sub_len); break;
                case 40: apply_master_slave(sub_data, sub_len); break;
                case 41: apply_aux_program(sub_data, sub_len); break;
                case 42: apply_analog_board(sub_data, sub_len); break;
                case 43: apply_graph_favorites(sub_data, sub_len); break;
                default:
                    /* Unhandled settings type — ACK OK but no-op */
                    break;
            }

            NovaDataExc_SettingsChanged = true;
            /* Mirror legacy DataExc.c behaviour: every settings POST queues
             * an OSPI save (the save loop in nova_thread_overrides.c is
             * debounced to 2s, so rapid edits coalesce into one write).
             * Without this, edits survive only until reboot — the explicit
             * "Save Settings" button is the only persistence trigger and
             * users won't know to press it after every change. */
            RequestSettingsSave(1 /*SR_REQUEST*/, 0 /*ACTIVE_SETTINGS*/);
            NovaMsg_SendAck(seq, 0 /* ACK_OK */);
            return;
        } else {
            pd_skip(&d, wt);
        }
    }

    NovaMsg_SendAck(seq, 1 /* ACK_INVALID_PARAM */);
}

static void handle_password_auth(const uint8_t *data, size_t len, uint32_t seq)
{
    /* PasswordAuth: field 1 = user_id (string), field 2 = password (string),
     *               field 3 = session (uint32) */
    PbDecoder d;
    pd_init(&d, data, len);

    char user_id[25] = {0};
    char password[25] = {0};

    while (pd_has_data(&d)) {
        uint8_t wt;
        uint32_t field = pd_tag(&d, &wt);
        if (field == 0) break;
        switch (field) {
            case 1: pd_string_into(&d, user_id, sizeof(user_id)); break;
            case 2: pd_string_into(&d, password, sizeof(password)); break;
            default: pd_skip(&d, wt); break;
        }
    }

    /* Authenticate — mirrors PasswordAuth() in StorePostData.c */
    signed char pgmLevel = -2;

    if (strcmp(password, "clear") == 0) {
        pgmLevel = 0;
    } else if (strcmp(password, "leveldown") == 0) {
        pgmLevel = 1;
    } else if (strcmp(password, Settings.FactoryBd) == 0) {
        pgmLevel = 2;
    } else if (strcmp(user_id, "login") == 0) {
        if (strcmp(password, Settings.LoginPw) == 0)
            pgmLevel = 0;
    } else if (UserAcctAuth(user_id, password)) {
        pgmLevel = 1;
    }

    /* Send PasswordResponse (MSG_PASSWORD_RESPONSE = 31) via NovaMsg */
    NovaMsg_SendPasswordResponse((int8_t)pgmLevel);
    NovaMsg_SendAck(seq, 0 /* ACK_OK */);
}

static void handle_io_name_update(const uint8_t *data, size_t len, uint32_t seq)
{
    /* IoNameUpdate: field 1 = eq_index (uint32), field 2 = name (string) */
    PbDecoder d;
    pd_init(&d, data, len);

    uint32_t eq_index = 0;
    char name[LOG_LABELS + 1] = {0};

    while (pd_has_data(&d)) {
        uint8_t wt;
        uint32_t field = pd_tag(&d, &wt);
        if (field == 0) break;
        switch (field) {
            case 1: eq_index = pd_uint32(&d); break;
            case 2: pd_string_into(&d, name, sizeof(name)); break;
            default: pd_skip(&d, wt); break;
        }
    }

    if (eq_index < EQ_TOTAL_IO && name[0] != '\0') {
        StringCopy(Settings.EquipIo[eq_index].Name, name, LOG_LABELS + 1);
        Settings.EquipIo[eq_index].Renamable = 1;
        NovaDataExc_IoConfigChanged = true;
    }

    NovaMsg_SendAck(seq, 0 /* ACK_OK */);
}

/* ─── Refrig diag command (MSG 81) ────────────────────────────────────── */
/* RefrigDiagCmd wire shape:
 *   field 1: stage_kind (uint32) — 0 = Stage[0..7], 1 = Defrost[0..1]
 *   field 2: index      (uint32) — 0..7 for stages, 0..1 for defrosts
 *   field 3: value      (uint32) — 1 = Off, 2 = On (mirrors StoreRefrigDiag)
 *
 * The legacy StoreRefrigDiag refuses to set diag mode unless the system
 * and refrig switches are ON.  The Nova path mirrors that behaviour by
 * checking the equivalent RemoteOff slots before applying.  Diag-clear-all
 * is a separate SystemCmd (CMD_CLEAR_DIAG, dispatched via case 6 in
 * handle_system_cmd → CtrlRefrigDiagClear()). */
static void handle_refrig_diag_cmd(const uint8_t *data, size_t len, uint32_t seq)
{
    PbDecoder d;
    pd_init(&d, data, len);

    uint32_t stage_kind = 0;  /* 0 = Stage, 1 = Defrost */
    uint32_t index      = 0;
    uint32_t value      = 0;

    while (pd_has_data(&d)) {
        uint8_t wt;
        uint32_t field = pd_tag(&d, &wt);
        if (field == 0) break;
        switch (field) {
            case 1: stage_kind = pd_uint32(&d); break;
            case 2: index      = pd_uint32(&d); break;
            case 3: value      = pd_uint32(&d); break;
            default: pd_skip(&d, wt); break;
        }
    }

    /* Reject if either system or refrigeration is in OFF/SysStop.  Auto
     * (0) and Manual (2) both permit diagnostic mode. */
    if (Settings.RemoteOff[RO_FAN]           == 1 ||
        Settings.RemoteOff[RO_FAN]           == 3 ||
        Settings.RemoteOff[RO_REFRIGERATION] == 1 ||
        Settings.RemoteOff[RO_REFRIGERATION] == 3) {
        NovaMsg_SendAck(seq, 1 /* ACK_REJECTED */);
        return;
    }

    /* Coerce legacy semantics: anything other than "On" (=2) means Off (=1). */
    uint8_t diag = (value == 2) ? 2 : 1;

    if (stage_kind == 0 && index < NUM_REFRIG_STAGES) {
        Settings.Refrig.Stage[index].Diagnostic = diag;
    } else if (stage_kind == 1 && index < NUM_DEFROST_STAGES) {
        Settings.Refrig.Defrost[index].Diagnostic = diag;
    } else {
        NovaMsg_SendAck(seq, 2 /* ACK_BAD_PARAM */);
        return;
    }

    /* Reset the diagnostic auto-clear timer so the bit doesn't time out
     * immediately (mirrors `IntervalTimer[IT_REFRIGDIAG] = XTimerVal`). */
    IntervalTimer[IT_REFRIGDIAG] = XTimerVal;

    NovaDataExc_EquipChanged = true;
    NovaMsg_SendAck(seq, 0);
}

/* ─── Log query (MSG 83) ──────────────────────────────────────────────── */
/* The bridge owns all log data in SQLite on the RPi5. Log queries from
 * the UI are served directly by the bridge — they never reach firmware.
 * If a LogQuery does arrive (e.g. file download), ACK it. */
static void handle_log_query(const uint8_t *data, size_t len, uint32_t seq)
{
    (void)data; (void)len;
    NovaMsg_SendAck(seq, 0);
}

/* ─── Network node command (MSG 85) ───────────────────────────────────── */
static void handle_network_node_cmd(const uint8_t *data, size_t len, uint32_t seq)
{
    PbDecoder d;
    pd_init(&d, data, len);

    uint32_t action = 0;
    char nodeId[SITE_ID_LEN + 1] = {0};
    char nodeIp[IP_ADD_LEN + 1] = {0};
    uint32_t slot = 0;

    while (pd_has_data(&d)) {
        uint8_t wt;
        uint32_t field = pd_tag(&d, &wt);
        if (field == 0) break;
        switch (field) {
            case 1: action = pd_uint32(&d); break;
            case 2: slot = pd_uint32(&d); break;
            case 3: pd_string_into(&d, nodeId, sizeof(nodeId)); break;
            case 4: pd_string_into(&d, nodeIp, sizeof(nodeIp)); break;
            default: pd_skip(&d, wt); break;
        }
    }

    switch (action) {
        case 1: /* ADD */
            if (slot < NUM_NETWORK_NODES) {
                StringCopy(Settings.Node[slot].ID, nodeId, SITE_ID_LEN + 1);
                StringCopy(Settings.Node[slot].IP, nodeIp, IP_ADD_LEN + 1);
            }
            break;
        case 2: /* DELETE */
            if (slot < NUM_NETWORK_NODES) {
                char idx[4];
                snprintf(idx, sizeof(idx), "%d", (int)slot);
                NetworkNodeDelete(idx);
            }
            break;
        case 3: /* DELETE ALL */
            for (int i = 0; i < NUM_NETWORK_NODES; i++) {
                char idx[4];
                snprintf(idx, sizeof(idx), "%d", i);
                NetworkNodeDelete(idx);
            }
            break;
    }

    NovaMsg_SendAck(seq, 0);
}

/* ─── Init ────────────────────────────────────────────────────────────── */

void NovaDataExc_Init(void)
{
    s_state = NOVA_STATE_DISCONNECTED;
    s_tick_count = 0;

    /* Register our envelope decoder as the protocol RX callback.
     * NovaProto_Init() must already have been called with the UART tx func.
     * We pass NULL for tx_func to keep the existing one — only update rx_cb. */
    NovaProto_SetRxCallback(dispatch_envelope);
}

/* ─── Periodic tick ───────────────────────────────────────────────────── */

void NovaDataExc_Tick(void)
{
    s_tick_count++;

    /* ─── Log record ring buffer: ALWAYS runs, even when disconnected ─── */
    /* Encode a snapshot into the ring when the interval fires.             */
    s_log_tick_counter++;
    if (s_log_tick_counter >= s_log_interval_ticks) {
        s_log_tick_counter = 0;
        LogRingSlot *slot = &s_log_ring[s_log_ring_wr % LOG_RING_SLOTS];
        size_t enc = NovaMsg_EncodeLogRecord(slot->data, LOG_RING_BUFSIZE,
                                              s_log_sequence++);
        if (enc > 0) {
            slot->len = enc;
            s_log_ring_wr++;
            /* If ring is full, advance read pointer (discard oldest) */
            if ((s_log_ring_wr - s_log_ring_rd) > LOG_RING_SLOTS) {
                s_log_ring_rd = s_log_ring_wr - LOG_RING_SLOTS;
            }
        }
    }

    if (s_state != NOVA_STATE_CONNECTED) {
        return; /* Don't push data until bridge connects */
    }

    /* ─── Drain ring buffer: send one buffered record per tick ────────── */
    /* Sending one per tick (50ms) avoids flooding the UART.               */
    if (s_log_ring_rd != s_log_ring_wr) {
        LogRingSlot *slot = &s_log_ring[s_log_ring_rd % LOG_RING_SLOTS];
        if (slot->len > 0) {
            NovaMsg_SendLogRecordRaw(slot->data, slot->len);
            slot->len = 0;
        }
        s_log_ring_rd++;
    }

    /* 1s interval: push live status */
    if ((s_tick_count % TICK_STATUS_INTERVAL) == 0) {
        NovaMsg_SendSystemStatus();
        NovaMsg_SendEquipmentStatus();
    }

    /* 5s interval: heartbeat + orbit status */
    if ((s_tick_count % TICK_HEARTBEAT_INTERVAL) == 0) {
        NovaMsg_SendHeartbeat();
        NovaMsg_SendOrbitStatus();
    }

    /* On-change: warnings — also detect which warnings changed for activity log */
    if (NovaDataExc_WarningsChanged) {
        NovaDataExc_WarningsChanged = false;
        NovaMsg_SendWarnings();

        /* Emit ActivityEvent for each warning that actually toggled */
        int i;
        for (i = 0; i < NUM_WARNINGS && i < ACTIVITY_WARN_SLOTS; i++) {
            uint8_t cur = (WarningStatus((WARNING_ITEMS)i) != 0) ? 1 : 0;
            if (cur != s_prev_warn_state[i]) {
                s_prev_warn_state[i] = cur;
                NovaMsg_SendActivityEvent(
                    cur ? 1 : 2, /* EVENT_WARNING_ON / EVENT_WARNING_OFF */
                    (uint32_t)i,
                    "Warning",
                    (uint32_t)cur);
            }
        }
    }

    /* On-change: equipment toggle — also detect which outputs changed for activity log */
    if (NovaDataExc_EquipChanged) {
        NovaDataExc_EquipChanged = false;
        NovaMsg_SendEquipmentStatus();

        /* Emit ActivityEvent for each output that actually toggled */
        int i;
        for (i = 0; i < EQ_TOTAL_IO && i < ACTIVITY_EQUIP_SLOTS; i++) {
            uint8_t cur = CheckOutputs(i) ? 1 : 0;
            if (cur != s_prev_equip_state[i]) {
                s_prev_equip_state[i] = cur;
                const char *name = Settings.EquipIo[i].Name;
                NovaMsg_SendActivityEvent(
                    0, /* EVENT_EQUIP_CHANGE */
                    (uint32_t)i,
                    (name && name[0]) ? name : "Equipment",
                    (uint32_t)cur);
            }
        }
    }

    /* On-change: IO configuration */
    if (NovaDataExc_IoConfigChanged) {
        NovaDataExc_IoConfigChanged = false;
        NovaMsg_SendIoConfig();
        NovaMsg_SendIoDefinition();
    }

    /* On-change: settings saved — resend all settings pages */
    if (NovaDataExc_SettingsChanged) {
        NovaDataExc_SettingsChanged = false;
        NovaMsg_SendAllSettings();
        /* Recalculate log interval from updated settings */
        uint32_t intervalMin = Settings.Log.User.Interval;
        if (intervalMin == 0) intervalMin = 15;
        s_log_interval_ticks = intervalMin * 60 * 20; /* minutes → 50ms ticks */
    }

    /* ─── Activity: mode change detection (no on-change flag, poll at 1s) ── */
    if ((s_tick_count % TICK_STATUS_INTERVAL) == 0) {
        if ((uint8_t)CurrentMode != s_prev_mode) {
            s_prev_mode = (uint8_t)CurrentMode;
            NovaMsg_SendActivityEvent(
                3, /* EVENT_MODE_CHANGE */
                0,
                "Mode",
                (uint32_t)(uint8_t)CurrentMode);
        }
    }
}

NovaConnState NovaDataExc_GetState(void)
{
    return s_state;
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Settings apply handlers — decode protobuf submessages and write to     */
/* Settings struct. Mirrors Store* functions in StorePostData.c.          */
/* ═══════════════════════════════════════════════════════════════════════ */

/* Helper: parse submessage fields into a scratch decoder */
#define APPLY_BEGIN(buf, buflen) \
    PbDecoder _d; pd_init(&_d, buf, buflen); \
    PbDecoder *D = &_d;

#define APPLY_FIELDS_BEGIN \
    while (pd_has_data(D)) { \
        uint8_t _wt; \
        uint32_t _fn = pd_tag(D, &_wt); \
        if (_fn == 0) break; \
        switch (_fn) {

#define APPLY_FIELDS_END \
        default: pd_skip(D, _wt); break; \
        } \
    }

/* ── Plenum (field 1 of SettingsUpdate) — mirrors StorePlenumSetup ────── */
static void apply_plenum(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.Plenum.TempSet = pd_float(D); break;
        case 2: Settings.Plenum.HumidSet = (uint8_t)pd_uint32(D); break;
        case 3: Settings.Plenum.HumidSetpointRef = (uint8_t)pd_uint32(D); break;
        case 4: Settings.Burner.TempSet = pd_float(D); break;
        /* field 5: burner_threshold string — not stored */
    APPLY_FIELDS_END

    Settings.Ramp.TargetTemp = Settings.Plenum.TempSet;
}

/* ── Fan Speed (field 2) — mirrors StoreFanSpeeds ─────────────────────── */
static void apply_fan_speed(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case  1: Settings.Fan.MaxSpeed = (uint8_t)pd_uint32(D); break;
        case  2: Settings.Fan.MinSpeed = (uint8_t)pd_uint32(D); break;
        case  3: Settings.Fan.RefrigSpeed = (uint8_t)pd_uint32(D); break;
        case  4: /* heat_speed — not stored */ pd_uint32(D); break;
        case  5: Settings.Fan.UpdatePeriod = (uint8_t)pd_uint32(D); break;
        case  7: Settings.Fan.RecircSpeed = (uint8_t)pd_uint32(D); break;
    APPLY_FIELDS_END

    if (Settings.Fan.MinSpeed > Settings.Fan.MaxSpeed)
        Settings.Fan.MinSpeed = Settings.Fan.MaxSpeed;
    if (Settings.FanBoost.Speed > Settings.Fan.MaxSpeed)
        Settings.FanBoost.Speed = Settings.Fan.MaxSpeed;
    if (Settings.FanBoost.Speed < Settings.Fan.MinSpeed)
        Settings.FanBoost.Speed = Settings.Fan.MinSpeed;
}

/* ── Fan Boost (field 3) — mirrors StoreFanBoost ──────────────────────── */
static void apply_fan_boost(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    uint8_t oldMode = Settings.FanBoost.Mode;
    APPLY_FIELDS_BEGIN
        case 1: Settings.FanBoost.Mode = (uint8_t)pd_uint32(D); break;
        case 2: Settings.FanBoost.Speed = pd_float(D); break;
        case 3: Settings.FanBoost.Interval = (uint8_t)pd_uint32(D); break;
        case 4: Settings.FanBoost.Duration = (uint8_t)pd_uint32(D); break;
        case 5: Settings.FanBoost.Temp = pd_float(D); break;
    APPLY_FIELDS_END

    if (Settings.FanBoost.Mode != oldMode)
        IntervalTimer[IT_FANBOOSTINTERVAL] = XTimerVal;
    if (Settings.FanBoost.Speed > Settings.Fan.MaxSpeed)
        Settings.FanBoost.Speed = Settings.Fan.MaxSpeed;
    if (Settings.FanBoost.Speed < Settings.Fan.MinSpeed)
        Settings.FanBoost.Speed = Settings.Fan.MinSpeed;
}

/* ── Ramp Rate (field 4) — mirrors StoreRampRate ──────────────────────── */
static void apply_ramp_rate(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.Ramp.UpdateTemp = pd_float(D); break;
        case 2: Settings.Ramp.UpdatePeriod = (uint8_t)pd_uint32(D); break;
    APPLY_FIELDS_END
}

/* ── Refrig (field 5) — mirrors StoreRefrig ───────────────────────────── */
static void apply_refrig(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    /* Per-call counters (NOT static — must reset on each save) so that
     * successive p2Refrigeration POSTs don't run off the end of the array. */
    int stage_idx   = 0;
    int defrost_idx = 0;
    APPLY_FIELDS_BEGIN
        case 1: Settings.Refrig.Mode = (uint8_t)pd_uint32(D); break;
        case 2: {
            /* repeated RefrigStage submessage — fields: on(1), off(2), diagnostic(3) */
            size_t slen;
            const uint8_t *sdata = pd_bytes(D, &slen);
            if (stage_idx < NUM_REFRIG_STAGES) {
                PbDecoder sd; pd_init(&sd, sdata, slen);
                while (pd_has_data(&sd)) {
                    uint8_t swt; uint32_t sfn = pd_tag(&sd, &swt);
                    if (sfn == 0) break;
                    switch (sfn) {
                        case 1: Settings.Refrig.Stage[stage_idx].On         = (uint8_t)pd_uint32(&sd); break;
                        case 2: Settings.Refrig.Stage[stage_idx].Off        = (uint8_t)pd_uint32(&sd); break;
                        case 3: Settings.Refrig.Stage[stage_idx].Diagnostic = (uint8_t)pd_uint32(&sd); break;
                        default: pd_skip(&sd, swt); break;
                    }
                }
                stage_idx++;
            }
            break;
        }
        case 3: Settings.Refrig.PID.P = (int)pd_float(D); PIDCtrl[PID_REFRIGERATION].Kp = Settings.Refrig.PID.P; break;
        case 4: Settings.Refrig.PID.I = (int)pd_float(D); PIDCtrl[PID_REFRIGERATION].Ki = Settings.Refrig.PID.I; break;
        case 5: Settings.Refrig.PID.D = (int)pd_float(D); PIDCtrl[PID_REFRIGERATION].Kd = Settings.Refrig.PID.D; break;
        case 6: Settings.Refrig.PID.U = (int)pd_float(D); break;
        case 7: Settings.Refrig.Mode = (uint8_t)pd_uint32(D); break;
        case 8: Settings.Refrig.DefrostPeriod   = (uint8_t)pd_uint32(D); break;
        case 9: Settings.Refrig.DefrostDuration = (uint8_t)pd_uint32(D); break;
        case 10: Settings.Refrig.Purge          = (uint8_t)pd_uint32(D); break;
        case 11: Settings.Co2.Purge.RefrigThresh = (uint8_t)pd_uint32(D); break;
        case 12: Settings.Refrig.Limit          = pd_float(D); break;
        case 13: Settings.Refrig.FailMode       = (uint8_t)pd_uint32(D); break;
        case 14: {
            /* repeated RefrigDefrost submessage — same wire shape as RefrigStage */
            size_t slen;
            const uint8_t *sdata = pd_bytes(D, &slen);
            if (defrost_idx < NUM_DEFROST_STAGES) {
                PbDecoder sd; pd_init(&sd, sdata, slen);
                while (pd_has_data(&sd)) {
                    uint8_t swt; uint32_t sfn = pd_tag(&sd, &swt);
                    if (sfn == 0) break;
                    switch (sfn) {
                        case 1: Settings.Refrig.Defrost[defrost_idx].On         = (uint8_t)pd_uint32(&sd); break;
                        case 2: Settings.Refrig.Defrost[defrost_idx].Off        = (uint8_t)pd_uint32(&sd); break;
                        case 3: Settings.Refrig.Defrost[defrost_idx].Diagnostic = (uint8_t)pd_uint32(&sd); break;
                        default: pd_skip(&sd, swt); break;
                    }
                }
                defrost_idx++;
            }
            break;
        }
    APPLY_FIELDS_END
}

/* ── Burner (field 6) — mirrors StoreBurner ───────────────────────────── *
 *
 * Wire fields:
 *   1 = Burner.On            5 = Burner.PID.D       9  = Climacell.Altitude
 *   2 = Burner.Low           6 = Burner.PID.U       10 = Climacell.AltitudeUnits
 *   3 = Burner.PID.P         7 = Burner.Mode
 *   4 = Burner.PID.I         8 = Burner.Manual
 *
 * The bridge encoder is mode-dependent (matches legacy StoreBurner): it
 * always emits Mode (field 7), then either Manual (mode=1) OR On/Low/PID
 * (mode=2/3), plus Altitude/AltitudeUnits (also written by the legacy POST
 * handler).  Unlisted fields keep their previous values.
 */
static void apply_burner(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.Burner.On = (uint8_t)pd_uint32(D); break;
        case 2: Settings.Burner.Low = (uint8_t)pd_uint32(D); break;
        case 3: Settings.Burner.PID.P = (int)pd_float(D); PIDCtrl[PID_BURNER].Kp = Settings.Burner.PID.P; break;
        case 4: Settings.Burner.PID.I = (int)pd_float(D); PIDCtrl[PID_BURNER].Ki = Settings.Burner.PID.I; break;
        case 5: Settings.Burner.PID.D = (int)pd_float(D); PIDCtrl[PID_BURNER].Kd = Settings.Burner.PID.D; break;
        case 6: Settings.Burner.PID.U = (int)pd_float(D); break;
        case 7: Settings.Burner.Mode = (uint8_t)pd_uint32(D); break;
        case 8: Settings.Burner.Manual = (uint8_t)pd_uint32(D); break;
        case 9: Settings.Climacell.Altitude = pd_uint32(D); break;
        case 10: Settings.Climacell.AltitudeUnits = (uint8_t)pd_uint32(D); break;
    APPLY_FIELDS_END
}

/* ── CO2 (field 7) — mirrors StoreCO2 ─────────────────────────────────── */
static void apply_co2(const uint8_t *d, size_t len)
{
    uint8_t oldMode = Settings.Co2.Purge.Mode;
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.Co2.Purge.Mode = (uint8_t)pd_uint32(D); break;
        case 2: Settings.Co2.Purge.MinTemp = pd_float(D); break;
        case 3: Settings.Co2.Purge.MaxTemp = pd_float(D); break;
        case 4: Settings.Co2.Purge.Duration = (uint8_t)pd_uint32(D); break;
        case 5: {
            uint32_t v = pd_uint32(D);
            if (Settings.Co2.Purge.Mode == 1)
                Settings.Co2.CylceTime = v;
            else
                Settings.Co2.Set = v;
            break;
        }
        case 6: Settings.Co2.FanOutput = (uint8_t)pd_uint32(D); break;
        case 7: Settings.Co2.DoorOutput = (uint8_t)pd_uint32(D); break;
    APPLY_FIELDS_END

    /* If changing from manual, reset the timer */
    if (oldMode == 1 && Settings.Co2.Purge.Mode != 1)
        Settings.Co2.Purge.Last = XTimerVal;
}

/* ── Cure (field 8) — mirrors StoreAirCure ────────────────────────────── */
static void apply_cure(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.Cure.StartTemp = pd_float(D); break;
        case 2: Settings.Cure.HumidRef = (uint8_t)pd_uint32(D); break;
        case 3: Settings.Cure.StartHumid = (int)pd_float(D); break;
        case 4: Settings.Cure.HumidHighLimit = (int)pd_float(D); break;
    APPLY_FIELDS_END
}

/* ── Climacell (field 9) — mirrors StoreClimacell ─────────────────────── */
static void apply_climacell(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.Climacell.Efficiency = (uint8_t)pd_uint32(D); break;
        case 2: Settings.Climacell.Altitude = pd_uint32(D); break;
        case 3: Settings.Climacell.AltitudeUnits = (uint8_t)pd_uint32(D); break;
        case 4: Settings.Climacell.PID.P = (int)pd_float(D);
                PIDCtrl[PID_CLIMACELL].Kp = Settings.Climacell.PID.P;
                PIDCtrl[PID_HUMIDIFIER].Kp = Settings.Climacell.PID.P; break;
        case 5: Settings.Climacell.PID.I = (int)pd_float(D);
                PIDCtrl[PID_CLIMACELL].Ki = Settings.Climacell.PID.I;
                PIDCtrl[PID_HUMIDIFIER].Ki = Settings.Climacell.PID.I; break;
        case 6: Settings.Climacell.PID.D = (int)pd_float(D);
                PIDCtrl[PID_CLIMACELL].Kd = Settings.Climacell.PID.D;
                PIDCtrl[PID_HUMIDIFIER].Kd = Settings.Climacell.PID.D; break;
        case 7: Settings.Climacell.PID.U = (int)pd_float(D); break;
    APPLY_FIELDS_END
}

/* ── Climacell Times (field 10) — mirrors StoreClimacellTimes ─────────── *
 *
 * Wire format: 48 repeated uint32 values in field 1 (one per half-hour slot).
 * The bridge force-encodes every slot (including 0s, which are legitimate),
 * so the decoder can rely on receiving exactly 48 values.  We still clear
 * the array up-front so that a partial message can never leave stale entries
 * mixed with the new schedule.
 */
static void apply_climacell_times(const uint8_t *d, size_t len)
{
    int idx = 0;
    /* Full replacement: clear all 48 slots before applying. */
    for (int i = 0; i < 48; i++) {
        Settings.Climacell.Times[i] = 0;
    }
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1:
            if (idx < 48)
                Settings.Climacell.Times[idx++] = (uint8_t)pd_uint32(D);
            else
                pd_uint32(D);
            break;
    APPLY_FIELDS_END
}

/* ── Humid Control (field 11) — mirrors StoreHumidCtrl ──────────────────
 *
 * The UI saves ONE humidifier at a time (the system supports up to 3).
 * Wire format (HumidCtrlSettings):
 *   repeated HumidCtrlEntry entries = 1;
 * Each entry carries: index, mode, cool_on, cool_off, recirc_on,
 * recirc_off, refrig_on, refrig_off — splice into Settings.HumidCtrl[index].
 *
 * Legacy reference: StoreHumidCtrl() in StorePostData.c only writes the
 * 6 duty-cycle values when Mode == 1 (Timer); we mirror that to avoid
 * clobbering values when switching to Manual or Auto mode.
 */
static void apply_humid_ctrl(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: {
            /* Repeated submessage: one HumidCtrlEntry per humidifier */
            size_t entry_len = 0;
            const uint8_t *entry_buf = pd_bytes(D, &entry_len);
            if (!entry_buf) break;

            uint32_t e_index = 0, e_mode = 0;
            uint32_t e_cool_on = 0,  e_cool_off = 0;
            uint32_t e_rec_on  = 0,  e_rec_off  = 0;
            uint32_t e_ref_on  = 0,  e_ref_off  = 0;
            bool have_index = false, have_mode = false;

            PbDecoder ed; pd_init(&ed, entry_buf, entry_len);
            while (pd_has_data(&ed)) {
                uint8_t ewt;
                uint32_t efn = pd_tag(&ed, &ewt);
                if (efn == 0) break;
                switch (efn) {
                    case 1: e_index    = pd_uint32(&ed); have_index = true; break;
                    case 2: e_mode     = pd_uint32(&ed); have_mode  = true; break;
                    case 3: e_cool_on  = pd_uint32(&ed); break;
                    case 4: e_cool_off = pd_uint32(&ed); break;
                    case 5: e_rec_on   = pd_uint32(&ed); break;
                    case 6: e_rec_off  = pd_uint32(&ed); break;
                    case 7: e_ref_on   = pd_uint32(&ed); break;
                    case 8: e_ref_off  = pd_uint32(&ed); break;
                    default: pd_skip(&ed, ewt); break;
                }
            }

            if (!have_index || e_index >= NUM_HUMIDIFIERS) break;

            if (have_mode) {
                Settings.HumidCtrl[e_index].Mode = (char)(uint8_t)e_mode;
            }
            /* Mirror legacy: only overwrite cycle values in Timer mode.
             * Auto/Manual leave the values untouched so they're preserved
             * if the operator later switches back to Timer. */
            if (have_mode && e_mode == 1 /* HM_TIMER */) {
                Settings.HumidCtrl[e_index].DutyCycle[0].On  = (char)(uint8_t)e_cool_on;
                Settings.HumidCtrl[e_index].DutyCycle[0].Off = (char)(uint8_t)e_cool_off;
                Settings.HumidCtrl[e_index].DutyCycle[1].On  = (char)(uint8_t)e_rec_on;
                Settings.HumidCtrl[e_index].DutyCycle[1].Off = (char)(uint8_t)e_rec_off;
                Settings.HumidCtrl[e_index].DutyCycle[2].On  = (char)(uint8_t)e_ref_on;
                Settings.HumidCtrl[e_index].DutyCycle[2].Off = (char)(uint8_t)e_ref_off;
            }
            break;
        }
    APPLY_FIELDS_END
}

/* ── Outside Air (field 12) — mirrors StoreOutsideAir ─────────────────── */
static void apply_outside_air(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.OutsideAir.CtrlMode = (uint8_t)pd_uint32(D); break;
        case 2: Settings.OutsideAir.Diff = pd_float(D); break;
    APPLY_FIELDS_END
}

/* ── Misc (field 13) — mirrors StoreMisc ──────────────────────────────── */
static void apply_misc(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.HeatTempThresh = pd_float(D); break;
        case 2: Settings.KbPref = (uint8_t)pd_uint32(D); break;
        case 3: Settings.LightsFailUnits = (uint8_t)pd_uint32(D); break;
        case 4: Settings.CavityHeat.StandbyOn = (uint8_t)pd_uint32(D); break;
    APPLY_FIELDS_END
}

/* ── Failures (field 14) — mirrors StoreFailures ──────────────────────── */
static void apply_failures(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case  1: Settings.Failure[FAIL_FAN].Mode = (uint8_t)pd_uint32(D); break;
        case  2: Settings.Failure[FAIL_FAN].Timer = (uint8_t)pd_uint32(D); break;
        case  3: Settings.Failure[FAIL_HEAT].Mode = (uint8_t)pd_uint32(D); break;
        case  4: Settings.Failure[FAIL_HEAT].Timer = (uint8_t)pd_uint32(D); break;
        case  5: Settings.Failure[FAIL_REFRIGERATION].Mode = (uint8_t)pd_uint32(D); break;
        case  6: Settings.Failure[FAIL_REFRIGERATION].Timer = (uint8_t)pd_uint32(D); break;
        case  7: Settings.Refrig.FailMode = (uint8_t)pd_uint32(D); break;
        case  8: Settings.Failure[FAIL_BURNER].Mode = (uint8_t)pd_uint32(D); break;
        case  9: Settings.Failure[FAIL_BURNER].Timer = (uint8_t)pd_uint32(D); break;
        case 10: Settings.Failure[FAIL_HUMIDIFIERS].Timer = (uint8_t)pd_uint32(D); break;
        case 11: Settings.Failure[FAIL_CLIMACELL].Timer = (uint8_t)pd_uint32(D); break;
    APPLY_FIELDS_END
}

/* ── Failures2 (field 15) — mirrors StoreFailures2 ────────────────────── */
static void apply_failures2(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case  1: Settings.Failure[FAIL_OUTSIDE_AIR].Mode = (uint8_t)pd_uint32(D); break;
        case  2: Settings.Failure[FAIL_OUTSIDE_AIR].Timer = (uint8_t)pd_uint32(D); break;
        case  3: Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode = (uint8_t)pd_uint32(D); break;
        case  4: Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Timer = (uint8_t)pd_uint32(D); break;
        case  5: Settings.Failure[FAIL_HIGH_CO2].Mode = (uint8_t)pd_uint32(D); break;
        case  6: Settings.Failure[FAIL_HIGH_CO2].Timer = (uint8_t)pd_uint32(D); break;
        case  7: Settings.Co2.HighFailure = (uint16_t)pd_uint32(D); break;
        case  8: Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode = (uint8_t)pd_uint32(D); break;
        case  9: Settings.Failure[FAIL_PLENUM_HUMIDITY].Timer = (uint8_t)pd_uint32(D); break;
        case 10: Settings.Plenum.HumidLowFailure = (uint8_t)pd_uint32(D); break;
        case 11: Settings.Failure[FAIL_PLENUM_SENSOR].Mode = (uint8_t)pd_uint32(D); break;
        case 12: Settings.Failure[FAIL_PLENUM_SENSOR].Timer = (uint8_t)pd_uint32(D); break;
        case 13: Settings.Plenum.SensorDiff = pd_float(D); break;
    APPLY_FIELDS_END
}

/* ── Temp Alarms (field 16) — mirrors StoreTempAlarms + cure limits ─────── */
/* Wire layout MUST match NovaMsg_SendTempAlarmSettings and the bridge
 * legacyShim AlarmTempLow encoder.  Order = legacy AlarmTempLow= CSV:
 *   1=LowAlarmTemp,  2=LowAlarmTimer,
 *   3=HighAlarmTemp, 4=HighAlarmTimer,
 *   5=Cure.TempLowLimit, 6=Cure.TempHighLimit. */
static void apply_temp_alarms(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.Plenum.LowAlarmTemp   = pd_float(D); break;
        case 2: Settings.Plenum.LowAlarmTimer  = (uint8_t)pd_uint32(D); break;
        case 3: Settings.Plenum.HighAlarmTemp  = pd_float(D); break;
        case 4: Settings.Plenum.HighAlarmTimer = (uint8_t)pd_uint32(D); break;
        case 5: Settings.Cure.TempLowLimit     = pd_float(D); break;
        case 6: Settings.Cure.TempHighLimit    = pd_float(D); break;
    APPLY_FIELDS_END
}

/* ── Cure Limits (field 17) — mirrors StoreCureLimits ─────────────────── */
static void apply_cure_limits(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.Cure.TempLowLimit = pd_float(D); break;
        case 2: Settings.Cure.TempHighLimit = pd_float(D); break;
    APPLY_FIELDS_END
}

/* ── Door (field 19) — mirrors StoreDoor ──────────────────────────────── */
static void apply_door(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.Door.PID.P = (int)pd_float(D); PIDCtrl[PID_DOOR].Kp = Settings.Door.PID.P; break;
        case 2: Settings.Door.PID.I = (int)pd_float(D); PIDCtrl[PID_DOOR].Ki = Settings.Door.PID.I; break;
        case 3: Settings.Door.PID.D = (int)pd_float(D); PIDCtrl[PID_DOOR].Kd = Settings.Door.PID.D; break;
        case 4: Settings.Door.PID.U = (int)pd_float(D); break;
        case 5: Settings.Door.ActuatorTime = pd_uint32(D); break;
        case 6: Settings.Door.CoolAirCycle = (uint8_t)pd_uint32(D); break;
    APPLY_FIELDS_END
}

/* ── Date/Time (field 26) — mirrors StoreDateTime ─────────────────────── */
static void apply_date_time(const uint8_t *d, size_t len)
{
    char dateStr[16] = {0};
    char timeStr[16] = {0};
    uint32_t amPm = 0;

    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: pd_string_into(D, dateStr, sizeof(dateStr)); break;
        case 2: pd_string_into(D, timeStr, sizeof(timeStr)); break;
        case 3: amPm = pd_uint32(D); break;
    APPLY_FIELDS_END

    if (dateStr[0] && timeStr[0]) {
        if (SetDateTimeStr(dateStr, timeStr, (int)amPm) == 0)
            WarningsSet(WARN_INVALIDDATETIME, FM_ALARM, FM_ALARM, NA);
    }
}

/* ── Email (field 27) — mirrors StoreEmail ────────────────────────────── */
static void apply_email(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: pd_string_into(D, Settings.Email.Server, EMAIL_FROM_LEN + 1); break;
        case 2: Settings.Email.Port = (uint16_t)pd_uint32(D); break;
        case 3: pd_string_into(D, Settings.Email.Account, EMAIL_FROM_LEN + 1); break;
        case 4: pd_string_into(D, Settings.Email.Password, EMAIL_FROM_LEN + 1); break;
        case 5: pd_string_into(D, Settings.Email.From, EMAIL_FROM_LEN + 1); break;
        case 6: pd_string_into(D, Settings.Email.To, EMAIL_TO_LEN + 1); break;
        case 7: Settings.Email.Alerts = (uint8_t)pd_uint32(D); break;
    APPLY_FIELDS_END
}

/* ── Service Info (field 31) — mirrors StoreServiceInfo ───────────────── */
static void apply_service_info(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: pd_string_into(D, Settings.DealerName, NAME_LEN + 1); break;
        case 2: pd_string_into(D, Settings.DealerPhone, PHONE_LEN + 1); break;
        case 3: pd_string_into(D, Settings.TechName, NAME_LEN + 1); break;
        case 4: pd_string_into(D, Settings.TechPhone, PHONE_LEN + 1); break;
    APPLY_FIELDS_END
}

/* ── Basic Setup (field 32) — mirrors StoreBasic ──────────────────────── */
static void apply_basic_setup(const uint8_t *d, size_t len)
{
    uint8_t newTempType = Settings.TempType;
    uint8_t newSysMode = Settings.SystemMode;

    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case  1: pd_string_into(D, Settings.StorageName, SITE_ID_LEN + 1); break;
        case  2: newTempType = (uint8_t)pd_uint32(D); break;
        case  5: newSysMode = (uint8_t)pd_uint32(D); break;
        case  4: pd_string_into(D, Settings.HomePage, HOMEPAGE_LEN + 1); break;
        case  8: Settings.MultiviewSessions = (uint8_t)pd_uint32(D); break;
        case  9: pd_string_into(D, Settings.LoginPw, PASSWORD_LEN + 1); break;
        case 10: Settings.LocalLogin = (uint8_t)pd_uint32(D); break;
        case 11: Settings.Animations = (uint8_t)pd_uint32(D); break;
    APPLY_FIELDS_END

    /* Temperature unit conversion (mirrors StoreBasic) */
    if (newTempType != Settings.TempType) {
        if (Settings.TempType == 0) {  /* F → C */
            Settings.Plenum.TempSet = (Settings.Plenum.TempSet - 32.0f) / 1.8f;
            PlenumTempAvg = (PlenumTempAvg - 32.0f) / 1.8f;
            StartTemp = (StartTemp - 32.0f) / 1.8f;
        } else {  /* C → F */
            Settings.Plenum.TempSet = (Settings.Plenum.TempSet * 1.8f) + 32.0f;
            PlenumTempAvg = (PlenumTempAvg * 1.8f) + 32.0f;
            StartTemp = (StartTemp * 1.8f) + 32.0f;
        }
        Settings.TempType = newTempType;
        Settings.Ramp.TargetTemp = Settings.Plenum.TempSet;
        ReadAnalogBoards(RT_ALL);
    }

    /* System mode change (potato ↔ onion) */
    if (newSysMode != Settings.SystemMode) {
        Settings.SystemMode = newSysMode;
        SystemState = ST_UNDEFINED;
        ClearAlarms(1);
        WarningsClear();
        ClearIntervalTimers();
        CtrlRefrigDiagClear();
        EquipIoInit();
        EquipPwmInit();
    }
}

/* ── Runtimes (field 33) — mirrors StoreRuntimes ──────────────────────── */
static void apply_runtimes(const uint8_t *d, size_t len)
{
    int idx = 0;
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1:
            if (idx < 48)
                Settings.RunTimes[idx++] = (uint8_t)pd_uint32(D);
            else
                pd_uint32(D);
            break;
    APPLY_FIELDS_END
}

/* ── Accounts (field 34) — mirrors StorePassword ──────────────────────── */
static void apply_accounts(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    /* field 1: repeated UserAccount submessage (slot, id, password) */
    /* field 2: login_pw string */
    APPLY_FIELDS_BEGIN
        case 1: {
            size_t slen;
            const uint8_t *sdata = pd_bytes(D, &slen);
            PbDecoder sd; pd_init(&sd, sdata, slen);
            uint32_t slot = 0;
            char uid[PASSWORD_LEN + 1] = {0};
            char pw[PASSWORD_LEN + 1] = {0};
            while (pd_has_data(&sd)) {
                uint8_t swt; uint32_t sfn = pd_tag(&sd, &swt);
                if (sfn == 0) break;
                switch (sfn) {
                    case 1: slot = pd_uint32(&sd); break;
                    case 2: pd_string_into(&sd, uid, sizeof(uid)); break;
                    case 3: pd_string_into(&sd, pw, sizeof(pw)); break;
                    default: pd_skip(&sd, swt); break;
                }
            }
            if (slot < NUM_PASSWORDS) {
                StringCopy(Settings.User[slot].ID, uid, PASSWORD_LEN + 1);
                StringCopy(Settings.User[slot].Password, pw, PASSWORD_LEN + 1);
            }
            break;
        }
        case 2: pd_string_into(D, Settings.LoginPw, PASSWORD_LEN + 1); break;
    APPLY_FIELDS_END
}

/* ── Alert Setup (field 35) — mirrors StoreAlerts ─────────────────────── */
static void apply_alert_setup(const uint8_t *d, size_t len)
{
    /* Clear all, then set flagged ones */
    memset(Settings.AlertsToSend, '0', NUM_WARNINGS);
    Settings.AlertsToSend[NUM_WARNINGS] = '\0';
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: {
            uint32_t idx = pd_uint32(D);
            if (idx < (uint32_t)NUM_WARNINGS)
                Settings.AlertsToSend[idx] = '1';
            break;
        }
    APPLY_FIELDS_END
}

/* ── IO Config (field 36) — mirrors StoreIoConfig (simplified) ────────── */
static void apply_io_config(const uint8_t *d, size_t len)
{
    /* Receives: field 1 = repeated output_map, field 2 = repeated input_map */
    int outIdx = 0, inIdx = 0;
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1:
            if (outIdx < EQ_TOTAL_IO)
                Settings.EquipIo[outIdx++].Output = pd_uint32(D);
            else pd_uint32(D);
            break;
        case 2:
            if (inIdx < EQ_TOTAL_IO)
                Settings.EquipIo[inIdx++].Input = pd_uint32(D);
            else pd_uint32(D);
            break;
    APPLY_FIELDS_END
    NovaDataExc_IoConfigChanged = true;
}

/* ── Log Settings (field 37) — mirrors StoreUserLog ───────────────────── */
static void apply_log_settings(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.Log.User.Interval = (short)pd_uint32(D); break;
        case 2: Settings.Log.User.Wrap = (char)pd_uint32(D); break;
    APPLY_FIELDS_END
}

/* ── PID Settings (field 38) — mirrors StorePIDSettings ───────────────── */
static void apply_pid_settings(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: /* eq_index — unused */ pd_uint32(D); break;
        case 6: Settings.Log.PID.Wrap = (char)pd_uint32(D); break;
    APPLY_FIELDS_END
}

/* ── PWM Settings (field 39) — mirrors StorePWMChannels ───────────────── *
 *
 * Wire format (per repeated PwmChannel submsg in field 1):
 *   field 1 = index   (PWM_EQUIPMENT enum, 0..PWM_TOTAL_EQ-1)
 *   field 2 = enabled (1 for entries actually sent; absent equipments get cleared)
 *   field 3 = channel (hardware PWM channel 0..5)
 *
 * Per-channel save semantics: the bridge sends ONE entry per assigned
 * channel.  All Settings.PWM[] entries are cleared up front so the new save
 * fully replaces the prior assignment list (an equipment that disappears
 * from the POST gets its Enabled=0 / Channel=PWM_UNDEFINED).
 */
static void apply_pwm_settings(const uint8_t *d, size_t len)
{
    /* Clear ALL PWM equipment assignments so this save fully replaces. */
    for (int i = 0; i < PWM_TOTAL_EQ; i++) {
        Settings.PWM[i].Enabled = 0;
        Settings.PWM[i].Channel = 0xFF; /* PWM_UNDEFINED */
    }
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: {
            size_t slen;
            const uint8_t *sdata = pd_bytes(D, &slen);
            PbDecoder sd; pd_init(&sd, sdata, slen);
            uint32_t idx = (uint32_t)PWM_TOTAL_EQ;  /* sentinel = invalid */
            uint32_t enabled = 0;
            uint32_t channel = 0xFF;
            while (pd_has_data(&sd)) {
                uint8_t swt; uint32_t sfn = pd_tag(&sd, &swt);
                if (sfn == 0) break;
                switch (sfn) {
                    case 1: idx = pd_uint32(&sd); break;
                    case 2: enabled = pd_uint32(&sd); break;
                    case 3: channel = pd_uint32(&sd); break;
                    default: pd_skip(&sd, swt); break;
                }
            }
            if (idx < (uint32_t)PWM_TOTAL_EQ) {
                Settings.PWM[idx].Enabled = (unsigned char)enabled;
                Settings.PWM[idx].Channel = (unsigned char)channel;
            }
            break;
        }
    APPLY_FIELDS_END
}

/* ── Master/Slave (field 40) — mirrors StoreMasterSlave ───────────────── */
static void apply_master_slave(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: Settings.MasterSlave = (char)pd_uint32(D); break;
        case 2: pd_string_into(D, Settings.MasterIP, IP_ADD_LEN + 1); break;
    APPLY_FIELDS_END
}

/* ── Aux Program (field 41) — mirrors StoreAuxProgram ─────────────────── *
 *
 * Wire format (per repeated AuxProgramEntry submsg in field 1):
 *   field 1 = aux_index          (0..NUM_AUX_OUTPUTS-1, **0-based**)
 *   field 2 = eq_index           (informational; ignored on apply)
 *   field 3 = duty_cycle
 *   field 4 = period
 *   field 5 = units
 *   field 6 = repeated AuxRule   (Type/IoIndex/State/Op/SensorValue/AndOr/Ref)
 *
 * Per-index save semantics (matches legacy StoreAuxProgram):
 *  - The bridge sends one AuxProgramEntry per save (one aux at a time).
 *  - All existing rules for that aux index are cleared up front so the new
 *    rule list fully replaces the old one (no stale-rule overlay).
 *  - ruleIdx is function-local; previously a `static` here caused per-aux
 *    rule writes to land in the wrong slots after the first save call.
 */
static void apply_aux_program(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: {
            size_t slen;
            const uint8_t *sdata = pd_bytes(D, &slen);
            PbDecoder sd; pd_init(&sd, sdata, slen);
            uint32_t auxIdx = (uint32_t)NUM_AUX_OUTPUTS;  /* sentinel = invalid */
            int ruleIdx = 0;
            int rules_cleared = 0;
            while (pd_has_data(&sd)) {
                uint8_t swt; uint32_t sfn = pd_tag(&sd, &swt);
                if (sfn == 0) break;
                switch (sfn) {
                    case 1: {
                        auxIdx = pd_uint32(&sd);
                        /* Clear all existing rules so this save is a full
                         * replacement (matches legacy StoreAuxProgram). */
                        if (!rules_cleared && auxIdx < NUM_AUX_OUTPUTS) {
                            for (int rr = 0; rr < NUM_AUX_PROGRAM_RULES; rr++) {
                                AUX_PROGRAM_RULE *rule = &Settings.AuxProgram[auxIdx].Rule[rr];
                                rule->Type           = RT_UNDEFINED;
                                rule->IoIndex        = 0;
                                rule->State          = 0;
                                rule->Op             = 0;
                                rule->SensorValue    = 0.0f;
                                rule->AndOr          = 255;
                                rule->ReferenceIndex = 0;
                            }
                            rules_cleared = 1;
                        }
                        break;
                    }
                    case 2: pd_uint32(&sd); break;  /* eq_index — informational */
                    case 3:
                        if (auxIdx < NUM_AUX_OUTPUTS)
                            Settings.AuxProgram[auxIdx].DutyCycle = (uint8_t)pd_uint32(&sd);
                        else pd_uint32(&sd);
                        break;
                    case 4:
                        if (auxIdx < NUM_AUX_OUTPUTS)
                            Settings.AuxProgram[auxIdx].Period = pd_uint32(&sd);
                        else pd_uint32(&sd);
                        break;
                    case 5:
                        if (auxIdx < NUM_AUX_OUTPUTS)
                            Settings.AuxProgram[auxIdx].Units = (uint8_t)pd_uint32(&sd);
                        else pd_uint32(&sd);
                        break;
                    case 6: {
                        /* AuxRule submessage */
                        size_t rlen;
                        const uint8_t *rdata = pd_bytes(&sd, &rlen);
                        if (auxIdx < NUM_AUX_OUTPUTS && ruleIdx < NUM_AUX_PROGRAM_RULES) {
                            AUX_PROGRAM_RULE *rule = &Settings.AuxProgram[auxIdx].Rule[ruleIdx];
                            PbDecoder rd; pd_init(&rd, rdata, rlen);
                            while (pd_has_data(&rd)) {
                                uint8_t rwt; uint32_t rfn = pd_tag(&rd, &rwt);
                                if (rfn == 0) break;
                                switch (rfn) {
                                    case 1: rule->Type = pd_uint32(&rd); break;
                                    case 2: rule->IoIndex = (int16_t)pd_uint32(&rd); break;
                                    case 3: rule->State = (uint8_t)pd_uint32(&rd); break;
                                    case 4: rule->Op = pd_uint32(&rd); break;
                                    case 5: rule->SensorValue = pd_float(&rd); break;
                                    case 6: rule->AndOr = pd_uint32(&rd); break;
                                    case 7: rule->ReferenceIndex = (int16_t)pd_uint32(&rd); break;
                                    default: pd_skip(&rd, rwt); break;
                                }
                            }
                            ruleIdx++;
                        }
                        break;
                    }
                    default: pd_skip(&sd, swt); break;
                }
            }
            break;
        }
    APPLY_FIELDS_END
}

/* ── Analog Board (field 42) — mirrors StoreAnalogBoard ───────────────── */
static void apply_analog_board(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    uint32_t boardIdx = 0;
    APPLY_FIELDS_BEGIN
        case 1: boardIdx = pd_uint32(D); break;
        case 6: {
            /* Sensor submessage: field 1=slot, 3=label, 4=offset, 5=disabled */
            size_t slen;
            const uint8_t *sdata = pd_bytes(D, &slen);
            if (boardIdx < ANALOG_BOARDS_PER_SYSTEM) {
                PbDecoder sd; pd_init(&sd, sdata, slen);
                uint32_t slot = 0;
                while (pd_has_data(&sd)) {
                    uint8_t swt; uint32_t sfn = pd_tag(&sd, &swt);
                    if (sfn == 0) break;
                    switch (sfn) {
                        case 1: slot = pd_uint32(&sd); break;
                        case 3:
                            if (slot < ANALOG_SENSORS_PER_BOARD)
                                pd_string_into(&sd, Settings.AnalogBoard[boardIdx].Sensor[slot].Label, BOARD_LABELS + 1);
                            else { size_t _l; pd_bytes(&sd, &_l); }
                            break;
                        case 4:
                            if (slot < ANALOG_SENSORS_PER_BOARD)
                                Settings.AnalogBoard[boardIdx].Sensor[slot].Offset = pd_float(&sd);
                            else pd_float(&sd);
                            break;
                        case 5:
                            if (slot < ANALOG_SENSORS_PER_BOARD)
                                Settings.AnalogBoard[boardIdx].Sensor[slot].Disabled = (uint8_t)pd_uint32(&sd);
                            else pd_uint32(&sd);
                            break;
                        default: pd_skip(&sd, swt); break;
                    }
                }
            } else {
                size_t _skip; pd_bytes(D, &_skip);
            }
            break;
        }
    APPLY_FIELDS_END
}

/* ── Graph Favorites (field 43) — mirrors StoreGraphFavorites ─────────── */
static void apply_graph_favorites(const uint8_t *d, size_t len)
{
    APPLY_BEGIN(d, len);
    APPLY_FIELDS_BEGIN
        case 1: pd_string_into(D, Settings.Log.GraphFavorites, MSG_TX_CGIVAR_SIZE); break;
    APPLY_FIELDS_END
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Firmware update command handlers                                       */
/* ═══════════════════════════════════════════════════════════════════════ */

static void send_fw_status(uint32_t error_code, const char *error_msg)
{
    NovaMsg_SendFwUpdateStatus(
        (uint32_t)NovaFwUpdate_GetState(),
        NovaFwUpdate_GetBytesWritten(),
        NovaFwUpdate_GetTotalSize(),
        error_code,
        error_msg,
        NovaFwUpdate_GetActiveBank());
}

static void handle_fw_begin_update(const uint8_t *data, size_t len, uint32_t seq)
{
    PbDecoder d;
    pd_init(&d, data, len);

    uint32_t total_size = 0, crc32 = 0, chunk_size = 1024;
    char version[32] = {0};

    while (pd_has_data(&d)) {
        uint8_t wt;
        uint32_t field = pd_tag(&d, &wt);
        if (field == 0) break;
        switch (field) {
            case 1: total_size = pd_uint32(&d); break;
            case 2: crc32      = pd_uint32(&d); break;
            case 3: pd_string_into(&d, version, sizeof(version)); break;
            case 4: chunk_size = pd_uint32(&d); break;
            default: pd_skip(&d, wt); break;
        }
    }

    uint32_t err = NovaFwUpdate_Begin(total_size, crc32, version, chunk_size);
    if (err == 0) {
        NovaMsg_SendAck(seq, 0 /* ACK_OK */);
    } else {
        NovaMsg_SendAck(seq, 3 /* ACK_BUSY */);
    }
    send_fw_status(err, err ? "Begin failed" : "");
}

static void handle_fw_data_chunk(const uint8_t *data, size_t len, uint32_t seq)
{
    PbDecoder d;
    pd_init(&d, data, len);

    uint32_t offset = 0, chunk_crc = 0;
    const uint8_t *chunk_data = NULL;
    size_t chunk_len = 0;

    while (pd_has_data(&d)) {
        uint8_t wt;
        uint32_t field = pd_tag(&d, &wt);
        if (field == 0) break;
        switch (field) {
            case 1: offset    = pd_uint32(&d); break;
            case 2: chunk_data = pd_bytes(&d, &chunk_len); break;
            case 3: chunk_crc = pd_uint32(&d); break;
            default: pd_skip(&d, wt); break;
        }
    }

    if (!chunk_data || chunk_len == 0) {
        NovaMsg_SendAck(seq, 1 /* ACK_INVALID_PARAM */);
        return;
    }

    uint32_t err = NovaFwUpdate_WriteChunk(offset, chunk_data, (uint32_t)chunk_len, chunk_crc);
    if (err == 0) {
        NovaMsg_SendAck(seq, 0 /* ACK_OK */);
    } else {
        NovaMsg_SendAck(seq, 1 /* ACK_INVALID_PARAM */);
        send_fw_status(err, "Chunk write failed");
    }

    /* Send progress status every 16 chunks to avoid UART flooding */
    if (err == 0 && (NovaFwUpdate_GetBytesWritten() % (16 * 1024)) == 0) {
        send_fw_status(0, "");
    }
}

static void handle_fw_finalize(const uint8_t *data, size_t len, uint32_t seq)
{
    PbDecoder d;
    pd_init(&d, data, len);

    uint32_t crc32 = 0;
    while (pd_has_data(&d)) {
        uint8_t wt;
        uint32_t field = pd_tag(&d, &wt);
        if (field == 0) break;
        switch (field) {
            case 1: crc32 = pd_uint32(&d); break;
            default: pd_skip(&d, wt); break;
        }
    }

    uint32_t err = NovaFwUpdate_Finalize(crc32);
    if (err == 0) {
        NovaMsg_SendAck(seq, 0 /* ACK_OK */);
    } else {
        NovaMsg_SendAck(seq, 1 /* ACK_INVALID_PARAM */);
    }
    send_fw_status(err, err ? "Verify failed" : "");
}

static void handle_fw_activate(const uint8_t *data, size_t len, uint32_t seq)
{
    PbDecoder d;
    pd_init(&d, data, len);

    uint32_t reboot = 0;
    while (pd_has_data(&d)) {
        uint8_t wt;
        uint32_t field = pd_tag(&d, &wt);
        if (field == 0) break;
        switch (field) {
            case 1: reboot = pd_uint32(&d); break;
            default: pd_skip(&d, wt); break;
        }
    }

    NovaMsg_SendAck(seq, 0 /* ACK_OK */);
    send_fw_status(0, "Activating...");

    /* This may not return if reboot=true */
    NovaFwUpdate_Activate(reboot ? true : false);
}
