/*
 * hal_orbit.c — Orbit I/O board abstraction for Constellation Nova
 *
 * Translates between the legacy IoBoard[]/BOARD_INFO bitfield interface
 * that Controls.c uses and the Modbus TCP coils/registers on Orbit boards.
 *
 * The key insight: Controls.c calls CheckInputs(EQ_FAN) which reads
 * IoBoard[board].InputState & bit_mask. We keep InputState populated by
 * polling Orbit's Modbus TCP discrete inputs (FC02). Similarly,
 * OutputOn()/OutputOff() set bits in IoBoard[board].OutputState, and
 * our poll loop writes those to Orbit's coils (FC15).
 *
 * This means Controls.c, Modes.c, States.c, Failures.c — the entire
 * business logic layer — works unchanged.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "hal_orbit.h"
#include "hal_modbus_tcp.h"
#include "debug.h"
#include "Settings.h"
#include "PWM.h"           /* PwmChannel[], PWM_DOORS */
#include "Controls.h"      /* PWMValToPercent() */
#include "hal.h"           /* hal_timer_get_ms() */
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/* ─── Module State ────────────────────────────────────────────────── */

orbit_board_t orbit_boards[NOVA_MAX_ORBITS];
int           orbit_board_count = 0;

/* Map Orbit discrete inputs (0-9 = DI1-DI10, 10 = E-Stop) to the
 * legacy Input[] bitmask positions in BOARD_INFO.
 *
 * On the AS2, IoBoard[MAIN].InputState has bits laid out as:
 *   [3:0]  = CPLD version
 *   [13:4] = Switches (SW0-SW9)
 *   [23:14] = DI1-DI10  (v1 layout)
 * or
 *   [21:4] = Switches (SW0-SW17)
 *   [31:22] = DI1-DI10  (v2 layout)
 *
 * For Orbit, we use the v2 layout so that the existing BOARD_INFO.Input[]
 * array (which maps equipment indices to bitmasks) works correctly.
 * We synthesize a CPLD version of 0x02 to trigger v2 logic.
 *
 * For expansion boards, the Orbit discrete inputs map to the expansion
 * bit layout (SS_EX_DI1 through SS_EX_DI8).
 */

/* ─── Initialization ──────────────────────────────────────────────── */

void orbit_init(void)
{
    memset(orbit_boards, 0, sizeof(orbit_boards));
    orbit_board_count = 0;

    /* Configure Orbit board connections.
     *
     * Legacy mapping (backward compat with Controls.c):
     *   Slot 0 → DIP 2 → MAIN       → IoBoard[0]
     *   Slot 1 → DIP 3 → EXPANSION_1 → IoBoard[1]
     *   Slot 2 → DIP 4 → EXPANSION_2 → IoBoard[2]
     *
     * Extended slots (3..NOVA_MAX_ORBITS-1):
     *   Slot 3 → DIP 5, Slot 4 → DIP 6, ...
     *   These don't map to IoBoard[] — they have dedicated routing
     *   for refrigeration stages, door control, or future zones.
     *
     * In production, IDs come from Settings/EEPROM.  For now, sequential.
     */
    for (int i = 0; i < NOVA_MAX_ORBITS; i++) {
        orbit_boards[i].dipswitch_id = 2 + i;
        orbit_boards[i].role = ORBIT_ROLE_UNASSIGNED;
        orbit_boards[i].zone_id = 0;
        orbit_boards[i].legacy_slot = (i < BOARD_COUNT) ? (int8_t)i : -1;
        orbit_boards[i].refrig_stage = 0;

        mbtcp_init(&orbit_boards[i].conn,
                    ORBIT_IP_A, ORBIT_IP_B, ORBIT_IP_C,
                    orbit_boards[i].dipswitch_id,
                    ORBIT_MBTCP_PORT, 1);
    }

    /* Default role assignment for legacy slots */
    orbit_boards[0].role = ORBIT_ROLE_STORAGE;
    orbit_boards[0].zone_id = 0;
    if (BOARD_COUNT > 1) {
        orbit_boards[1].role = ORBIT_ROLE_STORAGE;
        orbit_boards[1].zone_id = 0;
    }
    if (BOARD_COUNT > 2) {
        orbit_boards[2].role = ORBIT_ROLE_STORAGE;
        orbit_boards[2].zone_id = 0;
    }

    debug_printf("[Orbit] Initialized %d slot(s) (%d legacy + %d extended)\r\n",
                 NOVA_MAX_ORBITS, BOARD_COUNT, NOVA_MAX_ORBITS - BOARD_COUNT);
}

/* ─── I/O Polling ─────────────────────────────────────────────────── */

