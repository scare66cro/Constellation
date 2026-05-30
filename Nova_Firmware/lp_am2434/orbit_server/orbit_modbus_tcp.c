/*
 * orbit_modbus_tcp.c — Modbus TCP slave server
 *
 * MBAP frame:
 *   [0..1]  transaction id (echoed)
 *   [2..3]  protocol id    (must be 0)
 *   [4..5]  length         (count of bytes from byte 6 onward)
 *   [6]     unit id
 *   [7]     function code
 *   [8..]   PDU body
 *
 * On any framing error we drop the connection rather than send a
 * malformed reply — the master will reconnect on the next poll cycle.
 *
 * Concurrency: a single FreeRTOS task owns the listen socket plus up
 * to ORBIT_MBT_MAX_CONNS accepted sockets, multiplexed via select().
 * Per-connection state (RX buffer, fill pointer) lives in the on-task
 * stack-allocated array `s_conns[]`.
 *
 * Buffer sizing: max Modbus PDU = 253 bytes, plus 7 MBAP = 260 bytes.
 * We size the RX/TX buffer to 280 to leave a little headroom for
 * future extended FCs without any reallocation logic.
 */

#include "orbit_modbus_tcp.h"
#include "orbit_storage.h"
#include "orbit_gdc.h"
#include "orbit_triton.h"
#include "orbit_role.h"
#include "orbit_state.h"

#include <FreeRTOS.h>
#include <task.h>
#include <kernel/dpl/DebugP.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include <string.h>
#include <errno.h>

#define MBT_BUF_SIZE      280
#define MBT_HDR_SIZE      7    /* MBAP */
#define MBT_PDU_MAX       253
#define MBT_BACKLOG       4

typedef struct {
    int      fd;
    uint8_t  rx[MBT_BUF_SIZE];
    uint16_t rx_len;
} mbt_conn_t;

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline void     wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* --- per-role HR dispatch ----------------------------------------------
 *
 * Coil/discrete-input maps are identical across orbit roles (the GDC tick
 * just drives DO[0..9] from inside the same shared array), so they go
 * straight to storage_*. HR maps differ — GDC owns 300..339; everything
 * else falls through to storage. We keep the role check at this layer
 * so per-FC code below stays role-agnostic. */
static uint8_t hr_read_block(uint16_t start, uint16_t count, uint16_t *out)
{
    OrbitRole r = OrbitRole_Get();
    if (r == ORBIT_ROLE_GDC)    return OrbitGdc_ReadHrBlock   (start, count, out);
    if (r == ORBIT_ROLE_TRITON) return OrbitTriton_ReadHrBlock(start, count, out);
    return storage_read_hr_block(start, count, out);
}
static uint8_t hr_write_single(uint16_t addr, uint16_t value)
{
    OrbitRole r = OrbitRole_Get();
    if (r == ORBIT_ROLE_GDC)    return OrbitGdc_WriteHrSingle   (addr, value);
    if (r == ORBIT_ROLE_TRITON) return OrbitTriton_WriteHrSingle(addr, value);
    return storage_write_hr_single(addr, value);
}
static uint8_t hr_write_block(uint16_t start, uint16_t count, const uint16_t *vals)
{
    OrbitRole r = OrbitRole_Get();
    if (r == ORBIT_ROLE_GDC)    return OrbitGdc_WriteHrBlock   (start, count, vals);
    if (r == ORBIT_ROLE_TRITON) return OrbitTriton_WriteHrBlock(start, count, vals);
    return storage_write_hr_block(start, count, vals);
}

/* Build an exception response in-place. PDU = [func|0x80, exc]. */
static uint16_t build_exception(uint8_t *pdu, uint8_t func, uint8_t exc)
{
    pdu[0] = (uint8_t)(func | 0x80);
    pdu[1] = exc;
    return 2;
}

