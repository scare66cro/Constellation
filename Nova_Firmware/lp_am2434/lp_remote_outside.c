/*
 * lp_remote_outside.c — implementation. See header for design notes.
 *
 * Decoder is hand-rolled (matches lp_settings.c style — firmware
 * does not link nanopb runtime). Field map mirrors
 * `proto/agristar/settings.proto :: RemoteOutsideAir`:
 *   1 sint32 outside_temp_x10
 *   2 sint32 outside_humid_x10
 *   3 uint32 ttl_secs
 *   4 string master_ip      — accepted and skipped (not cached today)
 */
#include "lp_remote_outside.h"
#include <string.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/DebugP.h>

/* Slave-mode broadcast watchdog: AS2's `MasterBroadcastFailChk` (now
 * in nova_failures.c) raises WARN_NOBROADCAST after 10 minutes
 * without a master push. Whenever a fresh push arrives we MUST reset
 * its timer and clear any latched alarm so the slave doesn't stay
 * stuck in the fault state. Same pattern as legacy `StoreSlaveUpdate`
 * (StorePostData.c::2952). */
#include "Settings.h"   /* Settings.MasterSlave + MSMODE_SLAVE_NOBROADCAST */
#include "Timer.h"      /* AlarmTimer[], SystemAlarm[], AL_NOBROADCAST,
                           XTimerVal */
#include "Warnings.h"   /* WarningsSet, WARN_NOBROADCAST,
                           WARN_SLAVENOBROADCAST */
#include "States.h"     /* FM_NONE */

/* Sentinel — bridge sends INT16_MIN when its source value is invalid. */
#define LP_RO_INVALID INT16_MIN

#define LP_RO_DEFAULT_TTL_SECS  30U
#define LP_RO_MAX_TTL_SECS     300U  /* clamp to 5 min */

typedef struct {
    int16_t  temp_x10;
    int16_t  humid_x10;
    uint32_t captured_sec;  /* boot-uptime seconds */
    uint32_t ttl_sec;
    bool     have_temp;
    bool     have_humid;
} LpRemoteOutsideCache;

static LpRemoteOutsideCache s_cache = {
    .temp_x10  = LP_RO_INVALID,
    .humid_x10 = LP_RO_INVALID,
    .captured_sec = 0U,
    .ttl_sec      = LP_RO_DEFAULT_TTL_SECS,
    .have_temp    = false,
    .have_humid   = false,
};

/* ---- minimal protobuf decode helpers (mirrors lp_settings.c) ---- */
static size_t ro_decode_varint(const uint8_t *buf, size_t len, uint64_t *out)
{
    uint64_t v = 0U;
    unsigned shift = 0U;
    size_t i = 0U;
    for (; i < len && i < 10U; i++) {
        v |= (uint64_t)(buf[i] & 0x7FU) << shift;
        if ((buf[i] & 0x80U) == 0U) {
            *out = v;
            return i + 1U;
        }
        shift += 7U;
    }
    return 0U;
}

static size_t ro_skip(uint8_t wire, const uint8_t *buf, size_t len)
{
    switch (wire) {
    case 0U: { uint64_t d; return ro_decode_varint(buf, len, &d); }
    case 1U: return (len >= 8U) ? 8U : 0U;
    case 2U: {
        uint64_t sublen;
        size_t n = ro_decode_varint(buf, len, &sublen);
        if (n == 0U || n + sublen > len) return 0U;
        return n + (size_t)sublen;
    }
    case 5U: return (len >= 4U) ? 4U : 0U;
    default: return 0U;
    }
}

static int32_t ro_zigzag32(uint32_t v)
{
    return (int32_t)((v >> 1) ^ (~(v & 1U) + 1U));
}

static uint32_t ro_now_sec(void)
{
    return (uint32_t)(ClockP_getTimeUsec() / 1000000ULL);
}

