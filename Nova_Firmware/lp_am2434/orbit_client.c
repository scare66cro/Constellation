/*
 * orbit_client.c — Modbus TCP poller for Constellation Orbit boards
 *
 * One worker task per orbit. Each task:
 *   1. opens (and re-opens on failure) a TCP connection to the orbit
 *   2. issues three FC03 (Read Holding Registers) reads:
 *      - HR[200..263] sensor block  — every poll
 *      - HR[0..3]     analog outputs — every poll
 *      - HR[40000..40006] identity   — once per 10 polls (low churn)
 *   3. updates the shared cache under s_lock
 *   4. sleeps until next interval
 *
 * Connection lifecycle: keep-alive — the socket stays open between
 * polls. On any error (recv timeout, send fail, malformed reply) the
 * socket is closed and re-opened on the next iteration. This matches
 * the production-bridge pattern (`server/src/orbitModbus.ts`).
 *
 * Why one task per orbit (not one shared poller iterating all 5):
 *   - a hung orbit (cable yanked, sim crashed) should not stall the
 *     others; the poll-loop pacing is independent
 *   - lwIP socket calls block; serializing 5 of them adds up to
 *     5 × connect-timeout (= 25 s) worst-case for the round-trip
 *   - per-task stack stays small (~3 KB) so 5 tasks cost ~15 KB total
 */
#include "orbit_client.h"
#include "lp_settings.h"
#include "orbit_server/orbit_role.h"

#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/errno.h"

#include <kernel/dpl/DebugP.h>

/* ──────────────────────────────────────────────────────────────────── */
/*  Internal state                                                      */
/* ──────────────────────────────────────────────────────────────────── */

#define ORBIT_TASK_STACK_WORDS   (3U * 1024U)   /* words; SDK convention */
#define ORBIT_TASK_PRIORITY      6U             /* below lwIP TCPIP (8) */
#define ORBIT_RECV_TIMEOUT_S     3
#define ORBIT_DEFAULT_INTERVAL_MS 1000U
#define ORBIT_IDENT_PERIOD_POLLS 10U

typedef struct OrbitWorker {
    OrbitConfig cfg;
    OrbitSample sample;            /* cache, guarded by s_lock */
    StackType_t stack[ORBIT_TASK_STACK_WORDS];
    StaticTask_t taskMem;
    TaskHandle_t task;
    int sock;                      /* -1 when closed */
    uint16_t txnId;                /* monotonic per-orbit transaction id */
    uint32_t pollSerial;           /* increments every poll attempt */
} OrbitWorker;

static OrbitWorker s_workers[ORBIT_CLIENT_MAX_ORBITS];
static size_t      s_workerCount = 0;
static SemaphoreHandle_t s_lock = NULL;
static StaticSemaphore_t s_lockMem;

/* ──────────────────────────────────────────────────────────────────── */
/*  Modbus TCP helpers                                                  */
/* ──────────────────────────────────────────────────────────────────── */

static void mbap_build_read_hr(uint8_t out[12], uint16_t txn, uint8_t unit,
                               uint16_t startAddr, uint16_t qty)
{
    out[0]  = (uint8_t)(txn >> 8);
    out[1]  = (uint8_t)(txn & 0xFF);
    out[2]  = 0x00; out[3] = 0x00;             /* protocol id */
    out[4]  = 0x00; out[5] = 0x06;             /* PDU length */
    out[6]  = unit;
    out[7]  = 0x03;                            /* FC03 */
    out[8]  = (uint8_t)(startAddr >> 8);
    out[9]  = (uint8_t)(startAddr & 0xFF);
    out[10] = (uint8_t)(qty >> 8);
    out[11] = (uint8_t)(qty & 0xFF);
}

/* Read up to `qty` consecutive holding registers from `unit` starting
 * at `startAddr`. Stores results big-endian-decoded into `out[0..qty-1]`.
 * Returns 0 on success, negative on error. */
