/**
 * Django Cloud Sync Module
 *
 * Syncs controller data from the bridge to Django backend,
 * replacing the need for iotclient.service → Azure IoT Hub → Azure Functions.
 *
 * This enables:
 * - Direct bridge → Django communication
 * - No Azure IoT Hub dependency
 * - Simpler architecture for production
 */

import { createWriteStream } from 'fs';
import { pipeline } from 'stream/promises';
import type { DataCache } from './dataCache.js';
import type { UpgradeManager } from './upgradeManager.js';

/** Function to send a POST command to the ARM firmware */
type CommandExecutor = (body: string) => Promise<string>;

interface DjangoSyncConfig {
  /** Django backend URL (e.g., 'http://localhost:8000') */
  djangoUrl: string;
  /** IoTClient token for authentication */
  token?: string;
  /** IoTClient device ID (UUID) */
  deviceId?: string;
  /** Sync interval in milliseconds (default: 30000 = 30s) */
  syncInterval?: number;
  /** Enable sync (default: false for local dev) */
  enabled?: boolean;
  /** Command executor function (serialBridge.sendPost) */
  commandExecutor?: CommandExecutor;
}

export class DjangoSync {
  private config: DjangoSyncConfig;
  private cache: DataCache;
  private intervalId: NodeJS.Timeout | null = null;
  private lastSyncTime = 0;
  private syncCount = 0;
  private errorCount = 0;
  private commandExecutor: CommandExecutor | null = null;
  private upgradeManager: UpgradeManager | null = null;

  constructor(cache: DataCache, config: DjangoSyncConfig) {
    this.cache = cache;
    this.config = {
      syncInterval: 30000,
      enabled: false,
      ...config,
    };
    this.commandExecutor = config.commandExecutor || null;
  }

  /**
   * Set the command executor (called after serialBridge is created)
   */
  setCommandExecutor(executor: CommandExecutor): void {
    this.commandExecutor = executor;
    console.log('[DjangoSync] Command executor registered');
  }

  /**
   * Set the upgrade manager for cloud-triggered upgrades
   */
  setUpgradeManager(manager: UpgradeManager): void {
    this.upgradeManager = manager;
    console.log('[DjangoSync] Upgrade manager registered');
  }

  /**
   * Start periodic sync to Django
   */
  start(): void {
    if (!this.config.enabled) {
      console.log('[DjangoSync] Sync disabled (DJANGO_SYNC_ENABLED not set)');
      return;
    }

    if (!this.config.token && !this.config.deviceId) {
      console.error('[DjangoSync] No token or deviceId configured');
      return;
    }

    console.log(`[DjangoSync] Starting sync to ${this.config.djangoUrl}`);
    console.log(`[DjangoSync] Interval: ${this.config.syncInterval}ms`);
    console.log(`[DjangoSync] Auth: ${this.config.token ? 'token' : 'deviceId'}`);

    // Initial sync
    this.syncAndProcess();

    // Periodic sync
    this.intervalId = setInterval(() => {
      this.syncAndProcess();
    }, this.config.syncInterval!);
  }

  /**
   * Sync data to Django and process pending commands
   */
  private async syncAndProcess(): Promise<void> {
    // Sync controller data to Django
    await this.sync();
    
    // Fetch and execute pending commands
    if (this.commandExecutor) {
      await this.processCommands();
    }
  }

  /**
   * Stop periodic sync
   */
  stop(): void {
    if (this.intervalId) {
      clearInterval(this.intervalId);
      this.intervalId = null;
      console.log('[DjangoSync] Stopped');
    }
  }

