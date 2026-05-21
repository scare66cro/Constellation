/**
 * firmwareInstaller.ts — orchestrates a Constellation Firmware Update.
 *
 * Given a loaded `.cfu` bundle, runs the per-component install in
 * sequence:
 *   1. Pi5 UI         (Phase 2 — skipped here)
 *   2. Pi5 bridge     (Phase 2 — skipped here)
 *   3. Per-orbit firmware push via orbitOtaPush.pushImage()
 *
 * Progress is broadcast via the `broadcastProgress` callback on the `firmware-progress`
 * channel.  Each broadcast is a full state snapshot so a late
 * subscriber sees the complete picture.
 *
 * Concurrency: ONE install at a time.  A second install attempt while
 * the first is in flight returns `InstallError('already_running')`.
 */

import {
    loadBundle, cleanupBundle, orbitComponents, componentPath,
    type LoadedBundle, type OrbitComponent,
} from './firmwareBundle.js';
import { pushImage, OtaPushError } from './orbitOtaPush.js';
import { FwBankId } from './orbitOtaClient.js';
import { resolveOrbitHost } from './orbitMbtcp.js';
import {
    snapshotFleet, resolveByRoleFromSnapshot,
    FleetResolverError,
    type FleetSnapshot,
} from './orbitFleetResolver.js';
import * as crypto from 'crypto';
import * as fs from 'fs/promises';

/** Callback used to broadcast InstallProgress snapshots to UI clients. */
export type ProgressBroadcastFn = (p: InstallProgress) => void;

// ─── Progress reporting types ────────────────────────────────────────────

export type ComponentState =
    | 'pending'      // queued, not started
    | 'pushing'      // image upload to LP in progress
    | 'rebooting'    // LP warm-reset triggered, waiting for it to come back
    | 'verifying'    // post-reboot ping / version check
    | 'done'         // success
    | 'skipped'      // intentionally not run (Phase 2 component etc.)
    | 'failed';      // error — see errorMessage

export interface ComponentProgress {
    /** Manifest key (e.g. "controller", "storage"). */
    name: string;
    /** Target IP from manifest (informational). */
    targetIp?: string;
    /** Target slot (-1 = controller). */
    slot?: number;
    /** Current state. */
    state: ComponentState;
    /** Bytes pushed so far (orbit components only). */
    bytesWritten?: number;
    /** Total bytes to push (orbit components only). */
    totalSize?: number;
    /** Error message if state==='failed'. */
    errorMessage?: string;
}

export interface InstallProgress {
    /** Bundle version from manifest. */
    bundleVersion: string;
    /** Overall state. */
    overallState: 'idle' | 'starting' | 'installing' | 'done' | 'failed';
    /** First-seen failure reason (overall failure). */
    failureReason?: string;
    /** Per-component state — order follows manifest iteration. */
    components: ComponentProgress[];
}

// ─── Singleton state ─────────────────────────────────────────────────────

let currentInstall: InstallProgress | null = null;
let currentBundle: LoadedBundle | null = null;

export class InstallError extends Error {
    constructor(public code: string, message: string) {
        super(message);
        this.name = 'InstallError';
    }
}

/** Snapshot of current install state, or null if idle. */
export function getCurrentInstall(): InstallProgress | null {
    return currentInstall ? structuredClone(currentInstall) : null;
}

/** Is an install in progress? */
export function isInstalling(): boolean {
    return currentInstall != null
        && currentInstall.overallState !== 'done'
        && currentInstall.overallState !== 'failed';
}

// ─── Main install routine ───────────────────────────────────────────────

export interface InstallOptions {
    /** Chunk size for orbitOtaPush (bytes).  Default 1024 — matches test_ota_push.ts. */
    chunkSize?: number;
    /** Per-chunk ack timeout (ms).  Default 180_000 — OSPI erase can be ~100s. */
    ackTimeoutMs?: number;
    /** Whole-push timeout per component (ms).  Default 10 minutes. */
    totalTimeoutMs?: number;
    /** If true, push the image but do NOT trigger reboot on the LP.
     *  Used for dry-run validation.  Default false. */
    skipReboot?: boolean;
    /** Explicit fleet of candidate IPs for role-based routing. When
     *  set (or when `ORBIT_FLEET` env var is set), the installer probes
     *  each IP for FwBankInfo.current_role and routes each manifest
     *  component to the IP whose role matches `comp.role`, instead of
     *  using the legacy slot→IP map (`resolveOrbitHost`).
     *  See `orbitFleetResolver.ts`. */
    fleet?: string[];
}

