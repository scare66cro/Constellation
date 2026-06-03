/*
 * lp_settings.c — Phase E in-memory Settings + (de)serializer
 *
 * See lp_settings.h for the architectural rationale. This file is a
 * thin protobuf en/decoder that mirrors the field numbers in
 * settings.proto::BasicSetupUpdate and system.proto::BasicSetup.
 *
 * Why hand-rolled instead of nanopb: the LP toolchain doesn't have
 * nanopb wired up yet, and these messages are small (<10 fields). Once
 * Phase F adds a nanopb code-gen pass we can collapse this to a few
 * `pb_encode_msg(stream, &msg, BasicSetup_fields)` calls. The wire
 * format is by-design identical so the swap is binary-compatible.
 */

#include "lp_settings.h"
#include "lp_remote_outside.h"

#include <string.h>
#include <kernel/dpl/DebugP.h>

/* ─── Module state ───────────────────────────────────────────────────── */

static LpSettingsData s_data;

/* ─── Tiny protobuf primitives (mirrors main.c helpers) ─────────────── */

static size_t encode_varint(uint8_t *buf, uint32_t value)
{
    size_t i = 0;
    while (value >= 0x80U) { buf[i++] = (uint8_t)((value & 0x7FU) | 0x80U); value >>= 7; }
    buf[i++] = (uint8_t)value;
    return i;
}

static size_t decode_varint(const uint8_t *buf, size_t len, uint32_t *out)
{
    uint32_t v = 0; uint32_t shift = 0; size_t i = 0;
    while (i < len) {
        uint8_t b = buf[i++];
        v |= (uint32_t)(b & 0x7FU) << shift;
        if ((b & 0x80U) == 0U) { *out = v; return i; }
        shift += 7U;
        if (shift >= 35U) return 0U;   /* malformed */
    }
    return 0U;
}

/* Emit a wire-format key (varint of (field<<3)|wire_type).
 * Fields ≥16 require multi-byte varint encoding — a single-byte write
 * for field 16 (key=0x80) sets the continuation bit and corrupts the
 * stream. Always varint-encode. */
static size_t emit_key(uint8_t *buf, size_t bufsize, size_t pos,
                       uint32_t field, uint8_t wire)
{
    if (pos + 5U > bufsize) return 0U;
    return pos + encode_varint(&buf[pos], (field << 3) | (uint32_t)wire);
}

/* Length-delimited string field. */
static size_t emit_string(uint8_t *buf, size_t bufsize, size_t pos,
                          uint32_t field, const char *s)
{
    size_t slen = strlen(s);
    if (slen == 0U) return pos;   /* proto3 zero-suppression */
    if (pos + 5U + 5U + slen > bufsize) return 0U;
    pos = emit_key(buf, bufsize, pos, field, 2U);
    if (pos == 0U) return 0U;
    pos += encode_varint(&buf[pos], (uint32_t)slen);
    memcpy(&buf[pos], s, slen);
    return pos + slen;
}

static size_t emit_uint32(uint8_t *buf, size_t bufsize, size_t pos,
                          uint32_t field, uint32_t v)
{
    if (v == 0U) return pos;   /* proto3 zero-suppression */
    if (pos + 5U + 5U > bufsize) return 0U;
    pos = emit_key(buf, bufsize, pos, field, 0U);
    if (pos == 0U) return 0U;
    pos += encode_varint(&buf[pos], v);
    return pos;
}

/* fixed32 (LE) — float fields. proto3-suppress when bit-pattern is 0
 * (i.e. 0.0f). */
static size_t emit_float(uint8_t *buf, size_t bufsize, size_t pos,
                         uint32_t field, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    if (bits == 0U) return pos;
    if (pos + 5U + 4U > bufsize) return 0U;
    pos = emit_key(buf, bufsize, pos, field, 5U);  /* wire type 5 = fixed32 */
    if (pos == 0U) return 0U;
    buf[pos++] = (uint8_t)( bits        & 0xFFU);
    buf[pos++] = (uint8_t)((bits >>  8) & 0xFFU);
    buf[pos++] = (uint8_t)((bits >> 16) & 0xFFU);
    buf[pos++] = (uint8_t)((bits >> 24) & 0xFFU);
    return pos;
}

/* Decode 4 LE bytes → float. Returns 0 on short buffer, else 4. */
static size_t decode_float(const uint8_t *p, size_t avail, float *out)
{
    if (avail < 4U) return 0U;
    uint32_t bits = (uint32_t)p[0]
                  | ((uint32_t)p[1] <<  8)
                  | ((uint32_t)p[2] << 16)
                  | ((uint32_t)p[3] << 24);
    memcpy(out, &bits, sizeof(*out));
    return 4U;
}

/* Skip an unknown field of the given wire type. */
static size_t skip_field(uint8_t wire, const uint8_t *buf, size_t len)
{
    if (wire == 0U) {                        /* varint */
        uint32_t dummy;
        return decode_varint(buf, len, &dummy);
    }
    if (wire == 2U) {                        /* length-delimited */
        uint32_t sublen;
        size_t n = decode_varint(buf, len, &sublen);
        if (n == 0U || n + sublen > len) return 0U;
        return n + sublen;
    }
    if (wire == 5U) {                        /* fixed32 */
        return (len >= 4U) ? 4U : 0U;
    }
    if (wire == 1U) {                        /* fixed64 */
        return (len >= 8U) ? 8U : 0U;
    }
    return 0U;
}

/* ─── Public API ─────────────────────────────────────────────────────── */

/* Forward decl — defined alongside LpSettings_Deserialize. Pins
 * EQ_LOW_TEMP→DI1 and EQ_ESTOP→DI11 in the input map. */
static void io_config_pin_hardware_inputs(void);

void LpSettings_DataInit(void)
{
    memset(&s_data, 0, sizeof(s_data));

    /* ── Factory defaults ────────────────────────────────────────────
     * Mirrors legacy AS2 GetFactoryDefault() (Application/Settings.c
     * lines 123-419), scoped to fields the LP firmware has plumbed.
     * Any saved blob coming back via the bridge-replay path
     * (envelope 91) overwrites these in-place via LpSettings_Deserialize.
     *
     * RULE: only seed values that legacy AS2 explicitly wrote to a
     * non-zero default. Leaving a field at zero is intentional when
     * AS2 did the same — proto3 zero-suppression then emits no bytes
     * and the UI sees the type default.
     *
     * Strings stay empty — operator fills in storage name, dealer
     * info, login PW etc. on first config. Defaults like
     * "AgriStar.Alerts@gmail.com" from AS2 are a security risk and
     * are deliberately NOT carried over.
     *
     * NOTE: this runs BEFORE the scheduler starts (called from main()
     * before xTaskCreate). Do NOT log here — DebugP_log blocks until
     * the bridge_uart_task drains UART0. */

    /* basic setup */
    s_data.basic.multi_view  = 6;     /* AS2 default sessions */
    s_data.basic.animations  = 1;     /* AS2: animations on */
    /* storage_name — Q2.1 (factory-defaults-review-2026-05). Seed a
     * recognisable site name so System Monitor / page header shows
     * something on first boot. Operator overrides on Level 2 Basic. */
    strncpy(s_data.basic.storage_name, "Gellert Nova",
            sizeof(s_data.basic.storage_name) - 1U);

    /* plenum */
    s_data.plenum.temp_setpoint        = 46.0f;
    s_data.plenum.humid_setpoint       = 95;
    s_data.plenum.humid_setpoint_ref   = 0;       /* plenum (suppressed) */
    s_data.plenum.burner_temp_setpoint = 75.0f;
    s_data.plenum.burner_threshold     = 50.0f;

    /* fan speed (VFD) */
    s_data.fan_speed.max_speed     = 100;
    s_data.fan_speed.min_speed     = 25;
    s_data.fan_speed.refrig_speed  = 75;
    s_data.fan_speed.recirc_speed  = 50;
    s_data.fan_speed.update_period = 5;
    s_data.fan_speed.temp_diff     = 1.0f;
    s_data.fan_speed.temp_ref2     = 255;         /* return-air sentinel */
    s_data.fan_speed.prev_speed    = 25;          /* = min_speed */

    /* fan boost */
    s_data.fan_boost.speed    = 80;
    s_data.fan_boost.interval = 12;
    s_data.fan_boost.duration = 20;
    s_data.fan_boost.temp     = 40.0f;

    /* ramp rate */
    s_data.ramp_rate.rate_per_day  = 1.0f;
    s_data.ramp_rate.update_period = 2;
    s_data.ramp_rate.temp_diff     = 1.0f;
    s_data.ramp_rate.target_temp   = 46.0f;

    /* refrigeration — 8 stages × (on, off) per AS2 line 207-225 */
    {
        const uint32_t on_pts [8] = { 20, 30, 40, 50, 60, 70, 80, 90 };
        const uint32_t off_pts[8] = { 10, 20, 30, 40, 50, 60, 70, 80 };
        for (int i = 0; i < 8; i++) {
            s_data.refrig.stages[i].on  = on_pts[i];
            s_data.refrig.stages[i].off = off_pts[i];
        }
        s_data.refrig.stages_count   = 8;
        s_data.refrig.num_stages     = 8;
        s_data.refrig.p_gain         = 5.0f;
        s_data.refrig.i_gain         = 15.0f;
        s_data.refrig.d_gain         = 2.0f;
        s_data.refrig.u_limit        = 3.0f;
        s_data.refrig.limit          = 27.0f;
        s_data.refrig.fail_mode      = 255;       /* undefined */
    }

    /* climacell */
    s_data.climacell.efficiency = 90;
    s_data.climacell.p_gain     = 5.0f;
    s_data.climacell.i_gain     = 15.0f;
    s_data.climacell.d_gain     = 2.0f;
    s_data.climacell.u_limit    = 3.0f;

    /* climacell hourly times — AS2 memset(2,…) = "on" everywhere */
    for (int i = 0; i < LP_CLIMACELL_HOURS; i++) {
        s_data.climacell_times.hourly_efficiency[i] = 2;
    }
    s_data.climacell_times.hourly_count = LP_CLIMACELL_HOURS;

    /* burner */
    s_data.burner.on     = 10;
    s_data.burner.low    = 25;
    s_data.burner.p_gain = 5.0f;
    s_data.burner.i_gain = 15.0f;
    s_data.burner.d_gain = 2.0f;
    s_data.burner.u_limit = 3.0f;
    s_data.burner.manual = 75;

    /* door */
    s_data.door.p_gain         = 5.0f;
    s_data.door.i_gain         = 15.0f;
    s_data.door.d_gain         = 2.0f;
    s_data.door.u_limit        = 3.0f;
    s_data.door.actuator_time  = 180;
    s_data.door.cool_air_cycle = 10;

    /* CO2 */
    s_data.co2.cycle_or_set    = 1200;
    s_data.co2.duration_minutes = 20;
    s_data.co2.min_temp        = 40.0f;
    s_data.co2.max_temp        = 50.0f;
    s_data.co2.fan_output      = 100;
    s_data.co2.door_output     = 100;

    /* outside air */
    s_data.outside_air.differential   = 2.0f;
    s_data.outside_air.mode           = 1;        /* OSA_CTRL_OUTSIDE */
    s_data.outside_air.temp_ref       = 255;      /* plenum setpoint */
    s_data.outside_air.calc_humid_max = 80;

    /* cure (onion mode) */
    s_data.cure.start_temp        = 60.0f;
    s_data.cure.start_humid       = 70.0f;
    s_data.cure.humid_high_limit  = 85.0f;
    /* humid_ref = 0 (plenum) — suppressed */

    /* cure limits — Q9: high limit lowered 110 → 90 (factory-defaults-review-2026-05) */
    s_data.cure_limit.temp_low_limit  = 35.0f;
    s_data.cure_limit.temp_high_limit = 90.0f;

    /* temp alarm */
    s_data.temp_alarm.low_temp    = 5.0f;
    s_data.temp_alarm.low_timer   = 10;
    s_data.temp_alarm.high_temp   = 5.0f;
    s_data.temp_alarm.high_timer  = 10;

    /* misc */
    s_data.misc.heat_temp_thresh = 10.0f;
    s_data.misc.cavity_mode      = 1;             /* off */
    s_data.misc.cavity_diff      = -5.0f;
    s_data.misc.cavity_duty_or_sensor = 50;
    s_data.misc.defrost_duration = 10;

    /* failures 1 — Q2.6 (factory-defaults-review-2026-05).
     * All equipment failure timers default to 1 minute (was AS2's 10 /
     * fan's 3) so a real fault is acted on quickly out of the box.
     * Lights timer/units stay at 1 / 60 hours (units are different here). */
    {
        uint32_t *tptrs[] = {
            &s_data.failure.fan_timer, &s_data.failure.heat_timer,
            &s_data.failure.refrig_timer, &s_data.failure.burner_timer,
            &s_data.failure.humid_timer, &s_data.failure.climacell_timer,
            &s_data.failure.refrig_stages_timer,
            &s_data.failure.aux_timer, &s_data.failure.cavity_heat_timer,
        };
        for (size_t i = 0; i < sizeof(tptrs)/sizeof(tptrs[0]); i++) {
            *tptrs[i] = 1;
        }
        s_data.failure.fan_mode      = 2;         /* AS2: fan failure → fail */
        s_data.failure.lights_timer  = 1;
        s_data.failure.lights_units  = 60;        /* hours */
    }

    /* failures 2 */
    s_data.failure2.plen_sen_mode  = 1;            /* alarm */
    s_data.failure2.plen_sen_diff  = 2.0f;
    s_data.failure2.out_air_mode   = 1;
    s_data.failure2.out_humid_mode = 1;
    s_data.failure2.high_co2_mode  = 1;
    s_data.failure2.co2_setpt      = 4000;        /* Q2.7: lifted 2500→4000 ppm */
    s_data.failure2.low_humid_set  = 80;          /* Plenum.HumidLowFailure */

    /* humid ctrl — Q7: mode flipped 1 (Timer) → 0 (Manual) so humidifiers
     * run on first boot. Duty cycles inherited if operator switches to Timer. */
    for (int i = 0; i < LP_HUMID_CTRL_MAX; i++) {
        s_data.humid_ctrl.entries[i].index     = (uint32_t)i;
        s_data.humid_ctrl.entries[i].mode      = 0;
        s_data.humid_ctrl.entries[i].cool_on   = 60;
        s_data.humid_ctrl.entries[i].cool_off  = 60;
        s_data.humid_ctrl.entries[i].recirc_on = 60;
        s_data.humid_ctrl.entries[i].recirc_off = 60;
        s_data.humid_ctrl.entries[i].refrig_on  = 60;
        s_data.humid_ctrl.entries[i].refrig_off = 60;
    }
    s_data.humid_ctrl.entries_count = LP_HUMID_CTRL_MAX;

    /* runtimes — AS2 memset(3,…) = "standby" everywhere (mode=3) */
    for (int i = 0; i < LP_RUNTIME_MAX_ENTRIES; i++) {
        s_data.runtime.entries[i].slot = (uint32_t)i;
        s_data.runtime.entries[i].mode = 3;
    }
    s_data.runtime.count = LP_RUNTIME_MAX_ENTRIES;

    /* user log — Q2.19: 15-min log interval (was 60) */
    s_data.user_log.interval_minutes = 15;
    s_data.user_log.enabled          = 1;          /* wrap */

    /* PID log */
    s_data.pid_log.wrap = 1;

    /* email — Q11 (factory-defaults-review-2026-05). Pre-seeded with
     * Agristar's smtp2go relay so alerts work out of the box. Operator
     * can override on Level 1 Email page. NOTE: baking SMTP credentials
     * in firmware is a known security trade-off (binary-extractable);
     * accepted by user 2026-05-02. */
    s_data.email.port      = 465;                 /* implicit TLS/SSL */
    s_data.email.auth_type = 1;                   /* 1 = TLS-SSL (settings.proto enum) */
    strncpy(s_data.email.server,    "mail.smtp2go.com",
            sizeof(s_data.email.server)    - 1U);
    strncpy(s_data.email.username,  "agristar.alerts",
            sizeof(s_data.email.username)  - 1U);
    strncpy(s_data.email.password,  "4gri*st4r4l3rts",
            sizeof(s_data.email.password)  - 1U);
    strncpy(s_data.email.from_addr, "agristar.alerts@gellert.com",
            sizeof(s_data.email.from_addr) - 1U);
    strncpy(s_data.email.to_addr,   "youraccount@gmail.com",
            sizeof(s_data.email.to_addr)   - 1U);

    /* alerts — Q12: enable all 20 alert categories on first boot.
     * (Bug fix from review: previously seeded `flags[i] = i` which left
     * flag[0] disabled and rest mis-valued; meaning is now "1 = enabled".) */
    for (uint32_t i = 0; i < 20U && i < LP_ALERT_MAX; i++) {
        s_data.alert.flags[i] = 1U;
    }
    s_data.alert.count = 20;

    /* HTTP port (legacy) */
    s_data.http_port.port = 80;

    /* Account: no users by default — first login via factory PW elsewhere. */

    /* Seed IO Config with the AS2-derived factory default map. Same code
     * path as the Level 2 "Set To Defaults" button so cold boot and
     * factory-defaults always agree. Includes the hardware-pin tail
     * (DI1=AUX_LOW_LIMIT, DI11=ESTOP) so a virgin OSPI bank still has
     * the safety inputs wired the moment the firmware comes up. */
    LpSettings_ResetIoConfig();
}

const LpSettingsData *LpSettings_DataGet(void) { return &s_data; }

/* Decode one string field into dst (caps at LP_SET_STR_MAX-1). */
static size_t copy_string_field(const uint8_t *p, size_t avail,
                                char *dst, size_t dstsz)
{
    uint32_t slen;
    size_t ln = decode_varint(p, avail, &slen);
    if (ln == 0U || ln + slen > avail) return 0U;
    size_t cp = (slen < dstsz - 1U) ? slen : dstsz - 1U;
    memcpy(dst, p + ln, cp);
    dst[cp] = '\0';
    return ln + slen;
}

/* AS2-style temperature unit migration. When TempType toggles, walk
 * every persistent absolute-temperature setpoint and rewrite it so the
 * stored value still represents the SAME PHYSICAL TEMPERATURE in the
 * new unit (46 °F → 7.8 °C, not 46 °C). Mirrors the legacy block in
 * docs/legacy_AS2_reference/Application/StorePostData.c lines 538-562.
 *
 * Cached "current" sensor readings are NOT in this list — those get
 * re-derived from the orbit each poll via build_system_status_envelope
 * (which converts using the *new* TempType automatically).
 *
 * Deltas (temp_diff, temp_diff_*), thresholds, and "user-entered as a
 * number" fields are also NOT converted, matching AS2's behaviour.
 * If a future site reports those need to migrate too, extend this
 * function — DON'T add per-display conversion in the UI. */
static void migrate_setpoints_for_temp_unit(uint32_t old_unit,
                                            uint32_t new_unit)
{
    if (old_unit == new_unit) return;
    if (old_unit == 0U /* F */ && new_unit == 1U /* C */) {
        s_data.plenum.temp_setpoint = (s_data.plenum.temp_setpoint - 32.0f) / 1.8f;
    } else if (old_unit == 1U /* C */ && new_unit == 0U /* F */) {
        s_data.plenum.temp_setpoint = s_data.plenum.temp_setpoint * 1.8f + 32.0f;
    } else {
        /* Unknown unit values — leave setpoints alone. */
        return;
    }
    /* Match AS2: reset ramp target to the new setpoint so a stale
     * ramp doesn't immediately drive away from the user's value. */
    s_data.ramp_rate.target_temp = s_data.plenum.temp_setpoint;
}

bool LpSettings_ApplyBasicSetup(const uint8_t *payload, size_t len)
{
    bool changed = false;
    /* Snapshot the old TempType BEFORE decoding so we can detect a
     * change and migrate setpoints AS2-style after the field loop. */
    const uint32_t old_temp_type = s_data.basic.temp_type;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        switch (field) {
        case 1: /* storage_name */
            consumed = copy_string_field(payload + pos, len - pos,
                                         s_data.basic.storage_name,
                                         sizeof(s_data.basic.storage_name));
            changed = (consumed > 0U);
            break;
        case 4: /* home_page */
            consumed = copy_string_field(payload + pos, len - pos,
                                         s_data.basic.home_page,
                                         sizeof(s_data.basic.home_page));
            changed = (consumed > 0U);
            break;
        case 7: /* master_ip */
            consumed = copy_string_field(payload + pos, len - pos,
                                         s_data.basic.master_ip,
                                         sizeof(s_data.basic.master_ip));
            changed = (consumed > 0U);
            break;
        case 9: /* login_pw */
            consumed = copy_string_field(payload + pos, len - pos,
                                         s_data.basic.login_pw,
                                         sizeof(s_data.basic.login_pw));
            changed = (consumed > 0U);
            break;
        case 2: case 3: case 5: case 6: case 8: case 10: case 11: {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                switch (field) {
                case 2:  s_data.basic.temp_type   = v; break;
                case 3:  s_data.basic.mode        = v; break;
                case 5:  s_data.basic.system_mode = v; break;
                case 6:  s_data.basic.language    = v; break;
                case 8:  s_data.basic.multi_view  = v; break;
                case 10: s_data.basic.local_login = v; break;
                case 11: s_data.basic.animations  = v; break;
                }
                changed = true;
            }
            break;
        }
        default:
            consumed = skip_field(wire, payload + pos, len - pos);
            break;
        }
        if (consumed == 0U) break;
        pos += consumed;
    }
    /* AS2-faithful: when TempType toggled, walk persistent absolute
     * setpoints and rewrite them to keep the same physical temperature
     * in the new unit. */
    if (s_data.basic.temp_type != old_temp_type) {
        migrate_setpoints_for_temp_unit(old_temp_type, s_data.basic.temp_type);
    }
    return changed;
}

