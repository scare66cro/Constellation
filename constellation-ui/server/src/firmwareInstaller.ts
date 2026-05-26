/**
 * firmwareInstaller.ts — orchestrates a Constellation Firmware Update via
 * the Nova-side OTA broker (UART-airgap migration, Phase 4).
 *
 * The previous incarnation of this file (Phase 1B) opened TCP sockets
 * from the Pi5 directly to each orbit LP's `:5503` listener via
 * `orbitOtaPush.pushImage()`, with role resolution either through the
 * legacy slot map (`resolveOrbitHost`) or `snapshotFleet` /
 * `resolveByRoleFromSnapshot` from `orbitFleetResolver`. That pattern
 * relied on the bench Pi5 being dual-homed onto the equipment LAN
 * (10.47.27.0/24) — which violates CLAUDE.md invariant #12 (the
 * production Pi5 has NO IP route to the LPs; the only path is
 * UART → Nova → CPSW → orbit LPs).
 *
 * As of Phase 4 (2026-05-23, bench-validated against 0.A.193) all
 * bridge↔LP traffic flows through the Nova-side broker
 * (`nova_ota_broker.c` + `nova_ota_push.c`) using the FwInstall*
 * envelopes (proto/agristar/firmware.proto §"Pi5 ↔ Nova
 * install-orchestration layer", envelope tags 130-142):
 *
 *   Pi5 → Nova     FwFleetProbe                            (probe)
 *                  FwInstallBegin                          (enter install mode)
 *                    [ FwInstallComponentBegin             (per component)
 *                      FwInstallChunk*                     (streamed open-loop)
 *                      FwInstallComponentFinalize          (close component)
 *                    ]+
 *                  FwInstallComplete                       (exit install mode)
 *                  FwInstallAbort                          (cancellation)
 *
 *   Nova → Pi5     FwFleetSnapshot                         (probe reply)
 *                  FwInstallProgress (every state transition + per ~5% PUSHING)
 *                  FwInstallResult   (overall + per-component terminals)
 *
 * Controller (slot=-1) component: Nova handles this via a self-update
 * path that activates Bank B and reboots itself. UART falls silent; the
 * Pi5 reconciles by waiting for the bridge's reconnect edge + a
 * VersionInfo envelope that matches the manifest's controller version.
 *
 * Ordering invariant: orbit components install FIRST; the controller
 * component installs LAST. When the controller activates Nova reboots,
 * which drops install state — anything queued after the controller
 * would never reach the broker. The pre-sort in `installBundle` enforces
 * this regardless of manifest iteration order.
 *
 * Progress is broadcast via `broadcastProgress` on the
 * `firmware-progress` WS channel. Each broadcast is a full state
 * snapshot so a late subscriber sees the complete picture.
 *
 * Concurrency: ONE install at a time. A second install attempt while
 * the first is in flight returns `InstallError('already_running')`.
 *
 * Retired in this phase (kept on disk for Phase 5 deletion, see #6 in
 * the Phase 4 plan): `orbitOtaPush.ts`, `orbitFleetResolver.ts`,
 * `probe_fleet.ts`. `InstallOptions.fleet` is now ignored (broker
 * discovers); `ORBIT_FLEET` env var is also ignored bridge-side. Both
 * options stay on the API for back-compat until Phase 5.
 */

import {
    loadBundle, cleanupBundle, isOrbitComponent, componentPath,
    type LoadedBundle, type OrbitComponent,
} from './firmwareBundle.js';
import type { NovaSerialBridge } from './novaSerialBridge.js';
import {
    pbDecodeFields, pbGetVarint, pbGetString, pbGetAllSubmsg,
    MSG_FW_INSTALL_PROGRESS, MSG_FW_INSTALL_RESULT,
} from './novaSerialBridge.js';
import * as crypto from 'crypto';
import * as fs from 'fs/promises';

/** Callback used to broadcast InstallProgress snapshots to UI clients. */
export type ProgressBroadcastFn = (p: InstallProgress) => void;

// ─── Progress reporting types (UNCHANGED — preserved for UI compat) ────

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
    /** Per-component state — order follows install (orbits first, controller last). */
    components: ComponentProgress[];
}

// ─── Singleton state ─────────────────────────────────────────────────────

let currentInstall: InstallProgress | null = null;
let currentBundle: LoadedBundle | null = null;
let novaBridge: NovaSerialBridge | null = null;

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

/** Inject the Nova bridge handle. Called once by index.ts after the
 *  bridge is constructed. installBundle() throws InstallError('no_bridge')
 *  if the bridge hasn't been wired yet. */
export function setNovaBridge(bridge: NovaSerialBridge): void {
    novaBridge = bridge;
}

// ─── Main install routine ───────────────────────────────────────────────