static int orbit_read_hr(OrbitWorker *w, uint8_t unit, uint16_t startAddr,
                         uint16_t qty, uint16_t *out)
{
    if (w->sock < 0) {
        return -1;
    }
    if (qty == 0U || qty > 125U) {
        /* Modbus FC03 max-quantity is 125 (250 bytes) per spec. */
        return -2;
    }

    uint8_t  req[12];
    uint16_t txn = ++w->txnId;
    mbap_build_read_hr(req, txn, unit, startAddr, qty);

    if (lwip_send(w->sock, req, sizeof(req), 0) != (int)sizeof(req)) {
        return -3;
    }

    /* Reply: 7 B MBAP + 1 B FC + 1 B byte-count + qty*2 B data = 9 + qty*2.
     * Pre-Phase-4b code had `expected = 8 + qty*2` and `resp[8+250]` —
     * off-by-one that worked for sensor (64-reg, 137 B fits in 258) and
     * AO/ident (small) reads but stranded the 259th byte of any 125-reg
     * read in the kernel socket buffer, corrupting the next read on the
     * same socket. Hit Phase 4b's TRITON 256-reg role window (chunked
     * 125+125+6 → 256-reg over-sized variant) and again the 143-reg
     * variant which chunks to 125+18: the first chunk's stranded byte
     * broke the second chunk's MBAP echo check. Fix: size `resp[]` to
     * the actual max-FC03 response (9 + 250 = 259) and account for the
     * FC byte in `expected`. */
    const int expected = 9 + (int)qty * 2;
    uint8_t resp[9 + 250];
    int got = 0;
    while (got < expected) {
        int n = lwip_recv(w->sock, resp + got, sizeof(resp) - (size_t)got, 0);
        if (n <= 0) {
            return -4;
        }
        got += n;
    }

    /* Validate MBAP echo. */
    if (resp[0] != req[0] || resp[1] != req[1]) {
        return -5;  /* txn mismatch */
    }
    if (resp[6] != unit) {
        return -6;
    }
    if (resp[7] != 0x03) {
        /* Exception (FC | 0x80) or unexpected FC. */
        return -7;
    }
    if (resp[8] != qty * 2) {
        return -8;
    }
    for (uint16_t i = 0; i < qty; i++) {
        out[i] = ((uint16_t)resp[9 + i * 2] << 8) | resp[10 + i * 2];
    }
    return 0;
}

/* Read `qty` consecutive holding registers using one or more FC03
 * PDUs. Splits at the Modbus 125-reg-per-PDU limit transparently and
 * stitches the result back into `out[0..qty-1]`. Returns 0 only when
 * EVERY sub-read succeeded; first failure returns its rc and the rest
 * of `out` is left untouched.
 *
 * Used by Phase 4b's expanded sensor + role HR windows where a single
 * orbit poll covers >125 regs (e.g. TRITON role window 400..655 =
 * 256 regs → 3 sub-reads of 125 + 125 + 6). */
static int orbit_read_hr_window(OrbitWorker *w, uint8_t unit,
                                uint16_t startAddr, uint16_t qty,
                                uint16_t *out)
{
    if (qty == 0U) return -2;
    uint16_t remaining = qty;
    uint16_t offset    = 0U;
    while (remaining > 0U) {
        uint16_t chunk = remaining > 125U ? 125U : remaining;
        int rc = orbit_read_hr(w, unit,
                               (uint16_t)(startAddr + offset),
                               chunk, &out[offset]);
        if (rc != 0) return rc;
        offset    += chunk;
        remaining -= chunk;
    }
    return 0;
}

/* Read `qty` discrete bits from `unit` starting at `startAddr` using
 * Modbus function code `fc` (0x01 read-coils or 0x02 read-discrete-
 * inputs). Returns the LSB-packed bitmap in `*out_bits` (bit i = the
 * i-th address). Caller guarantees `qty <= 16`.
 *
 * Reused by orbit_task to pull the remote DO (FC01) and DI+E-stop+DC24V
 * monitor (FC02) bitmaps each cycle. */
static int orbit_read_bits(OrbitWorker *w, uint8_t unit, uint8_t fc,
                           uint16_t startAddr, uint16_t qty,
                           uint16_t *out_bits)
{
    if (w->sock < 0) return -1;
    if (qty == 0U || qty > 16U) return -2;
    if (fc != 0x01 && fc != 0x02) return -2;

    uint8_t  req[12];
    uint16_t txn = ++w->txnId;
    /* Same MBAP layout as FC03 but FC and "qty of bits" instead of regs. */
    req[0]  = (uint8_t)(txn >> 8);
    req[1]  = (uint8_t)(txn & 0xFF);
    req[2]  = 0x00; req[3] = 0x00;
    req[4]  = 0x00; req[5] = 0x06;
    req[6]  = unit;
    req[7]  = fc;
    req[8]  = (uint8_t)(startAddr >> 8);
    req[9]  = (uint8_t)(startAddr & 0xFF);
    req[10] = (uint8_t)(qty >> 8);
    req[11] = (uint8_t)(qty & 0xFF);

    if (lwip_send(w->sock, req, sizeof(req), 0) != (int)sizeof(req)) {
        return -3;
    }

    /* Reply: 7 B MBAP + 1 B byte-count + ceil(qty/8) data. */
    const uint8_t bcount = (uint8_t)((qty + 7U) / 8U);
    const int expected = 8 + (int)bcount;
    uint8_t resp[8 + 4];   /* qty<=16 → bcount<=2, plenty of room */
    int got = 0;
    while (got < expected) {
        int n = lwip_recv(w->sock, resp + got, sizeof(resp) - (size_t)got, 0);
        if (n <= 0) return -4;
        got += n;
    }

    if (resp[0] != req[0] || resp[1] != req[1]) return -5;
    if (resp[6] != unit) return -6;
    if (resp[7] != fc)   return -7;   /* exception or unexpected FC */
    if (resp[8] != bcount) return -8;

    uint16_t bits = 0;
    for (uint8_t i = 0; i < bcount; i++) {
        bits |= (uint16_t)resp[9 + i] << (i * 8);
    }
    /* Mask off any padding bits past qty so callers can compare bitmaps. */
    if (qty < 16U) {
        bits &= (uint16_t)((1U << qty) - 1U);
    }
    *out_bits = bits;
    return 0;
}