size_t LpSettings_BuildBasicSetupBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_string(buf, bufsize, pos, 1, s_data.basic.storage_name);
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.basic.temp_type);
    pos = emit_uint32(buf, bufsize, pos, 3, s_data.basic.mode);
    pos = emit_string(buf, bufsize, pos, 4, s_data.basic.home_page);
    pos = emit_uint32(buf, bufsize, pos, 5, s_data.basic.system_mode);
    pos = emit_uint32(buf, bufsize, pos, 6, s_data.basic.language);
    pos = emit_string(buf, bufsize, pos, 7, s_data.basic.master_ip);
    pos = emit_uint32(buf, bufsize, pos, 8, s_data.basic.multi_view);
    pos = emit_string(buf, bufsize, pos, 9, s_data.basic.login_pw);
    pos = emit_uint32(buf, bufsize, pos, 10, s_data.basic.local_login);
    pos = emit_uint32(buf, bufsize, pos, 11, s_data.basic.animations);
    return pos;
}

/* ─── Plenum (envelope tag 40, SettingsUpdate field 1) ───────────────── */

bool LpSettings_ApplyPlenum(const uint8_t *payload, size_t len)
{
    bool changed = false;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (wire == 5U) {                      /* float fields */
            float f;
            consumed = decode_float(payload + pos, len - pos, &f);
            if (consumed > 0U) {
                switch (field) {
                case 1:
                    s_data.plenum.temp_setpoint = f;
                    /* Snap-guard: when the operator sets a new plenum
                     * temp setpoint manually, force the ramp target to
                     * match so an old ramp config doesn't accidentally
                     * re-arm. Mirrors AS2 StorePostData.c:557 — the
                     * AS2 invariant "TargetTemp == TempSet means ramp
                     * idle". Without this, saving a new setpoint with
                     * a stale non-zero rate immediately starts ramping
                     * back toward the old target. */
                    s_data.ramp_rate.target_temp = f;
                    changed = true;
                    break;
                case 4: s_data.plenum.burner_temp_setpoint = f; changed = true; break;
                case 5: s_data.plenum.burner_threshold     = f; changed = true; break;
                default: break;
                }
            }
        } else if (wire == 0U) {               /* varint fields */
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                switch (field) {
                case 2: s_data.plenum.humid_setpoint     = v; changed = true; break;
                case 3: s_data.plenum.humid_setpoint_ref = v; changed = true; break;
                default: break;
                }
            }
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }
    return changed;
}

size_t LpSettings_BuildPlenumBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_float (buf, bufsize, pos, 1, s_data.plenum.temp_setpoint);
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.plenum.humid_setpoint);
    pos = emit_uint32(buf, bufsize, pos, 3, s_data.plenum.humid_setpoint_ref);
    pos = emit_float (buf, bufsize, pos, 4, s_data.plenum.burner_temp_setpoint);
    pos = emit_float (buf, bufsize, pos, 5, s_data.plenum.burner_threshold);
    return pos;
}

/* ─── FanSpeed (envelope tag 41, SettingsUpdate field 2) ─────────────── */

bool LpSettings_ApplyFanSpeed(const uint8_t *payload, size_t len)
{
    bool changed = false;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (wire == 5U && field == 6U) {       /* temp_diff (float) */
            float f;
            consumed = decode_float(payload + pos, len - pos, &f);
            if (consumed > 0U) { s_data.fan_speed.temp_diff = f; changed = true; }
        } else if (wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                switch (field) {
                case  1: s_data.fan_speed.max_speed     = v; changed = true; break;
                case  2: s_data.fan_speed.min_speed     = v; changed = true; break;
                case  3: s_data.fan_speed.refrig_speed  = v; changed = true; break;
                case  4: s_data.fan_speed.recirc_speed  = v; changed = true; break;
                case  5: s_data.fan_speed.update_period = v; changed = true; break;
                case  7: s_data.fan_speed.temp_ref1     = v; changed = true; break;
                case  8: s_data.fan_speed.temp_ref2     = v; changed = true; break;
                case  9: s_data.fan_speed.prev_speed    = v; changed = true; break;
                case 10: s_data.fan_speed.update_mode   = v; changed = true; break;
                default: break;
                }
            }
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }
    return changed;
}

size_t LpSettings_BuildFanSpeedBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos,  1, s_data.fan_speed.max_speed);
    pos = emit_uint32(buf, bufsize, pos,  2, s_data.fan_speed.min_speed);
    pos = emit_uint32(buf, bufsize, pos,  3, s_data.fan_speed.refrig_speed);
    pos = emit_uint32(buf, bufsize, pos,  4, s_data.fan_speed.recirc_speed);
    pos = emit_uint32(buf, bufsize, pos,  5, s_data.fan_speed.update_period);
    pos = emit_float (buf, bufsize, pos,  6, s_data.fan_speed.temp_diff);
    pos = emit_uint32(buf, bufsize, pos,  7, s_data.fan_speed.temp_ref1);
    pos = emit_uint32(buf, bufsize, pos,  8, s_data.fan_speed.temp_ref2);
    pos = emit_uint32(buf, bufsize, pos,  9, s_data.fan_speed.prev_speed);
    pos = emit_uint32(buf, bufsize, pos, 10, s_data.fan_speed.update_mode);
    return pos;
}

/* ─── FanBoost (envelope tag 42, SettingsUpdate field 3) ─────────────── */

bool LpSettings_ApplyFanBoost(const uint8_t *payload, size_t len)
{
    bool changed = false;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (wire == 5U && field == 5U) {       /* temp (float) */
            float f;
            consumed = decode_float(payload + pos, len - pos, &f);
            if (consumed > 0U) { s_data.fan_boost.temp = f; changed = true; }
        } else if (wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                switch (field) {
                case 1: s_data.fan_boost.mode     = v; changed = true; break;
                case 2: s_data.fan_boost.speed    = v; changed = true; break;
                case 3: s_data.fan_boost.interval = v; changed = true; break;
                case 4: s_data.fan_boost.duration = v; changed = true; break;
                default: break;
                }
            }
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }
    return changed;
}

size_t LpSettings_BuildFanBoostBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos, 1, s_data.fan_boost.mode);
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.fan_boost.speed);
    pos = emit_uint32(buf, bufsize, pos, 3, s_data.fan_boost.interval);
    pos = emit_uint32(buf, bufsize, pos, 4, s_data.fan_boost.duration);
    pos = emit_float (buf, bufsize, pos, 5, s_data.fan_boost.temp);
    return pos;
}

/* ─── RampRate (envelope tag 43, SettingsUpdate field 4) ─────────────── */

bool LpSettings_ApplyRampRate(const uint8_t *payload, size_t len)
{
    bool changed = false;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (wire == 5U) {
            float f;
            consumed = decode_float(payload + pos, len - pos, &f);
            if (consumed > 0U) {
                switch (field) {
                case 1: s_data.ramp_rate.rate_per_day = f; changed = true; break;
                case 3: s_data.ramp_rate.temp_diff    = f; changed = true; break;
                case 5: s_data.ramp_rate.target_temp  = f; changed = true; break;
                default: break;
                }
            }
        } else if (wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                switch (field) {
                case 2: s_data.ramp_rate.update_period = v; changed = true; break;
                case 4: s_data.ramp_rate.temp_ref      = v; changed = true; break;
                default: break;
                }
            }
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }
    return changed;
}

size_t LpSettings_BuildRampRateBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_float (buf, bufsize, pos, 1, s_data.ramp_rate.rate_per_day);
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.ramp_rate.update_period);
    pos = emit_float (buf, bufsize, pos, 3, s_data.ramp_rate.temp_diff);
    pos = emit_uint32(buf, bufsize, pos, 4, s_data.ramp_rate.temp_ref);
    pos = emit_float (buf, bufsize, pos, 5, s_data.ramp_rate.target_temp);
    return pos;
}

/* ─── Refrig (envelope tag 44, SettingsUpdate field 5) ───────────────── */
/* First page with a nested repeated submessage (RefrigStage at field 2).
 * On Apply: append decoded stages to the in-RAM array up to MAX. We
 * clear the array on every Apply call so the wire is authoritative —
 * partial-update isn't a concept the UI exposes for repeated fields. */

static size_t apply_refrig_stage(LpRefrigStage *st,
                                 const uint8_t *payload, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) return 0U;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) return 0U;
            switch (field) {
            case 1: st->on         = v; break;
            case 2: st->off        = v; break;
            case 3: st->diagnostic = v; break;
            default: break;
            }
            pos += vn;
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) return 0U;
            pos += sk;
        }
    }
    return pos;
}

bool LpSettings_ApplyRefrig(const uint8_t *payload, size_t len)
{
    bool changed = false;
    /* Stages array is wire-authoritative: clear before refill so a
     * shorter update genuinely shrinks the array. */
    bool stages_seen = false;
    LpRefrigStage new_stages[LP_REFRIG_MAX_STAGES];
    uint32_t      new_count = 0;
    memset(new_stages, 0, sizeof(new_stages));

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (wire == 2U && field == 2U) {
            /* Repeated RefrigStage. */
            uint32_t sublen;
            size_t ln = decode_varint(payload + pos, len - pos, &sublen);
            if (ln == 0U || ln + sublen > len - pos) break;
            stages_seen = true;
            if (new_count < LP_REFRIG_MAX_STAGES) {
                LpRefrigStage st = { 0 };
                if (apply_refrig_stage(&st, payload + pos + ln, sublen) > 0U) {
                    new_stages[new_count++] = st;
                }
            }
            consumed = ln + sublen;
        } else if (wire == 5U) {
            float f;
            consumed = decode_float(payload + pos, len - pos, &f);
            if (consumed > 0U) {
                switch (field) {
                case  3: s_data.refrig.p_gain  = f; changed = true; break;
                case  4: s_data.refrig.i_gain  = f; changed = true; break;
                case  5: s_data.refrig.d_gain  = f; changed = true; break;
                case  6: s_data.refrig.u_limit = f; changed = true; break;
                case 12: s_data.refrig.limit   = f; changed = true; break;
                default: break;
                }
            }
        } else if (wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                switch (field) {
                case  1: s_data.refrig.num_stages       = v; changed = true; break;
                case  7: s_data.refrig.mode             = v; changed = true; break;
                case  8: s_data.refrig.defrost_interval = v; changed = true; break;
                case  9: s_data.refrig.defrost_duration = v; changed = true; break;
                case 10: s_data.refrig.purge            = v; changed = true; break;
                case 11: s_data.refrig.purge_threshold  = v; changed = true; break;
                case 13: s_data.refrig.fail_mode        = v; changed = true; break;
                default: break;
                }
            }
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }

    if (stages_seen) {
        memcpy(s_data.refrig.stages, new_stages, sizeof(new_stages));
        s_data.refrig.stages_count = new_count;
        changed = true;
    }
    return changed;
}

bool LpSettings_ClearRefrigDiag(void)
{
    /* Mirrors legacy CtrlRefrigDiagClear() in nova_controls.c — zero
     * the per-stage `Diagnostic` latch on every populated refrig stage.
     * AS2 also cleared `Settings.Refrig.Defrost[i].Diagnostic`, but the
     * LP RefrigSettings struct has no defrost-stage array (only the
     * top-level defrost_interval/defrost_duration scalars), so this is
     * the LP-side equivalent. Returns true iff any latch was non-zero;
     * caller (envelope-82 SystemCmd dispatch in main.c) decides whether
     * to persist + re-broadcast. */
    bool changed = false;
    for (uint32_t i = 0; i < s_data.refrig.stages_count && i < LP_REFRIG_MAX_STAGES; i++) {
        if (s_data.refrig.stages[i].diagnostic != 0U) {
            s_data.refrig.stages[i].diagnostic = 0U;
            changed = true;
        }
    }
    return changed;
}

size_t LpSettings_BuildRefrigBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos, 1, s_data.refrig.num_stages);

    /* Repeated stages: emit each as a length-delimited submessage on
     * field 2. Stage inner is at most 3 varints (~6 bytes); 24 B is
     * generous. Stop early on overflow. */
    for (uint32_t i = 0; i < s_data.refrig.stages_count; i++) {
        const LpRefrigStage *st = &s_data.refrig.stages[i];
        uint8_t inner[24];
        size_t  ilen = 0;
        ilen = emit_uint32(inner, sizeof(inner), ilen, 1, st->on);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 2, st->off);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 3, st->diagnostic);
        if (ilen == 0U) continue;   /* fully default → suppress */
        if (pos + 1U + 5U + ilen > bufsize) return 0U;
        buf[pos++] = (uint8_t)((2U << 3) | 2U);   /* tag = field 2, wire 2 */
        pos += encode_varint(&buf[pos], (uint32_t)ilen);
        memcpy(&buf[pos], inner, ilen);
        pos += ilen;
    }

    pos = emit_float (buf, bufsize, pos,  3, s_data.refrig.p_gain);
    pos = emit_float (buf, bufsize, pos,  4, s_data.refrig.i_gain);
    pos = emit_float (buf, bufsize, pos,  5, s_data.refrig.d_gain);
    pos = emit_float (buf, bufsize, pos,  6, s_data.refrig.u_limit);
    pos = emit_uint32(buf, bufsize, pos,  7, s_data.refrig.mode);
    pos = emit_uint32(buf, bufsize, pos,  8, s_data.refrig.defrost_interval);
    pos = emit_uint32(buf, bufsize, pos,  9, s_data.refrig.defrost_duration);
    pos = emit_uint32(buf, bufsize, pos, 10, s_data.refrig.purge);
    pos = emit_uint32(buf, bufsize, pos, 11, s_data.refrig.purge_threshold);
    pos = emit_float (buf, bufsize, pos, 12, s_data.refrig.limit);
    pos = emit_uint32(buf, bufsize, pos, 13, s_data.refrig.fail_mode);
    return pos;
}

/* ─── BurnerSettings (settings.proto field 6, envelope tag 45) ─────── */

bool LpSettings_ApplyBurner(const uint8_t *payload, size_t len)
{
    LpBurner cur = s_data.burner;
    LpBurner nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 1: nw.on = v;     break;
            case 2: nw.low = v;    break;
            case 7: nw.mode = v;   break;
            case 8: nw.manual = v; break;
            default: break;
            }
        } else if (wire == 5U) {
            float f;
            size_t fn = decode_float(payload + pos, len - pos, &f);
            if (fn == 0U) break;
            pos += fn;
            switch (field) {
            case 3: nw.p_gain = f;  break;
            case 4: nw.i_gain = f;  break;
            case 5: nw.d_gain = f;  break;
            case 6: nw.u_limit = f; break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.burner = nw;
    return changed;
}

size_t LpSettings_BuildBurnerBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos, 1, s_data.burner.on);
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.burner.low);
    pos = emit_float (buf, bufsize, pos, 3, s_data.burner.p_gain);
    pos = emit_float (buf, bufsize, pos, 4, s_data.burner.i_gain);
    pos = emit_float (buf, bufsize, pos, 5, s_data.burner.d_gain);
    pos = emit_float (buf, bufsize, pos, 6, s_data.burner.u_limit);
    pos = emit_uint32(buf, bufsize, pos, 7, s_data.burner.mode);
    pos = emit_uint32(buf, bufsize, pos, 8, s_data.burner.manual);
    return pos;
}

/* ─── Co2Settings (settings.proto field 7, envelope tag 46) ────────── */

bool LpSettings_ApplyCo2(const uint8_t *payload, size_t len)
{
    LpCo2 cur = s_data.co2;
    LpCo2 nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 1: nw.mode = v;             break;
            case 4: nw.duration_minutes = v; break;
            case 5: nw.cycle_or_set = v;     break;
            case 6: nw.fan_output = v;       break;
            case 7: nw.door_output = v;      break;
            default: break;
            }
        } else if (wire == 5U) {
            float f;
            size_t fn = decode_float(payload + pos, len - pos, &f);
            if (fn == 0U) break;
            pos += fn;
            switch (field) {
            case 2: nw.min_temp = f; break;
            case 3: nw.max_temp = f; break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.co2 = nw;
    return changed;
}

size_t LpSettings_BuildCo2Body(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos, 1, s_data.co2.mode);
    pos = emit_float (buf, bufsize, pos, 2, s_data.co2.min_temp);
    pos = emit_float (buf, bufsize, pos, 3, s_data.co2.max_temp);
    pos = emit_uint32(buf, bufsize, pos, 4, s_data.co2.duration_minutes);
    pos = emit_uint32(buf, bufsize, pos, 5, s_data.co2.cycle_or_set);
    pos = emit_uint32(buf, bufsize, pos, 6, s_data.co2.fan_output);
    pos = emit_uint32(buf, bufsize, pos, 7, s_data.co2.door_output);
    return pos;
}

/* ─── CureSettings (settings.proto field 8, envelope tag 47) ───────── */

bool LpSettings_ApplyCure(const uint8_t *payload, size_t len)
{
    LpCure cur = s_data.cure;
    LpCure nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            if (field == 2U) nw.humid_ref = v;
        } else if (wire == 5U) {
            float f;
            size_t fn = decode_float(payload + pos, len - pos, &f);
            if (fn == 0U) break;
            pos += fn;
            switch (field) {
            case 1: nw.start_temp = f;       break;
            case 3: nw.start_humid = f;      break;
            case 4: nw.humid_high_limit = f; break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.cure = nw;
    return changed;
}

size_t LpSettings_BuildCureBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_float (buf, bufsize, pos, 1, s_data.cure.start_temp);
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.cure.humid_ref);
    pos = emit_float (buf, bufsize, pos, 3, s_data.cure.start_humid);
    pos = emit_float (buf, bufsize, pos, 4, s_data.cure.humid_high_limit);
    return pos;
}

/* ─── ClimacellSettings (settings.proto field 9, envelope tag 48) ──── */

bool LpSettings_ApplyClimacell(const uint8_t *payload, size_t len)
{
    LpClimacell cur = s_data.climacell;
    LpClimacell nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 1: nw.efficiency = v; break;
            case 2: nw.altitude = v;   break;
            case 3: nw.alt_units = v;  break;
            default: break;
            }
        } else if (wire == 5U) {
            float f;
            size_t fn = decode_float(payload + pos, len - pos, &f);
            if (fn == 0U) break;
            pos += fn;
            switch (field) {
            case 4: nw.p_gain = f;  break;
            case 5: nw.i_gain = f;  break;
            case 6: nw.d_gain = f;  break;
            case 7: nw.u_limit = f; break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.climacell = nw;
    return changed;
}

size_t LpSettings_BuildClimacellBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos, 1, s_data.climacell.efficiency);
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.climacell.altitude);
    pos = emit_uint32(buf, bufsize, pos, 3, s_data.climacell.alt_units);
    pos = emit_float (buf, bufsize, pos, 4, s_data.climacell.p_gain);
    pos = emit_float (buf, bufsize, pos, 5, s_data.climacell.i_gain);
    pos = emit_float (buf, bufsize, pos, 6, s_data.climacell.d_gain);
    pos = emit_float (buf, bufsize, pos, 7, s_data.climacell.u_limit);
    return pos;
}

/* ─── ClimacellTimes (settings.proto field 10, envelope tag 49) ─────
 *
 * Wire layout: `repeated uint32 hourly_efficiency = 1;` — proto3
 * defaults to PACKED, so the canonical wire is `0x0A <len> <varint>...`
 * (one length-delimited submsg containing concatenated varints). We
 * accept both packed and unpacked-repeated on the apply path. The
 * emit path always writes packed (matches protoc default and is
 * more compact). */