/* Dispatch one PDU. Returns response PDU length (>=2). */
static uint16_t dispatch_pdu(uint8_t *pdu, uint16_t pdu_len)
{
    if (pdu_len < 1) return 0;
    uint8_t func = pdu[0];
    uint8_t exc;

    switch (func) {
        case 0x01: /* Read Coils */
        case 0x02: /* Read Discrete Inputs */
        {
            if (pdu_len != 5) return build_exception(pdu, func, MB_EX_ILLEGAL_DATA_VALUE);
            uint16_t addr  = rd16(&pdu[1]);
            uint16_t count = rd16(&pdu[3]);
            if (count == 0 || count > 2000) return build_exception(pdu, func, MB_EX_ILLEGAL_DATA_VALUE);
            uint16_t bytes = (uint16_t)((count + 7u) / 8u);
            uint8_t  tmp[256];
            if (func == 0x01) exc = storage_read_coil_block(addr, count, tmp);
            else              exc = storage_read_discrete_block(addr, count, tmp);
            if (exc != MB_EX_NONE) return build_exception(pdu, func, exc);
            pdu[0] = func;
            pdu[1] = (uint8_t)bytes;
            memcpy(&pdu[2], tmp, bytes);
            return (uint16_t)(2 + bytes);
        }

        case 0x03: /* Read Holding Registers */
        case 0x04: /* Read Input Registers (aliased) */
        {
            if (pdu_len != 5) return build_exception(pdu, func, MB_EX_ILLEGAL_DATA_VALUE);
            uint16_t addr  = rd16(&pdu[1]);
            uint16_t count = rd16(&pdu[3]);
            if (count == 0 || count > 125) return build_exception(pdu, func, MB_EX_ILLEGAL_DATA_VALUE);
            uint16_t regs[125];
            exc = hr_read_block(addr, count, regs);
            if (exc != MB_EX_NONE) return build_exception(pdu, func, exc);
            pdu[0] = func;
            pdu[1] = (uint8_t)(count * 2u);
            for (uint16_t i = 0; i < count; i++) wr16(&pdu[2 + i * 2], regs[i]);
            return (uint16_t)(2 + count * 2u);
        }

        case 0x05: /* Write Single Coil */
        {
            if (pdu_len != 5) return build_exception(pdu, func, MB_EX_ILLEGAL_DATA_VALUE);
            uint16_t addr  = rd16(&pdu[1]);
            uint16_t value = rd16(&pdu[3]);
            exc = storage_write_coil_single(addr, value);
            if (exc != MB_EX_NONE) return build_exception(pdu, func, exc);
            /* Echo the request unchanged. */
            return 5;
        }

        case 0x06: /* Write Single Register */
        {
            if (pdu_len != 5) return build_exception(pdu, func, MB_EX_ILLEGAL_DATA_VALUE);
            uint16_t addr  = rd16(&pdu[1]);
            uint16_t value = rd16(&pdu[3]);
            exc = hr_write_single(addr, value);
            if (exc != MB_EX_NONE) return build_exception(pdu, func, exc);
            return 5;
        }

        case 0x0F: /* Write Multiple Coils */
        {
            if (pdu_len < 6) return build_exception(pdu, func, MB_EX_ILLEGAL_DATA_VALUE);
            uint16_t addr  = rd16(&pdu[1]);
            uint16_t count = rd16(&pdu[3]);
            uint8_t  bytec = pdu[5];
            if (pdu_len != 6u + bytec) return build_exception(pdu, func, MB_EX_ILLEGAL_DATA_VALUE);
            exc = storage_write_coil_block(addr, count, &pdu[6]);
            if (exc != MB_EX_NONE) return build_exception(pdu, func, exc);
            /* Response: func | addr | count */
            pdu[0] = func;
            wr16(&pdu[1], addr);
            wr16(&pdu[3], count);
            return 5;
        }

        case 0x10: /* Write Multiple Registers */
        {
            if (pdu_len < 6) return build_exception(pdu, func, MB_EX_ILLEGAL_DATA_VALUE);
            uint16_t addr  = rd16(&pdu[1]);
            uint16_t count = rd16(&pdu[3]);
            uint8_t  bytec = pdu[5];
            if (count == 0 || count > 123) return build_exception(pdu, func, MB_EX_ILLEGAL_DATA_VALUE);
            if (bytec != count * 2u || pdu_len != 6u + bytec) {
                return build_exception(pdu, func, MB_EX_ILLEGAL_DATA_VALUE);
            }
            uint16_t regs[123];
            for (uint16_t i = 0; i < count; i++) regs[i] = rd16(&pdu[6 + i * 2]);
            exc = hr_write_block(addr, count, regs);
            if (exc != MB_EX_NONE) return build_exception(pdu, func, exc);
            pdu[0] = func;
            wr16(&pdu[1], addr);
            wr16(&pdu[3], count);
            return 5;
        }

        default:
            return build_exception(pdu, func, MB_EX_ILLEGAL_FUNCTION);
    }
}

/* Pull complete MBAP frames out of a connection's RX buffer and
 * dispatch each one. Returns false if the socket is dead and should
 * be closed. */
static bool service_conn(mbt_conn_t *c)
{
    /* Drain any whole frames present. */
    while (c->rx_len >= MBT_HDR_SIZE) {
        uint16_t proto_id = rd16(&c->rx[2]);
        uint16_t length   = rd16(&c->rx[4]);
        if (proto_id != 0)               return false;
        if (length < 2 || length > MBT_PDU_MAX + 1) return false;
        uint16_t need = (uint16_t)(MBT_HDR_SIZE - 1 + length); /* hdr-1 + len */
        if (c->rx_len < need) break;

        /* Snapshot transaction id + unit id; dispatch overwrites pdu[]. */
        uint16_t tx_id   = rd16(&c->rx[0]);
        uint8_t  unit_id = c->rx[6];
        (void)unit_id; /* orbit roles accept any unit id */
        uint8_t *pdu     = &c->rx[7];
        uint16_t pdu_len = (uint16_t)(length - 1u);

        uint16_t resp_pdu_len = dispatch_pdu(pdu, pdu_len);
        if (resp_pdu_len < 1) return false;

        /* Build response in-place (MBAP header + PDU). */
        uint8_t resp[MBT_BUF_SIZE];
        wr16(&resp[0], tx_id);
        wr16(&resp[2], 0);
        wr16(&resp[4], (uint16_t)(resp_pdu_len + 1u)); /* length includes unit id */
        resp[6] = unit_id;
        memcpy(&resp[7], pdu, resp_pdu_len);
        uint16_t resp_total = (uint16_t)(MBT_HDR_SIZE + resp_pdu_len);

        int sent = lwip_send(c->fd, resp, resp_total, 0);
        if (sent != (int)resp_total) return false;

        /* Slide the consumed frame out of the buffer. */
        uint16_t consumed = need;
        if (c->rx_len > consumed) {
            memmove(c->rx, &c->rx[consumed], c->rx_len - consumed);
        }
        c->rx_len = (uint16_t)(c->rx_len - consumed);
    }
    return true;
}