export async function installBundle(
    cfuPath: string,
    broadcastProgress: ProgressBroadcastFn | null,
    opts: InstallOptions = {},
): Promise<InstallProgress> {
    if (isInstalling()) {
        throw new InstallError('already_running', 'A firmware install is already in progress');
    }

    // Phase 1: load + validate bundle.  Failures here happen before we
    // mutate any chip state — safe to throw.
    const bundle = await loadBundle(cfuPath);
    currentBundle = bundle;

    // Initialise progress: every component starts pending.
    const progress: InstallProgress = {
        bundleVersion: bundle.manifest.version,
        overallState: 'starting',
        components: Object.entries(bundle.manifest.components).map(([name, comp]) => ({
            name,
            targetIp: 'ip' in comp ? comp.ip : undefined,
            slot: 'slot' in comp ? comp.slot : undefined,
            state: 'pending',
        })),
    };
    currentInstall = progress;
    broadcast(broadcastProgress);

    try {
        progress.overallState = 'installing';
        broadcast(broadcastProgress);

        // Phase 2 stubs — Pi5 UI / bridge updates skipped in this session.
        for (const cp of progress.components) {
            if (cp.name === 'pi5_ui' || cp.name === 'pi5_bridge') {
                cp.state = 'skipped';
                cp.errorMessage = 'Pi5 self-update not implemented in Phase 1';
                broadcast(broadcastProgress);
            }
        }

        // Probe the fleet for role-based routing (G1-bridge follow-up,
        // 2026-05-20). If `ORBIT_FLEET` is set (or opts.fleet is passed),
        // each LP in the list is asked for its FwBankInfo.current_role
        // in parallel and the results are cached for the duration of
        // this install. The per-component lookup downstream then uses
        // `resolveByRoleFromSnapshot(snapshot, comp.role)` instead of
        // the legacy slot→IP map. Snapshot is null when no fleet is
        // configured → falls back to `resolveOrbitHost(slot)`. See
        // `docs/lp-am2434-ota-hardening-plan.md` and orbitFleetResolver.ts.
        const fleetSnap = await snapshotFleet({ fleet: opts.fleet });
        if (fleetSnap) {
            const memberLine = fleetSnap.members
                .map(m => `${m.host}=role${m.currentRole}`).join(', ');
            const unprovLine = fleetSnap.unprovisioned
                .map(u => u.host).join(', ');
            const unrchLine  = fleetSnap.unreachable
                .map(u => `${u.host}(${u.error})`).join(', ');
            console.log(`[fw-install] fleet snapshot: members=[${memberLine || 'none'}] ` +
                        `legacy-fw=[${unprovLine || 'none'}] unreachable=[${unrchLine || 'none'}]`);
        } else {
            console.log('[fw-install] no ORBIT_FLEET configured — using slot→IP map (resolveOrbitHost)');
        }

        // Per-orbit firmware push.
        for (const { name, comp } of orbitComponents(bundle)) {
            const cp = progress.components.find(c => c.name === name);
            if (!cp) continue;

            try {
                await pushOneComponent(bundle, name, comp, cp, broadcastProgress, opts, fleetSnap);
            } catch (e) {
                cp.state = 'failed';
                cp.errorMessage = (e instanceof OtaPushError)
                    ? `${e.kind}: ${e.message}`
                    : (e as Error).message ?? String(e);
                broadcast(broadcastProgress);
                throw e;  // abort install on first failure
            }
        }

        progress.overallState = 'done';
        broadcast(broadcastProgress);
        return progress;

    } catch (e) {
        progress.overallState = 'failed';
        progress.failureReason = (e as Error).message ?? String(e);
        broadcast(broadcastProgress);
        throw e;
    } finally {
        // Cleanup extract dir regardless of outcome.
        if (currentBundle) {
            await cleanupBundle(currentBundle);
            currentBundle = null;
        }
    }
}