bool LpSettings_ApplyClimacellTimes(const uint8_t *payload, size_t len)
{
    /* Wire-authoritative: any payload that mentions field 1 REPLACES
     * the whole hourly bank. Tracks via local accumulator. */
    uint32_t new_hourly[LP_CLIMACELL_HOURS] = {0};
    uint32_t new_count = 0;
    bool seen = false;

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        if (field == 1U && wire == 2U) {
            /* Packed varints. */
            uint32_t plen;
            size_t pn = decode_varint(payload + pos, len - pos, &plen);
            if (pn == 0U || pos + pn + plen > len) break;
            pos += pn;
            size_t end = pos + plen;
            seen = true;
            while (pos < end) {
                uint32_t v;
                size_t vn = decode_varint(payload + pos, end - pos, &v);
                if (vn == 0U) break;
                pos += vn;
                if (new_count < LP_CLIMACELL_HOURS) {
                    new_hourly[new_count++] = v;
                }
            }
            pos = end;
        } else if (field == 1U && wire == 0U) {
            /* Unpacked repeated varint. */
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            seen = true;
            if (new_count < LP_CLIMACELL_HOURS) {
                new_hourly[new_count++] = v;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }

    if (!seen && len > 0U) return false;
    bool changed = (new_count != s_data.climacell_times.hourly_count) ||
                   (memcmp(s_data.climacell_times.hourly_efficiency,
                           new_hourly, sizeof(new_hourly)) != 0);
    if (changed) {
        memcpy(s_data.climacell_times.hourly_efficiency, new_hourly,
               sizeof(new_hourly));
        s_data.climacell_times.hourly_count = new_count;
    }
    return changed;
}

size_t LpSettings_BuildClimacellTimesBody(uint8_t *buf, size_t bufsize)
{
    if (s_data.climacell_times.hourly_count == 0U) return 0U;

    /* Two-pass: first compute packed body length (sum of varint sizes),
     * then emit `tag(1<<3|2) | varint(len) | varints…`. */
    size_t inner_len = 0;
    for (uint32_t i = 0; i < s_data.climacell_times.hourly_count; i++) {
        uint8_t tmp[10];
        inner_len += encode_varint(tmp, s_data.climacell_times.hourly_efficiency[i]);
    }
    if (inner_len == 0U) return 0U;
    if (1U + 5U + inner_len > bufsize) return 0U;

    size_t pos = 0;
    buf[pos++] = (uint8_t)((1U << 3) | 2U);   /* tag = field 1, wire 2 */
    pos += encode_varint(&buf[pos], (uint32_t)inner_len);
    for (uint32_t i = 0; i < s_data.climacell_times.hourly_count; i++) {
        pos += encode_varint(&buf[pos], s_data.climacell_times.hourly_efficiency[i]);
    }
    return pos;
}

/* ─── HumidCtrlSettings (settings.proto field 11, envelope tag 50) ───
 *
 * SAVE direction: UI sends ONE entry (the humidifier being edited).
 * Splice into entries[entry.index]. LOAD direction: emit all 3 entries
 * (or up to entries_count if fewer). */

static bool apply_humid_entry(LpHumidCtrlEntry *out, const uint8_t *payload, size_t len)
{
    LpHumidCtrlEntry tmp = {0};
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 1: tmp.index = v;      break;
            case 2: tmp.mode = v;       break;
            case 3: tmp.cool_on = v;    break;
            case 4: tmp.cool_off = v;   break;
            case 5: tmp.recirc_on = v;  break;
            case 6: tmp.recirc_off = v; break;
            case 7: tmp.refrig_on = v;  break;
            case 8: tmp.refrig_off = v; break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    *out = tmp;
    return true;
}

bool LpSettings_ApplyHumidCtrl(const uint8_t *payload, size_t len)
{
    /* Splice-by-index semantics: each `entries` submsg is decoded
     * standalone and dropped into entries[entry.index]. Out-of-range
     * indexes silently dropped. Multiple entries with the same index
     * → last one wins (matches save-then-batch UI behavior). */
    bool changed = false;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (field == 1U && wire == 2U) {
            uint32_t plen;
            size_t pn = decode_varint(payload + pos, len - pos, &plen);
            if (pn == 0U || pos + pn + plen > len) break;
            pos += pn;
            LpHumidCtrlEntry e;
            (void)apply_humid_entry(&e, payload + pos, plen);
            pos += plen;
            if (e.index < LP_HUMID_CTRL_MAX) {
                if (memcmp(&s_data.humid_ctrl.entries[e.index], &e, sizeof(e)) != 0) {
                    s_data.humid_ctrl.entries[e.index] = e;
                    changed = true;
                }
                if (e.index + 1U > s_data.humid_ctrl.entries_count) {
                    s_data.humid_ctrl.entries_count = e.index + 1U;
                    changed = true;
                }
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    return changed;
}

size_t LpSettings_BuildHumidCtrlBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    for (uint32_t i = 0; i < s_data.humid_ctrl.entries_count; i++) {
        const LpHumidCtrlEntry *e = &s_data.humid_ctrl.entries[i];
        uint8_t inner[40];
        size_t  ilen = 0;
        /* index is field 1; force-emit so index=0 survives proto3
         * zero-suppression. */
        if (ilen + 2U > sizeof(inner)) continue;
        inner[ilen++] = (uint8_t)((1U << 3) | 0U);
        ilen += encode_varint(&inner[ilen], e->index);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 2, e->mode);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 3, e->cool_on);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 4, e->cool_off);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 5, e->recirc_on);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 6, e->recirc_off);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 7, e->refrig_on);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 8, e->refrig_off);
        if (ilen == 0U) continue;
        if (pos + 1U + 5U + ilen > bufsize) return 0U;
        buf[pos++] = (uint8_t)((1U << 3) | 2U);
        pos += encode_varint(&buf[pos], (uint32_t)ilen);
        memcpy(&buf[pos], inner, ilen);
        pos += ilen;
    }
    return pos;
}

/* ─── OutsideAirSettings (settings.proto field 12, envelope tag 51) ── */

bool LpSettings_ApplyOutsideAir(const uint8_t *payload, size_t len)
{
    LpOutsideAir cur = s_data.outside_air;
    LpOutsideAir nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 1: nw.mode = v;            break;
            case 3: nw.above_below = v;     break;
            case 4: nw.temp_ref = v;        break;
            case 5: nw.calc_humid_max = v;  break;
            default: break;
            }
        } else if (wire == 5U) {
            float f;
            size_t fn = decode_float(payload + pos, len - pos, &f);
            if (fn == 0U) break;
            pos += fn;
            if (field == 2U) nw.differential = f;
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.outside_air = nw;
    return changed;
}

size_t LpSettings_BuildOutsideAirBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos, 1, s_data.outside_air.mode);
    pos = emit_float (buf, bufsize, pos, 2, s_data.outside_air.differential);
    pos = emit_uint32(buf, bufsize, pos, 3, s_data.outside_air.above_below);
    pos = emit_uint32(buf, bufsize, pos, 4, s_data.outside_air.temp_ref);
    pos = emit_uint32(buf, bufsize, pos, 5, s_data.outside_air.calc_humid_max);
    return pos;
}

/* ─── MiscSettings (settings.proto field 13, envelope tag 52) ──────── */

bool LpSettings_ApplyMisc(const uint8_t *payload, size_t len)
{
    LpMisc cur = s_data.misc;
    LpMisc nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 1:  nw.refrig_mode = v;            break;
            case 2:  nw.defrost_interval = v;       break;
            case 3:  nw.defrost_duration = v;       break;
            case 5:  nw.cavity_target = v;          break;
            case 6:  nw.cavity_mode = v;            break;
            case 8:  nw.cavity_duty_or_sensor = v;  break;
            case 9:  nw.cavity_standby_on = v;      break;
            case 10: nw.kb_pref = v;                break;
            case 11: nw.lights_fail_units = v;      break;
            case 12: nw.enthalpy_off_pct = v;       break;
            default: break;
            }
        } else if (wire == 5U) {
            float f;
            size_t fn = decode_float(payload + pos, len - pos, &f);
            if (fn == 0U) break;
            pos += fn;
            switch (field) {
            case 4: nw.heat_temp_thresh = f; break;
            case 7: nw.cavity_diff = f;      break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.misc = nw;
    return changed;
}

size_t LpSettings_BuildMiscBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos,  1, s_data.misc.refrig_mode);
    pos = emit_uint32(buf, bufsize, pos,  2, s_data.misc.defrost_interval);
    pos = emit_uint32(buf, bufsize, pos,  3, s_data.misc.defrost_duration);
    pos = emit_float (buf, bufsize, pos,  4, s_data.misc.heat_temp_thresh);
    pos = emit_uint32(buf, bufsize, pos,  5, s_data.misc.cavity_target);
    pos = emit_uint32(buf, bufsize, pos,  6, s_data.misc.cavity_mode);
    pos = emit_float (buf, bufsize, pos,  7, s_data.misc.cavity_diff);
    pos = emit_uint32(buf, bufsize, pos,  8, s_data.misc.cavity_duty_or_sensor);
    pos = emit_uint32(buf, bufsize, pos,  9, s_data.misc.cavity_standby_on);
    pos = emit_uint32(buf, bufsize, pos, 10, s_data.misc.kb_pref);
    pos = emit_uint32(buf, bufsize, pos, 11, s_data.misc.lights_fail_units);
    pos = emit_uint32(buf, bufsize, pos, 12, s_data.misc.enthalpy_off_pct);
    return pos;
}

/* ─── FailureSettings (settings.proto field 14, envelope tag 53) ──── */

bool LpSettings_ApplyFailure(const uint8_t *payload, size_t len)
{
    LpFailure cur = s_data.failure;
    LpFailure nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 1:  nw.fan_mode = v;            break;
            case 2:  nw.fan_timer = v;           break;
            case 3:  nw.heat_mode = v;           break;
            case 4:  nw.heat_timer = v;          break;
            case 5:  nw.refrig_mode = v;         break;
            case 6:  nw.refrig_timer = v;        break;
            case 7:  nw.refrig_fail_mode = v;    break;
            case 8:  nw.burner_mode = v;         break;
            case 9:  nw.burner_timer = v;        break;
            case 10: nw.humid_timer = v;         break;
            case 11: nw.climacell_timer = v;     break;
            case 12: nw.lights_mode = v;         break;
            case 13: nw.lights_timer = v;        break;
            case 14: nw.lights_units = v;        break;
            case 15: nw.climacell_mode = v;      break;
            case 16: nw.refrig_stages_mode = v;  break;
            case 17: nw.refrig_stages_timer = v; break;
            case 18: nw.humid_mode = v;          break;
            case 19: nw.aux_mode = v;            break;
            case 20: nw.aux_timer = v;           break;
            case 21: nw.cavity_heat_mode = v;    break;
            case 22: nw.cavity_heat_timer = v;   break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.failure = nw;
    return changed;
}

size_t LpSettings_BuildFailureBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos,  1, s_data.failure.fan_mode);
    pos = emit_uint32(buf, bufsize, pos,  2, s_data.failure.fan_timer);
    pos = emit_uint32(buf, bufsize, pos,  3, s_data.failure.heat_mode);
    pos = emit_uint32(buf, bufsize, pos,  4, s_data.failure.heat_timer);
    pos = emit_uint32(buf, bufsize, pos,  5, s_data.failure.refrig_mode);
    pos = emit_uint32(buf, bufsize, pos,  6, s_data.failure.refrig_timer);
    pos = emit_uint32(buf, bufsize, pos,  7, s_data.failure.refrig_fail_mode);
    pos = emit_uint32(buf, bufsize, pos,  8, s_data.failure.burner_mode);
    pos = emit_uint32(buf, bufsize, pos,  9, s_data.failure.burner_timer);
    pos = emit_uint32(buf, bufsize, pos, 10, s_data.failure.humid_timer);
    pos = emit_uint32(buf, bufsize, pos, 11, s_data.failure.climacell_timer);
    pos = emit_uint32(buf, bufsize, pos, 12, s_data.failure.lights_mode);
    pos = emit_uint32(buf, bufsize, pos, 13, s_data.failure.lights_timer);
    pos = emit_uint32(buf, bufsize, pos, 14, s_data.failure.lights_units);
    pos = emit_uint32(buf, bufsize, pos, 15, s_data.failure.climacell_mode);
    pos = emit_uint32(buf, bufsize, pos, 16, s_data.failure.refrig_stages_mode);
    pos = emit_uint32(buf, bufsize, pos, 17, s_data.failure.refrig_stages_timer);
    pos = emit_uint32(buf, bufsize, pos, 18, s_data.failure.humid_mode);
    pos = emit_uint32(buf, bufsize, pos, 19, s_data.failure.aux_mode);
    pos = emit_uint32(buf, bufsize, pos, 20, s_data.failure.aux_timer);
    pos = emit_uint32(buf, bufsize, pos, 21, s_data.failure.cavity_heat_mode);
    pos = emit_uint32(buf, bufsize, pos, 22, s_data.failure.cavity_heat_timer);
    return pos;
}

/* ─── FailureSettings2 (settings.proto field 15, envelope tag 54) ─── */

bool LpSettings_ApplyFailure2(const uint8_t *payload, size_t len)
{
    LpFailure2 cur = s_data.failure2;
    LpFailure2 nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 1:  nw.out_air_mode = v;     break;
            case 2:  nw.out_air_timer = v;    break;
            case 3:  nw.out_humid_mode = v;   break;
            case 4:  nw.out_humid_timer = v;  break;
            case 5:  nw.high_co2_mode = v;    break;
            case 6:  nw.high_co2_timer = v;   break;
            case 7:  nw.co2_setpt = v;        break;
            case 8:  nw.low_humid_mode = v;   break;
            case 9:  nw.low_humid_timer = v;  break;
            case 10: nw.low_humid_set = v;    break;
            case 11: nw.plen_sen_mode = v;    break;
            case 12: nw.plen_sen_timer = v;   break;
            default: break;
            }
        } else if (wire == 5U) {
            float f;
            size_t fn = decode_float(payload + pos, len - pos, &f);
            if (fn == 0U) break;
            pos += fn;
            if (field == 13U) nw.plen_sen_diff = f;
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.failure2 = nw;
    return changed;
}

size_t LpSettings_BuildFailure2Body(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos,  1, s_data.failure2.out_air_mode);
    pos = emit_uint32(buf, bufsize, pos,  2, s_data.failure2.out_air_timer);
    pos = emit_uint32(buf, bufsize, pos,  3, s_data.failure2.out_humid_mode);
    pos = emit_uint32(buf, bufsize, pos,  4, s_data.failure2.out_humid_timer);
    pos = emit_uint32(buf, bufsize, pos,  5, s_data.failure2.high_co2_mode);
    pos = emit_uint32(buf, bufsize, pos,  6, s_data.failure2.high_co2_timer);
    pos = emit_uint32(buf, bufsize, pos,  7, s_data.failure2.co2_setpt);
    pos = emit_uint32(buf, bufsize, pos,  8, s_data.failure2.low_humid_mode);
    pos = emit_uint32(buf, bufsize, pos,  9, s_data.failure2.low_humid_timer);
    pos = emit_uint32(buf, bufsize, pos, 10, s_data.failure2.low_humid_set);
    pos = emit_uint32(buf, bufsize, pos, 11, s_data.failure2.plen_sen_mode);
    pos = emit_uint32(buf, bufsize, pos, 12, s_data.failure2.plen_sen_timer);
    pos = emit_float (buf, bufsize, pos, 13, s_data.failure2.plen_sen_diff);
    return pos;
}

/* ─── TempAlarmSettings (settings.proto field 16, envelope tag 55) ── */

bool LpSettings_ApplyTempAlarm(const uint8_t *payload, size_t len)
{
    LpTempAlarm cur = s_data.temp_alarm;
    LpTempAlarm nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 2: nw.low_timer = v;  break;
            case 4: nw.high_timer = v; break;
            default: break;
            }
        } else if (wire == 5U) {
            float f;
            size_t fn = decode_float(payload + pos, len - pos, &f);
            if (fn == 0U) break;
            pos += fn;
            switch (field) {
            case 1: nw.low_temp = f;   break;
            case 3: nw.high_temp = f;  break;
            case 5: nw.cure_low = f;   break;
            case 6: nw.cure_high = f;  break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.temp_alarm = nw;
    return changed;
}

size_t LpSettings_BuildTempAlarmBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_float (buf, bufsize, pos, 1, s_data.temp_alarm.low_temp);
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.temp_alarm.low_timer);
    pos = emit_float (buf, bufsize, pos, 3, s_data.temp_alarm.high_temp);
    pos = emit_uint32(buf, bufsize, pos, 4, s_data.temp_alarm.high_timer);
    pos = emit_float (buf, bufsize, pos, 5, s_data.temp_alarm.cure_low);
    pos = emit_float (buf, bufsize, pos, 6, s_data.temp_alarm.cure_high);
    return pos;
}

/* ─── DoorSettings (settings.proto field 19, envelope tag 56) ─────── */

bool LpSettings_ApplyDoor(const uint8_t *payload, size_t len)
{
    LpDoor cur = s_data.door;
    LpDoor nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 5: nw.actuator_time = v;  break;
            case 6: nw.cool_air_cycle = v; break;
            case 7: nw.manual_pct    = (v > 100U) ? 100U : v; break;
            default: break;
            }
        } else if (wire == 5U) {
            float f;
            size_t fn = decode_float(payload + pos, len - pos, &f);
            if (fn == 0U) break;
            pos += fn;
            switch (field) {
            case 1: nw.p_gain = f;  break;
            case 2: nw.i_gain = f;  break;
            case 3: nw.d_gain = f;  break;
            case 4: nw.u_limit = f; break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.door = nw;
    return changed;
}

size_t LpSettings_BuildDoorBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_float (buf, bufsize, pos, 1, s_data.door.p_gain);
    pos = emit_float (buf, bufsize, pos, 2, s_data.door.i_gain);
    pos = emit_float (buf, bufsize, pos, 3, s_data.door.d_gain);
    pos = emit_float (buf, bufsize, pos, 4, s_data.door.u_limit);
    pos = emit_uint32(buf, bufsize, pos, 5, s_data.door.actuator_time);
    pos = emit_uint32(buf, bufsize, pos, 6, s_data.door.cool_air_cycle);
    pos = emit_uint32(buf, bufsize, pos, 7, s_data.door.manual_pct);
    return pos;
}

/* ─── CureLimitSettings (settings.proto field 17, envelope tag 57) ── */

bool LpSettings_ApplyCureLimit(const uint8_t *payload, size_t len)
{
    LpCureLimit cur = s_data.cure_limit;
    LpCureLimit nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 5U) {
            float f;
            size_t fn = decode_float(payload + pos, len - pos, &f);
            if (fn == 0U) break;
            pos += fn;
            switch (field) {
            case 1: nw.temp_low_limit  = f; break;
            case 2: nw.temp_high_limit = f; break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.cure_limit = nw;
    return changed;
}

size_t LpSettings_BuildCureLimitBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_float(buf, bufsize, pos, 1, s_data.cure_limit.temp_low_limit);
    pos = emit_float(buf, bufsize, pos, 2, s_data.cure_limit.temp_high_limit);
    return pos;
}

/* ─── UserLogSettings (settings.proto field 22, envelope tag 58) ───── */

bool LpSettings_ApplyUserLog(const uint8_t *payload, size_t len)
{
    LpUserLog cur = s_data.user_log;
    LpUserLog nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 1: nw.interval_minutes = v; break;
            case 2: nw.enabled          = v; break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.user_log = nw;
    return changed;
}

size_t LpSettings_BuildUserLogBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos, 1, s_data.user_log.interval_minutes);
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.user_log.enabled);
    return pos;
}

/* ─── PidSettings (settings.proto field 23, envelope tag 59) ───────── */
/* Single-equipment PID write. UI sends the eq_index plus all four
 * gains + wrap; we mirror them flat in our struct. (No per-equipment
 * array yet — this matches the legacy single-active-PID-page UX.) */

bool LpSettings_ApplyPid(const uint8_t *payload, size_t len)
{
    LpPid cur = s_data.pid;
    LpPid nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 1: nw.eq_index = v; break;
            case 6: nw.wrap     = v; break;
            default: break;
            }
        } else if (wire == 5U) {
            float f;
            size_t fn = decode_float(payload + pos, len - pos, &f);
            if (fn == 0U) break;
            pos += fn;
            switch (field) {
            case 2: nw.p_gain  = f; break;
            case 3: nw.i_gain  = f; break;
            case 4: nw.d_gain  = f; break;
            case 5: nw.u_limit = f; break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.pid = nw;
    return changed;
}

size_t LpSettings_BuildPidBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos, 1, s_data.pid.eq_index);
    pos = emit_float (buf, bufsize, pos, 2, s_data.pid.p_gain);
    pos = emit_float (buf, bufsize, pos, 3, s_data.pid.i_gain);
    pos = emit_float (buf, bufsize, pos, 4, s_data.pid.d_gain);
    pos = emit_float (buf, bufsize, pos, 5, s_data.pid.u_limit);
    pos = emit_uint32(buf, bufsize, pos, 6, s_data.pid.wrap);
    return pos;
}

/* ─── MasterSlaveSettings (settings.proto field 25, envelope tag 60) ── */

