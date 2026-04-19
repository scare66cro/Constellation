/**
 * nova_crc16.h — CRC-16/CCITT for Constellation protocol framing
 *
 * Polynomial: 0x1021 (CRC-CCITT)
 * Init: 0xFFFF
 * Used by USB, HDLC, Bluetooth, and many industrial protocols.
 */
#ifndef NOVA_CRC16_H
#define NOVA_CRC16_H

#include <stdint.h>
#include <stddef.h>

/**
 * Compute CRC-16/CCITT over a buffer.
 *
 * @param data   Input data
 * @param len    Input length in bytes
 * @return       16-bit CRC value
 */
uint16_t nova_crc16(const uint8_t *data, size_t len);

/**
 * Incrementally update a CRC-16/CCITT value with one byte.
 * Use this for streaming CRC computation.
 *
 * @param crc    Current CRC value (init with 0xFFFF for first byte)
 * @param byte   Next data byte
 * @return       Updated CRC value
 */
uint16_t nova_crc16_update(uint16_t crc, uint8_t byte);

#endif /* NOVA_CRC16_H */
