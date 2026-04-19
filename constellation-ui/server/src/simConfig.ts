/**
 * Simulation Config Persistence
 * ─────────────────────────────
 * Saves and loads RS485 board configs and physics settings to JSON files
 * so they survive process restarts.
 *
 * Config directory: <server>/.sim-config/
 *   boards.json   — RS485 board definitions (address, type, sensors, labels)
 *   physics.json  — Physics engine parameters (diurnal cycle, potato mass, etc.)
 *   switches.json — CPLD switch positions and digital input state
 */

import * as fs from 'fs';
import * as path from 'path';

// Config directory next to the server src/
const CONFIG_DIR = path.resolve(
  typeof import.meta?.url === 'string'
    ? path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1'))
    : __dirname,
  '..',
  '.sim-config',
);

/** Ensure the config directory exists */
function ensureDir(): void {
  if (!fs.existsSync(CONFIG_DIR)) {
    fs.mkdirSync(CONFIG_DIR, { recursive: true });
    console.log(`[SimConfig] Created config dir: ${CONFIG_DIR}`);
  }
}

/** Save a config object to a named JSON file. Non-blocking, fire-and-forget. */
export function saveConfig(name: string, data: unknown): void {
  try {
    ensureDir();
    const filePath = path.join(CONFIG_DIR, `${name}.json`);
    const json = JSON.stringify(data, null, 2);
    fs.writeFileSync(filePath, json, 'utf-8');
  } catch (err) {
    console.error(`[SimConfig] Failed to save ${name}: ${(err as Error).message}`);
  }
}

/** Load a config object from a named JSON file. Returns null if not found or invalid. */
export function loadConfig<T = unknown>(name: string): T | null {
  try {
    const filePath = path.join(CONFIG_DIR, `${name}.json`);
    if (!fs.existsSync(filePath)) return null;
    const json = fs.readFileSync(filePath, 'utf-8');
    return JSON.parse(json) as T;
  } catch (err) {
    console.error(`[SimConfig] Failed to load ${name}: ${(err as Error).message}`);
    return null;
  }
}

/** Delete a config file (e.g. on reset). */
export function deleteConfig(name: string): void {
  try {
    const filePath = path.join(CONFIG_DIR, `${name}.json`);
    if (fs.existsSync(filePath)) {
      fs.unlinkSync(filePath);
    }
  } catch (err) {
    console.error(`[SimConfig] Failed to delete ${name}: ${(err as Error).message}`);
  }
}

/** Get the config directory path (for logging). */
export function getConfigDir(): string {
  return CONFIG_DIR;
}