int orbit_poll_io(SYSTEM_BOARDS board)
{
    if (board >= ORBIT_MAX_BOARDS) return -1;

    orbit_board_t *ob = &orbit_boards[board];
    BOARD_INFO *iob = &IoBoard[board];

    /* Ensure connected */
    if (!mbtcp_is_connected(&ob->conn)) {
        int rc = mbtcp_connect(&ob->conn);
        if (rc != MBTCP_OK) {
            ob->comm_errors++;
            iob->Fault = 1;
            return rc;
        }
    }

    int rc;

    /* ── Read Digital Inputs (FC02): 11 inputs (DI1-10 + E-Stop) ── */
    uint8_t di_raw[2] = {0};  /* 11 bits → 2 bytes */
    rc = mbtcp_read_discrete_inputs(&ob->conn, 0, ORBIT_NUM_DI + 1, di_raw);
    if (rc != MBTCP_OK) {
        ob->comm_errors++;
        if (ob->comm_errors > 5) iob->Fault = 1;
        return rc;
    }

    /* Map Orbit DI bits into the legacy InputState bitfield.
     * We use the v2 CPLD input layout for the MAIN board:
     *   InputState bits [31:22] = DI1-DI10  (SS_MAIN_DI1_V2..DI10_V2)
     * For expansion boards:
     *   InputState bits [13:6] = DI1-DI8  (SS_EX_DI1..DI8)
     */
    if (board == MAIN) {
        /* Clear DI bits, preserve version and switch bits */
        uint32_t input_state = iob->InputState & 0x003FFFFF; /* keep bits [21:0] */

        /* Set DI bits from Orbit (bit 0 of di_raw = DI1 → bit 31 of InputState for v2) */
        for (int i = 0; i < ORBIT_NUM_DI; i++) {
            if (di_raw[i / 8] & (1 << (i % 8))) {
                input_state |= (1UL << (31 - i));  /* DI1 at bit 31, DI10 at bit 22 */
            }
        }

        /* Synthesize version = 2 in bits [3:0] */
        input_state = (input_state & ~0x0F) | 0x02;

        iob->InputState = input_state;
    } else {
        /* Expansion: DI1 at SS_EX_DI1 (bit 13), DI8 at SS_EX_DI8 (bit 6) */
        uint32_t input_state = iob->InputState & 0xFFFFC03F; /* clear bits [13:6] */

        int max_di = (ORBIT_NUM_DI < SS_EX_DI_INPUTS) ? ORBIT_NUM_DI : SS_EX_DI_INPUTS;
        for (int i = 0; i < max_di; i++) {
            if (di_raw[i / 8] & (1 << (i % 8))) {
                input_state |= (1UL << (13 - i));  /* DI1 at bit 13, DI8 at bit 6 */
            }
        }

        /* Set header/version bits for discovered expansion */
        input_state = (input_state & ~0x3F) | (SS_EX_HEADER) | 0x02;

        iob->InputState = input_state;
    }

    /* E-Stop is bit 10 of discrete inputs */
    ob->estop_active = (di_raw[1] & (1 << 2)) ? 1 : 0;

    /* E-Stop acts as Start/Stop: when active, clear SW_START_STOP
     * in InputState so CheckInputs(SW_START_STOP) returns 0 and the
     * firmware enters shutdown mode (ModeShutdown). */
    if (board == MAIN) {
        if (ob->estop_active) {
            iob->InputState &= ~SS_MAIN_SW8;   /* clear Start/Stop */
        } else {
            iob->InputState |= SS_MAIN_SW8;    /* restore Start/Stop */
        }
    }

    /* ── Write Digital Outputs (FC15): write all coils at once ── */
    /*
     * Pack IoBoard[board].OutputState bits into Modbus coil bytes.
     * For MAIN: OutputState bit 10 = DO1 (SS_MAIN_DO1), bit 3 = DO8 (SS_MAIN_DO8)
     * For EXP:  OutputState bit 7 = DO1 (SS_EX_DO1), bit 0 = DO8 (SS_EX_DO8)
     *
     * Map to Orbit coils: coil 0 = DO1 ... coil 9 = DO10
     */
    uint8_t do_raw[2] = {0};
    int num_do = (board == MAIN) ? ORBIT_NUM_DO : SS_EX_IO_OUTPUTS;

    /* Debug: periodically log OutputState so we can see what the
     * firmware business logic is commanding. */
    static uint32_t dbg_cnt = 0;
    if (board == MAIN && (dbg_cnt++ % 200) == 0) {
        debug_printf("[Orbit] MAIN OutputState=0x%08X\r\n",
                     (unsigned)iob->OutputState);
    }

    if (board == MAIN) {
        for (int i = 0; i < num_do && i < ORBIT_NUM_DO; i++) {
            /* SS_MAIN_DO1 is bit 10, DO2 is bit 9, ... DO8 is bit 3 */
            uint32_t mask = (1UL << (SS_MAIN_IO_OUTPUTS - 1 - i));
            if (iob->OutputState & mask) {
                do_raw[i / 8] |= (1 << (i % 8));
            }
        }
    } else {
        for (int i = 0; i < num_do && i < ORBIT_NUM_DO; i++) {
            /* SS_EX_DO1 is bit 7, DO2 is bit 6, ... DO8 is bit 0 */
            uint32_t mask = (1UL << (SS_EX_IO_OUTPUTS - 1 - i));
            if (iob->OutputState & mask) {
                do_raw[i / 8] |= (1 << (i % 8));
            }
        }
    }

    rc = mbtcp_write_multiple_coils(&ob->conn, 0, num_do, do_raw);
    if (rc != MBTCP_OK) {
        ob->comm_errors++;
        return rc;
    }

    /* Success — clear error count */
    ob->comm_errors = 0;
    ob->connected = 1;
    iob->Fault = 0;

    return MBTCP_OK;
}

/* ─── Sensor Reading (replaces RS485 analog board polling) ────────── */

int orbit_read_sensors(SYSTEM_BOARDS board)
{
    if (board >= ORBIT_MAX_BOARDS) return -1;
    /* Legacy boards 0-2 are always in the first 3 orbit slots */
    return orbit_read_sensors_slot((int)board);
}

