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

**Status:** ☑→◐ **LP-side DONE (2026-05-20, 0.A.188);** bridge-side ELF parser still ☐ but no longer urgent — the LP now both reports its role in `FwBankInfo.current_role` (bridge cross-check) and rejects role-mismatched `FwBeginUpdate` in the BEGIN handler. Two layers of defense without the ELF parser.

---

#### Gap 2 — version downgrade rejection

**Failure mode:** Operator (or buggy CFU bundle picker) ships 0.A.1 over a board running 0.A.187. Cleanly downgrades the chip with no warning. May or may not be the right move; should be a conscious decision.

**Fix locations:**
- LP-side: `lp_ota_task.c` BEGIN handler compares `version` string to running `LP_FW_VERSION`. If newer or equal, accept; else reject unless override bit is set in `FwBeginUpdate`.
- Bridge-side: read the LP's current version from `FwBankInfo` on connect, compare to bundle version, refuse unless `opts.allowDowngrade` is set.

**Status:** ☑ **DONE** firmware-side (2026-05-20, 0.A.188) — BEGIN handler parses `0.A.<N>` from `b.version`, compares to `LP_FW_VERSION`, rejects with `LP_OTA_ERR_DOWNGRADE` (28) unless the bridge sets `allow_downgrade=true`. Bridge forwards `!rejectDowngrade` automatically. ☑ **DONE** bridge-side (in Gap 3 patch, 6042c59).

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
| **G1-bridge** image role/IP introspection | (needs ELF parser) | ☐ deprioritized — G1-LP + `FwBankInfo.current_role` cover the failure mode |
| **G1-LP** firmware role-vs-config cross-check | `lp_ota_task.c`, `firmware.proto` | ☑ **DONE 2026-05-20 (0.A.188)** — see below |
| **G2-LP** firmware version downgrade rejection | `lp_ota_task.c` | ☑ **DONE 2026-05-20 (0.A.188)** — see below |

## Patch progress 2026-05-20 (session 3) — role-based OTA routing

Until this session the bridge installer routed `.cfu` components by an
env-var slot→IP map (`resolveOrbitHost(slot)` in `orbitMbtcp.ts`). That
worked on the bench rig where slot↔IP is conventional, but couldn't
catch a mis-cabled rack or a board whose `lp_device_config.role` was
provisioned wrong.

Now that LP fw ≥ 0.A.188 self-reports `FwBankInfo.current_role`, the
installer can probe a configured fleet and route by role.

### Resolution order (firmwareInstaller.ts:pushOneComponent)

1. **Controller** (`comp.slot === -1`): use `comp.ip` from the manifest.
2. **Fleet snapshot present** (`ORBIT_FLEET` env or `opts.fleet`):
   `resolveByRoleFromSnapshot(snap, comp.role)` looks up the IP whose
   reported `current_role` matches the manifest. Hard errors on
   ambiguity (>1 LP claiming the role) or no match.
3. **Fallback**: legacy `resolveOrbitHost(comp.slot)` slot→IP map.
   The downstream `pushImage()` pre-Begin cross-check still validates
   `comp.role` vs the LP's actual `currentRole`, so misrouting here
   aborts cleanly before the Bank-B erase.

### Files

| Patch | Files | Status |
|---|---|---|
| New `orbitFleetResolver.ts` — parallel-probe fleet, build `role → host` map | `constellation-ui/server/src/orbitFleetResolver.ts` | ☑ |
| `firmwareInstaller.ts` takes a snapshot at install start, threads to `pushOneComponent`, prefers role-based routing | `firmwareInstaller.ts` (`InstallOptions.fleet`, `installBundle`, `pushOneComponent`) | ☑ |

### Operator UX

Set `ORBIT_FLEET=10.47.27.2,10.47.27.3,10.47.27.4` in the bridge env
(systemd unit / `.env` / `Start-Constellation.ps1`) and role-based
routing kicks in automatically. The bridge logs which routing decision
was used per component:

```
[fw-install] fleet snapshot: members=[10.47.27.2=role1, 10.47.27.3=role2, 10.47.27.4=role3] legacy-fw=[] unreachable=[]
[fw-install] "controller" routed to controller IP 10.47.27.1 (slot=-1)
[fw-install] "storage" routed by role: role=1 → 10.47.27.2 (matches legacy slot map)
[fw-install] "gdc" routed by role: role=2 → 10.47.27.3 (matches legacy slot map)
[fw-install] "triton" routed by role: role=3 → 10.47.27.4 (matches legacy slot map)
```

If a cable is swapped between snapshot and the actual push, you'd see:
```
[fw-install] "gdc" routed by role: role=2 → 10.47.27.4 (legacy slot=1 → 10.47.27.3 suppressed)
```

If two boards claim the same role (mis-provisioned), the install fails
with `FleetResolverError('ambiguous', ...)` before any push, with both
hosts in the message so the operator can fix one via `/iot/lp/provision`.

## Patch progress 2026-05-20 (session 2)