/* (Re)connect to the orbit. Returns 0 on success. Caller must close
 * `w->sock` first if it was already open. */
static int orbit_connect(OrbitWorker *w)
{
    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        DebugP_log("[ORBIT %u] socket() failed errno=%d\r\n",
                   (unsigned)w->cfg.index, errno);
        return -1;
    }

    struct timeval tv = { .tv_sec = ORBIT_RECV_TIMEOUT_S, .tv_usec = 0 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int one = 1;
    lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = lwip_htons(w->cfg.port);
    dst.sin_addr.s_addr = ipaddr_addr(w->cfg.ipv4);

    if (lwip_connect(sock, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        int e = errno;
        lwip_close(sock);
        /* errno=103 (ECONNABORTED) shows up when ARP/L2 fails. Logging
         * at every retry would spam UART once a single orbit drops. */
        DebugP_log("[ORBIT %u] connect %s:%u failed errno=%d\r\n",
                   (unsigned)w->cfg.index, w->cfg.ipv4,
                   (unsigned)w->cfg.port, e);
        return -1;
    }

    DebugP_log("[ORBIT %u] connected to %s:%u\r\n",
               (unsigned)w->cfg.index, w->cfg.ipv4, (unsigned)w->cfg.port);
    w->sock = sock;
    return 0;
}

static void orbit_disconnect(OrbitWorker *w)
{
    if (w->sock >= 0) {
        lwip_close(w->sock);
        w->sock = -1;
    }
}

/* ──────────────────────────────────────────────────────────────────── */
/*  Worker task                                                         */
/* ──────────────────────────────────────────────────────────────────── */

static void orbit_publish(OrbitWorker *w, const uint16_t *sensorHr,
                          const uint16_t *aoHr, const uint16_t *ident,
                          bool gotIdent)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (sensorHr) {
        memcpy(w->sample.sensorHr, sensorHr, sizeof(w->sample.sensorHr));
    }
    if (aoHr) {
        memcpy(w->sample.aoHr, aoHr, sizeof(w->sample.aoHr));
    }
    if (gotIdent && ident) {
        memcpy(w->sample.ident, ident, sizeof(w->sample.ident));
    }
    w->sample.lastUpdateTickMs = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    w->sample.pollCount++;
    w->sample.online = true;
    xSemaphoreGive(s_lock);
}

/* Update the cached DI/DO bitmaps after a successful FC01+FC02 cycle.
 * NEVER called with stale/failed data — caller is responsible for only
 * invoking on a clean read. Stamps `lastIoOkTickMs` so a future age-out
 * (UINT32_MAX sentinel pattern, see proto-orbit-iostatus.md) can mark
 * old data absent. */
static void orbit_publish_io(OrbitWorker *w, uint16_t do_bitmap,
                             uint16_t di_bitmap)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    w->sample.do_bitmap = do_bitmap;
    w->sample.di_bitmap = di_bitmap;
    w->sample.io_valid = true;
    w->sample.lastIoOkTickMs =
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    xSemaphoreGive(s_lock);
}

static void orbit_mark_offline(OrbitWorker *w)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    w->sample.errorCount++;
    w->sample.online = false;
    xSemaphoreGive(s_lock);
}

/* Resolve the per-role HR window for an orbit slot. Writes 0/0 when
 * the role doesn't need a secondary window (STORAGE / UNASSIGNED /
 * unpopulated). Reads from LpSettings which is updated by the
 * controller's OrbitRoleAssign envelope handler. */