export interface InstallOptions {
    /** Chunk size for FwInstallChunk envelopes (bytes). Default 1024. */
    chunkSize?: number;
    /** Per-chunk ack timeout (ms). RETIRED in Phase 4 — chunks are
     *  open-loop, no per-chunk ack. Kept for API compat. */
    ackTimeoutMs?: number;
    /** Whole-push timeout per component (ms). Default 10 minutes. */
    totalTimeoutMs?: number;
    /** If true, push the image but do NOT trigger reboot on the LP.
     *  Used for dry-run validation. Default false.
     *  NOTE: broker does not currently honour this — controller reboot
     *  is unconditional. The flag still short-circuits the post-reboot
     *  reconciliation logic for orbit components though. */
    skipReboot?: boolean;
    /** RETIRED Phase 4 — broker discovers the fleet. Ignored on the
     *  bridge side; pre-Phase-4 callers that set this get back-compat. */
    fleet?: string[];
    /** Allow downgrade by version-string comparison. Default false. */
    allowDowngrade?: boolean;
}

export async function installBundle(
    cfuPath: string,
    broadcastProgress: ProgressBroadcastFn | null,
    opts: InstallOptions = {},
): Promise<InstallProgress> {
    if (isInstalling()) {
        throw new InstallError('already_running', 'A firmware install is already in progress');
    }
    if (!novaBridge) {
        throw new InstallError('no_bridge', 'Nova bridge not wired into firmwareInstaller (setNovaBridge)');
    }
    if (!novaBridge.isConnected()) {
        throw new InstallError('bridge_down', 'Nova bridge is not currently connected to the controller');
    }
    const bridge = novaBridge;

    // Phase 1: load + validate bundle. Failures here happen before we
    // touch broker state — safe to throw.
    const bundle = await loadBundle(cfuPath);
    currentBundle = bundle;

    // Pre-sort components so orbits install first and controller installs
    // LAST. When controller activates Nova reboots; anything queued after
    // it would never reach the broker. Phase-2 stubs (pi5_*) come out
    // ordered with their manifest position but are immediately marked
    // 'skipped' in the progress view.
    const orderedEntries = sortComponentsControllerLast(bundle);

    // Initialise progress in display order.
    const progress: InstallProgress = {
        bundleVersion: bundle.manifest.version,
        overallState: 'starting',
        components: orderedEntries.map(({ name, comp }) => ({
            name,
            targetIp: 'ip' in comp ? comp.ip : undefined,
            slot:     'slot' in comp ? comp.slot : undefined,
            state:    'pending',
        })),
    };
    currentInstall = progress;
    broadcast(broadcastProgress);

    // Maintain a Map<component_index → latest progress envelope> (O1).
    // Wire-side component_index is assigned as we send each
    // FwInstallComponentBegin (orbits 0..N-1, controller N if present).
    const latestProgressByIndex = new Map<number, BrokerProgress>();
    // Wire componentIndex → UI ComponentProgress. Populated by the install
    // loop as it sends each FwInstallComponentBegin (orbits 0..N-1,
    // controller N). Empty before the loop starts, which is correct —
    // the broker won't emit any FwInstallProgress until our first Begin.
    const cpByIndex = new Map<number, ComponentProgress>();
    let resultEnvelope: BrokerResult | null = null;

    // Per-component waiters for the windowed chunk pump (Phase 4.1). Each
    // entry resolves when a FwInstallProgress arrives for componentIndex
    // with bytesWritten >= target. Registered by `waitForProgressAtLeast`
    // (inside pushOneComponent) and drained by the shared `onMsg` below.
    // The waiter design routes through the SINGLE 'message' subscription
    // — adding a second listener would re-decode every envelope and risk
    // double-counting. Per the Phase 4.1 brief, keep one subscription.
    const progressWaiters = new Map<number, ProgressWaiter[]>();

    // Wire up a single 'message' listener for the broker's progress
    // and result envelopes. We need this for the WHOLE install,
    // including the controller-reboot reconciliation, so it's set up
    // once here and torn down in finally.
    const onMsg = (msgId: number, data: Buffer): void => {
        if (msgId === MSG_FW_INSTALL_PROGRESS) {
            const p = decodeProgress(data);
            latestProgressByIndex.set(p.componentIndex, p);
            const cp = cpByIndex.get(p.componentIndex);
            if (cp) applyProgressToCp(cp, p);
            // Resolve any in-flight chunk-pump waiters whose target byte
            // count has been reached. We resolve on FAILED too so the
            // pump unblocks immediately and surfaces the broker error
            // instead of hanging until the per-chunk ack timeout.
            const waiters = progressWaiters.get(p.componentIndex);
            if (waiters && waiters.length > 0) {
                const stillWaiting: ProgressWaiter[] = [];
                for (const w of waiters) {
                    if (p.state === FwInstallComponentState.FAILED
                        || p.bytesWritten >= w.target) {
                        w.resolve(p);
                    } else {
                        stillWaiting.push(w);
                    }
                }
                if (stillWaiting.length > 0) {
                    progressWaiters.set(p.componentIndex, stillWaiting);
                } else {
                    progressWaiters.delete(p.componentIndex);
                }
            }
            broadcast(broadcastProgress);
        } else if (msgId === MSG_FW_INSTALL_RESULT) {
            resultEnvelope = decodeResult(data);
        }
    };
    bridge.on('message', onMsg);

    try {
        progress.overallState = 'installing';
        broadcast(broadcastProgress);

        // Phase-2 stubs — Pi5 UI / bridge updates skipped in Phase 4.
        for (const cp of progress.components) {
            if (cp.name === 'pi5_ui' || cp.name === 'pi5_bridge') {
                cp.state = 'skipped';
                cp.errorMessage = 'Pi5 self-update not implemented in Phase 4';
                broadcast(broadcastProgress);
            }
        }

        // Broker-side discovery — single FwFleetProbe round-trip, just
        // for the log (we don't need its content for routing; the broker
        // does its own role-match per FwInstallComponentBegin). Failure
        // is non-fatal because the broker will surface the same
        // diagnostics via FwInstallProgress(state=FAILED).
        try {
            const raw = await bridge.sendFleetProbe(5000);
            logFleetSnapshot(raw);
        } catch (e) {
            console.warn(`[fw-install] FwFleetProbe failed (continuing): ${(e as Error).message}`);
        }

        // Filter down to install-eligible components and assign wire
        // indices in order. Pi5 stubs and any non-orbit junk are NOT
        // sent over the wire (the broker has no install path for them).
        const installable = orderedEntries.filter(e =>
            isOrbitComponent(e.comp) && e.name !== 'pi5_ui' && e.name !== 'pi5_bridge'
        );

        if (installable.length === 0) {
            throw new InstallError('empty_install',
                'Bundle contains no installable components (only Pi5 stubs?)');
        }

        // FwInstallBegin.
        const manifestSha = bundleManifestSha256(bundle);
        const chunkSize = opts.chunkSize ?? 1024;
        const beginInner = Buffer.concat([
            encField(1, 2, Buffer.from(bundle.manifest.version, 'utf8')),
            encField(2, 2, manifestSha),
            encField(3, 0, encVarint(installable.length)),
            encField(4, 0, encVarint(chunkSize)),
            ...(opts.allowDowngrade ? [encField(5, 0, encVarint(1))] : []),
        ]);
        console.log(`[fw-install] FwInstallBegin: bundle=${bundle.manifest.version} ` +
                    `components=${installable.length} chunkSize=${chunkSize} ` +
                    `allowDowngrade=${!!opts.allowDowngrade}`);
        bridge.sendFwInstallBegin(beginInner);

        // Per-component push, in pre-sorted order.
        for (let i = 0; i < installable.length; i++) {
            const { name, comp } = installable[i];
            // `comp` was verified by the filter above; narrow for TS.
            if (!isOrbitComponent(comp)) continue;
            const cp = progress.components.find(c => c.name === name);
            if (!cp) continue;
            cpByIndex.set(i, cp);

            try {
                await pushOneComponent(
                    bridge, bundle, name, comp, i, cp, chunkSize, opts,
                    broadcastProgress, latestProgressByIndex, progressWaiters,
                );
            } catch (e) {
                cp.state = 'failed';
                cp.errorMessage = (e as Error).message ?? String(e);
                broadcast(broadcastProgress);
                throw e;  // abort install on first failure
            }
        }

        // FwInstallComplete — broker exits install mode + emits result.
        // For the controller-last case the broker has already rebooted
        // by the time we get here; pushOneComponent's reconciliation
        // path waits for the reconnect, and after reconnect the broker
        // is back in IDLE. Sending Complete is a no-op for the broker
        // but completes the bridge-side state machine cleanly.
        console.log('[fw-install] FwInstallComplete');
        sendFwInstallComplete(bridge);

        // Await FwInstallResult for up to 5 s. If we already saw it
        // (e.g. after a per-component FAILED), short-circuit.
        await waitFor(() => resultEnvelope !== null, 5_000, 50);

        if (resultEnvelope) {
            reconcileResultEnvelope(resultEnvelope, latestProgressByIndex, cpByIndex);
        }

        // Overall outcome — if any component is failed, mark overall failed.
        const anyFailed = progress.components.some(c => c.state === 'failed');
        if (anyFailed) {
            progress.overallState = 'failed';
            const firstFail = progress.components.find(c => c.state === 'failed');
            progress.failureReason = firstFail?.errorMessage ?? 'one or more components failed';
        } else {
            progress.overallState = 'done';
        }
        broadcast(broadcastProgress);
        return progress;

    } catch (e) {
        // Best-effort abort so the broker releases install mode + the
        // safety gate on follow-up installs.
        try {
            const abortInner = encField(1, 2, Buffer.from(
                `Pi5 abort: ${(e as Error).message?.slice(0, 120) ?? 'unknown'}`, 'utf8'));
            bridge.sendFwInstallAbort(abortInner);
            // Brief wait for the broker's overall=ABORTED FwInstallResult.
            await waitFor(() => resultEnvelope !== null, 2_000, 50);
        } catch (abortErr) {
            console.warn(`[fw-install] abort send failed: ${(abortErr as Error).message}`);
        }

        progress.overallState = 'failed';
        progress.failureReason ??= (e as Error).message ?? String(e);
        broadcast(broadcastProgress);
        throw e;
    } finally {
        bridge.off('message', onMsg);
        if (currentBundle) {
            await cleanupBundle(currentBundle);
            currentBundle = null;
        }
    }
}

