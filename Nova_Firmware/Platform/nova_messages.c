/**
 * nova_messages.c — Build and send protobuf Envelope messages
 *
 * Strategy: Until nanopb-generated code is available, we use a lightweight
 * manual protobuf encoder. Protobuf wire format is simple:
 *   - Varint fields: [field<<3|0] [varint value]
 *   - Fixed32/float: [field<<3|5] [4 bytes little-endian]
 *   - Length-delimited (string, bytes, submsg): [field<<3|2] [varint len] [data]
 *
 * When nanopb codegen is integrated, this file will be refactored to use
 * the generated encode functions. The API (NovaMsg_Send*) stays the same.
 *
 * Each NovaMsg_Send* function:
 *   1. Encodes the inner message (e.g., SystemStatus) into a buffer
 *   2. Wraps it in an Envelope (protocol_version, seq, oneof field)
 *   3. Calls NovaProto_SendRaw() with the encoded bytes
 */
#include "nova_messages.h"
#include "nova_protocol.h"

/* Firmware application headers (from Mini_IO via ../../Mini_IO/Application) */
#include "Settings.h"
#include "Controls.h"
#include "Modes.h"
#include "Warnings.h"
#include "SerialShift.h"
#include "PWM.h"
#include "RTC.h"
#include "Analog_Input.h"
#include "States.h"
#include "hal_orbit.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ─── Sequence counter ────────────────────────────────────────────────── */

static uint32_t s_seq_counter = 0;

