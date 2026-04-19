/*
 * hal_modbus_tcp.c — Lightweight Modbus TCP client for AM2434 Cortex-R5F
 *
 * Uses POSIX-style sockets (provided by lwIP's socket API or, in QEMU
 * builds, the nosys stubs that redirect to host TCP via QEMU's
 * slirp/user-mode networking).
 *
 * For QEMU development: we use raw TCP via a simple socket layer.
 * For real hardware: same API, lwIP sockets under the hood.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "hal_modbus_tcp.h"
#include "debug.h"

#include <string.h>

#ifdef QEMU_BUILD
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#endif

/*
 * Socket abstraction — in QEMU builds we use the host networking
 * via semihosting or a simple TCP shim. On real hardware this would
 * be lwIP sockets. For now we provide a minimal implementation that
 * works with the QEMU user-networking stack.
 *
 * In the QEMU constellation-nova machine, Ethernet is modeled as
 * user-mode networking with the guest at 10.0.2.15 and the host
 * at 10.0.2.2. The Orbit simulator runs on the Windows host.
 */

#ifdef QEMU_BUILD
/* For QEMU: we use a simple embedded TCP client via UART tunneling
 * or direct memory-mapped I/O. Since QEMU doesn't provide real
 * network sockets to bare-metal firmware, we implement a Modbus TCP
 * frame builder/parser that operates over a UART-tunneled TCP link.
 *
 * The QEMU machine model provides a "modbus-proxy" chardev on UART2
 * that transparently tunnels Modbus TCP frames to the host network.
 * This means the firmware sends/receives raw Modbus TCP MBAP+PDU frames
 * on UART2, and the QEMU machine relays them via TCP to the Orbit
 * simulator.
 */

#include "hal.h"

/* UART2 is configured as the Modbus TCP tunnel in QEMU builds */
#define MODBUS_UART    HAL_UART_RS485A

static uint8_t tx_buf[MBTCP_MAX_FRAME];
static uint8_t rx_buf[MBTCP_MAX_FRAME];

/* Mutex protecting the shared tx_buf/rx_buf and UART2 access.
 * ThreadSerialShift (orbit_poll_io) and ThreadSerialCom
 * (ReadAnalogBoards → orbit_read_sensors) both call transact()
 * concurrently.  Without this mutex, interleaved UART I/O
 * causes frame misalignment, buffer corruption, and stack smashing. */
static xSemaphoreHandle modbus_mutex = NULL;

void mbtcp_init(mbtcp_conn_t *conn,
                uint8_t ip_a, uint8_t ip_b, uint8_t ip_c, uint8_t ip_d,
                uint16_t port, uint8_t unit_id)
{
    /* Create the mutex once (first caller wins) */
    if (modbus_mutex == NULL) {
        modbus_mutex = xSemaphoreCreateMutex();
    }
    memset(conn, 0, sizeof(*conn));
    conn->socket_fd = -1;
    conn->unit_id = unit_id;
    conn->ip_addr = ((uint32_t)ip_a << 24) | ((uint32_t)ip_b << 16) |
                    ((uint32_t)ip_c << 8)  | (uint32_t)ip_d;
    conn->port = port;
    conn->timeout_ms = MBTCP_TIMEOUT_MS;
    conn->transaction_id = 0;
}

int mbtcp_connect(mbtcp_conn_t *conn)
{
    /* In QEMU mode, the UART tunnel is always "connected" if the
     * chardev is attached. Signal connected by setting socket_fd = 0 */
    conn->socket_fd = 0;
    debug_printf("[MBTCP] Connected to %d.%d.%d.%d:%d (UART tunnel)\r\n",
                 (conn->ip_addr >> 24) & 0xFF,
                 (conn->ip_addr >> 16) & 0xFF,
                 (conn->ip_addr >> 8) & 0xFF,
                 conn->ip_addr & 0xFF,
                 conn->port);
    return MBTCP_OK;
}

void mbtcp_disconnect(mbtcp_conn_t *conn)
{
    conn->socket_fd = -1;
}

int mbtcp_is_connected(mbtcp_conn_t *conn)
{
    return conn->socket_fd >= 0;
}

/* ─── Internal: build MBAP header ─────────────────────────────────── */