// ─── Per-component push ─────────────────────────────────────────────────

async function pushOneComponent(
    bridge: NovaSerialBridge,
    bundle: LoadedBundle,
    name: string,
    comp: OrbitComponent,
    componentIndex: number,
    cp: ComponentProgress,
    chunkSize: number,
    opts: InstallOptions,
    broadcastProgress: ProgressBroadcastFn | null,
    latestProgressByIndex: Map<number, BrokerProgress>,
    progressWaiters: Map<number, ProgressWaiter[]>,
): Promise<void> {
    const imgPath = componentPath(bundle, name);

    // Re-hash image bytes immediately before pushing (Gap 4 carried
    // forward from Phase 1B). The bundle's sha256 was verified at
    // loadBundle() but the file could have been modified since.
    const imgBytes = await fs.readFile(imgPath);
    const actualSha = crypto.createHash('sha256').update(imgBytes).digest('hex');
    if (actualSha !== comp.sha256.toLowerCase()) {
        cp.state = 'failed';
        cp.errorMessage = `image sha256 mismatch immediately before push ` +
            `(expected ${comp.sha256}, got ${actualSha}). ` +
            `File modified after bundle load? Refusing to push.`;
        broadcast(broadcastProgress);
        throw new Error(`[fw-install] ${cp.errorMessage}`);
    }
    const totalSize = imgBytes.length;

    cp.state = 'pushing';
    cp.bytesWritten = 0;
    cp.totalSize = totalSize;
    broadcast(broadcastProgress);

    console.log(`[fw-install] component[${componentIndex}] "${name}" role=${comp.role} slot=${comp.slot} ` +
                `(${totalSize} B sha256=${actualSha.slice(0, 16)}…) → broker`);

    // FwInstallComponentBegin. component_index + role are force-encoded
    // because 0 is a valid value for both (CONTROLLER role = 0, first
    // component index = 0). See CLAUDE.md invariant #1.
    const imgShaBuf = Buffer.from(actualSha, 'hex');
    const compBeginInner = Buffer.concat([
        encField(1, 2, Buffer.from(name, 'utf8')),
        encField(2, 0, encVarint(componentIndex)),           // FORCE
        encField(3, 0, encVarint(comp.role)),                 // FORCE
        encField(4, 0, encVarint(totalSize)),
        encField(5, 2, imgShaBuf),
        encField(6, 2, Buffer.from(bundle.manifest.version, 'utf8')),
    ]);
    bridge.sendFwInstallComponentBegin(compBeginInner);

    // Windowed chunk pump (Phase 4.1, 2026-05-24). The Phase 4 open-loop
    // stream overran the broker's 16-slot × 2 KB chunk ring (32 KB) during
    // the target LP's ~5 s Bank-B erase window: the broker task is blocked
    // in NovaOtaPush_BeginToLp's synchronous lwip_recv waiting for the LP's
    // FwUpdateStatus(ERASING→RECEIVING) while the bridge streams ~90
    // chunks/s. Ring fills in ~178 ms, broker emits FwInstallProgress
    // (FAILED, error_code=104 "broker chunk ring full").
    //
    // Fix: send up to WINDOW chunks ahead of the broker's ack
    // (FwInstallProgress.bytesWritten), then wait for ack to advance
    // before sending more. WINDOW=4 keeps us inside the 16-slot ring
    // with 4x headroom and avoids paying full Pi5↔Nova RTT per chunk
    // in steady state. Profile + tune after first green push.
    // (Hardcoded per the brief — do not move to opts.)
    const WINDOW = 1;  // 2026-05-26: WINDOW=4 bursts 4400B in 48ms which
                       // overruns Nova's UART RX framer (one CRC-mismatch
                       // 843-byte truncation per attempt, chunks dropped).
                       // WINDOW=1 paces 1 chunk per ack — broker now emits
                       // PUSHING per chunk so this is fast in steady state.
    const windowBytes = WINDOW * chunkSize;
    // First-chunk ack must cover the LP's Bank-B erase (~2-5 s observed,
    // see bridge-broker-install-begin-abort-debug.md). Subsequent chunks
    // are steady-state LP-write rate (~10-50 ms each).
    const FIRST_ACK_TIMEOUT_MS = 30_000;
    const STEADY_ACK_TIMEOUT_MS = 5_000;
    let nextOffset = 0;
    let ackedOffset = 0;
    let sawFirstAck = false;
    while (ackedOffset < totalSize) {
        // Refill the in-flight window.
        while (nextOffset < totalSize
               && (nextOffset - ackedOffset) < windowBytes) {
            const slice = imgBytes.subarray(
                nextOffset, Math.min(nextOffset + chunkSize, totalSize));
            const chunkInner = Buffer.concat([
                encField(1, 0, encVarint(componentIndex)),   // FORCE
                encField(2, 0, encVarint(nextOffset)),        // FORCE
                encField(3, 2, slice),
            ]);
            bridge.sendFwInstallChunk(chunkInner);
            nextOffset += slice.length;
        }
        // Wait for the broker to ack at least one more chunk than we've
        // already accounted for. We target ackedOffset + 1 (any forward
        // movement satisfies us — the broker's FwInstallProgress cadence
        // is every ~5% of total_size + every state transition, so each
        // event typically advances many chunks at once).
        const target = ackedOffset + 1;
        const timeoutMs = sawFirstAck ? STEADY_ACK_TIMEOUT_MS : FIRST_ACK_TIMEOUT_MS;
        let progress: BrokerProgress;
        try {
            progress = await waitForProgressAtLeast(
                componentIndex, target, timeoutMs,
                latestProgressByIndex, progressWaiters,
            );
        } catch (e) {
            cp.state = 'failed';
            cp.errorMessage = `chunk ack timeout at offset ${ackedOffset} ` +
                `(target ${target}, waited ${timeoutMs} ms): ${(e as Error).message}`;
            broadcast(broadcastProgress);
            // Best-effort abort so the broker releases install mode before
            // the installBundle()-level catch tries the same. Idempotent.
            try {
                const abortInner = encField(1, 2, Buffer.from(
                    `chunk ack timeout component[${componentIndex}] @${ackedOffset}`, 'utf8'));
                bridge.sendFwInstallAbort(abortInner);
            } catch { /* installBundle catch will retry */ }
            throw new Error(cp.errorMessage);
        }
        sawFirstAck = true;
        if (progress.state === FwInstallComponentState.FAILED) {
            // Broker surfaced an error (ring full / push error / etc.) —
            // bubble up immediately so installBundle aborts cleanly.
            cp.state = 'failed';
            cp.errorMessage = renderProgressError(progress, comp);
            broadcast(broadcastProgress);
            throw new Error(cp.errorMessage);
        }
        ackedOffset = progress.bytesWritten;
        // Update local cp optimistically too (onMsg already did this from
        // applyProgressToCp, but keep symmetry with the old loop).
        cp.bytesWritten = ackedOffset;
    }

    // FwInstallComponentFinalize.
    const finalizeInner = Buffer.concat([
        encField(1, 0, encVarint(componentIndex)),           // FORCE
        encField(2, 2, imgShaBuf),
    ]);
    bridge.sendFwInstallComponentFinalize(finalizeInner);

    // Branch on controller vs orbit. The controller component (slot=-1)
    // is the LAST item in `installable` (sortComponentsControllerLast).
    // Nova self-update path: after Finalize, the broker emits
    // ACTIVATING then REBOOTING, then UART falls silent as Nova warm-
    // resets. The Pi5 reconciles by waiting for the bridge's reconnect
    // edge + the first VersionInfo envelope.
    const isController = comp.slot < 0;
    const totalTimeoutMs = opts.totalTimeoutMs ?? 10 * 60_000;

    if (isController) {
        await reconcileControllerReboot(bridge, comp, bundle.manifest.version, cp, broadcastProgress, totalTimeoutMs);
        return;
    }

    // Orbit components: terminal state comes from a FwInstallProgress
    // event (state=DONE / CONFIRMED / FAILED) OR from the final
    // FwInstallResult.components[]. Wait up to totalTimeoutMs for the
    // broker to land one of those states for our component_index.
    const ok = await waitFor(() => {
        const lp = latestProgressByIndex.get(componentIndex);
        if (!lp) return false;
        return lp.state === FwInstallComponentState.DONE
            || lp.state === FwInstallComponentState.CONFIRMED
            || lp.state === FwInstallComponentState.FAILED;
    }, totalTimeoutMs, 100);

    const finalProgress = latestProgressByIndex.get(componentIndex);
    if (!ok || !finalProgress) {
        cp.state = 'failed';
        cp.errorMessage = `broker did not report terminal state for component[${componentIndex}] within ${totalTimeoutMs} ms`;
        broadcast(broadcastProgress);
        throw new Error(cp.errorMessage);
    }
    if (finalProgress.state === FwInstallComponentState.FAILED) {
        cp.state = 'failed';
        cp.errorMessage = renderProgressError(finalProgress, comp);
        broadcast(broadcastProgress);
        throw new Error(cp.errorMessage);
    }
    cp.state = 'done';
    broadcast(broadcastProgress);
}