  /**
   * Get all CGI data from cache as a payload object
   */
  private buildPayload(): Record<string, unknown> {
    const payload: Record<string, unknown> = {};

    // Map varNames to their values
    const varNames = [
      'MainData', 'PgmData', 'DailyFanRun', 'TotalFanRun', 'OutsideAirData',
      'RunTimesData', 'FreqCtrlData', 'RampRateData', 'HumidCtrlData',
      'Co2PurgeData', 'EquipStatusData', 'MiscData', 'P2BasicSetupData',
      'P2Password', 'P2AnalogBoardData', 'P2FreshAirData', 'P2RefrigData',
      'P2ClimaCellData', 'FailureData1', 'FailureData2', 'CurrentMode',
      'PlenTempDevData', 'DateTimeData', 'P2ServiceData', 'UserLogSettings',
      'AlarmData', 'OutputConfigData', 'InputConfigData', 'AvailableIoData',
      'AirCureData', 'SysVersions', 'BoardType', 'ControllerList',
      'ClimaCellTimesData', 'LoadMonitorData', 'P2BurnerData', 'PileTempsData',
      'PileHumidsData', 'PileTempsLabels', 'PileHumidsLabels',
    ];

    for (const varName of varNames) {
      const entry = this.cache.getByVarName(varName);
      if (entry?.value) {
        // Parse comma-separated values into array
        payload[varName] = entry.value.split(',');
      } else {
        // Always include the key so the PWA's defensive `arr?.[i]` checks
        // see an array (returning undefined per index) instead of indexing
        // into `undefined` and throwing a render-time TypeError. The cache
        // may not yet have been populated by the ARM (slow boot, missing
        // CGI emit, etc); shipping [] is the safest default.
        payload[varName] = [];
      }
    }

    // IoNames is no longer cached as a CSV string. The Azure storage PWA
    // still expects the legacy "Name:Mode:IoType:Renamable:Index,..." shape,
    // so synthesise it on demand from the structured IoEntry[]. When the
    // PWA migrates to the structured format, drop this branch (and remove
    // getIoNamesCsv()).
    const ioCsv = this.cache.getIoNamesCsv();
    payload['IoNames'] = ioCsv ? ioCsv.split(',') : [];

    // Temperature-unit normalization.
    //
    // The legacy storage PWA uses P2BasicSetupData[1] as "what unit the
    // payload values are already in" (0 = Fahrenheit, 1 = Celsius) and
    // only converts values whose unit differs from the user preference.
    //
    // The Nova firmware now emits both setpoints AND MainData/PileTemps
    // in the user-selected TempType (ReadAnalogBoards converts orbit's
    // wire-format °C into the configured unit before storing in
    // Sensor.Value), so no conversion is needed in the bridge.

    // Add bridge metadata
    payload['BridgeVersion'] = '1.0.0';
    payload['Protocol'] = 'bridge-direct';
    // Announce ourselves as a v2.0.0+ IoT client. The bridge implements the
    // modern controller contract (sync push, /api/bridge/command), so the PWA
    // should treat it as such — this enables is102plus / is200plus features
    // (Advanced/Level-2 gear FAB, short polling timeouts, etc).
    payload['IoTClientVersion'] = '2.0.0';

    return payload;
  }

  /**
   * Perform a single sync to Django
   */
  async sync(): Promise<boolean> {
    if (!this.config.enabled) return false;

    const payload = this.buildPayload();

    // Skip if no meaningful data
    if (!payload['MainData'] || (payload['MainData'] as string[]).length < 5) {
      // console.log('[DjangoSync] Skipping sync - no MainData');
      return false;
    }

    const body = JSON.stringify({
      timestamp: new Date().toISOString(),
      payload,
    });

    const headers: Record<string, string> = {
      'Content-Type': 'application/json',
    };
    
    // Use Authorization header for token auth
    if (this.config.token) {
      headers['Authorization'] = `Token ${this.config.token}`;
    }

    try {
      const response = await fetch(`${this.config.djangoUrl}/api/bridge/sync/`, {
        method: 'POST',
        headers,
        body,
      });

      if (!response.ok) {
        const text = await response.text();
        console.error(`[DjangoSync] Error: ${response.status} - ${text}`);
        this.errorCount++;
        return false;
      }

      const result = await response.json();
      this.lastSyncTime = Date.now();
      this.syncCount++;

      if (this.syncCount % 10 === 0 || this.syncCount === 1) {
        console.log(`[DjangoSync] Synced #${this.syncCount} → ${result.updatedAt}`);
      }

      return true;
    } catch (err) {
      console.error('[DjangoSync] Fetch error:', err);
      this.errorCount++;
      return false;
    }
  }