static int build_mbap(mbtcp_conn_t *conn, uint8_t *buf, uint16_t pdu_len)
{
    conn->transaction_id++;
    buf[0] = (conn->transaction_id >> 8) & 0xFF;  /* Transaction ID high */
    buf[1] = conn->transaction_id & 0xFF;          /* Transaction ID low */
    buf[2] = 0x00;                                  /* Protocol ID high */
    buf[3] = 0x00;                                  /* Protocol ID low */
    buf[4] = ((pdu_len + 1) >> 8) & 0xFF;          /* Length high (unit + PDU) */
    buf[5] = (pdu_len + 1) & 0xFF;                 /* Length low */
    buf[6] = conn->unit_id;                          /* Unit ID */
    return MBTCP_MBAP_LEN;
}

/* ─── Internal: send frame and receive response ───────────────────── */

static int transact(mbtcp_conn_t *conn, uint8_t *frame, int frame_len,
                    uint8_t *resp, int resp_max)
{
    /* Acquire the Modbus mutex — serializes all UART2 access across
     * ThreadSerialShift and ThreadSerialCom. */
    if (modbus_mutex != NULL) {
        xSemaphoreTake(modbus_mutex, portMAX_DELAY);
    }

    /* Flush any stale RX bytes left over from a previous timed-out
     * transaction.  Without this, leftover bytes cause MBAP frame
     * misalignment which leads to incorrect byte_count values and
     * stack-corrupting memcpy overflows in the caller.
     * Do multiple passes with small delays to catch bytes still
     * arriving through the QEMU chardev pipeline. */
    {
        int flushed = 0;
        for (int pass = 0; pass < 5; pass++) {
            while (hal_uart_rx_available(MODBUS_UART)) {
                (void)hal_uart_get_char(MODBUS_UART);
                if (++flushed > MBTCP_MAX_FRAME) goto pre_flush_done;
            }
            if (flushed > 0 || pass > 0) vTaskDelay(2);
            else break;  /* no stale data on first check — skip delays */
        }
pre_flush_done:
        if (flushed > 0) {
            debug_printf("[MBTCP] flushed %d stale RX bytes\r\n", flushed);
        }
    }

    /* Debug: log the TX frame */
    debug_printf("[MBTCP] TX %d bytes: FC=%02X addr=%04X\r\n",
                 frame_len, frame[7],
                 (frame_len > 9) ? ((frame[8] << 8) | frame[9]) : 0);

    /* Send raw MBAP+PDU frame over UART tunnel */
    hal_uart_send(MODBUS_UART, frame, frame_len);
    conn->tx_count++;

    /* Receive response: read 7-byte MBAP header first, then PDU body */
    uint32_t start = hal_timer_get_ms();
    int got = 0;
    int iter = 0;  /* fallback iteration-count timeout */

    /* Phase 1: read the 6-byte MBAP header (TxnID(2)+Proto(2)+Len(2))
     *
     * Yield via vTaskDelay(1) when no data available.  This triggers a
     * FreeRTOS context switch — other tasks execute on the vCPU, and
     * QEMU's main loop processes chardev socket I/O between their
     * translation blocks.  Each vTaskDelay costs ~1ms. */
    while (got < MBTCP_MBAP_LEN) {
        if (hal_uart_rx_available(MODBUS_UART)) {
            resp[got++] = hal_uart_get_char(MODBUS_UART);
        } else {
            vTaskDelay(1);
            iter++;
        }
        uint32_t elapsed = hal_timer_get_ms() - start;
        if (elapsed > conn->timeout_ms || iter > 8000) {
            conn->err_count++;
            conn->last_error = MBTCP_ERR_TIMEOUT;
            debug_printf("[MBTCP] TIMEOUT ph1 got=%d/%d ms=%lu iter=%d\r\n",
                         got, MBTCP_MBAP_LEN,
                         (unsigned long)elapsed, iter);
            goto flush_and_return_timeout;
        }
    }

    /* Validate protocol ID — must be 0x0000 for Modbus TCP.
     * If not, we've read misaligned data (stale bytes from a
     * previous timed-out transaction). */
    if (resp[2] != 0x00 || resp[3] != 0x00) {
        conn->err_count++;
        conn->last_error = MBTCP_ERR_SHORT_RESP;
        debug_printf("[MBTCP] bad proto=%02X%02X — frame misaligned\r\n",
                     resp[2], resp[3]);
        goto flush_and_return_error;
    }

    /* Extract "length" from MBAP bytes 4-5.
     * This is the number of bytes following the length field:
     * Unit ID (1) + PDU (N).  We already read 7 bytes (MBAP header
     * including Unit ID), so total frame = 6 + body_len. */
    int body_len = (resp[4] << 8) | resp[5];
    int total_len = 6 + body_len;  /* 6 fixed MBAP bytes + body_len */

    if (total_len > resp_max || body_len < 1 || body_len > MBTCP_MAX_PDU + 1) {
        conn->err_count++;
        conn->last_error = MBTCP_ERR_SHORT_RESP;
        debug_printf("[MBTCP] bad body_len=%d — flushing\r\n", body_len);
        goto flush_and_return_error;
    }

    /* Phase 2: read the body (unit_id + PDU) — same vTaskDelay yield */
    while (got < total_len) {
        if (hal_uart_rx_available(MODBUS_UART)) {
            resp[got++] = hal_uart_get_char(MODBUS_UART);
        } else {
            vTaskDelay(1);
            iter++;
        }
        uint32_t elapsed = hal_timer_get_ms() - start;
        if (elapsed > conn->timeout_ms || iter > 8000) {
            conn->err_count++;
            conn->last_error = MBTCP_ERR_TIMEOUT;
            debug_printf("[MBTCP] TIMEOUT ph2 got=%d/%d ms=%lu iter=%d\r\n",
                         got, total_len,
                         (unsigned long)elapsed, iter);
            goto flush_and_return_timeout;
        }
    }

    /* Check for Modbus exception */
    if (resp[7] & 0x80) {
        conn->err_count++;
        conn->last_error = MBTCP_ERR_EXCEPTION;
        if (modbus_mutex) xSemaphoreGive(modbus_mutex);
        return MBTCP_ERR_EXCEPTION;
    }

    conn->rx_count++;
    if (modbus_mutex) xSemaphoreGive(modbus_mutex);
    return total_len;

flush_and_return_timeout:
    conn->last_error = MBTCP_ERR_TIMEOUT;
    /* Fall through to flush */
flush_and_return_error:
    /* Drain any remaining RX bytes to prevent frame misalignment
     * on the next transaction.  Bytes may still be in-flight through
     * the WSL network stack → QEMU chardev pipeline, so we drain
     * repeatedly with delays to catch late arrivals. */
    {
        int flushed = 0;
        for (int drain_pass = 0; drain_pass < 50; drain_pass++) {
            vTaskDelay(2);
            int got_any = 0;
            while (hal_uart_rx_available(MODBUS_UART)) {
                (void)hal_uart_get_char(MODBUS_UART);
                flushed++;
                got_any = 1;
                if (flushed > MBTCP_MAX_FRAME * 2) goto flush_done;
            }
            /* If we've drained for 10+ passes with no new data, stop */
            if (!got_any && drain_pass >= 10) break;
        }
flush_done:
        if (flushed > 0) {
            debug_printf("[MBTCP] post-error flush: %d bytes\r\n", flushed);
        }
    }
    if (modbus_mutex) xSemaphoreGive(modbus_mutex);
    return (int)conn->last_error;
}