// ─── Controller post-reboot reconciliation ──────────────────────────────

async function reconcileControllerReboot(
    bridge: NovaSerialBridge,
    comp: OrbitComponent,
    expectedVersion: string,
    cp: ComponentProgress,
    broadcastProgress: ProgressBroadcastFn | null,
    totalTimeoutMs: number,
): Promise<void> {
    cp.state = 'rebooting';
    broadcast(broadcastProgress);
    console.log(`[fw-install] controller component finalized — awaiting reboot + reconnect ` +
                `(expectedVersion="${expectedVersion}")`);

    // Wait for the bridge's disconnected → connected edge. UART transports
    // never emit socket.close, but the heartbeat watchdog + the per-msgId
    // seq regression test in handleMessage() will fire 'disconnected'
    // shortly after the controller's UART falls silent, and 'connected'
    // again once it re-handshakes via DataLoadStatus(ready=true).
    const reconnectDeadline = Date.now() + Math.min(totalTimeoutMs, 120_000);
    const reconnected = await new Promise<boolean>((resolve) => {
        // Edge cases:
        //   - The controller's reboot is so fast the bridge never sees
        //     disconnected. We then look for a 'VersionInfo' edge below.
        //   - The bridge IS already disconnected when we get here (the
        //     finalize→activate path raced us). 'connected' fires on the
        //     re-handshake regardless.
        let timer: NodeJS.Timeout | null = null;
        const onConnected = (): void => {
            if (timer) clearTimeout(timer);
            bridge.off('connected', onConnected);
            resolve(true);
        };
        timer = setTimeout(() => {
            bridge.off('connected', onConnected);
            // Not necessarily a failure — if the bridge never lost
            // connection in the first place (warm-reset too fast for
            // heartbeat to notice) we'll still see VersionInfo below.
            resolve(false);
        }, reconnectDeadline - Date.now());
        bridge.on('connected', onConnected);
    });
    console.log(`[fw-install] controller reboot wait: reconnect=${reconnected}`);

    // Now wait for the first VersionInfo envelope (or the next one if
    // we missed the reconnect-time one). The bridge emits 'VersionInfo'
    // by message name (see MSG_NAMES in novaSerialBridge.ts), so listen
    // by that name.
    cp.state = 'verifying';
    broadcast(broadcastProgress);

    const versionDeadlineMs = 60_000;
    const seenVersion = await new Promise<string | null>((resolve) => {
        let timer: NodeJS.Timeout | null = null;
        const onVer = (data: Buffer): void => {
            const f = pbDecodeFields(data);
            const armVersion = pbGetString(f, 1);
            if (!armVersion) return;
            if (timer) clearTimeout(timer);
            bridge.off('VersionInfo', onVer);
            resolve(armVersion);
        };
        timer = setTimeout(() => {
            bridge.off('VersionInfo', onVer);
            resolve(null);
        }, versionDeadlineMs);
        bridge.on('VersionInfo', onVer);
    });

    if (!seenVersion) {
        cp.state = 'failed';
        cp.errorMessage = `post-reboot version probe failed (expected ${expectedVersion}, no VersionInfo within 60s)`;
        broadcast(broadcastProgress);
        throw new Error(cp.errorMessage);
    }
    // Match if the manifest version is a prefix of the reported version
    // (firmware embeds "<version>+<git-sha>[-dirty]"; the bundle ships
    // the "<version>" tag without the sha because the OTA pipeline
    // doesn't re-sign on a per-binary basis).
    const matches = seenVersion === expectedVersion
                 || seenVersion.startsWith(expectedVersion + '+')
                 || seenVersion.startsWith(expectedVersion + '-')
                 || expectedVersion.startsWith(seenVersion.split('+')[0]);
    if (!matches) {
        cp.state = 'failed';
        cp.errorMessage = `post-reboot version probe failed (expected ${expectedVersion}, got ${seenVersion})`;
        broadcast(broadcastProgress);
        throw new Error(cp.errorMessage);
    }
    console.log(`[fw-install] controller reboot verified: running=${seenVersion} matches manifest=${expectedVersion}`);
    cp.state = 'done';
    void comp; // role/slot are informational here
    broadcast(broadcastProgress);
}