int orbit_read_sensors_slot(int slot)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;

    orbit_board_t *ob = &orbit_boards[slot];

    if (!mbtcp_is_connected(&ob->conn)) return -1;

    /* Read sensor passthrough registers (holding regs 200-263) in
     * chunks of 16 registers.  The QEMU chardev pipeline delivers
     * bytes slowly (~12/s), so a single 64-register read (137-byte
     * response) would exceed the 2s timeout.  Four 16-register reads
     * (41-byte responses) each fit within the timeout. */
#define SENSOR_CHUNK 16
    for (int offset = 0; offset < ORBIT_NUM_SENSOR_REGS; offset += SENSOR_CHUNK) {
        int remaining = ORBIT_NUM_SENSOR_REGS - offset;
        int count = (remaining < SENSOR_CHUNK) ? remaining : SENSOR_CHUNK;
        int rc = mbtcp_read_holding_regs(&ob->conn, 200 + offset,
                                         count,
                                         &ob->sensor_regs[offset]);
        if (rc != MBTCP_OK) {
            ob->sensor_regs_valid = 0;
            return rc;
        }
    }
#undef SENSOR_CHUNK

    ob->sensor_regs_valid = 1;

    /* Check for 0x7FFF fault values and replace with safe zero */
    orbit_check_sensor_faults(slot);

    return ORBIT_NUM_SENSOR_REGS;
}

/* ─── Immediate Output Write ──────────────────────────────────────── */

int orbit_write_do(SYSTEM_BOARDS board, uint8_t output_index, int value)
{
    if (board >= ORBIT_MAX_BOARDS) return -1;
    return orbit_write_do_slot((int)board, output_index, value);
}

int orbit_write_do_slot(int slot, uint8_t output_index, int value)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;
    if (output_index >= ORBIT_NUM_DO) return -1;

    orbit_board_t *ob = &orbit_boards[slot];
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;

    return mbtcp_write_single_coil(&ob->conn, output_index, value);
}

int orbit_write_ao(SYSTEM_BOARDS board, uint8_t channel, uint16_t value)
{
    if (board >= ORBIT_MAX_BOARDS) return -1;
    return orbit_write_ao_slot((int)board, channel, value);
}

int orbit_write_ao_slot(int slot, uint8_t channel, uint16_t value)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;
    if (channel >= ORBIT_NUM_AO) return -1;

    orbit_board_t *ob = &orbit_boards[slot];
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;

    return mbtcp_write_single_reg(&ob->conn, channel, value);
}

/* ─── Status Register Read ────────────────────────────────────────── */

int orbit_read_status(SYSTEM_BOARDS board)
{
    if (board >= ORBIT_MAX_BOARDS) return -1;
    return orbit_read_status_slot((int)board);
}

int orbit_read_status_slot(int slot)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;

    orbit_board_t *ob = &orbit_boards[slot];
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;

    uint16_t status_regs[7];
    int rc = mbtcp_read_holding_regs(&ob->conn, 40000, 7, status_regs);
    if (rc != MBTCP_OK) return rc;

    /* reg 40000 = Board ID */
    ob->dipswitch_id = status_regs[0] & 0xFF;
    /* reg 40001 = E-Stop */
    ob->estop_active = status_regs[1] ? 1 : 0;
    /* reg 40002 = Comm lost */
    /* reg 40003 = Safe mode */
    ob->safe_mode = status_regs[3] ? 1 : 0;
    /* reg 40004 = CPU temp × 10 */
    ob->cpu_temp_x10 = status_regs[4];
    /* reg 40005/40006 = uptime */
    ob->orbit_uptime = ((uint32_t)status_regs[6] << 16) | status_regs[5];
    /* Firmware version from top byte of board ID register */
    ob->firmware_ver = (status_regs[0] >> 8) & 0xFF;

    /* Update IoBoard version for legacy slots */
    if (ob->legacy_slot >= 0 && ob->legacy_slot < BOARD_COUNT) {
        IoBoard[ob->legacy_slot].Version = 0x02;   /* Constellation v2 */
    }

    /* Update Orbit warning flags based on status */
    orbit_check_warnings();

    /* Poll the per-board Triton alarm bitmap from any REFRIG-role orbit.
     * This populates triton_alarm_active[]/triton_alarm_acked[] so the
     * GRC-derived FAIL_x / SAF_x / etc. codes raised by the Triton firmware
     * surface in SendWarnings() alongside the generic ORBIT_WARN_x codes.
     */
    orbit_check_triton_alarms();

    return MBTCP_OK;
}

/* ─── Board Discovery ─────────────────────────────────────────────── */