/* ─── FC01: Read Coils ────────────────────────────────────────────── */

int mbtcp_read_coils(mbtcp_conn_t *conn,
                     uint16_t start_addr, uint16_t quantity,
                     uint8_t *coils_out)
{
    int hdr = build_mbap(conn, tx_buf, 5);  /* FC(1) + addr(2) + qty(2) */
    tx_buf[hdr]   = 0x01;                       /* FC01 */
    tx_buf[hdr+1] = (start_addr >> 8) & 0xFF;
    tx_buf[hdr+2] = start_addr & 0xFF;
    tx_buf[hdr+3] = (quantity >> 8) & 0xFF;
    tx_buf[hdr+4] = quantity & 0xFF;

    int rc = transact(conn, tx_buf, hdr + 5, rx_buf, sizeof(rx_buf));
    if (rc < 0) return rc;

    uint8_t byte_count = rx_buf[8];
    uint8_t max_bytes = (uint8_t)((quantity + 7) / 8);
    if (byte_count > max_bytes) byte_count = max_bytes;
    memcpy(coils_out, &rx_buf[9], byte_count);
    return MBTCP_OK;
}

/* ─── FC02: Read Discrete Inputs ──────────────────────────────────── */

int mbtcp_read_discrete_inputs(mbtcp_conn_t *conn,
                               uint16_t start_addr, uint16_t quantity,
                               uint8_t *inputs_out)
{
    int hdr = build_mbap(conn, tx_buf, 5);
    tx_buf[hdr]   = 0x02;                       /* FC02 */
    tx_buf[hdr+1] = (start_addr >> 8) & 0xFF;
    tx_buf[hdr+2] = start_addr & 0xFF;
    tx_buf[hdr+3] = (quantity >> 8) & 0xFF;
    tx_buf[hdr+4] = quantity & 0xFF;

    int rc = transact(conn, tx_buf, hdr + 5, rx_buf, sizeof(rx_buf));
    if (rc < 0) return rc;

    uint8_t byte_count = rx_buf[8];
    uint8_t max_bytes = (uint8_t)((quantity + 7) / 8);
    if (byte_count > max_bytes) byte_count = max_bytes;
    memcpy(inputs_out, &rx_buf[9], byte_count);
    return MBTCP_OK;
}