  /**
   * Check for pending commands from Django
   */
  async fetchCommands(): Promise<Array<{ id: number; method: string; payload: unknown }>> {
    if (!this.config.enabled) return [];

    const headers: Record<string, string> = {};
    if (this.config.token) {
      headers['Authorization'] = `Token ${this.config.token}`;
    }

    try {
      const response = await fetch(
        `${this.config.djangoUrl}/api/bridge/commands/`,
        { method: 'GET', headers }
      );

      if (!response.ok) {
        return [];
      }

      const result = await response.json();
      return result.commands || [];
    } catch (err) {
      console.error('[DjangoSync] Command fetch error:', err);
      return [];
    }
  }

  /**
   * Process pending commands from Django
   */
  async processCommands(): Promise<void> {
    if (!this.commandExecutor) return;

    const commands = await this.fetchCommands();
    if (commands.length === 0) return;

    console.log(`[DjangoSync] Processing ${commands.length} pending commands`);

    for (const cmd of commands) {
      try {
        // Handle upgrade commands separately
        if (cmd.method === 'upgrade') {
          await this.handleUpgradeCommand(cmd);
          continue;
        }
        
        const postBody = this.translateCommand(cmd);
        if (postBody) {
          console.log(`[DjangoSync] Executing command #${cmd.id}: ${postBody.substring(0, 50)}...`);
          const result = await this.commandExecutor(postBody);
          await this.acknowledgeCommand(cmd.id, true, result);
        } else {
          console.warn(`[DjangoSync] Unknown command method: ${cmd.method}`);
          await this.acknowledgeCommand(cmd.id, false, 'Unknown command method');
        }
      } catch (err) {
        console.error(`[DjangoSync] Command #${cmd.id} failed:`, err);
        await this.acknowledgeCommand(cmd.id, false, String(err));
      }
    }
  }

  /**
   * Handle cloud-triggered upgrade command
   */
  private async handleUpgradeCommand(cmd: { id: number; method: string; payload: unknown }): Promise<void> {
    const payload = cmd.payload as Record<string, unknown>;
    const version = payload['upgrade'] as string;
    
    console.log(`[DjangoSync] Upgrade command #${cmd.id} for version: ${version}`);
    
    if (!this.upgradeManager) {
      console.warn('[DjangoSync] No upgrade manager registered, skipping upgrade');
      await this.acknowledgeCommand(cmd.id, false, 'Upgrade manager not available');
      return;
    }
    
    try {
      // Get upgrade info from Django
      const headers: Record<string, string> = {};
      if (this.config.token) {
        headers['Authorization'] = `Token ${this.config.token}`;
      }
      
      // Download upgrade payload
      const downloadUrl = `${this.config.djangoUrl}/api/bridge/upgrade/${version}/payload/`;
      console.log(`[DjangoSync] Downloading upgrade from: ${downloadUrl}`);
      
      const response = await fetch(downloadUrl, { method: 'GET', headers });
      
      if (!response.ok) {
        throw new Error(`Failed to download upgrade: ${response.status}`);
      }
      
      // Save to temp file
      const tempPath = `/tmp/upgrade_${version}.rpi`;
      const fileStream = createWriteStream(tempPath);
      
      // @ts-ignore - Node.js fetch streams are compatible
      await pipeline(response.body!, fileStream);
      
      console.log(`[DjangoSync] Downloaded upgrade to: ${tempPath}`);
      
      // Trigger upgrade manager
      // Note: This mimics what the API route does for manual upgrades
      await this.acknowledgeCommand(cmd.id, true, 'Upgrade download complete, starting install');
      
      // Start the upgrade (async - don't await)
      // controllerIp is for UI display only; use localhost for cloud-triggered upgrades
      this.upgradeManager.startUpgrade(tempPath, '127.0.0.1').catch(err => {
        console.error('[DjangoSync] Upgrade failed:', err);
      });
      
    } catch (err) {
      console.error('[DjangoSync] Upgrade download failed:', err);
      await this.acknowledgeCommand(cmd.id, false, String(err));
    }
  }

