/**
 * nova_protocol.c — Constellation binary protocol: COBS + CRC-16 framing
 *
 * Frame accumulation state machine:
 *   - Bytes are fed one at a time from UART ISR
 *   - 0x00 byte signals end of frame
 *   - Non-zero bytes accumulate into rx_frame_buf
 *   - On 0x00: COBS-decode → CRC-16 validate → callback with payload
 *
 * TX path:
 *   - Caller provides nanopb-encoded payload bytes
 *   - Append CRC-16 (big-endian) to payload
 *   - COBS-encode the payload+CRC
 *   - Append 0x00 delimiter
 *   - Send via registered tx_func
 */
#include "nova_protocol.h"
#include "nova_cobs.h"
#include "nova_crc16.h"
#include <string.h>

/* FreeRTOS — needed for the TX mutex that serializes NovaProto_SendRaw().
 * Without serialization, two tasks (e.g. ThreadUIUpdate periodic data and
 * the RX dispatcher's ACK reply path) interleave their COBS bytes through
 * the shared s_tx_pre_cobs / s_tx_scratch buffers and produce frames where
 * one envelope's prefix ends up embedded mid-payload of another.  The bridge
 * sees that as a CRC error and drops the frame. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

extern void debug_printf(const char *fmt, ...);

/* ─── State ───────────────────────────────────────────────────────────── */

static void (*s_tx_func)(const uint8_t *data, size_t len);
static NovaProto_RxCallback s_rx_callback;

/* RX accumulation buffer (COBS-encoded bytes, before decode) */
static uint8_t  s_rx_frame_buf[NOVA_MAX_FRAME_SIZE];
static size_t   s_rx_frame_pos;

/* Scratch buffers for encode/decode */
static uint8_t  s_decode_buf[NOVA_MAX_PAYLOAD_SIZE + 2]; /* +2 for CRC */
static uint8_t  s_tx_scratch[NOVA_MAX_FRAME_SIZE + 1];   /* +1 for delimiter */
static uint8_t  s_tx_pre_cobs[NOVA_MAX_PAYLOAD_SIZE + 2];/* payload + CRC, pre-COBS */

static NovaProto_Stats s_stats;

/* Serializes concurrent NovaProto_SendRaw() callers.  Created lazily on
 * first send so the order of NovaBridge_Init / scheduler-start does not
 * matter. */
static xSemaphoreHandle s_tx_mutex = NULL;

/* ─── Init ────────────────────────────────────────────────────────────── */

void NovaProto_Init(void (*tx_func)(const uint8_t *data, size_t len),
                    NovaProto_RxCallback rx_cb)
{
    s_tx_func = tx_func;
    s_rx_callback = rx_cb;
    s_rx_frame_pos = 0;
    memset(&s_stats, 0, sizeof(s_stats));
}

void NovaProto_SetRxCallback(NovaProto_RxCallback rx_cb)
{
    s_rx_callback = rx_cb;
}

/* ─── RX: byte-level accumulation ─────────────────────────────────────── */

static void process_frame(void)
{
    if (s_rx_frame_pos < 3) {
        /* Minimum frame: 1 byte payload + 2 bytes CRC = 3 bytes before COBS */
        return;
    }

    /* COBS-decode the accumulated bytes */
    size_t decoded_len = cobs_decode(s_decode_buf, s_rx_frame_buf, s_rx_frame_pos);
    if (decoded_len == 0) {
        s_stats.rx_cobs_errors++;
        extern void debug_printf(const char *fmt, ...);
        debug_printf("[NovaProto] COBS decode fail (pos=%u)\r\n",
                     (unsigned)s_rx_frame_pos);
        return;
    }

    if (decoded_len < 3) {
        /* Need at least 1 byte payload + 2 bytes CRC */
        s_stats.rx_cobs_errors++;
        return;
    }

    /* Extract CRC-16 from the last 2 bytes (big-endian) */
    size_t payload_len = decoded_len - 2;
    uint16_t rx_crc = ((uint16_t)s_decode_buf[payload_len] << 8) |
                       (uint16_t)s_decode_buf[payload_len + 1];

    /* Verify CRC over the payload bytes */
    uint16_t calc_crc = nova_crc16(s_decode_buf, payload_len);
    if (rx_crc != calc_crc) {
        s_stats.rx_crc_errors++;
        extern void debug_printf(const char *fmt, ...);
        debug_printf("[NovaProto] CRC mismatch rx=%04x calc=%04x len=%u\r\n",
                     rx_crc, calc_crc, (unsigned)payload_len);
        {
            size_t i;
            debug_printf("[NovaProto] decoded:");
            for (i = 0; i < decoded_len && i < 32; i++) {
                debug_printf(" %02x", s_decode_buf[i]);
            }
            debug_printf("\r\n");
        }
        return;
    }

    /* Valid frame — invoke callback with protobuf payload */
    s_stats.rx_frames++;
    if (s_rx_callback) {
        s_rx_callback(s_decode_buf, payload_len);
    }
}

