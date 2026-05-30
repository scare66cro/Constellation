/*
 * lp_rtc.c — see lp_rtc.h for design notes.
 */

#include "lp_rtc.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <kernel/dpl/ClockP.h>

/* ─── Internal state ──────────────────────────────────────────────── */

static bool   s_have_baseline       = false;
static bool   s_authoritative_set   = false;
static time_t s_baseline_t          = 0;       /* time_t at last set */
static uint64_t s_baseline_us       = 0;       /* ClockP_getTimeUsec() at last set */

/* ─── Helpers ─────────────────────────────────────────────────────── */

/* Parse __DATE__ "Mmm dd yyyy" + __TIME__ "hh:mm:ss" into a time_t.
 * Used as cold-boot fallback before the bridge sets the clock. */
static time_t parse_build_time(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;
    const char *t = __TIME__;
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    int mon = 0;
    for (int i = 0; i < 12; i++) {
        if (d[0] == months[i*3] && d[1] == months[i*3+1] && d[2] == months[i*3+2]) {
            mon = i; break;
        }
    }
    tm.tm_mon  = mon;
    tm.tm_mday = (d[4] == ' ' ? 0 : (d[4] - '0') * 10) + (d[5] - '0');
    tm.tm_year = (d[7]-'0')*1000 + (d[8]-'0')*100 + (d[9]-'0')*10 + (d[10]-'0') - 1900;
    tm.tm_hour = (t[0]-'0')*10 + (t[1]-'0');
    tm.tm_min  = (t[3]-'0')*10 + (t[4]-'0');
    tm.tm_sec  = (t[6]-'0')*10 + (t[7]-'0');
    tm.tm_isdst = -1;
    return mktime(&tm);
}

/* Compute current time_t from baseline + monotonic delta. */
static time_t current_time(void)
{
    if (!s_have_baseline) {
        return (time_t)0;
    }
    uint64_t now_us = ClockP_getTimeUsec();
    /* Wrap-safe delta: ClockP_getTimeUsec() is 64-bit so no wrap concern. */
    uint64_t elapsed_s = (now_us - s_baseline_us) / 1000000ULL;
    return s_baseline_t + (time_t)elapsed_s;
}

/* Parse a 1- or 2-digit decimal number from `s`, advancing `*end` to
 * the next character. Returns -1 on error. */
static int parse_uint(const char *s, const char **end, int max_digits)
{
    int v = 0, n = 0;
    while (n < max_digits && s[n] >= '0' && s[n] <= '9') {
        v = v * 10 + (s[n] - '0');
        n++;
    }
    if (n == 0) return -1;
    if (end) *end = s + n;
    return v;
}

/* ─── Public API ──────────────────────────────────────────────────── */

void LpRtc_Init(void)
{
    if (s_have_baseline) return;
    time_t bt = parse_build_time();
    if (bt == (time_t)-1) return;   /* leave unset on parse failure */
    s_baseline_t  = bt;
    s_baseline_us = ClockP_getTimeUsec();
    s_have_baseline = true;
    /* s_authoritative_set stays false until bridge/operator sets the clock. */
}

bool LpRtc_SetFromStrings(const char *date_str, const char *time_str,
                          uint32_t am_pm)
{
    if (date_str == NULL || time_str == NULL) return false;

    /* Date: "MM/DD/YYYY" */
    const char *p = date_str;
    int mon = parse_uint(p, &p, 2);
    if (mon < 1 || mon > 12 || *p != '/') return false;
    p++;
    int day = parse_uint(p, &p, 2);
    if (day < 1 || day > 31 || *p != '/') return false;
    p++;
    int year = parse_uint(p, &p, 4);
    if (year < 1970 || year > 2099) return false;

    /* Time: "HH:MM:SS" (12-hour) */
    p = time_str;
    int hh = parse_uint(p, &p, 2);
    if (hh < 1 || hh > 12 || *p != ':') return false;
    p++;
    int mm = parse_uint(p, &p, 2);
    if (mm < 0 || mm > 59 || *p != ':') return false;
    p++;
    int ss = parse_uint(p, &p, 2);
    if (ss < 0 || ss > 59) return false;

    /* 12-hour → 24-hour. RTC.c convention: noon = "12:00:00 PM",
     * midnight = "12:00:00 AM". So PM and hh<12 → hh+=12; AM and hh==12 → hh=0. */
    if (am_pm) {
        if (hh < 12) hh += 12;
    } else {
        if (hh == 12) hh = 0;
    }

    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = year - 1900;
    tm.tm_mon  = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hh;
    tm.tm_min  = mm;
    tm.tm_sec  = ss;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    if (t == (time_t)-1) return false;

    s_baseline_t  = t;
    s_baseline_us = ClockP_getTimeUsec();
    s_have_baseline = true;
    s_authoritative_set = true;
    return true;
}

void LpRtc_GetStrings(char *date_out, char *time_out, uint32_t *am_pm_out)
{
    if (!s_have_baseline) {
        if (date_out) date_out[0] = '\0';
        if (time_out) time_out[0] = '\0';
        if (am_pm_out) *am_pm_out = 0;
        return;
    }

    time_t t = current_time();
    struct tm *tm = localtime(&t);
    if (tm == NULL) {
        if (date_out) date_out[0] = '\0';
        if (time_out) time_out[0] = '\0';
        if (am_pm_out) *am_pm_out = 0;
        return;
    }

    int hh24 = tm->tm_hour;
    int hh12 = hh24 % 12;
    if (hh12 == 0) hh12 = 12;
    uint32_t am_pm = (hh24 >= 12) ? 1U : 0U;

    if (date_out) {
        snprintf(date_out, 16, "%02d/%02d/%04d",
                 tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900);
    }
    if (time_out) {
        snprintf(time_out, 16, "%02d:%02d:%02d",
                 hh12, tm->tm_min, tm->tm_sec);
    }
    if (am_pm_out) *am_pm_out = am_pm;
}

bool LpRtc_IsAuthoritative(void)
{
    return s_authoritative_set;
}
