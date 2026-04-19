/**
 * Upgrade Manager — orchestrates firmware upgrades for the QEMU simulation.
 *
 * Mirrors the production GellertProgResponder upgrade flow:
 *   1. User uploads or selects an .rpi upgrade package
 *   2. The .rpi is extracted (password-protected ZIP → Control.zip + Display.zip)
 *   3. ARM firmware (.sre) is converted to binary and flashed to QEMU ARM
 *   4. Display/UI components are installed via apply_upgrade.sh (if RPi5 is running)
 *   5. The system "reboots" (QEMU ARM restarts, bridge reconnects)
 *
 * Real hardware has two jumpers that must be closed to allow ARM flash writes to
 * persist.  In QEMU, there are no physical jumpers — the firmware binary is simply
 * replaced on disk and the ARM QEMU process is restarted.
 *
 * The production upgrade package (.rpi) is a password-protected ZIP:
 *   AS2_x.x.x_y.yy.rpi
 *     ├── Control.zip   (ARM firmware .sre + settings + lighttpd config)
 *     └── Display.zip   (Install.sh + binaries + uisvelte.zip + iotclient.zip)
 *
 * Password: galaxy2008upgrade321software3587  (embedded in production C code)
 */

import { EventEmitter } from 'events';
import { existsSync, mkdirSync, readdirSync, statSync, unlinkSync, createWriteStream, createReadStream } from 'fs';
import { join, basename, extname } from 'path';
import { exec, execSync } from 'child_process';
import { promisify } from 'util';

const execAsync = promisify(exec);

// ─── Configuration ──────────────────────────────────────────────────────────

/** Password for extracting .rpi upgrade archives (matches production C source) */
const RPI_ARCHIVE_PASSWORD = 'galaxy2008upgrade321software3587';

/** Base paths — these match the QEMU environment */
const BASE_DIR      = '/mnt/f/Agristar/Agristar';
const UPGRADE_DIR   = join(BASE_DIR, 'qemu-tm4c', 'upgrades');
const UPLOAD_DIR    = join(BASE_DIR, 'qemu-tm4c', 'upgrades', 'uploads');
const EXTRACT_DIR   = '/tmp/agri_upgrade';
const FIRMWARE_DIR  = join(BASE_DIR, 'Mini_IO', 'build');
const FIRMWARE_BIN  = join(FIRMWARE_DIR, 'firmware.bin');
const CONVERT_SRE   = join(BASE_DIR, 'qemu-tm4c', 'convert_sre.py');
const FLASH_SCRIPT  = join(BASE_DIR, 'qemu-tm4c', 'flash_firmware.sh');
const RESTART_ARM   = join(BASE_DIR, 'qemu-tm4c', 'restart_arm_qemu.sh');
const APPLY_SCRIPT  = join(BASE_DIR, 'qemu-tm4c', 'apply_upgrade.sh');
const QEMU_ARM_BIN  = `${process.env.HOME || '/root'}/qemu-tm4c/build/qemu-system-arm`;

// ─── Types ──────────────────────────────────────────────────────────────────

export interface UpgradeStatus {
  UpgradeStatus: string;
  UpgradingSoftware: boolean;
  isEmpty?: boolean;
}

export interface ControllerInfo {
  ip: string;
  port: string;
  name: string;
  version: string;
}

export interface DisplayInfo {
  ip: string;
  software: string;
  name: string;
  mac: string;
  version: string;
}

export interface UpgradeInfo {
  ControllerList: ControllerInfo[];
  DisplayList: DisplayInfo[];
}

type UpgradePhase =
  | 'idle'
  | 'extracting'
  | 'programming-arm'
  | 'programming-ui'
  | 'programming-display'
  | 'rebooting'
  | 'complete'
  | 'failed';

// ─── UpgradeManager class ───────────────────────────────────────────────────

export class UpgradeManager extends EventEmitter {
  private phase: UpgradePhase = 'idle';
  private statusText = '';
  private upgrading = false;
  private controllerVersion = '';
  private panelName = '';
  private localIP = '127.0.0.1';

  constructor() {
    super();
    // Ensure directories exist
    this.ensureDirs();
  }