bool LpSettings_ApplyMasterSlave(const uint8_t *payload, size_t len)
{
    bool changed = false;
    /* Snapshot prior mode so we can detect a transition INTO Slave
     * and reset the no-broadcast watchdog (otherwise a freshly
     * configured slave would trip WARN_NOBROADCAST instantly because
     * `XTimerVal - 0 >= 10*T_MINS`). Mirrors AS2's StoreMasterSlave
     * (StorePostData.c::1563). */
    const uint32_t prior_mode = s_data.master_slave.mode;
    /* Repeated fields fully replace on each save (matches every other
     * repeated decoder in this file). Reset counters before walking;
     * if the wire carries no entries, the lists end up empty. */
    uint32_t new_slave_count   = 0U;
    uint32_t new_allowed_count = 0U;
    char new_slaves [LP_MS_PEER_MAX][LP_SET_STR_MAX];
    char new_allowed[LP_MS_PEER_MAX][LP_SET_STR_MAX];
    /* memset suppresses uninitialised-warning + lets us memcmp below. */
    memset(new_slaves,  0, sizeof(new_slaves));
    memset(new_allowed, 0, sizeof(new_allowed));

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (wire == 0U && field == 1U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U && s_data.master_slave.mode != v) {
                s_data.master_slave.mode = v;
                changed = true;
            }
        } else if (wire == 2U && field == 2U) {
            consumed = copy_string_field(payload + pos, len - pos,
                                         s_data.master_slave.master_ip,
                                         sizeof(s_data.master_slave.master_ip));
            if (consumed > 0U) changed = true;
        } else if (wire == 2U && field == 3U) {
            /* repeated string slave_ips */
            if (new_slave_count < LP_MS_PEER_MAX) {
                consumed = copy_string_field(payload + pos, len - pos,
                                             new_slaves[new_slave_count],
                                             LP_SET_STR_MAX);
                if (consumed > 0U) new_slave_count++;
            } else {
                consumed = skip_field(wire, payload + pos, len - pos);
            }
        } else if (wire == 2U && field == 4U) {
            /* repeated string allowed_masters */
            if (new_allowed_count < LP_MS_PEER_MAX) {
                consumed = copy_string_field(payload + pos, len - pos,
                                             new_allowed[new_allowed_count],
                                             LP_SET_STR_MAX);
                if (consumed > 0U) new_allowed_count++;
            } else {
                consumed = skip_field(wire, payload + pos, len - pos);
            }
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }

    if (new_slave_count != s_data.master_slave.slave_count ||
        memcmp(new_slaves, s_data.master_slave.slave_ips, sizeof(new_slaves)) != 0) {
        s_data.master_slave.slave_count = new_slave_count;
        memcpy(s_data.master_slave.slave_ips, new_slaves, sizeof(new_slaves));
        changed = true;
    }
    if (new_allowed_count != s_data.master_slave.allowed_count ||
        memcmp(new_allowed, s_data.master_slave.allowed_masters,
               sizeof(new_allowed)) != 0) {
        s_data.master_slave.allowed_count = new_allowed_count;
        memcpy(s_data.master_slave.allowed_masters, new_allowed,
               sizeof(new_allowed));
        changed = true;
    }

    /* Transition INTO Slave mode (from anything else): clear the
     * remote-outside cache + restart the no-broadcast watchdog so the
     * slave gets a fresh 10-minute window before its first alarm. */
    if (prior_mode != 2U /* Slave */ && s_data.master_slave.mode == 2U) {
        LpRemoteOutside_Clear();
    }
    return changed;
}

size_t LpSettings_BuildMasterSlaveBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos, 1, s_data.master_slave.mode);
    pos = emit_string(buf, bufsize, pos, 2, s_data.master_slave.master_ip);
    for (uint32_t i = 0U; i < s_data.master_slave.slave_count; i++) {
        pos = emit_string(buf, bufsize, pos, 3, s_data.master_slave.slave_ips[i]);
    }
    for (uint32_t i = 0U; i < s_data.master_slave.allowed_count; i++) {
        pos = emit_string(buf, bufsize, pos, 4, s_data.master_slave.allowed_masters[i]);
    }
    return pos;
}

/* ─── HttpPortSettings (settings.proto field 33, envelope tag 61) ──── */

bool LpSettings_ApplyHttpPort(const uint8_t *payload, size_t len)
{
    LpHttpPort cur = s_data.http_port;
    LpHttpPort nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U && field == 1U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            nw.port = v;
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (cur.port != nw.port);
    if (changed) s_data.http_port = nw;
    return changed;
}

size_t LpSettings_BuildHttpPortBody(uint8_t *buf, size_t bufsize)
{
    return emit_uint32(buf, bufsize, 0, 1, s_data.http_port.port);
}

/* ─── PublicAddressSettings (settings.proto field 34, envelope tag 62) ─ */
/* NOTE proto3 zero-suppress: oct=0 octets are not emitted. The receiver
 * decodes missing fields as 0, so 0.0.0.0 round-trips correctly, but a
 * "10.0.0.5" address will only place tags for oct1 and oct4 on the wire
 * — that's intentional and matches the proto contract. */

bool LpSettings_ApplyPublicAddress(const uint8_t *payload, size_t len)
{
    LpPublicAddress cur = s_data.public_address;
    LpPublicAddress nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 1: nw.oct1 = v; break;
            case 2: nw.oct2 = v; break;
            case 3: nw.oct3 = v; break;
            case 4: nw.oct4 = v; break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.public_address = nw;
    return changed;
}

size_t LpSettings_BuildPublicAddressBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos, 1, s_data.public_address.oct1);
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.public_address.oct2);
    pos = emit_uint32(buf, bufsize, pos, 3, s_data.public_address.oct3);
    pos = emit_uint32(buf, bufsize, pos, 4, s_data.public_address.oct4);
    return pos;
}

/* ─── SysModeSettings (settings.proto field 36, envelope tag 63) ──── */

bool LpSettings_ApplySysMode(const uint8_t *payload, size_t len)
{
    LpSysMode cur = s_data.sys_mode;
    LpSysMode nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U && field == 1U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            nw.mode = v;
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (cur.mode != nw.mode);
    if (changed) s_data.sys_mode = nw;
    return changed;
}

size_t LpSettings_BuildSysModeBody(uint8_t *buf, size_t bufsize)
{
    return emit_uint32(buf, bufsize, 0, 1, s_data.sys_mode.mode);
}

/* ─── PidLogSettings (settings.proto field 37, envelope tag 64) ───── */

bool LpSettings_ApplyPidLog(const uint8_t *payload, size_t len)
{
    LpPidLog cur = s_data.pid_log;
    LpPidLog nw  = cur;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            switch (field) {
            case 1: nw.wrap       = v; break;
            case 2: nw.log_doors  = v; break;
            case 3: nw.log_refrig = v; break;
            default: break;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    bool changed = (memcmp(&cur, &nw, sizeof(nw)) != 0);
    if (changed) s_data.pid_log = nw;
    return changed;
}

size_t LpSettings_BuildPidLogBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_uint32(buf, bufsize, pos, 1, s_data.pid_log.wrap);
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.pid_log.log_doors);
    pos = emit_uint32(buf, bufsize, pos, 3, s_data.pid_log.log_refrig);
    return pos;
}

/* ─── ServiceInfoUpdate (settings.proto field 31, envelope tag 65) ── */

bool LpSettings_ApplyServiceInfo(const uint8_t *payload, size_t len)
{
    bool changed = false;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (wire == 2U) {
            char *dst = NULL;
            switch (field) {
            case 1: dst = s_data.service_info.dealer_name;  break;
            case 2: dst = s_data.service_info.dealer_phone; break;
            case 3: dst = s_data.service_info.dealer_email; break;
            case 4: dst = s_data.service_info.notes;        break;
            default: break;
            }
            if (dst != NULL) {
                consumed = copy_string_field(payload + pos, len - pos,
                                             dst, LP_SET_STR_MAX);
                if (consumed > 0U) changed = true;
            } else {
                consumed = skip_field(wire, payload + pos, len - pos);
            }
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }
    return changed;
}

size_t LpSettings_BuildServiceInfoBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_string(buf, bufsize, pos, 1, s_data.service_info.dealer_name);
    pos = emit_string(buf, bufsize, pos, 2, s_data.service_info.dealer_phone);
    pos = emit_string(buf, bufsize, pos, 3, s_data.service_info.dealer_email);
    pos = emit_string(buf, bufsize, pos, 4, s_data.service_info.notes);
    return pos;
}

/* ─── EmailSettings (settings.proto field 27, envelope tag 66) ────── */

bool LpSettings_ApplyEmail(const uint8_t *payload, size_t len)
{
    bool changed = false;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                switch (field) {
                case 2: if (s_data.email.port      != v) { s_data.email.port      = v; changed = true; } break;
                case 7: if (s_data.email.enabled   != v) { s_data.email.enabled   = v; changed = true; } break;
                case 8: if (s_data.email.auth_type != v) { s_data.email.auth_type = v; changed = true; } break;
                default: break;
                }
            }
        } else if (wire == 2U) {
            char *dst = NULL;
            switch (field) {
            case 1: dst = s_data.email.server;     break;
            case 3: dst = s_data.email.username;   break;
            case 4: dst = s_data.email.password;   break;
            case 5: dst = s_data.email.from_addr;  break;
            case 6: dst = s_data.email.to_addr;    break;
            case 9: dst = s_data.email.display_id; break;
            default: break;
            }
            if (dst != NULL) {
                consumed = copy_string_field(payload + pos, len - pos,
                                             dst, LP_SET_STR_MAX);
                if (consumed > 0U) changed = true;
            } else {
                consumed = skip_field(wire, payload + pos, len - pos);
            }
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }
    return changed;
}

size_t LpSettings_BuildEmailBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_string(buf, bufsize, pos, 1, s_data.email.server);
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.email.port);
    pos = emit_string(buf, bufsize, pos, 3, s_data.email.username);
    pos = emit_string(buf, bufsize, pos, 4, s_data.email.password);
    pos = emit_string(buf, bufsize, pos, 5, s_data.email.from_addr);
    pos = emit_string(buf, bufsize, pos, 6, s_data.email.to_addr);
    pos = emit_uint32(buf, bufsize, pos, 7, s_data.email.enabled);
    pos = emit_uint32(buf, bufsize, pos, 8, s_data.email.auth_type);
    pos = emit_string(buf, bufsize, pos, 9, s_data.email.display_id);
    return pos;
}

/* ─── GraphFavoriteSettings (settings.proto field 24, envelope tag 67) ─
 * Packed-repeated uint32 — same pattern as ClimacellTimes. Wire is
 * authoritative: clear before refill so a shorter list shrinks. */

bool LpSettings_ApplyGraphFavorites(const uint8_t *payload, size_t len)
{
    bool seen = false;
    uint32_t new_favs[LP_GRAPH_FAV_MAX] = {0};
    uint32_t new_count = 0;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        if (field == 1U && wire == 2U) {
            uint32_t plen;
            size_t pn = decode_varint(payload + pos, len - pos, &plen);
            if (pn == 0U || pos + pn + plen > len) break;
            pos += pn;
            size_t end = pos + plen;
            seen = true;
            while (pos < end) {
                uint32_t v;
                size_t vn = decode_varint(payload + pos, end - pos, &v);
                if (vn == 0U) break;
                pos += vn;
                if (new_count < LP_GRAPH_FAV_MAX) new_favs[new_count++] = v;
            }
            pos = end;
        } else if (field == 1U && wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            seen = true;
            if (new_count < LP_GRAPH_FAV_MAX) new_favs[new_count++] = v;
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    if (!seen && len > 0U) return false;
    bool changed = (new_count != s_data.graph_favorites.count) ||
                   (memcmp(s_data.graph_favorites.favorites, new_favs,
                           sizeof(new_favs)) != 0);
    if (changed) {
        memcpy(s_data.graph_favorites.favorites, new_favs, sizeof(new_favs));
        s_data.graph_favorites.count = new_count;
    }
    return changed;
}

size_t LpSettings_BuildGraphFavoritesBody(uint8_t *buf, size_t bufsize)
{
    if (s_data.graph_favorites.count == 0U) return 0U;
    size_t inner_len = 0;
    for (uint32_t i = 0; i < s_data.graph_favorites.count; i++) {
        uint8_t tmp[10];
        inner_len += encode_varint(tmp, s_data.graph_favorites.favorites[i]);
    }
    if (inner_len == 0U) return 0U;
    if (1U + 5U + inner_len > bufsize) return 0U;
    size_t pos = 0;
    buf[pos++] = (uint8_t)((1U << 3) | 2U);
    pos += encode_varint(&buf[pos], (uint32_t)inner_len);
    for (uint32_t i = 0; i < s_data.graph_favorites.count; i++) {
        pos += encode_varint(&buf[pos], s_data.graph_favorites.favorites[i]);
    }
    return pos;
}

/* ─── AlertSettings (settings.proto field 28, envelope tag 68) ─────── */

bool LpSettings_ApplyAlert(const uint8_t *payload, size_t len)
{
    bool seen = false;
    uint32_t new_flags[LP_ALERT_MAX] = {0};
    uint32_t new_count = 0;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        if (field == 1U && wire == 2U) {
            uint32_t plen;
            size_t pn = decode_varint(payload + pos, len - pos, &plen);
            if (pn == 0U || pos + pn + plen > len) break;
            pos += pn;
            size_t end = pos + plen;
            seen = true;
            while (pos < end) {
                uint32_t v;
                size_t vn = decode_varint(payload + pos, end - pos, &v);
                if (vn == 0U) break;
                pos += vn;
                if (new_count < LP_ALERT_MAX) new_flags[new_count++] = v;
            }
            pos = end;
        } else if (field == 1U && wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) break;
            pos += vn;
            seen = true;
            if (new_count < LP_ALERT_MAX) new_flags[new_count++] = v;
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    /* Wire-authoritative replace: an empty payload (user un-checked
     * every alert) is a legitimate "clear the list" command. ts-proto
     * encodes a zero-length `repeated` as zero bytes, so field 1 never
     * appears on the wire in that case — but the bridge still calls
     * us with len==0 to mean "this is your new list". Treat that as
     * seen so an empty new_flags / count=0 actually lands. */
    if (!seen && len > 0U) return false;
    bool changed = (new_count != s_data.alert.count) ||
                   (memcmp(s_data.alert.flags, new_flags, sizeof(new_flags)) != 0);
    if (changed) {
        memcpy(s_data.alert.flags, new_flags, sizeof(new_flags));
        s_data.alert.count = new_count;
    }
    return changed;
}

size_t LpSettings_BuildAlertBody(uint8_t *buf, size_t bufsize)
{
    if (s_data.alert.count == 0U) return 0U;
    /* Slot is meaningful: emit ALL `count` entries (including 0s) so the
     * receiver gets the right array length. ClimacellTimes does the same. */
    size_t inner_len = 0;
    for (uint32_t i = 0; i < s_data.alert.count; i++) {
        uint8_t tmp[10];
        inner_len += encode_varint(tmp, s_data.alert.flags[i]);
    }
    if (1U + 5U + inner_len > bufsize) return 0U;
    size_t pos = 0;
    buf[pos++] = (uint8_t)((1U << 3) | 2U);
    pos += encode_varint(&buf[pos], (uint32_t)inner_len);
    for (uint32_t i = 0; i < s_data.alert.count; i++) {
        pos += encode_varint(&buf[pos], s_data.alert.flags[i]);
    }
    return pos;
}

/* ─── BayNameSettings (settings.proto field 35, envelope tag 69) ────
 * Repeated string on field 1, max 8 names × 24 chars. Wire-authoritative
 * (clear and refill). Unlike repeated uint32, repeated strings are
 * always encoded as separate length-delimited records (no packing). */

bool LpSettings_ApplyBayName(const uint8_t *payload, size_t len)
{
    bool seen = false;
    char     new_names[LP_BAYNAME_MAX][LP_BAYNAME_STRMAX];
    uint32_t new_count = 0;
    memset(new_names, 0, sizeof(new_names));

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        if (field == 1U && wire == 2U) {
            seen = true;
            if (new_count < LP_BAYNAME_MAX) {
                size_t cn = copy_string_field(payload + pos, len - pos,
                                              new_names[new_count],
                                              LP_BAYNAME_STRMAX);
                if (cn == 0U) break;
                pos += cn;
                new_count++;
            } else {
                /* Skip overflow names. */
                uint32_t slen;
                size_t sn = decode_varint(payload + pos, len - pos, &slen);
                if (sn == 0U || pos + sn + slen > len) break;
                pos += sn + slen;
            }
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) break;
            pos += sk;
        }
    }
    if (!seen && len > 0U) return false;
    bool changed = (new_count != s_data.bay_name.count) ||
                   (memcmp(s_data.bay_name.names, new_names, sizeof(new_names)) != 0);
    if (changed) {
        memcpy(s_data.bay_name.names, new_names, sizeof(new_names));
        s_data.bay_name.count = new_count;
    }
    return changed;
}

size_t LpSettings_BuildBayNameBody(uint8_t *buf, size_t bufsize)
{
    /* Emit one length-delimited record per name (including empties) so
     * the receiver gets the same array length back. Suppress whole-page
     * when count==0. */
    if (s_data.bay_name.count == 0U) return 0U;
    size_t pos = 0;
    for (uint32_t i = 0; i < s_data.bay_name.count; i++) {
        const char *s = s_data.bay_name.names[i];
        size_t slen = 0;
        while (slen < LP_BAYNAME_STRMAX && s[slen] != '\0') slen++;
        if (pos + 1U + 5U + slen > bufsize) return 0U;
        buf[pos++] = (uint8_t)((1U << 3) | 2U);
        pos += encode_varint(&buf[pos], (uint32_t)slen);
        if (slen > 0U) {
            memcpy(&buf[pos], s, slen);
            pos += slen;
        }
    }
    return pos;
}

/* ─── LoadMonitorSettings (settings.proto field 20, OSPI blob field 70) ──
 * NOTE: "field 70" here is the inner field number inside the LP's
 * own OSPI save blob (LpSettings_Serialize). It has nothing to do
 * with envelope tag 70 (LogRecord) on the UART wire — the blob is
 * never sent as an envelope. */

bool LpSettings_ApplyLoadMonitor(const uint8_t *payload, size_t len)
{
    bool changed = false;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                switch (field) {
                case  3: if (s_data.load_monitor.bay_count    != v) { s_data.load_monitor.bay_count    = v; changed = true; } break;
                case  4: if (s_data.load_monitor.enabled      != v) { s_data.load_monitor.enabled      = v; changed = true; } break;
                case  6: if (s_data.load_monitor.sensor_type  != v) { s_data.load_monitor.sensor_type  = v; changed = true; } break;
                case  8: if (s_data.load_monitor.pipe_count   != v) { s_data.load_monitor.pipe_count   = v; changed = true; } break;
                case  9: if (s_data.load_monitor.interval     != v) { s_data.load_monitor.interval     = v; changed = true; } break;
                case 10: if (s_data.load_monitor.auto_start   != v) { s_data.load_monitor.auto_start   = v; changed = true; } break;
                case 11: if (s_data.load_monitor.threshold    != v) { s_data.load_monitor.threshold    = v; changed = true; } break;
                case 12: if (s_data.load_monitor.mode         != v) { s_data.load_monitor.mode         = v; changed = true; } break;
                case 15: if (s_data.load_monitor.total_count  != v) { s_data.load_monitor.total_count  = v; changed = true; } break;
                default: break;
                }
            }
        } else if (wire == 5U) {
            float f;
            consumed = decode_float(payload + pos, len - pos, &f);
            if (consumed > 0U) {
                switch (field) {
                case 13: s_data.load_monitor.low_limit  = f; changed = true; break;
                case 14: s_data.load_monitor.high_limit = f; changed = true; break;
                default: break;
                }
            }
        } else if (wire == 2U) {
            char *dst = NULL;
            switch (field) {
            case 1: dst = s_data.load_monitor.bay1_label; break;
            case 2: dst = s_data.load_monitor.bay2_label; break;
            case 5: dst = s_data.load_monitor.units;      break;
            case 7: dst = s_data.load_monitor.pipe_label; break;
            default: break;
            }
            if (dst != NULL) {
                consumed = copy_string_field(payload + pos, len - pos,
                                             dst, LP_SET_STR_MAX);
                if (consumed > 0U) changed = true;
            } else {
                consumed = skip_field(wire, payload + pos, len - pos);
            }
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }
    return changed;
}

size_t LpSettings_BuildLoadMonitorBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    pos = emit_string(buf, bufsize, pos,  1, s_data.load_monitor.bay1_label);
    pos = emit_string(buf, bufsize, pos,  2, s_data.load_monitor.bay2_label);
    pos = emit_uint32(buf, bufsize, pos,  3, s_data.load_monitor.bay_count);
    pos = emit_uint32(buf, bufsize, pos,  4, s_data.load_monitor.enabled);
    pos = emit_string(buf, bufsize, pos,  5, s_data.load_monitor.units);
    pos = emit_uint32(buf, bufsize, pos,  6, s_data.load_monitor.sensor_type);
    pos = emit_string(buf, bufsize, pos,  7, s_data.load_monitor.pipe_label);
    pos = emit_uint32(buf, bufsize, pos,  8, s_data.load_monitor.pipe_count);
    pos = emit_uint32(buf, bufsize, pos,  9, s_data.load_monitor.interval);
    pos = emit_uint32(buf, bufsize, pos, 10, s_data.load_monitor.auto_start);
    pos = emit_uint32(buf, bufsize, pos, 11, s_data.load_monitor.threshold);
    pos = emit_uint32(buf, bufsize, pos, 12, s_data.load_monitor.mode);
    pos = emit_float (buf, bufsize, pos, 13, s_data.load_monitor.low_limit);
    pos = emit_float (buf, bufsize, pos, 14, s_data.load_monitor.high_limit);
    pos = emit_uint32(buf, bufsize, pos, 15, s_data.load_monitor.total_count);
    return pos;
}

/* ─── Runtime (OSPI blob field 71, SettingsUpdate field 18) ─────────
 * NOTE: "field 71" is internal to LpSettings_Serialize's OSPI save
 * blob, not envelope tag 71 (ActivityEvent). The blob never traverses
 * the UART envelope path.────
 * Wire-authoritative repeated submsg of RuntimeEntry on field 1. Mirrors
 * the Refrig pattern. NOTE: slot is force-encoded — slot 0 is a valid
 * schedule index, so we bypass emit_uint32's proto3 zero-suppress. */

static size_t apply_runtime_entry(LpRuntimeEntry *re,
                                  const uint8_t *payload, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) return 0U;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) return 0U;
            switch (field) {
            case 1: re->slot = v; break;
            case 2: re->mode = v; break;
            default: break;
            }
            pos += vn;
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) return 0U;
            pos += sk;
        }
    }
    return pos;
}