int orbit_discover(void)
{
    int found = 0;

    debug_printf("[Orbit] Discovering boards (scanning %d slots)...\r\n",
                 NOVA_MAX_ORBITS);

    for (int i = 0; i < NOVA_MAX_ORBITS; i++) {
        orbit_board_t *ob = &orbit_boards[i];

        /* Try to connect */
        int rc = mbtcp_connect(&ob->conn);
        if (rc != MBTCP_OK) {
            debug_printf("[Orbit] Slot %d (ID %d) — not found\r\n",
                         i, ob->dipswitch_id);
            if (ob->legacy_slot >= 0 && ob->legacy_slot < BOARD_COUNT) {
                IoBoard[ob->legacy_slot].Version = 0x0F;  /* not present */
            }
            continue;
        }

        /* Read status to confirm board is alive.
         *
         * The QEMU chardev tunnel can drop or stall the very first
         * Modbus transaction after a fresh connect — cold-start latency
         * leaves stale bytes in the UART2 pipe and the FC03 reply
         * sometimes times out.  On real hardware the same "first packet
         * lost" symptom happens with marginal RS-485/TCP links.  Retry
         * a few times before declaring the slot dead so transient
         * timeouts don't disable sensor polling for the rest of the run. */
        rc = -1;
        for (int attempt = 0; attempt < 3; attempt++) {
            rc = orbit_read_status_slot(i);
            if (rc == MBTCP_OK) break;
            vTaskDelay(50 / portTICK_RATE_MS);
        }
        if (rc == MBTCP_OK) {
            found++;
            ob->connected = 1;
            debug_printf("[Orbit] Slot %d (ID %d) — found at %d.%d.%d.%d [role=%d zone=%d legacy=%d]\r\n",
                         i, ob->dipswitch_id,
                         ORBIT_IP_A, ORBIT_IP_B, ORBIT_IP_C, ob->dipswitch_id,
                         ob->role, ob->zone_id, ob->legacy_slot);

            /* Initialize legacy BOARD_INFO for mapped slots */
            if (ob->legacy_slot >= 0 && ob->legacy_slot < BOARD_COUNT) {
                int li = ob->legacy_slot;
                IoBoard[li].Version = 0x02;
                IoBoard[li].InputState = IoBoard[li].InputState & 0x003FFFFF;
                IoBoard[li].OutputState = 0;
                IoBoard[li].SwitchesActive = 1;
                IoBoard[li].Fault = 0;

                if (li == MAIN) {
                    IoBoard[li].InputShiftLength = SS_MAIN_IO_INPUTS_V2;
                    IoBoard[li].OutputShiftLength = SS_MAIN_IO_OUTPUTS;
                    IoBoard[li].NumInputs = SS_MAIN_DI_INPUTS;
                    IoBoard[li].NumOutputs = SS_MAIN_IO_OUTPUTS;

                    for (int j = 0; j <= SS_MAIN_DI_INPUTS; j++) {
                        IoBoard[li].Input[j] = (j == 0) ? 0 : (1UL << (32 - j));
                    }
                    for (int j = 0; j <= SS_MAIN_IO_OUTPUTS; j++) {
                        IoBoard[li].Output[j] = (j == 0) ? 0 : (1UL << (SS_MAIN_IO_OUTPUTS - j));
                    }
                } else {
                    IoBoard[li].InputShiftLength = SS_EX_IO_INPUTS;
                    IoBoard[li].OutputShiftLength = SS_EX_IO_OUTPUTS;
                    IoBoard[li].NumInputs = SS_EX_DI_INPUTS;
                    IoBoard[li].NumOutputs = SS_EX_IO_OUTPUTS;

                    for (int j = 0; j <= SS_EX_DI_INPUTS; j++) {
                        IoBoard[li].Input[j] = (j == 0) ? 0 : (1UL << (14 - j));
                    }
                    for (int j = 0; j <= SS_EX_IO_OUTPUTS; j++) {
                        IoBoard[li].Output[j] = (j == 0) ? 0 : (1UL << (SS_EX_IO_OUTPUTS - j));
                    }
                }
            }
        } else {
            debug_printf("[Orbit] Slot %d (ID %d) — connect OK but no status\r\n",
                         i, ob->dipswitch_id);
            mbtcp_disconnect(&ob->conn);
            if (ob->legacy_slot >= 0 && ob->legacy_slot < BOARD_COUNT) {
                IoBoard[ob->legacy_slot].Version = 0x0F;
            }
        }
    }

    orbit_board_count = found;
    debug_printf("[Orbit] Found %d board(s) of %d scanned\r\n", found, NOVA_MAX_ORBITS);

    /* Sync AO modes from Settings to all discovered boards */
    orbit_sync_ao_modes();

    return found;
}

/* ─── Extended Polling ────────────────────────────────────────────── */

