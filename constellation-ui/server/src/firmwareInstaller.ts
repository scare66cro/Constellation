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
import { resolveOrbitHost } from './orbitMbtcp.js';
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

        // Per-orbit firmware push.
        for (const { name, comp } of orbitComponents(bundle)) {
            const cp = progress.components.find(c => c.name === name);
            if (!cp) continue;

            try {
                await pushOneComponent(bundle, name, comp, cp, broadcastProgress, opts);
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
): Promise<void> {
    // Resolve host.  Slot==-1 means CONTROLLER; pushImage just needs a
    // direct IP, no slot magic required.  For orbit slots 0..7 we go
    // through resolveOrbitHost so the bridge's bench IP mapping stays
    // authoritative (and matches what /iot/orbit/ota/version reads).
    const host = comp.slot >= 0 ? resolveOrbitHost(comp.slot) : comp.ip;

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
        onBankInfo: (info) => {
            if (info) {
                const active = info.activeBank === 1 ? info.bankAVersion
                             : info.activeBank === 2 ? info.goldenVersion
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