bool LpSettings_ApplyRuntime(const uint8_t *payload, size_t len)
{
    bool entries_seen = false;
    LpRuntimeEntry new_entries[LP_RUNTIME_MAX_ENTRIES];
    uint32_t       new_count = 0;
    memset(new_entries, 0, sizeof(new_entries));

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (wire == 2U && field == 1U) {
            uint32_t sublen;
            size_t ln = decode_varint(payload + pos, len - pos, &sublen);
            if (ln == 0U || ln + sublen > len - pos) break;
            entries_seen = true;
            if (new_count < LP_RUNTIME_MAX_ENTRIES) {
                LpRuntimeEntry re = { 0 };
                if (apply_runtime_entry(&re, payload + pos + ln, sublen) > 0U) {
                    new_entries[new_count++] = re;
                }
            }
            consumed = ln + sublen;
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }

    if (entries_seen) {
        memcpy(s_data.runtime.entries, new_entries, sizeof(new_entries));
        s_data.runtime.count = new_count;
        return true;
    }
    return false;
}

size_t LpSettings_BuildRuntimeBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    for (uint32_t i = 0; i < s_data.runtime.count; i++) {
        const LpRuntimeEntry *re = &s_data.runtime.entries[i];
        uint8_t inner[16];
        size_t  ilen = 0;
        /* slot is force-encoded (slot 0 valid). */
        if (ilen + 1U + 5U > sizeof(inner)) continue;
        inner[ilen++] = (uint8_t)((1U << 3) | 0U);
        ilen += encode_varint(&inner[ilen], re->slot);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 2, re->mode);
        if (ilen == 0U) continue;
        if (pos + 1U + 5U + ilen > bufsize) return 0U;
        buf[pos++] = (uint8_t)((1U << 3) | 2U);
        pos += encode_varint(&buf[pos], (uint32_t)ilen);
        memcpy(&buf[pos], inner, ilen);
        pos += ilen;
    }
    return pos;
}

/* ─── AuxProgram (envelope tag 72, SettingsUpdate field 21) ──────────
 * Splice-by-aux_index. SAVE direction: UI sends ONE channel; we mark
 * it populated and its rules array is wire-authoritative for that
 * channel. LOAD direction: emit every populated channel back-to-back
 * as length-delimited submsgs on the parent envelope. */

static size_t apply_aux_rule(LpAuxRule *r,
                             const uint8_t *payload, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) return 0U;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        size_t consumed = 0;
        if (wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                switch (field) {
                case 1: r->type      = v; break;
                case 2: r->io_index  = v; break;
                case 3: r->state     = v; break;
                case 4: r->op        = v; break;
                case 6: r->and_or    = v; break;
                case 7: r->ref_index = v; break;
                default: break;
                }
            }
        } else if (wire == 5U) {
            float f;
            consumed = decode_float(payload + pos, len - pos, &f);
            if (consumed > 0U && field == 5U) {
                r->sensor_val = f;
            }
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) return 0U;
        pos += consumed;
    }
    return pos;
}

/* Parse one AuxProgram submsg into `ap`. Returns true on success. */
static bool apply_aux_program_one(LpAuxProgram *ap,
                                  const uint8_t *payload, size_t len)
{
    LpAuxRule new_rules[LP_AUX_RULES_MAX];
    uint32_t  new_rules_count = 0;
    bool      rules_seen = false;
    memset(new_rules, 0, sizeof(new_rules));

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) return false;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        size_t consumed = 0;
        if (wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                switch (field) {
                case 1: ap->aux_index  = v; break;
                case 2: ap->eq_index   = v; break;
                case 3: ap->duty_cycle = v; break;
                case 4: ap->period     = v; break;
                case 5: ap->units      = v; break;
                default: break;
                }
            }
        } else if (wire == 2U && field == 6U) {
            uint32_t sublen;
            size_t ln = decode_varint(payload + pos, len - pos, &sublen);
            if (ln == 0U || ln + sublen > len - pos) return false;
            rules_seen = true;
            if (new_rules_count < LP_AUX_RULES_MAX) {
                LpAuxRule r = { 0 };
                if (apply_aux_rule(&r, payload + pos + ln, sublen) > 0U) {
                    new_rules[new_rules_count++] = r;
                }
            }
            consumed = ln + sublen;
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) return false;
        pos += consumed;
    }
    if (rules_seen) {
        memcpy(ap->rules, new_rules, sizeof(new_rules));
        ap->rules_count = new_rules_count;
    }
    return true;
}

bool LpSettings_ApplyAuxProgram(const uint8_t *payload, size_t len)
{
    /* Outer carries one or more AuxProgram submsgs at field 1.
     * Splice each into s_data.aux_program.channels[aux_index]. */
    bool changed = false;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        size_t consumed = 0;
        if (wire == 2U && field == 1U) {
            uint32_t sublen;
            size_t ln = decode_varint(payload + pos, len - pos, &sublen);
            if (ln == 0U || ln + sublen > len - pos) break;
            LpAuxProgram nw = { 0 };
            if (apply_aux_program_one(&nw, payload + pos + ln, sublen)) {
                if (nw.aux_index < LP_AUX_PROGRAM_MAX) {
                    nw.populated = 1;
                    s_data.aux_program.channels[nw.aux_index] = nw;
                    changed = true;
                }
            }
            consumed = ln + sublen;
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }
    return changed;
}

size_t LpSettings_BuildAuxProgramBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    for (uint32_t i = 0; i < LP_AUX_PROGRAM_MAX; i++) {
        const LpAuxProgram *ap = &s_data.aux_program.channels[i];
        if (!ap->populated) continue;
        uint8_t inner[256];
        size_t  ilen = 0;
        /* aux_index force-encoded (index 0 is valid). */
        if (ilen + 1U + 5U > sizeof(inner)) continue;
        inner[ilen++] = (uint8_t)((1U << 3) | 0U);
        ilen += encode_varint(&inner[ilen], ap->aux_index);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 2, ap->eq_index);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 3, ap->duty_cycle);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 4, ap->period);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 5, ap->units);
        /* Rules: nested repeated submsg on field 6. */
        for (uint32_t j = 0; j < ap->rules_count; j++) {
            const LpAuxRule *r = &ap->rules[j];
            uint8_t rinner[40];
            size_t  rilen = 0;
            rilen = emit_uint32(rinner, sizeof(rinner), rilen, 1, r->type);
            rilen = emit_uint32(rinner, sizeof(rinner), rilen, 2, r->io_index);
            rilen = emit_uint32(rinner, sizeof(rinner), rilen, 3, r->state);
            rilen = emit_uint32(rinner, sizeof(rinner), rilen, 4, r->op);
            rilen = emit_float (rinner, sizeof(rinner), rilen, 5, r->sensor_val);
            rilen = emit_uint32(rinner, sizeof(rinner), rilen, 6, r->and_or);
            rilen = emit_uint32(rinner, sizeof(rinner), rilen, 7, r->ref_index);
            if (rilen == 0U) continue;
            if (ilen + 1U + 5U + rilen > sizeof(inner)) break;
            inner[ilen++] = (uint8_t)((6U << 3) | 2U);
            ilen += encode_varint(&inner[ilen], (uint32_t)rilen);
            memcpy(&inner[ilen], rinner, rilen);
            ilen += rilen;
        }
        if (ilen == 0U) continue;
        if (pos + 1U + 5U + ilen > bufsize) return 0U;
        buf[pos++] = (uint8_t)((1U << 3) | 2U);
        pos += encode_varint(&buf[pos], (uint32_t)ilen);
        memcpy(&buf[pos], inner, ilen);
        pos += ilen;
    }
    return pos;
}

/* ─── AnalogBoard (envelope tag 73, SettingsUpdate field 29) ─────────
 * Splice-by-address. Each board has up to 4 SensorConfig submsgs on
 * field 4. */

static size_t apply_sensor_config(LpSensorConfig *sc,
                                  const uint8_t *payload, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) return 0U;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        size_t consumed = 0;
        if (wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                switch (field) {
                case 1: sc->slot     = v; break;
                case 4: sc->disabled = v; break;
                default: break;
                }
            }
        } else if (wire == 5U && field == 3U) {
            float f;
            consumed = decode_float(payload + pos, len - pos, &f);
            if (consumed > 0U) sc->offset = f;
        } else if (wire == 2U && field == 2U) {
            consumed = copy_string_field(payload + pos, len - pos,
                                         sc->label, LP_SET_STR_MAX);
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) return 0U;
        pos += consumed;
    }
    return pos;
}

static bool apply_analog_board_one(LpAnalogBoard *ab,
                                   const uint8_t *payload, size_t len)
{
    LpSensorConfig new_sensors[LP_SENSORS_PER_BOARD];
    uint32_t       new_sensors_count = 0;
    bool           sensors_seen = false;
    memset(new_sensors, 0, sizeof(new_sensors));

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) return false;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        size_t consumed = 0;
        if (wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                switch (field) {
                case 1: ab->address  = v; break;
                case 3: ab->disabled = v; break;
                default: break;
                }
            }
        } else if (wire == 2U && field == 2U) {
            consumed = copy_string_field(payload + pos, len - pos,
                                         ab->label, LP_SET_STR_MAX);
        } else if (wire == 2U && field == 4U) {
            uint32_t sublen;
            size_t ln = decode_varint(payload + pos, len - pos, &sublen);
            if (ln == 0U || ln + sublen > len - pos) return false;
            sensors_seen = true;
            if (new_sensors_count < LP_SENSORS_PER_BOARD) {
                LpSensorConfig sc = { 0 };
                if (apply_sensor_config(&sc, payload + pos + ln, sublen) > 0U) {
                    sc.populated = 1;
                    new_sensors[new_sensors_count++] = sc;
                }
            }
            consumed = ln + sublen;
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) return false;
        pos += consumed;
    }
    if (sensors_seen) {
        memcpy(ab->sensors, new_sensors, sizeof(new_sensors));
        ab->sensors_count = new_sensors_count;
    }
    return true;
}

bool LpSettings_ApplyAnalogBoard(const uint8_t *payload, size_t len)
{
    bool changed = false;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        size_t consumed = 0;
        if (wire == 2U && field == 1U) {
            uint32_t sublen;
            size_t ln = decode_varint(payload + pos, len - pos, &sublen);
            if (ln == 0U || ln + sublen > len - pos) break;
            LpAnalogBoard nw = { 0 };
            if (apply_analog_board_one(&nw, payload + pos + ln, sublen)) {
                if (nw.address < LP_ANALOG_BOARD_MAX) {
                    nw.populated = 1;
                    s_data.analog_board.boards[nw.address] = nw;
                    changed = true;
                }
            }
            consumed = ln + sublen;
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }
    return changed;
}

size_t LpSettings_BuildAnalogBoardBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    for (uint32_t i = 0; i < LP_ANALOG_BOARD_MAX; i++) {
        const LpAnalogBoard *ab = &s_data.analog_board.boards[i];
        if (!ab->populated) continue;
        uint8_t inner[256];
        size_t  ilen = 0;
        /* address force-encoded (address 0 valid). */
        if (ilen + 1U + 5U > sizeof(inner)) continue;
        inner[ilen++] = (uint8_t)((1U << 3) | 0U);
        ilen += encode_varint(&inner[ilen], ab->address);
        ilen = emit_string(inner, sizeof(inner), ilen, 2, ab->label);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 3, ab->disabled);
        for (uint32_t j = 0; j < ab->sensors_count; j++) {
            const LpSensorConfig *sc = &ab->sensors[j];
            uint8_t sinner[96];
            size_t  silen = 0;
            /* slot force-encoded (slot 0 valid). */
            if (silen + 1U + 5U > sizeof(sinner)) continue;
            sinner[silen++] = (uint8_t)((1U << 3) | 0U);
            silen += encode_varint(&sinner[silen], sc->slot);
            silen = emit_string(sinner, sizeof(sinner), silen, 2, sc->label);
            silen = emit_float (sinner, sizeof(sinner), silen, 3, sc->offset);
            silen = emit_uint32(sinner, sizeof(sinner), silen, 4, sc->disabled);
            if (silen == 0U) continue;
            if (ilen + 1U + 5U + silen > sizeof(inner)) break;
            inner[ilen++] = (uint8_t)((4U << 3) | 2U);
            ilen += encode_varint(&inner[ilen], (uint32_t)silen);
            memcpy(&inner[ilen], sinner, silen);
            ilen += silen;
        }
        if (ilen == 0U) continue;
        if (pos + 1U + 5U + ilen > bufsize) return 0U;
        buf[pos++] = (uint8_t)((1U << 3) | 2U);
        pos += encode_varint(&buf[pos], (uint32_t)ilen);
        memcpy(&buf[pos], inner, ilen);
        pos += ilen;
    }
    return pos;
}

/* ─── PwmChannel (envelope tag 74, SettingsUpdate field 30) ──────────
 * Outer carries `repeated PwmChannel channels = 1`. Splice-by-index. */

static bool apply_pwm_channel_one(LpPwmChannelEntry *pe,
                                  const uint8_t *payload, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) return false;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (wire == 0U) {
            uint32_t v;
            size_t vn = decode_varint(payload + pos, len - pos, &v);
            if (vn == 0U) return false;
            switch (field) {
            case 1: pe->index   = v; break;
            case 2: pe->enabled = v; break;
            case 3: pe->port    = v; break;
            case 4: pe->duty    = v; break;
            default: break;
            }
            pos += vn;
        } else {
            size_t sk = skip_field(wire, payload + pos, len - pos);
            if (sk == 0U) return false;
            pos += sk;
        }
    }
    return true;
}

bool LpSettings_ApplyPwmChannel(const uint8_t *payload, size_t len)
{
    bool changed = false;
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        size_t consumed = 0;
        if (wire == 2U && field == 1U) {
            uint32_t sublen;
            size_t ln = decode_varint(payload + pos, len - pos, &sublen);
            if (ln == 0U || ln + sublen > len - pos) break;
            LpPwmChannelEntry nw = { 0 };
            if (apply_pwm_channel_one(&nw, payload + pos + ln, sublen)) {
                if (nw.index < LP_PWM_CHANNEL_MAX) {
                    nw.populated = 1;
                    s_data.pwm_channel.channels[nw.index] = nw;
                    changed = true;
                }
            }
            consumed = ln + sublen;
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }
    return changed;
}

size_t LpSettings_BuildPwmChannelBody(uint8_t *buf, size_t bufsize)
{
    size_t pos = 0;
    for (uint32_t i = 0; i < LP_PWM_CHANNEL_MAX; i++) {
        const LpPwmChannelEntry *pe = &s_data.pwm_channel.channels[i];
        if (!pe->populated) continue;
        uint8_t inner[32];
        size_t  ilen = 0;
        /* index force-encoded (index 0 valid). */
        if (ilen + 1U + 5U > sizeof(inner)) continue;
        inner[ilen++] = (uint8_t)((1U << 3) | 0U);
        ilen += encode_varint(&inner[ilen], pe->index);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 2, pe->enabled);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 3, pe->port);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 4, pe->duty);
        if (ilen == 0U) continue;
        if (pos + 1U + 5U + ilen > bufsize) return 0U;
        buf[pos++] = (uint8_t)((1U << 3) | 2U);
        pos += encode_varint(&buf[pos], (uint32_t)ilen);
        memcpy(&buf[pos], inner, ilen);
        pos += ilen;
    }
    return pos;
}

/* ─── AccountSettings (settings.proto field 38, envelope tag 29) ─────
 * Wire-authoritative replace: any payload that mentions field 1
 * REPLACES the whole user list. password_defined (field 3) is sticky
 * across saves that omit it, since the UI typically toggles it
 * separately via the password change flow.
 *
 * Inner UserAccount layout: { slot=1 force varint, user_id=2 string }. */
static size_t apply_user_account(LpUserAccount *u,
                                 const uint8_t *payload, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) return 0U;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        size_t consumed = 0;
        if (wire == 0U && field == 1U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) u->slot = v;
        } else if (wire == 2U && field == 2U) {
            consumed = copy_string_field(payload + pos, len - pos,
                                         u->user_id, LP_ACCOUNT_STRMAX);
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) return 0U;
        pos += consumed;
    }
    return pos;
}

bool LpSettings_ApplyAccount(const uint8_t *payload, size_t len)
{
    bool          users_seen = false;
    LpUserAccount new_users[LP_ACCOUNT_MAX];
    uint32_t      new_count = 0;
    uint32_t      new_pwdef = s_data.account.password_defined;
    bool          pwdef_seen = false;
    memset(new_users, 0, sizeof(new_users));

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (wire == 2U && field == 1U) {
            uint32_t sublen;
            size_t ln = decode_varint(payload + pos, len - pos, &sublen);
            if (ln == 0U || ln + sublen > len - pos) break;
            users_seen = true;
            if (new_count < LP_ACCOUNT_MAX) {
                LpUserAccount u = { 0 };
                if (apply_user_account(&u, payload + pos + ln, sublen) > 0U) {
                    new_users[new_count++] = u;
                }
            }
            consumed = ln + sublen;
        } else if (wire == 0U && field == 2U) {
            /* user_count — derived; ignore wire value, use new_count. */
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
        } else if (wire == 0U && field == 3U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) { new_pwdef = v ? 1U : 0U; pwdef_seen = true; }
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }

    bool changed = false;
    /* Empty payload = wire-authoritative "wipe all accounts" (UI sent
     * an empty users[] with count=0 / password_defined=0, all of which
     * proto3 zero-suppresses to no bytes). */
    if (len == 0U) { users_seen = true; pwdef_seen = true; }
    if (users_seen) {
        if (new_count != s_data.account.count ||
            memcmp(new_users, s_data.account.users, sizeof(new_users)) != 0) {
            memcpy(s_data.account.users, new_users, sizeof(new_users));
            s_data.account.count = new_count;
            changed = true;
        }
    }
    if (pwdef_seen && new_pwdef != s_data.account.password_defined) {
        s_data.account.password_defined = new_pwdef;
        changed = true;
    }
    return changed;
}

size_t LpSettings_BuildAccountBody(uint8_t *buf, size_t bufsize)
{
    if (s_data.account.count == 0U && s_data.account.password_defined == 0U) {
        return 0U;
    }
    size_t pos = 0;
    /* Emit each user as length-delimited submsg on field 1. */
    for (uint32_t i = 0; i < s_data.account.count; i++) {
        const LpUserAccount *u = &s_data.account.users[i];
        uint8_t inner[1 + 5 + 1 + 5 + LP_ACCOUNT_STRMAX];
        size_t  ilen = 0;
        /* slot is force-encoded (slot 0 valid). */
        inner[ilen++] = (uint8_t)((1U << 3) | 0U);
        ilen += encode_varint(&inner[ilen], u->slot);
        ilen = emit_string(inner, sizeof(inner), ilen, 2, u->user_id);
        if (ilen == 0U) continue;
        if (pos + 1U + 5U + ilen > bufsize) return 0U;
        buf[pos++] = (uint8_t)((1U << 3) | 2U);
        pos += encode_varint(&buf[pos], (uint32_t)ilen);
        memcpy(&buf[pos], inner, ilen);
        pos += ilen;
    }
    /* user_count (field 2) — derived; emit only if non-zero. */
    pos = emit_uint32(buf, bufsize, pos, 2, s_data.account.count);
    /* password_defined (field 3, bool). */
    pos = emit_uint32(buf, bufsize, pos, 3, s_data.account.password_defined);
    return pos;
}

/* ─── IoConfig (settings.proto field 39, envelope tag 24) ────────────
 * Wire-authoritative half-replace: a payload that mentions field 1
 * replaces output_map; field 2 replaces input_map; field 3 sets
 * board_count. Saves typically include both arrays.
 *
 * Both halves accept either packed (wire=2) OR unpacked-repeated
 * (wire=0) varint, mirroring ClimacellTimes. ts-proto packed default
 * uses wire=2; legacy / hand-rolled UI may use wire=0.
 *
 * The Level 2 IO Config save (Phase 5.1 proto-direct) uses a sparse
 * indexed encoding for compactness — a payload listing all 60 EQ slots
 * blows past the 1500-byte settings envelope budget. Disambiguated by
 * wire type so it doesn't conflict with field 3 board_count (wire=0):
 *   field 3 wire=2  → indexed OUTPUT submsg { slot(1), value(2) }
 *   field 4 wire=2  → indexed INPUT  submsg { slot(1), value(2) }
 * Whenever any indexed sub-message is parsed we treat it as an
 * authoritative "wipe + repopulate" of the corresponding map (mirrors
 * legacy AS2 Platform/nova_dataexc.c::apply_io_config). */
static bool ioconf_collect_packed(const uint8_t *p, uint32_t plen,
                                  uint32_t *dst, uint32_t *dst_count)
{
    size_t pos = 0;
    while (pos < plen) {
        uint32_t v;
        size_t vn = decode_varint(p + pos, plen - pos, &v);
        if (vn == 0U) return false;
        pos += vn;
        if (*dst_count < LP_IO_MAP_MAX) {
            dst[(*dst_count)++] = v;
        }
    }
    return true;
}

/* Decode a single { slot=1, value=2 } indexed submsg and store
 * map[slot] = value. Updates *count to max(slot+1, *count) so the
 * emit pass below covers all populated slots. */
