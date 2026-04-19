/**
 * Account metadata + audit log store (local).
 *
 * Stores per-slot role/last-login metadata and an append-only audit log of
 * logins and save operations. Intended as the foundation for a future
 * Django-backed unified identity story:
 *   pass 1 (this file): local-only metadata + NDJSON audit, no Django
 *   pass 2: cloud link records on each slot, so a local slot can be
 *           bound to a Django UserAccount UUID and audit entries can
 *           carry both local-slot and cloud-user identity
 *
 * Files live under <serverRoot>/data/ (auto-created):
 *   account-meta.json    — {schema, factory, slots[10], cloudLinks[]}
 *   audit-log.ndjson     — append-only JSON lines, rotated at 5 MB
 *   audit-log.ndjson.1   — previous generation (one backup)
 */

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

// ── Paths ─────────────────────────────────────────────────────────────────
// accountStore.ts compiles to server/dist/accountStore.js so __dirname
// there is …/server/dist — place the data dir one level up next to src/.
const DATA_DIR = path.resolve(
  path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1')),
  '..', 'data'
);
const META_FILE  = path.join(DATA_DIR, 'account-meta.json');
const AUDIT_FILE = path.join(DATA_DIR, 'audit-log.ndjson');
const AUDIT_ROTATE_BYTES = 5 * 1024 * 1024;      // 5 MB
const AUDIT_READ_CAP = 2000;                     // largest response size

function ensureDataDir(): void {
  try { fs.mkdirSync(DATA_DIR, { recursive: true }); } catch { /* ignore */ }
}

// ── Types ─────────────────────────────────────────────────────────────────
export type SlotRole = 'disabled' | 'operator' | 'admin';

export interface SlotMeta {
  role:        SlotRole;      // disabled / operator(L1) / admin(L2)
  lastLogin:   string | null; // ISO-8601 UTC, null = never
  lastLoginIp: string | null; // source address of last successful login
  loginCount:  number;        // lifetime login count for this slot
}

export interface CloudLink {
  /** Django UserAccount UUID — primary key for the link. */
  cloudUserId:      string;
  /** Django username. */
  username:         string;
  /** Pretty name for the UI. */
  displayName:      string;
  /** Role from Django ('operator'|'admin'). */
  role:             SlotRole;
  /** Opaque token the bridge presents on remote-login calls. */
  linkToken:        string;
  /** ISO timestamp the link was created. */
  linkedAt:         string;
  /** 0-9 when bound to a local slot, null for cloud-only admins. */
  slot:             number | null;
  /** Last successful remote-login through this link. */
  lastRemoteLogin:  string | null;
  /** scrypt(password, salt) hex — cached so we can verify when Django is offline. */
  passwordHash?:    string;
  /** 16-byte salt hex paired with passwordHash. */
  passwordSalt?:    string;
  /** ISO timestamp the password hash was last refreshed via a successful online login. */
  passwordHashedAt?: string;
}

export interface AccountMeta {
  schema:            1;
  factoryLastLogin:  string | null;
  factoryLoginCount: number;
  slots:             SlotMeta[];
  cloudLinks:        CloudLink[];
}

export type AuditKind = 'login' | 'logout' | 'login_fail' | 'save' | 'level_change';

export interface AuditDiffEntry {
  field:      string;                  // e.g. 'basic[9]', 'pid.p'
  before:     string | null;
  after:      string | null;
  encrypted?: true;                    // true when ciphertext redacted
}

export interface AuditEntry {
  ts:       string;           // ISO-8601
  kind:     AuditKind;
  actor:    string;           // username OR 'factory' OR 'anonymous'
  slot:     number | null;    // 0-9 when resolvable, null for factory/anon
  level:    number;           // access level at time of event
  route?:   string;           // for kind='save'
  detail?:  string;           // short human-readable note
  diff?:    AuditDiffEntry[]; // field-level before/after for kind='save'
  ip?:      string;           // client source ip
}

// ── Metadata ──────────────────────────────────────────────────────────────
function defaultMeta(): AccountMeta {
  return {
    schema: 1,
    factoryLastLogin: null,
    factoryLoginCount: 0,
    slots: Array.from({ length: 10 }, () => ({
      role: 'operator' as SlotRole,
      lastLogin: null,
      lastLoginIp: null,
      loginCount: 0,
    })),
    cloudLinks: [],
  };
}

let cachedMeta: AccountMeta | null = null;

