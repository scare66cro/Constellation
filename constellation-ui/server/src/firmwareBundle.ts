/**
 * firmwareBundle.ts — Constellation Firmware Update (.cfu) bundle handling.
 *
 * A `.cfu` is a zip archive (later: AES-encrypted) containing:
 *   manifest.json
 *   <component>.mcelf.hs_fs    (per-orbit firmware images)
 *   agristar-bridge.tar.gz     (optional, Pi5 bridge code — Phase 2)
 *   constellation-ui.tar.gz    (optional, Pi5 UI build — Phase 2)
 *
 * manifest.json schema (v1):
 *   {
 *     "schema":      "constellation-firmware/v1",
 *     "version":     "0.A.190",
 *     "build_date":  "2026-05-19T15:00:00Z",
 *     "min_bootloader": "0.A.1",        // optional, refuse install if LP older
 *     "components": {
 *       "controller": {
 *         "file":  "controller.mcelf.hs_fs",
 *         "slot":  -1,                  // -1 = controller, 0..7 = orbit slot
 *         "role":  0,                   // 0=CTL, 1=STORAGE, 2=GDC, 3=TRITON
 *         "ip":    "10.47.27.1",
 *         "sha256": "<hex>"
 *       },
 *       "storage":    { ... },
 *       ...
 *       "pi5_bridge": { "file": "agristar-bridge.tar.gz", "sha256": "<hex>" },
 *       "pi5_ui":     { "file": "constellation-ui.tar.gz", "sha256": "<hex>" }
 *     }
 *   }
 *
 * For Phase 1 (this session) only orbit-firmware components are honoured.
 * `pi5_bridge` and `pi5_ui` entries are accepted in the manifest but skipped
 * at install time — see firmwareInstaller.ts.
 */

import * as fs from 'fs/promises';
import * as path from 'path';
import * as crypto from 'crypto';
import AdmZip from 'adm-zip';

// ─── Schema ──────────────────────────────────────────────────────────────

export const CFU_EXTENSION = '.cfu';
export const MANIFEST_FILENAME = 'manifest.json';
export const MANIFEST_SCHEMA_V1 = 'constellation-firmware/v1';

/**
 * Password for the `.cfu` archive.  Currently empty (plain zip) for
 * Phase-1 bench work.  Wire to a Pi5-side secret in Phase 2.  Note: zip
 * "password" (ZipCrypto) is weak; treat as obfuscation, not security.
 * For real security, swap to AES-encrypted zip via a different library
 * (e.g. `node-stream-zip` + manual AES) or pre-decrypt the bundle.
 */
export const BUNDLE_PASSWORD = process.env.CFU_PASSWORD ?? '';

/** Per-component manifest entry — orbit firmware. */
export interface OrbitComponent {
    file: string;       // filename inside the zip
    slot: number;       // -1 = controller, 0..7 = orbit slot
    role: number;       // 0=CTL, 1=STORAGE, 2=GDC, 3=TRITON
    ip: string;         // target IP (informational; bridge uses resolveOrbitHost(slot) at push time)
    sha256: string;     // lowercase hex
}

/** Per-component manifest entry — Pi5-side (Phase 2). */
export interface Pi5Component {
    file: string;
    sha256: string;
}

export type ComponentEntry = OrbitComponent | Pi5Component;

export interface Manifest {
    schema: string;
    version: string;
    build_date: string;
    min_bootloader?: string;
    components: Record<string, ComponentEntry>;
}

export function isOrbitComponent(c: ComponentEntry): c is OrbitComponent {
    return 'slot' in c && 'role' in c && 'ip' in c;
}

// ─── Bundle loader ──────────────────────────────────────────────────────

export interface LoadedBundle {
    /** Absolute path to a temp directory holding the extracted contents. */
    extractDir: string;
    /** Parsed + validated manifest. */
    manifest: Manifest;
    /** Original .cfu source path (for logging only). */
    sourcePath: string;
}

export class BundleError extends Error {
    constructor(public code: string, message: string) {
        super(message);
        this.name = 'BundleError';
    }
}

/**
 * Open a `.cfu`, extract its contents to a fresh temp directory, parse
 * + validate the manifest, and verify each component file's sha256.
 *
 * Throws `BundleError` with a code on any validation failure.  The
 * caller is responsible for eventually cleaning up `extractDir` via
 * `cleanupBundle()`.
 */