// ─── Per-component push ─────────────────────────────────────────────────

async function pushOneComponent(
    bundle: LoadedBundle,
    name: string,
    comp: OrbitComponent,
    cp: ComponentProgress,
    broadcastProgress: ProgressBroadcastFn | null,
    opts: InstallOptions,
    fleetSnap: FleetSnapshot | null,
): Promise<void> {
    // ── Resolve host ───────────────────────────────────────────────────
    // Precedence (most specific first):
    //   (a) Slot == -1 (CONTROLLER component): use comp.ip from the
    //       manifest directly. The bridge's controller LP is at a
    //       configuration-known IP; no discovery involved.
    //   (b) Fleet snapshot present (operator set ORBIT_FLEET or
    //       opts.fleet): match comp.role against the LPs we probed at
    //       install start. This is the authoritative path now that
    //       FwBankInfo.current_role exists (LP fw ≥ 0.A.188). Throws
    //       FleetResolverError on noMatch / ambiguous — both are
    //       deployment mistakes that the operator must fix.
    //   (c) Legacy slot→IP map (resolveOrbitHost). Used on bench rigs
    //       and any deployment that hasn't configured ORBIT_FLEET yet.
    //       The G1-bridge cross-check in pushImage() still validates
    //       comp.role vs FwBankInfo.current_role at push time, so a
    //       misrouted slot still aborts cleanly before the Bank-B erase.
    let host: string;
    if (comp.slot < 0) {
        host = comp.ip;
        console.log(`[fw-install] "${name}" routed to controller IP ${host} (slot=-1)`);
    } else if (fleetSnap) {
        const resolved = resolveByRoleFromSnapshot(fleetSnap, comp.role);
        if (!resolved) {
            // snapshot is non-null here so the helper either returns a
            // string or throws — defensive fall-through for type safety.
            throw new FleetResolverError('noMatch',
                `Fleet snapshot returned null for role=${comp.role}`);
        }
        host = resolved;
        const slotHost = resolveOrbitHost(comp.slot);
        if (host !== slotHost) {
            console.log(`[fw-install] "${name}" routed by role: role=${comp.role} → ${host} ` +
                        `(legacy slot=${comp.slot} → ${slotHost} suppressed)`);
        } else {
            console.log(`[fw-install] "${name}" routed by role: role=${comp.role} → ${host} ` +
                        `(matches legacy slot map)`);
        }
    } else {
        host = resolveOrbitHost(comp.slot);
        console.log(`[fw-install] "${name}" routed by slot map: slot=${comp.slot} → ${host} ` +
                    `(set ORBIT_FLEET to enable role-based routing)`);
    }

    cp.state = 'pushing';
    cp.bytesWritten = 0;
    cp.totalSize = 0;
    broadcast(broadcastProgress);

    const imgPath = componentPath(bundle, name);

    // ── Gap 4: re-hash file IMMEDIATELY before push ─────────────────────
    // The bundle's sha256 was verified on loadBundle(), but the file
    // could have been modified/corrupted between load and now. Re-check.
    // See docs/lp-am2434-ota-hardening-plan.md.
    const imgBytes = await fs.readFile(imgPath);
    const actualSha = crypto.createHash('sha256').update(imgBytes).digest('hex');
    if (actualSha !== comp.sha256.toLowerCase()) {
        cp.state = 'failed';
        cp.errorMessage = `image sha256 mismatch immediately before push (expected ${comp.sha256}, got ${actualSha}). ` +
                          `File modified after bundle load? Refusing to push.`;
        broadcast(broadcastProgress);
        throw new Error(`[fw-install] ${cp.errorMessage}`);
    }
    console.log(`[fw-install] pushing component "${name}" to ${host} (slot=${comp.slot} role=${comp.role}) from ${imgPath} (sha256=${actualSha.slice(0,16)}…)`);

    const result = await pushImage(host, imgPath, {
        chunkSize: opts.chunkSize ?? 1024,
        ackTimeoutMs: opts.ackTimeoutMs ?? 180_000,
        totalTimeoutMs: opts.totalTimeoutMs ?? 10 * 60_000,
        version: bundle.manifest.version,
        rebootAfterActivate: !opts.skipReboot,
        // Gap 3 (bridge-side) — bridge captures + logs the LP's pre-push
        // bank state for audit and rejects downgrades by default.
        rejectDowngrade: true,
        // Gap 1 (2026-05-20) — manifest's role is the source of truth for
        // "what should be flashed here". The bridge pre-flight cross-checks
        // it against FwBankInfo.currentRole (if the LP firmware is new
        // enough to emit it) AND forwards it to the LP via FwBeginUpdate
        // for the firmware-side gate. Wrong-role bundles abort before
        // burning a Bank-B erase cycle.
        expectedRole: comp.role,
        onBankInfo: (info) => {
            if (info) {
                // Same off-by-one fix as orbitOtaPush.ts — FwBankId enum
                // is BankA=0/BankB=1/Golden=2; the literal-1-with-"BankA"
                // pattern from commit 6042c59 printed empty strings.
                // See memories/repo/orbitotapush-bankid-off-by-one.md.
                const active = info.activeBank === FwBankId.BankA  ? info.bankAVersion
                             : info.activeBank === FwBankId.Golden ? info.goldenVersion
                             : info.bankBVersion;
                console.log(`[fw-install] component "${name}" @${host} pre-push: ` +
                            `active="${active}" bootCount=${info.bootCount}`);
            }
        },
        onProgress: (s) => {
            cp.bytesWritten = s.bytesWritten;
            cp.totalSize    = s.totalSize;
            // Throttle WS broadcasts to avoid flooding — broadcast on
            // every 5% change or state transition.  pushImage's
            // own progress callback already throttles, so most ticks
            // are spaced ~hundreds of ms apart; safe to forward each.
            broadcast(broadcastProgress);
        },
    });

    console.log(`[fw-install] component "${name}" pushed: bytes=${result.bytesSent}/${result.totalSize}`);

    if (opts.skipReboot) {
        // Dry-run finish.
        cp.state = 'done';
        broadcast(broadcastProgress);
        return;
    }

    // Activate triggered reboot.  Mark as rebooting and wait for ping.
    cp.state = 'rebooting';
    broadcast(broadcastProgress);

    // Give the LP a few seconds to actually warm-reset before we start
    // pinging it.  pushImage already sleeps briefly post-Activate.
    await new Promise(r => setTimeout(r, 5_000));

    // Phase-1 verification = a single ping check via TCP connect on the
    // OTA port.  Phase-2 = full ConfirmBoot version-string round-trip.
    cp.state = 'verifying';
    broadcast(broadcastProgress);

    const reachable = await waitForReachable(host, 60_000);
    if (!reachable) {
        cp.state = 'failed';
        cp.errorMessage = `LP did not return on ${host}:5503 within 60 s after Activate`;
        broadcast(broadcastProgress);
        throw new Error(cp.errorMessage);
    }

    cp.state = 'done';
    broadcast(broadcastProgress);
}

// ─── Helpers ────────────────────────────────────────────────────────────

import * as net from 'net';
import { LP_OTA_PORT } from './orbitOtaClient.js';

/** Best-effort liveness check: TCP-connect to host:LP_OTA_PORT, close immediately.
 *  Returns true once a connect succeeds within timeoutMs total wait. */
async function waitForReachable(host: string, timeoutMs: number): Promise<boolean> {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        const ok = await new Promise<boolean>((resolve) => {
            const sock = new net.Socket();
            const done = (val: boolean) => { try { sock.destroy(); } catch {} resolve(val); };
            sock.setTimeout(3_000);
            sock.once('connect', () => done(true));
            sock.once('timeout', () => done(false));
            sock.once('error',   () => done(false));
            sock.connect(LP_OTA_PORT, host);
        });
        if (ok) return true;
        await new Promise(r => setTimeout(r, 2_000));
    }
    return false;
}

function broadcast(broadcastProgress: ProgressBroadcastFn | null): void {
    if (!currentInstall || !broadcastProgress) return;
    try {
        broadcastProgress(currentInstall);
    } catch (e) {
        console.warn(`[fw-install] broadcast failed: ${(e as Error).message}`);
    }
}
