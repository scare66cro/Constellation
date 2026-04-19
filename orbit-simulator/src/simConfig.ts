/**
 * Simulation Config Persistence
 * ─────────────────────────────
 * Saves and loads Orbit simulator state to JSON files
 * so they survive process restarts.
 *
 * Config directory: <project>/.sim-config/
 */

import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const CONFIG_DIR = path.resolve(__dirname, '..', '.sim-config');

function ensureDir(): void {
  if (!fs.existsSync(CONFIG_DIR)) {
    fs.mkdirSync(CONFIG_DIR, { recursive: true });
    console.log(`[SimConfig] Created config dir: ${CONFIG_DIR}`);
  }
}

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
