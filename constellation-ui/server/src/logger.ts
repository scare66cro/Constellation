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