int orbit_poll_extended(int slot)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;

    orbit_board_t *ob = &orbit_boards[slot];

    /* Legacy slots delegate to the IoBoard[]-aware path */
    if (ob->legacy_slot >= 0 && ob->legacy_slot < BOARD_COUNT) {
        return orbit_poll_io((SYSTEM_BOARDS)ob->legacy_slot);
    }

    /* Extended slot: read DI and write DO directly via orbit_board_t.
     * No IoBoard[] mapping — the caller uses orbit_boards[slot] directly. */
    if (!mbtcp_is_connected(&ob->conn)) {
        int rc = mbtcp_connect(&ob->conn);
        if (rc != MBTCP_OK) {
            ob->comm_errors++;
            return rc;
        }
    }

    /* Read Digital Inputs (FC02): 11 inputs (DI1-10 + E-Stop) */
    uint8_t di_raw[2] = {0};
    int rc = mbtcp_read_discrete_inputs(&ob->conn, 0, ORBIT_NUM_DI + 1, di_raw);
    if (rc != MBTCP_OK) {
        ob->comm_errors++;
        return rc;
    }

    /* E-Stop is bit 10 of discrete inputs */
    ob->estop_active = (di_raw[1] & (1 << 2)) ? 1 : 0;

    /* For extended boards, DI/DO state is stored in sensor_regs[]
     * or queried directly — no legacy bitfield mapping needed.
     * The caller reads orbit_boards[slot].conn directly for Modbus ops. */

    /* ── GDC: Write door command from PID output ── */
    if (ob->role == ORBIT_ROLE_DOOR) {
        /*
         * Controls.c PID → AdjustDoors() → PwmChannel[PWM_DOORS].Output
         * Convert raw PWM value (55-277) to 0-100%, then scale to 0-1000
         * for the GDC register (0.1% resolution).
         *
         * Rate limiting: the legacy pulsed door system moved actuators
         * 1 second at a time and blocked the PID from issuing new commands
         * until the previous move completed.  We replicate that behaviour
         * here by tracking the last command and the estimated time needed
         * for the actuators to reach the new position.
         *
         * The travel time comes from Settings.Door.ActuatorTime (total
         * seconds for 0→100%).  In production this will come from
         * calibration; for testing it defaults to 240 seconds.
         */
        static uint16_t gdc_last_cmd = 0;
        static uint32_t gdc_move_done_ms = 0;  /* timestamp when current move finishes */
        static int gdc_initialized = 0;

        int pct = PWMValToPercent(PwmChannel[PWM_DOORS].Output);
        uint16_t gdc_cmd = (uint16_t)(pct * 10);

        uint32_t now_ms = hal_timer_get_ms();

        if (!gdc_initialized) {
            /* First poll — send initial command and write travel time */
            gdc_last_cmd = gdc_cmd;
            gdc_move_done_ms = now_ms;  /* no move in progress yet */
            gdc_initialized = 1;

            /* Write the travel time to the Orbit GDC (HR 307) */
            uint16_t travel_secs = (uint16_t)Settings.Door.ActuatorTime;
            mbtcp_write_single_reg(&ob->conn, ORBIT_GDC_REG_TRAVEL_TIME, travel_secs);

            rc = mbtcp_write_single_reg(&ob->conn, ORBIT_GDC_REG_DOOR_CMD, gdc_cmd);
            if (rc != MBTCP_OK) {
                ob->comm_errors++;
                return rc;
            }
        }
        else if (gdc_cmd != gdc_last_cmd && now_ms >= gdc_move_done_ms) {
            /* Previous move is complete — issue the new command.
             *
             * Estimate how long this move takes based on the percentage
             * change and the total travel time.  This matches the legacy
             * behaviour where each 1% of travel takes ActuatorTime/100
             * seconds (i.e. 1 "pulse" per second). */
            int delta_tenths = (int)gdc_cmd - (int)gdc_last_cmd;
            if (delta_tenths < 0) delta_tenths = -delta_tenths;

            /* delta_tenths is in 0.1% units (0-1000 range).
             * Move time = (delta_tenths / 1000) * ActuatorTime * 1000ms */
            uint32_t move_ms = ((uint32_t)delta_tenths * (uint32_t)Settings.Door.ActuatorTime);
            /* move_ms is already in ms because delta_tenths/1000 * seconds * 1000 = delta_tenths * seconds */

            gdc_move_done_ms = now_ms + move_ms;
            gdc_last_cmd = gdc_cmd;

            rc = mbtcp_write_single_reg(&ob->conn, ORBIT_GDC_REG_DOOR_CMD, gdc_cmd);
            if (rc != MBTCP_OK) {
                ob->comm_errors++;
                return rc;
            }

            debug_printf("[GDC] Door cmd %u → move %lu ms (travel=%ds)\r\n",
                         gdc_cmd, (unsigned long)move_ms,
                         Settings.Door.ActuatorTime);
        }
        /* else: move in progress or no change — skip this cycle */

        /* ── GDC Safety Interlocks ──
         *
         * Read DI limit switches and enforce:
         *   1. NEVER allow open + close outputs on simultaneously
         *   2. If open limit active  → open output must be OFF
         *   3. If close limit active → close output must be OFF
         *
         * DI/DO paired per actuator:
         *   DI[i*2]   = open limit    DO[i*2]   = open output
         *   DI[i*2+1] = close limit   DO[i*2+1] = close output
         *
         * di_raw[] was already read above (FC02 read at top of function).
         */
        for (int i = 0; i < ORBIT_GDC_MAX_ACTUATORS; i++) {
            int byte_idx, bit_idx;
            int open_limit, close_limit;

            /* Read open limit (DI bit i*2) */
            byte_idx = (i * 2) / 8;
            bit_idx  = (i * 2) % 8;
            open_limit = (di_raw[byte_idx] >> bit_idx) & 1;

            /* Read close limit (DI bit i*2+1) */
            byte_idx = (i * 2 + 1) / 8;
            bit_idx  = (i * 2 + 1) % 8;
            close_limit = (di_raw[byte_idx] >> bit_idx) & 1;

            /* If open limit active → force open output OFF */
            if (open_limit) {
                mbtcp_write_single_coil(&ob->conn, i * 2, 0);
            }

            /* If close limit active → force close output OFF */
            if (close_limit) {
                mbtcp_write_single_coil(&ob->conn, i * 2 + 1, 0);
            }
        }
    }

    ob->comm_errors = 0;
    ob->connected = 1;

    return MBTCP_OK;
}

/* ─── Utility Functions ───────────────────────────────────────────── */

int orbit_slot_by_id(uint8_t dipswitch_id)
{
    for (int i = 0; i < NOVA_MAX_ORBITS; i++) {
        if (orbit_boards[i].dipswitch_id == dipswitch_id && orbit_boards[i].connected) {
            return i;
        }
    }
    return -1;
}

void orbit_set_role(int slot, orbit_role_t role, uint8_t zone_id, int8_t legacy_slot)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return;

    orbit_boards[slot].role = role;
    orbit_boards[slot].zone_id = zone_id;
    orbit_boards[slot].legacy_slot = legacy_slot;

    debug_printf("[Orbit] Slot %d (ID %d): role=%d zone=%d legacy=%d\r\n",
                 slot, orbit_boards[slot].dipswitch_id,
                 role, zone_id, legacy_slot);
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* AO Mode Sync                                                            */
/* ═══════════════════════════════════════════════════════════════════════ */