static void orbit_resolve_role_window(uint8_t slot,
                                      uint16_t *out_base,
                                      uint16_t *out_count)
{
    *out_base  = 0U;
    *out_count = 0U;
    const LpOrbitRoleEntry *e = LpSettings_GetOrbitRole(slot);
    if (e == NULL || !e->populated) return;
    switch ((OrbitRole)e->role) {
        case ORBIT_ROLE_GDC:
            *out_base  = ORBIT_ROLE_HR_GDC_BASE;
            *out_count = ORBIT_ROLE_HR_GDC_COUNT;
            return;
        case ORBIT_ROLE_TRITON:
            *out_base  = ORBIT_ROLE_HR_TRITON_BASE;
            *out_count = ORBIT_ROLE_HR_TRITON_COUNT;
            return;
        default:
            return;
    }
}

/* Publish the role HR window. Caller passes `count=0` (no role
 * window for this orbit) or `count>0` with a freshly-read buffer.
 * Cache zero-clear is done only on role *change* (base differs)
 * so a transient role-read failure keeps last-known values, mirroring
 * the sensor-block keep-last-on-error policy.
 *
 * Phase 4b 2026-06-01. */
static void orbit_publish_role(OrbitWorker *w,
                               uint16_t base, uint16_t count,
                               const uint16_t *vals, bool readOk)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (count == 0U) {
        /* Role no longer needs a secondary window (e.g. operator
         * re-assigned a TRITON slot to STORAGE). Drop the cache so
         * the bridge doesn't keep emitting stale bank envelopes. */
        if (w->sample.roleHrCount != 0U) {
            w->sample.roleHrBase  = 0U;
            w->sample.roleHrCount = 0U;
            w->sample.roleHrValid = false;
        }
    } else {
        /* Role-change wipe: if the operator switched roles, last
         * cycle's role buffer is now garbage. */
        if (w->sample.roleHrBase != base) {
            w->sample.roleHrBase  = base;
            w->sample.roleHrCount = count;
            w->sample.roleHrValid = false;
            memset(w->sample.roleHr, 0, sizeof(w->sample.roleHr));
        } else {
            /* Bank size may grow if we ever bump the constants.
             * Keep count in sync. */
            w->sample.roleHrCount = count;
        }
        if (readOk && vals != NULL) {
            memcpy(w->sample.roleHr, vals,
                   (size_t)count * sizeof(uint16_t));
            w->sample.roleHrValid = true;
        }
    }
    xSemaphoreGive(s_lock);
}

