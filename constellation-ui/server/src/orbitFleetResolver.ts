/**
 * orbitFleetResolver.ts — Role-based LP discovery for OTA push routing.
 *
 * Background: until 2026-05-20, the bridge resolved OTA targets purely by
 * slot index → IP (env-var `ORBIT_IP_BASE + slot`, see
 * `orbitMbtcp.ts:resolveOrbitHost`). That works on the bench rig where
 * slot↔IP is conventional, but it cannot catch a mis-provisioned board
 * (lp_device_config.role assigned wrong) or a swapped-cables rack.
 *
 * Once `FwBankInfo.current_role` shipped (proto firmware.proto, LP fw
 * 0.A.188), each LP self-reports its provisioned OrbitRole on TCP connect.
 * This module probes a configured fleet of candidate IPs in parallel,
 * pulls each LP's currentRole, and builds a `role → host` map — so the
 * installer can route by the firmware-identity OrbitRole carried in
 * the .cfu manifest (`components.<name>.role`) instead of by slot.
 *
 * Fleet source (in order of precedence):
 *   1. `opts.fleet`              — explicit list of candidate IPs
 *   2. `process.env.ORBIT_FLEET` — comma-separated list, e.g.
 *                                  `10.47.27.2,10.47.27.3,10.47.27.4`
 *
 * If neither is supplied, `resolveByRole` returns `null` and the caller
 * falls back to the legacy slot map.
 *
 * Ambiguity handling: if two LPs report the same role, that's almost
 * always a deployment mistake — refusing to route is safer than picking
 * one. Throws `FleetResolverError('ambiguous', ...)` with both hosts in
 * the message so the operator can see the conflict.
 *
 * The resolver is stateless apart from the per-call probe — there is no
 * background cache or refresh loop. A typical install fires one probe at
 * the start, then routes 3-4 components from that single snapshot.
 *
 * Cross-references:
 *   - docs/lp-am2434-ota-hardening-plan.md (G1 / G1-bridge cross-check)
 *   - Nova_Firmware/lp_am2434/orbit_server/orbit_role.h (firmware enum)
 *   - constellation-ui/server/src/orbitOtaClient.ts:fetchBankInfo
 */

import { fetchBankInfo, type FwBankInfo } from './orbitOtaClient.js';

export interface FleetResolverOptions {
  /** Explicit list of candidate IPs. Overrides ORBIT_FLEET env. */
  fleet?: string[];
  /** Per-LP probe timeout in ms (default 2000 — matches fetchBankInfo). */
  timeoutMs?: number;
}

export interface FleetMember {
  host: string;
  currentRole: number;
  bankInfo: FwBankInfo;
}

export interface FleetSnapshot {
  /** Members successfully probed AND reporting current_role. Older LP
   *  firmwares (pre-0.A.188) won't have current_role; those are dropped
   *  from this list and reported under `unprovisioned` instead. */
  members: FleetMember[];
  /** Probed hosts that responded with FwBankInfo but no current_role
   *  field (legacy firmware). The caller can warn the operator that
   *  these boards should be reflashed via JTAG before OTA routing works
   *  for them. */
  unprovisioned: { host: string; bankInfo: FwBankInfo }[];
  /** Hosts that didn't respond within the probe timeout. */
  unreachable: { host: string; error: string }[];
}

export class FleetResolverError extends Error {
  constructor(public readonly kind: string, msg: string) {
    super(msg);
    this.name = 'FleetResolverError';
  }
}

function readFleetFromEnv(): string[] | null {
  const raw = process.env.ORBIT_FLEET;
  if (!raw) return null;
  const items = raw.split(',').map(s => s.trim()).filter(s => s.length > 0);
  return items.length > 0 ? items : null;
}

/**
 * Probe each candidate IP for its FwBankInfo and bucket the results.
 * Never throws — failure modes are reported in the snapshot.
 */
export async function snapshotFleet(
  opts: FleetResolverOptions = {},
): Promise<FleetSnapshot | null> {
  const fleet = opts.fleet ?? readFleetFromEnv();
  if (!fleet || fleet.length === 0) return null;

  const timeoutMs = opts.timeoutMs ?? 2000;
  const outcomes = await Promise.all(
    fleet.map(host => fetchBankInfo(host, { timeoutMs }))
  );

  const snap: FleetSnapshot = { members: [], unprovisioned: [], unreachable: [] };
  for (const o of outcomes) {
    if (!o.reachable) {
      snap.unreachable.push({ host: o.host, error: o.error });
      continue;
    }
    if (o.bankInfo.currentRole === undefined) {
      snap.unprovisioned.push({ host: o.host, bankInfo: o.bankInfo });
      continue;
    }
    snap.members.push({
      host: o.host,
      currentRole: o.bankInfo.currentRole,
      bankInfo: o.bankInfo,
    });
  }
  return snap;
}

/**
 * Find the single host whose current_role matches `role` in the
 * provided snapshot. Returns null if `snapshot` is null (no fleet
 * configured). Throws on 0 matches or >1 matches.
 */
export function resolveByRoleFromSnapshot(
  snapshot: FleetSnapshot | null,
  role: number,
): string | null {
  if (snapshot === null) return null;
  const matches = snapshot.members.filter(m => m.currentRole === role);
  if (matches.length === 0) {
    const seen = snapshot.members
      .map(m => `${m.host}=role${m.currentRole}`).join(', ');
    const unprov = snapshot.unprovisioned.map(u => u.host).join(', ');
    const unrch  = snapshot.unreachable.map(u => `${u.host}(${u.error})`).join(', ');
    throw new FleetResolverError('noMatch',
      `No LP in fleet reports current_role=${role}. ` +
      `Reachable+provisioned: [${seen || 'none'}]. ` +
      `Reachable+legacy-fw: [${unprov || 'none'}]. ` +
      `Unreachable: [${unrch || 'none'}].`);
  }
  if (matches.length > 1) {
    const hosts = matches.map(m => m.host).join(', ');
    throw new FleetResolverError('ambiguous',
      `Multiple LPs in fleet claim role=${role}: [${hosts}]. ` +
      `This is a provisioning mistake — exactly one board should hold each role. ` +
      `Re-provision via /iot/lp/provision before retrying.`);
  }
  return matches[0].host;
}

/**
 * One-shot convenience: probe + resolve in a single call. Most callers
 * should use `snapshotFleet` once and `resolveByRoleFromSnapshot` per
 * component instead, to avoid re-probing for each lookup.
 */
export async function resolveByRole(
  role: number,
  opts: FleetResolverOptions = {},
): Promise<string | null> {
  const snap = await snapshotFleet(opts);
  return resolveByRoleFromSnapshot(snap, role);
}