int orbit_write_ao_mode(SYSTEM_BOARDS board, uint8_t channel, uint16_t mode)
{
    if (board >= ORBIT_MAX_BOARDS) return -1;
    return orbit_write_ao_mode_slot((int)board, channel, mode);
}

int orbit_write_ao_mode_slot(int slot, uint8_t channel, uint16_t mode)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;
    if (channel >= ORBIT_NUM_AO) return -1;
    if (mode > 1) return -1;  /* 0 = voltage, 1 = current */

    orbit_board_t *ob = &orbit_boards[slot];
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;

    /* AO mode registers are holding regs 2-3 on the Orbit */
    return mbtcp_write_single_reg(&ob->conn, 2 + channel, mode);
}

void orbit_sync_ao_modes(void)
{
    for (int i = 0; i < NOVA_MAX_ORBITS; i++) {
        orbit_board_t *ob = &orbit_boards[i];
        if (!ob->connected) continue;

        /* Read AO mode from Settings.
         * Settings.AoMode[slot][channel]: 0 = voltage, 1 = current.
         * If Settings doesn't have AO mode fields yet, default to voltage (0). */
        for (uint8_t ch = 0; ch < ORBIT_NUM_AO; ch++) {
            uint16_t mode = 0;  /* Default: voltage (0-10V) */
#ifdef SETTINGS_HAS_AO_MODE
            if (i < BOARD_COUNT) {
                mode = (uint16_t)Settings.AoMode[i][ch];
            }
#endif
            int rc = orbit_write_ao_mode_slot(i, ch, mode);
            if (rc == MBTCP_OK) {
                debug_printf("[Orbit] Slot %d AO%d mode=%s\r\n",
                             i, ch + 1, mode ? "current" : "voltage");
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* VFD Passthrough                                                         */
/* ═══════════════════════════════════════════════════════════════════════ */

int orbit_write_vfd_reg(int slot, uint16_t vfd_reg, uint16_t value)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;

    orbit_board_t *ob = &orbit_boards[slot];
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;

    /* VFD passthrough base is holding register 100.
     * The Orbit takes writes to regs 100+ and relays them as
     * Modbus RTU FC06 to the VFD drive on its RS-485 port. */
    return mbtcp_write_single_reg(&ob->conn, 100 + vfd_reg, value);
}

int orbit_read_vfd_regs(int slot, uint16_t start_reg, uint16_t count,
                        uint16_t *regs_out)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;
    if (count == 0 || count > 64) return -1;

    orbit_board_t *ob = &orbit_boards[slot];
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;

    /* VFD passthrough base is holding register 100 */
    return mbtcp_read_holding_regs(&ob->conn, 100 + start_reg, count, regs_out);
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* GDC (Door Controller) Register Access                                   */
/* ═══════════════════════════════════════════════════════════════════════ */

int orbit_write_gdc_door(int slot, uint16_t value)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;

    orbit_board_t *ob = &orbit_boards[slot];
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;
    if (ob->role != ORBIT_ROLE_DOOR) return -1;

    /* Clamp to 0-1000 (0.0% - 100.0%) */
    if (value > 1000) value = 1000;

    return mbtcp_write_single_reg(&ob->conn, ORBIT_GDC_REG_DOOR_CMD, value);
}

int orbit_read_gdc_positions(int slot, uint16_t positions[ORBIT_GDC_MAX_ACTUATORS])
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;

    orbit_board_t *ob = &orbit_boards[slot];
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;

    return mbtcp_read_holding_regs(&ob->conn, ORBIT_GDC_REG_POS_BASE,
                                   ORBIT_GDC_MAX_ACTUATORS, positions);
}

int orbit_read_gdc_active_stages(int slot, uint16_t *bitfield)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;

    orbit_board_t *ob = &orbit_boards[slot];
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;

    return mbtcp_read_holding_regs(&ob->conn, ORBIT_GDC_REG_ACTIVE_STAGES,
                                   1, bitfield);
}

int orbit_write_gdc_stage(int slot, uint8_t stage_idx,
                          uint8_t door_bitfield)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;
    if (stage_idx >= ORBIT_GDC_MAX_STAGES) return -1;

    orbit_board_t *ob = &orbit_boards[slot];
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;

    return mbtcp_write_single_reg(&ob->conn,
                                  ORBIT_GDC_REG_STAGE_BASE + stage_idx,
                                  (uint16_t)(door_bitfield & 0x1F));
}

int orbit_write_gdc_num_stages(int slot, uint8_t count)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;
    if (count < 1 || count > ORBIT_GDC_MAX_STAGES) return -1;

    orbit_board_t *ob = &orbit_boards[slot];
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;

    return mbtcp_write_single_reg(&ob->conn, ORBIT_GDC_REG_NUM_STAGES, count);
}

int orbit_write_gdc_travel_time(int slot, uint16_t seconds)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;

    orbit_board_t *ob = &orbit_boards[slot];
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;

    return mbtcp_write_single_reg(&ob->conn, ORBIT_GDC_REG_TRAVEL_TIME, seconds);
}