export function loadAccountMeta(): AccountMeta {
  if (cachedMeta) return cachedMeta;
  ensureDataDir();
  try {
    if (fs.existsSync(META_FILE)) {
      const raw = JSON.parse(fs.readFileSync(META_FILE, 'utf8')) as AccountMeta;
      if (raw?.schema === 1 && Array.isArray(raw.slots)) {
        // Defensive normalize — make sure exactly 10 slots
        while (raw.slots.length < 10) {
          raw.slots.push({ role: 'operator', lastLogin: null, lastLoginIp: null, loginCount: 0 });
        }
        raw.slots.length = 10;
        raw.cloudLinks ??= [];
        cachedMeta = raw;
        return raw;
      }
    }
  } catch (e: any) {
    console.warn('[accountStore] Meta load failed, using defaults:', e.message);
  }
  cachedMeta = defaultMeta();
  return cachedMeta;
}

export function saveAccountMeta(meta: AccountMeta): void {
  ensureDataDir();
  cachedMeta = meta;
  const tmp = META_FILE + '.tmp';
  try {
    fs.writeFileSync(tmp, JSON.stringify(meta, null, 2), 'utf8');
    fs.renameSync(tmp, META_FILE);
  } catch (e: any) {
    console.error('[accountStore] Meta save failed:', e.message);
  }
}

/** Update a single slot and persist. Callers should pass only changed fields. */
export function updateSlot(slot: number, patch: Partial<SlotMeta>): void {
  if (slot < 0 || slot >= 10) return;
  const meta = loadAccountMeta();
  meta.slots[slot] = { ...meta.slots[slot], ...patch };
  saveAccountMeta(meta);
}

// ── Audit log ─────────────────────────────────────────────────────────────
function rotateIfNeeded(): void {
  try {
    const st = fs.statSync(AUDIT_FILE);
    if (st.size < AUDIT_ROTATE_BYTES) return;
    const bak = AUDIT_FILE + '.1';
    try { fs.unlinkSync(bak); } catch { /* ignore */ }
    fs.renameSync(AUDIT_FILE, bak);
  } catch {
    // file missing — nothing to rotate
  }
}

export function appendAudit(entry: Omit<AuditEntry, 'ts'> & { ts?: string }): void {
  ensureDataDir();
  rotateIfNeeded();
  const full: AuditEntry = { ts: entry.ts ?? new Date().toISOString(), ...entry } as AuditEntry;
  try {
    fs.appendFileSync(AUDIT_FILE, JSON.stringify(full) + '\n', 'utf8');
  } catch (e: any) {
    console.warn('[accountStore] Audit append failed:', e.message);
  }
  // Notify listeners (e.g. cloud forwarder). Listener failures must never
  // disrupt the local write path.
  for (const cb of auditListeners) {
    try { cb(full); } catch (e: any) {
      console.warn('[accountStore] Audit listener failed:', e.message);
    }
  }
}

type AuditListener = (entry: AuditEntry) => void;
const auditListeners: AuditListener[] = [];

/** Register a callback invoked after each audit entry is persisted. */
export function onAuditAppend(cb: AuditListener): () => void {
  auditListeners.push(cb);
  return () => {
    const i = auditListeners.indexOf(cb);
    if (i >= 0) auditListeners.splice(i, 1);
  };
}

/** Return the most recent `limit` entries, newest first. */
export function readAudit(limit = 100): AuditEntry[] {
  const cap = Math.min(Math.max(1, limit), AUDIT_READ_CAP);
  ensureDataDir();
  const lines: string[] = [];
  try {
    if (fs.existsSync(AUDIT_FILE)) {
      // Read whole file. For 5 MB it's fine; switching to tail-read-optimized
      // reverse scan is a pass-2 improvement.
      const content = fs.readFileSync(AUDIT_FILE, 'utf8');
      lines.push(...content.split('\n').filter(Boolean));
    }
  } catch (e: any) {
    console.warn('[accountStore] Audit read failed:', e.message);
  }
  const slice = lines.slice(Math.max(0, lines.length - cap)).reverse();
  const out: AuditEntry[] = [];
  for (const line of slice) {
    try { out.push(JSON.parse(line)); } catch { /* skip bad line */ }
  }
  return out;
}

// ── Resolvers ─────────────────────────────────────────────────────────────
/** Resolve a slot index from a username by matching against UserAccounts. */
export function resolveSlotByUsername(
  username: string,
  userAccounts: string[],
): number | null {
  if (!username) return null;
  const idx = userAccounts.findIndex(u => u && u.trim() === username.trim());
  return idx >= 0 && idx < 10 ? idx : null;
}