bool LpRemoteOutside_ApplyProto(const uint8_t *payload, size_t len)
{
    if (payload == NULL || len == 0U) {
        /* Empty body = explicit clear. */
        LpRemoteOutside_Clear();
        return true;
    }

    int16_t  new_temp  = LP_RO_INVALID;
    int16_t  new_humid = LP_RO_INVALID;
    uint32_t new_ttl   = 0U;
    bool     have_temp  = false;
    bool     have_humid = false;

    size_t pos = 0U;
    while (pos < len) {
        uint64_t tag;
        size_t tn = ro_decode_varint(payload + pos, len - pos, &tag);
        if (tn == 0U) break;
        pos += tn;
        const uint32_t field = (uint32_t)(tag >> 3);
        const uint8_t  wire  = (uint8_t)(tag & 0x07U);

        size_t consumed = 0U;
        if (wire == 0U && field == 1U) {
            /* outside_temp_x10 (sint32 → varint zigzag) */
            uint64_t v;
            consumed = ro_decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                int32_t s = ro_zigzag32((uint32_t)v);
                new_temp = (int16_t)s;
                have_temp = true;
            }
        } else if (wire == 0U && field == 2U) {
            /* outside_humid_x10 */
            uint64_t v;
            consumed = ro_decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                int32_t s = ro_zigzag32((uint32_t)v);
                new_humid = (int16_t)s;
                have_humid = true;
            }
        } else if (wire == 0U && field == 3U) {
            uint64_t v;
            consumed = ro_decode_varint(payload + pos, len - pos, &v);
            if (consumed > 0U) {
                new_ttl = (uint32_t)v;
            }
        } else {
            consumed = ro_skip(wire, payload + pos, len - pos);
        }
        if (consumed == 0U) break;
        pos += consumed;
    }

    if (new_ttl == 0U) {
        new_ttl = LP_RO_DEFAULT_TTL_SECS;
    } else if (new_ttl > LP_RO_MAX_TTL_SECS) {
        new_ttl = LP_RO_MAX_TTL_SECS;
    }

    s_cache.temp_x10     = new_temp;
    s_cache.humid_x10    = new_humid;
    s_cache.have_temp    = have_temp  && (new_temp  != LP_RO_INVALID);
    s_cache.have_humid   = have_humid && (new_humid != LP_RO_INVALID);
    s_cache.ttl_sec      = new_ttl;
    s_cache.captured_sec = ro_now_sec();

    DebugP_log("[RO] cached temp=%d/10C humid=%d/10%% ttl=%us\r\n",
               (int)s_cache.temp_x10, (int)s_cache.humid_x10,
               (unsigned)s_cache.ttl_sec);

    /* Reset the no-broadcast watchdog and clear any latched alarm.
     * Mirrors AS2's StoreSlaveUpdate (StorePostData.c). The mode
     * revert from NOBROADCAST→SLAVE makes the UI stop indicating
     * "using local sensor" the next time the engine tick mirrors
     * `lp->master_slave.mode` back over (mirror_master_slave skips
     * the clobber while NOBROADCAST is latched). */
    AlarmTimer[AL_NOBROADCAST] = XTimerVal;
    SystemAlarm[AL_NOBROADCAST] = FM_NONE;
    WarningsSet(WARN_NOBROADCAST,      FM_NONE, FM_NONE, 0);
    WarningsSet(WARN_SLAVENOBROADCAST, FM_NONE, FM_NONE, 0);
    if (Settings.MasterSlave == MSMODE_SLAVE_NOBROADCAST) {
        Settings.MasterSlave = MSMODE_SLAVE;
    }
    return true;
}

void LpRemoteOutside_Clear(void)
{
    s_cache.have_temp    = false;
    s_cache.have_humid   = false;
    s_cache.captured_sec = 0U;
    /* Restart the watchdog so a Clear() (e.g. mode change to Slave,
     * or an explicit empty body from the master) doesn't trip the
     * 10-minute alarm immediately. */
    AlarmTimer[AL_NOBROADCAST] = XTimerVal;
}

bool LpRemoteOutside_GetX10(int16_t *outTempX10, int16_t *outHumidX10)
{
    if (s_cache.captured_sec == 0U) return false;
    const uint32_t now = ro_now_sec();
    /* Tolerate 1 s of monotonic-clock skew (the boot-uptime clock
     * cannot actually go backwards, but defensive). */
    if (now < s_cache.captured_sec) return false;
    if (now - s_cache.captured_sec > s_cache.ttl_sec) return false;

    bool any = false;
    if (s_cache.have_temp && outTempX10 != NULL) {
        *outTempX10 = s_cache.temp_x10;
        any = true;
    }
    if (s_cache.have_humid && outHumidX10 != NULL) {
        *outHumidX10 = s_cache.humid_x10;
        any = true;
    }
    return any;
}

uint32_t LpRemoteOutside_LastUpdateSec(void) { return s_cache.captured_sec; }
uint32_t LpRemoteOutside_TtlSec(void)        { return s_cache.ttl_sec;      }