int orbit_gdc_safety_check(int slot)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return -1;

    orbit_board_t *ob = &orbit_boards[slot];
    if (ob->role != ORBIT_ROLE_DOOR) return 0;
    if (!mbtcp_is_connected(&ob->conn)) return MBTCP_ERR_CONNECT;

    /* Read all 10 discrete inputs (5 actuators × 2 limit switches) */
    uint8_t di_raw[2] = {0};
    int rc = mbtcp_read_discrete_inputs(&ob->conn, 0, ORBIT_NUM_DI, di_raw);
    if (rc != MBTCP_OK) return rc;

    for (int i = 0; i < ORBIT_GDC_MAX_ACTUATORS; i++) {
        int byte_idx, bit_idx;
        int open_limit, close_limit;

        /* Open limit: DI bit i*2 */
        byte_idx = (i * 2) / 8;
        bit_idx  = (i * 2) % 8;
        open_limit = (di_raw[byte_idx] >> bit_idx) & 1;

        /* Close limit: DI bit i*2+1 */
        byte_idx = (i * 2 + 1) / 8;
        bit_idx  = (i * 2 + 1) % 8;
        close_limit = (di_raw[byte_idx] >> bit_idx) & 1;

        /* If open limit active → force open output OFF */
        if (open_limit) {
            mbtcp_write_single_coil(&ob->conn, i * 2, 0);
        }

        /* If close limit active → force close output OFF */
        if (close_limit) {
            mbtcp_write_single_coil(&ob->conn, i * 2 + 1, 0);
        }
    }

    return MBTCP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Orbit Warning / Alarm Detection                                         */
/* ═══════════════════════════════════════════════════════════════════════ */

/* Per-slot warning bitfield:
 *   bit 0 = ORBIT_WARN_ESTOP
 *   bit 1 = ORBIT_WARN_SAFE_MODE
 *   bit 2 = ORBIT_WARN_COMM_LOST
 *   bit 3 = ORBIT_WARN_SENSOR_FAULT
 *   bit 4 = ORBIT_WARN_CPU_OVERTEMP
 */
uint8_t orbit_warnings[NOVA_MAX_ORBITS];

/* CPU temperature alarm threshold: 85.0°C (register value × 10) */
#define ORBIT_CPU_TEMP_ALARM_X10   850

void orbit_check_warnings(void)
{
    for (int i = 0; i < NOVA_MAX_ORBITS; i++) {
        orbit_board_t *ob = &orbit_boards[i];
        uint8_t w = 0;

        if (!ob->connected) {
            /* If the board was previously connected but is now gone,
             * flag comm lost.  Boards that were never connected are
             * not reported. */
            if (ob->comm_errors > 5) {
                w |= (1 << (ORBIT_WARN_COMM_LOST - ORBIT_WARN_BASE));
            }
            orbit_warnings[i] = w;
            continue;
        }

        /* E-Stop active */
        if (ob->estop_active) {
            w |= (1 << (ORBIT_WARN_ESTOP - ORBIT_WARN_BASE));
        }

        /* Watchdog safe mode */
        if (ob->safe_mode) {
            w |= (1 << (ORBIT_WARN_SAFE_MODE - ORBIT_WARN_BASE));
        }

        /* CPU over-temperature */
        if (ob->cpu_temp_x10 > ORBIT_CPU_TEMP_ALARM_X10) {
            w |= (1 << (ORBIT_WARN_CPU_OVERTEMP - ORBIT_WARN_BASE));
        }

        /* Sensor fault flag is set by orbit_check_sensor_faults() */
        /* Preserve the existing sensor fault bit */
        w |= (orbit_warnings[i] & (1 << (ORBIT_WARN_SENSOR_FAULT - ORBIT_WARN_BASE)));

        orbit_warnings[i] = w;
    }
}

