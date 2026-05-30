/*
 * orbit_sensor_rtu.c — Sensor RTU master poller (transport-agnostic).
 *
 * This is the framing/poll-loop layer; the actual UART4 + RS-485
 * driver plugs in via `orbit_sensor_rtu_bind_transport()`. See
 * mb_rtu.h for the transport vtable contract.
 *
 * Polling cadence: ORBIT_SENSOR_POLL_MS between full sweeps. A single
 * sweep walks unit_id 1..ORBIT_SENSOR_BOARDS sequentially with a
 * 100 ms timeout per board (≥ 5 char-times at 9600 baud, well above
 * the 3.5-char inter-frame gap, well under the per-sweep budget).
 */

#include "orbit_sensor_rtu.h"
#include "orbit_state.h"
#include "adc_convert.h"

#include <FreeRTOS.h>
#include <task.h>
#include <kernel/dpl/DebugP.h>
#include <string.h>

#define POLL_TIMEOUT_MS  100U

static const MbRtuTransport *s_transport = NULL;

void orbit_sensor_rtu_bind_transport(const MbRtuTransport *t)
{
    s_transport = t;
}

void orbit_sensor_rtu_set_test_value(uint8_t board_idx, uint8_t channel,
                                     int16_t value_x10)
{
    if (board_idx >= ORBIT_SENSOR_BOARDS) return;
    if (channel   >= ORBIT_SENSORS_PER_BOARD) return;
    OrbitState_Lock();
    OrbitState_Get()->sensor_block[
        (size_t)board_idx * ORBIT_SENSORS_PER_BOARD + channel
    ] = value_x10;
    OrbitState_Unlock();
}

/* Mark a board's 4 slots as "no data". Used on RTU error so the
 * controller LP sees a clean undef instead of a stale reading. */
static void mark_board_undef(uint8_t board_idx)
{
    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    size_t base = (size_t)board_idx * ORBIT_SENSORS_PER_BOARD;
    for (size_t i = 0; i < ORBIT_SENSORS_PER_BOARD; i++) {
        s->sensor_block[base + i] = ORBIT_SENSOR_VAL_UNDEF;
    }
    OrbitState_Unlock();
}

/* Poll one board: read HR 0..6, parse, convert, write into
 * OrbitState. Returns MB_RTU_OK on success. */
static int poll_one(uint8_t board_idx)
{
    if (s_transport == NULL) return MB_RTU_ERR_PARAM;

    /* Sensor-board unit ID = address + 1 (sim convention). */
    uint8_t unit_id = (uint8_t)(board_idx + 1);

    uint16_t regs[7];
    uint8_t  exc = 0;
    int rc = mb_rtu_read_holding(s_transport, unit_id, 0, 7,
                                 regs, POLL_TIMEOUT_MS, &exc);
    if (rc != MB_RTU_OK) {
        mark_board_undef(board_idx);
        return rc;
    }

    /* Stamp last-good poll for this board so the OrbitBoardStatus
     * encoder can report sensor_activity_secs. Anything past this
     * point counts as "we successfully spoke to the board" — disabled
     * boards still answer the FC03 frame. */
    OrbitState_Lock();
    OrbitState_Get()->sensor_last_ok_secs[board_idx] =
        OrbitState_GetUptimeSecs();
    OrbitState_Unlock();

    /* Decode per sensorBusClient.ts. */
    bool     disabled = (regs[1] & 0x01u) != 0u;
    uint16_t packed   = regs[2];
    uint8_t  types[ORBIT_SENSORS_PER_BOARD] = {
        (uint8_t)((packed >> 12) & 0xFu),
        (uint8_t)((packed >>  8) & 0xFu),
        (uint8_t)((packed >>  4) & 0xFu),
        (uint8_t)( packed        & 0xFu),
    };

    if (disabled) {
        mark_board_undef(board_idx);
        return MB_RTU_OK;
    }

    int16_t eng[ORBIT_SENSORS_PER_BOARD];
    for (int i = 0; i < ORBIT_SENSORS_PER_BOARD; i++) {
        eng[i] = adc_to_orbit_register(regs[3 + i], types[i]);
    }

    OrbitState_Lock();
    OrbitStateData *s = OrbitState_Get();
    size_t base = (size_t)board_idx * ORBIT_SENSORS_PER_BOARD;
    for (size_t i = 0; i < ORBIT_SENSORS_PER_BOARD; i++) {
        s->sensor_block[base + i] = eng[i];
    }
    OrbitState_Unlock();
    return MB_RTU_OK;
}

void orbit_sensor_rtu_task(void *args)
{
    (void)args;

    /* If no transport binds within the first second, log once and
     * keep the task alive — the framing layer is ready, only the
     * UART4 driver is missing. Useful state during bring-up. */
    bool warned = false;

    for (;;) {
        if (s_transport == NULL) {
            if (!warned) {
                DebugP_log("[SENSOR-RTU] no transport bound — UART4 driver pending\r\n");
                warned = true;
            }
            vTaskDelay(pdMS_TO_TICKS(ORBIT_SENSOR_POLL_MS));
            continue;
        }

        for (uint8_t i = 0; i < ORBIT_SENSOR_BOARDS; i++) {
            (void)poll_one(i);
            /* Inter-frame gap — 5 ms is comfortably > 3.5 char times
             * at 9600 baud (3.6 ms). At higher baud it's even more
             * conservative; the per-sweep budget is 16*(100+5) =
             * 1.68 s worst case, but in practice present boards
             * answer within a few ms so the sweep finishes in well
             * under 1 s. */
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        vTaskDelay(pdMS_TO_TICKS(ORBIT_SENSOR_POLL_MS));
    }
}
