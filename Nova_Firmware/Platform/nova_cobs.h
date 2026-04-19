/**
 * nova_cobs.h — COBS (Consistent Overhead Byte Stuffing) for Constellation
 *
 * COBS encodes arbitrary data so that zero bytes never appear in the output,
 * allowing 0x00 to serve as an unambiguous packet delimiter. Overhead is at
 * most 1 byte per 254 bytes of input (~0.4%).
 *
 * Wire format:  [COBS-encoded payload] [0x00 delimiter]
 *
 * References:
 *   Cheshire & Baker, "Consistent Overhead Byte Stuffing", IEEE/ACM 1999
 */
#ifndef NOVA_COBS_H
#define NOVA_COBS_H

#include <stdint.h>
#include <stddef.h>

/**
 * Maximum encoded size for a given decoded size.
 * COBS adds at most 1 byte per 254 input bytes, plus 1 byte overhead.
 */
#define COBS_MAX_ENCODED_SIZE(decoded_len) ((decoded_len) + ((decoded_len) / 254) + 1)

/**
 * Encode data using COBS.
 *
 * @param dst     Output buffer (must be at least COBS_MAX_ENCODED_SIZE(len) bytes)
 * @param src     Input data (may contain zero bytes)
 * @param len     Input data length
 * @return        Number of bytes written to dst (excluding the trailing 0x00 delimiter)
 *
 * The caller must append a 0x00 byte after the returned length to complete the frame.
 */
size_t cobs_encode(uint8_t *dst, const uint8_t *src, size_t len);

/**
 * Decode COBS-encoded data.
 *
 * @param dst     Output buffer (must be at least len bytes)
 * @param src     COBS-encoded input (NOT including the trailing 0x00 delimiter)
 * @param len     Encoded input length
 * @return        Number of decoded bytes, or 0 on error (invalid COBS data)
 */
size_t cobs_decode(uint8_t *dst, const uint8_t *src, size_t len);

#endif /* NOVA_COBS_H */
