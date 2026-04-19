/**
 * novaCobs.ts — COBS (Consistent Overhead Byte Stuffing) for the bridge server
 *
 * Matches the firmware implementation in nova_cobs.c exactly.
 * TypeScript implementation using Uint8Array for zero-copy performance.
 */

/**
 * Maximum encoded size for a given decoded size.
 */
export function cobsMaxEncodedSize(decodedLen: number): number {
  return decodedLen + Math.ceil(decodedLen / 254) + 1;
}

/**
 * COBS-encode data. Returns a new Buffer with the encoded bytes.
 * Does NOT include the trailing 0x00 delimiter — caller must append it.
 */
export function cobsEncode(src: Buffer | Uint8Array): Buffer {
  const dst = Buffer.alloc(cobsMaxEncodedSize(src.length));
  let codePtr = 0;
  let writePtr = 1;
  let code = 1;

  for (let i = 0; i < src.length; i++) {
    if (src[i] === 0x00) {
      dst[codePtr] = code;
      codePtr = writePtr++;
      code = 1;
    } else {
      dst[writePtr++] = src[i];
      code++;
      if (code === 0xFF) {
        dst[codePtr] = code;
        codePtr = writePtr++;
        code = 1;
      }
    }
  }

  dst[codePtr] = code;
  return dst.subarray(0, writePtr);
}

/**
 * COBS-decode data. Returns a new Buffer with the decoded bytes,
 * or null if the input is invalid.
 *
 * @param src  COBS-encoded bytes (NOT including the trailing 0x00 delimiter)
 */
export function cobsDecode(src: Buffer | Uint8Array): Buffer | null {
  const dst = Buffer.alloc(src.length);
  let readPtr = 0;
  let writePtr = 0;

  while (readPtr < src.length) {
    const code = src[readPtr++];
    if (code === 0x00) return null; // Invalid

    const runLen = code - 1;
    if (readPtr + runLen > src.length) return null; // Truncated

    for (let i = 0; i < runLen; i++) {
      dst[writePtr++] = src[readPtr++];
    }

    if (code < 0xFF && readPtr < src.length) {
      dst[writePtr++] = 0x00;
    }
  }

  return dst.subarray(0, writePtr);
}
