/*
 * nova_ota_broker.h — Pi5 ↔ Nova install-orchestration broker (Phase 3.7,
 *                      UART-airgap OTA migration).
 *
 * The Nova-side counterpart to the Pi5's `firmwareInstaller.ts`. Phase 3
 * was the dispatchable skeleton (state machine + envelope decoders +
 * progress emitters); Phase 3.5 wired the remote-LP push leaf
 * (`nova_ota_push.c`); Phase 3.6 wired the controller self-update leaf
 * (`Platform/nova_fw_update.h`).
 *
 * Phase 3.7 (2026-05-23) — concurrency split. Up through 3.6 every
 * broker code path ran on whatever task processed UART RX
 * (`bridge_uart_task`). The leaf operations are synchronous lwIP +
 * OSPI calls that block for seconds at a time; the heartbeat emit on
 * the same task starves for the duration → the bridge tears down the
 * `firmware-data` socket after 15 s and reconnect-loops while the
 * broker is still pushing chunks. Today's bench rig reproduces this
 * with a ~6-minute install wedge.
 *
 * Solution: split. `bridge_uart_task` stays the UART RX owner and only
 * COPIES bytes into broker-owned queues + signals a semaphore — sub-ms
 * per envelope. A new `nova_ota_broker_task` (created in
 * `NovaOtaBroker_Init`, runs once the scheduler starts) drains the
 * queues and performs ALL leaf I/O. Heartbeat keeps flowing on
 * `bridge_uart_task` regardless of how long an install takes.
 *
 * Memory model: chunks are copied into a 16-slot ring at enqueue time
 * (each slot = chunk_size bytes, today 1024 B → 16 KB total in MSRAM).
 * Non-chunk envelopes (Begin/ComponentBegin/Finalize/Complete/Abort/
 * FleetProbe) go into a small command queue (depth 4, each ≤256 B
 * body). Ring overflow → install aborts cleanly with FwInstallProgress
 * (state=FAILED, error_code=104, "chunk ring full"). See CLAUDE.md
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

/* One-shot init. Resets state to IDLE, clears the current-component
 * struct, creates the FreeRTOS queues + mutex + signalling semaphore,
 * and spawns `nova_ota_broker_task` (statically allocated). Safe to
 * call before the scheduler starts; the task is created but doesn't
 * actually run until vTaskStartScheduler(). Per CLAUDE.md #9, no
 * DebugP_log calls — uses bb_uart0_puts via main.c's banner pattern. */
void NovaOtaBroker_Init(void);

/* True while a bundle install is in progress (BROKER_INSTALLING or the
 * sticky BROKER_ERROR tail). Called by `main.c` per-tick broadcasters
 * (SystemStatus, EquipmentStatus, SensorData, OrbitStatus,
 * OrbitSensorBank) to skip their per-tick work and free the UART back-
 * direction for progress envelopes. Heartbeat is intentionally NOT
 * gated on this — the bridge still needs to know we're alive during
 * the install. Lock-free read of a `volatile bool` updated by the
 * broker task on state transitions; safe from any context. */
bool NovaOtaBroker_IsInInstallMode(void);

/* Called from `bridge_rx_callback` when an Envelope field with tag in
 * the install-orchestration range (130..136) is decoded.
 *
 * MUST stay lightweight — runs on `bridge_uart_task` which also owns
 * the 100 ms heartbeat emit and the cadence broadcasters. Total wall
 * time budget per call: sub-millisecond. Implementation copies the
 * envelope bytes into a broker-owned queue (small command queue for
 * 130/131/133/134/135/136; dedicated 16-slot ring for 132 chunk
 * payloads) and signals the broker task via a binary semaphore. All
 * decoding + leaf I/O happens later on `nova_ota_broker_task`.
 *
 *   tag           Envelope field number (130..136).
 *   body          Pointer to the inner message bytes (after the
 *                 length-delimited varint, i.e. the proto3 wire bytes
 *                 of the FwInstall* sub-message). Buffer lifetime ends
 *                 when this function returns — we MUST copy.
 *   len           Length of `body`.
 *   envelope_seq  Envelope.seq for any Ack pairing the bridge expects.
 *                 (Today the install-side messages don't drive Ack —
 *                 the bridge listens for FwInstallProgress instead —
 *                 but we capture it for diagnostics.) */
void NovaOtaBroker_OnEnvelope(uint32_t tag,
                              const uint8_t *body,
                              size_t len,
                              uint32_t envelope_seq);

/* DEPRECATED in 3.7. Phase 3 had `main.c` per-tick into the broker for
 * state-machine maintenance; the broker task now ticks itself on a
 * 100 ms `vTaskDelay`. Retained as a no-op so callers can be removed
 * incrementally. */
void NovaOtaBroker_Tick(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* NOVA_OTA_BROKER_H */
