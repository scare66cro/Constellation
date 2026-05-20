# LP-AM2434 OTA hardening — progress tracker

> Living doc for the OTA defensive-checks work kicked off 2026-05-20
> after a multi-hour session where:
> 1. The wrong-probe trap on JTAG (`Flash-LP.ps1`) reflashed Nova as
>    TRITON role, causing a `.4` IP conflict and ~30 min of confused
>    debug
> 2. A swallowed gmake error silently flashed a stale binary,
>    compounding (1)
>
> Flash-LP.ps1 got belt-and-suspenders hardening in commit
> [43b0e16](../) ("LP-AM2434: harden Flash-LP, add Probe P, recover
> S24L0707 chip"). This doc tracks the equivalent work for the **OTA
> path** so we can leave boards in OSPI from now on with confidence.

## Goals

After this work is complete:
- The bridge cannot push a firmware image to the wrong board
- The bridge cannot push an image whose bytes don't match the manifest
- The LP rejects images flashed for the wrong role
- The LP rejects downgrades unless explicitly allowed
- Every OTA push is logged with target IP + role + version + bank info
  (audit trail)

## Current state of the OTA pipeline (as of 2026-05-20)

End-to-end OTA validated on bench since 2026-05-15 — full pipeline
runs from a CLI test harness through to a successfully booted-into-new-
firmware LP. Cycle time ~2 min per OTA. Per
[docs/LP-AM2434-OTA-Update-Plan.md](LP-AM2434-OTA-Update-Plan.md), the
state machine is `Begin → Chunk* → Finalize → Activate`, transported
over TCP `:5503` with length-prefixed framing.

### Defenses already in place ✓

| Defense | Location | Status |
|---|---|---|
| `.cfu` bundle sha256 verified per-component on `loadBundle()` | [`firmwareBundle.ts:132`](../constellation-ui/server/src/firmwareBundle.ts) | ✓ |
| Per-chunk CRC32 on the wire (Ethernet/zlib polynomial) | [`orbitOtaPush.ts:68-74`](../constellation-ui/server/src/orbitOtaPush.ts), `lp_ota_task.c` | ✓ |
| Full Bank-B CRC32 verified after streaming, before Activate | LP side | ✓ |
| Bank-A CRC re-verified before stage-copy commits to Bank A | LP side | ✓ — prevents bricking |
| HS-FS image x509 signature checked by SBL at next boot | TI ROM/SBL | ✓ — bad image won't boot |
| Concurrent-install lock (one OTA at a time) | [`firmwareInstaller.ts:isInstalling()`](../constellation-ui/server/src/firmwareInstaller.ts) | ✓ |
| Slot → IP resolution via canonical bridge map | [`firmwareInstaller.ts:195`](../constellation-ui/server/src/firmwareInstaller.ts) via `resolveOrbitHost` | ✓ |

### Gaps and status

#### Gap 1 — image-baked-role vs. manifest-claimed-role cross-check

**Failure mode:** `.cfu` manifest says `components.gdc.role=2, file=gdc.hs_fs`, but the bundle actually contains a TRITON-baked binary in `gdc.hs_fs` (bundle build error, manual mis-naming, or supply-chain attack). Bridge pushes it; LP accepts; LP boots as TRITON at the wrong IP.

**Detection cost on bench:** ~30 min, plus a forced reflash to recover. Same blast radius as the wrong-probe trap on JTAG.

**Fix locations:**
- Bridge-side (preferred): parse the `nova_lp.release.mcelf.hs_fs` binary, extract embedded `CONFIG_NOVA_LP_ROLE` / `CONFIG_NOVA_LP_IP` values, assert they match the manifest entry. ELF parsing — non-trivial but doable.
- LP-side: in `FwBeginUpdate` handler, require the incoming `version` string to encode the role, and assert it matches the chip's stored `lp_device_config` role. Requires proto + firmware change.

**Status:** ☐ **OPEN** — defer to next session. Both bridge-side and LP-side fixes need a focused work block.

---

#### Gap 2 — version downgrade rejection

**Failure mode:** Operator (or buggy CFU bundle picker) ships 0.A.1 over a board running 0.A.187. Cleanly downgrades the chip with no warning. May or may not be the right move; should be a conscious decision.

**Fix locations:**
- LP-side: `lp_ota_task.c` BEGIN handler compares `version` string to running `LP_FW_VERSION`. If newer or equal, accept; else reject unless override bit is set in `FwBeginUpdate`.
- Bridge-side: read the LP's current version from `FwBankInfo` on connect, compare to bundle version, refuse unless `opts.allowDowngrade` is set.

**Status:** ☐ **OPEN** firmware-side, ◐ **PARTIAL** bridge-side (added as part of Gap 3 patch — see below).

---

#### Gap 3 — LP identity verification before send (bridge-side)

**Failure mode:** Bridge connects to host `10.47.27.3` expecting GDC, but the actual board at `.3` is in some unexpected state (mis-flashed earlier, version mismatch, no firmware running, etc.). Bridge starts streaming bytes anyway.

**Fix location:** `orbitOtaPush.ts:pushImage()` — the LP auto-pushes `FwBankInfo` on TCP connect (`orbitOtaPush.ts:274` currently ignores it). Bridge should capture it, assert it's well-formed (LP is responsive + sane), surface it back to the caller, and use it for downgrade comparison (Gap 2 bridge-side).

**Limitation today:** `FwBankInfo` proto has no `role` field (just bank versions, CRCs, valid flags). So the full "LP-claims-role == manifest-expects-role" check isn't possible from the bridge until Gap 1 lands a `current_role` field. What we CAN do today: confirm LP is alive, log + return bank state, do version-based downgrade rejection.

**Status:** ☐→◐ — implementing the bridge-side capture + downgrade rejection now (2026-05-20).

---

#### Gap 4 — re-hash image file before pushing

**Failure mode:** Bundle gets validated on `loadBundle()`, then the file gets swapped/corrupted between load and push (concurrent FS operation, half-written cache file, ext eviction of disk page, …). Bridge happily pushes the swapped bytes — they'll fail the wire-level CRC, but the wasted time + risk of mid-flash interruption is real.

**Fix location:** `firmwareInstaller.ts:pushOneComponent()` — re-compute sha256 of `componentPath(bundle, name)` immediately before `pushImage()`, compare to manifest's `sha256` field, throw if mismatch.

**Status:** ☐→◐ — implementing now (2026-05-20).

---

#### Gap 5 — Pi5 bridge + UI updates are stubbed

**Failure mode:** No actual fix needed — these components just don't have OTA wired yet (Phase 2 of the OTA plan). Listed for completeness.

**Status:** ⏸ **DEFERRED** — Phase 2 work, separate session.

---

#### Gap 6 — no SBL A/B chooser

**Failure mode:** Power-fail during the ~60-s window between `ACTIVATE reboot=1` (Bank-B copy starts) and the new firmware's first heartbeat (Bank A is now valid) leaves Bank A partially overwritten. SBL boots into a broken image. **This is the only path that can hard-brick a chip during OTA today.** Documented in the OTA plan as Phase 3.

**Fix location:** Custom F2c SBL chooser per [docs/lp-am2434-f2c-sbl-chooser-design.md](lp-am2434-f2c-sbl-chooser-design.md). Boots whichever bank has the higher generation counter + valid signature, watchdog-driven rollback if the new image fails.

**Status:** ⏸ **DEFERRED** — Phase 3 / F2c work, separate session. Mitigation today: "don't power-cycle during activate" (write into operator docs).

## Patch progress this session (2026-05-20)

| Patch | Files | Status |
|---|---|---|
| **G3-bridge** capture + return FwBankInfo on connect | `orbitOtaPush.ts` | ☑ **DONE** — `preBankInfo` captured before Begin, surfaced in `PushImageResult`, optional `onBankInfo` callback for installer audit. Soft 3 s timeout falls through to legacy-fw path. |
| **G2-bridge** version downgrade rejection (bridge-side) | `orbitOtaPush.ts`, `firmwareInstaller.ts` | ☑ **DONE** — `parseAlphaN()` extracts integer from `0.A.<n>+sha`; if running version's N > incoming version's N, push throws `OtaPushError('downgradeBlocked', …)`. Default `rejectDowngrade: true`; override via option. Installer now passes `rejectDowngrade: true` explicitly. |
| **G4** re-hash before push | `firmwareInstaller.ts` | ☑ **DONE** — `pushOneComponent()` re-reads the file + computes sha256 + compares to manifest's `comp.sha256` immediately before `pushImage()`. Throws "image sha256 mismatch immediately before push (file modified after bundle load?)" on mismatch. |
| **G1-bridge** image role/IP introspection | (needs ELF parser) | ☐ next session |
| **G1-LP** firmware role-vs-config cross-check | `lp_ota_task.c`, `firmware.proto` | ☐ next session |
| **G2-LP** firmware version downgrade rejection | `lp_ota_task.c` | ☐ next session |

### Bench-verification status

- ☐ G3-bridge / G2-bridge / G4 — TS compiles clean (`tsc --noEmit` no errors on the two patched files); pending bench verification (push a real `.cfu` against the 4-board topology and confirm the new log lines + downgrade rejection appear).

## Test recipes

After each patch lands, verify on bench against the four-board topology
(Nova .1, STORAGE .2, GDC/Pulsar .3, TRITON .4):

### G3-bridge / G2-bridge test
```powershell
# Should succeed and log FwBankInfo from each LP
node F:\Constellation\constellation-ui\server\src\test_ota_push.ts \
    --host 10.47.27.2 --image <gdc-build>.hs_fs --dry-run
# Should REFUSE because we're pointing GDC build at STORAGE IP — once
# Gap 1 lands. Today: dry-run succeeds but FwBankInfo log will show
# STORAGE's identity, prompting operator to abort.
```

### G4 test
```powershell
# 1. Build a .cfu bundle
# 2. After loadBundle() returns, manually corrupt one component file
#    on disk (echo 'oops' >> ...hs_fs)
# 3. Invoke installer → should throw "sha256 mismatch" before pushing
```

## Cross-references

- Original OTA design: [docs/LP-AM2434-OTA-Update-Plan.md](LP-AM2434-OTA-Update-Plan.md)
- CFU bundle architecture: `memories/repo/cfu-firmware-bundle-design.md`
- SBL chooser design (Gap 6): [docs/lp-am2434-f2c-sbl-chooser-design.md](lp-am2434-f2c-sbl-chooser-design.md)
- Sister hardening that just landed: commit `43b0e16` (Flash-LP.ps1)

## When you re-open this doc

- Check off ☐→☑ on each patch as it ships + lands a commit
- Add bench-verification notes in the row when tested
- Promote any gaps that turn out to be more urgent
- New gaps go in the table above with the same template