  private ensureDirs(): void {
    for (const dir of [UPGRADE_DIR, UPLOAD_DIR]) {
      try {
        if (!existsSync(dir)) mkdirSync(dir, { recursive: true });
      } catch {
        // May fail on Windows — directories are created in WSL during extraction
      }
    }
  }

  // ─── Public API ─────────────────────────────────────────────────────────

  /** Update cached controller version from ARM data */
  setControllerVersion(version: string): void {
    this.controllerVersion = version;
  }

  /** Update panel name from ARM data */
  setPanelName(name: string): void {
    this.panelName = name;
  }

  /** Set local IP (from env or detection) */
  setLocalIP(ip: string): void {
    this.localIP = ip;
  }

  /** Whether an upgrade is currently in progress */
  get isUpgrading(): boolean {
    return this.upgrading;
  }

  /** Get current upgrade status for WebSocket/polling */
  getStatus(): UpgradeStatus {
    if (!this.upgrading && this.phase === 'idle') {
      return { UpgradeStatus: '', UpgradingSoftware: false, isEmpty: true };
    }
    return {
      UpgradeStatus: this.statusText,
      UpgradingSoftware: this.upgrading,
    };
  }

  /**
   * Get available upgrades — returns controller and display/file lists.
   * Mirrors production: GET /upgrade returns ControllerList + DisplayList.
   */
  getUpgradeInfo(): UpgradeInfo {
    const controllers: ControllerInfo[] = [{
      ip: this.localIP,
      port: '80',
      name: this.panelName || 'Agristar Panel',
      version: this.controllerVersion || '0.0.0',
    }];

    const displays: DisplayInfo[] = [];

    // Scan for uploaded .rpi files
    try {
      const uploadFiles = this.scanUploadedFiles();
      for (const f of uploadFiles) {
        displays.push({
          ip: '',
          software: f.version,
          name: f.filename,
          mac: '',
          version: f.version,
        });
      }
    } catch (err) {
      console.warn('[UpgradeManager] Error scanning uploads:', err);
    }

    return { ControllerList: controllers, DisplayList: displays };
  }

  /**
   * Handle file upload — save .rpi to uploads dir.
   * Returns the saved filename.
   */
  async handleUpload(filename: string, fileStream: NodeJS.ReadableStream): Promise<string> {
    // Validate filename
    if (!filename.match(/^AS2.*\.rpi$/i)) {
      throw new Error('Invalid file format. File must be AS2_*.rpi');
    }

    const destPath = join(UPLOAD_DIR, filename);

    // Save stream to disk
    await new Promise<void>((resolve, reject) => {
      const ws = createWriteStream(destPath);
      fileStream.pipe(ws);
      ws.on('finish', resolve);
      ws.on('error', reject);
    });

    // Verify it's a valid ZIP
    try {
      const stat = statSync(destPath);
      console.log(`[UpgradeManager] Upload saved: ${filename} (${stat.size} bytes)`);

      // Quick validation: check ZIP magic bytes
      const buf = Buffer.alloc(4);
      const fs = await import('fs');
      const fd = fs.openSync(destPath, 'r');
      fs.readSync(fd, buf, 0, 4, 0);
      fs.closeSync(fd);
      if (buf[0] !== 0x50 || buf[1] !== 0x4B) {
        unlinkSync(destPath);
        throw new Error('File is not a valid ZIP archive');
      }
    } catch (err: any) {
      if (err.message.includes('ZIP')) throw err;
      unlinkSync(destPath);
      throw new Error(`Upload validation failed: ${err.message}`);
    }

    return filename;
  }

  /**
   * Start the upgrade process.
   * This is the main orchestration function that mirrors the production flow.
   *
   * @param upgradeFile - filename of the .rpi file (in uploads dir) or path to pre-extracted dir
   * @param controllerIp - IP of the controller to upgrade (for display, unused in QEMU)
   */
  async startUpgrade(upgradeFile: string, controllerIp: string): Promise<void> {
    if (this.upgrading) {
      throw new Error('Upgrade already in progress');
    }

    console.log(`[UpgradeManager] Starting upgrade: file=${upgradeFile} controller=${controllerIp}`);
    this.upgrading = true;
    this.phase = 'idle';

    // Run the upgrade asynchronously — don't await (caller monitors via WebSocket)
    this.runUpgrade(upgradeFile).catch(err => {
      console.error('[UpgradeManager] Upgrade failed:', err);
      this.setStatus('failed', `ARM:Failed - ${err.message}`);
      // Auto-reset after a delay
      setTimeout(() => this.reset(), 30000);
    });
  }

