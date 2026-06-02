/*
 * nova_vfd_client.h — Modbus TCP master to VFD endpoint(s)
 *
 * Phase 4b Sub-phase 3 (2026-06-01). Mirrors `orbit_client.c`'s shape
 * but targets a single VFD adapter (ABB FENA-01 in production, the
 * `rs485Responder` sim on the bench) instead of a fleet of orbit
 * boards. The endpoint is configured by the bridge via the
 * `VfdConfig` envelope (tag 127) at startup and any time the operator
 * changes the host/port; Nova caches in `LpSettings.vfd` and rebrings
 * this task with the new parameters.
 *
 * Behaviour:
 *   - On enable (host_ipv4 != 0), connect TCP to <host>:<port>.
 *   - Scan unit IDs 1..max_scan_unit_id every poll cycle until first
 *     response, then track the discovered set.
 *   - Poll each known drive at poll_interval_ms cadence — reads four
 *     register groups (process / group01 / fault / nameplate); each
 *     group emits one `VfdRegBank` envelope (tag 128) per cycle. The
 *     bridge decodes manufacturer-specific via vfdRegisterMaps.ts.
 *   - On disable (host_ipv4 == 0), tear down the socket + clear the
 *     known set — the bridge UI hides drives whose banks have aged out.
 *
 * Bridge → Nova writes ride `VfdRawWrite` (tag 129); main.c's RX
 * dispatch calls `NovaVfdClient_WriteRegisters` synchronously on a
 * dedicated short-lived socket so the polling task isn't interrupted.
 *
 * Threading: one FreeRTOS task (3 KB stack), spawned by lwip_smoke_task
 * after netif up. Reentry into the polling task is guarded by an
 * internal lock identical to orbit_client.c's `s_lock`.
 */
#ifndef NOVA_VFD_CLIENT_H
#define NOVA_VFD_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NOVA_VFD_MAX_DRIVES        4U    /* max scanned unit IDs (1..N).
                                          * Production sites have 1-4 fans;
                                          * raising this needs a corresponding
                                          * MSRAM budget review (link map). */
#define NOVA_VFD_MAX_REGS_PER_BANK 16U   /* sized for the largest group we read */

/* Register groups we poll per drive. Group identity matters because the
 * bridge picks a per-group decode rule (process-data vs group-01 actual
 * signals vs fault vs nameplate). Keep IDs stable — they're the
 * `hr_base` field on the wire. */
#define NOVA_VFD_GROUP_PROCESS_BASE   0U      /* CW, SpeedRef, StatusWord, ActualSpeed */
#define NOVA_VFD_GROUP_PROCESS_COUNT  4U
#define NOVA_VFD_GROUP_LIVE_BASE      100U    /* ABB Group 01 actual signals (9 regs) */
#define NOVA_VFD_GROUP_LIVE_COUNT     9U
#define NOVA_VFD_GROUP_FAULT_BASE     400U    /* ABB Group 04 active fault code */
#define NOVA_VFD_GROUP_FAULT_COUNT    1U
#define NOVA_VFD_GROUP_LIMITS_BASE    2001U   /* ABB Group 20: min/max freq */
#define NOVA_VFD_GROUP_LIMITS_COUNT   2U
#define NOVA_VFD_GROUP_RAMP_BASE      2202U   /* ABB Group 22: accel/decel */
#define NOVA_VFD_GROUP_RAMP_COUNT     2U
#define NOVA_VFD_GROUP_NAMEPLATE_BASE 9901U   /* ABB Group 99: rated I/V/Hz/RPM/kW */
#define NOVA_VFD_GROUP_NAMEPLATE_COUNT 5U

/* Per-drive snapshot of the most recent successful poll. The bridge
 * decodes from this via the VfdRegBank wire format; the cache lives on
 * Nova so `LpSettings_GetVfdConfig`-driven restarts don't drop data. */
typedef struct {
    uint8_t  unit_id;
    bool     online;
    uint32_t lastPollTickMs;
    uint32_t pollCount;
    uint32_t errorCount;

    uint16_t process  [NOVA_VFD_GROUP_PROCESS_COUNT];
    uint16_t live     [NOVA_VFD_GROUP_LIVE_COUNT];
    uint16_t fault    [NOVA_VFD_GROUP_FAULT_COUNT];
    uint16_t limits   [NOVA_VFD_GROUP_LIMITS_COUNT];
    uint16_t ramp     [NOVA_VFD_GROUP_RAMP_COUNT];
    uint16_t nameplate[NOVA_VFD_GROUP_NAMEPLATE_COUNT];
} NovaVfdSnapshot;

/* Initialize the VFD client subsystem.
 *
 * Spawns the worker task on first call. Safe to call before any
 * VfdConfig has been received — the task idles until
 * `LpSettings_GetVfdConfig()->configured == true && host_ipv4 != 0`.
 * Returns 0 on success, -1 on resource exhaustion. */
int NovaVfdClient_Init(void);

/* Re-read `LpSettings.vfd` and reconnect to the new endpoint on the
 * next poll cycle. Idempotent — safe to call repeatedly on each
 * VfdConfig RX. */
void NovaVfdClient_ConfigChanged(void);

/* Total number of drives currently considered "discovered" (last poll
 * succeeded). 0 means "no drives, or VFD subsystem disabled". */
size_t NovaVfdClient_DriveCount(void);

/* Snapshot the cached state for one drive by index (0..DriveCount-1).
 * Returns false if `index` is out of range. */
bool NovaVfdClient_GetSnapshot(uint8_t index, NovaVfdSnapshot *out);

/* Write a contiguous block of holding registers to one VFD unit.
 *
 * `count == 1` → FC06, `count > 1` → FC16, `count > 123` is rejected
 * (Modbus FC16 spec cap). Opens a dedicated short-lived TCP socket so
 * the polling task's socket isn't disturbed. Used by main.c's
 * `VfdRawWrite` envelope handler (tag 129) to forward bridge-issued
 * action writes.
 *
 * Returns 0 on success, negative on error (no config, no socket,
 * Modbus exception). */
int NovaVfdClient_WriteRegisters(uint8_t unit_id, uint16_t addr,
                                 const uint16_t *values, uint16_t count);

#endif /* NOVA_VFD_CLIENT_H */
