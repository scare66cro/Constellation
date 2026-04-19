/**
 * nova_protocol.h — Constellation binary protocol layer
 *
 * Wire format: [COBS-encoded frame][0x00 delimiter]
 * Frame contents (before COBS): [payload bytes][CRC16-hi][CRC16-lo]
 * Payload: nanopb-encoded agristar.Envelope protobuf message
 *
 * This layer handles:
 *   - Frame accumulation from UART byte stream
 *   - COBS decode/encode
 *   - CRC-16 validation/generation
 *   - nanopb Envelope encode/decode
 *
 * Usage:
 *   TX: NovaProto_Send(envelope_ptr)  → encodes + sends via UART
 *   RX: NovaProto_FeedByte(byte)      → accumulates, fires callback on complete msg
 */
#ifndef NOVA_PROTOCOL_H
#define NOVA_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Maximum payload size (protobuf-encoded Envelope, before COBS) */
#define NOVA_MAX_PAYLOAD_SIZE   4096

/* Maximum wire frame size (COBS-encoded + CRC16 + delimiter) */
#define NOVA_MAX_FRAME_SIZE     (NOVA_MAX_PAYLOAD_SIZE + (NOVA_MAX_PAYLOAD_SIZE / 254) + 4)

/* Protocol version — bump only on breaking framing changes */
#define NOVA_PROTOCOL_VERSION   1

/* ─── Forward declarations ────────────────────────────────────────────── */

/* Callback when a valid Envelope is received */
typedef void (*NovaProto_RxCallback)(const uint8_t *payload, size_t payload_len);

/* ─── Initialization ──────────────────────────────────────────────────── */

/**
 * Initialize the protocol layer.
 *
 * @param tx_func    Function to transmit raw bytes over UART
 * @param rx_cb      Callback invoked when a complete, CRC-valid payload arrives
 */
void NovaProto_Init(void (*tx_func)(const uint8_t *data, size_t len),
                    NovaProto_RxCallback rx_cb);

/* ─── Receiving ───────────────────────────────────────────────────────── */

/**
 * Feed one byte from the UART RX interrupt to the protocol layer.
 * Call this from the UART ISR or DMA completion handler.
 *
 * When a complete frame is accumulated (0x00 delimiter found), the layer
 * COBS-decodes it, validates CRC-16, and if valid, invokes the rx_cb
 * with the raw protobuf payload bytes.
 *
 * @param byte   Next byte from UART
 */
void NovaProto_FeedByte(uint8_t byte);

/**
 * Feed multiple bytes from a UART DMA buffer.
 * Equivalent to calling NovaProto_FeedByte() for each byte.
 */
void NovaProto_FeedBytes(const uint8_t *data, size_t len);

/* ─── Transmitting ────────────────────────────────────────────────────── */

/**
 * Encode and transmit a protobuf payload.
 *
 * @param payload      nanopb-encoded Envelope bytes
 * @param payload_len  Length of the encoded payload
 * @return             true if sent successfully, false if payload too large
 *
 * The function appends CRC-16, COBS-encodes the result, appends the 0x00
 * delimiter, and calls the tx_func registered in NovaProto_Init().
 */
bool NovaProto_SendRaw(const uint8_t *payload, size_t payload_len);

/**
 * Update the RX callback after initialization.
 * Allows nova_dataexc to register its envelope dispatcher.
 */
void NovaProto_SetRxCallback(NovaProto_RxCallback rx_cb);

/* ─── Statistics ──────────────────────────────────────────────────────── */

typedef struct {
    uint32_t tx_frames;       /* Total frames transmitted */
    uint32_t rx_frames;       /* Total valid frames received */
    uint32_t rx_crc_errors;   /* CRC validation failures */
    uint32_t rx_cobs_errors;  /* COBS decode failures */
    uint32_t rx_overflows;    /* Frame too large for buffer */
} NovaProto_Stats;

/**
 * Get protocol statistics (for diagnostics/heartbeat).
 */
const NovaProto_Stats* NovaProto_GetStats(void);

#endif /* NOVA_PROTOCOL_H */
