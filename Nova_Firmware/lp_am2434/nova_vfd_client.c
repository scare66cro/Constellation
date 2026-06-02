/*
 * nova_vfd_client.c — Modbus TCP master to VFD endpoint.
 *
 * See nova_vfd_client.h for the API contract and threading model.
 *
 * Phase 4b Sub-phase 3 (2026-06-01).
 */

#include "nova_vfd_client.h"
#include "lp_settings.h"

#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/errno.h"

#include <kernel/dpl/DebugP.h>

/* Stack sized for the poll loop's deepest call chain (lwip_recv into
 * resp[259] + a handful of int-locals + 6 group buffers totaling ~46
 * uint16 = 92 B). 2 KB words = 8 KB is comfortable headroom and keeps
 * us inside the AM2434 MSRAM budget — see the 2026-06-01 link failure
 * note in nova_vfd_client.h. */
#define VFD_TASK_STACK_WORDS  (2U * 1024U)
#define VFD_TASK_PRI          (configMAX_PRIORITIES - 6)
#define VFD_RECV_TIMEOUT_S    3
#define VFD_SCAN_RESCAN_TICKS 30U   /* re-scan for new units every N polls */

typedef struct {
    NovaVfdSnapshot sample;
    bool            known;  /* set true once we've seen a response on this unit */
} VfdWorker;

static StaticTask_t   s_task_obj;
static StackType_t    s_task_stack[VFD_TASK_STACK_WORDS];
static TaskHandle_t   s_task = NULL;

static SemaphoreHandle_t s_lock = NULL;
static StaticSemaphore_t s_lock_buf;

static VfdWorker s_workers[NOVA_VFD_MAX_DRIVES];
static size_t   s_drive_count = 0;  /* number of `known` entries with online=true */

static int      s_sock      = -1;
static bool     s_connected = false;
static uint16_t s_txn_id    = 0;

/* Cached endpoint pulled atomically from LpSettings each cycle. The
 * config-changed signal triggers a re-evaluate + reconnect on the next
 * poll. */
static uint32_t s_cur_host_ipv4   = 0;
static uint16_t s_cur_port        = 0;
static uint8_t  s_cur_max_scan    = 0;
static uint16_t s_cur_poll_ms     = 1000;
static bool     s_config_dirty    = true;  /* force first reload */

/* Per-poll round-robin scan counter (mirrors orbit_client's pattern). */
static uint32_t s_scan_counter = 0;

static void vfd_disconnect(void)
{
    if (s_sock >= 0) {
        lwip_close(s_sock);
        s_sock = -1;
    }
    s_connected = false;
}

static int vfd_connect(void)
{
    if (s_connected) return 0;
    if (s_cur_host_ipv4 == 0U) return -1;  /* disabled */

    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        DebugP_log("[VFD] socket() failed errno=%d\r\n", errno);
        return -2;
    }

    struct timeval tv = { .tv_sec = VFD_RECV_TIMEOUT_S, .tv_usec = 0 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int one = 1;
    lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
             (unsigned)((s_cur_host_ipv4 >> 24) & 0xFFU),
             (unsigned)((s_cur_host_ipv4 >> 16) & 0xFFU),
             (unsigned)((s_cur_host_ipv4 >>  8) & 0xFFU),
             (unsigned) (s_cur_host_ipv4        & 0xFFU));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = lwip_htons(s_cur_port);
    dst.sin_addr.s_addr = ipaddr_addr(ip_str);
    if (lwip_connect(sock, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        int e = errno;
        lwip_close(sock);
        DebugP_log("[VFD] connect %s:%u failed errno=%d\r\n",
                   ip_str, (unsigned)s_cur_port, e);
        return -3;
    }

    s_sock = sock;
    s_connected = true;
    DebugP_log("[VFD] connected %s:%u\r\n", ip_str, (unsigned)s_cur_port);
    return 0;
}

/* Read `qty` HRs starting at `addr` from `unit_id` over the shared
 * polling socket. Uses the same `9 + qty*2` response budget as the
 * post-fix `orbit_read_hr` (see
 * memories/repo/orbit-client-fc03-buffer-off-by-one-2026-06-01.md). */
