/**
 * nova_cobs.c — COBS encoder/decoder for Constellation protocol
 *
 * Zero-allocation, no dependencies beyond stdint/stddef.
 * ~100 bytes of flash on Cortex-R5F.
 */
#include "nova_cobs.h"

size_t cobs_encode(uint8_t *dst, const uint8_t *src, size_t len)
{
    const uint8_t *end = src + len;
    uint8_t *code_ptr = dst;   /* pointer to the next code byte */
    uint8_t *out = dst + 1;    /* write position after the code byte */
    uint8_t code = 1;          /* distance to next zero (starts at 1) */

    while (src < end) {
        if (*src == 0x00) {
            /* Write the run length to the code byte */
            *code_ptr = code;
            code_ptr = out++;
            code = 1;
        } else {
            *out++ = *src;
            code++;
            if (code == 0xFF) {
                /* Maximum run length reached — emit and start new block */
                *code_ptr = code;
                code_ptr = out++;
                code = 1;
            }
        }
        src++;
    }

    /* Write the final code byte */
    *code_ptr = code;

    return (size_t)(out - dst);
}

size_t cobs_decode(uint8_t *dst, const uint8_t *src, size_t len)
{
    const uint8_t *end = src + len;
    uint8_t *out = dst;

    while (src < end) {
        uint8_t code = *src++;
        if (code == 0x00) {
            /* Zero code byte is invalid in COBS */
            return 0;
        }

        uint8_t run = code - 1;
        if ((size_t)(end - src) < run) {
            /* Not enough data for this run — corrupt frame */
            return 0;
        }

        /* Copy the non-zero data bytes */
        for (uint8_t i = 0; i < run; i++) {
            *out++ = *src++;
        }

        /* If this wasn't a 0xFF block, the implicit byte is 0x00 */
        if (code < 0xFF && src < end) {
            *out++ = 0x00;
        }
    }

    return (size_t)(out - dst);
}