// ─── Helpers ────────────────────────────────────────────────────────────

function sortComponentsControllerLast(
    bundle: LoadedBundle,
): Array<{ name: string; comp: import('./firmwareBundle.js').ComponentEntry }> {
    const entries = Object.entries(bundle.manifest.components).map(([name, comp]) => ({ name, comp }));
    return entries.sort((a, b) => {
        const aSlot = isOrbitComponent(a.comp) ? a.comp.slot : 99;
        const bSlot = isOrbitComponent(b.comp) ? b.comp.slot : 99;
        // Slot >= 0: orbit, sort by slot. Slot -1: controller, push last.
        const aKey = aSlot < 0 ? 1000 + aSlot : aSlot;
        const bKey = bSlot < 0 ? 1000 + bSlot : bSlot;
        return aKey - bKey;
    });
}

function bundleManifestSha256(bundle: LoadedBundle): Buffer {
    // The broker validates this only for audit logging; we hash the
    // manifest as-canonical-JSON-on-disk.
    const text = JSON.stringify(bundle.manifest);
    return crypto.createHash('sha256').update(Buffer.from(text, 'utf8')).digest();
}

function applyProgressToCp(cp: ComponentProgress, p: BrokerProgress): void {
    cp.bytesWritten = p.bytesWritten;
    cp.totalSize    = p.totalSize > 0 ? p.totalSize : cp.totalSize;
    switch (p.state) {
        case FwInstallComponentState.BEGIN:
        case FwInstallComponentState.ERASING:
        case FwInstallComponentState.PUSHING:
        case FwInstallComponentState.VERIFYING:
            cp.state = 'pushing';
            break;
        case FwInstallComponentState.ACTIVATING:
        case FwInstallComponentState.REBOOTING:
            cp.state = 'rebooting';
            break;
        case FwInstallComponentState.CONFIRMED:
        case FwInstallComponentState.DONE:
            cp.state = 'done';
            break;
        case FwInstallComponentState.SKIPPED:
            cp.state = 'skipped';
            break;
        case FwInstallComponentState.FAILED:
            cp.state = 'failed';
            cp.errorMessage = renderProgressError(p);
            break;
        default:
            // PENDING or unknown: leave cp.state alone.
            break;
    }
}