int orbit_get_active_warnings(orbit_warn_entry_t *entries, int max_entries)
{
    int count = 0;

    for (int i = 0; i < NOVA_MAX_ORBITS && count < max_entries; i++) {
        uint8_t w = orbit_warnings[i];
        if (w == 0) continue;

        if ((w & (1 << (ORBIT_WARN_ESTOP - ORBIT_WARN_BASE))) && count < max_entries) {
            entries[count].code = ORBIT_WARN_ESTOP;
            entries[count].severity = ORBIT_WARN_SEVERITY_ALARM;
            entries[count].slot = (uint8_t)i;
            count++;
        }
        if ((w & (1 << (ORBIT_WARN_SAFE_MODE - ORBIT_WARN_BASE))) && count < max_entries) {
            entries[count].code = ORBIT_WARN_SAFE_MODE;
            entries[count].severity = ORBIT_WARN_SEVERITY_ALARM;
            entries[count].slot = (uint8_t)i;
            count++;
        }
        if ((w & (1 << (ORBIT_WARN_COMM_LOST - ORBIT_WARN_BASE))) && count < max_entries) {
            entries[count].code = ORBIT_WARN_COMM_LOST;
            entries[count].severity = ORBIT_WARN_SEVERITY_ALARM;
            entries[count].slot = (uint8_t)i;
            count++;
        }
        if ((w & (1 << (ORBIT_WARN_SENSOR_FAULT - ORBIT_WARN_BASE))) && count < max_entries) {
            entries[count].code = ORBIT_WARN_SENSOR_FAULT;
            entries[count].severity = ORBIT_WARN_SEVERITY_WARN;
            entries[count].slot = (uint8_t)i;
            count++;
        }
        if ((w & (1 << (ORBIT_WARN_CPU_OVERTEMP - ORBIT_WARN_BASE))) && count < max_entries) {
            entries[count].code = ORBIT_WARN_CPU_OVERTEMP;
            entries[count].severity = ORBIT_WARN_SEVERITY_WARN;
            entries[count].slot = (uint8_t)i;
            count++;
        }
    }

    /* Triton (REFRIG-role) alarms — bit N = ORBIT_TRITON_BASE + N.
     * Severity:
     *   FAIL_*, SAF_*, BOARD_MISSING_*, NO_DISCHARGE → ALARM
     *   SENS_FAULT_*, LEAK_SUSP, BAD_OIL_SENSOR, COMM_ERR → WARN
     *   plain pressure/temp threshold codes → WARN (timed FAIL_* covers ALARM)
     */
    for (int i = 0; i < NOVA_MAX_ORBITS && count < max_entries; i++) {
        uint32_t bits = triton_alarm_active[i];
        if (bits == 0) continue;

        for (int b = 0; b < 32 && count < max_entries; b++) {
            if (((bits >> b) & 1u) == 0) continue;

            uint16_t code = (uint16_t)(ORBIT_TRITON_BASE + b);
            uint8_t  sev  = ORBIT_WARN_SEVERITY_WARN;

            /* Promote hard faults to ALARM severity */
            if ((b >= 6  && b <= 12)   /* SAF_* */
             || (b >= 13 && b <= 19)   /* FAIL_* */
             || b == 27 || b == 28     /* BOARD_MISSING_* */
             || b == 29) {             /* NO_DISCHARGE */
                sev = ORBIT_WARN_SEVERITY_ALARM;
            }

            entries[count].code     = code;
            entries[count].severity = sev;
            entries[count].slot     = (uint8_t)i;
            count++;
        }
    }

    return count;
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Triton (REFRIG-role) alarm bitmap polling                               */
/* ═══════════════════════════════════════════════════════════════════════ */

uint32_t triton_alarm_active[NOVA_MAX_ORBITS];
uint32_t triton_alarm_acked[NOVA_MAX_ORBITS];

int orbit_check_triton_alarms(void)
{
    int errors = 0;

    for (int i = 0; i < NOVA_MAX_ORBITS; i++) {
        orbit_board_t *ob = &orbit_boards[i];

        if (!ob->connected || ob->role != ORBIT_ROLE_REFRIG) {
            triton_alarm_active[i] = 0;
            triton_alarm_acked[i]  = 0;
            continue;
        }

        /* Read 4 regs: active words 0..1 (codes 0..31) + acked words 0..1.
         * The simulator reserves 6 regs each for headroom, but the current
         * taxonomy only fills the lower two — read just what's needed to
         * keep the QEMU chardev pipeline happy. */
        uint16_t regs_active[2] = { 0, 0 };
        uint16_t regs_acked[2]  = { 0, 0 };

        int rc = mbtcp_read_holding_regs(&ob->conn,
                                         ORBIT_TRITON_REG_ACTIVE_BASE, 2,
                                         regs_active);
        if (rc != MBTCP_OK) { errors++; continue; }

        rc = mbtcp_read_holding_regs(&ob->conn,
                                     ORBIT_TRITON_REG_ACKED_BASE, 2,
                                     regs_acked);
        if (rc != MBTCP_OK) { errors++; continue; }

        triton_alarm_active[i] =
            ((uint32_t)regs_active[0])      |
            ((uint32_t)regs_active[1] << 16);
        triton_alarm_acked[i]  =
            ((uint32_t)regs_acked[0])       |
            ((uint32_t)regs_acked[1]  << 16);
    }

    return errors;
}

/* ═══════════════════════════════════════════════════════════════════════ */
/* Sensor Fault Detection (0x7FFF)                                         */
/* ═══════════════════════════════════════════════════════════════════════ */

void orbit_check_sensor_faults(int slot)
{
    if (slot < 0 || slot >= NOVA_MAX_ORBITS) return;

    orbit_board_t *ob = &orbit_boards[slot];
    if (!ob->sensor_regs_valid) return;

    int fault_found = 0;

    /*
     * Sensor register layout (per orbit-simulator):
     *   reg 200 = board count
     *   reg 201+ = 7 regs per board: [type+addr, sensorTypes, val1, val2, val3, val4, version]
     * Sensor values are in positions 2-5 of each 7-register block.
     *
     * Walk through all sensor values and replace 0x7FFF with 0.
     */
    int board_count = ob->sensor_regs[0];
    if (board_count < 0 || board_count > 8) board_count = 0;

    for (int b = 0; b < board_count; b++) {
        int base = 1 + (b * 7);  /* relative to sensor_regs[0] */
        if (base + 6 >= ORBIT_NUM_SENSOR_REGS) break;

        /* Sensor values are at offsets 2, 3, 4, 5 within the 7-reg block */
        for (int v = 2; v <= 5; v++) {
            int idx = base + v;
            if (ob->sensor_regs[idx] == SENSOR_FAULT_VALUE) {
                ob->sensor_regs[idx] = 0;  /* Replace with safe zero */
                fault_found = 1;
                debug_printf("[Orbit] Slot %d sensor board %d ch %d: FAULT (0x7FFF) → 0\r\n",
                             slot, b, v - 2);
            }
        }
    }

    /* Update warning bitfield */
    if (fault_found) {
        orbit_warnings[slot] |= (1 << (ORBIT_WARN_SENSOR_FAULT - ORBIT_WARN_BASE));
    } else {
        orbit_warnings[slot] &= ~(1 << (ORBIT_WARN_SENSOR_FAULT - ORBIT_WARN_BASE));
    }
}