/* ─── FC03: Read Holding Registers ────────────────────────────────── */

int mbtcp_read_holding_regs(mbtcp_conn_t *conn,
                            uint16_t start_addr, uint16_t quantity,
                            uint16_t *regs_out)
{
    int hdr = build_mbap(conn, tx_buf, 5);
    tx_buf[hdr]   = 0x03;
    tx_buf[hdr+1] = (start_addr >> 8) & 0xFF;
    tx_buf[hdr+2] = start_addr & 0xFF;
    tx_buf[hdr+3] = (quantity >> 8) & 0xFF;
    tx_buf[hdr+4] = quantity & 0xFF;

    int rc = transact(conn, tx_buf, hdr + 5, rx_buf, sizeof(rx_buf));
    if (rc < 0) return rc;

    for (int i = 0; i < quantity; i++) {
        regs_out[i] = (rx_buf[9 + i*2] << 8) | rx_buf[10 + i*2];
    }
    return MBTCP_OK;
}

/* ─── FC04: Read Input Registers ──────────────────────────────────── */

int mbtcp_read_input_regs(mbtcp_conn_t *conn,
                          uint16_t start_addr, uint16_t quantity,
                          uint16_t *regs_out)
{
    int hdr = build_mbap(conn, tx_buf, 5);
    tx_buf[hdr]   = 0x04;
    tx_buf[hdr+1] = (start_addr >> 8) & 0xFF;
    tx_buf[hdr+2] = start_addr & 0xFF;
    tx_buf[hdr+3] = (quantity >> 8) & 0xFF;
    tx_buf[hdr+4] = quantity & 0xFF;

    int rc = transact(conn, tx_buf, hdr + 5, rx_buf, sizeof(rx_buf));
    if (rc < 0) return rc;

    for (int i = 0; i < quantity; i++) {
        regs_out[i] = (rx_buf[9 + i*2] << 8) | rx_buf[10 + i*2];
    }
    return MBTCP_OK;
}

/* ─── FC05: Write Single Coil ─────────────────────────────────────── */

int mbtcp_write_single_coil(mbtcp_conn_t *conn,
                            uint16_t addr, int value)
{
    int hdr = build_mbap(conn, tx_buf, 5);
    tx_buf[hdr]   = 0x05;
    tx_buf[hdr+1] = (addr >> 8) & 0xFF;
    tx_buf[hdr+2] = addr & 0xFF;
    tx_buf[hdr+3] = value ? 0xFF : 0x00;
    tx_buf[hdr+4] = 0x00;

    return transact(conn, tx_buf, hdr + 5, rx_buf, sizeof(rx_buf)) < 0
           ? (int)conn->last_error : MBTCP_OK;
}

/* ─── FC06: Write Single Register ─────────────────────────────────── */

int mbtcp_write_single_reg(mbtcp_conn_t *conn,
                           uint16_t addr, uint16_t value)
{
    int hdr = build_mbap(conn, tx_buf, 5);
    tx_buf[hdr]   = 0x06;
    tx_buf[hdr+1] = (addr >> 8) & 0xFF;
    tx_buf[hdr+2] = addr & 0xFF;
    tx_buf[hdr+3] = (value >> 8) & 0xFF;
    tx_buf[hdr+4] = value & 0xFF;

    return transact(conn, tx_buf, hdr + 5, rx_buf, sizeof(rx_buf)) < 0
           ? (int)conn->last_error : MBTCP_OK;
}

/* ─── FC15: Write Multiple Coils ──────────────────────────────────── */

