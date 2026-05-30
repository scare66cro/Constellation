/*
 * orbit_role.h — Orbit firmware role enum + selection
 *
 * The same LP-AM2434 binary can boot as one of four roles depending on
 * what's persisted in OSPI device config (lp_device_config.h):
 *
 *   CONTROLLER  — talks to the RPi5 bridge over UART2 (legacy "controller LP"
 *                 behaviour). Spawns bridge_uart_task + nova_main_task +
 *                 lwip_smoke_task. Currently the only role with the bridge
 *                 envelope/CSV plumbing wired up.
 *
 *   STORAGE     — Modbus TCP slave on :5502, services HR 200-263 sensor
 *                 block, HR 100-163 VFD pass-through, 10 DI / 10 DO,
 *                 communication-loss safe mode. Drop-in replacement for
 *                 the orbit-simulator STORAGE role.
 *
 *   GDC         — Modbus TCP slave on :5502, services GDC register block
 *                 300-319, drives 5 actuator open/close pairs, runs the
 *                 stage-fill door logic and calibration cycle.
 *
 *   TRITON      — Modbus TCP slave on :5502, services Triton register
 *                 block 320-541, runs the GRC-faithful refrigeration
 *                 control core (tritonControl.ts equivalent in C).
 *
 * Role is read once at boot from `lp_device_config` and cached for the
 * lifetime of the process. There is no live role-swap; reprovisioning
 * requires a reboot (firmware-update path or settings-page reset).
 *
 * Pinning the role at boot lets the cadence/dispatch code branch on
 * compile-time-constant checks via `OrbitRole_Get()` returning a single
 * stable value, so the dead code for non-active roles can be inlined
 * away by the optimizer.
 */
#ifndef ORBIT_ROLE_H
#define ORBIT_ROLE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ORBIT_ROLE_CONTROLLER = 0,   /* Default — RPi5 bridge LP */
    ORBIT_ROLE_STORAGE    = 1,
    ORBIT_ROLE_GDC        = 2,
    ORBIT_ROLE_TRITON     = 3,
    ORBIT_ROLE_PULSAR     = 4,   /* Reserved, no impl yet */
} OrbitRole;

/* Cached role read from lp_device_config at boot. Safe to call from any
 * task once main() has reached the scheduler-start point. */
OrbitRole OrbitRole_Get(void);

/* True for any orbit-server role (STORAGE/GDC/TRITON/PULSAR). */
static inline bool OrbitRole_IsOrbit(OrbitRole r)
{
    return r == ORBIT_ROLE_STORAGE
        || r == ORBIT_ROLE_GDC
        || r == ORBIT_ROLE_TRITON
        || r == ORBIT_ROLE_PULSAR;
}

const char *OrbitRole_Name(OrbitRole r);

#endif /* ORBIT_ROLE_H */