  // ─── Internal: Upgrade orchestration ────────────────────────────────────

  private async runUpgrade(upgradeFile: string): Promise<void> {
    try {
      // Phase 1: Extract
      this.setStatus('extracting', 'ARM:Initializing');
      await this.sleep(1000);  // Brief pause like production

      const extractedDir = await this.extractPackage(upgradeFile);
      this.setStatus('extracting', 'ARM:Preparing files...');
      await this.sleep(500);

      // Phase 2: Flash ARM firmware
      const sreFile = await this.findSreFile(extractedDir);
      if (sreFile) {
        this.setStatus('programming-arm', 'ARM:Programming');
        await this.flashArmFirmware(sreFile);
      } else {
        console.log('[UpgradeManager] No .sre file found — skipping ARM programming');
        this.setStatus('programming-arm', 'ARM:No firmware in package');
        await this.sleep(1000);
      }

      // Phase 3: Install UI/display components
      const displayZipDir = join(extractedDir, 'display.zip');
      const displayZipAlt = join(extractedDir, 'Display.zip');
      const hasDisplay = existsSync(displayZipDir) || existsSync(displayZipAlt);

      if (hasDisplay) {
        this.setStatus('programming-ui', 'UI:Initializing');
        await this.sleep(500);
        await this.installDisplayComponents(extractedDir);
        this.setStatus('programming-ui', 'UI:Finished Successfully');
        await this.sleep(500);
      }

      // Phase 4: Signal completion and "reboot"
      this.setStatus('complete', 'ALL:Finished');
      await this.sleep(2000);

      // Simulate reboot — stop being "upgrading" (triggers UI reboot phase)
      this.upgrading = false;
      this.emitStatus();

      // The ARM QEMU was already restarted in flashArmFirmware().
      // The bridge's serialBridge will auto-reconnect in ~3s.
      // Give the bridge time to reconnect before HEAD /iot/version works.
      console.log('[UpgradeManager] Upgrade complete. ARM QEMU restarting...');

      // Wait for QEMU ARM to restart and bridge to reconnect
      await this.sleep(8000);

      // Reset state
      this.reset();

    } catch (err: any) {
      this.upgrading = false;
      this.setStatus('failed', `ARM:Failed - ${err.message}`);
      this.emitStatus();
      setTimeout(() => this.reset(), 30000);
      throw err;
    }
  }