static bool ioconf_apply_indexed(const uint8_t *p, uint32_t plen,
                                 uint32_t *map, uint32_t *count)
{
    uint32_t slot = LP_IO_MAP_MAX; /* sentinel */
    uint32_t value = 0U;
    size_t pos = 0;
    while (pos < plen) {
        uint32_t tag;
        size_t tn = decode_varint(p + pos, plen - pos, &tag);
        if (tn == 0U) return false;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);
        if (field == 1U && wire == 0U) {
            size_t vn = decode_varint(p + pos, plen - pos, &slot);
            if (vn == 0U) return false;
            pos += vn;
        } else if (field == 2U && wire == 0U) {
            size_t vn = decode_varint(p + pos, plen - pos, &value);
            if (vn == 0U) return false;
            pos += vn;
        } else {
            size_t skip = skip_field(wire, p + pos, plen - pos);
            if (skip == 0U) return false;
            pos += skip;
        }
    }
    if (slot < LP_IO_MAP_MAX) {
        map[slot] = value;
        if (slot + 1U > *count) *count = slot + 1U;
    }
    return true;
}

bool LpSettings_ApplyIoConfig(const uint8_t *payload, size_t len)
{
    uint32_t new_out[LP_IO_MAP_MAX] = {0};
    uint32_t new_in [LP_IO_MAP_MAX] = {0};
    uint32_t new_out_count = 0;
    uint32_t new_in_count  = 0;
    uint32_t new_boards    = s_data.io_config.board_count;
    bool out_seen = false;
    bool in_seen  = false;
    bool boards_seen = false;
    /* The indexed format (field 3/4 wire=2) is wipe-and-repopulate,
     * but the indexed entries arrive one slot at a time. These flags
     * mark the buffer as "owned by indexed mode" so subsequent
     * indexed entries continue to write into the same fresh buffer
     * instead of trampling it. */
    bool out_indexed_started = false;
    bool in_indexed_started  = false;

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (field == 1U && wire == 2U) {
            uint32_t plen;
            size_t pn = decode_varint(payload + pos, len - pos, &plen);
            if (pn == 0U || pn + plen > len - pos) break;
            out_seen = true;
            if (!ioconf_collect_packed(payload + pos + pn, plen,
                                       new_out, &new_out_count)) break;
            consumed = pn + plen;
        } else if (field == 1U && wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                out_seen = true;
                if (new_out_count < LP_IO_MAP_MAX) {
                    new_out[new_out_count++] = v;
                }
            }
        } else if (field == 2U && wire == 2U) {
            uint32_t plen;
            size_t pn = decode_varint(payload + pos, len - pos, &plen);
            if (pn == 0U || pn + plen > len - pos) break;
            in_seen = true;
            if (!ioconf_collect_packed(payload + pos + pn, plen,
                                       new_in, &new_in_count)) break;
            consumed = pn + plen;
        } else if (field == 2U && wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                in_seen = true;
                if (new_in_count < LP_IO_MAP_MAX) {
                    new_in[new_in_count++] = v;
                }
            }
        } else if (field == 3U && wire == 0U) {
            uint32_t v;
            consumed = decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) { new_boards = v; boards_seen = true; }
        } else if (field == 3U && wire == 2U) {
            /* Indexed OUTPUT submsg { slot=1, value=2 } — wipe + populate. */
            uint32_t plen;
            size_t pn = decode_varint(payload + pos, len - pos, &plen);
            if (pn == 0U || pn + plen > len - pos) break;
            if (!out_indexed_started) {
                memset(new_out, 0, sizeof(new_out));
                new_out_count = 0;
                out_indexed_started = true;
                out_seen = true;
            }
            if (!ioconf_apply_indexed(payload + pos + pn, plen,
                                      new_out, &new_out_count)) break;
            consumed = pn + plen;
        } else if (field == 4U && wire == 2U) {
            /* Indexed INPUT submsg { slot=1, value=2 } — wipe + populate. */
            uint32_t plen;
            size_t pn = decode_varint(payload + pos, len - pos, &plen);
            if (pn == 0U || pn + plen > len - pos) break;
            if (!in_indexed_started) {
                memset(new_in, 0, sizeof(new_in));
                new_in_count = 0;
                in_indexed_started = true;
                in_seen = true;
            }
            if (!ioconf_apply_indexed(payload + pos + pn, plen,
                                      new_in, &new_in_count)) break;
            consumed = pn + plen;
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }

    bool changed = false;
    /* Empty payload = wire-authoritative "wipe IO maps" (UI cleared all
     * outputs/inputs; proto3 zero-suppresses each empty packed array
     * down to zero bytes). */
    if (len == 0U) { out_seen = true; in_seen = true; boards_seen = true; }
    if (out_seen) {
        if (new_out_count != s_data.io_config.output_count ||
            memcmp(new_out, s_data.io_config.output_map, sizeof(new_out)) != 0) {
            memcpy(s_data.io_config.output_map, new_out, sizeof(new_out));
            s_data.io_config.output_count = new_out_count;
            changed = true;
        }
    }
    if (in_seen) {
        if (new_in_count != s_data.io_config.input_count ||
            memcmp(new_in, s_data.io_config.input_map, sizeof(new_in)) != 0) {
            memcpy(s_data.io_config.input_map, new_in, sizeof(new_in));
            s_data.io_config.input_count = new_in_count;
            changed = true;
        }
    }
    if (boards_seen && new_boards != s_data.io_config.board_count) {
        s_data.io_config.board_count = new_boards;
        changed = true;
    }
    return changed;
}

size_t LpSettings_BuildIoConfigBody(uint8_t *buf, size_t bufsize)
{
    if (s_data.io_config.output_count == 0U &&
        s_data.io_config.input_count  == 0U &&
        s_data.io_config.board_count  == 0U) {
        return 0U;
    }
    size_t pos = 0;

    /* Emit packed varint arrays for output_map (field 1) and
     * input_map (field 2). Two-pass: compute body length, then write. */
    for (uint32_t pass = 0; pass < 2U; pass++) {
        uint32_t fnum   = (pass == 0U) ? 1U : 2U;
        const uint32_t *src = (pass == 0U) ? s_data.io_config.output_map
                                           : s_data.io_config.input_map;
        uint32_t cnt = (pass == 0U) ? s_data.io_config.output_count
                                    : s_data.io_config.input_count;
        if (cnt == 0U) continue;

        size_t inner_len = 0;
        for (uint32_t i = 0; i < cnt; i++) {
            uint8_t tmp[5];
            inner_len += encode_varint(tmp, src[i]);
        }
        if (pos + 1U + 5U + inner_len > bufsize) return 0U;
        buf[pos++] = (uint8_t)((fnum << 3) | 2U);
        pos += encode_varint(&buf[pos], (uint32_t)inner_len);
        for (uint32_t i = 0; i < cnt; i++) {
            pos += encode_varint(&buf[pos], src[i]);
        }
    }
    /* board_count (field 3). */
    pos = emit_uint32(buf, bufsize, pos, 3, s_data.io_config.board_count);
    return pos;
}

/* ─── OrbitRole table ─────────────────────────────────────────────────
 * See lp_settings.h LpOrbitRoleSet doc for wire layout. The whole
 * table lives in s_data.orbit_role; entries with `populated == 0` are
 * skipped on emit. The UI reads each board's role back via
 * OrbitBoardStatus.role (envelope tag 120, field 10), populated from
 * LpSettings_GetOrbitRole() in build_orbit_board. */

const LpOrbitRoleEntry *LpSettings_GetOrbitRole(uint32_t slot)
{
    if (slot >= LP_ORBIT_ROLE_MAX) return NULL;
    return &s_data.orbit_role.slots[slot];
}

bool LpSettings_SetOrbitRole(uint32_t slot,
                             uint32_t role,
                             uint32_t zone_id,
                             int32_t  legacy_slot,
                             uint32_t refrig_stage)
{
    if (slot >= LP_ORBIT_ROLE_MAX) return false;
    LpOrbitRoleEntry *e = &s_data.orbit_role.slots[slot];
    /* Clamp wide ints down to the storage widths. role/zone_id range
     * is enforced by the caller (main.c bridge_rx_callback) so a bad
     * value never reaches here. */
    uint8_t  new_role         = (uint8_t)(role & 0xFFU);
    uint8_t  new_zone         = (uint8_t)(zone_id & 0xFFU);
    int8_t   new_legacy       = (legacy_slot < INT8_MIN) ? INT8_MIN
                              : (legacy_slot > INT8_MAX) ? INT8_MAX
                              : (int8_t)legacy_slot;
    uint8_t  new_refrig_stage = (uint8_t)(refrig_stage & 0xFFU);

    bool changed = (e->populated == 0U)
                || e->role         != new_role
                || e->zone_id      != new_zone
                || e->legacy_slot  != new_legacy
                || e->refrig_stage != new_refrig_stage;

    e->role         = new_role;
    e->zone_id      = new_zone;
    e->legacy_slot  = new_legacy;
    e->refrig_stage = new_refrig_stage;
    e->populated    = 1U;
    return changed;
}

/* Encode-helpers: force-varint / signed varint variants kept local to
 * this section so the builders below stay readable.  emit_uint32_force
 * mirrors the pb_uint32_force pattern from the bridge protocol
 * invariants — slot 0 / role 0 (=UNASSIGNED) are valid values that
 * MUST appear on the wire. */
static size_t emit_uint32_force(uint8_t *buf, size_t bufsize, size_t pos,
                                uint32_t field, uint32_t v)
{
    if (pos + 5U + 5U > bufsize) return 0U;
    pos = emit_key(buf, bufsize, pos, field, 0U);
    if (pos == 0U) return 0U;
    pos += encode_varint(&buf[pos], v);
    return pos;
}

/* Signed int → zigzag varint (proto3 sint32). */
static size_t emit_sint32(uint8_t *buf, size_t bufsize, size_t pos,
                          uint32_t field, int32_t v)
{
    if (v == 0) return pos;   /* zero suppressed */
    uint32_t zz = (uint32_t)((v << 1) ^ (v >> 31));
    if (pos + 5U + 5U > bufsize) return 0U;
    pos = emit_key(buf, bufsize, pos, field, 0U);
    if (pos == 0U) return 0U;
    pos += encode_varint(&buf[pos], zz);
    return pos;
}

size_t LpSettings_BuildOrbitRoleBody(uint8_t *buf, size_t bufsize)
{
    /* Two-pass: emit nothing if no slot was ever assigned. */
    bool any = false;
    for (uint32_t i = 0; i < LP_ORBIT_ROLE_MAX; i++) {
        if (s_data.orbit_role.slots[i].populated) { any = true; break; }
    }
    if (!any) return 0U;

    size_t pos = 0;
    for (uint32_t i = 0; i < LP_ORBIT_ROLE_MAX; i++) {
        const LpOrbitRoleEntry *e = &s_data.orbit_role.slots[i];
        if (!e->populated) continue;

        /* Build inner submsg first into scratch. Worst case:
         *   slot   : 1+5 = 6
         *   role   : 1+5 = 6
         *   zone   : 1+5 = 6
         *   legacy : 1+5 = 6
         *   stage  : 1+5 = 6
         *   total  : 30 B
         */
        uint8_t inner[40];
        size_t  ilen = 0;
        ilen = emit_uint32_force(inner, sizeof(inner), ilen, 1, i);
        if (ilen == 0U) return 0U;
        ilen = emit_uint32_force(inner, sizeof(inner), ilen, 2, e->role);
        if (ilen == 0U) return 0U;
        ilen = emit_uint32(inner, sizeof(inner), ilen, 3, e->zone_id);
        ilen = emit_sint32(inner, sizeof(inner), ilen, 4, (int32_t)e->legacy_slot);
        ilen = emit_uint32(inner, sizeof(inner), ilen, 5, e->refrig_stage);

        /* Wrap as field 1 (repeated submsg) of the outer message. */
        if (pos + 1U + 5U + ilen > bufsize) return 0U;
        buf[pos++] = (uint8_t)((1U << 3) | 2U);
        pos += encode_varint(&buf[pos], (uint32_t)ilen);
        memcpy(&buf[pos], inner, ilen);
        pos += ilen;
    }
    return pos;
}

bool LpSettings_ApplyOrbitRoleBlob(const uint8_t *buf, size_t len)
{
    /* Wire-authoritative replace: clear all slots first, then re-apply
     * the entries present in the blob. Any slot not mentioned ends up
     * unpopulated (= UNASSIGNED), matching how a wiped IoConfig works. */
    memset(&s_data.orbit_role, 0, sizeof(s_data.orbit_role));

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(buf + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        if (field == 1U && wire == 2U) {
            uint32_t sublen;
            size_t ln = decode_varint(buf + pos, len - pos, &sublen);
            if (ln == 0U || ln + sublen > len - pos) break;
            pos += ln;

            /* Decode one entry. */
            const uint8_t *p = buf + pos;
            size_t ipos = 0;
            uint32_t slot = LP_ORBIT_ROLE_MAX;
            uint32_t role = 0, zone = 0, stage = 0;
            int32_t  legacy = -1;
            while (ipos < sublen) {
                uint32_t itag;
                size_t in = decode_varint(p + ipos, sublen - ipos, &itag);
                if (in == 0U) break;
                ipos += in;
                const uint32_t ifield = itag >> 3;
                const uint8_t  iwire  = (uint8_t)(itag & 0x07U);
                if (iwire == 0U) {
                    uint32_t v;
                    size_t vn = decode_varint(p + ipos, sublen - ipos, &v);
                    if (vn == 0U) break;
                    ipos += vn;
                    switch (ifield) {
                        case 1: slot  = v; break;
                        case 2: role  = v; break;
                        case 3: zone  = v; break;
                        case 4: { /* sint32 zigzag */
                            legacy = (int32_t)((v >> 1) ^ -(int32_t)(v & 1U));
                            break;
                        }
                        case 5: stage = v; break;
                        default: break;
                    }
                } else {
                    size_t sk = skip_field(iwire, p + ipos, sublen - ipos);
                    if (sk == 0U) break;
                    ipos += sk;
                }
            }
            pos += sublen;

            if (slot < LP_ORBIT_ROLE_MAX) {
                (void)LpSettings_SetOrbitRole(slot, role, zone, legacy, stage);
            }
            continue;
        }

        size_t sk = skip_field(wire, buf + pos, len - pos);
        if (sk == 0U) break;
        pos += sk;
    }
    return true;
}

/* ─── AoEquip (Level 2 PWM page) ─────────────────────────────────────
 *
 * Per-AO equipment program — operator picks which equipment a given
 * orbit AO drives via Level 2 → 4-20 mA Output Setup. The bridge
 * translates each operator pick into envelope tag 123 (AoEquipAssign);
 * main.c::bridge_rx_callback decodes {slot, channel, equip} and calls
 * LpSettings_SetAoEquip. Persisted to OSPI as save-blob field 80.
 * Read back to the UI via OrbitBoardStatus.ao_equip (orbit.proto field
 * 15) emitted by build_orbit_board.
 *
 * Wire layout for the save-blob field 80 body:
 *   field 1 (length-delimited submsg) = one entry per non-zero pick.
 *     Inside each entry:
 *       field 1 uint32 slot     (0..15)
 *       field 2 uint32 channel  (0..1)
 *       field 3 uint32 equip    (1..255)  — entries with equip==0
 *                                          are not emitted (default).
 *
 * Apply path is wire-authoritative: the table is wiped first, then
 * the entries in the blob are applied. Slots/channels not mentioned
 * end up at AO_EQUIP_UNUSED (= 0).
 */
bool LpSettings_SetAoEquip(uint32_t slot, uint32_t channel, uint32_t equip)
{
    if (slot    >= LP_AO_EQUIP_SLOT_MAX) return false;
    if (channel >= LP_AO_EQUIP_CH_MAX)   return false;
    /* Clamp to uint8 range — proto allows uint32 but storage is byte. */
    if (equip > 255U) equip = 0U;
    uint8_t *cell = &s_data.ao_equip.equip[slot][channel];
    if (*cell == (uint8_t)equip) return false;
    *cell = (uint8_t)equip;
    return true;
}

uint8_t LpSettings_GetAoEquip(uint32_t slot, uint32_t channel)
{
    if (slot    >= LP_AO_EQUIP_SLOT_MAX) return 0U;
    if (channel >= LP_AO_EQUIP_CH_MAX)   return 0U;
    return s_data.ao_equip.equip[slot][channel];
}

size_t LpSettings_BuildAoEquipBody(uint8_t *buf, size_t bufsize)
{
    /* Two-pass: skip emit when every cell is 0 (factory state). */
    bool any = false;
    for (uint32_t s = 0; s < LP_AO_EQUIP_SLOT_MAX && !any; s++) {
        for (uint32_t c = 0; c < LP_AO_EQUIP_CH_MAX && !any; c++) {
            if (s_data.ao_equip.equip[s][c] != 0U) any = true;
        }
    }
    if (!any) return 0U;

    size_t pos = 0;
    for (uint32_t s = 0; s < LP_AO_EQUIP_SLOT_MAX; s++) {
        for (uint32_t c = 0; c < LP_AO_EQUIP_CH_MAX; c++) {
            uint8_t e = s_data.ao_equip.equip[s][c];
            if (e == 0U) continue;
            /* Inner submsg: slot+channel MUST force-emit even when 0
             * (slot 0 / channel 0 are valid coordinates).  equip is
             * always > 0 here so emit_uint32 (zero-suppressed) is
             * fine for it. */
            uint8_t inner[16];
            size_t  ipos = 0;
            ipos = emit_uint32_force(inner, sizeof(inner), ipos, 1, s);
            ipos = emit_uint32_force(inner, sizeof(inner), ipos, 2, c);
            ipos = emit_uint32      (inner, sizeof(inner), ipos, 3, (uint32_t)e);
            /* Wrap as field 1 length-delimited (one entry per submsg). */
            pos = emit_key(buf, bufsize, pos, 1, 2);
            if (pos == 0U) return 0U;
            if (pos + 5U + ipos > bufsize) return 0U;
            pos += encode_varint(&buf[pos], (uint32_t)ipos);
            memcpy(&buf[pos], inner, ipos);
            pos += ipos;
        }
    }
    return pos;
}

bool LpSettings_ApplyAoEquipBlob(const uint8_t *buf, size_t len)
{
    /* Wire-authoritative replace. */
    memset(&s_data.ao_equip, 0, sizeof(s_data.ao_equip));

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(buf + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        if (field == 1U && wire == 2U) {
            uint32_t sublen;
            size_t ln = decode_varint(buf + pos, len - pos, &sublen);
            if (ln == 0U || ln + sublen > len - pos) break;
            pos += ln;

            const uint8_t *p = buf + pos;
            size_t ipos = 0;
            uint32_t slot = LP_AO_EQUIP_SLOT_MAX;
            uint32_t chan = LP_AO_EQUIP_CH_MAX;
            uint32_t equip = 0;
            while (ipos < sublen) {
                uint32_t itag;
                size_t in = decode_varint(p + ipos, sublen - ipos, &itag);
                if (in == 0U) break;
                ipos += in;
                const uint32_t ifield = itag >> 3;
                const uint8_t  iwire  = (uint8_t)(itag & 0x07U);
                if (iwire == 0U) {
                    uint32_t v;
                    size_t vn = decode_varint(p + ipos, sublen - ipos, &v);
                    if (vn == 0U) break;
                    ipos += vn;
                    switch (ifield) {
                        case 1: slot  = v; break;
                        case 2: chan  = v; break;
                        case 3: equip = v; break;
                        default: break;
                    }
                } else {
                    size_t sk = skip_field(iwire, p + ipos, sublen - ipos);
                    if (sk == 0U) break;
                    ipos += sk;
                }
            }
            pos += sublen;

            if (slot < LP_AO_EQUIP_SLOT_MAX
                && chan < LP_AO_EQUIP_CH_MAX) {
                (void)LpSettings_SetAoEquip(slot, chan, equip);
            }
            continue;
        }

        size_t sk = skip_field(wire, buf + pos, len - pos);
        if (sk == 0U) break;
        pos += sk;
    }
    return true;
}

/* ─── RemoteOff (Phase E1) ───────────────────────────────────────────
 *
 * Equipment manual-override table — see lp_settings.h LpRemoteOff.
 *
 * Wire layout for the save-blob field 79 body:
 *   field 1 (length-delimited bytes) = packed state[] array, one byte
 *     per slot, fixed length LP_IO_ENTRIES_MAX (sender authoritative).
 *
 * Zero-suppression note: bytes are emitted only if any slot is non-
 * zero. An all-zero table (factory state, every slot AUTO) skips emit
 * to keep small-blob saves small.
 *
 * No envelope-side standalone push — the bridge reads remote_off via
 * EquipmentStatus (envelope tag 11) which is built by main.c at a
 * 2 s cadence (see build_equipment_status_envelope). */
bool LpSettings_SetRemoteOff(uint32_t eq_index, uint8_t new_state)
{
    if (eq_index >= LP_IO_ENTRIES_MAX) return false;
    /* Clamp to the 4 valid RemoteOffState values to keep the blob
     * sane (proto enum is 0..3). */
    if (new_state > 3U) new_state = 0U;
    if (s_data.remote_off.state[eq_index] == new_state) return false;
    s_data.remote_off.state[eq_index] = new_state;
    return true;
}

uint8_t LpSettings_GetRemoteOff(uint32_t eq_index)
{
    if (eq_index >= LP_IO_ENTRIES_MAX) return 0U;
    return s_data.remote_off.state[eq_index];
}