/** Tiny renderer fix-up (O2 from the previous bench round): when
 *  target_host is empty AND state=FAILED, substitute a role-keyed
 *  placeholder so the UI's failure line is diagnosable. */
function renderProgressError(p: BrokerProgress, comp?: OrbitComponent): string {
    const host = p.targetHost
              || (comp ? `(no LP with role=${comp.role})` : '(no target host)');
    const msg = p.errorMessage || `error_code=${p.errorCode}`;
    return `${host}: ${msg}`;
}

function reconcileResultEnvelope(
    result: BrokerResult,
    latestProgressByIndex: Map<number, BrokerProgress>,
    cpByIndex: Map<number, ComponentProgress>,
): void {
    // O1: source of truth is the progress-event map (we may have seen
    // a per-component CONFIRMED/DONE/FAILED already; the result envelope
    // may omit entries that failed pre-Begin or short-circuited).
    // Fall back to result.components[] only when the map lacks an entry.
    for (const rc of result.components) {
        const lp = latestProgressByIndex.get(rc.componentIndex);
        const state = lp?.state ?? rc.state;
        const errorMessage = lp?.errorMessage || rc.errorMessage;
        const cp = cpByIndex.get(rc.componentIndex);
        if (!cp) continue;
        if (state === FwInstallComponentState.FAILED && cp.state !== 'failed') {
            cp.state = 'failed';
            cp.errorMessage = errorMessage || `error_code=${rc.errorCode}`;
        } else if ((state === FwInstallComponentState.DONE
                   || state === FwInstallComponentState.CONFIRMED)
                   && cp.state !== 'failed' && cp.state !== 'done') {
            cp.state = 'done';
        }
    }
}