static int vfd_read_hr(uint8_t unit_id, uint16_t addr, uint16_t qty,
                       uint16_t *out)
{
    if (!s_connected || s_sock < 0) return -1;
    if (qty == 0U || qty > 125U) return -2;

    uint16_t txn = ++s_txn_id;
    uint8_t req[12];
    req[0]  = (uint8_t)(txn >> 8);
    req[1]  = (uint8_t)(txn & 0xFF);
    req[2]  = 0x00; req[3] = 0x00;
    req[4]  = 0x00; req[5] = 0x06;
    req[6]  = unit_id;
    req[7]  = 0x03;
    req[8]  = (uint8_t)(addr >> 8);
    req[9]  = (uint8_t)(addr & 0xFF);
    req[10] = (uint8_t)(qty >> 8);
    req[11] = (uint8_t)(qty & 0xFF);

    if (lwip_send(s_sock, req, sizeof(req), 0) != (int)sizeof(req)) {
        return -3;
    }

    const int expected = 9 + (int)qty * 2;
    uint8_t resp[9 + 250];
    int got = 0;
    while (got < expected) {
        int n = lwip_recv(s_sock, resp + got, sizeof(resp) - (size_t)got, 0);
        if (n <= 0) return -4;
        got += n;
    }

    if (resp[0] != req[0] || resp[1] != req[1]) return -5;
    if (resp[6] != unit_id) return -6;
    if (resp[7] != 0x03) return -7;             /* exception or wrong FC */
    if (resp[8] != qty * 2) return -8;
    for (uint16_t i = 0; i < qty; i++) {
        out[i] = ((uint16_t)resp[9 + i * 2] << 8) | resp[10 + i * 2];
    }
    return 0;
}

/* Pull the latest VfdConfig snapshot into our local cache. Called
 * after `NovaVfdClient_ConfigChanged` flips `s_config_dirty`. */
static void vfd_reload_config(void)
{
    const LpVfdConfig *cfg = LpSettings_GetVfdConfig();
    s_cur_host_ipv4   = cfg ? cfg->host_ipv4        : 0U;
    s_cur_port        = cfg ? cfg->port             : 502U;
    s_cur_max_scan    = cfg ? cfg->max_scan_unit_id : 8U;
    s_cur_poll_ms     = cfg ? cfg->poll_interval_ms : 1000U;
    if (s_cur_max_scan == 0U) s_cur_max_scan = 8U;
    if (s_cur_max_scan > NOVA_VFD_MAX_DRIVES) s_cur_max_scan = NOVA_VFD_MAX_DRIVES;
    if (s_cur_poll_ms < 100U) s_cur_poll_ms = 100U;
    s_config_dirty = false;

    /* Force a reconnect so the new endpoint takes effect. */
    vfd_disconnect();
    /* Drop the known-set; rediscover on first poll under the new config. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < NOVA_VFD_MAX_DRIVES; i++) {
        s_workers[i].known = false;
        s_workers[i].sample.online = false;
    }
    s_drive_count = 0;
    xSemaphoreGive(s_lock);
}

/* Probe unit IDs 1..max_scan to find new drives we haven't seen yet.
 * Records "known" status under s_lock so the snapshot accessor can
 * iterate without racing. */
static void vfd_scan_for_drives(void)
{
    for (uint8_t uid = 1; uid <= s_cur_max_scan; uid++) {
        VfdWorker *w = &s_workers[uid - 1U];
        if (w->known) continue;
        uint16_t probe = 0;
        if (vfd_read_hr(uid, 0U, 1U, &probe) == 0) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            w->known = true;
            w->sample.unit_id = uid;
            xSemaphoreGive(s_lock);
            DebugP_log("[VFD] discovered unit %u\r\n", (unsigned)uid);
        }
    }
}

static void vfd_publish_group(uint8_t uid, uint16_t hr_base, uint16_t count,
                              const uint16_t *src)
{
    /* Stub for the wire emit hook — main.c calls
     * `nova_vfd_emit_bank_envelope` directly off the snapshot after
     * each cycle. Keeping this here as the integration point so future
     * pushes (e.g. immediate emit on big change) don't need to thread
     * a callback through. */
    (void)uid; (void)hr_base; (void)count; (void)src;
}