| Patch | Files | Status |
|---|---|---|
| **proto** add `FwBeginUpdate.expected_role` (5) + `allow_downgrade` (6); add `FwBankInfo.current_role` (11) | `proto/agristar/firmware.proto`, `generated/c/agristar/firmware.pb.*`, `generated/ts/agristar/firmware.ts` | ☑ regen'd via `proto/generate.ps1` (one-time tooling install: `pip install nanopb grpcio-tools` into `.venv`; `protoc` from `torch/bin` on PATH). Helper script `proto/_regen.sh` left in place for next session. |
| **G2-LP** version-N parse + downgrade gate in BEGIN handler | `Nova_Firmware/lp_am2434/lp_ota_task.c` (`parse_alpha_n`, BEGIN handler), `lp_ota_task.h` (`LP_OTA_ERR_DOWNGRADE=28`) | ☑ fires before `s_image_active=true` and before the ~5 s Bank-B erase. Equal-N accepted (re-flash same N with new sha). Non-alpha version strings → gate skipped (back-compat). |
| **G1-LP** role-mismatch gate in BEGIN handler + `current_role` emit in BankInfo | `lp_ota_task.c` (`decode_begin`, BEGIN handler, `encode_bank_info`), `lp_ota_task.h` (`LP_OTA_ERR_ROLE_MISMATCH=27`) | ☑ `expected_role` is presence-tracked (proto3 zero suppression workaround: LP decoder sets `has_expected_role=true` when field 5 is on the wire, so 0=CONTROLLER is distinguishable from "absent"). `current_role` in BankInfo is force-encoded (`pb_uint32_force`) for the same reason. Older bridges that don't send field 5 → gate skipped (back-compat). |
| **G1-bridge cross-check** pre-Begin role assert against `preBankInfo.currentRole` | `constellation-ui/server/src/orbitOtaPush.ts` (`pushImage` Gap-3 BankInfo block), `orbitOtaClient.ts` (`FwBankInfo.currentRole?` + decode tag 11) | ☑ throws `OtaPushError('roleMismatch', …)` BEFORE Begin so we save a TCP roundtrip + Bank-B erase on the obvious-wrong-target case. Soft gate: skipped when LP firmware too old to emit `current_role` (legacy compat). |
| **bridge** thread `expectedRole` + `allow_downgrade` into `FwBeginUpdate` | `orbitOtaPush.ts` (`encodeBegin`, `pbUint32Force`, `PushImageOptions.expectedRole`), `firmwareInstaller.ts` (passes `comp.role` from manifest) | ☑ `allow_downgrade` is the inverse of `rejectDowngrade` so the two sides agree. `expectedRole` is force-encoded only when the caller sets it (test harnesses / legacy bundles can omit it to skip the LP-side gate). |
| **bridge** map LP error codes 27/28 to distinct `OtaPushError.kind` values | `orbitOtaPush.ts` (`checkStatus`) | ☑ `roleMismatch` (27) + `downgradeBlocked` (28) bubble up cleanly to the installer / UI. |

### Bench-verification status

- ☐ G3-bridge / G2-bridge / G4 / **G1-LP / G2-LP / G1-bridge cross-check** — TS compiles clean on touched files (`tsc --noEmit`: the 4 unrelated `apiRoutes.ts` duplicate-identifier errors are pre-existing and out of scope). LP C compile + bench-flash pending. Test recipes below.

## Test recipes

After each patch lands, verify on bench against the four-board topology
(Nova .1, STORAGE .2, GDC/Pulsar .3, TRITON .4):

### G3-bridge / G2-bridge test
```powershell
# Should succeed and log FwBankInfo from each LP
node F:\Constellation\constellation-ui\server\src\test_ota_push.ts \
    --host 10.47.27.2 --image <gdc-build>.hs_fs --dry-run
# After 0.A.188 lands the LP, the FwBankInfo line should also include
# the new currentRole, e.g. "currentRole=1 (STORAGE)". Run against all
# four boards (.1 .2 .3 .4) and verify each reports the expected role.
```

### G4 test
```powershell
# 1. Build a .cfu bundle
# 2. After loadBundle() returns, manually corrupt one component file
#    on disk (echo 'oops' >> ...hs_fs)
# 3. Invoke installer → should throw "sha256 mismatch" before pushing
```

### G1-LP / G1-bridge cross-check test (2026-05-20)
```powershell
# Wrong-role push: a GDC-built image targeted at the STORAGE LP.
# Three expected lines of defense, in order of which fires:
#   1. Bridge pre-Begin cross-check (currentRole vs expectedRole)
#      → OtaPushError('roleMismatch', '...10.47.27.2 reports currentRole=1
#        but bundle expects role=2. Aborting before Begin...') 
#      → installs nothing, saves the Bank-B erase cycle.
#   2. If #1 is bypassed (test harness passes expectedRole=undefined
#      and somehow gets past #3), the LP rejects in BEGIN handler:
#      → FwUpdateStatus { state=FW_ERROR, error_code=27, message=
#        "bundle role does not match LP role" }
#      → bridge surfaces as OtaPushError('roleMismatch', '...code=27...').
#   3. Both bypassed (older LP firmware that doesn't emit current_role
#      AND doesn't accept expected_role) → legacy behavior, no gate.

# Bench:
# Build a real .cfu bundle (manifest declares components.gdc.role=2 +
# components.storage.role=1). Install with the slot/IP swapped (point
# the gdc component at the storage IP via override). Verify gate #1 fires.
```

### G2-LP downgrade test (2026-05-20)
```powershell
# 1. Push 0.A.190 (or any N > running) → succeeds normally.
# 2. Push 0.A.50 with rejectDowngrade=true (the default) →
#    bridge throws OtaPushError('downgradeBlocked', ...) BEFORE Begin
#    (bridge-side gate from session 1, commit 6042c59).
# 3. Push 0.A.50 with rejectDowngrade=false →
#    bridge passes allow_downgrade=true to LP, LP accepts the downgrade,
#    OTA completes normally.
# 4. To exercise the LP-side gate in isolation: build a test harness
#    that hand-encodes a FwBeginUpdate with allow_downgrade omitted but
#    a deliberately-low version string ("0.A.1+test"). LP should reject
#    with error_code=28 + "downgrade rejected" message.
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