int mbtcp_write_multiple_coils(mbtcp_conn_t *conn,
                               uint16_t start_addr, uint16_t quantity,
                               const uint8_t *coils_in)
{
    uint8_t byte_count = (quantity + 7) / 8;
    uint16_t pdu_len = 6 + byte_count;  /* FC(1)+addr(2)+qty(2)+bc(1)+data */

    int hdr = build_mbap(conn, tx_buf, pdu_len);
    tx_buf[hdr]   = 0x0F;
    tx_buf[hdr+1] = (start_addr >> 8) & 0xFF;
    tx_buf[hdr+2] = start_addr & 0xFF;
    tx_buf[hdr+3] = (quantity >> 8) & 0xFF;
    tx_buf[hdr+4] = quantity & 0xFF;
    tx_buf[hdr+5] = byte_count;
    memcpy(&tx_buf[hdr+6], coils_in, byte_count);

    return transact(conn, tx_buf, hdr + pdu_len, rx_buf, sizeof(rx_buf)) < 0
           ? (int)conn->last_error : MBTCP_OK;
}

/* ─── FC16: Write Multiple Registers ──────────────────────────────── */

int mbtcp_write_multiple_regs(mbtcp_conn_t *conn,
                              uint16_t start_addr, uint16_t quantity,
                              const uint16_t *regs_in)
{
    uint8_t byte_count = quantity * 2;
    uint16_t pdu_len = 6 + byte_count;

    int hdr = build_mbap(conn, tx_buf, pdu_len);
    tx_buf[hdr]   = 0x10;
    tx_buf[hdr+1] = (start_addr >> 8) & 0xFF;
    tx_buf[hdr+2] = start_addr & 0xFF;
    tx_buf[hdr+3] = (quantity >> 8) & 0xFF;
    tx_buf[hdr+4] = quantity & 0xFF;
    tx_buf[hdr+5] = byte_count;
    for (int i = 0; i < quantity; i++) {
        tx_buf[hdr+6+i*2]   = (regs_in[i] >> 8) & 0xFF;
        tx_buf[hdr+6+i*2+1] = regs_in[i] & 0xFF;
    }

    return transact(conn, tx_buf, hdr + pdu_len, rx_buf, sizeof(rx_buf)) < 0
           ? (int)conn->last_error : MBTCP_OK;
}

#else /* Real hardware — lwIP sockets */

/*
 * Real AM2434 hardware using lwIP TCP/IP stack with CPSW Ethernet.
 *
 * Nova controller IP: 10.47.27.1 (static, air-gapped network)
 * Orbit boards:       10.47.27.{DIP_ID} (DIP = 2..63)
 * Port:               502 (standard Modbus TCP)
 *
 * Uses lwIP's POSIX-like socket API (lwip/sockets.h).
 * Thread safety is provided by a FreeRTOS mutex around transact().
 */

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/errno.h"

#include "FreeRTOS.h"
#include "semphr.h"

static uint8_t tx_buf[MBTCP_MAX_FRAME];
static uint8_t rx_buf[MBTCP_MAX_FRAME];
static xSemaphoreHandle modbus_mutex = NULL;

void mbtcp_init(mbtcp_conn_t *conn,
                uint8_t ip_a, uint8_t ip_b, uint8_t ip_c, uint8_t ip_d,
                uint16_t port, uint8_t unit_id)
{
    if (modbus_mutex == NULL) {
        modbus_mutex = xSemaphoreCreateMutex();
    }
    memset(conn, 0, sizeof(*conn));
    conn->socket_fd = -1;
    conn->unit_id = unit_id;
    conn->ip_addr = ((uint32_t)ip_a << 24) | ((uint32_t)ip_b << 16) |
                    ((uint32_t)ip_c << 8)  | (uint32_t)ip_d;
    conn->port = port;
    conn->timeout_ms = MBTCP_TIMEOUT_MS;
    conn->transaction_id = 0;
}

int mbtcp_connect(mbtcp_conn_t *conn)
{
    if (conn->socket_fd >= 0) return MBTCP_OK;  /* already connected */

    int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        debug_printf("[MBTCP] socket() failed: %d\r\n", errno);
        return MBTCP_ERR_SOCKET;
    }

    /* Set receive timeout so recv() doesn't block forever */
    struct timeval tv;
    tv.tv_sec  = conn->timeout_ms / 1000;
    tv.tv_usec = (conn->timeout_ms % 1000) * 1000;
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Set send timeout to prevent indefinite blocking on write */
    lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Disable Nagle algorithm — Modbus frames must go out immediately */
    int flag = 1;
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = lwip_htons(conn->port);
    addr.sin_addr.s_addr = lwip_htonl(conn->ip_addr);

    if (lwip_connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        debug_printf("[MBTCP] connect to %d.%d.%d.%d:%d failed: %d\r\n",
                     (conn->ip_addr >> 24) & 0xFF,
                     (conn->ip_addr >> 16) & 0xFF,
                     (conn->ip_addr >> 8) & 0xFF,
                     conn->ip_addr & 0xFF,
                     conn->port, errno);
        lwip_close(fd);
        return MBTCP_ERR_CONNECT;
    }

    conn->socket_fd = fd;
    debug_printf("[MBTCP] Connected to %d.%d.%d.%d:%d (fd=%d)\r\n",
                 (conn->ip_addr >> 24) & 0xFF,
                 (conn->ip_addr >> 16) & 0xFF,
                 (conn->ip_addr >> 8) & 0xFF,
                 conn->ip_addr & 0xFF,
                 conn->port, fd);
    return MBTCP_OK;
}

