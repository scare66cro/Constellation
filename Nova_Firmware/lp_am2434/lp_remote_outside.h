/*
 * lp_remote_outside.h — slave-side remote outside-air cache.
 *
 * The master panel pushes its own outside_temp / outside_humid to each
 * slave every 5 s via the bridge's `masterSlaveSync` (HTTP POST to
 * `/iot/master-slave/push`). The slave bridge wraps the JSON in a
 * `RemoteOutsideAir` protobuf and forwards it to the controller LP via
 * `SettingsUpdate.remote_outside` (settings.proto field 41). The
 * controller LP caches the values here in RAM (no OSPI) and applies
 * them as an override on the storage orbit's outside-air HRs whenever
 * the cache is fresh AND `MasterSlaveSettings.mode == 2 (Slave)`.
 *
 * Wire units:
 *   - temp_x10  : tenths-of-°C  (orbit native, signed; INT16_MIN = invalid)
 *   - humid_x10 : tenths-of-%RH (orbit native, signed; INT16_MIN = invalid)
 *
 * The cache stamps each update with `ClockP_getTimeUsec()/1e6` (boot
 * uptime seconds) and considers itself stale once `now - captured > ttl`.
 * AS2 had no freshness check at all — we add one because the new
 * confirmed-delivery scheme means a slave should "fail safe" back to
 * its own sensors if the master goes silent rather than freeze on the
 * last value forever.
 */
#ifndef LP_REMOTE_OUTSIDE_H_
#define LP_REMOTE_OUTSIDE_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Apply a `RemoteOutsideAir` protobuf body (length-delimited submessage
 * payload — caller has already stripped the outer SettingsUpdate tag).
 * Returns true if the cache changed (mostly informational; this path
 * never persists, so the caller does not save to OSPI). */
bool LpRemoteOutside_ApplyProto(const uint8_t *payload, size_t len);

/* Force-clear the cache (used when leaving Slave mode). */
void LpRemoteOutside_Clear(void);

/* Read the cached values if fresh. Returns true and fills *outTemp /
 * *outHumid (each may be left untouched if the corresponding field
 * was sent as INT16_MIN). Returns false if no fresh entry is held;
 * caller should then fall through to the local sensor. */
bool LpRemoteOutside_GetX10(int16_t *outTempX10, int16_t *outHumidX10);

/* Diagnostic accessors (for future status emit). */
uint32_t LpRemoteOutside_LastUpdateSec(void);  /* 0 if never */
uint32_t LpRemoteOutside_TtlSec(void);

#ifdef __cplusplus
}
#endif

#endif /* LP_REMOTE_OUTSIDE_H_ */
