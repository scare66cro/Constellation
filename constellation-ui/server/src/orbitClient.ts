/**
 * Orbit Modbus TCP Client — writes outputs to Orbit I/O boards via Modbus TCP.
 *
 * This runs inside the bridge server and talks Modbus TCP to:
 *   - Orbit simulators on ports 5502, 5503, etc.
 *   - Real Orbit boards at their IP addresses (10.47.27.2, 10.47.27.3, etc.)
 *
 * Matches Nova firmware behavior:
 *   - Write Multiple Coils (FC15) to set digital outputs
 *   - Read Discrete Inputs (FC02) for digital inputs
 *   - Polls periodically to maintain communication heartbeat
 *
 * Register/coil addresses match the Orbit firmware:
 *   Coils 0-9    : Digital Outputs (write via FC05/FC15)
 *   Coils 10-13  : DC24V Outputs (write via FC05/FC15)
 *   Inputs 0-9   : Digital Inputs (read via FC02)
 *   Input 10     : E-Stop state (read via FC02)
 *   Holdings 0-1 : Analog Outputs 0-10V (write via FC06/FC16)
 */

import ModbusRTU from 'modbus-serial';

// ── Types ──

export interface SensorBoardData {
  address: number;
  type: number;        // 0=IR, 1=Humid, 2=CO2, 3=Temp
  present: boolean;
  sensorTypes: number[];  // 4 type nibbles (0=IR,1=Humid,2=CO2,3=Temp,0xF=none)
  sensorValues: number[]; // 4 values: temp/humid ×10, CO2 raw ppm
}

export interface OrbitBoardSnapshot {
  id: number;
  host: string;
  port: number;
  online: boolean;
  digitalOutputs: boolean[];
  digitalInputs: boolean[];
  eStop: boolean;
  analogOutputs: number[];
  lastComm: number;
  commErrors: number;
  sensorBoards: SensorBoardData[];
}

function emptySnapshot(id: number, host: string, port: number): OrbitBoardSnapshot {
  return {
    id,
    host,
    port,
    online: false,
    digitalOutputs: new Array(10).fill(false),
    digitalInputs: new Array(10).fill(false),
    eStop: false,
    analogOutputs: [0, 0],
    lastComm: 0,
    commErrors: 0,
    sensorBoards: [],
  };
}

// ── Orbit Client ──

export interface OrbitClientOptions {
  /** Poll interval in ms (default 500) */
  pollIntervalMs?: number;
  /** Connection timeout in ms (default 1000) */
  timeoutMs?: number;
}

export interface OrbitBoardConfig {
  id: number;
  host: string;
  port: number;
}

export class OrbitClient {
  private boards: Map<number, OrbitBoardSnapshot> = new Map();
  private clients: Map<number, ModbusRTU> = new Map();
  private pollMs: number;
  private timeoutMs: number;
  private pollTimer: ReturnType<typeof setInterval> | null = null;
  private started = false;
  private updateCallbacks: Array<() => void> = [];
  private pendingOutputs: Map<number, boolean[]> = new Map();

  constructor(options: OrbitClientOptions = {}) {
    this.pollMs = options.pollIntervalMs ?? 500;
    this.timeoutMs = options.timeoutMs ?? 1000;
  }

  /** Add an orbit board to poll */
  addBoard(config: OrbitBoardConfig): void {
    if (!this.boards.has(config.id)) {
      this.boards.set(config.id, emptySnapshot(config.id, config.host, config.port));
      console.log(`[OrbitClient] Added board ${config.id} at ${config.host}:${config.port}`);
    }
  }

  /** Remove an orbit board */
  removeBoard(id: number): void {
    this.boards.delete(id);
    const client = this.clients.get(id);
    if (client) {
      client.close(() => {});
      this.clients.delete(id);
    }
  }

  /** Start polling all boards */
  async start(): Promise<void> {
    if (this.started) return;
    this.started = true;

    // Initial connection attempt for all boards
    for (const [id, board] of this.boards) {
      this.connectBoard(id, board).catch(() => {});
    }

    // Start poll loop
    this.pollTimer = setInterval(() => this.pollAll(), this.pollMs);
    console.log(`[OrbitClient] Started polling ${this.boards.size} board(s) every ${this.pollMs}ms`);
  }

  /** Stop polling */
  stop(): void {
    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
    for (const client of this.clients.values()) {
      client.close(() => {});
    }
    this.clients.clear();
    this.started = false;
  }

  /** Get snapshot of all boards */
  getBoards(): OrbitBoardSnapshot[] {
    return Array.from(this.boards.values());
  }

  /** Get snapshot of a specific board */
  getBoard(id: number): OrbitBoardSnapshot | undefined {
    return this.boards.get(id);
  }

  /** Register callback for state updates */
  onUpdate(cb: () => void): void {
    this.updateCallbacks.push(cb);
  }

  /** Queue digital output writes — will be sent on next poll cycle */
  setOutputs(boardId: number, outputs: boolean[]): void {
    this.pendingOutputs.set(boardId, outputs);
  }

  /** Write digital outputs immediately */
  async writeOutputs(boardId: number, outputs: boolean[]): Promise<boolean> {
    const board = this.boards.get(boardId);
    const client = this.clients.get(boardId);
    if (!board || !client || !client.isOpen) {
      return false;
    }

    try {
      // FC15: Write Multiple Coils starting at address 0
      await client.writeCoils(0, outputs.slice(0, 10));
      board.digitalOutputs = outputs.slice(0, 10);
      board.lastComm = Date.now();
      board.online = true;
      return true;
    } catch {
      board.commErrors++;
      return false;
    }
  }

