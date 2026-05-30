/**
 * Structured logger for the Constellation Bridge Server.
 *
 * Outputs JSON lines to stdout for machine-parseable log aggregation.
 * Compatible with Azure App Service log streams, ELK, Loki, etc.
 *
 * Usage:
 *   import { logger } from './logger.js';
 *   logger.info('message', { key: 'value' });
 *   logger.warn('timeout', { elapsed: 5000 });
 *   logger.error('failed', { err: error.message });
 *
 * Each log line includes: timestamp, level, component, message, and any extra fields.
 * The `seq` field can be attached to correlate a log entry to a specific envelope.
 */

export type LogLevel = 'debug' | 'info' | 'warn' | 'error';

const LEVEL_NUM: Record<LogLevel, number> = { debug: 0, info: 1, warn: 2, error: 3 };

const MIN_LEVEL: LogLevel = (process.env.LOG_LEVEL as LogLevel) ?? 'info';

interface LogEntry {
  ts: string;
  level: LogLevel;
  component: string;
  msg: string;
  [key: string]: unknown;
}

function emit(level: LogLevel, component: string, msg: string, extra?: Record<string, unknown>): void {
  if (LEVEL_NUM[level] < LEVEL_NUM[MIN_LEVEL]) return;

  const entry: LogEntry = {
    ts: new Date().toISOString(),
    level,
    component,
    msg,
    ...extra,
  };

  const line = JSON.stringify(entry);
  if (level === 'error') {
    process.stderr.write(line + '\n');
  } else {
    process.stdout.write(line + '\n');
  }
}

/**
 * Create a scoped logger for a specific component.
 *
 * @example
 *   const log = createLogger('NovaBridge');
 *   log.info('connected', { port: 9000 });
 *   // → {"ts":"...","level":"info","component":"NovaBridge","msg":"connected","port":9000}
 */
export function createLogger(component: string) {
  return {
    debug: (msg: string, extra?: Record<string, unknown>) => emit('debug', component, msg, extra),
    info:  (msg: string, extra?: Record<string, unknown>) => emit('info',  component, msg, extra),
    warn:  (msg: string, extra?: Record<string, unknown>) => emit('warn',  component, msg, extra),
    error: (msg: string, extra?: Record<string, unknown>) => emit('error', component, msg, extra),
  };
}

/** Default logger (component = 'Bridge') */
export const logger = createLogger('Bridge');

/**
 * Reroute Node's global `console.*` methods through the structured
 * logger. Existing `console.log/warn/error/info/debug` call sites
 * (~250 across the bridge) automatically emit JSON lines on stdout/
 * stderr with `level`, `ts`, `component`, `msg` — so production logs
 * become `journalctl -o json | jq` filterable without per-file edits.
 *
 * Component tagging is kept simple: every console-routed line gets
 * `component: "Console"`. New code that wants a proper component tag
 * should call `createLogger('MyArea')` instead.
 *
 * The original methods are preserved on `console._raw.{log,warn,…}`
 * for the rare case that plain text output is required (e.g. piping
 * to a downstream tool that expects unstructured stdout).
 *
 * Idempotent. Must be called before any module-level `console.*` runs;
 * `index.ts` invokes it immediately after importing the logger.
 */
import { format } from 'node:util';

let consoleInstalled = false;
export function installConsoleBridge(): void {
  if (consoleInstalled) return;
  consoleInstalled = true;

  const consoleLog = createLogger('Console');

  // Preserve originals for opt-out callers.
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  (console as any)._raw = {
    log: console.log.bind(console),
    info: console.info.bind(console),
    warn: console.warn.bind(console),
    error: console.error.bind(console),
    debug: console.debug.bind(console),
  };

  console.log   = (...a: unknown[]) => consoleLog.info(format(...a));
  console.info  = (...a: unknown[]) => consoleLog.info(format(...a));
  console.warn  = (...a: unknown[]) => consoleLog.warn(format(...a));
  console.error = (...a: unknown[]) => consoleLog.error(format(...a));
  console.debug = (...a: unknown[]) => consoleLog.debug(format(...a));
}

/**
 * Restore native `console.*` methods. Test-only; production never
 * needs to undo the bridge. No-op if the bridge isn't installed.
 */
export function uninstallConsoleBridge(): void {
  if (!consoleInstalled) return;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const raw = (console as any)._raw;
  if (raw) {
    console.log = raw.log;
    console.info = raw.info;
    console.warn = raw.warn;
    console.error = raw.error;
    console.debug = raw.debug;
  }
  consoleInstalled = false;
}
