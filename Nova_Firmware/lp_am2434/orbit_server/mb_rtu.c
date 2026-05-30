/*
 * mb_rtu.c — Modbus RTU master core (transport-agnostic).
 */

#include "mb_rtu.h"

#include <string.h>

/* CRC-16/Modbus, table-free (≈ same speed as table at this size, no
 * .data footprint). Poly 0xA001 = bit-reverse of 0x8005. */
uint16_t mb_rtu_crc16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1u) crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            else          crc = (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static int read_n(const MbRtuTransport *t, uint8_t *buf, size_t n,
                  uint32_t timeout_ms)
{
    for (size_t i = 0; i < n; i++) {
        int rc = t->rx_byte_blocking(&buf[i], timeout_ms);
        if (rc != MB_RTU_OK) return rc;
    }
    return MB_RTU_OK;
}

int mb_rtu_read_holding(const MbRtuTransport *t,
                        uint8_t unit_id, uint16_t start_addr, uint16_t qty,
                        uint16_t *regs_out, uint32_t timeout_ms,
                        uint8_t *exc_out)
{
    if (t == NULL || regs_out == NULL || qty == 0 || qty > 125) {
        return MB_RTU_ERR_PARAM;
    }

    /* Build request: UnitID(1) FC(1) StartAddr(2) Qty(2) CRC(2) */
    uint8_t req[8];
    req[0] = unit_id;
    req[1] = 0x03;
    req[2] = (uint8_t)(start_addr >> 8);
    req[3] = (uint8_t)(start_addr & 0xFF);
    req[4] = (uint8_t)(qty >> 8);
    req[5] = (uint8_t)(qty & 0xFF);
    uint16_t crc = mb_rtu_crc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);    /* CRC low first on the wire */
    req[7] = (uint8_t)(crc >> 8);

    /* Half-duplex turnaround: drop any noise (echo from previous
     * transaction or line crosstalk) before driving. */
    t->rx_flush();
    t->tx_set_drive(true);
    int rc = t->tx_bytes(req, sizeof(req));
    t->tx_set_drive(false);
    if (rc != 0) return MB_RTU_ERR_TX;

    /* Read header — Unit + FC. The exception case has FC|0x80 + 1
     * byte exception code + 2-byte CRC (5 bytes total). The normal
     * response is Unit + FC + ByteCount + N data bytes + 2-byte CRC.
     * Read 3 bytes first to disambiguate. */
    uint8_t hdr[3];
    rc = read_n(t, hdr, 3, timeout_ms);
    if (rc != MB_RTU_OK) return rc;

    if (hdr[0] != unit_id) return MB_RTU_ERR_UNIT_MISMATCH;

    if (hdr[1] == (0x03 | 0x80)) {
        /* Exception: hdr[2] = exc code, then 2-byte CRC. */
        uint8_t crc_buf[2];
        rc = read_n(t, crc_buf, 2, timeout_ms);
        if (rc != MB_RTU_OK) return rc;
        uint8_t  full[5] = { hdr[0], hdr[1], hdr[2], crc_buf[0], crc_buf[1] };
        uint16_t want = mb_rtu_crc16(full, 3);
        uint16_t got  = (uint16_t)(crc_buf[0] | (crc_buf[1] << 8));
        if (want != got) return MB_RTU_ERR_CRC;
        if (exc_out) *exc_out = hdr[2];
        return MB_RTU_ERR_EXCEPTION;
    }

    if (hdr[1] != 0x03) return MB_RTU_ERR_FC_MISMATCH;

    uint8_t byte_count = hdr[2];
    if (byte_count != qty * 2u) return MB_RTU_ERR_LENGTH;

    /* Read data + CRC. */
    uint8_t body[125 * 2 + 2]; /* qty bounded to 125 */
    rc = read_n(t, body, (size_t)byte_count + 2u, timeout_ms);
    if (rc != MB_RTU_OK) return rc;

    /* Verify CRC over UnitID|FC|ByteCount|Data. */
    uint8_t check[3 + 125 * 2];
    check[0] = hdr[0]; check[1] = hdr[1]; check[2] = hdr[2];
    memcpy(&check[3], body, byte_count);
    uint16_t want = mb_rtu_crc16(check, 3u + byte_count);
    uint16_t got  = (uint16_t)(body[byte_count] | (body[byte_count + 1] << 8));
    if (want != got) return MB_RTU_ERR_CRC;

    /* Unpack registers (big-endian). */
    for (uint16_t i = 0; i < qty; i++) {
        regs_out[i] = (uint16_t)((body[i * 2] << 8) | body[i * 2 + 1]);
    }
    return MB_RTU_OK;
}