/* Setter for the live plenum temperature setpoint — used by the
 * lp_engine_tick reverse-sync to capture in-tick mutations from
 * legacy SetRamp() (Platform/nova_states.c:919) back into the lp
 * store. Without this, mirror_basic_setpoints overwrites
 * Settings.Plenum.TempSet from lp on every tick and the ramp's
 * progress is lost.
 *
 * Persistence: RAM-only. The OSPI flusher (LpSettings_Save called
 * from main.c on save events) catches the updated value on its next
 * scheduled flush. No need to flush per-tick — every-second flushes
 * would thrash flash and shorten its life expectancy. */
void LpSettings_SetPlenumTempSetpoint(float v)
{
    s_data.plenum.temp_setpoint = v;
}

size_t LpSettings_BuildRemoteOffBody(uint8_t *buf, size_t bufsize)
{
    /* Emit nothing if the entire table is zero (saves bytes for the
     * common factory-fresh case). */
    bool any = false;
    for (uint32_t i = 0; i < LP_IO_ENTRIES_MAX; i++) {
        if (s_data.remote_off.state[i] != 0U) { any = true; break; }
    }
    if (!any) return 0U;

    /* field 1, wire type 2 (length-delimited bytes), payload = state[]. */
    if (bufsize < 1U + 5U + LP_IO_ENTRIES_MAX) return 0U;
    size_t pos = 0;
    buf[pos++] = (uint8_t)((1U << 3) | 2U);
    pos += encode_varint(&buf[pos], (uint32_t)LP_IO_ENTRIES_MAX);
    memcpy(&buf[pos], s_data.remote_off.state, LP_IO_ENTRIES_MAX);
    pos += LP_IO_ENTRIES_MAX;
    return pos;
}

bool LpSettings_ApplyRemoteOffBlob(const uint8_t *buf, size_t len)
{
    /* Wire-authoritative replace: clear first, then copy bytes from
     * field 1 if present. An all-zero / missing field-1 leaves the
     * table cleared, matching factory fresh. */
    memset(&s_data.remote_off, 0, sizeof(s_data.remote_off));

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(buf + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        if (field == 1U && wire == 2U) {
            uint32_t bytes_len;
            size_t ln = decode_varint(buf + pos, len - pos, &bytes_len);
            if (ln == 0U || ln + bytes_len > len - pos) break;
            pos += ln;
            const size_t cp = (bytes_len < LP_IO_ENTRIES_MAX)
                            ? bytes_len : LP_IO_ENTRIES_MAX;
            memcpy(s_data.remote_off.state, buf + pos, cp);
            pos += bytes_len;
            /* Clamp any out-of-range value loaded from a future schema. */
            for (uint32_t i = 0; i < LP_IO_ENTRIES_MAX; i++) {
                if (s_data.remote_off.state[i] > 3U) {
                    s_data.remote_off.state[i] = 0U;
                }
            }
            continue;
        }

        size_t sk = skip_field(wire, buf + pos, len - pos);
        if (sk == 0U) break;
        pos += sk;
    }
    return true;
}

/* ─── IoDefinition / IoNameUpdate ────────────────────────────────────── */
/*
 * Static factory-default table for the 58 EQUIPMENT_IO slots from AS2
 * (see SerialShift.h:51-109). Names stay empty — UI carries the i18n
 * defaults; this table only provides metadata (mode, io_type, renamable
 * flags) so the UI knows which entries the user is allowed to rename.
 *
 * AS2 source: docs/legacy_AS2_reference/Application/Settings.c#L430-L498
 * (`EquipDefine()` — verbatim Mode/IO/Renamable mapping). The
 * `populated` flag distinguishes "seeded from defaults" (1) from
 * "blank slot" (0) so build_io_definition_envelope only emits real
 * entries.
 *
 * SYSTEM_MODE encoding (mirrors agristar.SYSTEM_MODE proto enum
 * implicitly used by AS2):
 *   0 = MODE_NONE   1 = MODE_ALL   2 = MODE_POTATO   3 = MODE_ONION
 *
 * IO_OPTION encoding (mirrors io.proto IoEntry.io_type):
 *   0 = OUTPUT  1 = INPUT  2 = BOTH  3 = SWITCH  4 = NONE
 */
typedef struct {
    uint8_t mode;       /* SYSTEM_MODE */
    uint8_t io_type;    /* IO_OPTION */
    uint8_t renamable;  /* 0/1 */
} LpIoDefault;

#define M_NONE   0U
#define M_POTATO 1U
#define M_ONION  2U
#define M_BEE    3U
#define M_ALL    4U
/* SYSTEM_MODE values MUST stay in lock-step with:
 *   docs/legacy_AS2_reference/Application/SerialShift.h::SYSTEM_MODE
 *   constellation-ui/src/lib/business/equipmentMeta.ts::SystemModeEnum
 * They are written into the IoEntry.mode wire field (env tag 25) and
 * the UI uses entryAppliesToSystem() to filter dropdown options. */

#define IO_OUT    0U
#define IO_IN     1U
#define IO_BOTH   2U
#define IO_SWITCH 3U
#define IO_NONE   4U

/* IoEntry default table — index/order MUST match AS2 EQUIPMENT_IO enum
 * in docs/legacy_AS2_reference/Application/SerialShift.h:51-113. The UI
 * (Level 2 IO Config / Equipment Settings) maps each index to a stable
 * i18n key via constellation-ui/src/lib/business/equipmentLabels.ts.
 * Mode field uses the AS2 SYSTEM_MODE enum from Settings.h:
 *   MODE_NONE=0, MODE_POTATO=1, MODE_ONION=2, MODE_BEE=3, MODE_ALL=4 — see
 *   the M_* defines just above. Re-order with extreme care: settings blobs
 *   in OSPI persist user-renamed names by index, and io_definition_seed_
 *   defaults() refreshes (mode/io_type/renamable) from THIS table on every
 *   boot. Total = 59 (AS2 EQ_TOTAL_IO). */
static const LpIoDefault s_io_defaults[61] = {
    /*  0 EQ_FAN              */ { M_ALL,    IO_BOTH,   0 }, /* doubles as Green Light indicator */
    /*  1 EQ_DOORS            */ { M_NONE,   IO_NONE,   0 }, /* placeholder — GDC owns door I/O */
    /*  2 EQ_REFRIGERATION    */ { M_ALL,    IO_IN,     0 },
    /*  3 EQ_CLIMACELL        */ { M_POTATO, IO_BOTH,   0 },
    /*  4 EQ_HEAT             */ { M_POTATO, IO_BOTH,   0 },
    /*  5 EQ_CAVITY_HEAT      */ { M_ALL,    IO_BOTH,   0 },
    /*  6 EQ_BURNER           */ { M_ONION,  IO_BOTH,   0 },
    /*  7 EQ_HUMID_HEAD1      */ { M_POTATO, IO_BOTH,   0 },
    /*  8 EQ_HUMID_PUMP1      */ { M_POTATO, IO_OUT,    0 },
    /*  9 EQ_HUMID_HEAD2      */ { M_POTATO, IO_BOTH,   0 },
    /* 10 EQ_HUMID_PUMP2      */ { M_POTATO, IO_OUT,    0 },
    /* 11 EQ_HUMID_HEAD3      */ { M_POTATO, IO_BOTH,   0 },
    /* 12 EQ_HUMID_PUMP3      */ { M_POTATO, IO_OUT,    0 },
    /* 13 EQ_REFRIG_STAGE1    */ { M_ALL,    IO_BOTH,   1 },
    /* 14 EQ_REFRIG_STAGE2    */ { M_ALL,    IO_BOTH,   1 },
    /* 15 EQ_REFRIG_STAGE3    */ { M_ALL,    IO_BOTH,   1 },
    /* 16 EQ_REFRIG_STAGE4    */ { M_ALL,    IO_BOTH,   1 },
    /* 17 EQ_REFRIG_STAGE5    */ { M_ALL,    IO_BOTH,   1 },
    /* 18 EQ_REFRIG_STAGE6    */ { M_ALL,    IO_BOTH,   1 },
    /* 19 EQ_REFRIG_STAGE7    */ { M_ALL,    IO_BOTH,   1 },
    /* 20 EQ_REFRIG_STAGE8    */ { M_ALL,    IO_BOTH,   1 },
    /* 21 EQ_REFRIG_DEFROST1  */ { M_ALL,    IO_BOTH,   0 },
    /* 22 EQ_REFRIG_DEFROST2  */ { M_ALL,    IO_BOTH,   0 },
    /* 23 EQ_LIGHTS1          */ { M_ALL,    IO_BOTH,   1 }, /* renamable 2026-06-03 — operator names bay lights via IO Config since the dedicated L1 Bay Lights page was consolidated into Equipment Control */
    /* 24 EQ_LIGHTS2          */ { M_ALL,    IO_BOTH,   1 },
    /* 25 EQ_AUX1             */ { M_ALL,    IO_BOTH,   1 },
    /* 26 EQ_AUX2             */ { M_ALL,    IO_BOTH,   1 },
    /* 27 EQ_AUX3             */ { M_ALL,    IO_BOTH,   1 },
    /* 28 EQ_AUX4             */ { M_ALL,    IO_BOTH,   1 },
    /* 29 EQ_AUX5             */ { M_ALL,    IO_BOTH,   1 },
    /* 30 EQ_AUX6             */ { M_ALL,    IO_BOTH,   1 },
    /* 31 EQ_AUX7             */ { M_ALL,    IO_BOTH,   1 },
    /* 32 EQ_AUX8             */ { M_ALL,    IO_BOTH,   1 },
    /* AS2 had 33..42 marked M_NONE so the legacy UI hid them; the
     * Constellation IO Config page exposes everything assignable, so
     * these are now M_ALL. They remain renamable=0 because their roles
     * are wired into firmware logic (POWER detect, STANDBY interlocks,
     * indicator lights, pulse-door relay timing). */
    /* 33 EQ_POWER            */ { M_ALL,    IO_IN,     0 },
    /* 34 EQ_REMOTE_STANDBY   */ { M_ALL,    IO_IN,     0 },
    /* 35 EQ_REFRIG_STANDBY   */ { M_ALL,    IO_IN,     0 },
    /* 36 EQ_AIR_FLOW         */ { M_ALL,    IO_IN,     0 },
    /* 37 EQ_LOW_TEMP         */ { M_NONE,   IO_NONE,   0 }, /* deprecated: superseded by EQ_AUX_LOW_LIMIT (59); slot kept for wire-compat */
    /* 38 EQ_REDLIGHT         */ { M_ALL,    IO_OUT,    0 },
    /* 39 EQ_YELLOWLIGHT      */ { M_ALL,    IO_OUT,    0 },
    /* 40 EQ_PULSEDOOR_POWER  */ { M_ALL,    IO_OUT,    0 },
    /* 41 EQ_PULSEDOOR_OPEN   */ { M_ALL,    IO_OUT,    0 },
    /* 42 EQ_PULSEDOOR_CLOSE  */ { M_ALL,    IO_OUT,    0 },
    /* 43 SW_START_STOP       */ { M_ALL,    IO_SWITCH, 0 },
    /* 44 SW_FAN_AUTO         */ { M_ALL,    IO_SWITCH, 0 },
    /* 45 SW_FAN_MANUAL       */ { M_ALL,    IO_SWITCH, 0 },
    /* 46 SW_FRESHAIR_AUTO    */ { M_ALL,    IO_SWITCH, 0 },
    /* 47 SW_FRESHAIR_MANUAL  */ { M_ALL,    IO_SWITCH, 0 },
    /* 48 SW_CLIMACELL_AUTO   */ { M_POTATO, IO_SWITCH, 0 },
    /* 49 SW_CLIMACELL_MANUAL */ { M_POTATO, IO_SWITCH, 0 },
    /* 50 SW_HUMID_AUTO       */ { M_POTATO, IO_SWITCH, 0 },
    /* 51 SW_HUMID_MANUAL     */ { M_POTATO, IO_SWITCH, 0 },
    /* 52 SW_REFRIG_AUTO      */ { M_ALL,    IO_SWITCH, 0 },
    /* 53 SW_CURE_AUTO        */ { M_ONION,  IO_SWITCH, 0 },
    /* 54 SW_BURNER_AUTO      */ { M_ONION,  IO_SWITCH, 0 },
    /* 55 SW_AUX1_AUTO        */ { M_ALL,    IO_SWITCH, 1 },
    /* 56 SW_AUX1_MANUAL      */ { M_ALL,    IO_SWITCH, 1 },
    /* 57 SW_AUX2_AUTO        */ { M_ALL,    IO_SWITCH, 1 },
    /* 58 SW_AUX2_MANUAL      */ { M_ALL,    IO_SWITCH, 1 },
    /* Constellation-new equipment (no AS2 equivalent). Append-only —
     * never insert above index 58 because the SW_* slots are wire-
     * compatible with legacy and renumbering them would silently re-
     * map every saved switch assignment. */
    /* 59 EQ_AUX_LOW_LIMIT    */ { M_NONE,   IO_NONE,   0 }, /* hardware-only: wired to DI1 on the controller board, never operator-assignable (replaces deprecated EQ_LOW_TEMP) */
    /* 60 EQ_ESTOP            */ { M_NONE,   IO_NONE,   0 }, /* hardware-only: wired to DI11 on the controller board, never operator-assignable */
};

/* Seed io_definition.entries[] with the AS2 defaults. Idempotent —
 * only writes slots whose `populated` is 0, so user renames carried
 * forward via the save blob aren't clobbered on a hot reseed. Called
 * from LpSettings_DataInit() after the memset, and again opportunis-
 * tically from the build path so a deserialized blob from before this
 * field existed gets populated. */
static void io_definition_seed_defaults(void)
{
    const uint32_t n = (uint32_t)(sizeof(s_io_defaults) / sizeof(s_io_defaults[0]));
    uint32_t high = s_data.io_definition.count;
    for (uint32_t i = 0; i < n; i++) {
        LpIoEntry *e = &s_data.io_definition.entries[i];
        if (e->populated) {
            /* Refresh structural fields (mode/io_type/renamable) from
             * the source-of-truth table in case they changed across a
             * firmware build (e.g. SYSTEM_MODE constant alignment with
             * AS2 in 0.A.5). User-customized names are preserved
             * because we never touch e->name here. */
            e->mode      = s_io_defaults[i].mode;
            e->renamable = s_io_defaults[i].renamable;
            e->io_type   = s_io_defaults[i].io_type;
            if (i + 1U > high) high = i + 1U;
            continue;
        }
        e->index     = i;
        e->name[0]   = '\0';
        e->mode      = s_io_defaults[i].mode;
        e->io_pin    = 0U;                 /* pin lives in io_config */
        e->renamable = s_io_defaults[i].renamable;
        e->visible   = 1U;
        e->io_type   = s_io_defaults[i].io_type;
        e->populated = 1U;
        if (i + 1U > high) high = i + 1U;
    }
    s_data.io_definition.count = high;
}

bool LpSettings_ApplyIoName(const uint8_t *payload, size_t len)
{
    /* Decode IoNameUpdate { uint32 index = 1, string new_name = 2 }. */
    uint32_t index = 0;
    bool index_seen = false;
    char new_name[LP_SET_STR_MAX];
    new_name[0] = '\0';
    bool name_seen = false;

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0;
        if (field == 1U && wire == 0U) {
            consumed = decode_varint(payload + pos, len - pos, &index);
            if (consumed > 0U) index_seen = true;
        } else if (field == 2U && wire == 2U) {
            consumed = copy_string_field(payload + pos, len - pos,
                                         new_name, sizeof(new_name));
            if (consumed > 0U) name_seen = true;
        } else {
            consumed = skip_field(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }

    if (!index_seen) return false;
    if (index >= LP_IO_ENTRIES_MAX) return false;
    /* Lazily seed defaults so first-rename on a blob without
     * io_definition still validates renamability correctly. */
    if (s_data.io_definition.count == 0U) {
        io_definition_seed_defaults();
    }
    LpIoEntry *e = &s_data.io_definition.entries[index];
    /* Reject non-renamable entries (mirrors AS2 StoreIoName at
     * StorePostData.c:1219-1230 — only entries flagged renamable in the
     * defaults table accept rename writes). */
    if (!e->renamable) return false;

    /* Empty new_name = "revert to default" (UI clears the field). */
    const char *src = name_seen ? new_name : "";
    if (strncmp(e->name, src, sizeof(e->name)) == 0) return false;
    strncpy(e->name, src, sizeof(e->name) - 1U);
    e->name[sizeof(e->name) - 1U] = '\0';
    return true;
}

size_t LpSettings_BuildIoDefinitionBody(uint8_t *buf, size_t bufsize)
{
    /* Lazy-seed in case a blob predating io_definition was just
     * deserialized (count==0 on a fresh boot or imported old save). */
    if (s_data.io_definition.count == 0U) {
        io_definition_seed_defaults();
    }

    size_t pos = 0;
    /* Emit each populated entry as a length-delimited IoEntry sub-msg
     * on field 1 (IoDefinition.entries). Worst case per entry: tag(1)
     * + length(2) + index(2 force) + name(2+24) + mode(2) + io_pin(2)
     * + renamable(2) + visible(2) + io_type(2 force) ≈ 41 B. */
    static uint8_t inner[64];
    for (uint32_t i = 0; i < s_data.io_definition.count; i++) {
        const LpIoEntry *e = &s_data.io_definition.entries[i];
        if (!e->populated) continue;

        size_t ipos = 0;
        /* field 1 — index (force-encoded; 0 is valid). */
        if (ipos + 6U > sizeof(inner)) return 0U;
        inner[ipos++] = (uint8_t)((1U << 3) | 0U);
        ipos += encode_varint(&inner[ipos], e->index);
        /* field 2 — name (proto3-suppress when empty; UI uses i18n default). */
        ipos = emit_string(inner, sizeof(inner), ipos, 2U, e->name);
        if (ipos == 0U) return 0U;
        /* field 3 — mode (proto3-suppress when 0/MODE_NONE). */
        ipos = emit_uint32(inner, sizeof(inner), ipos, 3U, e->mode);
        if (ipos == 0U) return 0U;
        /* field 4 — io_pin (suppressed when 0; pin assignment is in
         * io_config, not here — kept as a forward-compat placeholder). */
        ipos = emit_uint32(inner, sizeof(inner), ipos, 4U, e->io_pin);
        if (ipos == 0U) return 0U;
        /* field 5 — renamable (bool, suppress when false). */
        if (e->renamable) {
            if (ipos + 2U > sizeof(inner)) return 0U;
            inner[ipos++] = (uint8_t)((5U << 3) | 0U);
            inner[ipos++] = 1U;
        }
        /* field 6 — visible (bool, suppress when false). */
        if (e->visible) {
            if (ipos + 2U > sizeof(inner)) return 0U;
            inner[ipos++] = (uint8_t)((6U << 3) | 0U);
            inner[ipos++] = 1U;
        }
        /* field 7 — io_type (force-encoded; 0=OUTPUT is meaningful). */
        if (ipos + 6U > sizeof(inner)) return 0U;
        inner[ipos++] = (uint8_t)((7U << 3) | 0U);
        ipos += encode_varint(&inner[ipos], e->io_type);

        /* Wrap as IoDefinition.entries[i]: field 1, wire 2. */
        if (pos + 1U + 5U + ipos > bufsize) return 0U;
        buf[pos++] = (uint8_t)((1U << 3) | 2U);
        pos += encode_varint(&buf[pos], (uint32_t)ipos);
        memcpy(&buf[pos], inner, ipos);
        pos += ipos;
    }
    return pos;
}

/* Restore the IoDefinition table from the save blob. The blob shape is
 * the same wire format LpSettings_BuildIoDefinitionBody emits: repeated
 * IoEntry on field 1. Used only by LpSettings_Deserialize (top-level
 * field 77). Re-seeds defaults afterwards so any newly-added slots
 * (firmware update with extra entries) get their metadata. */
bool LpSettings_ApplyIoDefinitionBlob(const uint8_t *buf, size_t len)
{
    /* Wipe before refill — saved blob is authoritative. */
    memset(&s_data.io_definition, 0, sizeof(s_data.io_definition));

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(buf + pos, len - pos, &tag);
        if (tn == 0U) return false;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        if (field == 1U && wire == 2U) {
            uint32_t sublen;
            size_t ln = decode_varint(buf + pos, len - pos, &sublen);
            if (ln == 0U || pos + ln + sublen > len) return false;
            pos += ln;
            const uint8_t *e_buf = buf + pos;
            const size_t   e_len = sublen;
            pos += sublen;

            /* Decode one IoEntry. */
            LpIoEntry e;
            memset(&e, 0, sizeof(e));
            size_t ep = 0;
            bool   index_seen = false;
            while (ep < e_len) {
                uint32_t etag;
                size_t etn = decode_varint(e_buf + ep, e_len - ep, &etag);
                if (etn == 0U) break;
                ep += etn;
                const uint32_t ef = etag >> 3;
                const uint8_t  ew = (uint8_t)(etag & 0x07U);
                size_t cn = 0;
                if (ef == 1U && ew == 0U) {
                    cn = decode_varint(e_buf + ep, e_len - ep, &e.index);
                    if (cn > 0U) index_seen = true;
                } else if (ef == 2U && ew == 2U) {
                    cn = copy_string_field(e_buf + ep, e_len - ep,
                                           e.name, sizeof(e.name));
                } else if (ef == 3U && ew == 0U) {
                    cn = decode_varint(e_buf + ep, e_len - ep, &e.mode);
                } else if (ef == 4U && ew == 0U) {
                    cn = decode_varint(e_buf + ep, e_len - ep, &e.io_pin);
                } else if (ef == 5U && ew == 0U) {
                    uint32_t v; cn = decode_varint(e_buf + ep, e_len - ep, &v);
                    if (cn > 0U) e.renamable = (v != 0U) ? 1U : 0U;
                } else if (ef == 6U && ew == 0U) {
                    uint32_t v; cn = decode_varint(e_buf + ep, e_len - ep, &v);
                    if (cn > 0U) e.visible = (v != 0U) ? 1U : 0U;
                } else if (ef == 7U && ew == 0U) {
                    cn = decode_varint(e_buf + ep, e_len - ep, &e.io_type);
                } else {
                    cn = skip_field(ew, e_buf + ep, e_len - ep);
                }
                if (cn == 0U) break;
                ep += cn;
            }

            if (index_seen && e.index < LP_IO_ENTRIES_MAX) {
                e.populated = 1U;
                s_data.io_definition.entries[e.index] = e;
                if (e.index + 1U > s_data.io_definition.count) {
                    s_data.io_definition.count = e.index + 1U;
                }
            }
        } else {
            size_t sk = skip_field(wire, buf + pos, len - pos);
            if (sk == 0U) return false;
            pos += sk;
        }
    }

    /* Re-seed any slot the saved blob didn't cover (e.g. firmware
     * upgrade added new equipment IDs). Existing slots stay intact. */
    io_definition_seed_defaults();
    return true;
}

