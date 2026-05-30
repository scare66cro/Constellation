/*
 * orbit_state.c — Shared state owner.
 */

#include "orbit_state.h"
#include "orbit_role.h"
#include "../lp_device_config.h"

#include <string.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

static OrbitStateData s_state;
static SemaphoreHandle_t s_state_mtx;
static StaticSemaphore_t s_state_mtx_buf;

void OrbitState_Init(void)
{
    memset(&s_state, 0, sizeof(s_state));

    const LpDeviceConfig *c = LpDeviceConfig_Get();
    /* Default unit_id mirrors the orbit-simulator: each orbit answers
     * unit_id = 1 on its own TCP socket. board_id is for cross-orbit
     * disambiguation when multiple orbits are bridged through a
     * single gateway. */
    s_state.unit_id  = 1;
    s_state.board_id = (uint16_t)(c->board_id != 0U ? c->board_id : 1U);

    /* Seed sensor block to "no sensor" so a fresh boot doesn't report
     * stale 0 °C readings. */
    for (size_t i = 0; i < ORBIT_SENSOR_BLOCK_SIZE; i++) {
        s_state.sensor_block[i] = ORBIT_SENSOR_VAL_UNDEF;
    }

    /* AO mode default = voltage (0). */
    for (size_t i = 0; i < ORBIT_AO_COUNT; i++) s_state.ao_mode[i] = 0;

    /* GDC cold-boot defaults — see orbit_gdc.h "cold-boot semantics".
     * memset already zeroed the GdcState struct; just seed close_switch
     * true so the master sees actuators "parked at 0%" and so the first
     * calibration cycle skips the pre-close phase. */
    for (size_t i = 0; i < ORBIT_GDC_NUM_ACTUATORS; i++) {
        s_state.gdc.act[i].close_switch = true;
    }

    /* TRITON cold-boot defaults — see orbit_triton.h.
     *   - All setpoints = 0 (no operator config; bridge programs via FC16).
     *   - All sensor-failure modes default to SAFE_OFF (1) with delay=0
     *     so an unconfigured sensor immediately latches a SAFE_OFF on
     *     the very first invalid reading — fail-safe for refrigeration.
     *   - All sensor values undef + invalid until the sensor RTU master
     *     binds and starts publishing.
     *   - compressor + unloaders OFF; alarms clear.
     * memset already zeroed the rest. */
    for (size_t i = 0; i < ORBIT_TRITON_NUM_SENSORS; i++) {
        s_state.triton.failure[i].mode      = 1; /* SAFE_OFF */
        s_state.triton.failure[i].delay_sec = 0;
        s_state.triton.sensor_value_x10[i]  = ORBIT_SENSOR_VAL_UNDEF;
        s_state.triton.sensor_valid[i]      = false;
    }

    s_state.last_poll_ticks = 0;
    s_state.comm_lost       = true;   /* until first poll arrives */
    s_state.safe_mode       = true;   /* default-safe on cold boot */

    s_state_mtx = xSemaphoreCreateMutexStatic(&s_state_mtx_buf);
    configASSERT(s_state_mtx != NULL);
}

void OrbitState_Lock(void)
{
    if (s_state_mtx == NULL) return;
    xSemaphoreTake(s_state_mtx, portMAX_DELAY);
}

void OrbitState_Unlock(void)
{
    if (s_state_mtx == NULL) return;
    xSemaphoreGive(s_state_mtx);
}

OrbitStateData *OrbitState_Get(void)
{
    return &s_state;
}

void OrbitState_TouchPoll(uint32_t now_ticks)
{
    s_state.last_poll_ticks = now_ticks;
}

uint32_t OrbitState_GetUptimeSecs(void)
{
    /* Single 32-bit aligned read — atomic on R5F; no lock needed. */
    return s_state.uptime_sec;
}