void NovaProto_FeedByte(uint8_t byte)
{
    if (byte == 0x00) {
        /* End of frame delimiter */
        if (s_rx_frame_pos > 0) {
            process_frame();
        }
        s_rx_frame_pos = 0;
    } else {
        /* Accumulate COBS-encoded byte */
        if (s_rx_frame_pos < NOVA_MAX_FRAME_SIZE) {
            s_rx_frame_buf[s_rx_frame_pos++] = byte;
        } else {
            /* Frame overflow — discard and wait for next delimiter */
            s_stats.rx_overflows++;
            s_rx_frame_pos = 0;
        }
    }
}

void NovaProto_FeedBytes(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        NovaProto_FeedByte(data[i]);
    }
}

/* ─── TX: encode and send ─────────────────────────────────────────────── */

bool NovaProto_SendRaw(const uint8_t *payload, size_t payload_len)
{
    if (payload_len > NOVA_MAX_PAYLOAD_SIZE || !s_tx_func) {
        return false;
    }

    /* Lazy mutex creation.  Safe even if called before scheduler start —
     * xSemaphoreTake on the resulting mutex blocks indefinitely once the
     * scheduler runs, and is a no-op return-immediately before that. */
    if (s_tx_mutex == NULL) {
        s_tx_mutex = xSemaphoreCreateMutex();
    }

    /* Serialize entire encode + transmit so two tasks cannot interleave
     * bytes through the shared s_tx_pre_cobs / s_tx_scratch buffers.
     * portMAX_DELAY blocks indefinitely; the tx_func is bounded (per-byte
     * spin-wait timeout in hal_uart_send_char), so the only way to wedge
     * is a real deadlock or scheduler stall, which would already be fatal. */
    if (s_tx_mutex != NULL) {
        if (xSemaphoreTake(s_tx_mutex, portMAX_DELAY) != pdTRUE) {
            return false;
        }
    }

    /* Build the pre-COBS buffer: [payload][CRC16-hi][CRC16-lo].
     * Uses a file-scope static buffer (4KB) rather than stack — the
     * protocol layer is single-threaded (only ThreadUIUpdate sends),
     * and tasks with realistic stacks cannot afford this on-stack. */
    memcpy(s_tx_pre_cobs, payload, payload_len);

    uint16_t crc = nova_crc16(payload, payload_len);
    s_tx_pre_cobs[payload_len]     = (uint8_t)(crc >> 8);
    s_tx_pre_cobs[payload_len + 1] = (uint8_t)(crc & 0xFF);

    size_t pre_len = payload_len + 2;

    /* COBS-encode */
    size_t encoded_len = cobs_encode(s_tx_scratch, s_tx_pre_cobs, pre_len);

    /* Append 0x00 delimiter */
    s_tx_scratch[encoded_len] = 0x00;
    encoded_len++;

    /* Transmit */
    s_tx_func(s_tx_scratch, encoded_len);
    s_stats.tx_frames++;

    if (s_tx_mutex != NULL) {
        xSemaphoreGive(s_tx_mutex);
    }

    return true;
}

/* ─── Stats ───────────────────────────────────────────────────────────── */

const NovaProto_Stats* NovaProto_GetStats(void)
{
    return &s_stats;
}