/* ─── Save-blob serialization ────────────────────────────────────────── */
/* Layout: top-level message containing one length-delimited submessage
 * per page, keyed by the same field number as the matching envelope
 * tag (so a generic protoc decoder can dump the file). Each page is
 * emitted only when its body is non-empty (proto3 zero-suppress). */

/* Emit one length-delimited submessage at top-level field `field`.
 * Returns new pos (>= old pos) or 0 on overflow. body_len==0 is a
 * no-op (skips the page entirely). */
static size_t emit_submsg(uint8_t *buf, size_t bufsize, size_t pos,
                          uint32_t field, const uint8_t *body, size_t body_len)
{
    if (body_len == 0U) return pos;
    /* Tag = (field<<3)|2, encoded as varint. Worst case 5 bytes for
     * tag + 5 bytes for length + body. */
    if (pos + 5U + 5U + body_len > bufsize) return 0U;
    pos += encode_varint(&buf[pos], (field << 3) | 2U);
    pos += encode_varint(&buf[pos], (uint32_t)body_len);
    memcpy(&buf[pos], body, body_len);
    return pos + body_len;
}

size_t LpSettings_Serialize(uint8_t *buf, size_t bufsize)
{
    if (bufsize < 2U) return 0U;
    size_t pos = 0;
    uint8_t inner[256];
    size_t  ilen;

    ilen = LpSettings_BuildBasicSetupBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 20, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildPlenumBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 40, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildFanSpeedBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 41, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildFanBoostBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 42, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildRampRateBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 43, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    /* Refrig may exceed our 256-byte page-inner scratch when many
     * stages are populated (8 stages * 8 B + 13 flat fields ~= 130 B
     * worst case, well within budget — but bump the local buffer
     * just for this page to leave headroom). */
    {
        uint8_t refrig_inner[384];
        size_t  rilen = LpSettings_BuildRefrigBody(refrig_inner, sizeof(refrig_inner));
        pos = emit_submsg(buf, bufsize, pos, 44, refrig_inner, rilen);
        if (pos == 0U && rilen > 0U) return 0U;
    }

    ilen = LpSettings_BuildBurnerBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 45, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildCo2Body(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 46, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildCureBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 47, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildClimacellBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 48, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    /* ClimacellTimes can grow up to ~245 B (48 packed varints + tag).
     * Use the larger refrig-class scratch to avoid stomping `inner`. */
    {
        uint8_t cct_inner[384];
        size_t  cilen = LpSettings_BuildClimacellTimesBody(cct_inner, sizeof(cct_inner));
        pos = emit_submsg(buf, bufsize, pos, 49, cct_inner, cilen);
        if (pos == 0U && cilen > 0U) return 0U;
    }

    ilen = LpSettings_BuildHumidCtrlBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 50, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildOutsideAirBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 51, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildMiscBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 52, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildFailureBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 53, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildFailure2Body(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 54, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildTempAlarmBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 55, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildDoorBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 56, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildCureLimitBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 57, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildUserLogBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 58, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildPidBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 59, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildMasterSlaveBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 60, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildHttpPortBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 61, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildPublicAddressBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 62, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildSysModeBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 63, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildPidLogBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 64, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildServiceInfoBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 65, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    /* Email may be up to ~580 B (9 strings × 64). Use a larger scratch. */
    {
        uint8_t email_inner[640];
        size_t  eilen = LpSettings_BuildEmailBody(email_inner, sizeof(email_inner));
        pos = emit_submsg(buf, bufsize, pos, 66, email_inner, eilen);
        if (pos == 0U && eilen > 0U) return 0U;
    }

    ilen = LpSettings_BuildGraphFavoritesBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 67, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildAlertBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 68, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    ilen = LpSettings_BuildBayNameBody(inner, sizeof(inner));
    pos  = emit_submsg(buf, bufsize, pos, 69, inner, ilen);
    if (pos == 0U && ilen > 0U) return 0U;

    /* LoadMonitor: 4 strings × 64B + 9 varints + 2 floats ≈ 280 B. */
    {
        uint8_t lm_inner[384];
        size_t  lilen = LpSettings_BuildLoadMonitorBody(lm_inner, sizeof(lm_inner));
        pos = emit_submsg(buf, bufsize, pos, 70, lm_inner, lilen);
        if (pos == 0U && lilen > 0U) return 0U;
    }

    /* Runtime: up to 48 entries × ~6B = 288 B. */
    {
        uint8_t rt_inner[384];
        size_t  rilen = LpSettings_BuildRuntimeBody(rt_inner, sizeof(rt_inner));
        pos = emit_submsg(buf, bufsize, pos, 71, rt_inner, rilen);
        if (pos == 0U && rilen > 0U) return 0U;
    }

    /* AuxProgram: 8 channels × (~16B flat + 4 rules × ~30B) ≈ 1100 B. */
    {
        static uint8_t ap_inner[1280];
        size_t  ailen = LpSettings_BuildAuxProgramBody(ap_inner, sizeof(ap_inner));
        pos = emit_submsg(buf, bufsize, pos, 72, ap_inner, ailen);
        if (pos == 0U && ailen > 0U) return 0U;
    }

    /* AnalogBoard: 8 boards × (~70B flat + 4 sensors × ~80B) ≈ 3200 B. */
    {
        static uint8_t ab_inner[3584];
        size_t  bilen = LpSettings_BuildAnalogBoardBody(ab_inner, sizeof(ab_inner));
        pos = emit_submsg(buf, bufsize, pos, 73, ab_inner, bilen);
        if (pos == 0U && bilen > 0U) return 0U;
    }

    /* PwmChannel: 8 channels × ~16B = 128 B. */
    {
        uint8_t pc_inner[256];
        size_t  pilen = LpSettings_BuildPwmChannelBody(pc_inner, sizeof(pc_inner));
        pos = emit_submsg(buf, bufsize, pos, 74, pc_inner, pilen);
        if (pos == 0U && pilen > 0U) return 0U;
    }

    /* Account: 16 users × ~30B = 480 B. */
    {
        uint8_t ac_inner[768];
        size_t  ailen = LpSettings_BuildAccountBody(ac_inner, sizeof(ac_inner));
        pos = emit_submsg(buf, bufsize, pos, 75, ac_inner, ailen);
        if (pos == 0U && ailen > 0U) return 0U;
    }

    /* IoConfig: 192 outs + 192 ins × varint ≤5B + headers ≈ 2 KB. */
    {
        static uint8_t ic_inner[2560];
        size_t  iclen = LpSettings_BuildIoConfigBody(ic_inner, sizeof(ic_inner));
        pos = emit_submsg(buf, bufsize, pos, 76, ic_inner, iclen);
        if (pos == 0U && iclen > 0U) return 0U;
    }

    /* IoDefinition: 58 entries × ~41 B + headers ≈ 2.5 KB. Persisted on
     * top-level field 77 (IoConfig=76). Field 77 is internal to the
     * save blob — the on-wire envelope tag is 25 (separate emit). */
    {
        static uint8_t id_inner[3072];
        size_t  idlen = LpSettings_BuildIoDefinitionBody(id_inner, sizeof(id_inner));
        pos = emit_submsg(buf, bufsize, pos, 77, id_inner, idlen);
        if (pos == 0U && idlen > 0U) return 0U;
    }

    /* OrbitRole table — top-level field 78 (save-blob only, no
     * envelope-side push). Up to 16 slots × ~30 B inner ≈ 500 B. */
    {
        uint8_t or_inner[640];
        size_t  orlen = LpSettings_BuildOrbitRoleBody(or_inner, sizeof(or_inner));
        pos = emit_submsg(buf, bufsize, pos, 78, or_inner, orlen);
        if (pos == 0U && orlen > 0U) return 0U;
    }

    /* AoEquip table — top-level field 80 (save-blob only; rides back
     * out to the UI inside OrbitBoardStatus.ao_equip emitted from
     * main.c::build_orbit_board). 16 slots × 2 ch × ~6 B inner ≈ 200 B. */
    {
        uint8_t ae_inner[256];
        size_t  aelen = LpSettings_BuildAoEquipBody(ae_inner, sizeof(ae_inner));
        pos = emit_submsg(buf, bufsize, pos, 80, ae_inner, aelen);
        if (pos == 0U && aelen > 0U) return 0U;
    }

    /* RemoteOff table — top-level field 79 (save-blob only; on-wire
     * the data rides out inside EquipmentStatus tag 11 emitted from
     * main.c). 64-byte payload + 2 B header ≈ 70 B max. */
    {
        uint8_t ro_inner[80];
        size_t  rolen = LpSettings_BuildRemoteOffBody(ro_inner, sizeof(ro_inner));
        pos = emit_submsg(buf, bufsize, pos, 79, ro_inner, rolen);
        if (pos == 0U && rolen > 0U) return 0U;
    }

    return pos;
}

/* ─── Hardware-pinned IO slots ─────────────────────────────────────────
 * Two equipment indices map to fixed, soldered-down pins on every
 * Constellation controller board and must NOT be operator-assignable
 * or operator-removable:
 *
 *   EQ_AUX_LOW_LIMIT (59) → DI1  (port 1  = board 0 / pin 1)
 *   EQ_ESTOP         (60) → DI11 (port 11 = board 0 / pin 11)
 *
 * Both EQ entries are marked M_NONE/IO_NONE in s_io_defaults so the
 * Level 2 IO Config dropdowns hide them; this function plus the
 * InputCell.svelte "DI1/DI11 hardwired" lock keep the wiring
 * authoritative even if a corrupted save tried to repurpose the slot.
 *
 * EQ_LOW_TEMP (37) is deprecated — its slot is forced clear here so a
 * stale OSPI blob from before the rename cannot leave it pointing at
 * any pin. The slot index stays in the table for wire compatibility
 * with old saves, but the equipment is invisible to the UI.
 *
 * Called from LpSettings_DataInit (cold start) AND at the tail of
 * LpSettings_Deserialize (after replay) so a stale OSPI blob can't
 * clobber the hardware mapping. */
#define LP_PORT_AUX_LOW_LIMIT  1U   /* DI1 on MAIN board */
#define LP_PORT_ESTOP          11U  /* DI11 on MAIN board */
#define EQ_LOW_TEMP_IDX        37U  /* deprecated, force-cleared */
#define EQ_AUX_LOW_LIMIT_IDX   59U
#define EQ_ESTOP_IDX           60U

static void io_config_pin_hardware_inputs(void)
{
    if (s_data.io_config.input_count <= EQ_ESTOP_IDX) {
        s_data.io_config.input_count = EQ_ESTOP_IDX + 1U;
    }
    s_data.io_config.input_map[EQ_LOW_TEMP_IDX]      = 0U;
    s_data.io_config.input_map[EQ_AUX_LOW_LIMIT_IDX] = LP_PORT_AUX_LOW_LIMIT;
    s_data.io_config.input_map[EQ_ESTOP_IDX]         = LP_PORT_ESTOP;
}

/* Set IO Config to factory defaults (Level 2 IO Config "Set To Defaults"
 * button → bridge → SystemCmd cmd_type=8, AND cold boot via
 * LpSettings_DataInit). Mirrors the legacy AS2 EquipIoInit() seed map
 * (docs/legacy_AS2_reference/Application/Settings.c:515) on the MAIN
 * controller board, with two Constellation deltas:
 *
 *   1. EQ_REMOTE_STANDBY (34) is intentionally LEFT UNASSIGNED. AS2
 *      defaulted it to DI2 which collided with EQ_FAN's DI3 input
 *      anyway and is rarely wired in the field. Operator picks a port
 *      on the IO Config page if the option is wanted.
 *   2. EQ_LOW_TEMP (37) is gone — superseded by hardware-pinned
 *      EQ_AUX_LOW_LIMIT (59) on DI1; the AS2 DI10 mapping is dropped.
 *
 * POTATO-only equipment (ClimaCell, Humidifier 1 head/pump) is only
 * mapped when s_data.basic.system_mode == M_POTATO, matching legacy
 * AS2 logic. Hardware pins (DI1=AUX_LOW_LIMIT, DI11=ESTOP) are
 * re-applied at the end via io_config_pin_hardware_inputs().
 *
 * Operator equipment renames in io_definition.entries[] are preserved
 * (the legacy AS2 EquipIoInit() never touched names either).
 * Persistence to OSPI is the caller's responsibility. */
void LpSettings_ResetIoConfig(void)
{
    memset(s_data.io_config.output_map, 0,
           sizeof(s_data.io_config.output_map));
    memset(s_data.io_config.input_map, 0,
           sizeof(s_data.io_config.input_map));
    s_data.io_config.output_count = 0U;
    s_data.io_config.input_count  = 0U;

    /* === Outputs (DO ports on the MAIN controller board) === */
    /* LP-AM2434 CONTROLLER hardware exposes DO1..DO10 only (10 outputs);
     * earlier defaults pinned EQ_PULSEDOOR_OPEN to DO11 which doesn't
     * exist physically. Pulse-door open/close are now left unassigned
     * in factory defaults — operator picks valid ports if a pulse door
     * is wired (typically a pair on DO9+DO10). */
    s_data.io_config.output_map[38U] = 1U;   /* EQ_REDLIGHT       → DO1  */
    s_data.io_config.output_map[39U] = 2U;   /* EQ_YELLOWLIGHT    → DO2  */
    s_data.io_config.output_map[ 0U] = 3U;   /* EQ_FAN/Green Light → DO3  */

    /* === Inputs (DI ports on the MAIN controller board) === */
    s_data.io_config.input_map[ 0U] = 3U;    /* EQ_FAN feedback   → DI3  */
    s_data.io_config.input_map[33U] = 2U;    /* EQ_POWER          → DI2  */
    s_data.io_config.input_map[36U] = 9U;    /* EQ_AIR_FLOW       → DI9  */

    /* === POTATO-mode equipment === */
    if (s_data.basic.system_mode == M_POTATO) {
        s_data.io_config.output_map[3U] = 4U;  /* EQ_CLIMACELL    → DO4 */
        s_data.io_config.input_map [3U] = 4U;  /* EQ_CLIMACELL FB → DI4 */
        s_data.io_config.output_map[7U] = 5U;  /* EQ_HUMID_HEAD1  → DO5 */
        s_data.io_config.input_map [7U] = 5U;  /* EQ_HUMID_HEAD1  → DI5 */
        s_data.io_config.output_map[8U] = 6U;  /* EQ_HUMID_PUMP1  → DO6 */
    }

    /* Set counts to cover the highest assigned slot (the wire format
     * is length-authoritative so trailing zeros must be included).
     * Highest output slot is now EQ_YELLOWLIGHT (39); pulse door
     * defaults removed (see comment in outputs block above). */
    s_data.io_config.output_count = 40U;  /* up to EQ_YELLOWLIGHT+1 */
    s_data.io_config.input_count  = 37U;  /* up to EQ_AIR_FLOW+1    */

    /* Re-pin DI1 = AUX_LOW_LIMIT, DI11 = E-STOP, clear deprecated
     * EQ_LOW_TEMP slot. Bumps input_count to EQ_ESTOP_IDX + 1. */
    io_config_pin_hardware_inputs();
}

bool LpSettings_Deserialize(const uint8_t *buf, size_t len)
{
    /* Mirror image of LpSettings_Serialize: walk the top-level message
     * looking for known sub-msg fields and dispatch them. */
    LpSettings_DataInit();   /* clear before refilling */
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t tn = decode_varint(buf + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = tag >> 3;
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        if (wire == 2U) {
            uint32_t sublen;
            size_t ln = decode_varint(buf + pos, len - pos, &sublen);
            if (ln == 0U || ln + sublen > len - pos) break;
            pos += ln;
            switch (field) {
            case 20: (void)LpSettings_ApplyBasicSetup(buf + pos, sublen); break;
            case 40: (void)LpSettings_ApplyPlenum   (buf + pos, sublen); break;
            case 41: (void)LpSettings_ApplyFanSpeed (buf + pos, sublen); break;
            case 42: (void)LpSettings_ApplyFanBoost (buf + pos, sublen); break;
            case 43: (void)LpSettings_ApplyRampRate (buf + pos, sublen); break;
            case 44: (void)LpSettings_ApplyRefrig   (buf + pos, sublen); break;
            case 45: (void)LpSettings_ApplyBurner   (buf + pos, sublen); break;
            case 46: (void)LpSettings_ApplyCo2      (buf + pos, sublen); break;
            case 47: (void)LpSettings_ApplyCure          (buf + pos, sublen); break;
            case 48: (void)LpSettings_ApplyClimacell     (buf + pos, sublen); break;
            case 49: (void)LpSettings_ApplyClimacellTimes(buf + pos, sublen); break;
            case 50: (void)LpSettings_ApplyHumidCtrl     (buf + pos, sublen); break;
            case 51: (void)LpSettings_ApplyOutsideAir    (buf + pos, sublen); break;
            case 52: (void)LpSettings_ApplyMisc          (buf + pos, sublen); break;
            case 53: (void)LpSettings_ApplyFailure       (buf + pos, sublen); break;
            case 54: (void)LpSettings_ApplyFailure2      (buf + pos, sublen); break;
            case 55: (void)LpSettings_ApplyTempAlarm     (buf + pos, sublen); break;
            case 56: (void)LpSettings_ApplyDoor          (buf + pos, sublen); break;
            case 57: (void)LpSettings_ApplyCureLimit     (buf + pos, sublen); break;
            case 58: (void)LpSettings_ApplyUserLog       (buf + pos, sublen); break;
            case 59: (void)LpSettings_ApplyPid           (buf + pos, sublen); break;
            case 60: (void)LpSettings_ApplyMasterSlave   (buf + pos, sublen); break;
            case 61: (void)LpSettings_ApplyHttpPort      (buf + pos, sublen); break;
            case 62: (void)LpSettings_ApplyPublicAddress (buf + pos, sublen); break;
            case 63: (void)LpSettings_ApplySysMode       (buf + pos, sublen); break;
            case 64: (void)LpSettings_ApplyPidLog        (buf + pos, sublen); break;
            case 65: (void)LpSettings_ApplyServiceInfo   (buf + pos, sublen); break;
            case 66: (void)LpSettings_ApplyEmail          (buf + pos, sublen); break;
            case 67: (void)LpSettings_ApplyGraphFavorites (buf + pos, sublen); break;
            case 68: (void)LpSettings_ApplyAlert          (buf + pos, sublen); break;
            case 69: (void)LpSettings_ApplyBayName        (buf + pos, sublen); break;
            case 70: (void)LpSettings_ApplyLoadMonitor    (buf + pos, sublen); break;
            case 71: (void)LpSettings_ApplyRuntime        (buf + pos, sublen); break;
            case 72: (void)LpSettings_ApplyAuxProgram     (buf + pos, sublen); break;
            case 73: (void)LpSettings_ApplyAnalogBoard    (buf + pos, sublen); break;
            case 74: (void)LpSettings_ApplyPwmChannel     (buf + pos, sublen); break;
            case 75: (void)LpSettings_ApplyAccount        (buf + pos, sublen); break;
            case 76: (void)LpSettings_ApplyIoConfig       (buf + pos, sublen); break;
            case 77: (void)LpSettings_ApplyIoDefinitionBlob(buf + pos, sublen); break;
            case 78: (void)LpSettings_ApplyOrbitRoleBlob   (buf + pos, sublen); break;
            case 79: (void)LpSettings_ApplyRemoteOffBlob   (buf + pos, sublen); break;
            case 80: (void)LpSettings_ApplyAoEquipBlob     (buf + pos, sublen); break;
            default: break;
            }
            pos += sublen;
            continue;
        }

        size_t sk = skip_field(wire, buf + pos, len - pos);
        if (sk == 0U) break;
        pos += sk;
    }
    /* Re-seed io_definition structural fields after deserialize so a
     * blob saved with old SYSTEM_MODE constants gets refreshed. seed
     * is idempotent and preserves user-renamed names. */
    io_definition_seed_defaults();
    /* Hardware-pinned inputs override any saved mapping. */
    io_config_pin_hardware_inputs();
    return true;
}