export async function loadBundle(cfuPath: string): Promise<LoadedBundle> {
    // 1. File-extension gate (matches the AS2 .rpi pattern — explicit
    //    refusal of anything else).
    if (path.extname(cfuPath).toLowerCase() !== CFU_EXTENSION) {
        throw new BundleError(
            'bad_extension',
            `Bundle must have ${CFU_EXTENSION} extension; got ${path.basename(cfuPath)}`,
        );
    }

    // 2. Load zip.  adm-zip is sync, so blocking call — but bundles are
    //    small enough (< 50 MB typical) that this is acceptable.
    let zip: AdmZip;
    try {
        zip = new AdmZip(cfuPath);
    } catch (e: any) {
        throw new BundleError('zip_open', `Failed to open ${cfuPath}: ${e?.message ?? e}`);
    }

    // 3. Extract to fresh temp dir.  Use a content-addressed name so
    //    concurrent installs don't collide.
    const sha = crypto.createHash('sha256');
    sha.update(cfuPath);
    sha.update(String(Date.now()));
    const tag = sha.digest('hex').slice(0, 12);
    const extractDir = path.join(
        process.env.TMPDIR ?? process.env.TMP ?? '/tmp',
        `constellation-cfu-${tag}`,
    );
    await fs.mkdir(extractDir, { recursive: true });

    try {
        zip.extractAllTo(extractDir, /* overwrite */ true, /* keepOriginalPermission */ false, BUNDLE_PASSWORD || undefined);
    } catch (e: any) {
        await cleanupBundle({ extractDir, manifest: {} as Manifest, sourcePath: cfuPath });
        throw new BundleError('zip_extract', `Failed to extract: ${e?.message ?? e}`);
    }

    // 4. Locate + parse manifest.
    const manifestPath = path.join(extractDir, MANIFEST_FILENAME);
    let manifestText: string;
    try {
        manifestText = await fs.readFile(manifestPath, 'utf8');
    } catch {
        await cleanupBundle({ extractDir, manifest: {} as Manifest, sourcePath: cfuPath });
        throw new BundleError('manifest_missing', `${MANIFEST_FILENAME} not found in bundle`);
    }

    // Strip UTF-8 BOM if present. PowerShell 5.1's `Set-Content -Encoding utf8`
    // emits a BOM that Node's JSON.parse rejects ("Unexpected token '﻿'").
    if (manifestText.charCodeAt(0) === 0xFEFF) {
        manifestText = manifestText.slice(1);
    }

    let manifest: Manifest;
    try {
        manifest = JSON.parse(manifestText);
    } catch (e: any) {
        await cleanupBundle({ extractDir, manifest: {} as Manifest, sourcePath: cfuPath });
        throw new BundleError('manifest_parse', `${MANIFEST_FILENAME} is not valid JSON: ${e?.message ?? e}`);
    }

    // 5. Validate manifest schema + required fields.
    if (manifest.schema !== MANIFEST_SCHEMA_V1) {
        await cleanupBundle({ extractDir, manifest, sourcePath: cfuPath });
        throw new BundleError(
            'manifest_schema',
            `Unsupported manifest schema "${manifest.schema}"; expected "${MANIFEST_SCHEMA_V1}"`,
        );
    }
    if (!manifest.version || !manifest.build_date) {
        await cleanupBundle({ extractDir, manifest, sourcePath: cfuPath });
        throw new BundleError('manifest_fields', 'manifest.version and manifest.build_date are required');
    }
    if (!manifest.components || Object.keys(manifest.components).length === 0) {
        await cleanupBundle({ extractDir, manifest, sourcePath: cfuPath });
        throw new BundleError('manifest_fields', 'manifest.components is empty');
    }

    // 6. Verify each component file exists + matches sha256.
    for (const [name, comp] of Object.entries(manifest.components)) {
        if (!comp.file) {
            throw new BundleError(
                'component_invalid',
                `Component "${name}" is missing required 'file' field`,
            );
        }
        const filePath = path.join(extractDir, comp.file);
        let bytes: Buffer;
        try {
            bytes = await fs.readFile(filePath);
        } catch {
            await cleanupBundle({ extractDir, manifest, sourcePath: cfuPath });
            throw new BundleError(
                'component_missing',
                `Component "${name}": file "${comp.file}" not found in bundle`,
            );
        }
        if (comp.sha256) {
            const actual = crypto.createHash('sha256').update(bytes).digest('hex').toLowerCase();
            const expected = comp.sha256.toLowerCase();
            if (actual !== expected) {
                await cleanupBundle({ extractDir, manifest, sourcePath: cfuPath });
                throw new BundleError(
                    'component_sha_mismatch',
                    `Component "${name}": sha256 mismatch (expected ${expected}, got ${actual})`,
                );
            }
        }
    }

    return { extractDir, manifest, sourcePath: cfuPath };
}

/** Resolve the absolute path of a component's file inside the bundle. */
export function componentPath(bundle: LoadedBundle, componentName: string): string {
    const comp = bundle.manifest.components[componentName];
    if (!comp) {
        throw new BundleError('component_unknown', `No component "${componentName}" in bundle`);
    }
    return path.join(bundle.extractDir, comp.file);
}

/** List orbit-firmware components in manifest-iteration order. */
export function orbitComponents(bundle: LoadedBundle): Array<{ name: string; comp: OrbitComponent }> {
    const out: Array<{ name: string; comp: OrbitComponent }> = [];
    for (const [name, comp] of Object.entries(bundle.manifest.components)) {
        if (isOrbitComponent(comp)) {
            out.push({ name, comp });
        }
    }
    return out;
}

/** Best-effort cleanup of an extracted bundle's temp directory. */
export async function cleanupBundle(bundle: LoadedBundle): Promise<void> {
    if (!bundle.extractDir) return;
    try {
        await fs.rm(bundle.extractDir, { recursive: true, force: true });
    } catch (e) {
        console.warn(`[cfu] cleanup warning: ${(e as Error).message}`);
    }
}