  /** Write analog output (0-10V scaled to 0-1000) */
  async writeAnalogOutput(boardId: number, channel: number, value: number): Promise<boolean> {
    const board = this.boards.get(boardId);
    const client = this.clients.get(boardId);
    if (!board || !client || !client.isOpen || channel < 0 || channel > 1) {
      return false;
    }

    try {
      // FC06: Write Single Register
      await client.writeRegister(channel, Math.round(value));
      board.analogOutputs[channel] = value;
      board.lastComm = Date.now();
      board.online = true;
      return true;
    } catch {
      board.commErrors++;
      return false;
    }
  }

  /** Write an arbitrary holding register (FC06) */
  async writeHoldingRegister(boardId: number, addr: number, value: number): Promise<boolean> {
    const board = this.boards.get(boardId);
    if (!board) return false;
    let client = this.clients.get(boardId);
    // Lazy-reconnect for boards added after start() (e.g. via discovery scan).
    if (!client || !client.isOpen) {
      await this.connectBoard(boardId, board);
      client = this.clients.get(boardId);
      if (!client || !client.isOpen) return false;
    }

    try {
      // Always reassert the unit id — pollAll may have set a different one
      // on the same modbus-serial client between writes.
      client.setID(board.id);
      await client.writeRegister(addr, Math.round(value));
      board.lastComm = Date.now();
      board.online = true;
      return true;
    } catch (err) {
      board.commErrors++;
      console.warn(`[OrbitClient] writeHoldingRegister(board=${boardId}, addr=${addr}, val=${value}) failed:`, (err as Error).message);
      return false;
    }
  }

  // ── Private Methods ──

  private async connectBoard(id: number, board: OrbitBoardSnapshot): Promise<void> {
    let client = this.clients.get(id);
    
    if (client && client.isOpen) {
      return; // Already connected
    }

    // Create new client
    client = new ModbusRTU();
    client.setTimeout(this.timeoutMs);

    try {
      await client.connectTCP(board.host, { port: board.port });
      client.setID(board.id); // Unit ID matches board ID
      this.clients.set(id, client);
      board.online = true;
      board.lastComm = Date.now();
      console.log(`[OrbitClient] Connected to board ${id} at ${board.host}:${board.port}`);
    } catch (err) {
      board.online = false;
      board.commErrors++;
      // Don't log every failure to avoid spam
    }
  }

  private async pollAll(): Promise<void> {
    for (const [id, board] of this.boards) {
      await this.pollBoard(id, board);
    }
    this.notifyUpdate();
  }

  private async pollBoard(id: number, board: OrbitBoardSnapshot): Promise<void> {
    let client = this.clients.get(id);

    // Reconnect if needed
    if (!client || !client.isOpen) {
      await this.connectBoard(id, board);
      client = this.clients.get(id);
      if (!client || !client.isOpen) return;
    }

    try {
      client.setID(board.id);

      // Check for pending output writes first
      const pendingOuts = this.pendingOutputs.get(id);
      if (pendingOuts) {
        await client.writeCoils(0, pendingOuts.slice(0, 10));
        board.digitalOutputs = pendingOuts.slice(0, 10);
        this.pendingOutputs.delete(id);
      }

      // Read digital inputs (FC02: Read Discrete Inputs)
      // Address 0-10: 10 DIs + E-Stop
      const inputResult = await client.readDiscreteInputs(0, 11);
      if (inputResult.data) {
        for (let i = 0; i < 10 && i < inputResult.data.length; i++) {
          board.digitalInputs[i] = inputResult.data[i];
        }
        if (inputResult.data.length > 10) {
          board.eStop = inputResult.data[10];
        }
      }

      // Read sensor registers (FC03: Read Holding Registers 200+)
      // Reg 200 = board_count, then 7 regs per board
      try {
        const countResult = await client.readHoldingRegisters(200, 1);
        const boardCount = countResult.data?.[0] ?? 0;
        if (boardCount > 0 && boardCount <= 9) {
          const regCount = boardCount * 7;
          const sensorResult = await client.readHoldingRegisters(201, regCount);
          if (sensorResult.data && sensorResult.data.length >= regCount) {
            const boards: SensorBoardData[] = [];
            for (let bi = 0; bi < boardCount; bi++) {
              const base = bi * 7;
              const header = sensorResult.data[base];
              const typesPacked = sensorResult.data[base + 1];
              boards.push({
                address: header & 0x3F,
                type: (header >> 8) & 0xFF,
                present: !!(header & 0x80),
                sensorTypes: [
                  (typesPacked >> 12) & 0xF,
                  (typesPacked >> 8) & 0xF,
                  (typesPacked >> 4) & 0xF,
                  typesPacked & 0xF,
                ],
                sensorValues: [
                  sensorResult.data[base + 2],
                  sensorResult.data[base + 3],
                  sensorResult.data[base + 4],
                  sensorResult.data[base + 5],
                ],
              });
            }
            board.sensorBoards = boards;
          }
        }
      } catch {
        // Sensor read failure is non-critical — DI poll still succeeded
      }

      board.lastComm = Date.now();
      board.online = true;
    } catch {
      board.commErrors++;
      // Mark offline if too many errors
      if (board.commErrors > 5) {
        board.online = false;
        // Close and reconnect on next cycle
        client.close(() => {});
        this.clients.delete(id);
      }
    }
  }

  private notifyUpdate(): void {
    for (const cb of this.updateCallbacks) {
      try { cb(); } catch { /* ignore */ }
    }
  }
}
