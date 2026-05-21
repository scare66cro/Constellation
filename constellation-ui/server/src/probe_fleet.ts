/**
 * probe_fleet.ts — One-shot fleet probe for OTA readiness check.
 *
 * Hits the OTA port on each given IP, captures FwBankInfo (if any),
 * and prints a per-host readout. Use before running a multi-board OTA
 * install to confirm which boards are reachable, which firmware version
 * each is running, and which boards self-report a role (current_role —
 * proto field 11, LP fw >= 0.A.188).
 *
 * Usage:
 *   tsx src/probe_fleet.ts                      # uses ORBIT_FLEET env
 *   tsx src/probe_fleet.ts 10.47.27.1 .2 .3 .4  # explicit
 *
 * Each row prints:
 *   <host>  <result>  active=<version>  role=<n or ?>  banks=<A/B/G valid>
 *
 *   result is one of:
 *     OK       — reachable + currentRole present (ready for role-based OTA)
 *     LEGACY   — reachable, BankInfo decoded, but no currentRole field
 *                (LP fw < 0.A.188 — needs a JTAG reflash before role
 *                 routing will pick it up)
 *     OFFLINE  — TCP connect failed or BankInfo not received
 *
 * Side-effect-free: never writes to any board.
 */

import { fetchBankInfo } from './orbitOtaClient.js';

function roleLabel(n: number | undefined): string {
  switch (n) {
    case 0:  return 'CONTROLLER';
    case 1:  return 'STORAGE';
    case 2:  return 'GDC';
    case 3:  return 'TRITON';
    case 4:  return 'PULSAR';
    default: return n === undefined ? '?'        : `unknown(${n})`;
  }
}

async function main(): Promise<void> {
  let hosts = process.argv.slice(2);
  if (hosts.length === 0) {
    const env = process.env.ORBIT_FLEET;
    if (!env) {
      console.error('Usage: tsx probe_fleet.ts <host> [host ...]');
      console.error('  Or set ORBIT_FLEET=10.47.27.1,10.47.27.2,...');
      process.exit(2);
    }
    hosts = env.split(',').map(s => s.trim()).filter(s => s.length > 0);
  }

  console.log(`[probe-fleet] probing ${hosts.length} host(s): ${hosts.join(', ')}`);
  console.log('');

  const outcomes = await Promise.all(
    hosts.map(h => fetchBankInfo(h, { timeoutMs: 2500 }))
  );

  // Per-row print.
  let okCount = 0, legacyCount = 0, offlineCount = 0;
  for (const o of outcomes) {
    if (!o.reachable) {
      offlineCount++;
      console.log(`  ${o.host.padEnd(15)}  OFFLINE   ${o.error}`);
      continue;
    }
    const bi = o.bankInfo;
    const active = bi.activeBank === 0 ? `bankA "${bi.bankAVersion}"` :
                   bi.activeBank === 1 ? `bankB "${bi.bankBVersion}"` :
                                         `golden "${bi.goldenVersion}"`;
    const banks = `${bi.bankAValid ? 'A' : '-'}${bi.bankBValid ? 'B' : '-'}`;
    if (bi.currentRole === undefined) {
      legacyCount++;
      console.log(`  ${o.host.padEnd(15)}  LEGACY    active=${active}  role=?  banks=${banks}  ` +
                  `bootCount=${bi.bootCount}`);
    } else {
      okCount++;
      const label = roleLabel(bi.currentRole);
      console.log(`  ${o.host.padEnd(15)}  OK        active=${active}  role=${bi.currentRole}(${label})  ` +
                  `banks=${banks}  bootCount=${bi.bootCount}`);
    }
  }

  console.log('');
  console.log(`[probe-fleet] summary: ${okCount} OK, ${legacyCount} LEGACY (pre-0.A.188), ${offlineCount} OFFLINE`);

  // Role-based routing readiness check.
  if (okCount > 0) {
    const roleCounts = new Map<number, string[]>();
    for (const o of outcomes) {
      if (o.reachable && o.bankInfo.currentRole !== undefined) {
        const r = o.bankInfo.currentRole;
        if (!roleCounts.has(r)) roleCounts.set(r, []);
        roleCounts.get(r)!.push(o.host);
      }
    }
    const dups = [...roleCounts.entries()].filter(([_, h]) => h.length > 1);
    if (dups.length > 0) {
      console.log('');
      console.log('[probe-fleet] AMBIGUITY DETECTED — multiple boards claim same role:');
      for (const [r, h] of dups) {
        console.log(`  role=${r}(${roleLabel(r)}): ${h.join(', ')}`);
      }
      console.log('  Role-based OTA routing will throw FleetResolverError("ambiguous") until this is fixed.');
    }
  }

  if (legacyCount > 0 || offlineCount > 0) {
    console.log('');
    console.log('[probe-fleet] NOTE: LEGACY + OFFLINE boards will not be reachable via role-based OTA.');
    console.log('  - LEGACY  → JTAG flash via Flash-LP.ps1 with a >= 0.A.188 build to expose current_role.');
    console.log('  - OFFLINE → check power/network/IP; or board may be running pre-OTA firmware (no :5503 listener).');
  }
}

main().catch(err => {
  console.error('[probe-fleet] fatal:', err);
  process.exit(1);
});
