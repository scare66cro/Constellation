/*
 * orbit_client.h — Modbus TCP client for the Constellation Orbit boards
 *
 * Polls one or more Orbit simulators / real Orbit AM2432 boards over
 * Modbus TCP (port 5502 in the simulator, standard 502 in production).
 *
 * Holding-register layout (matches `orbit-simulator/src/orbitSimulator.ts
 * ::getHoldingRegister`):
 *   0..1     analog outputs        (uint16, raw DAC counts)
 *   2..3     AO modes              (0=voltage, 1=current)
 *   100..163 VFD register window   (live VFD HR pass-through)
 *   200..263 sensor board readings (uint16 raw)
 *   300..319 GDC stage/door state  (only on GDC-role orbits)
 *   320..541 TRITON state/control  (only on TRITON-role orbits)
 *   40000    orbit ID
 *   40001    e-stop (0/1)
 *   40002    comm-lost (0/1)
 *   40003    safe-mode (0/1)
 *   40004    cpu temp × 10 (degC)
 *   40005    uptime low16
 *   40006    uptime high16
 *
 * The poller is intentionally transport-agnostic of role: it pulls the
 * full sensor + identity slice from every orbit, and the slices that
 * only matter for one role (GDC/TRITON) are read on-demand by callers
 * that know what they want.
 *
 * Threading: each orbit gets its own FreeRTOS task and its own TCP
 * socket so a single hung orbit cannot stall the others.
 */
#ifndef ORBIT_CLIENT_H
#define ORBIT_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ORBIT_CLIENT_MAX_ORBITS    5U   /* matches orbit-sim default 5-orbit deployment */
#define ORBIT_SENSOR_HR_BASE       200U
#define ORBIT_SENSOR_HR_COUNT      64U  /* 200..263 — bounded by orbit's HR_SENSOR_END.
                                         * Growing the bank requires bumping the orbit
                                         * server's HR_SENSOR_END *and* the firmware on
                                         * every orbit board, or extending the orbit
                                         * server to return 0 for reserved gaps the way
                                         * orbit_triton.c already does. */
#define ORBIT_AO_HR_BASE           0U
#define ORBIT_AO_HR_COUNT          4U   /* 0..3 */
#define ORBIT_IDENT_HR_BASE        40000U
#define ORBIT_IDENT_HR_COUNT       7U   /* 40000..40006 */

/* Per-role secondary HR window (Phase 4b 2026-06-01).
 *
 * STORAGE orbits leave this window empty — the sensor block above
 * already covers everything the UI consumes. GDC and TRITON orbits
 * advertise a windowed view of their role-specific HR region so the
 * controller LP can mirror it into an `OrbitSensorBank` envelope
 * and the bridge can stop opening Modbus TCP sockets from the Pi5
 * (production UART airgap, see `docs/uart-airgap-architecture.md`).
 *
 * Bank sizes match what the orbit server actually serves today.
 * Growth requires extending the orbit server's HR maps (TRITON has
 * a reserved-gap pattern that returns 0 — extending the bank just
 * means bumping HR_TRITON_END). The buffer `roleHr[ORBIT_ROLE_HR_MAX]`
 * is sized to 256 so the per-role count can grow without a struct
 * resize. */
#define ORBIT_ROLE_HR_MAX          256U /* upper bound — sizes the cache buffer */
#define ORBIT_ROLE_HR_TRITON_BASE  400U
#define ORBIT_ROLE_HR_TRITON_COUNT 143U /* 400..542, matches HR_TRITON_END=543 */
#define ORBIT_ROLE_HR_GDC_BASE     300U
#define ORBIT_ROLE_HR_GDC_COUNT    40U  /* 300..339, matches HR_GDC_END=340 */
#define ORBIT_ROLE_HR_STORAGE_BASE 100U
#define ORBIT_ROLE_HR_STORAGE_COUNT 48U /* 100..147 — VFD RTU passthrough.
                                         * 3 drives × 16 regs, per the orbit_storage.h
                                         * HR_VFD_BASE..HR_VFD_END layout. STORAGE
                                         * orbit's role window publishes whatever its
                                         * vfd_regs[] cache currently holds; the bridge
                                         * decodes per-unit slices via vfdRegisterMaps.
                                         * Bench note (2026-06-02): orbit_storage.c
                                         * stubs writes to this region locally —
                                         * Phase 4b plumbs the wire; orbit-side RS485
                                         * RTU master comes later. */