void mbtcp_disconnect(mbtcp_conn_t *conn)
{
    if (conn->socket_fd >= 0) {
        lwip_close(conn->socket_fd);
        conn->socket_fd = -1;
    }
}

int mbtcp_is_connected(mbtcp_conn_t *conn) { return conn->socket_fd >= 0; }

/* ─── Internal: build MBAP header ─────────────────────────────────── */

static int build_mbap(mbtcp_conn_t *conn, uint8_t *buf, uint16_t pdu_len)
{
    conn->transaction_id++;
    buf[0] = (conn->transaction_id >> 8) & 0xFF;
    buf[1] = conn->transaction_id & 0xFF;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = ((pdu_len + 1) >> 8) & 0xFF;
    buf[5] = (pdu_len + 1) & 0xFF;
    buf[6] = conn->unit_id;
    return MBTCP_MBAP_LEN;
}

/* ─── Internal: send frame and receive response (lwIP) ────────────── */

static int transact(mbtcp_conn_t *conn, uint8_t *frame, int frame_len,
                    uint8_t *resp, int resp_max)
{
    if (conn->socket_fd < 0) {
        conn->last_error = MBTCP_ERR_CONNECT;
        return MBTCP_ERR_CONNECT;
    }

    if (modbus_mutex != NULL) {
        xSemaphoreTake(modbus_mutex, portMAX_DELAY);
    }

    /* Send the Modbus TCP frame */
    int sent = lwip_send(conn->socket_fd, frame, frame_len, 0);
    if (sent != frame_len) {
        conn->err_count++;
        conn->last_error = MBTCP_ERR_SOCKET;
        debug_printf("[MBTCP] send failed: sent=%d expected=%d errno=%d\r\n",
                     sent, frame_len, errno);
        /* Socket is likely dead — close it so next call reconnects */
        mbtcp_disconnect(conn);
        if (modbus_mutex) xSemaphoreGive(modbus_mutex);
        return MBTCP_ERR_SOCKET;
    }
    conn->tx_count++;

    /* Phase 1: Read MBAP header (7 bytes) */
    int got = 0;
    while (got < MBTCP_MBAP_LEN) {
        int n = lwip_recv(conn->socket_fd, resp + got, MBTCP_MBAP_LEN - got, 0);
        if (n <= 0) {
            conn->err_count++;
            conn->last_error = (n == 0) ? MBTCP_ERR_SHORT_RESP : MBTCP_ERR_TIMEOUT;
            debug_printf("[MBTCP] recv header failed: got=%d n=%d errno=%d\r\n",
                         got, n, errno);
            if (n == 0) mbtcp_disconnect(conn);  /* peer closed */
            if (modbus_mutex) xSemaphoreGive(modbus_mutex);
            return (int)conn->last_error;
        }
        got += n;
    }

    /* Validate Modbus protocol ID (must be 0x0000) */
    if (resp[2] != 0x00 || resp[3] != 0x00) {
        conn->err_count++;
        conn->last_error = MBTCP_ERR_SHORT_RESP;
        debug_printf("[MBTCP] bad proto=%02X%02X\r\n", resp[2], resp[3]);
        mbtcp_disconnect(conn);
        if (modbus_mutex) xSemaphoreGive(modbus_mutex);
        return MBTCP_ERR_SHORT_RESP;
    }

    /* Extract body length from MBAP bytes 4-5 */
    int body_len = (resp[4] << 8) | resp[5];
    int total_len = 6 + body_len;

    if (total_len > resp_max || body_len < 1 || body_len > MBTCP_MAX_PDU + 1) {
        conn->err_count++;
        conn->last_error = MBTCP_ERR_SHORT_RESP;
        debug_printf("[MBTCP] bad body_len=%d\r\n", body_len);
        mbtcp_disconnect(conn);
        if (modbus_mutex) xSemaphoreGive(modbus_mutex);
        return MBTCP_ERR_SHORT_RESP;
    }

    /* Phase 2: Read body (unit_id + PDU) */
    while (got < total_len) {
        int n = lwip_recv(conn->socket_fd, resp + got, total_len - got, 0);
        if (n <= 0) {
            conn->err_count++;
            conn->last_error = (n == 0) ? MBTCP_ERR_SHORT_RESP : MBTCP_ERR_TIMEOUT;
            debug_printf("[MBTCP] recv body failed: got=%d/%d n=%d\r\n",
                         got, total_len, n);
            if (n == 0) mbtcp_disconnect(conn);
            if (modbus_mutex) xSemaphoreGive(modbus_mutex);
            return (int)conn->last_error;
        }
        got += n;
    }

    /* Check for Modbus exception response */
    if (resp[7] & 0x80) {
        conn->err_count++;
        conn->last_error = MBTCP_ERR_EXCEPTION;
        if (modbus_mutex) xSemaphoreGive(modbus_mutex);
        return MBTCP_ERR_EXCEPTION;
    }

    conn->rx_count++;
    if (modbus_mutex) xSemaphoreGive(modbus_mutex);
    return total_len;
}

