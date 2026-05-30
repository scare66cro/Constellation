/*
 * lp_version.h — Constellation LP-AM2434 firmware version strings
 *
 * Single source of truth for the firmware identity surfaced to the
 * bridge / UI in the VersionInfo envelope (proto tag 22).
 *
 * ─── Alpha versioning scheme (May 2026 → production) ────────────────
 *
 * While the LP firmware is pre-release, every flash MUST carry a
 * version string of the form:
 *
 *     0.A.<N>+<git-sha>[-dirty]
 *
 *   - `0.A.<N>`  = "alpha build #N", manually bumped in
 *                  `docs/firmware-version-current.md` whenever a
 *                  meaningful change ships. The same N stays here as
 *                  the compile-time fallback.
 *   - `+<sha>`   = appended by `Flash-LP.ps1` from `git rev-parse
 *                  --short=8 HEAD`, so two builds from the same N but
 *                  different commits are still distinguishable.
 *   - `-dirty`   = appended when the working tree has uncommitted
 *                  changes. Do NOT ship a dirty image to a customer.
 *
 * The compile-time default below is only used when nobody passes
 * `-DLP_FW_VERSION=...` at build time (e.g. an IDE-driven build that
 * bypasses Flash-LP.ps1). The flash script always overrides it.
 *
 * After alpha → beta → 1.0 cutover, switch to plain `<major>.<minor>.
 * <patch>` and drop the `-dirty` enforcement.
 *
 * See:
 *   - docs/firmware-version-current.md  — current alpha N + changelog
 *   - docs/firmware-deployed.md         — append-only flash log
 *   - docs/register-and-ui-map.md       — HR ↔ proto ↔ UI map
 *
 * LP_BOOTLOADER_VERSION reflects the boot sequence that loaded this
 * image. On AM2434 LaunchPad we currently boot through the TI MCU+SDK
 * SBL (Secondary Bootloader). When a custom Constellation bootloader
 * is fabbed for production hardware, replace this with the runtime
 * value handed off from the SBL via shared SRAM (see
 * docs/LP-AM2434-Hardware-Bringup-Plan.md "Bootloader handoff").
 */

#ifndef LP_AM2434_LP_VERSION_H
#define LP_AM2434_LP_VERSION_H

/* If a build script (Flash-LP.ps1) has dropped a generated header next
 * to this one, prefer it. The generated header carries the full
 * `0.A.<n>+<git-sha>[-dirty]` string with proper C-string quoting,
 * which is hard to pass reliably through nested PowerShell/cmd/gmake
 * shells via `-DLP_FW_VERSION=...`. The generated file is git-ignored
 * and overwritten on every flash. */
#if __has_include("lp_version_injected.h")
#  include "lp_version_injected.h"
#endif

#ifndef LP_FW_VERSION
#define LP_FW_VERSION         "0.A.1+local"
#endif

#ifndef LP_BOOTLOADER_VERSION
#define LP_BOOTLOADER_VERSION "ti-sbl-9.02"
#endif

/* ─── ServiceInfo (proto tag 23) ─────────────────────────────────────────
 * Dealer / installer contact info shown on the Level-1 Service page.
 * Per-installation values; today they are compile-time placeholders.
 * Once OSPI persistence lands (Phase D of the envelope-emission plan),
 * these move into the persistent Settings struct and become editable
 * via the Service page save path.
 *
 * UI hides each line when its string is blank, so empty defaults are
 * acceptable. Keep ≤24 chars for the UI layout.
 */
#ifndef LP_SVC_DEALER_NAME
#define LP_SVC_DEALER_NAME   "Agristar"
#endif
#ifndef LP_SVC_DEALER_PHONE
#define LP_SVC_DEALER_PHONE  ""
#endif
#ifndef LP_SVC_TECH_NAME
#define LP_SVC_TECH_NAME     ""
#endif
#ifndef LP_SVC_TECH_PHONE
#define LP_SVC_TECH_PHONE    ""
#endif

#endif /* LP_AM2434_LP_VERSION_H */
