/**
 * novaCrc16.ts — CRC-16/CCITT for Constellation protocol framing
 *
 * Matches the firmware implementation in nova_crc16.c exactly.
 * Polynomial: 0x1021, Init: 0xFFFF
 */

/** Pre-computed CRC-16/CCITT lookup table */
const CRC16_TABLE = new Uint16Array(256);
(function buildTable() {
  for (let i = 0; i < 256; i++) {
    let crc = i << 8;
    for (let j = 0; j < 8; j++) {
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
      crc &= 0xFFFF;
    }
    CRC16_TABLE[i] = crc;
  }
})();

/**
 * Compute CRC-16/CCITT over a buffer.
 */
export function crc16(data: Buffer | Uint8Array): number {
  let crc = 0xFFFF;
  for (let i = 0; i < data.length; i++) {
    const idx = ((crc >> 8) ^ data[i]) & 0xFF;
    crc = ((crc << 8) ^ CRC16_TABLE[idx]) & 0xFFFF;
  }
  return crc;
}

/**
 * Incrementally update a CRC-16/CCITT value with one byte.
 */
export function crc16Update(crc: number, byte: number): number {
  const idx = ((crc >> 8) ^ byte) & 0xFF;
  return ((crc << 8) ^ CRC16_TABLE[idx]) & 0xFFFF;
}