// ── Convenience recorders ─────────────────────────────────────────────────
/** Record a successful login. `slot` may be null for factory/anonymous. */
export function recordLogin(
  actor: string,
  slot: number | null,
  level: number,
  ip?: string,
): void {
  const nowIso = new Date().toISOString();
  const meta = loadAccountMeta();
  if (slot !== null && slot >= 0 && slot < 10) {
    meta.slots[slot] = {
      ...meta.slots[slot],
      lastLogin: nowIso,
      lastLoginIp: ip ?? null,
      loginCount: (meta.slots[slot].loginCount ?? 0) + 1,
    };
    saveAccountMeta(meta);
  } else if (actor === 'factory') {
    meta.factoryLastLogin = nowIso;
    meta.factoryLoginCount = (meta.factoryLoginCount ?? 0) + 1;
    saveAccountMeta(meta);
  }
  appendAudit({ kind: 'login', actor, slot, level, ip });
}

export function recordLoginFailure(actor: string, ip?: string): void {
  appendAudit({ kind: 'login_fail', actor, slot: null, level: 0, ip });
}

export function recordLogout(actor: string, slot: number | null, ip?: string): void {
  appendAudit({ kind: 'logout', actor, slot, level: 0, ip });
}

export function recordSave(
  actor: string,
  slot: number | null,
  level: number,
  route: string,
  detail?: string,
  ip?: string,
  diff?: AuditDiffEntry[],
): void {
  const entry: Omit<AuditEntry, 'ts'> = { kind: 'save', actor, slot, level, route, detail, ip };
  if (diff && diff.length > 0) entry.diff = diff;
  appendAudit(entry);
}

// ── Cloud links (pass 2) ──────────────────────────────────────────────────
/** Insert or update a CloudLink keyed by cloudUserId. Persists meta. */
export function upsertCloudLink(link: CloudLink): void {
  const meta = loadAccountMeta();
  const i = meta.cloudLinks.findIndex(l => l.cloudUserId === link.cloudUserId);
  if (i >= 0) meta.cloudLinks[i] = link;
  else meta.cloudLinks.push(link);
  saveAccountMeta(meta);
}

/** Remove a CloudLink by Django UserAccount UUID. Returns true if removed. */
export function removeCloudLink(cloudUserId: string): boolean {
  const meta = loadAccountMeta();
  const before = meta.cloudLinks.length;
  meta.cloudLinks = meta.cloudLinks.filter(l => l.cloudUserId !== cloudUserId);
  if (meta.cloudLinks.length === before) return false;
  saveAccountMeta(meta);
  return true;
}

export function findCloudLinkByUserId(cloudUserId: string): CloudLink | null {
  const meta = loadAccountMeta();
  return meta.cloudLinks.find(l => l.cloudUserId === cloudUserId) ?? null;
}

export function findCloudLinkByToken(linkToken: string): CloudLink | null {
  const meta = loadAccountMeta();
  return meta.cloudLinks.find(l => l.linkToken === linkToken) ?? null;
}

/** Stamp a successful remote login on the matching link. */
export function touchCloudLinkLogin(cloudUserId: string): void {
  const meta = loadAccountMeta();
  const link = meta.cloudLinks.find(l => l.cloudUserId === cloudUserId);
  if (!link) return;
  link.lastRemoteLogin = new Date().toISOString();
  saveAccountMeta(meta);
}

export function findCloudLinkByUsername(username: string): CloudLink | null {
  const meta = loadAccountMeta();
  const needle = String(username ?? '').toLowerCase();
  return meta.cloudLinks.find(l => l.username.toLowerCase() === needle) ?? null;
}

/**
 * Cache a scrypt hash of the user's password on the link.
 * Called after a successful *online* Django login so subsequent
 * offline logins can verify locally.
 */
export function setCloudLinkPassword(cloudUserId: string, password: string): void {
  const meta = loadAccountMeta();
  const link = meta.cloudLinks.find(l => l.cloudUserId === cloudUserId);
  if (!link) return;
  const salt = crypto.randomBytes(16);
  const hash = crypto.scryptSync(String(password), salt, 64);
  link.passwordSalt = salt.toString('hex');
  link.passwordHash = hash.toString('hex');
  link.passwordHashedAt = new Date().toISOString();
  saveAccountMeta(meta);
}

/** Constant-time verify a password against a link's cached hash. */
export function verifyCloudLinkPassword(link: CloudLink, password: string): boolean {
  if (!link.passwordHash || !link.passwordSalt) return false;
  try {
    const salt = Buffer.from(link.passwordSalt, 'hex');
    const expected = Buffer.from(link.passwordHash, 'hex');
    const actual = crypto.scryptSync(String(password ?? ''), salt, expected.length);
    return expected.length === actual.length && crypto.timingSafeEqual(expected, actual);
  } catch {
    return false;
  }
}
