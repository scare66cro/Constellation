/*
 * lp_orbit_io.h — Real-hardware GPIO bridge for orbit s->do_[] / s->di[].
 *
 * Maps the orbit_state digital arrays onto LP-AM2434 GPIO pads. The pin
 * table lives in lp_orbit_io.c and references the
 * generated/ti_drivers_config.h macros emitted by SysConfig
 * (CONFIG_<NAME>_BASE_ADDR + CONFIG_<NAME>_PIN). Pin direction +
 * pinmux are programmed automatically by the SysConfig-generated
 * Drivers_open() — this module just reads/writes pin state.
 *
 * Channels with no syscfg instance are left as no-ops; this is
 * intentional so the firmware can ship while the BoosterPack pinout
 * is still being decided.
 *
 * Push (DO):  state machine → real hardware. Called from each orbit
 * role's tick task while holding OrbitState_Lock, after the state
 * machine update.
 *
 * Latch (DI): real hardware → state machine. NOT called for GDC today
 * (GDC's "limit switches" are virtual, driven by the position model
 * in orbit_gdc.c). Other roles that read real DI should call this at
 * the top of their tick.
 *
 * See docs/LP-AM2434-Hardware-Bringup-Plan.md §11 for the channel
 * table and BoosterPack pin allocation plan.
 */

#ifndef LP_ORBIT_IO_H
#define LP_ORBIT_IO_H

#include "orbit_server/orbit_state.h"

/* Initialize the IO module. Safe to call multiple times; idempotent.
 * Must be called AFTER Board_driversOpen (which programs pinmux + DIR
 * via the SysConfig-generated bring-up). Drives all wired DOs low. */
void LpOrbitIo_Init(void);

/* Push s->do_[0..9] to real GPIO outputs. Channels without a wired
 * pin are skipped silently. Caller must hold OrbitState_Lock. */
void LpOrbitIo_PushOutputs(const OrbitStateData *s);

/* Read real GPIO inputs into s->di[0..9]. Channels without a wired
 * pin leave s->di[i] unchanged. Caller must hold OrbitState_Lock. */
void LpOrbitIo_LatchInputs(OrbitStateData *s);

#endif /* LP_ORBIT_IO_H */