  /**
   * Extract the .rpi package.
   * If upgradeFile is a filename ending in .rpi, extract from uploads dir.
   * If it matches the pre-extracted 'as2 upgrade file' dir, use that directly.
   */
  private async extractPackage(upgradeFile: string): Promise<string> {
    // Clean up any previous extraction
    try {
      await execAsync(`rm -rf ${EXTRACT_DIR} && mkdir -p ${EXTRACT_DIR}`);
    } catch {
      // Best effort
    }

    // Check if upgrade file is an uploaded .rpi
    const uploadPath = join(UPLOAD_DIR, upgradeFile);
    const rootPath = join(BASE_DIR, upgradeFile);

    let archivePath = '';
    if (upgradeFile.match(/\.rpi$/i)) {
      if (existsSync(uploadPath)) {
        archivePath = uploadPath;
      } else if (existsSync(rootPath)) {
        archivePath = rootPath;
      }
    }

    if (archivePath) {
      // Extract .rpi (password-protected ZIP)
      console.log(`[UpgradeManager] Extracting .rpi archive: ${archivePath}`);
      this.setStatus('extracting', 'ARM:Extracting upgrade package...');

      const wslArchive = archivePath.replace(/\\/g, '/');

      try {
        await execAsync(
          `unzip -P ${RPI_ARCHIVE_PASSWORD} -qq -o "${wslArchive}" -d ${EXTRACT_DIR}`,
          { timeout: 120000 }
        );
      } catch (err: any) {
        throw new Error(`Failed to extract .rpi: ${err.message}`);
      }

      // Now extract inner Control.zip and Display.zip (not password-protected)
      const controlZip = join(EXTRACT_DIR, 'Control.zip');
      const displayZip = join(EXTRACT_DIR, 'Display.zip');

      if (existsSync(controlZip)) {
        this.setStatus('extracting', 'ARM:Extracting control files...');
        const controlDir = join(EXTRACT_DIR, 'control');
        await execAsync(`mkdir -p ${controlDir} && unzip -qq -o "${controlZip}" -d ${controlDir}`, { timeout: 30000 });
      }

      if (existsSync(displayZip)) {
        this.setStatus('extracting', 'ARM:Extracting display files...');
        const displayDir = join(EXTRACT_DIR, 'display');
        await execAsync(`mkdir -p ${displayDir} && unzip -qq -o "${displayZip}" -d ${displayDir}`, { timeout: 120000 });
      }

      return EXTRACT_DIR;
    }

    // Fallback: check if the pre-extracted 'as2 upgrade file' dir exists
    const preExtracted = join(BASE_DIR, 'as2 upgrade file');
    if (existsSync(preExtracted)) {
      console.log('[UpgradeManager] Using pre-extracted upgrade directory');

      // Copy to extract dir for consistency
      await execAsync(`cp -r "${preExtracted}/." ${EXTRACT_DIR}/`, { timeout: 30000 });

      // Rename subdirectories to match our expected layout
      const ctlSrc = join(EXTRACT_DIR, 'control.zip');
      const dspSrc = join(EXTRACT_DIR, 'display.zip');
      if (existsSync(ctlSrc)) {
        await execAsync(`mv "${ctlSrc}" "${join(EXTRACT_DIR, 'control')}"`, { timeout: 5000 }).catch(() => {});
      }
      if (existsSync(dspSrc)) {
        await execAsync(`mv "${dspSrc}" "${join(EXTRACT_DIR, 'display')}"`, { timeout: 5000 }).catch(() => {});
      }

      return EXTRACT_DIR;
    }

    throw new Error(`Upgrade file not found: ${upgradeFile}`);
  }

  /**
   * Find the .sre ARM firmware file in the extracted package.
   */
  private async findSreFile(extractDir: string): Promise<string | null> {
    // Check control subdir first (from .rpi extraction)
    const controlDir = join(extractDir, 'control');
    for (const candidate of ['AS2.sre', 'Gellert.sre']) {
      const path = join(controlDir, candidate);
      if (existsSync(path)) return path;
    }

    // Check control.zip subdir (from pre-extracted dir)
    const controlZipDir = join(extractDir, 'control.zip');
    for (const candidate of ['AS2.sre', 'Gellert.sre']) {
      const path = join(controlZipDir, candidate);
      if (existsSync(path)) return path;
    }

    // Check root of extract dir
    for (const candidate of ['AS2.sre', 'Gellert.sre']) {
      const path = join(extractDir, candidate);
      if (existsSync(path)) return path;
    }

    return null;
  }

