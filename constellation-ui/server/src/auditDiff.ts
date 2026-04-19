/**
 * Field-level diff for accountability auditing.
 *
 * Given a "before" snapshot and the incoming request body, walk both
 * recursively and return a flat list of {field, before, after} entries
 * for every value that changed.
 *
 * Encryption-aware: long base64 blobs that look like CryptoJS AES
 * ciphertext (start with "U2FsdGVkX1" = base64 "Salted__") are redacted
 * so we never persist plaintext-recoverable ciphertexts to the audit log.
 */

import type { AuditDiffEntry } from './accountStore.js';

const ENCRYPTED_MARKER = /^U2FsdGVkX1/;   // CryptoJS AES "Salted__" prefix

function looksEncrypted(v: unknown): boolean {
  return typeof v === 'string' && v.length > 24 && ENCRYPTED_MARKER.test(v);
}

function norm(v: unknown): string | null {
  if (v === undefined || v === null) return null;
  if (typeof v === 'string') return v;
  if (typeof v === 'number' || typeof v === 'boolean') return String(v);
  // Objects / arrays shouldn't reach here — we recurse on them.
  return JSON.stringify(v);
}

function pushScalar(out: AuditDiffEntry[], field: string, before: unknown, after: unknown): void {
  const b = norm(before);
  const a = norm(after);
  if (b === a) return;

  const encrypted = looksEncrypted(before) || looksEncrypted(after);
  if (encrypted) {
    out.push({ field, before: b === null ? null : '***', after: a === null ? null : '***', encrypted: true });
  } else {
    out.push({ field, before: b, after: a });
  }
}

function walk(out: AuditDiffEntry[], path: string, before: any, after: any): void {
  // Both missing — nothing
  if (before === undefined && after === undefined) return;

  const bIsArr = Array.isArray(before);
  const aIsArr = Array.isArray(after);
  const bIsObj = before !== null && typeof before === 'object' && !bIsArr;
  const aIsObj = after  !== null && typeof after  === 'object' && !aIsArr;

  // Arrays: compare by index, length union
  if (bIsArr || aIsArr) {
    const bArr: any[] = bIsArr ? before : [];
    const aArr: any[] = aIsArr ? after : [];
    const n = Math.max(bArr.length, aArr.length);
    for (let i = 0; i < n; i++) {
      walk(out, `${path}[${i}]`, bArr[i], aArr[i]);
    }
    return;
  }

  // Objects: union of keys
  if (bIsObj || aIsObj) {
    const keys = new Set<string>([
      ...(bIsObj ? Object.keys(before) : []),
      ...(aIsObj ? Object.keys(after) : []),
    ]);
    for (const k of keys) {
      const childPath = path ? `${path}.${k}` : k;
      walk(out, childPath, bIsObj ? before[k] : undefined, aIsObj ? after[k] : undefined);
    }
    return;
  }

  pushScalar(out, path, before, after);
}

/** Cap the emitted diff to avoid pathological payloads. */
const MAX_DIFF_ENTRIES = 100;

export function computeDiff(before: any, after: any): AuditDiffEntry[] {
  const out: AuditDiffEntry[] = [];
  try {
    walk(out, '', before, after);
  } catch {
    return [];
  }
  if (out.length > MAX_DIFF_ENTRIES) {
    const truncated = out.slice(0, MAX_DIFF_ENTRIES);
    truncated.push({ field: '__truncated__', before: null, after: String(out.length - MAX_DIFF_ENTRIES) });
    return truncated;
  }
  return out;
}