static void vfd_task_body(void *arg)
{
    (void)arg;
    /* Stagger startup so we don't race with orbit_client connect. */
    vTaskDelay(pdMS_TO_TICKS(800));

    for (;;) {
        if (s_config_dirty) vfd_reload_config();

        if (s_cur_host_ipv4 == 0U) {
            /* Disabled — keep the task alive cheaply so the bridge can
             * re-enable us by re-sending VfdConfig. */
            vTaskDelay(pdMS_TO_TICKS(s_cur_poll_ms));
            continue;
        }

        if (!s_connected) {
            if (vfd_connect() != 0) {
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
        }

        if (++s_scan_counter >= VFD_SCAN_RESCAN_TICKS || s_drive_count == 0U) {
            s_scan_counter = 0;
            vfd_scan_for_drives();
        }

        size_t online_count = 0;
        for (uint8_t i = 0; i < NOVA_VFD_MAX_DRIVES; i++) {
            VfdWorker *w = &s_workers[i];
            if (!w->known) continue;

            const uint8_t uid = w->sample.unit_id;
            uint16_t tmp_process [NOVA_VFD_GROUP_PROCESS_COUNT];
            uint16_t tmp_live    [NOVA_VFD_GROUP_LIVE_COUNT];
            uint16_t tmp_fault   [NOVA_VFD_GROUP_FAULT_COUNT];
            uint16_t tmp_limits  [NOVA_VFD_GROUP_LIMITS_COUNT];
            uint16_t tmp_ramp    [NOVA_VFD_GROUP_RAMP_COUNT];
            uint16_t tmp_nameplate[NOVA_VFD_GROUP_NAMEPLATE_COUNT];
            bool process_ok = false;
            bool live_ok = false, fault_ok = false, limits_ok = false;
            bool ramp_ok = false, nameplate_ok = false;

            int rc = vfd_read_hr(uid, NOVA_VFD_GROUP_PROCESS_BASE,
                                 NOVA_VFD_GROUP_PROCESS_COUNT, tmp_process);
            if (rc == 0) process_ok = true;
            else if (rc == -4 || rc == -5) {
                /* Hard socket failure — drop & reconnect on next cycle. */
                vfd_disconnect();
                continue;
            }

            if (vfd_read_hr(uid, NOVA_VFD_GROUP_LIVE_BASE,
                            NOVA_VFD_GROUP_LIVE_COUNT, tmp_live) == 0)      live_ok = true;
            if (vfd_read_hr(uid, NOVA_VFD_GROUP_FAULT_BASE,
                            NOVA_VFD_GROUP_FAULT_COUNT, tmp_fault) == 0)    fault_ok = true;
            if (vfd_read_hr(uid, NOVA_VFD_GROUP_LIMITS_BASE,
                            NOVA_VFD_GROUP_LIMITS_COUNT, tmp_limits) == 0)  limits_ok = true;
            if (vfd_read_hr(uid, NOVA_VFD_GROUP_RAMP_BASE,
                            NOVA_VFD_GROUP_RAMP_COUNT, tmp_ramp) == 0)      ramp_ok = true;
            if (vfd_read_hr(uid, NOVA_VFD_GROUP_NAMEPLATE_BASE,
                            NOVA_VFD_GROUP_NAMEPLATE_COUNT, tmp_nameplate) == 0)
                nameplate_ok = true;

            xSemaphoreTake(s_lock, portMAX_DELAY);
            if (process_ok)   memcpy(w->sample.process,   tmp_process,   sizeof(tmp_process));
            if (live_ok)      memcpy(w->sample.live,      tmp_live,      sizeof(tmp_live));
            if (fault_ok)     memcpy(w->sample.fault,     tmp_fault,     sizeof(tmp_fault));
            if (limits_ok)    memcpy(w->sample.limits,    tmp_limits,    sizeof(tmp_limits));
            if (ramp_ok)      memcpy(w->sample.ramp,      tmp_ramp,      sizeof(tmp_ramp));
            if (nameplate_ok) memcpy(w->sample.nameplate, tmp_nameplate, sizeof(tmp_nameplate));
            w->sample.online = process_ok;   /* process-data is the canonical liveness signal */
            if (process_ok) {
                w->sample.pollCount++;
                w->sample.lastPollTickMs =
                    (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                online_count++;
            } else {
                w->sample.errorCount++;
            }
            xSemaphoreGive(s_lock);

            /* Reserved hook — main.c emits VfdRegBank from the snapshot
             * accessor; if we ever want to push on first-failure
             * transitions we can wire it through here. */
            (void)vfd_publish_group;
        }

        s_drive_count = online_count;
        vTaskDelay(pdMS_TO_TICKS(s_cur_poll_ms));
    }
}

/* ──────────────────────────────────────────────────────────────────── */
/*  Public API                                                          */
/* ──────────────────────────────────────────────────────────────────── */

int NovaVfdClient_Init(void)
{
    if (s_task != NULL) return 0;

    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    if (s_lock == NULL) return -1;

    s_task = xTaskCreateStatic(vfd_task_body, "vfd_cli",
                               VFD_TASK_STACK_WORDS, NULL,
                               VFD_TASK_PRI, s_task_stack, &s_task_obj);
    return (s_task != NULL) ? 0 : -1;
}

void NovaVfdClient_ConfigChanged(void)
{
    s_config_dirty = true;
}

size_t NovaVfdClient_DriveCount(void)
{
    return s_drive_count;
}

bool NovaVfdClient_GetSnapshot(uint8_t index, NovaVfdSnapshot *out)
{
    if (index >= NOVA_VFD_MAX_DRIVES || out == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = s_workers[index].known;
    if (ok) *out = s_workers[index].sample;
    xSemaphoreGive(s_lock);
    return ok;
}

int NovaVfdClient_WriteRegisters(uint8_t unit_id, uint16_t addr,
                                 const uint16_t *values, uint16_t count)
{
    if (values == NULL || count == 0U) return -1;
    if (count > 123U) return -2;
    if (s_cur_host_ipv4 == 0U) return -3;  /* VFD disabled */

    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -4;

    struct timeval tv = { .tv_sec = VFD_RECV_TIMEOUT_S, .tv_usec = 0 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int one = 1;
    lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
             (unsigned)((s_cur_host_ipv4 >> 24) & 0xFFU),
             (unsigned)((s_cur_host_ipv4 >> 16) & 0xFFU),
             (unsigned)((s_cur_host_ipv4 >>  8) & 0xFFU),
             (unsigned) (s_cur_host_ipv4        & 0xFFU));
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = lwip_htons(s_cur_port);
    dst.sin_addr.s_addr = ipaddr_addr(ip_str);
    if (lwip_connect(sock, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        int e = errno;
        lwip_close(sock);
        DebugP_log("[VFD] write connect %s:%u failed errno=%d\r\n",
                   ip_str, (unsigned)s_cur_port, e);
        return -5;
    }

    const uint16_t txn = (uint16_t)(0xA000U | ((uint16_t)unit_id << 8) | (uint8_t)addr);

    if (count == 1U) {
        /* FC06 — single register, 12-byte request, 12-byte echo. */
        uint8_t req[12];
        req[0]  = (uint8_t)(txn >> 8);
        req[1]  = (uint8_t)(txn & 0xFF);
        req[2]  = 0x00; req[3] = 0x00;
        req[4]  = 0x00; req[5] = 0x06;
        req[6]  = unit_id;
        req[7]  = 0x06;
        req[8]  = (uint8_t)(addr >> 8);
        req[9]  = (uint8_t)(addr & 0xFF);
        req[10] = (uint8_t)(values[0] >> 8);
        req[11] = (uint8_t)(values[0] & 0xFF);
        if (lwip_send(sock, req, sizeof(req), 0) != (int)sizeof(req)) {
            lwip_close(sock);
            return -6;
        }
        uint8_t resp[12];
        int got = 0;
        while (got < (int)sizeof(resp)) {
            int n = lwip_recv(sock, resp + got, sizeof(resp) - (size_t)got, 0);
            if (n <= 0) { lwip_close(sock); return -7; }
            got += n;
        }
        lwip_close(sock);
        if (resp[0] != req[0] || resp[1] != req[1] || resp[6] != unit_id) return -8;
        if (resp[7] != 0x06) {
            DebugP_log("[VFD] write FC06 exc fc=0x%02x ec=0x%02x\r\n",
                       resp[7], resp[8]);
            return -9;
        }
        return 0;
    }

    /* FC16 — multi-register block. */
    const uint16_t bcnt = (uint16_t)(2U * count);
    const uint16_t pdu_len = (uint16_t)(7U + bcnt);
    uint8_t req[13 + 246];
    req[0]  = (uint8_t)(txn >> 8);
    req[1]  = (uint8_t)(txn & 0xFF);
    req[2]  = 0x00; req[3] = 0x00;
    req[4]  = (uint8_t)(pdu_len >> 8);
    req[5]  = (uint8_t)(pdu_len & 0xFF);
    req[6]  = unit_id;
    req[7]  = 0x10;
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
        return -10;
    }
    /* FC16 response: 7 MBAP + FC + 2 addr + 2 qty = 12 B. */
    uint8_t resp[12];
    int got = 0;
    while (got < (int)sizeof(resp)) {
        int n = lwip_recv(sock, resp + got, sizeof(resp) - (size_t)got, 0);
        if (n <= 0) { lwip_close(sock); return -11; }
        got += n;
    }
    lwip_close(sock);
    if (resp[0] != req[0] || resp[1] != req[1] || resp[6] != unit_id) return -12;
    if (resp[7] != 0x10) {
        DebugP_log("[VFD] write FC16 exc fc=0x%02x ec=0x%02x\r\n",
                   resp[7], resp[8]);
        return -13;
    }
    const uint16_t r_addr = (uint16_t)((resp[8] << 8) | resp[9]);
    const uint16_t r_qty  = (uint16_t)((resp[10] << 8) | resp[11]);
    if (r_addr != addr || r_qty != count) return -14;
    return 0;
}