function logFleetSnapshot(raw: Buffer): void {
    try {
        const fields = pbDecodeFields(raw);
        const members = pbGetAllSubmsg(fields, 1).map((mbuf) => {
            const f = pbDecodeFields(mbuf);
            return {
                host:          pbGetString(f, 1),
                reachable:     pbGetVarint(f, 2) !== 0,
                currentRole:   pbGetVarint(f, 3),
                activeVersion: pbGetString(f, 4),
                bootCount:     pbGetVarint(f, 8),
            };
        });
        const summary = members.map(m =>
            `${m.host}=role${m.currentRole}/v${m.activeVersion || '?'}/boots=${fmtBootCount(m.bootCount)}` +
            `${m.reachable ? '' : '(unreachable)'}`
        ).join(', ');
        console.log(`[fw-install] FwFleetSnapshot: ${summary || '(empty)'}`);
    } catch (e) {
        console.warn(`[fw-install] FwFleetSnapshot decode failed: ${(e as Error).message}`);
    }
}

/** O3: render the OSPI-erased sentinel 0xFFFFFFFF (or 0 if unset) as a
 *  human-readable placeholder. Lifted out here so the same renderer is
 *  used everywhere boot_count surfaces. */
function fmtBootCount(n: number): string {
    if (n === 0xFFFFFFFF || n === undefined) return '?';
    return String(n);
}

function sendFwInstallComplete(bridge: NovaSerialBridge): void {
    bridge.sendFwInstallComplete();
}

async function waitFor(
    predicate: () => boolean,
    timeoutMs: number,
    pollMs: number,
): Promise<boolean> {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        if (predicate()) return true;
        await new Promise(r => setTimeout(r, pollMs));
    }
    return predicate();
}

function broadcast(broadcastProgress: ProgressBroadcastFn | null): void {
    if (!currentInstall || !broadcastProgress) return;
    try {
        broadcastProgress(currentInstall);
    } catch (e) {
        console.warn(`[fw-install] broadcast failed: ${(e as Error).message}`);
    }
}

// ─── Broker envelope decoding ───────────────────────────────────────────

/** Mirror of FwInstallComponentState (firmware.proto). */
const FwInstallComponentState = {
    PENDING:    0,
    BEGIN:      1,
    ERASING:    2,
    PUSHING:    3,
    VERIFYING:  4,
    ACTIVATING: 5,
    REBOOTING:  6,
    CONFIRMED:  7,
    DONE:       8,
    SKIPPED:    9,
    FAILED:    10,
} as const;

interface BrokerProgress {
    componentIndex: number;
    componentName: string;
    targetHost: string;
    state: number;
    bytesWritten: number;
    totalSize: number;
    errorCode: number;
    errorMessage: string;
    overall: number;
}