/* Discrete I/O windows polled from each remote orbit (Apr 2026).
 * See orbit_server/orbit_storage.h for the canonical bit map:
 *   - Coils 0..9        : digital outputs (DO 0..9)
 *   - Discrete in 0..9  : digital inputs  (DI 0..9)
 *   - Discrete in 10    : E-stop
 *   - Discrete in 11..14: DC24V monitor channels
 */
#define ORBIT_DO_COIL_BASE         0U
#define ORBIT_DO_COIL_COUNT        10U   /* coils 0..9 = DO 0..9 */
#define ORBIT_DI_DISC_BASE         0U
#define ORBIT_DI_DISC_COUNT        15U   /* 0..9 + estop(10) + DC24V mon 11..14 */

/* Per-orbit cached sample. Updated atomically (under s_lock) by the
 * orbit task, read by the bridge-side encoder. */
typedef struct OrbitSample {
    uint32_t lastUpdateTickMs;   /* osal tick at last successful poll */
    uint32_t pollCount;          /* successful polls since boot */
    uint32_t errorCount;         /* failed polls since boot */
    bool     online;             /* true while last poll succeeded */

    uint16_t sensorHr[ORBIT_SENSOR_HR_COUNT]; /* HR[200..263] (Phase 4b: stays at 64 until orbit server's HR_SENSOR_END is bumped in lockstep) */
    uint16_t aoHr    [ORBIT_AO_HR_COUNT];     /* HR[0..3] */
    uint16_t ident   [ORBIT_IDENT_HR_COUNT];  /* HR[40000..40006] */

    /* Per-role secondary HR window (Phase 4b 2026-06-01).
     *
     * Populated based on `LpSettings_GetOrbitRole(slot)` at the start
     * of each poll cycle:
     *   STORAGE / UNASSIGNED → roleHrCount = 0  (no secondary read)
     *   GDC                  → roleHrBase = 300, roleHrCount = 96
     *   TRITON               → roleHrBase = 400, roleHrCount = 256
     *
     * `roleHr[]` is sized to `ORBIT_ROLE_HR_MAX` so the same buffer
     * holds whichever role's data is active. The encoder reads only
     * `[0..roleHrCount)` and emits an `OrbitSensorBank` keyed by
     * `roleHrBase`. `roleHrValid` distinguishes "never polled" from
     * "really zeros"; cleared on role change so the bridge can drop
     * stale role data when an orbit gets re-assigned. */
    uint16_t roleHr[ORBIT_ROLE_HR_MAX];
    uint16_t roleHrBase;
    uint16_t roleHrCount;
    bool     roleHrValid;

    /* Discrete I/O bitmaps (Apr 2026 LP-I/O extension).
     *  - do_bitmap : bits 0..9 = remote DO 0..9 (FC01 read coils 0..9).
     *  - di_bitmap : bits 0..9 = DI 0..9, bit 10 = E-stop, bits 11..14
     *                = DC24V monitor channels (FC02 discrete inputs 0..14).
     * io_valid is `false` until the first successful FC01+FC02 cycle so
     * the encoder can distinguish "never polled" from "really 0". The
     * bitmap fields are NEVER zeroed on poll failure — last-known value
     * is held until the next successful read (mirrors the sensor-block
     * keep-last-on-error policy).  lastIoOkTickMs stamps the last
     * successful FC01+FC02 cycle for future age-out.
     */
    uint16_t do_bitmap;
    uint16_t di_bitmap;
    bool     io_valid;
    uint32_t lastIoOkTickMs;
} OrbitSample;

typedef struct OrbitConfig {
    uint8_t  index;         /* 0-based slot, also default Modbus unit ID */
    char     ipv4[16];      /* "10.1.2.230" etc. */
    uint16_t port;          /* Modbus TCP port (5502 sim, 502 prod) */
    uint16_t pollIntervalMs;/* min interval between polls (default 1000) */
} OrbitConfig;