void orbit_modbus_tcp_task(void *args)
{
    (void)args;

    /* Wait for lwIP to come up. Other tasks (lwip_smoke, orbit_client)
     * use the same gate: sit on a 100 ms tick until lwip_init has run.
     * Here we just delay 1 s and trust the network task to be ahead;
     * the listen socket call will fail and we'll retry. */
    vTaskDelay(pdMS_TO_TICKS(2000));

    int listen_fd = -1;
    while (listen_fd < 0) {
        listen_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) {
            DebugP_log("[MBT] socket() failed, retrying\r\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int yes = 1;
        lwip_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(ORBIT_MBT_PORT);
        if (lwip_bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            DebugP_log("[MBT] bind(:%d) failed (errno %d), retrying\r\n",
                       ORBIT_MBT_PORT, errno);
            lwip_close(listen_fd);
            listen_fd = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (lwip_listen(listen_fd, MBT_BACKLOG) < 0) {
            DebugP_log("[MBT] listen() failed, retrying\r\n");
            lwip_close(listen_fd);
            listen_fd = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
    }

    DebugP_log("[MBT] %s orbit listening on :%d\r\n",
               OrbitRole_Name(OrbitRole_Get()), ORBIT_MBT_PORT);

    mbt_conn_t conns[ORBIT_MBT_MAX_CONNS];
    for (int i = 0; i < ORBIT_MBT_MAX_CONNS; i++) { conns[i].fd = -1; conns[i].rx_len = 0; }

    for (;;) {
        /* Heartbeat to independent watchdog core: this loop running
         * means lwIP socket layer + Modbus accept path are alive. */
        extern void LpWatchdog_Ping(uint32_t alive_bit);
        LpWatchdog_Ping(0x01u /* LP_WD_ALIVE_MODBUS */);

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        int max_fd = listen_fd;
        for (int i = 0; i < ORBIT_MBT_MAX_CONNS; i++) {
            if (conns[i].fd >= 0) {
                FD_SET(conns[i].fd, &rfds);
                if (conns[i].fd > max_fd) max_fd = conns[i].fd;
            }
        }

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int rc = lwip_select(max_fd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (rc == 0) continue; /* timeout — loop and re-check */

        if (FD_ISSET(listen_fd, &rfds)) {
            struct sockaddr_in cli;
            socklen_t          cli_len = sizeof(cli);
            int new_fd = lwip_accept(listen_fd, (struct sockaddr *)&cli, &cli_len);
            if (new_fd >= 0) {
                int slot = -1;
                for (int i = 0; i < ORBIT_MBT_MAX_CONNS; i++) {
                    if (conns[i].fd < 0) { slot = i; break; }
                }
                if (slot < 0) {
                    DebugP_log("[MBT] no slots, dropping new conn\r\n");
                    lwip_close(new_fd);
                } else {
                    conns[slot].fd     = new_fd;
                    conns[slot].rx_len = 0;
                    DebugP_log("[MBT] accepted slot=%d fd=%d\r\n", slot, new_fd);
                }
            }
        }

        for (int i = 0; i < ORBIT_MBT_MAX_CONNS; i++) {
            if (conns[i].fd < 0) continue;
            if (!FD_ISSET(conns[i].fd, &rfds)) continue;

            int space = (int)(MBT_BUF_SIZE - conns[i].rx_len);
            if (space <= 0) {
                /* Shouldn't happen — buffer is sized for max frame.
                 * Drop the connection rather than overflow. */
                lwip_close(conns[i].fd);
                conns[i].fd = -1;
                conns[i].rx_len = 0;
                continue;
            }
            int n = lwip_recv(conns[i].fd, &conns[i].rx[conns[i].rx_len], space, 0);
            if (n <= 0) {
                lwip_close(conns[i].fd);
                conns[i].fd = -1;
                conns[i].rx_len = 0;
                continue;
            }
            conns[i].rx_len = (uint16_t)(conns[i].rx_len + (uint16_t)n);
            if (!service_conn(&conns[i])) {
                lwip_close(conns[i].fd);
                conns[i].fd = -1;
                conns[i].rx_len = 0;
            }
        }
    }
}
