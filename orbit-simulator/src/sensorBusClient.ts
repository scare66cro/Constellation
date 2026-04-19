/**
 * Sensor Bus Client — Orbit's RS-485 Bus Abstraction
 * ───────────────────────────────────────────────────
 * Manages a Modbus RTU connection to the sensor board simulator,
 * polls each discovered board for raw ADC data, converts to engineering
 * values, and updates the Orbit's internal sensor register block.
 *
 * This replaces the static `analogBoards` array in OrbitState with
 * live-polled data from real (simulated) sensor board slaves.
 */

import { ModbusRtuClient } from './modbusRtuClient.js';
import {
  adcToOrbitRegister,
  SENSOR_VAL_UNDEF,
  SENSOR_TYPE_NONE,
} from './adcConversion.js';
import { EventEmitter } from 'events';

// ─── Types ─────────────────────────────────────────────────────────────────

export interface SensorBoardData {
  address: number;         // RS-485 address (0-31)
  present: boolean;
  boardType: number;       // High byte from HR 0
  firmwareMajor: number;   // Low byte from HR 0
  firmwareMinor: number;   // High byte from HR 1
  disabled: boolean;       // Low byte bit 0 from HR 1
  sensorTypes: [number, number, number, number];    // From HR 2 (4-bit nibbles)
  rawAdc: [number, number, number, number];          // Raw ADC × 16 from HR 3-6
  engineeringValues: [number, number, number, number]; // Values for Orbit TCP regs
}

export interface SensorBusConfig {
  host: string;
  port: number;
  /** Board addresses to poll (unit ID = address + 1) */
  boardAddresses: number[];
  /** Poll interval in ms (default: 1000) */
  pollInterval?: number;
  /** RTU response timeout in ms (default: 2000) */
  timeout?: number;
}

// ─── Sensor Bus Client ─────────────────────────────────────────────────────

export class SensorBusClient extends EventEmitter {
  private client: ModbusRtuClient;
  private config: Required<SensorBusConfig>;
  private boards = new Map<number, SensorBoardData>();
  private pollTimer: ReturnType<typeof setInterval> | null = null;
  private polling = false;
  private connected = false;

  constructor(config: SensorBusConfig) {
    super();
    this.config = {
      pollInterval: 1000,
      timeout: 2000,
      ...config,
    };

    this.client = new ModbusRtuClient({
      host: config.host,
      port: config.port,
      timeout: this.config.timeout,
      reconnect: true,
      reconnectInterval: 3000,
    });

    this.client.on('connected', () => {
      this.connected = true;
      console.log(`[SensorBus] Connected to RTU server ${config.host}:${config.port}`);
      this.emit('connected');
    });

    this.client.on('disconnected', () => {
      this.connected = false;
      console.log('[SensorBus] Disconnected from RTU server');
      this.emit('disconnected');
    });

    this.client.on('error', (err: Error) => {
      this.emit('error', err);
    });
  }

  get isConnected(): boolean {
    return this.connected;
  }

  get boardCount(): number {
    return this.boards.size;
  }

  /** All discovered boards. */
  getBoards(): SensorBoardData[] {
    return Array.from(this.boards.values());
  }

  /** Get a specific board by address. */
  getBoard(address: number): SensorBoardData | undefined {
    return this.boards.get(address);
  }

  /** Start connecting and polling. */
  async start(): Promise<void> {
    try {
      await this.client.connect();
    } catch {
      console.log('[SensorBus] Initial connect failed — will retry');
    }

    this.pollTimer = setInterval(() => this.pollAll(), this.config.pollInterval);
  }

  /** Stop polling and disconnect. */
  stop(): void {
    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
    this.client.disconnect();
  }

  /** Update the list of board addresses to poll. */
  setBoardAddresses(addresses: number[]): void {
    this.config.boardAddresses = addresses;
    // Remove boards that are no longer in the list
    for (const addr of this.boards.keys()) {
      if (!addresses.includes(addr)) {
        this.boards.delete(addr);
      }
    }
  }

  // ─── Polling ──────────────────────────────────────────────────────────

  private async pollAll(): Promise<void> {
    if (this.polling || !this.connected) return;
    this.polling = true;

    for (const addr of this.config.boardAddresses) {
      try {
        await this.pollBoard(addr);
      } catch (err) {
        // Mark board as not present on error
        const existing = this.boards.get(addr);
        if (existing) {
          existing.present = false;
        }
        this.emit('poll-error', { address: addr, error: (err as Error).message });
      }
    }

    this.polling = false;
    this.emit('poll-complete', this.boards.size);
  }

  private async pollBoard(address: number): Promise<void> {
    const unitId = address + 1;

    // Read 7 holding registers (HR 0-6) from the sensor board
    const regs = await this.client.readHoldingRegisters(unitId, 0, 7);

    // Parse register values
    const boardType = (regs[0] >> 8) & 0xFF;
    const fwMajor = regs[0] & 0xFF;
    const fwMinor = (regs[1] >> 8) & 0xFF;
    const disabled = (regs[1] & 0x01) !== 0;
    const packedTypes = regs[2];

    const sensorTypes: [number, number, number, number] = [
      (packedTypes >> 12) & 0xF,
      (packedTypes >> 8) & 0xF,
      (packedTypes >> 4) & 0xF,
      packedTypes & 0xF,
    ];

    const rawAdc: [number, number, number, number] = [
      regs[3], regs[4], regs[5], regs[6],
    ];

    // Convert ADC → engineering values for Orbit's TCP register block
    const engineeringValues: [number, number, number, number] = [
      adcToOrbitRegister(rawAdc[0], sensorTypes[0]),
      adcToOrbitRegister(rawAdc[1], sensorTypes[1]),
      adcToOrbitRegister(rawAdc[2], sensorTypes[2]),
      adcToOrbitRegister(rawAdc[3], sensorTypes[3]),
    ];

    const data: SensorBoardData = {
      address,
      present: !disabled,
      boardType,
      firmwareMajor: fwMajor,
      firmwareMinor: fwMinor,
      disabled,
      sensorTypes,
      rawAdc,
      engineeringValues,
    };

    this.boards.set(address, data);
  }
}