static void orbit_task(void *arg)
{
    OrbitWorker *w = (OrbitWorker *)arg;
    /* Stagger startup so 5 orbits don't all SYN at the same instant. */
    vTaskDelay(pdMS_TO_TICKS(200U + 150U * w->cfg.index));

    for (;;) {
        if (w->sock < 0) {
            if (orbit_connect(w) != 0) {
                orbit_mark_offline(w);
                vTaskDelay(pdMS_TO_TICKS(2000));  /* back off on conn-fail */
                continue;
            }
        }

        uint16_t sensorHr[ORBIT_SENSOR_HR_COUNT];
        uint16_t aoHr    [ORBIT_AO_HR_COUNT];
        uint16_t ident   [ORBIT_IDENT_HR_COUNT];
        const uint8_t unit = (uint8_t)(w->cfg.index + 1);

        /* Sensor block: now 128 regs (Phase 4b growth), split into
         * 2× FC03 sub-reads by orbit_read_hr_window. */
        int rc = orbit_read_hr_window(w, unit, ORBIT_SENSOR_HR_BASE,
                                      ORBIT_SENSOR_HR_COUNT, sensorHr);
        if (rc != 0) {
            DebugP_log("[ORBIT %u] sensor read failed rc=%d\r\n",
                       (unsigned)w->cfg.index, rc);
            orbit_mark_offline(w);
            orbit_disconnect(w);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        rc = orbit_read_hr(w, unit, ORBIT_AO_HR_BASE,
                           ORBIT_AO_HR_COUNT, aoHr);
        if (rc != 0) {
            DebugP_log("[ORBIT %u] AO read failed rc=%d\r\n",
                       (unsigned)w->cfg.index, rc);
            orbit_mark_offline(w);
            orbit_disconnect(w);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* Role secondary HR window (Phase 4b 2026-06-01).
         * GDC: 96 regs @ 300 (1× FC03). TRITON: 256 regs @ 400
         * (3× FC03 split internally by orbit_read_hr_window).
         * STORAGE / UNASSIGNED → roleCount=0, skipped.
         *
         * Treat as soft-fail like DI/DO above: a transient role-read
         * failure must NOT drop the sensor cadence or disconnect.
         * Keep-last-on-error policy via orbit_publish_role(readOk=false). */
        uint16_t roleBase = 0U, roleCount = 0U;
        orbit_resolve_role_window(w->cfg.index, &roleBase, &roleCount);
        if (roleCount > 0U) {
            uint16_t roleHr[ORBIT_ROLE_HR_MAX];
            int rc_role = orbit_read_hr_window(w, unit, roleBase,
                                               roleCount, roleHr);
            if (rc_role == 0) {
                orbit_publish_role(w, roleBase, roleCount, roleHr, true);
            } else {
                DebugP_log("[ORBIT %u] role HR window @%u..%u soft-fail "
                           "rc=%d (keeping last-known)\r\n",
                           (unsigned)w->cfg.index,
                           (unsigned)roleBase,
                           (unsigned)(roleBase + roleCount - 1U),
                           rc_role);
                orbit_publish_role(w, roleBase, roleCount, NULL, false);
            }
        } else {
            /* No role window today — drops the cache if the operator
             * re-assigned a TRITON/GDC orbit back to STORAGE. */
            orbit_publish_role(w, 0U, 0U, NULL, false);
        }

        /* Discrete I/O: 2 extra PDUs per cycle (FC01 coils 0..9 = DO,
         * FC02 discrete inputs 0..14 = DI/E-stop/DC24V monitors).
         * On failure we DO NOT zero the cached bitmap (would be
         * indistinguishable from "all bits cleared") and we DO NOT
         * disconnect — the sensor block already proved the link is
         * up, and a transient bit-FC failure shouldn't drop the
         * sensor poll cadence. The cache holds last-known values
         * until the next successful read; `io_valid` stays true once
         * set so the encoder keeps emitting. */
        uint16_t do_bits = 0;
        uint16_t di_bits = 0;
        int rc_do = orbit_read_bits(w, unit, 0x01,
                                    ORBIT_DO_COIL_BASE,
                                    ORBIT_DO_COIL_COUNT, &do_bits);
        int rc_di = orbit_read_bits(w, unit, 0x02,
                                    ORBIT_DI_DISC_BASE,
                                    ORBIT_DI_DISC_COUNT, &di_bits);
        if (rc_do == 0 && rc_di == 0) {
            orbit_publish_io(w, do_bits, di_bits);
        } else {
            DebugP_log("[ORBIT %u] DI/DO read soft-fail "
                       "rc_do=%d rc_di=%d (keeping last-known)\r\n",
                       (unsigned)w->cfg.index, rc_do, rc_di);
        }

        bool gotIdent = false;
        if ((w->pollSerial++ % ORBIT_IDENT_PERIOD_POLLS) == 0U) {
            rc = orbit_read_hr(w, unit, ORBIT_IDENT_HR_BASE,
                               ORBIT_IDENT_HR_COUNT, ident);
            if (rc == 0) {
                gotIdent = true;
            } else {
                /* Identity read can fail without losing the socket — sim
                 * may not have populated 40000-range yet. Don't disconnect. */
                DebugP_log("[ORBIT %u] ident read soft-fail rc=%d\r\n",
                           (unsigned)w->cfg.index, rc);
            }
        }

        orbit_publish(w, sensorHr, aoHr, gotIdent ? ident : NULL, gotIdent);

        /* Periodic visibility — once every 10 polls so the log isn't
         * a firehose. Same cadence as the identity refresh. */
        if ((w->sample.pollCount % ORBIT_IDENT_PERIOD_POLLS) == 0U) {
            DebugP_log("[ORBIT %u] poll #%u OK: HR[200]=%u HR[201]=%u "
                       "HR[202]=%u HR[203]=%u id=%u uptime=%u\r\n",
                       (unsigned)w->cfg.index,
                       (unsigned)w->sample.pollCount,
                       (unsigned)sensorHr[0], (unsigned)sensorHr[1],
                       (unsigned)sensorHr[2], (unsigned)sensorHr[3],
                       (unsigned)w->sample.ident[0],
                       (unsigned)w->sample.ident[5]);
        }

        vTaskDelay(pdMS_TO_TICKS(w->cfg.pollIntervalMs));
    }
}

/* ──────────────────────────────────────────────────────────────────── */
/*  Public API                                                          */
/* ──────────────────────────────────────────────────────────────────── */

int OrbitClient_Init(const OrbitConfig *configs, size_t count)
{
    if (count == 0U || count > ORBIT_CLIENT_MAX_ORBITS || configs == NULL) {
        return -1;
    }
    if (s_workerCount != 0U) {
        /* Already initialized — ignore subsequent calls. */
        return 0;
    }

    s_lock = xSemaphoreCreateMutexStatic(&s_lockMem);
    if (s_lock == NULL) {
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        OrbitWorker *w = &s_workers[i];
        memset(w, 0, sizeof(*w));
        w->cfg = configs[i];
        if (w->cfg.pollIntervalMs == 0U) {
            w->cfg.pollIntervalMs = ORBIT_DEFAULT_INTERVAL_MS;
        }
        w->cfg.index = (uint8_t)i;  /* enforce contiguous slot indices */
        w->sock = -1;

        char name[16];
        DebugP_log("[ORBIT %u] starting worker for %s:%u\r\n",
                   (unsigned)i, w->cfg.ipv4, (unsigned)w->cfg.port);
        snprintf(name, sizeof(name), "orbit%u", (unsigned)i);
        w->task = xTaskCreateStatic(orbit_task, name, ORBIT_TASK_STACK_WORDS,
                                    w, ORBIT_TASK_PRIORITY,
                                    w->stack, &w->taskMem);
        if (w->task == NULL) {
            return -1;
        }
    }
    s_workerCount = count;
    return 0;
}

bool OrbitClient_GetSample(uint8_t index, OrbitSample *out)
{
    if (index >= s_workerCount || out == NULL) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = (s_workers[index].sample.pollCount > 0U);
    if (ok) {
        *out = s_workers[index].sample;
    }
    xSemaphoreGive(s_lock);
    return ok;
}

size_t OrbitClient_Count(void)
{
    return s_workerCount;
}

const char *OrbitClient_GetIpv4(uint8_t index)
{
    if (index >= s_workerCount) {
        return NULL;
    }
    return s_workers[index].cfg.ipv4;
}

/* ──────────────────────────────────────────────────────────────────── */
/*  Single-register write (FC06) — used by Triton control forwarding   */
/* ──────────────────────────────────────────────────────────────────── */

int OrbitClient_WriteHoldingRegister(uint8_t index, uint16_t addr,
                                     uint16_t value)
{
    if (index >= s_workerCount) {
        return -1;
    }
    OrbitWorker *w = &s_workers[index];

    /* Open a dedicated short-lived socket for this write — never touch
     * the polling task's socket (would corrupt its in-flight FC03
     * txn/byte stream). */
    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        DebugP_log("[ORBIT %u] write: socket() failed errno=%d\r\n",
                   (unsigned)index, errno);
        return -2;
    }

    struct timeval tv = { .tv_sec = ORBIT_RECV_TIMEOUT_S, .tv_usec = 0 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int one = 1;
    lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = lwip_htons(w->cfg.port);
    dst.sin_addr.s_addr = ipaddr_addr(w->cfg.ipv4);
    if (lwip_connect(sock, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        int e = errno;
        lwip_close(sock);
        DebugP_log("[ORBIT %u] write: connect %s:%u failed errno=%d\r\n",
                   (unsigned)index, w->cfg.ipv4, (unsigned)w->cfg.port, e);
        return -3;
    }

    /* MBAP + FC06 request: 7 B header + 5 B PDU = 12 B total. */
    const uint16_t txn = (uint16_t)(0x8000U | (index << 8) | (uint8_t)(addr));
    const uint8_t  unit = (uint8_t)(index + 1);
    uint8_t req[12];
    req[0]  = (uint8_t)(txn >> 8);
    req[1]  = (uint8_t)(txn & 0xFF);
    req[2]  = 0x00; req[3] = 0x00;       /* protocol id */
    req[4]  = 0x00; req[5] = 0x06;       /* PDU length (unit + 5 B FC06) */
    req[6]  = unit;
    req[7]  = 0x06;                      /* FC06 = Write Single Register */
    req[8]  = (uint8_t)(addr >> 8);
    req[9]  = (uint8_t)(addr & 0xFF);
    req[10] = (uint8_t)(value >> 8);
    req[11] = (uint8_t)(value & 0xFF);

    if (lwip_send(sock, req, sizeof(req), 0) != (int)sizeof(req)) {
        lwip_close(sock);
        DebugP_log("[ORBIT %u] write: send failed errno=%d\r\n",
                   (unsigned)index, errno);
        return -4;
    }

    /* FC06 echoes the request verbatim: 12 B. */
    uint8_t resp[12];
    int got = 0;
    while (got < (int)sizeof(resp)) {
        int n = lwip_recv(sock, resp + got, sizeof(resp) - (size_t)got, 0);
        if (n <= 0) {
            lwip_close(sock);
            DebugP_log("[ORBIT %u] write: recv failed errno=%d got=%d\r\n",
                       (unsigned)index, errno, got);
            return -5;
        }
        got += n;
    }
    lwip_close(sock);

    if (resp[0] != req[0] || resp[1] != req[1] || resp[6] != unit) {
        return -6;
    }
    if (resp[7] != 0x06) {
        /* Modbus exception (FC | 0x80) — typically illegal address. */
        DebugP_log("[ORBIT %u] write: Modbus exception fc=0x%02x ec=0x%02x\r\n",
                   (unsigned)index, resp[7], resp[8]);
        return -7;
    }
    DebugP_log("[ORBIT %u] write OK addr=%u val=%u\r\n",
               (unsigned)index, (unsigned)addr, (unsigned)value);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────── */
/*  Multi-register write (FC16) — Phase 4b Sub-phase 2 orbit writes    */
/* ──────────────────────────────────────────────────────────────────── */

int OrbitClient_WriteHoldingRegisters(uint8_t index, uint16_t addr,
                                      const uint16_t *values,
                                      uint16_t count)
{
    if (index >= s_workerCount || values == NULL) {
        return -1;
    }
    /* Modbus FC16 max-quantity is 123 (data byte count fits in u8). */
    if (count == 0U || count > 123U) {
        return -2;
    }
    OrbitWorker *w = &s_workers[index];

    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        DebugP_log("[ORBIT %u] writemulti: socket() failed errno=%d\r\n",
                   (unsigned)index, errno);
        return -2;
    }

    struct timeval tv = { .tv_sec = ORBIT_RECV_TIMEOUT_S, .tv_usec = 0 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int one = 1;
    lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = lwip_htons(w->cfg.port);
    dst.sin_addr.s_addr = ipaddr_addr(w->cfg.ipv4);
    if (lwip_connect(sock, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        int e = errno;
        lwip_close(sock);
        DebugP_log("[ORBIT %u] writemulti: connect %s:%u failed errno=%d\r\n",
                   (unsigned)index, w->cfg.ipv4, (unsigned)w->cfg.port, e);
        return -3;
    }

    /* MBAP + FC16 request: 7 B header + 1 FC + 2 addr + 2 qty + 1 bcnt
     * + 2*count data = 13 + 2*count total. PDU length = 6 + 2*count. */
    const uint16_t txn = (uint16_t)(0x9000U | (index << 8) | (uint8_t)(addr));
    const uint8_t  unit = (uint8_t)(index + 1);
    const uint16_t bcnt = (uint16_t)(2U * count);
    const uint16_t pdu_len = (uint16_t)(7U + bcnt);   /* unit + 6 PDU hdr + data */
    /* Worst case at count=123: 13 + 246 = 259 B request. */
    uint8_t req[13 + 246];
    req[0]  = (uint8_t)(txn >> 8);
    req[1]  = (uint8_t)(txn & 0xFF);
    req[2]  = 0x00; req[3] = 0x00;       /* protocol id */
    req[4]  = (uint8_t)(pdu_len >> 8);
    req[5]  = (uint8_t)(pdu_len & 0xFF);
    req[6]  = unit;
    req[7]  = 0x10;                      /* FC16 = Write Multiple Registers */
    req[8]  = (uint8_t)(addr >> 8);
    req[9]  = (uint8_t)(addr & 0xFF);
    req[10] = (uint8_t)(count >> 8);
    req[11] = (uint8_t)(count & 0xFF);
    req[12] = (uint8_t)bcnt;
    for (uint16_t i = 0; i < count; i++) {
        req[13 + i * 2U]     = (uint8_t)(values[i] >> 8);
        req[13 + i * 2U + 1] = (uint8_t)(values[i] & 0xFF);
    }
    const int req_len = 13 + 2 * (int)count;

    if (lwip_send(sock, req, (size_t)req_len, 0) != req_len) {
        lwip_close(sock);
        DebugP_log("[ORBIT %u] writemulti: send failed errno=%d\r\n",
                   (unsigned)index, errno);
        return -4;
    }

    /* FC16 response: 7 MBAP + 1 FC + 2 addr + 2 qty = 12 B. */
    uint8_t resp[12];
    int got = 0;
    while (got < (int)sizeof(resp)) {
        int n = lwip_recv(sock, resp + got, sizeof(resp) - (size_t)got, 0);
        if (n <= 0) {
            lwip_close(sock);
            DebugP_log("[ORBIT %u] writemulti: recv failed errno=%d got=%d\r\n",
                       (unsigned)index, errno, got);
            return -5;
        }
        got += n;
    }
    lwip_close(sock);

    if (resp[0] != req[0] || resp[1] != req[1] || resp[6] != unit) {
        return -6;
    }
    if (resp[7] != 0x10) {
        /* Modbus exception (FC | 0x80). */
        DebugP_log("[ORBIT %u] writemulti: Modbus exception fc=0x%02x ec=0x%02x\r\n",
                   (unsigned)index, resp[7], resp[8]);
        return -7;
    }
    /* Sanity: response echoes addr + qty. */
    const uint16_t r_addr = (uint16_t)((resp[8] << 8) | resp[9]);
    const uint16_t r_qty  = (uint16_t)((resp[10] << 8) | resp[11]);
    if (r_addr != addr || r_qty != count) {
        DebugP_log("[ORBIT %u] writemulti: echo mismatch (addr=%u qty=%u)\r\n",
                   (unsigned)index, (unsigned)r_addr, (unsigned)r_qty);
        return -8;
    }
    DebugP_log("[ORBIT %u] writemulti OK addr=%u qty=%u\r\n",
               (unsigned)index, (unsigned)addr, (unsigned)count);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────── */
/*  Single-coil write (FC05) — used by equipment-output sync loop      */
/* ──────────────────────────────────────────────────────────────────── */

int OrbitClient_WriteCoil(uint8_t index, uint16_t coil_addr, bool on)
{
    if (index >= s_workerCount) {
        return -1;
    }
    OrbitWorker *w = &s_workers[index];

    /* Dedicated short-lived socket — same rationale as
     * OrbitClient_WriteHoldingRegister: never share the polling task's
     * socket because an in-flight FC03 read would corrupt MBAP framing. */
    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        DebugP_log("[ORBIT %u] coil: socket() failed errno=%d\r\n",
                   (unsigned)index, errno);
        return -2;
    }

    struct timeval tv = { .tv_sec = ORBIT_RECV_TIMEOUT_S, .tv_usec = 0 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int one = 1;
    lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = lwip_htons(w->cfg.port);
    dst.sin_addr.s_addr = ipaddr_addr(w->cfg.ipv4);
    if (lwip_connect(sock, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        int e = errno;
        lwip_close(sock);
        DebugP_log("[ORBIT %u] coil: connect %s:%u failed errno=%d\r\n",
                   (unsigned)index, w->cfg.ipv4, (unsigned)w->cfg.port, e);
        return -3;
    }

    /* MBAP + FC05 request: 7 B header + 5 B PDU = 12 B total.
     * FC05 payload value is fixed: 0xFF00 = ON, 0x0000 = OFF
     * (Modbus spec; any other value is illegal). */
    const uint16_t value = on ? 0xFF00U : 0x0000U;
    const uint16_t txn   = (uint16_t)(0xC000U | (index << 8) | (uint8_t)(coil_addr));
    const uint8_t  unit  = (uint8_t)(index + 1);
    uint8_t req[12];
    req[0]  = (uint8_t)(txn >> 8);
    req[1]  = (uint8_t)(txn & 0xFF);
    req[2]  = 0x00; req[3] = 0x00;       /* protocol id */
    req[4]  = 0x00; req[5] = 0x06;       /* PDU length (unit + 5 B FC05) */
    req[6]  = unit;
    req[7]  = 0x05;                      /* FC05 = Write Single Coil */
    req[8]  = (uint8_t)(coil_addr >> 8);
    req[9]  = (uint8_t)(coil_addr & 0xFF);
    req[10] = (uint8_t)(value >> 8);
    req[11] = (uint8_t)(value & 0xFF);

    if (lwip_send(sock, req, sizeof(req), 0) != (int)sizeof(req)) {
        lwip_close(sock);
        DebugP_log("[ORBIT %u] coil: send failed errno=%d\r\n",
                   (unsigned)index, errno);
        return -4;
    }

    /* FC05 echoes the request verbatim: 12 B. */
    uint8_t resp[12];
    int got = 0;
    while (got < (int)sizeof(resp)) {
        int n = lwip_recv(sock, resp + got, sizeof(resp) - (size_t)got, 0);
        if (n <= 0) {
            lwip_close(sock);
            DebugP_log("[ORBIT %u] coil: recv failed errno=%d got=%d\r\n",
                       (unsigned)index, errno, got);
            return -5;
        }
        got += n;
    }
    lwip_close(sock);

    if (resp[0] != req[0] || resp[1] != req[1] || resp[6] != unit) {
        return -6;
    }
    if (resp[7] != 0x05) {
        DebugP_log("[ORBIT %u] coil: Modbus exception fc=0x%02x ec=0x%02x\r\n",
                   (unsigned)index, resp[7], resp[8]);
        return -7;
    }
    DebugP_log("[ORBIT %u] coil OK addr=%u %s\r\n",
               (unsigned)index, (unsigned)coil_addr, on ? "ON" : "OFF");
    return 0;
}
