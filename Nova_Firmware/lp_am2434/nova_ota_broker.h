/*
 * nova_ota_broker.h — Pi5 ↔ Nova install-orchestration broker (Phase 3
 *                      scaffold, UART-airgap OTA migration).
 *
 * The Nova-side counterpart to the Pi5's `firmwareInstaller.ts`. Phase 3
 * scope (this file) is the dispatchable skeleton: state machine,
 * envelope decoders for fields 130-136, progress/result emitters via
 * `NovaProto_SendRaw`, and an `IsInInstallMode()` gate so the rest of
 * `main.c` can suspend non-essential broadcasts during an install.
 *
 * The leaf operations — opening TCP to a fleet LP and driving its
 * `lp_ota_task.h` state machine (tags 0x10/0x11/0x12/0x13), or calling
 * `NovaFwUpdate_Begin/WriteChunk/Finalize/Activate` for the self-update
 * path — are STUBBED in Phase 3 and surface as
 * `FwInstallProgress(state=FAILED, error_code=99)`. Phase 3.5 / 3.6
 * sessions wire the real bytes through.
 *
 * Memory model: chunks flow Pi5 → UART → Nova → TCP/local-OSPI with at
 * most one chunk in flight. No bundle-wide buffering. See CLAUDE.md
 * invariant #12 (UART airgap) and docs/uart-airgap-architecture.md.
 *
 * Wire reference: proto/agristar/firmware.proto §"Pi5 ↔ Nova install-
 * orchestration layer" (FwInstall* messages) and
 * proto/agristar/envelope.proto slots 130-149.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */
#ifndef NOVA_OTA_BROKER_H
#define NOVA_OTA_BROKER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Public API ─────────────────────────────────────────────────────── */

/* One-shot init. Resets state to IDLE and clears the current-component
 * struct. Safe to call before the scheduler starts; does not touch any
 * lwIP / OSPI state yet. */
void NovaOtaBroker_Init(void);

/* True while a bundle install is in progress (any state other than
 * IDLE). Called by `main.c` per-tick broadcasters (SystemStatus,
 * EquipmentStatus, SensorData, OrbitStatus, OrbitSensorBank) to skip
 * their per-tick work and free the UART back-direction for progress
 * envelopes. Heartbeat is intentionally NOT gated on this — the bridge
 * still needs to know we're alive during the install. */
bool NovaOtaBroker_IsInInstallMode(void);

/* Called from `bridge_rx_callback` when an Envelope field with tag in
 * the install-orchestration range (130..136) is decoded. The broker
 * walks the inner length-prefixed message body and updates state.
 *
 *   tag           Envelope field number (130..136).
 *   body          Pointer to the inner message bytes (after the
 *                 length-delimited varint, i.e. the proto3 wire bytes
 *                 of the FwInstall* sub-message).
 *   len           Length of `body`.
 *   envelope_seq  Envelope.seq for any Ack pairing the bridge expects.
 *                 (Today the install-side messages don't drive Ack —
 *                 the bridge listens for FwInstallProgress instead —
 *                 but we capture it for diagnostics.) */
void NovaOtaBroker_OnEnvelope(uint32_t tag,
                              const uint8_t *body,
                              size_t len,
                              uint32_t envelope_seq);

/* Called by `main.c`'s 100 ms periodic loop. Drives state-machine
 * timeouts (LP reboot wait, post-reboot probe) and emits periodic
 * progress envelopes during PUSHING. Cheap when state == IDLE. */
void NovaOtaBroker_Tick(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* NOVA_OTA_BROKER_H */
