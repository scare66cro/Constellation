/*
 * lp_rtc.h — LP-AM2434 wall-clock for the DateTime envelope (proto tag 21).
 *
 * AM2434 has no battery-backed RTC. We synthesize one from a baseline
 * `time_t` + ClockP_getTimeUsec() deltas. Same approach as
 * Platform/hal_rtc.c, but standalone for the LP build (the legacy
 * Application/RTC.c layer is not pulled into the LP firmware).
 *
 * Sources of authoritative time, in priority order:
 *   1. Bridge auto-push of host wall-clock right after the bridge sees
 *      DataLoadStatus(ready=true) — arrives as SettingsUpdate field 26
 *      (DateTimeUpdate{date_str, time_str, am_pm}). Handled in
 *      bridge_rx_callback (main.c).
 *   2. Operator change via the Svelte Date/Time settings page — same
 *      wire path as #1.
 *
 * Until something authoritative sets it, `LpRtc_GetStrings` returns the
 * firmware build time (`__DATE__` / `__TIME__`) so the DateTime envelope
 * is sensible from cold boot. `LpRtc_IsAuthoritative()` reports whether
 * the bridge/operator has set the clock yet — the UI Date page can use
 * this later to flag an unsynced clock (mirrors legacy WARN_DATETIME).
 */

#ifndef LP_AM2434_LP_RTC_H
#define LP_AM2434_LP_RTC_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize from compile-time __DATE__ / __TIME__. Idempotent. */
void LpRtc_Init(void);

/* Set the wall-clock baseline from the wire format used by
 * DateTimeUpdate (settings field 26):
 *   date_str: "MM/DD/YYYY" (NUL-terminated)
 *   time_str: "HH:MM:SS"   (NUL-terminated, 12-hour)
 *   am_pm   : 0=AM, 1=PM   (legacy convention: 12 AM = midnight,
 *                           12 PM = noon — see /memories/repo/nova-rtc.md)
 *
 * Returns true on success, false on parse error (clock unchanged).
 */
bool LpRtc_SetFromStrings(const char *date_str, const char *time_str,
                          uint32_t am_pm);

/* Format the current wall-clock into the same wire format. Buffers must
 * be at least 16 bytes (date_out: "MM/DD/YYYY\0" = 11, time_out:
 * "HH:MM:SS\0" = 9; 16 leaves slack for safety). */
void LpRtc_GetStrings(char *date_out, char *time_out, uint32_t *am_pm_out);

/* True once LpRtc_SetFromStrings has been called successfully. */
bool LpRtc_IsAuthoritative(void);

#endif /* LP_AM2434_LP_RTC_H */
