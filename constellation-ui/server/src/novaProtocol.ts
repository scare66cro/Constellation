/**
 * novaProtocol.ts — Constellation binary protocol framing layer
 *
 * Replaces protocol.ts (ASCII ^tag=value$CRC! framing) with
 * COBS + CRC-16 binary framing for protobuf messages.
 *
 * Wire format: [COBS-encoded frame][0x00 delimiter]
 * Frame contents (before COBS): [protobuf payload][CRC16-hi][CRC16-lo]
 *
 * Usage:
 *   TX: NovaProtocol.buildFrame(payload) → Buffer ready for UART
 *   RX: NovaProtocol.feedBytes(data) → emits 'message' with decoded payload
 */

import { EventEmitter } from 'events';
import { cobsEncode, cobsDecode } from './novaCobs.js';
import { crc16 } from './novaCrc16.js';

export const NOVA_MAX_PAYLOAD_SIZE = 4096;
export const NOVA_PROTOCOL_VERSION = 1;

export interface NovaProtocolStats {
  txFrames: number;
  rxFrames: number;
  rxCrcErrors: number;
  rxCobsErrors: number;
  rxOverflows: number;
}

/**
 * NovaProtocol — COBS + CRC-16 frame layer.
 *
 * Events:
 *   'message' (payload: Buffer) — emitted when a valid, CRC-checked protobuf
 *     payload is extracted from the byte stream.
 *   'error' (err: Error) — emitted on framing errors (CRC mismatch, COBS decode fail)
 */
export class NovaProtocol extends EventEmitter {
  private rxBuf: Buffer;
  private rxPos: number;
  private stats: NovaProtocolStats;

  constructor() {
    super();
    // Generous buffer — COBS overhead is minimal
    this.rxBuf = Buffer.alloc(NOVA_MAX_PAYLOAD_SIZE * 2);
    this.rxPos = 0;
    this.stats = {
      txFrames: 0,
      rxFrames: 0,
      rxCrcErrors: 0,
      rxCobsErrors: 0,
      rxOverflows: 0,
    };
  }

  /**
   * Build a wire frame from a protobuf-encoded payload.
   * Returns a Buffer ready to write to the serial port.
   *
   * Format: COBS(payload + CRC16) + 0x00
   */
  buildFrame(payload: Buffer): Buffer {
    // Append CRC-16 (big-endian) to payload
    const crcVal = crc16(payload);
    const withCrc = Buffer.alloc(payload.length + 2);
    payload.copy(withCrc, 0);
    withCrc[payload.length] = (crcVal >> 8) & 0xFF;
    withCrc[payload.length + 1] = crcVal & 0xFF;

    // COBS-encode
    const encoded = cobsEncode(withCrc);

    // Append 0x00 delimiter
    const frame = Buffer.alloc(encoded.length + 1);
    encoded.copy(frame, 0);
    frame[encoded.length] = 0x00;

    this.stats.txFrames++;
    return frame;
  }

  /**
   * Feed raw bytes from the serial port into the protocol layer.
   * When a complete frame is found (0x00 delimiter), it's decoded,
   * CRC-checked, and emitted as a 'message' event.
   */
  feedBytes(data: Buffer): void {
    for (let i = 0; i < data.length; i++) {
      const byte = data[i];

      if (byte === 0x00) {
        // End of frame delimiter
        if (this.rxPos > 0) {
          this.processFrame();
        }
        this.rxPos = 0;
      } else {
        // Accumulate COBS-encoded byte
        if (this.rxPos < this.rxBuf.length) {
          this.rxBuf[this.rxPos++] = byte;
        } else {
          // Overflow — discard
          this.stats.rxOverflows++;
          this.rxPos = 0;
        }
      }
    }
  }

  getStats(): NovaProtocolStats {
    return { ...this.stats };
  }

  /**
   * Process a complete COBS-encoded frame.
   */
  private processFrame(): void {
    const encoded = this.rxBuf.subarray(0, this.rxPos);

    // COBS-decode
    const decoded = cobsDecode(encoded);
    if (!decoded) {
      this.stats.rxCobsErrors++;
      this.emit('error', new Error('COBS decode failed'));
      return;
    }

    if (decoded.length < 3) {
      // Need at least 1 byte payload + 2 bytes CRC
      this.stats.rxCobsErrors++;
      return;
    }

    // Extract CRC-16 from last 2 bytes (big-endian)
    const payloadLen = decoded.length - 2;
    const rxCrc = (decoded[payloadLen] << 8) | decoded[payloadLen + 1];
    const payload = decoded.subarray(0, payloadLen);

    // Verify CRC
    const calcCrc = crc16(payload);
    if (rxCrc !== calcCrc) {
      this.stats.rxCrcErrors++;
      this.emit('error', new Error(`CRC mismatch: expected ${calcCrc.toString(16)}, got ${rxCrc.toString(16)}`));
      return;
    }

    // Valid frame — emit the protobuf payload
    this.stats.rxFrames++;
    this.emit('message', Buffer.from(payload));
  }
}
