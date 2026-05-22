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

    /* Reply: 7 B MBAP + 1 B byte-count + qty*2 B data = 8 + qty*2. */
    const int expected = 8 + (int)qty * 2;
    uint8_t resp[8 + 250];
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

        int rc = orbit_read_hr(w, unit, ORBIT_SENSOR_HR_BASE,
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