uint32_t NovaMsg_NextSeq(void)
{
    return ++s_seq_counter;
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Minimal protobuf encoder (replaces nanopb until codegen is integrated) */
/* ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t *buf;
    size_t   pos;
    size_t   max;
    bool     overflow;
} PbEncoder;

static void pb_init(PbEncoder *e, uint8_t *buf, size_t max)
{
    e->buf = buf;
    e->pos = 0;
    e->max = max;
    e->overflow = false;
}

static void pb_put(PbEncoder *e, uint8_t byte)
{
    if (e->pos < e->max) {
        e->buf[e->pos++] = byte;
    } else {
        e->overflow = true;
    }
}

static void pb_write(PbEncoder *e, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        pb_put(e, data[i]);
    }
}

/* Encode a varint (variable-length integer) */
static void pb_varint(PbEncoder *e, uint64_t val)
{
    while (val > 0x7F) {
        pb_put(e, (uint8_t)(val & 0x7F) | 0x80);
        val >>= 7;
    }
    pb_put(e, (uint8_t)val);
}

/* Encode a field tag */
static void pb_tag(PbEncoder *e, uint32_t field, uint8_t wire_type)
{
    pb_varint(e, ((uint64_t)field << 3) | wire_type);
}

/* Wire types */
#define PB_WIRE_VARINT  0
#define PB_WIRE_64BIT   1
#define PB_WIRE_LEN     2
#define PB_WIRE_32BIT   5

/* Encode a uint32 field (varint) — skip if zero (proto3 default) */
static void pb_uint32(PbEncoder *e, uint32_t field, uint32_t val)
{
    if (val == 0) return;
    pb_tag(e, field, PB_WIRE_VARINT);
    pb_varint(e, val);
}

/* Encode a uint32 field UNCONDITIONALLY (does not skip zero).
 * Use for fields where the receiver must distinguish "explicitly zero"
 * from "absent" — e.g. a humidifier index where 0 is a valid unit number,
 * or a Mode enum where 0 (Manual) is a meaningful selection. */
static void pb_uint32_force(PbEncoder *e, uint32_t field, uint32_t val)
{
    pb_tag(e, field, PB_WIRE_VARINT);
    pb_varint(e, val);
}

/* Encode a bool field — skip if false */
static void pb_bool(PbEncoder *e, uint32_t field, bool val)
{
    if (!val) return;
    pb_tag(e, field, PB_WIRE_VARINT);
    pb_varint(e, 1);
}

/* Encode a float field (fixed32) — skip if zero */
static void pb_float(PbEncoder *e, uint32_t field, float val)
{
    if (val == 0.0f) return;
    pb_tag(e, field, PB_WIRE_32BIT);
    uint32_t bits;
    memcpy(&bits, &val, 4);
    pb_put(e, (uint8_t)(bits));
    pb_put(e, (uint8_t)(bits >> 8));
    pb_put(e, (uint8_t)(bits >> 16));
    pb_put(e, (uint8_t)(bits >> 24));
}

/* Encode a string field — skip if empty */
static void pb_string(PbEncoder *e, uint32_t field, const char *str)
{
    if (!str || str[0] == '\0') return;
    size_t len = strlen(str);
    pb_tag(e, field, PB_WIRE_LEN);
    pb_varint(e, len);
    pb_write(e, (const uint8_t *)str, len);
}

/* Encode a bytes/submessage field from pre-encoded buffer.
 *
 * Always emits tag + length, even when len == 0.  In proto3, a submessage
 * whose every field is the default value will encode to an empty byte
 * string — but the *outer* envelope still needs the field present so the
 * receiver can dispatch the oneof.  Skipping the tag here drops the entire
 * dispatch hint and the bridge sees a bare envelope with no payload field.
 * (Same bug existed in the bridge encoder — fixed there in PbEncoder.submsg.) */
static void pb_submsg(PbEncoder *e, uint32_t field, const uint8_t *data, size_t len)
{
    pb_tag(e, field, PB_WIRE_LEN);
    pb_varint(e, len);
    if (len > 0) pb_write(e, data, len);
}

/* Encode an enum field (varint) — skip if zero */
#define pb_enum pb_uint32

/* ═══════════════════════════════════════════════════════════════════════ */
/* Envelope wrapper                                                       */
/* ═══════════════════════════════════════════════════════════════════════ */

/* Build an Envelope around a pre-encoded inner message.
 * Envelope fields: protocol_version(1), seq(2), oneof msg(field_num)
 * Returns encoded size, or 0 on overflow. */
static size_t build_envelope(uint8_t *out, size_t out_max,
                             uint32_t msg_field,
                             const uint8_t *inner, size_t inner_len)
{
    PbEncoder e;
    pb_init(&e, out, out_max);

    /* field 1: protocol_version */
    pb_uint32(&e, 1, NOVA_PROTOCOL_VERSION);

    /* field 2: seq */
    pb_uint32(&e, 2, NovaMsg_NextSeq());

    /* field msg_field: the oneof payload (length-delimited submessage) */
    pb_submsg(&e, msg_field, inner, inner_len);

    return e.overflow ? 0 : e.pos;
}

/* Convenience: encode inner message + wrap in envelope + send.
 *
 * envelope_buf is file-scope static (4 KB) rather than on stack.  All
 * NovaMsg_Send* paths run from a single task (ThreadUIUpdate) so a
 * shared static buffer is safe.  Putting 4 KB on stack inside a deep
 * call chain (FeedByte → dispatch → handler → Send → send_envelope →
 * NovaProto_SendRaw → nova_uart_tx) brings the task uncomfortably
 * close to its stack limit and was correlated with post-ACK
 * PREFETCH_ABORTs caused by a corrupted return frame. */
static uint8_t s_envelope_buf[NOVA_MAX_PAYLOAD_SIZE];
static bool send_envelope(uint32_t msg_field,
                          const uint8_t *inner, size_t inner_len)
{
    size_t env_len = build_envelope(s_envelope_buf, sizeof(s_envelope_buf),
                                    msg_field, inner, inner_len);
    if (env_len == 0) return false;
    return NovaProto_SendRaw(s_envelope_buf, env_len);
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Message builders                                                       */
/* ═══════════════════════════════════════════════════════════════════════ */

/* Extern declarations for firmware globals we need.
 * These are defined in Mini_IO Application code. */
extern char CurrentMode;

/* Burner-cure substate (legacy CURE_STATE enum in States.h).  Tracked by the
 * legacy ARM control thread (ModeBurnerCure / ModeAirCure).  Reported to the
 * UI so dashboards can show "Burner Cure: modulating door" etc.  When not in
 * a cure mode the value is CS_OFF (0). */
extern char CureState;
extern char SystemState;
extern float PlenumTempAvg;
extern float StartTemp;
extern uint32_t UptimeSeconds;
extern int CalculatedHumidity(void);

/* Sensor value helpers — read from analog board arrays.
 * Returns SENSOR_VAL_UNDEFINED (-9999) if board/sensor is disabled or absent. */
static float get_sensor_value(int board, int sensor)
{
    if (Settings.AnalogBoard[board].Disabled || !Settings.AnalogBoard[board].Present)
        return 0.0f;
    if (Settings.AnalogBoard[board].Sensor[sensor].Disabled)
        return 0.0f;
    float val = Settings.AnalogBoard[board].Sensor[sensor].Value;
    if (val == SENSOR_VAL_UNDEFINED)
        return 0.0f;
    return val;
}

/* Forward declarations */
static bool equip_output_on(int eq_index);

bool NovaMsg_SendSystemStatus(void)
{
    char fanSpeedStr[16] = "Off";
    char coolOutput[16] = "--";
    char coolLabel[4] = "0";
    char burnerOutput[16] = "--";

    /* Format fan speed mirroring FormatFanSpeed() */
    if (CheckInputs(SW_FAN_AUTO) && CheckOutputs(EQ_FAN)) {
        int pct = PWMValToPercent(PwmChannel[PWM_FAN].Output);
        snprintf(fanSpeedStr, sizeof(fanSpeedStr), "%d", pct);
    } else if (CheckInputs(SW_FAN_MANUAL) && CheckOutputs(EQ_FAN)) {
        strcpy(fanSpeedStr, "Manual");
    }
    if (CurrentMode == UI_FAILURE) {
        strcpy(fanSpeedStr, "Off");
    }

    /* Cooling/burner output — mirrors legacy FormatCoolingOutput() in
     * Mini_IO/Application/UI_Messages.c (which is file-static and not
     * linkable from the platform code).  CoolLabel selects how the UI
     * renders this value:
     *   "0" = cooling   (door PWM %)
     *   "1" = refrigeration (refrig PWM %)
     *   "2" = onion-cure burner mode
     */
    if (   (   SystemState == ST_REFRIG
            || SystemState == ST_REFRIGDEHUMID
            || SystemState == ST_DEFROST)
        && CurrentMode != UI_PURGE)
    {
        int pct = PWMValToPercent(PwmChannel[PWM_REFRIGERATION].Output);
        snprintf(coolOutput, sizeof(coolOutput), "%d", pct);
        coolLabel[0] = '1';

        if (SystemState == ST_REFRIGDEHUMID) {
            int bp = PWMValToPercent(PwmChannel[PWM_BURNER].Output);
            snprintf(burnerOutput, sizeof(burnerOutput), "%d", bp);
        }
    }
    else if (   Settings.SystemMode == SM_ONION
             && CheckInputs(SW_CURE_AUTO))
    {
        int bp = PWMValToPercent(PwmChannel[PWM_BURNER].Output);
        snprintf(burnerOutput, sizeof(burnerOutput), "%d", bp);

        if (CheckInputs(SW_FRESHAIR_MANUAL)) {
            strcpy(coolOutput, "Manual");
        } else if (CheckInputs(SW_FRESHAIR_AUTO)) {
            int dp = PWMValToPercent(PwmChannel[PWM_DOORS].Output);
            snprintf(coolOutput, sizeof(coolOutput), "%d", dp);
        } else {
            strcpy(coolOutput, "Off");
        }
        coolLabel[0] = '2';
    }
    else
    {
        if (CheckInputs(SW_FRESHAIR_MANUAL)) {
            strcpy(coolOutput, "Manual");
            coolLabel[0] = '0';
        } else if (CheckInputs(SW_FRESHAIR_AUTO)) {
            if (   SystemState == ST_COOLING
                || SystemState == ST_COOLDEHUMID
                || CurrentMode == UI_PURGE)
            {
                int dp = PWMValToPercent(PwmChannel[PWM_DOORS].Output);
                snprintf(coolOutput, sizeof(coolOutput), "%d", dp);
                coolLabel[0] = '0';

                if (SystemState == ST_COOLDEHUMID) {
                    int bp = PWMValToPercent(PwmChannel[PWM_BURNER].Output);
                    snprintf(burnerOutput, sizeof(burnerOutput), "%d", bp);
                }
            }
            /* else: leave default "--" so UI knows cooling not engaged */
        } else {
            strcpy(coolOutput, "Off");
            coolLabel[0] = '0';
        }
    }

    /* Get sensor values from analog boards */
    float plenumHumid = get_sensor_value(DEFAULT_HUMID_BOARD, SENSOR_PLENUM_HUMID);
    float outsideTemp = get_sensor_value(DEFAULT_TEMP_BOARD, SENSOR_OUTSIDE_TEMP);
    float outsideHumid = get_sensor_value(DEFAULT_HUMID_BOARD, SENSOR_OUTSIDE_HUMID);
    float returnHumid = get_sensor_value(DEFAULT_HUMID_BOARD, SENSOR_RETURN_HUMID);
    float returnTemp = get_sensor_value(DEFAULT_TEMP_BOARD, SENSOR_RETURN_TEMP);
    float co2Level = get_sensor_value(DEFAULT_HUMID_BOARD, SENSOR_CO2);
    int calcHumid = CalculatedHumidity();

    uint8_t inner[256];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_float(&e,  1, PlenumTempAvg);
    pb_float(&e,  2, plenumHumid);
    pb_float(&e,  3, outsideTemp);
    pb_float(&e,  4, 0.0f);              /* remote_temp — from master/slave */
    pb_float(&e,  5, outsideHumid);
    pb_float(&e,  6, 0.0f);              /* remote_humid — from master/slave */
    pb_float(&e,  7, StartTemp);
    pb_float(&e,  8, returnHumid);
    pb_float(&e,  9, returnTemp);
    pb_float(&e, 10, co2Level);
    pb_string(&e, 11, fanSpeedStr);
    pb_string(&e, 12, coolOutput);
    pb_string(&e, 13, coolLabel);
    pb_string(&e, 14, burnerOutput);
    pb_float(&e, 15, calcHumid != SENSOR_VAL_UNDEFINED ? (float)calcHumid : 0.0f);
    pb_enum(&e,  16, (uint32_t)CurrentMode);
    pb_uint32(&e, 17, (uint32_t)(unsigned char)CureState);

    if (e.overflow) return false;
    return send_envelope(MSG_SYSTEM_STATUS, inner, e.pos);
}

bool NovaMsg_SendEquipmentStatus(void)
{
    uint8_t items_buf[512];
    PbEncoder items;
    pb_init(&items, items_buf, sizeof(items_buf));

    /* Build repeated EquipState submessages.
     * Each EquipState is a submessage inside field 1 of EquipmentStatus. */
    for (int i = 0; i < EQ_TOTAL_IO; i++) {
        uint8_t item[32];
        PbEncoder ie;
        pb_init(&ie, item, sizeof(item));

        pb_uint32(&ie, 1, (uint32_t)i);              /* eq_index */
        pb_bool(&ie, 2, equip_output_on(i));          /* output_on */
        pb_enum(&ie, 3, Settings.RemoteOff[i]);        /* remote_off */
        /* alarm: check warning status for equipment-related warnings */
        {
            uint8_t alarm = 0;
            if (i < NUM_WARNINGS) {
                char ws = WarningStatus((WARNING_ITEMS)i);
                if (ws != 0) alarm = (uint8_t)ws;
            }
            pb_enum(&ie, 4, alarm);                    /* alarm severity */
        }
        /* field 5: label — sent separately in IoDefinition */

        if (!ie.overflow) {
            pb_submsg(&items, 1, item, ie.pos);  /* repeated field 1 */
        }
    }

    if (items.overflow) return false;
    return send_envelope(MSG_EQUIPMENT_STATUS, items_buf, items.pos);
}

bool NovaMsg_SendDateTime(void)
{
    char dateStr[16], timeStr[16];
    uint8_t amPm = 0;

    GetDateStr(dateStr);
    GetTimeStr(timeStr, &amPm);

    uint8_t inner[64];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_string(&e, 1, dateStr);
    pb_string(&e, 2, timeStr);
    pb_uint32(&e, 3, (uint32_t)amPm);

    if (e.overflow) return false;
    return send_envelope(MSG_DATE_TIME, inner, e.pos);
}

bool NovaMsg_SendBasicSetup(void)
{
    uint8_t inner[128];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_string(&e, 1, Settings.StorageName);
    pb_uint32(&e, 2, (uint32_t)(unsigned char)Settings.TempType);
    pb_uint32(&e, 3, (uint32_t)Settings.SystemMode);
    pb_string(&e, 4, Settings.HomePage);
    pb_uint32(&e, 5, Settings.SystemMode);
    pb_uint32(&e, 6, (uint32_t)(unsigned char)Settings.Language);
    pb_string(&e, 7, Settings.MasterIP);
    pb_uint32(&e, 8, Settings.MultiviewSessions);
    pb_string(&e, 9, Settings.LoginPw);
    pb_uint32(&e, 10, Settings.LocalLogin);
    pb_uint32(&e, 11, Settings.Animations);

    if (e.overflow) return false;
    return send_envelope(MSG_BASIC_SETUP, inner, e.pos);
}

bool NovaMsg_SendPlenumSettings(void)
{
    uint8_t inner[64];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_float(&e,  1, Settings.Plenum.TempSet);
    pb_uint32(&e, 2, Settings.Plenum.HumidSet);
    pb_uint32(&e, 3, Settings.Plenum.HumidSetpointRef);
    pb_float(&e,  4, Settings.Burner.TempSet);
    /* field 5: burner_threshold */

    if (e.overflow) return false;
    return send_envelope(MSG_PLENUM_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendBurnerSettings(void)
{
    uint8_t inner[64];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, (uint32_t)(unsigned char)Settings.Burner.On);
    pb_uint32(&e, 2, (uint32_t)(unsigned char)Settings.Burner.Low);
    pb_float(&e,  3, (float)Settings.Burner.PID.P);
    pb_float(&e,  4, (float)Settings.Burner.PID.I);
    pb_float(&e,  5, (float)Settings.Burner.PID.D);
    pb_float(&e,  6, (float)Settings.Burner.PID.U);
    pb_uint32(&e, 7, (uint32_t)(unsigned char)Settings.Burner.Mode);
    pb_uint32(&e, 8, (uint32_t)(unsigned char)Settings.Burner.Manual);

    if (e.overflow) return false;
    return send_envelope(MSG_BURNER_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendCo2Settings(void)
{
    uint8_t inner[64];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, (uint32_t)(unsigned char)Settings.Co2.Purge.Mode);
    pb_float(&e,  2, Settings.Co2.Purge.MinTemp);
    pb_float(&e,  3, Settings.Co2.Purge.MaxTemp);
    pb_uint32(&e, 4, (uint32_t)(unsigned char)Settings.Co2.Purge.Duration);
    pb_uint32(&e, 5, (uint32_t)Settings.Co2.Set);
    pb_uint32(&e, 6, (uint32_t)(unsigned char)Settings.Co2.FanOutput);
    pb_uint32(&e, 7, (uint32_t)(unsigned char)Settings.Co2.DoorOutput);

    if (e.overflow) return false;
    return send_envelope(MSG_CO2_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendCureSettings(void)
{
    uint8_t inner[64];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_float(&e,  1, Settings.Cure.StartTemp);
    pb_uint32(&e, 2, (uint32_t)(unsigned char)Settings.Cure.HumidRef);
    pb_float(&e,  3, (float)(unsigned char)Settings.Cure.StartHumid);
    pb_float(&e,  4, (float)(unsigned char)Settings.Cure.HumidHighLimit);

    if (e.overflow) return false;
    return send_envelope(MSG_CURE_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendFanBoostSettings(void)
{
    uint8_t inner[32];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, (uint32_t)(unsigned char)Settings.FanBoost.Mode);
    pb_uint32(&e, 2, (uint32_t)(unsigned char)Settings.FanBoost.Speed);
    pb_uint32(&e, 3, (uint32_t)(unsigned char)Settings.FanBoost.Interval);
    pb_uint32(&e, 4, (uint32_t)(unsigned char)Settings.FanBoost.Duration);
    pb_float(&e,  5, Settings.FanBoost.Temp);

    if (e.overflow) return false;
    return send_envelope(MSG_FAN_BOOST_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendClimacellSettings(void)
{
    uint8_t inner[64];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, (uint32_t)(unsigned char)Settings.Climacell.Efficiency);
    pb_uint32(&e, 2, (uint32_t)Settings.Climacell.Altitude);
    pb_uint32(&e, 3, (uint32_t)(unsigned char)Settings.Climacell.AltitudeUnits);
    pb_float(&e,  4, (float)Settings.Climacell.PID.P);
    pb_float(&e,  5, (float)Settings.Climacell.PID.I);
    pb_float(&e,  6, (float)Settings.Climacell.PID.D);
    pb_float(&e,  7, (float)Settings.Climacell.PID.U);

    if (e.overflow) return false;
    return send_envelope(MSG_CLIMACELL_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendHeartbeat(void)
{
    extern size_t xPortGetFreeHeapSize(void);

    uint8_t inner[32];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, UptimeSeconds);
    pb_uint32(&e, 2, (uint32_t)xPortGetFreeHeapSize());
    const NovaProto_Stats *stats = NovaProto_GetStats();
    pb_uint32(&e, 3, stats->tx_frames);
    pb_uint32(&e, 4, stats->rx_frames);

    if (e.overflow) return false;
    return send_envelope(MSG_HEARTBEAT, inner, e.pos);
}

bool NovaMsg_SendAck(uint32_t ref_seq, uint8_t status)
{
    uint8_t inner[16];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, ref_seq);
    pb_enum(&e, 2, status);

    if (e.overflow) return false;
    return send_envelope(MSG_ACK, inner, e.pos);
}

bool NovaMsg_SendPasswordResponse(int8_t level)
{
    uint8_t inner[16];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* PasswordResponse: field 1 = level (int32 as varint) */
    pb_tag(&e, 1, PB_WIRE_VARINT);
    /* Encode signed value as varint (protobuf uses zigzag for sint32,
     * but we use regular varint since the bridge knows the range) */
    pb_varint(&e, (uint64_t)(uint32_t)(int32_t)level);

    if (e.overflow) return false;
    return send_envelope(MSG_PASSWORD_RESPONSE, inner, e.pos);
}

bool NovaMsg_SendDataLoadStatus(bool ready, uint32_t session_id)
{
    uint8_t inner[16];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_bool(&e, 1, ready);
    pb_uint32(&e, 2, session_id);

    if (e.overflow) return false;
    return send_envelope(MSG_DATA_LOAD_STATUS, inner, e.pos);
}

/* ─── Log chunk streaming ─────────────────────────────────────────────── */

bool NovaMsg_SendLogChunk(uint32_t log_type, uint32_t chunk_index,
                          uint32_t total_chunks,
                          const uint8_t *data, size_t data_len,
                          const char *metadata)
{
    /* 4 KB on stack would risk overflow — UI task already has the
     * envelope build path active.  Use file-scope static (single-task
     * sender). */
    static uint8_t inner[4096];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* LogChunk: log_type(1), chunk_index(2), total_chunks(3), data(4), metadata(5) */
    pb_enum(&e, 1, log_type);
    pb_uint32(&e, 2, chunk_index);
    pb_uint32(&e, 3, total_chunks);
    if (data && data_len > 0) {
        pb_tag(&e, 4, PB_WIRE_LEN);
        pb_varint(&e, data_len);
        pb_write(&e, data, data_len);
    }
    if (metadata && metadata[0]) {
        pb_string(&e, 5, metadata);
    }

    if (e.overflow) return false;
    return send_envelope(MSG_LOG_CHUNK, inner, e.pos);
}

/* ─── LogRecord — periodic data snapshot for RPi5 SQLite storage ──────── */

size_t NovaMsg_EncodeLogRecord(uint8_t *buf, size_t bufsize, uint32_t sequence)
{
    PbEncoder e;
    pb_init(&e, buf, bufsize);

    /* Date and time */
    char dateStr[16], timeStr[16];
    uint8_t amPm = 0;
    GetDateStr(dateStr);
    GetTimeStr(timeStr, &amPm);
    pb_string(&e, 1, dateStr);
    pb_string(&e, 2, timeStr);

    /* Core process values */
    pb_float(&e, 3, StartTemp);                          /* plenum_temp_sp */
    pb_float(&e, 4, PlenumTempAvg);                      /* plenum_temp */
    pb_float(&e, 5, get_sensor_value(DEFAULT_TEMP_BOARD, SENSOR_OUTSIDE_TEMP)); /* cool_avl_temp */
    pb_float(&e, 6, (float)Settings.Plenum.HumidSet);    /* plenum_humid_sp */
    pb_uint32(&e, 7, (uint32_t)CurrentMode);              /* mode */

    /* Fan speed as percentage */
    {
        uint32_t fanPct = 0;
        if (CheckInputs(SW_FAN_AUTO) && CheckOutputs(EQ_FAN)) {
            fanPct = (uint32_t)PWMValToPercent(PwmChannel[PWM_FAN].Output);
        }
        pb_uint32(&e, 8, fanPct);                         /* fan_speed */
    }

    /* Equipment outputs as on counts */
    pb_uint32(&e, 9,  equip_output_on(EQ_CLIMACELL) ? 1 : 0);     /* cool_output */
    pb_uint32(&e, 10, equip_output_on(EQ_REFRIGERATION) ? 1 : 0);  /* refrig_output */
    pb_uint32(&e, 11, equip_output_on(EQ_BURNER) ? 1 : 0);         /* burner_output */

    /* Calculated humidity */
    {
        int ch = CalculatedHumidity();
        pb_float(&e, 12, ch != SENSOR_VAL_UNDEFINED ? (float)ch : 0.0f);
    }

    /* Fan runtime in minutes */
    pb_uint32(&e, 13, Settings.Fan.DailyRunTime / 60);    /* fan_runtime_min */

    /* CO2 */
    pb_float(&e, 14, get_sensor_value(DEFAULT_HUMID_BOARD, SENSOR_CO2));

    /* Temp sensors (repeated SensorValue: field 15) */
    for (int b = 0; b < ANALOG_BOARDS_PER_SYSTEM; b++) {
        if (!Settings.AnalogBoard[b].Present) continue;
        for (int s = 0; s < ANALOG_SENSORS_PER_BOARD; s++) {
            if (Settings.AnalogBoard[b].Sensor[s].Disabled) continue;
            float val = Settings.AnalogBoard[b].Sensor[s].Value;
            if (val == SENSOR_VAL_UNDEFINED) continue;
            int sType = Settings.AnalogBoard[b].Sensor[s].Type;
            /* Temperature sensor types: 0(IR),3(Temp),4,5,9,11 */
            if (sType == 0 || sType == 3 || sType == 4 ||
                sType == 5 || sType == 9 || sType == 11) {
                uint8_t sv[16];
                PbEncoder sve;
                pb_init(&sve, sv, sizeof(sv));
                pb_uint32(&sve, 1, (uint32_t)(b * ANALOG_SENSORS_PER_BOARD + s));
                pb_float(&sve, 2, val);
                if (!sve.overflow)
                    pb_submsg(&e, 15, sv, sve.pos);
            }
        }
    }

    /* Humid sensors (repeated SensorValue: field 16) */
    for (int b = 0; b < ANALOG_BOARDS_PER_SYSTEM; b++) {
        if (!Settings.AnalogBoard[b].Present) continue;
        for (int s = 0; s < ANALOG_SENSORS_PER_BOARD; s++) {
            if (Settings.AnalogBoard[b].Sensor[s].Disabled) continue;
            float val = Settings.AnalogBoard[b].Sensor[s].Value;
            if (val == SENSOR_VAL_UNDEFINED) continue;
            int sType = Settings.AnalogBoard[b].Sensor[s].Type;
            /* Humidity sensor types: 1,6,7,10 */
            if (sType == 1 || sType == 6 || sType == 7 || sType == 10) {
                uint8_t sv[16];
                PbEncoder sve;
                pb_init(&sve, sv, sizeof(sv));
                pb_uint32(&sve, 1, (uint32_t)(b * ANALOG_SENSORS_PER_BOARD + s));
                pb_float(&sve, 2, val);
                if (!sve.overflow)
                    pb_submsg(&e, 16, sv, sve.pos);
            }
        }
    }

    /* Sequence number (field 17) */
    pb_uint32(&e, 17, sequence);

    if (e.overflow) return 0;
    return e.pos;
}

bool NovaMsg_SendLogRecordRaw(const uint8_t *inner, size_t inner_len)
{
    return send_envelope(MSG_LOG_RECORD, inner, inner_len);
}

bool NovaMsg_SendLogRecord(void)
{
    uint8_t inner[512];
    size_t len = NovaMsg_EncodeLogRecord(inner, sizeof(inner), 0);
    if (len == 0) return false;
    return send_envelope(MSG_LOG_RECORD, inner, len);
}

/* ─── ActivityEvent — pushed on equipment/warning state changes ───────── */

bool NovaMsg_SendActivityEvent(uint32_t event_type, uint32_t eq_index,
                               const char *description, uint32_t new_state)
{
    uint8_t inner[128];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    char dateStr[16], timeStr[16];
    uint8_t amPm = 0;
    GetDateStr(dateStr);
    GetTimeStr(timeStr, &amPm);

    pb_string(&e, 1, dateStr);
    pb_string(&e, 2, timeStr);
    pb_uint32(&e, 3, event_type);
    pb_uint32(&e, 4, eq_index);
    if (description && description[0]) {
        pb_string(&e, 5, description);
    }
    pb_uint32(&e, 6, new_state);
    pb_uint32(&e, 7, (uint32_t)CurrentMode);

    if (e.overflow) return false;
    return send_envelope(MSG_ACTIVITY_EVENT, inner, e.pos);
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Batch send: all settings after handshake                               */
/* Mirrors the sequence from UI_SendAllSettings in Mini_IO                */
/* ═══════════════════════════════════════════════════════════════════════ */

void NovaMsg_SendAllSettings(void)
{
    NovaMsg_SendSystemStatus();
    NovaMsg_SendBasicSetup();
    NovaMsg_SendDateTime();
    NovaMsg_SendEquipmentStatus();
    NovaMsg_SendPlenumSettings();
    NovaMsg_SendFanSpeedSettings();
    NovaMsg_SendFanBoostSettings();
    NovaMsg_SendRampRateSettings();
    NovaMsg_SendRefrigSettings();
    NovaMsg_SendBurnerSettings();
    NovaMsg_SendCo2Settings();
    NovaMsg_SendCureSettings();
    NovaMsg_SendClimacellSettings();
    NovaMsg_SendClimacellTimes();
    NovaMsg_SendHumidCtrlSettings();
    NovaMsg_SendOutsideAirSettings();
    NovaMsg_SendMiscSettings();
    NovaMsg_SendFailureSettings();
    NovaMsg_SendFailureSettings2();
    NovaMsg_SendTempAlarmSettings();
    NovaMsg_SendDoorSettings();
    NovaMsg_SendLoadMonitorSettings();
    NovaMsg_SendAuxProgram();
    NovaMsg_SendUserLogSettings();
    NovaMsg_SendPidSettings();
    NovaMsg_SendGraphFavorites();
    NovaMsg_SendEmailSettings();
    NovaMsg_SendAlertSettings();
    NovaMsg_SendPwmSettings();
    NovaMsg_SendIoConfig();
    NovaMsg_SendIoDefinition();
    NovaMsg_SendAvailableIo();
    NovaMsg_SendSensorLabels();
    NovaMsg_SendVersionInfo();
    NovaMsg_SendServiceInfo();
    NovaMsg_SendAccountSettings();
    NovaMsg_SendNetworkNodes();
    NovaMsg_SendRuntimes();
    NovaMsg_SendFanRuntime();
    NovaMsg_SendHumidModes();
    NovaMsg_SendAuxSwitches();

    /* Send all present analog board data */
    for (uint32_t b = 0; b < ANALOG_BOARDS_PER_SYSTEM; b++) {
        if (Settings.AnalogBoard[b].Present) {
            NovaMsg_SendAnalogBoard(b);
        }
    }
    NovaMsg_SendSensorData();

    NovaMsg_SendWarnings();
    NovaMsg_SendDataLoadStatus(true, NovaMsg_NextSeq());
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Additional extern declarations for remaining senders                   */
/* ═══════════════════════════════════════════════════════════════════════ */
extern BOARD_INFO IoBoard[BOARD_COUNT];

/* Helper: check if equipment output is physically on */
static bool equip_output_on(int eq_index)
{
    unsigned int pin = Settings.EquipIo[eq_index].Output;
    if (pin == IO_UNDEFINED) return false;
    return (IoBoard[MAIN].OutputState & pin) != 0;
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Stub implementations for remaining senders                             */
/* These will be filled in as the Settings struct fields are confirmed.    */
/* ═══════════════════════════════════════════════════════════════════════ */

bool NovaMsg_SendMode(void)                 { return NovaMsg_SendSystemStatus(); }

bool NovaMsg_SendWarnings(void)
{
    uint8_t inner[1024];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* Standard firmware warnings from Application layer */
    for (int i = 0; i < NUM_WARNINGS; i++) {
        char status = WarningStatus((WARNING_ITEMS)i);
        if (status == 0) continue;

        uint8_t item[64];
        PbEncoder ie;
        pb_init(&ie, item, sizeof(item));

        pb_uint32(&ie, 1, (uint32_t)i);         /* code */
        pb_enum(&ie, 2, (uint32_t)status);       /* severity */
        /* field 3: message — skip, bridge can look up from code */
        /* field 4: eq_index — 0 */
        /* field 5: timestamp — 0 */

        if (!ie.overflow) {
            pb_submsg(&e, 1, item, ie.pos);       /* repeated Warning */
        }
    }

    /* Orbit-specific warnings (E-Stop, safe mode, comm lost, etc.) */
    {
        orbit_warn_entry_t ow[NOVA_MAX_ORBITS * 5];
        int ow_count = orbit_get_active_warnings(ow, sizeof(ow) / sizeof(ow[0]));

        for (int j = 0; j < ow_count; j++) {
            uint8_t item[64];
            PbEncoder ie;
            pb_init(&ie, item, sizeof(item));

            pb_uint32(&ie, 1, (uint32_t)ow[j].code);     /* code (0x100+) */
            pb_enum(&ie, 2, (uint32_t)ow[j].severity);    /* severity */
            pb_uint32(&ie, 4, (uint32_t)ow[j].slot);      /* eq_index = orbit slot */

            if (!ie.overflow) {
                pb_submsg(&e, 1, item, ie.pos);
            }
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_WARNING_REPORT, inner, e.pos);
}

bool NovaMsg_SendVersionInfo(void)
{
    uint8_t inner[128];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_string(&e, 1, Settings.ArmVersion);        /* arm_version */
    pb_string(&e, 2, Settings.SettingsVersion);    /* bootloader_version */
    /* field 3: repeated BoardVersion — omit for now */

    if (e.overflow) return false;
    return send_envelope(MSG_VERSION_INFO, inner, e.pos);
}

bool NovaMsg_SendServiceInfo(void)
{
    uint8_t inner[128];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_string(&e, 1, Settings.DealerName);
    pb_string(&e, 2, Settings.DealerPhone);
    pb_string(&e, 3, Settings.TechName);
    pb_string(&e, 4, Settings.TechPhone);

    if (e.overflow) return false;
    return send_envelope(MSG_SERVICE_INFO, inner, e.pos);
}

bool NovaMsg_SendFanSpeedSettings(void)
{
    uint8_t inner[64];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, (uint32_t)(unsigned char)Settings.Fan.MaxSpeed);
    pb_uint32(&e, 2, (uint32_t)(unsigned char)Settings.Fan.MinSpeed);
    pb_uint32(&e, 3, (uint32_t)(unsigned char)Settings.Fan.RefrigSpeed);
    /* field 4: heat_speed — not in FAN_CTRL, skip */
    /* field 5: dry_speed — not in FAN_CTRL, skip */
    /* field 6: cure_speed — not in FAN_CTRL, skip */
    pb_uint32(&e, 7, (uint32_t)(unsigned char)Settings.Fan.RecircSpeed);
    /* field 8-10: defrost/manual/outside_air — not in FAN_CTRL, skip */

    if (e.overflow) return false;
    return send_envelope(MSG_FAN_SPEED_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendRampRateSettings(void)
{
    uint8_t inner[32];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_float(&e, 1, Settings.Ramp.UpdateTemp);    /* rate_per_day */
    pb_uint32(&e, 2, (uint32_t)(unsigned char)Settings.Ramp.UpdatePeriod); /* enabled (period > 0 = enabled) */

    if (e.overflow) return false;
    return send_envelope(MSG_RAMP_RATE_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendRefrigSettings(void)
{
    uint8_t inner[512];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* RefrigSettings (matches StoreRefrig + UI_SendRefrig in legacy)
     *   field  1: mode (uint32, also re-emitted at field 7 for back-compat)
     *   field  2: repeated RefrigStage stages (8 entries)
     *   field  3: p_gain (float — legacy stores as int but wire is float)
     *   field  4: i_gain
     *   field  5: d_gain
     *   field  6: u_limit
     *   field  7: mode (duplicate of field 1 — historical)
     *   field  8: defrost_period (hours)
     *   field  9: defrost_duration (mins)
     *   field 10: purge (uint32)
     *   field 11: purge_threshold (uint32 — Settings.Co2.Purge.RefrigThresh)
     *   field 12: limit (float — onion-only)
     *   field 13: fail_mode (uint32)
     *   field 14: repeated RefrigDefrost defrosts (2 entries)
     *
     * RefrigStage / RefrigDefrost wire shape (identical):
     *   field 1: on (uint32 0..100)
     *   field 2: off (uint32 0..100)
     *   field 3: diagnostic (uint32 0=auto 1=off 2=on)
     *
     * pb_uint32_force is required for `on`/`off`/`diagnostic` on the
     * inner messages because 0 is a meaningful threshold/state and must
     * not be dropped by the proto3 default-skip rule. */
    pb_uint32(&e, 1, (uint32_t)Settings.Refrig.Mode);

    for (int i = 0; i < NUM_REFRIG_STAGES; i++) {
        uint8_t stage[32];
        PbEncoder se;
        pb_init(&se, stage, sizeof(stage));
        pb_uint32_force(&se, 1, Settings.Refrig.Stage[i].On);
        pb_uint32_force(&se, 2, Settings.Refrig.Stage[i].Off);
        pb_uint32_force(&se, 3, Settings.Refrig.Stage[i].Diagnostic);
        if (!se.overflow) {
            pb_submsg(&e, 2, stage, se.pos);
        }
    }

    pb_float(&e, 3, (float)Settings.Refrig.PID.P);
    pb_float(&e, 4, (float)Settings.Refrig.PID.I);
    pb_float(&e, 5, (float)Settings.Refrig.PID.D);
    pb_float(&e, 6, (float)Settings.Refrig.PID.U);
    pb_uint32(&e, 7, (uint32_t)Settings.Refrig.Mode);
    pb_uint32(&e, 8, Settings.Refrig.DefrostPeriod);
    pb_uint32(&e, 9, Settings.Refrig.DefrostDuration);
    pb_uint32_force(&e, 10, (uint32_t)Settings.Refrig.Purge);
    pb_uint32_force(&e, 11, (uint32_t)Settings.Co2.Purge.RefrigThresh);
    pb_float (&e, 12, Settings.Refrig.Limit);
    pb_uint32(&e, 13, (uint32_t)Settings.Refrig.FailMode);

    for (int i = 0; i < NUM_DEFROST_STAGES; i++) {
        uint8_t def[32];
        PbEncoder se;
        pb_init(&se, def, sizeof(def));
        pb_uint32_force(&se, 1, Settings.Refrig.Defrost[i].On);
        pb_uint32_force(&se, 2, Settings.Refrig.Defrost[i].Off);
        pb_uint32_force(&se, 3, Settings.Refrig.Defrost[i].Diagnostic);
        if (!se.overflow) {
            pb_submsg(&e, 14, def, se.pos);
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_REFRIG_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendHumidCtrlSettings(void)
{
    /* Encode all NUM_HUMIDIFIERS as `repeated HumidCtrlEntry entries = 1`
     * inside HumidCtrlSettings. The bridge expects ALL humidifiers in one
     * envelope so it can build the 21-field HumidCtrlData CGI variable
     * (3 humidifiers × 7 fields per the legacy UI_SendHumCtrl format).
     *
     * Each entry carries: index, mode, cool_on, cool_off, recirc_on,
     * recirc_off, refrig_on, refrig_off  (proto fields 1..8). */
    uint8_t inner[256];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    for (int h = 0; h < NUM_HUMIDIFIERS; h++) {
        uint8_t item[40];
        PbEncoder ie;
        pb_init(&ie, item, sizeof(item));

        pb_uint32_force(&ie, 1, (uint32_t)h);
        pb_uint32_force(&ie, 2, (uint32_t)(unsigned char)Settings.HumidCtrl[h].Mode);
        pb_uint32(&ie, 3, (uint32_t)(unsigned char)Settings.HumidCtrl[h].DutyCycle[0].On);
        pb_uint32(&ie, 4, (uint32_t)(unsigned char)Settings.HumidCtrl[h].DutyCycle[0].Off);
        pb_uint32(&ie, 5, (uint32_t)(unsigned char)Settings.HumidCtrl[h].DutyCycle[1].On);
        pb_uint32(&ie, 6, (uint32_t)(unsigned char)Settings.HumidCtrl[h].DutyCycle[1].Off);
        pb_uint32(&ie, 7, (uint32_t)(unsigned char)Settings.HumidCtrl[h].DutyCycle[2].On);
        pb_uint32(&ie, 8, (uint32_t)(unsigned char)Settings.HumidCtrl[h].DutyCycle[2].Off);

        if (ie.overflow) return false;
        pb_submsg(&e, 1, item, ie.pos);  /* repeated entries field = 1 */
    }

    if (e.overflow) return false;
    return send_envelope(MSG_HUMID_CTRL_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendOutsideAirSettings(void)
{
    uint8_t inner[32];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, Settings.OutsideAir.CtrlMode);       /* mode */
    pb_float(&e, 2, Settings.OutsideAir.Diff);             /* differential */
    /* field 3: min_temp — not in OUTSIDE_AIR_CTRL, skip */
    /* field 4: max_temp — not in OUTSIDE_AIR_CTRL, skip */

    if (e.overflow) return false;
    return send_envelope(MSG_OUTSIDE_AIR_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendMiscSettings(void)
{
    uint8_t inner[32];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* Misc values: HeatTempThresh, KbPref, LightsFailUnits, CavStandbyOn */
    pb_float(&e, 1, Settings.HeatTempThresh);
    pb_uint32(&e, 2, Settings.KbPref);
    pb_uint32(&e, 3, Settings.LightsFailUnits);
    pb_uint32(&e, 4, Settings.CavityHeat.StandbyOn);

    if (e.overflow) return false;
    return send_envelope(MSG_MISC_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendFailureSettings(void)
{
    uint8_t inner[128];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* Failure modes: first 11 map to specific proto fields */
    pb_uint32(&e, 1, Settings.Failure[FAIL_FAN].Mode);
    pb_uint32(&e, 2, Settings.Failure[FAIL_FAN].Timer);
    /* Combine remaining failures as extra_limits */
    for (int i = 0; i < NUM_FAILURES; i++) {
        uint8_t item[8];
        PbEncoder ie;
        pb_init(&ie, item, sizeof(item));
        pb_uint32(&ie, 1, Settings.Failure[i].Mode);
        pb_uint32(&ie, 2, Settings.Failure[i].Timer);
        if (!ie.overflow) {
            pb_submsg(&e, 12, item, ie.pos);   /* repeated extra_limits */
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_FAILURE_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendFailureSettings2(void)
{
    /* Failure Settings Group 2 shares the same Failure[] array.
     * Encode upper-range failure modes. */
    uint8_t inner[64];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, Settings.Failure[FAIL_OUTSIDE_AIR].Mode);
    pb_uint32(&e, 2, Settings.Failure[FAIL_PLENUM_SENSOR].Mode);
    /* fields 3-13: extra environment/power/comm failures */

    if (e.overflow) return false;
    return send_envelope(MSG_FAILURE_SETTINGS2, inner, e.pos);
}

bool NovaMsg_SendTempAlarmSettings(void)
{
    /* Legacy AlarmTempLow= reply is 6 fields:
     *   1=LowAlarmTemp(float), 2=LowAlarmTimer(uint),
     *   3=HighAlarmTemp(float), 4=HighAlarmTimer(uint),
     *   5=Cure.TempLowLimit(float), 6=Cure.TempHighLimit(float).
     * Bridge composes plensetup[5..10] from this CSV in that order;
     * sending fewer fields leaves the alarm fields blank in the UI. */
    uint8_t inner[64];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_float (&e, 1, Settings.Plenum.LowAlarmTemp);
    pb_uint32(&e, 2, (uint32_t)(unsigned char)Settings.Plenum.LowAlarmTimer);
    pb_float (&e, 3, Settings.Plenum.HighAlarmTemp);
    pb_uint32(&e, 4, (uint32_t)(unsigned char)Settings.Plenum.HighAlarmTimer);
    pb_float (&e, 5, Settings.Cure.TempLowLimit);
    pb_float (&e, 6, Settings.Cure.TempHighLimit);

    if (e.overflow) return false;
    return send_envelope(MSG_TEMP_ALARM_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendDoorSettings(void)
{
    uint8_t inner[32];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_float(&e, 1, 0.0f);  /* air_pressure — not directly in DOOR_CTRL */
    pb_uint32(&e, 2, (uint32_t)Settings.Door.ActuatorTime);  /* door_sensor → actuator_time */
    pb_uint32(&e, 3, (uint32_t)(unsigned char)Settings.Door.CoolAirCycle);  /* close_delay → cool_air_cycle */

    if (e.overflow) return false;
    return send_envelope(MSG_DOOR_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendLoadMonitorSettings(void)
{
    uint8_t inner[128];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_string(&e, 1, Settings.LoadMonitor.Bay[0].Label);
    if (NUM_LOADLOG_SENSORS > 1) {
        pb_string(&e, 2, Settings.LoadMonitor.Bay[1].Label);
    }
    pb_uint32(&e, 3, NUM_LOADLOG_SENSORS);   /* bay_count */
    /* field 4-15: remaining LoadMonitor fields as needed */

    if (e.overflow) return false;
    return send_envelope(MSG_LOAD_MONITOR, inner, e.pos);
}

bool NovaMsg_SendAuxProgram(void)
{
    uint8_t inner[512];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    for (int a = 0; a < NUM_AUX_OUTPUTS; a++) {
        uint8_t aux[128];
        PbEncoder ae;
        pb_init(&ae, aux, sizeof(aux));

        pb_uint32(&ae, 1, (uint32_t)a);                               /* aux_index */
        /* field 2: eq_index — maps to EQ_AUX1+a */
        pb_uint32(&ae, 2, (uint32_t)(EQ_AUX1 + a));
        pb_uint32(&ae, 3, Settings.AuxProgram[a].DutyCycle);
        pb_uint32(&ae, 4, Settings.AuxProgram[a].Period);
        pb_uint32(&ae, 5, Settings.AuxProgram[a].Units);

        /* field 6: repeated AuxRule */
        for (int r = 0; r < NUM_AUX_PROGRAM_RULES; r++) {
            AUX_PROGRAM_RULE *rule = &Settings.AuxProgram[a].Rule[r];
            if (rule->Type == 0) continue;  /* skip empty rules */
            uint8_t rb[32];
            PbEncoder re;
            pb_init(&re, rb, sizeof(rb));
            pb_uint32(&re, 1, (uint32_t)rule->Type);
            pb_uint32(&re, 2, (uint32_t)rule->IoIndex);
            pb_uint32(&re, 3, rule->State);
            pb_uint32(&re, 4, (uint32_t)rule->Op);
            pb_float(&re, 5, rule->SensorValue);
            pb_uint32(&re, 6, (uint32_t)rule->AndOr);
            pb_uint32(&re, 7, (uint32_t)rule->ReferenceIndex);
            if (!re.overflow) {
                pb_submsg(&ae, 6, rb, re.pos);
            }
        }

        if (!ae.overflow) {
            pb_submsg(&e, 1, aux, ae.pos);  /* repeated — wrap in field 1 */
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_AUX_PROGRAM, inner, e.pos);
}

bool NovaMsg_SendUserLogSettings(void)
{
    uint8_t inner[16];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, (uint32_t)Settings.Log.User.Interval);
    pb_uint32(&e, 2, (uint32_t)(unsigned char)Settings.Log.User.Wrap);

    if (e.overflow) return false;
    return send_envelope(MSG_USER_LOG_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendPidSettings(void)
{
    uint8_t inner[32];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* PID log settings */
    pb_uint32(&e, 1, 0);  /* eq_index — general */
    pb_uint32(&e, 6, (uint32_t)(unsigned char)Settings.Log.PID.Wrap);

    if (e.overflow) return false;
    return send_envelope(MSG_PID_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendGraphFavorites(void)
{
    uint8_t inner[256];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* GraphFavorites is a CSV string in Settings.Log.GraphFavorites.
     * Send as a string for now — bridge can parse. */
    pb_string(&e, 1, Settings.Log.GraphFavorites);

    if (e.overflow) return false;
    return send_envelope(MSG_GRAPH_FAVORITES, inner, e.pos);
}

bool NovaMsg_SendEmailSettings(void)
{
    uint8_t inner[256];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_string(&e, 1, Settings.Email.Server);
    {
        char portStr[8];
        int len = 0;
        uint16_t p = Settings.Email.Port;
        if (p > 0) {
            /* Simple uint16 to string */
            char tmp[6];
            int n = 0;
            do { tmp[n++] = '0' + (p % 10); p /= 10; } while (p > 0);
            for (int i = n - 1; i >= 0; i--) portStr[len++] = tmp[i];
            portStr[len] = '\0';
            pb_string(&e, 2, portStr);
        }
    }
    pb_string(&e, 3, Settings.Email.Account);
    pb_string(&e, 4, Settings.Email.Password);
    pb_string(&e, 5, Settings.Email.From);
    pb_string(&e, 6, Settings.Email.To);
    pb_uint32(&e, 7, Settings.Email.Alerts);

    if (e.overflow) return false;
    return send_envelope(MSG_EMAIL_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendAlertSettings(void)
{
    uint8_t inner[128];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* AlertsToSend is a char array, each index: '0' or '1' */
    for (int i = 0; i < NUM_WARNINGS; i++) {
        if (Settings.AlertsToSend[i] == '1' || Settings.AlertsToSend[i] == 1) {
            pb_uint32(&e, 1, (uint32_t)i);   /* repeated alert_flags */
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_ALERT_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendPwmSettings(void)
{
    uint8_t inner[256];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    for (int i = 0; i < PWM_TOTAL_EQ; i++) {
        uint8_t ch[32];
        PbEncoder ce;
        pb_init(&ce, ch, sizeof(ch));

        pb_uint32(&ce, 1, (uint32_t)i);                       /* index */
        pb_uint32(&ce, 2, Settings.PWM[i].Enabled);            /* enabled */
        pb_uint32(&ce, 3, Settings.PWM[i].Channel);            /* frequency (channel assignment) */
        /* field 4: duty — not stored per PWM_CONFIG */

        if (!ce.overflow) {
            pb_submsg(&e, 1, ch, ce.pos);  /* repeated PwmChannel */
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_PWM_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendClimacellTimes(void)
{
    uint8_t inner[128];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* Climacell Times: 48 half-hour slots */
    for (int i = 0; i < 48; i++) {
        pb_uint32(&e, 1, (uint32_t)(unsigned char)Settings.Climacell.Times[i]);
    }

    if (e.overflow) return false;
    return send_envelope(MSG_CLIMACELL_TIMES, inner, e.pos);
}

bool NovaMsg_SendIoConfig(void)
{
    /* EQ_TOTAL_IO=112; each entry tag(1) + varint(up to 5) for IO_UNDEFINED
     * (0xFFFFFFFF). 112 * 6 * 2 maps ≈ 1344 bytes — needs > 512. */
    uint8_t inner[2048];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* field 1: repeated output_map */
    for (int i = 0; i < EQ_TOTAL_IO; i++) {
        pb_uint32(&e, 1, Settings.EquipIo[i].Output);
    }
    /* field 2: repeated input_map */
    for (int i = 0; i < EQ_TOTAL_IO; i++) {
        pb_uint32(&e, 2, Settings.EquipIo[i].Input);
    }

    if (e.overflow) return false;
    return send_envelope(MSG_IO_CONFIG, inner, e.pos);
}

bool NovaMsg_SendIoDefinition(void)
{
    uint8_t inner[2048];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    for (int i = 0; i < EQ_TOTAL_IO; i++) {
        uint8_t entry[64];
        PbEncoder ie;
        pb_init(&ie, entry, sizeof(entry));

        pb_uint32(&ie, 1, (uint32_t)i);                        /* index */
        pb_string(&ie, 2, Settings.EquipIo[i].Name);           /* name */
        pb_uint32(&ie, 3, (uint32_t)Settings.EquipIo[i].Mode); /* mode */
        pb_uint32(&ie, 4, Settings.EquipIo[i].Output);         /* io_pin (output) */
        pb_bool(&ie,   5, Settings.EquipIo[i].Renamable != 0); /* renamable */
        pb_bool(&ie,   6, Settings.EquipIo[i].Enabled != 0);   /* visible (enabled) */

        if (!ie.overflow) {
            pb_submsg(&e, 1, entry, ie.pos);    /* repeated IoEntry */
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_IO_DEFINITION, inner, e.pos);
}

bool NovaMsg_SendAvailableIo(void)
{
    uint8_t inner[256];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* Output pins that are unassigned (Output == IO_UNDEFINED) */
    for (int i = 0; i < EQ_TOTAL_IO; i++) {
        if (Settings.EquipIo[i].Output == IO_UNDEFINED) {
            pb_uint32(&e, 1, (uint32_t)i);   /* repeated output_pins */
        }
    }
    /* Input pins that are unassigned */
    for (int i = 0; i < EQ_TOTAL_IO; i++) {
        if (Settings.EquipIo[i].Input == IO_UNDEFINED) {
            pb_uint32(&e, 2, (uint32_t)i);   /* repeated input_pins */
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_AVAILABLE_IO, inner, e.pos);
}

bool NovaMsg_SendAnalogBoard(uint32_t idx)
{
    if (idx >= ANALOG_BOARDS_PER_SYSTEM) return false;
    ANALOG_BOARD *b = &Settings.AnalogBoard[idx];
    if (!b->Present) return false;

    uint8_t inner[256];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, b->Address);
    pb_uint32(&e, 2, b->Type);
    pb_string(&e, 3, b->Label);
    pb_string(&e, 4, b->Version);
    pb_bool(&e,   5, b->Disabled != 0);

    /* field 6: repeated AnalogSensor */
    for (int s = 0; s < ANALOG_SENSORS_PER_BOARD; s++) {
        ANALOG_SENSOR *sen = &b->Sensor[s];
        uint8_t sb[48];
        PbEncoder se;
        pb_init(&se, sb, sizeof(sb));

        pb_uint32(&se, 1, (uint32_t)s);           /* slot */
        pb_uint32(&se, 2, sen->Type);              /* type */
        pb_string(&se, 3, sen->Label);             /* label */
        pb_float(&se,  4, sen->Offset);            /* offset */
        pb_bool(&se,   5, sen->Disabled != 0);     /* disabled */
        pb_float(&se,  6, sen->Value);             /* value */

        if (!se.overflow) {
            pb_submsg(&e, 6, sb, se.pos);
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_ANALOG_BOARD, inner, e.pos);
}

bool NovaMsg_SendSensorData(void)
{
    /* Sensor data is populated from analog boards only.
     * Build SensorData from AnalogBoard entries. */
    uint8_t inner[1024];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    for (uint32_t b = 0; b < ANALOG_BOARDS_PER_SYSTEM; b++) {
        ANALOG_BOARD *board = &Settings.AnalogBoard[b];
        if (!board->Present) continue;

        for (int s = 0; s < ANALOG_SENSORS_PER_BOARD; s++) {
            ANALOG_SENSOR *sen = &board->Sensor[s];
            if (sen->Disabled) continue;

            uint8_t reading[16];
            PbEncoder re;
            pb_init(&re, reading, sizeof(reading));

            uint32_t sid = (board->Address) * ANALOG_SENSORS_PER_BOARD + (uint32_t)s;
            pb_uint32(&re, 1, sid);          /* index */
            pb_float(&re, 2, sen->Value);    /* value */
            pb_bool(&re, 3, true);           /* valid */

            if (!re.overflow) {
                /* Temp types go to field 1, humidity to field 2 */
                uint32_t field = (sen->Type == 1 || sen->Type == 6 || sen->Type == 7 || sen->Type == 10) ? 2 : 1;
                pb_submsg(&e, field, reading, re.pos);
            }
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_SENSOR_DATA, inner, e.pos);
}

bool NovaMsg_SendSensorLabels(void)
{
    uint8_t inner[1024];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    for (uint32_t b = 0; b < ANALOG_BOARDS_PER_SYSTEM; b++) {
        ANALOG_BOARD *board = &Settings.AnalogBoard[b];
        if (!board->Present) continue;

        for (int s = 0; s < ANALOG_SENSORS_PER_BOARD; s++) {
            ANALOG_SENSOR *sen = &board->Sensor[s];

            uint8_t label[48];
            PbEncoder le;
            pb_init(&le, label, sizeof(label));

            uint32_t sid = (board->Address) * ANALOG_SENSORS_PER_BOARD + (uint32_t)s;
            pb_uint32(&le, 1, sid);
            pb_string(&le, 2, sen->Label);

            if (!le.overflow) {
                uint32_t field = (sen->Type == 1 || sen->Type == 6 || sen->Type == 7 || sen->Type == 10) ? 2 : 1;
                pb_submsg(&e, field, label, le.pos);
            }
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_SENSOR_LABELS, inner, e.pos);
}

bool NovaMsg_SendRuntimes(void)
{
    uint8_t inner[256];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* Runtime schedule: Settings.RunTimes[48] — 48 half-hour slots */
    for (int i = 0; i < 48; i++) {
        uint8_t entry[16];
        PbEncoder re;
        pb_init(&re, entry, sizeof(entry));

        pb_uint32(&re, 1, (uint32_t)i);
        pb_uint32(&re, 2, (uint32_t)(unsigned char)Settings.RunTimes[i]);

        if (!re.overflow) {
            pb_submsg(&e, 1, entry, re.pos);    /* repeated RuntimeEntry */
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_RUNTIMES, inner, e.pos);
}

bool NovaMsg_SendFanRuntime(void)
{
    uint8_t inner[32];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, Settings.Fan.DailyRunTime / 3600);
    pb_uint32(&e, 2, (Settings.Fan.DailyRunTime % 3600) / 60);
    pb_uint32(&e, 3, Settings.Fan.TotalRunTime / 3600);
    pb_uint32(&e, 4, (Settings.Fan.TotalRunTime % 3600) / 60);

    if (e.overflow) return false;
    return send_envelope(MSG_FAN_RUNTIME, inner, e.pos);
}

bool NovaMsg_SendHumidModes(void)
{
    uint8_t inner[16];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, (uint32_t)(unsigned char)Settings.HumidCtrl[0].Mode);
    pb_uint32(&e, 2, (uint32_t)(unsigned char)Settings.HumidCtrl[0].Mode);  /* pump follows head */
    if (NUM_HUMIDIFIERS > 1) {
        pb_uint32(&e, 3, (uint32_t)(unsigned char)Settings.HumidCtrl[1].Mode);
        pb_uint32(&e, 4, (uint32_t)(unsigned char)Settings.HumidCtrl[1].Mode);
    }

    if (e.overflow) return false;
    return send_envelope(MSG_HUMID_MODES, inner, e.pos);
}

bool NovaMsg_SendAuxSwitches(void)
{
    uint8_t inner[32];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    for (int i = 0; i < NUM_AUX_SWITCHES; i++) {
        pb_uint32(&e, 1, Settings.RemoteOff[RO_AUX1 + i]);  /* repeated switch_state */
    }

    if (e.overflow) return false;
    return send_envelope(MSG_AUX_SWITCHES, inner, e.pos);
}

bool NovaMsg_SendAccountSettings(void)
{
    uint8_t inner[256];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    uint32_t count = 0;
    for (int i = 0; i < NUM_PASSWORDS; i++) {
        if (Settings.User[i].ID[0] == '\0') continue;

        uint8_t user[32];
        PbEncoder ue;
        pb_init(&ue, user, sizeof(user));
        pb_uint32(&ue, 1, (uint32_t)i);        /* slot */
        pb_string(&ue, 2, Settings.User[i].ID); /* user_id */

        if (!ue.overflow) {
            pb_submsg(&e, 1, user, ue.pos);     /* repeated UserAccount */
        }
        count++;
    }
    pb_uint32(&e, 2, count);
    pb_bool(&e, 3, Settings.LoginPw[0] != '\0');

    if (e.overflow) return false;
    return send_envelope(MSG_ACCOUNT_SETTINGS, inner, e.pos);
}

bool NovaMsg_SendNetworkNodes(void)
{
    uint8_t inner[512];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    for (int i = 0; i < NUM_NETWORK_NODES; i++) {
        if (Settings.Node[i].IP[0] == '\0') continue;

        uint8_t node[64];
        PbEncoder ne;
        pb_init(&ne, node, sizeof(node));
        pb_uint32(&ne, 1, (uint32_t)i);
        pb_string(&ne, 2, Settings.Node[i].IP);
        pb_string(&ne, 3, Settings.Node[i].ID);
        /* field 4: status — not stored in settings */

        if (!ne.overflow) {
            pb_submsg(&e, 1, node, ne.pos);
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_NETWORK_NODES, inner, e.pos);
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Firmware update messages                                               */
/* ═══════════════════════════════════════════════════════════════════════ */

#include "nova_fw_update.h"

bool NovaMsg_SendFwUpdateStatus(uint32_t state, uint32_t bytes_written,
                                 uint32_t total_size, uint32_t error_code,
                                 const char *error_message, uint32_t active_bank)
{
    uint8_t inner[256];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    pb_uint32(&e, 1, state);
    pb_uint32(&e, 2, bytes_written);
    pb_uint32(&e, 3, total_size);
    if (error_code) pb_uint32(&e, 4, error_code);
    if (error_message && error_message[0])
        pb_string(&e, 5, error_message);

    /* Active version (from running bank header) */
    FwBankHeader hdr;
    NovaFwUpdate_GetBankHeader(NovaFwUpdate_GetActiveBank(), &hdr);
    if (hdr.version[0]) pb_string(&e, 6, hdr.version);

    /* Staged version (from target bank header, if verified) */
    FwBankHeader staged;
    NovaFwUpdate_GetBankHeader(NovaFwUpdate_GetInactiveBank(), &staged);
    if (staged.valid && staged.version[0]) pb_string(&e, 7, staged.version);

    pb_uint32(&e, 8, active_bank);

    if (e.overflow) return false;
    return send_envelope(MSG_FW_UPDATE_STATUS, inner, e.pos);
}

bool NovaMsg_SendFwBankInfo(void)
{
    uint8_t inner[256];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    FwBankHeader a, b;
    FwBootMeta meta;
    NovaFwUpdate_GetBankHeader(0, &a);
    NovaFwUpdate_GetBankHeader(1, &b);
    NovaFwUpdate_GetBootMeta(&meta);

    pb_uint32(&e, 1, NovaFwUpdate_GetActiveBank());

    /* Bank A */
    if (a.version[0]) pb_string(&e, 2, a.version);
    pb_uint32(&e, 3, a.image_crc);
    pb_uint32(&e, 4, (a.magic == FW_BANK_MAGIC && a.valid) ? 1 : 0);

    /* Bank B */
    if (b.version[0]) pb_string(&e, 5, b.version);
    pb_uint32(&e, 6, b.image_crc);
    pb_uint32(&e, 7, (b.magic == FW_BANK_MAGIC && b.valid) ? 1 : 0);

    /* Golden version — read directly from golden header area */
    /* For now, just report "golden" as placeholder */
    pb_string(&e, 8, "golden");

    pb_uint32(&e, 9, meta.boot_count);
    pb_uint32(&e, 10, meta.boot_reason);

    if (e.overflow) return false;
    return send_envelope(MSG_FW_BANK_INFO, inner, e.pos);
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Orbit module status messages                                           */
/* ═══════════════════════════════════════════════════════════════════════ */

#include "hal_orbit.h"

/**
 * Build an OrbitBoardStatus submessage for a single board.
 * Returns encoded size, or 0 on overflow.
 */
static size_t encode_orbit_board_status(uint8_t *buf, size_t bufsize,
                                        int slot, const orbit_board_t *ob)
{
    PbEncoder e;
    pb_init(&e, buf, bufsize);

    pb_uint32(&e, 1, (uint32_t)slot);                    /* slot */
    pb_uint32(&e, 2, (uint32_t)ob->dipswitch_id);        /* dipswitch_id */
    pb_bool(&e,   3, ob->connected != 0);                 /* connected */
    pb_uint32(&e, 4, (uint32_t)ob->comm_errors);         /* comm_errors */
    pb_bool(&e,   5, ob->estop_active != 0);              /* estop_active */
    pb_bool(&e,   6, ob->safe_mode != 0);                 /* safe_mode */
    pb_float(&e,  7, (float)ob->cpu_temp_x10 / 10.0f);   /* cpu_temp */
    pb_uint32(&e, 8, ob->orbit_uptime);                  /* uptime_secs */
    pb_uint32(&e, 9, (uint32_t)ob->firmware_ver);        /* firmware_ver */
    pb_enum(&e,  10, (uint32_t)ob->role);                 /* role */
    pb_uint32(&e, 11, (uint32_t)ob->zone_id);            /* zone_id */
    /* field 12: legacy_slot — signed, encode as zigzag varint */
    {
        int32_t ls = (int32_t)ob->legacy_slot;
        uint32_t zigzag = (ls >= 0) ? (uint32_t)(ls << 1) : (uint32_t)(((-ls) << 1) - 1);
        pb_uint32(&e, 12, zigzag);
    }
    pb_uint32(&e, 13, (uint32_t)ob->refrig_stage);       /* refrig_stage */
    /* field 14: ip_address — construct from Orbit IP format */
    {
        char ip[20];
        snprintf(ip, sizeof(ip), "%d.%d.%d.%d",
                 ORBIT_IP_A, ORBIT_IP_B, ORBIT_IP_C, ob->dipswitch_id);
        pb_string(&e, 14, ip);
    }

    if (e.overflow) return 0;
    return e.pos;
}

bool NovaMsg_SendOrbitStatus(void)
{
    uint8_t inner[1024];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* Encode only connected boards into OrbitStatus.boards (field 1) */
    for (int i = 0; i < NOVA_MAX_ORBITS; i++) {
        orbit_board_t *ob = &orbit_boards[i];
        if (!ob->connected) continue;

        uint8_t board_buf[128];
        size_t board_len = encode_orbit_board_status(board_buf, sizeof(board_buf), i, ob);
        if (board_len > 0) {
            pb_submsg(&e, 1, board_buf, board_len);  /* repeated boards */
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_ORBIT_STATUS, inner, e.pos);
}

bool NovaMsg_SendOrbitDiscovery(void)
{
    uint8_t inner[2048];
    PbEncoder e;
    pb_init(&e, inner, sizeof(inner));

    /* field 1: max_slots */
    pb_uint32(&e, 1, (uint32_t)NOVA_MAX_ORBITS);

    /* field 2: boards_found — count connected boards */
    uint32_t found = 0;
    for (int i = 0; i < NOVA_MAX_ORBITS; i++) {
        if (orbit_boards[i].connected) found++;
    }
    pb_uint32(&e, 2, found);

    /* field 3: repeated boards — all slots, not just connected */
    for (int i = 0; i < NOVA_MAX_ORBITS; i++) {
        orbit_board_t *ob = &orbit_boards[i];

        uint8_t board_buf[128];
        size_t board_len = encode_orbit_board_status(board_buf, sizeof(board_buf), i, ob);
        if (board_len > 0) {
            pb_submsg(&e, 3, board_buf, board_len);
        }
    }

    if (e.overflow) return false;
    return send_envelope(MSG_ORBIT_DISCOVERY, inner, e.pos);
}