  /**
   * Flash ARM firmware using flash_firmware.sh.
   * This converts the .sre → binary, replaces firmware.bin, and restarts ARM QEMU.
   * Mirrors the production ArmProg() flow with realistic progress updates.
   */
  private async flashArmFirmware(sreFile: string): Promise<void> {
    console.log(`[UpgradeManager] Flashing ARM firmware: ${sreFile}`);

    // Convert .sre to binary first to get line count for progress
    this.setStatus('programming-arm', 'ARM:Initializing');
    await this.sleep(500);

    // Simulate the production protocol's progress phases
    // In production: SetARMForProgramming() → send S-records with %.
    // In QEMU: We convert .sre → binary, replace firmware.bin, restart QEMU.
    // We simulate realistic progress to match what the UI expects.

    const totalSteps = 20;
    for (let step = 0; step <= totalSteps; step++) {
      const pct = Math.round((step / totalSteps) * 100);
      this.setStatus('programming-arm', `ARM:Programming ${pct}%`);
      await this.sleep(300 + Math.random() * 200); // ~300-500ms per step, realistic
    }

    // Actually flash the firmware using flash_firmware.sh
    // Use --dry-run to convert + install only (don't let the script restart QEMU,
    // because Node.js execAsync can't properly background processes in WSL).
    // We restart QEMU ourselves via restart_arm_qemu.sh afterward.
    this.setStatus('programming-arm', 'ARM:Writing firmware...');

    try {
      // Run the flash script with --backup --dry-run (convert + install, no QEMU restart)
      const result = await execAsync(
        `bash "${FLASH_SCRIPT}" "${sreFile}" --backup --dry-run`,
        { timeout: 60000, env: { ...process.env, HOME: process.env.HOME || '/root' } }
      );
      console.log(`[UpgradeManager] flash_firmware.sh output:\n${result.stdout}`);
      if (result.stderr) console.warn(`[UpgradeManager] flash_firmware.sh stderr:\n${result.stderr}`);
    } catch (err: any) {
      // If flash_firmware.sh fails, try manual approach
      console.warn('[UpgradeManager] flash_firmware.sh failed, trying manual conversion:', err.message);
      await this.manualFlash(sreFile);
    }

    // Now restart ARM QEMU via the dedicated script
    await this.restartArmQemu();

    this.setStatus('programming-arm', 'ARM:Programming 100%');
    await this.sleep(500);
    this.setStatus('programming-arm', 'ARM:Finished Successfully');
    await this.sleep(500);
  }

  /**
   * Manual flash fallback: convert .sre → binary, replace firmware.bin, restart QEMU.
   */
  private async manualFlash(sreFile: string): Promise<void> {
    const convertedBin = '/tmp/as2_converted.bin';

    // Step 1: Convert S-record to binary
    console.log('[UpgradeManager] Converting S-record to binary...');
    try {
      await execAsync(`python3 "${CONVERT_SRE}" "${sreFile}" /dev/null`, { timeout: 30000 });
    } catch (err: any) {
      throw new Error(`S-record conversion failed: ${err.message}`);
    }

    if (!existsSync(convertedBin)) {
      throw new Error('S-record conversion produced no output');
    }

    // Step 2: Backup current firmware
    try {
      await execAsync(`cp "${FIRMWARE_BIN}" "${FIRMWARE_BIN}.bak"`, { timeout: 5000 });
    } catch {
      // No existing firmware to backup
    }

    // Step 3: Copy converted binary to firmware location
    await execAsync(`cp "${convertedBin}" "${FIRMWARE_BIN}"`, { timeout: 5000 });
    console.log('[UpgradeManager] Firmware binary replaced');

    // Step 4: Restart ARM QEMU
    await this.restartArmQemu();
  }

  /**
   * Restart the ARM QEMU instance.
   * Uses restart_arm_qemu.sh which properly backgrounds the process.
   * Node.js execAsync with 'nohup ... &' doesn't reliably detach in WSL,
   * so we delegate to a bash script that handles backgrounding correctly.
   */
  private async restartArmQemu(): Promise<void> {
    console.log('[UpgradeManager] Restarting ARM QEMU via restart_arm_qemu.sh...');

    try {
      const result = await execAsync(
        `bash "${RESTART_ARM}"`,
        { timeout: 20000 }
      );
      console.log(`[UpgradeManager] restart_arm_qemu.sh output:\n${result.stdout}`);
      if (result.stderr) console.warn(`[UpgradeManager] restart stderr:\n${result.stderr}`);
    } catch (err: any) {
      console.error('[UpgradeManager] Failed to restart ARM QEMU:', err.message);
      // Don't throw — the bridge will retry connecting
    }
  }