/* ─── FC01: Read Coils ────────────────────────────────────────────── */

int mbtcp_read_coils(mbtcp_conn_t *conn,
                     uint16_t start_addr, uint16_t quantity,
                     uint8_t *coils_out)
{
    int hdr = build_mbap(conn, tx_buf, 5);
    tx_buf[hdr]   = 0x01;
    tx_buf[hdr+1] = (start_addr >> 8) & 0xFF;
    tx_buf[hdr+2] = start_addr & 0xFF;
    tx_buf[hdr+3] = (quantity >> 8) & 0xFF;
    tx_buf[hdr+4] = quantity & 0xFF;

    int rc = transact(conn, tx_buf, hdr + 5, rx_buf, sizeof(rx_buf));
    if (rc < 0) return rc;

    uint8_t byte_count = rx_buf[8];
    uint8_t max_bytes = (uint8_t)((quantity + 7) / 8);
    if (byte_count > max_bytes) byte_count = max_bytes;
    memcpy(coils_out, &rx_buf[9], byte_count);
    return MBTCP_OK;
}

/* ─── FC02: Read Discrete Inputs ──────────────────────────────────── */

int mbtcp_read_discrete_inputs(mbtcp_conn_t *conn,
                               uint16_t start_addr, uint16_t quantity,
                               uint8_t *inputs_out)
{
    int hdr = build_mbap(conn, tx_buf, 5);
    tx_buf[hdr]   = 0x02;
    tx_buf[hdr+1] = (start_addr >> 8) & 0xFF;
    tx_buf[hdr+2] = start_addr & 0xFF;
    tx_buf[hdr+3] = (quantity >> 8) & 0xFF;
    tx_buf[hdr+4] = quantity & 0xFF;

    int rc = transact(conn, tx_buf, hdr + 5, rx_buf, sizeof(rx_buf));
    if (rc < 0) return rc;

    uint8_t byte_count = rx_buf[8];
    uint8_t max_bytes = (uint8_t)((quantity + 7) / 8);
    if (byte_count > max_bytes) byte_count = max_bytes;
    memcpy(inputs_out, &rx_buf[9], byte_count);
    return MBTCP_OK;
}

/* ─── FC03: Read Holding Registers ────────────────────────────────── */

int mbtcp_read_holding_regs(mbtcp_conn_t *conn,
                            uint16_t start_addr, uint16_t quantity,
                            uint16_t *regs_out)
{
    int hdr = build_mbap(conn, tx_buf, 5);
    tx_buf[hdr]   = 0x03;
    tx_buf[hdr+1] = (start_addr >> 8) & 0xFF;
    tx_buf[hdr+2] = start_addr & 0xFF;
    tx_buf[hdr+3] = (quantity >> 8) & 0xFF;
    tx_buf[hdr+4] = quantity & 0xFF;

    int rc = transact(conn, tx_buf, hdr + 5, rx_buf, sizeof(rx_buf));
    if (rc < 0) return rc;

    for (int i = 0; i < quantity; i++) {
        regs_out[i] = (rx_buf[9 + i*2] << 8) | rx_buf[10 + i*2];
    }
    return MBTCP_OK;
}

/* ─── FC04: Read Input Registers ──────────────────────────────────── */