/** Phase 4.1 chunk-pump waiter. Registered by `waitForProgressAtLeast`
 *  and resolved by the shared `onMsg` listener inside installBundle()
 *  when a FwInstallProgress for the matching componentIndex arrives
 *  with bytesWritten >= target (or state=FAILED — any terminal
 *  unblocks the pump so the caller can surface the broker error). */
interface ProgressWaiter {
    target: number;
    resolve: (p: BrokerProgress) => void;
}

/** Block until the broker reports `bytesWritten >= target` for
 *  componentIndex, or `state=FAILED`. Times out with a rejected
 *  Promise; the caller wraps the rejection into an InstallError-style
 *  message and aborts the install.
 *
 *  Fast path: if `latestProgressByIndex` already has an entry that
 *  satisfies the target (e.g. broker batched multiple chunks into one
 *  progress event and the next pump iteration is already behind), we
 *  resolve synchronously without registering a waiter. */
function waitForProgressAtLeast(
    componentIndex: number,
    target: number,
    timeoutMs: number,
    latestProgressByIndex: Map<number, BrokerProgress>,
    progressWaiters: Map<number, ProgressWaiter[]>,
): Promise<BrokerProgress> {
    const existing = latestProgressByIndex.get(componentIndex);
    if (existing && (existing.state === FwInstallComponentState.FAILED
                  || existing.bytesWritten >= target)) {
        return Promise.resolve(existing);
    }
    return new Promise((resolve, reject) => {
        let settled = false;
        const waiter: ProgressWaiter = {
            target,
            resolve: (p) => {
                if (settled) return;
                settled = true;
                clearTimeout(timer);
                resolve(p);
            },
        };
        const list = progressWaiters.get(componentIndex);
        if (list) list.push(waiter);
        else progressWaiters.set(componentIndex, [waiter]);
        const timer = setTimeout(() => {
            if (settled) return;
            settled = true;
            // Unregister so we don't leak the waiter into a later
            // (post-timeout) progress event.
            const cur = progressWaiters.get(componentIndex);
            if (cur) {
                const filtered = cur.filter(w => w !== waiter);
                if (filtered.length > 0) progressWaiters.set(componentIndex, filtered);
                else progressWaiters.delete(componentIndex);
            }
            reject(new Error(`no FwInstallProgress with bytesWritten >= ${target} within ${timeoutMs} ms`));
        }, timeoutMs);
    });
}

interface BrokerResultComponent {
    componentIndex: number;
    componentName: string;
    targetHost: string;
    state: number;
    errorCode: number;
    errorMessage: string;
    postInstallVersion: string;
}

interface BrokerResult {
    overall: number;
    failureReason: string;
    components: BrokerResultComponent[];
}

function decodeProgress(data: Buffer): BrokerProgress {
    const f = pbDecodeFields(data);
    return {
        componentIndex: pbGetVarint(f, 1),
        componentName:  pbGetString(f, 2),
        targetHost:     pbGetString(f, 3),
        state:          pbGetVarint(f, 4),
        bytesWritten:   pbGetVarint(f, 5),
        totalSize:      pbGetVarint(f, 6),
        errorCode:      pbGetVarint(f, 7),
        errorMessage:   pbGetString(f, 8),
        overall:        pbGetVarint(f, 9),
    };
}

function decodeResult(data: Buffer): BrokerResult {
    const f = pbDecodeFields(data);
    const components = pbGetAllSubmsg(f, 3).map((cbuf): BrokerResultComponent => {
        const cf = pbDecodeFields(cbuf);
        return {
            componentIndex:     pbGetVarint(cf, 1),
            componentName:      pbGetString(cf, 2),
            targetHost:         pbGetString(cf, 3),
            state:              pbGetVarint(cf, 4),
            errorCode:          pbGetVarint(cf, 5),
            errorMessage:       pbGetString(cf, 6),
            postInstallVersion: pbGetString(cf, 7),
        };
    });
    return {
        overall:       pbGetVarint(f, 1),
        failureReason: pbGetString(f, 2),
        components,
    };
}

// ─── Wire-side helpers (hand-rolled varint + length-delimited encode) ───
// Mirror of the encVarint/encField pair in index.ts debug endpoints. We
// duplicate intentionally rather than exporting — keeps the broker wire
// format reviewable in one file (this file) once the debug endpoints are
// retired in Phase 5.

function encVarint(v: number): Buffer {
    const out: number[] = [];
    do {
        let b = v & 0x7F;
        v >>>= 7;
        if (v > 0) b |= 0x80;
        out.push(b);
    } while (v > 0);
    return Buffer.from(out);
}

function encField(field: number, wireType: number, payload: Buffer): Buffer {
    const tag = encVarint((field << 3) | wireType);
    if (wireType === 0) return Buffer.concat([tag, payload]);          // varint payload pre-encoded
    if (wireType === 2) return Buffer.concat([tag, encVarint(payload.length), payload]);
    throw new Error(`unsupported wireType ${wireType}`);
}