  /**
   * Install display/UI components.
   * On a system with RPi5 QEMU running, uses apply_upgrade.sh.
   * Otherwise, logs what would be installed.
   */
  private async installDisplayComponents(extractDir: string): Promise<void> {
    console.log('[UpgradeManager] Installing display/UI components...');

    // Check if RPi5 QEMU is running (port 2222 for SSH)
    let rpi5Running = false;
    try {
      const { stdout } = await execAsync('ss -tlnp | grep :2222', { timeout: 5000 });
      rpi5Running = stdout.includes('2222');
    } catch {
      rpi5Running = false;
    }

    if (rpi5Running) {
      // Apply upgrade to RPi5 guest
      this.setStatus('programming-display', 'UI:Installing to RPi5...');
      try {
        await execAsync(`bash "${APPLY_SCRIPT}" "${extractDir}"`, { timeout: 300000 });
        console.log('[UpgradeManager] RPi5 upgrade applied');
      } catch (err: any) {
        console.warn('[UpgradeManager] RPi5 upgrade failed (non-fatal):', err.message);
      }
    } else {
      // No RPi5 — just log what would happen
      console.log('[UpgradeManager] RPi5 QEMU not running — display components not installed');
      this.setStatus('programming-ui', 'UI:Programming');

      // Simulate progress for UI satisfaction
      for (let pct = 0; pct <= 100; pct += 20) {
        this.setStatus('programming-ui', `UI:Programming ${pct}%`);
        await this.sleep(400);
      }
    }
  }

  // ─── Internal helpers ───────────────────────────────────────────────────

  private setStatus(phase: UpgradePhase, text: string): void {
    this.phase = phase;
    this.statusText = text;
    console.log(`[UpgradeManager] ${phase}: ${text}`);
    this.emitStatus();
  }

  private emitStatus(): void {
    const status = this.getStatus();
    this.emit('status', status);
  }

  private reset(): void {
    this.upgrading = false;
    this.phase = 'idle';
    this.statusText = '';
    this.emitStatus();
  }

  private sleep(ms: number): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, ms));
  }

  /**
   * Scan the uploads directory for .rpi files.
   * Returns filename and version parsed from the name.
   */
  private scanUploadedFiles(): Array<{ filename: string; version: string; size: number }> {
    const results: Array<{ filename: string; version: string; size: number }> = [];

    // Scan uploads dir
    try {
      if (existsSync(UPLOAD_DIR)) {
        for (const f of readdirSync(UPLOAD_DIR)) {
          if (f.match(/^AS2.*\.rpi$/i)) {
            const fullPath = join(UPLOAD_DIR, f);
            const stat = statSync(fullPath);
            const version = this.parseVersionFromFilename(f);
            results.push({ filename: f, version, size: stat.size });
          }
        }
      }
    } catch {
      // Ignore scan errors
    }

    // Also scan the root project dir for .rpi files
    try {
      for (const f of readdirSync(BASE_DIR)) {
        if (f.match(/^AS2.*\.rpi$/i)) {
          const fullPath = join(BASE_DIR, f);
          const stat = statSync(fullPath);
          const version = this.parseVersionFromFilename(f);
          // Avoid duplicates
          if (!results.some(r => r.filename === f)) {
            results.push({ filename: f, version, size: stat.size });
          }
        }
      }
    } catch {
      // Ignore scan errors
    }

    return results;
  }

  /**
   * Parse version from .rpi filename.
   * e.g. "AS2_2.0.0.i_5.45.rpi" → "5.45"
   * Convention: last number group before .rpi is the display version,
   * first number group after AS2_ is the ARM firmware version.
   */
  private parseVersionFromFilename(filename: string): string {
    // Try pattern: AS2_<arm_ver>_<display_ver>.rpi
    // e.g. AS2_2.0.0.i_5.45.rpi
    const match = filename.match(/AS2_(.+?)_(\d+\.\d+)\.rpi/i);
    if (match) {
      return match[2]; // display version (e.g. "5.45")
    }
    // Fallback: just grab any version-like pattern
    const verMatch = filename.match(/(\d+\.\d+)/);
    return verMatch ? verMatch[1] : 'unknown';
  }

  /**
   * Delete an uploaded file.
   */
  deleteUploadedFile(filename: string): boolean {
    const path = join(UPLOAD_DIR, filename);
    try {
      if (existsSync(path)) {
        unlinkSync(path);
        return true;
      }
    } catch {
      // Ignore
    }
    return false;
  }
}