int mbtcp_read_input_regs(mbtcp_conn_t *conn,
                          uint16_t start_addr, uint16_t quantity,
                          uint16_t *regs_out)
{
    int hdr = build_mbap(conn, tx_buf, 5);
    tx_buf[hdr]   = 0x04;
    tx_buf[hdr+1] = (start_addr >> 8) & 0xFF;
    tx_buf[hdr+2] = start_addr & 0xFF;
    tx_buf[hdr+3] = (quantity >> 8) & 0xFF;
    tx_buf[hdr+4] = quantity & 0xFF;

    int rc = transact(conn, tx_buf, hdr + 5, rx_buf, sizeof(rx_buf));
    if (rc < 0) return rc;

    for (int i = 0; i < quantity; i++) {
        regs_out[i] = (rx_buf[9 + i*2] << 8) | rx_buf[10 + i*2];
    }
    return MBTCP_OK;
}

/* ─── FC05: Write Single Coil ─────────────────────────────────────── */

int mbtcp_write_single_coil(mbtcp_conn_t *conn,
                            uint16_t addr, int value)
{
    int hdr = build_mbap(conn, tx_buf, 5);
    tx_buf[hdr]   = 0x05;
    tx_buf[hdr+1] = (addr >> 8) & 0xFF;
    tx_buf[hdr+2] = addr & 0xFF;
    tx_buf[hdr+3] = value ? 0xFF : 0x00;
    tx_buf[hdr+4] = 0x00;

    return transact(conn, tx_buf, hdr + 5, rx_buf, sizeof(rx_buf)) < 0
           ? (int)conn->last_error : MBTCP_OK;
}

/* ─── FC06: Write Single Register ─────────────────────────────────── */

int mbtcp_write_single_reg(mbtcp_conn_t *conn,
                           uint16_t addr, uint16_t value)
{
    int hdr = build_mbap(conn, tx_buf, 5);
    tx_buf[hdr]   = 0x06;
    tx_buf[hdr+1] = (addr >> 8) & 0xFF;
    tx_buf[hdr+2] = addr & 0xFF;
    tx_buf[hdr+3] = (value >> 8) & 0xFF;
    tx_buf[hdr+4] = value & 0xFF;

    return transact(conn, tx_buf, hdr + 5, rx_buf, sizeof(rx_buf)) < 0
           ? (int)conn->last_error : MBTCP_OK;
}

/* ─── FC15: Write Multiple Coils ──────────────────────────────────── */

int mbtcp_write_multiple_coils(mbtcp_conn_t *conn,
                               uint16_t start_addr, uint16_t quantity,
                               const uint8_t *coils_in)
{
    uint8_t byte_count = (quantity + 7) / 8;
    uint16_t pdu_len = 6 + byte_count;

    int hdr = build_mbap(conn, tx_buf, pdu_len);
    tx_buf[hdr]   = 0x0F;
    tx_buf[hdr+1] = (start_addr >> 8) & 0xFF;
    tx_buf[hdr+2] = start_addr & 0xFF;
    tx_buf[hdr+3] = (quantity >> 8) & 0xFF;
    tx_buf[hdr+4] = quantity & 0xFF;
    tx_buf[hdr+5] = byte_count;
    memcpy(&tx_buf[hdr+6], coils_in, byte_count);

    return transact(conn, tx_buf, hdr + pdu_len, rx_buf, sizeof(rx_buf)) < 0
           ? (int)conn->last_error : MBTCP_OK;
}

/* ─── FC16: Write Multiple Registers ──────────────────────────────── */

int mbtcp_write_multiple_regs(mbtcp_conn_t *conn,
                              uint16_t start_addr, uint16_t quantity,
                              const uint16_t *regs_in)
{
    uint8_t byte_count = quantity * 2;
    uint16_t pdu_len = 6 + byte_count;

    int hdr = build_mbap(conn, tx_buf, pdu_len);
    tx_buf[hdr]   = 0x10;
    tx_buf[hdr+1] = (start_addr >> 8) & 0xFF;
    tx_buf[hdr+2] = start_addr & 0xFF;
    tx_buf[hdr+3] = (quantity >> 8) & 0xFF;
    tx_buf[hdr+4] = quantity & 0xFF;
    tx_buf[hdr+5] = byte_count;
    for (int i = 0; i < quantity; i++) {
        tx_buf[hdr+6+i*2]   = (regs_in[i] >> 8) & 0xFF;
        tx_buf[hdr+6+i*2+1] = regs_in[i] & 0xFF;
    }

    return transact(conn, tx_buf, hdr + pdu_len, rx_buf, sizeof(rx_buf)) < 0
           ? (int)conn->last_error : MBTCP_OK;
}

#endif /* QEMU_BUILD */