  /**
   * Execute a single command synchronously: translate → send to ARM → flush sync to Django.
   *
   * Used by the bridge's POST /api/bridge/command endpoint so the cloud (Django) can
   * push a command to this bridge and immediately observe the resulting controller
   * state on the next read. Falls through the same translateCommand() path used by
   * the polled HttpMessageQueue path so behavior is consistent.
   *
   * Returns the ARM's reply string (typically 'true' on success).
   */
  async executeCommand(method: string, payload: Record<string, unknown>): Promise<string> {
    if (!this.commandExecutor) {
      throw new Error('Command executor not registered');
    }
    const postBody = this.translateCommand({ id: 0, method, payload });
    if (!postBody) {
      throw new Error(`Unknown command method: ${method}`);
    }
    console.log(`[DjangoSync] Push-execute: ${postBody.substring(0, 80)}`);
    const result = await this.commandExecutor(postBody);
    // Flush controller state back to Django so the caller's response reflects the
    // new state without waiting for the next periodic sync.
    try {
      await this.sync();
    } catch (err) {
      console.warn('[DjangoSync] Post-command sync failed (non-fatal):', err);
    }
    return result;
  }

  /**
   * Translate a Django command to ARM POST format
   */
  private translateCommand(cmd: { id: number; method: string; payload: unknown }): string | null {
    const payload = cmd.payload as Record<string, unknown>;
    
    if (cmd.method === 'settings') {
      // Settings have a tag and key-value pairs
      // Translate to: PlenumTempSet=68.5&PlenumHumidSet=96
      const parts: string[] = [];
      for (const [key, value] of Object.entries(payload)) {
        if (key !== 'tag') {
          parts.push(`${key}=${value}`);
        }
      }
      return parts.join('&');
    }
    
    if (cmd.method === 'action') {
      // Actions are button presses or simple commands
      const tag = payload['tag'] as string;
      if (tag === 'ClearAlarm') {
        return 'ClearAlarm=ClearAlarm';
      }
      if (tag === 'RemoteStop') {
        return `RemoteStop=${payload['remoteStop']}`;
      }
      if (tag === 'DailyFanRuntime') {
        return 'DailyFanRuntime=1';
      }
      if (tag === 'TotalFanRuntime') {
        return 'TotalFanRuntime=1';
      }
      // Fallback: tag=tag format
      return `${tag}=${tag}`;
    }

    // Upgrades are handled separately in handleUpgradeCommand()
    return null;
  }

  /**
   * Acknowledge a command back to Django
   */
  async acknowledgeCommand(commandId: number, success: boolean, result: string): Promise<void> {
    const headers: Record<string, string> = {
      'Content-Type': 'application/json',
    };
    if (this.config.token) {
      headers['Authorization'] = `Token ${this.config.token}`;
    }

    try {
      const response = await fetch(
        `${this.config.djangoUrl}/api/bridge/command-ack/`,
        {
          method: 'POST',
          headers,
          body: JSON.stringify({ commandId, success, result }),
        }
      );

      if (!response.ok) {
        console.error(`[DjangoSync] Command ack failed: ${response.status}`);
      }
    } catch (err) {
      console.error('[DjangoSync] Command ack error:', err);
    }
  }

  /**
   * Get sync statistics
   */
  getStats(): { syncCount: number; errorCount: number; lastSyncTime: number } {
    return {
      syncCount: this.syncCount,
      errorCount: this.errorCount,
      lastSyncTime: this.lastSyncTime,
    };
  }
}

/**
 * Create DjangoSync from environment variables
 */
export function createDjangoSync(cache: DataCache): DjangoSync {
  const token = process.env.DJANGO_TOKEN;
  const deviceId = process.env.DJANGO_DEVICE_ID;
  
  // Enable by default if token is configured, unless explicitly disabled
  const explicitlyDisabled = process.env.DJANGO_SYNC_ENABLED === 'false';
  const hasAuth = !!(token || deviceId);
  const enabled = hasAuth && !explicitlyDisabled;
  
  return new DjangoSync(cache, {
    djangoUrl: process.env.DJANGO_URL || 'http://localhost:8000',
    token,
    deviceId,
    syncInterval: parseInt(process.env.DJANGO_SYNC_INTERVAL || '30000', 10),
    enabled,
  });
}