/* Initialize the orbit client subsystem.
 *
 * Spawns one worker task per configured orbit. Safe to call exactly
 * once after lwIP is up. Returns 0 on success, -1 on resource
 * exhaustion (queue / task / socket).
 */
int OrbitClient_Init(const OrbitConfig *configs, size_t count);

/* Snapshot the latest sample for one orbit.
 *
 * Returns false if `index` is out of range or the orbit has never
 * successfully polled. Otherwise writes a value-copy into `*out`.
 * Safe to call from any task.
 */
bool OrbitClient_GetSample(uint8_t index, OrbitSample *out);

/* Total number of orbits configured (== `count` passed to Init). */
size_t OrbitClient_Count(void);

/* Get the configured IPv4 string for an orbit slot. Returns NULL if
 * `index` is out of range. Pointer remains valid for the lifetime of
 * the OrbitWorker (i.e. forever — workers are static).
 *
 * Used by nova_ota_push.c to enumerate orbit destinations for the
 * fleet probe (Phase 3.5). Read-only: never mutate `*cfg`. */
const char *OrbitClient_GetIpv4(uint8_t index);

/* Write a single Modbus holding register on one orbit (FC06).
 *
 * Synchronous: opens a short-lived dedicated TCP socket so the call
 * does not interfere with the per-orbit polling task's socket. Used
 * by the bridge `TritonRegWrite` proto handler to forward UI-side
 * Triton control writes (manual mode, setpoints, fail config, ack).
 *
 * Returns 0 on success, negative on error (index out of range,
 * connect/send/recv failure, Modbus exception). Typical latency on a
 * healthy LAN is 10-20 ms; cap your caller-side timeout accordingly.
 */
int OrbitClient_WriteHoldingRegister(uint8_t index, uint16_t addr,
                                     uint16_t value);

/* Write a contiguous block of Modbus holding registers on one orbit
 * (FC16 — Write Multiple Holding Registers).
 *
 * Phase 4b Sub-phase 2 (2026-06-01): used by the bridge's
 * `OrbitRegWrite` envelope handler when the operator hands down 2+
 * values (e.g. `/iot/gdc/stages` writes a 5-reg actuator→stage
 * assignment block in one round-trip). `count` must be 1..123 per
 * Modbus spec; for `count==1` callers prefer
 * `OrbitClient_WriteHoldingRegister` (FC06) for the smaller PDU.
 *
 * Synchronous: opens a short-lived dedicated TCP socket so the call
 * does not interfere with the per-orbit polling task's socket.
 *
 * Returns 0 on success, negative on error (index out of range,
 * connect/send/recv failure, Modbus exception). Typical latency on a
 * healthy LAN is 10-20 ms; cap your caller-side timeout accordingly. */
int OrbitClient_WriteHoldingRegisters(uint8_t index, uint16_t addr,
                                      const uint16_t *values,
                                      uint16_t count);

/* Write a single Modbus coil on one orbit (FC05).
 *
 * Synchronous: opens a short-lived dedicated TCP socket so the call
 * does not interfere with the per-orbit polling task's socket. Used
 * by the CONTROLLER LP equipment-output-sync loop (Phase E2,
 * fw 0.A.15+) to drive STORAGE remote DOs in response to a
 * REMOTE_MANUAL operator override on the Equipment Control page.
 *
 * `coil_addr` is the zero-based coil index (0..9 for STORAGE DO0..DO9 —
 * see `ORBIT_DO_COIL_COUNT`). `on=true` writes 0xFF00 (energize),
 * `on=false` writes 0x0000 (de-energize), per Modbus FC05 spec.
 *
 * Returns 0 on success, negative on error (index out of range,
 * connect/send/recv failure, Modbus exception). Typical latency on a
 * healthy LAN is 10-20 ms; cap your caller-side timeout accordingly.
 */
int OrbitClient_WriteCoil(uint8_t index, uint16_t coil_addr, bool on);

#endif /* ORBIT_CLIENT_H */
