/* nova_serialshift.c
 *
 * Nova-native implementation of legacy Application/SerialShift.c, minus
 * SerialShift_Init (which lives in nova_thread_overrides.c because it
 * carries Nova-specific default equipment mappings and the orbit-
 * emulated v2 CPLD bring-up sequence).
 *
 * MIGRATION STATUS (Phase 7 of legacy → Nova-native):
 *   Replaces docs/legacy_AS2_reference/Application/SerialShift.c
 *   (717 lines → ~250 here).  All behaviour preserved bit-for-bit
 *   except the boot sequence, which has always been Nova-specific.
 *
 * Owns the Nova-side globals:
 *   IoBoard[BOARD_COUNT], PulseDoor, PulseDoorFlag, PulseDoorInit,
 *   PulseDoorMove, PulseDoorPosition.
 *
 * Does NOT provide SerialShift_Init — the Nova orbit-aware version
 * remains in nova_thread_overrides.c and owns the equipment defaults.
 *
 * No hardware pokes — all legacy CPLD shift-register R/W has been
 * relocated to the orbit simulator (Modbus TCP HR 200+).  Functions
 * here operate purely on the in-memory IoBoard[].InputState /
 * OutputState bitmasks, which are updated by the data-exchange
 * thread from orbit telemetry.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "hal.h"
#include "PWM.h"
#include "SerialShift.h"
#include "Settings.h"
#include "States.h"
#include "Timer.h"
#include "UI_Messages.h"
#include "Warnings.h"

/* ─── Public globals (declared extern in SerialShift.h) ───────────── */
EQUIPMENT_IO PulseDoor;
int          PulseDoorFlag = 0;
int          PulseDoorInit = 0;
int          PulseDoorMove = 0;
int          PulseDoorPosition = 0;

BOARD_INFO   IoBoard[BOARD_COUNT];

/* ─────────────────────────────────────────────────────────────────── */

int CheckInputs(unsigned int eqIndex)
{
    if (eqIndex >= EQ_TOTAL_IO) return 0;

    if (Settings.EquipIo[eqIndex].Input == IO_UNDEFINED) {
        /* These three have an "assume present" bypass for legacy
         * hardware topologies that don't wire the proving input. */
        if (eqIndex == EQ_POWER
         || eqIndex == EQ_REFRIG_STANDBY
         || eqIndex == EQ_REMOTE_STANDBY) {
            return 1;
        }
        return 0;
    }

    unsigned int board, input, checkBit;
    if (eqIndex < SW_START_STOP) {
        board = Settings.EquipIo[eqIndex].Input / SS_PORT_ID_MULTIPLIER;
        input = Settings.EquipIo[eqIndex].Input % SS_PORT_ID_MULTIPLIER;
        checkBit = IoBoard[board].Input[input];
    } else {
        board = MAIN;
        input = Settings.EquipIo[eqIndex].Input;
        checkBit = input;   /* switches carry bitmask directly */
    }

    int returnVal = ((IoBoard[board].InputState & checkBit) == checkBit) ? 1 : 0;

    /* Bay-light sense is inverted on the physical board. */
    if (eqIndex == EQ_LIGHTS1 || eqIndex == EQ_LIGHTS2) {
        returnVal ^= 1;
    }
    return returnVal;
}

int CheckOutputs(unsigned int eqIndex)
{
    if (eqIndex >= SW_START_STOP) return 0;
    if (Settings.EquipIo[eqIndex].Output == IO_UNDEFINED) return 0;

    unsigned int board    = Settings.EquipIo[eqIndex].Output / SS_PORT_ID_MULTIPLIER;
    unsigned int output   = Settings.EquipIo[eqIndex].Output % SS_PORT_ID_MULTIPLIER;
    unsigned int checkBit = IoBoard[board].Output[output];

    return ((IoBoard[board].OutputState & checkBit) == checkBit) ? 1 : 0;
}

int FanOn(void)
{
    if ((CheckInputs(SW_FAN_AUTO) && CheckOutputs(EQ_FAN))
        || CheckInputs(SW_FAN_MANUAL)) {
        return 1;
    }
    return 0;
}

void GetAvailableIo(char *message)
{
    int i;
    char str[20];

    strcpy(message, "AvailableIo=");

    for (i = 0; i < BOARD_COUNT; i++) {
        if (IoBoard[i].Version != 0x0F) {
            snprintf(str, sizeof(str), "%s:%d:%d:%d,",
                     IoBoard[i].Name,
                     IoBoard[i].NumOutputs,
                     IoBoard[i].NumInputs,
                     IoBoard[i].NumPwms);
        } else {
            snprintf(str, sizeof(str), "%s:%d:%d:%d,", "none", 0, 0, 0);
        }
        strncat(message, str, MSG_TX_BUFFER_SIZE - strlen(message) - 1);
    }
}

void GetEquipStatus(char *status, int len)
{
    memset(status, 0, len);

    if (CheckInputs(SW_FAN_AUTO))   status[0] = 1;
    if (CheckInputs(SW_FAN_MANUAL)) status[0] = 2;
    if (CheckInputs(EQ_FAN))        status[1] = 1;
    if (CheckOutputs(EQ_FAN))       status[2] = 1;

    if (CheckInputs(SW_CLIMACELL_AUTO))        status[3] = 1;
    if (Settings.SystemMode == SM_POTATO) {
        if (CheckInputs(SW_CLIMACELL_MANUAL))  status[3] = 2;
    }
    if (CheckInputs(EQ_CLIMACELL))  status[4] = 1;
    if (CheckOutputs(EQ_CLIMACELL)) status[5] = 1;

    if (CheckInputs(EQ_BURNER))  status[6] = 1;
    if (CheckOutputs(EQ_BURNER)) status[7] = 1;

    if (CheckInputs(SW_HUMID_AUTO)) status[8] = 1;
    if (Settings.SystemMode == SM_POTATO) {
        if (CheckInputs(SW_HUMID_MANUAL)) status[8] = 2;
    }

    if (CheckInputs(EQ_HUMID_HEAD1))  status[9] = 1;
    if (CheckOutputs(EQ_HUMID_HEAD1)) status[10] = 1;
    if (CheckOutputs(EQ_HUMID_PUMP1)) status[11] = 1;
    if (CheckInputs(EQ_HUMID_HEAD2))  status[12] = 1;
    if (CheckOutputs(EQ_HUMID_HEAD2)) status[13] = 1;
    if (CheckOutputs(EQ_HUMID_PUMP2)) status[14] = 1;

    if (CheckInputs(SW_REFRIG_AUTO))    status[15] = 1;
    if (CheckInputs(EQ_REFRIGERATION))  status[16] = 1;
    if (CheckOutputs(EQ_REFRIG_STAGE1)) status[17] = 1;
    if (CheckOutputs(EQ_REFRIG_STAGE2)) status[18] = 1;
    if (CheckOutputs(EQ_REFRIG_STAGE3)) status[19] = 1;
    if (CheckOutputs(EQ_REFRIG_STAGE4)) status[20] = 1;
    if (CheckOutputs(EQ_REFRIG_STAGE5)) status[21] = 1;
    if (CheckOutputs(EQ_REFRIG_STAGE6)) status[22] = 1;
    if (CheckOutputs(EQ_REFRIG_STAGE7)) status[23] = 1;
    if (CheckOutputs(EQ_REFRIG_STAGE8)) status[24] = 1;

    if (CheckOutputs(EQ_REFRIG_DEFROST1)) status[25] = 1;
    if (CheckOutputs(EQ_REFRIG_DEFROST2)) status[26] = 1;

    if (CheckInputs(EQ_HEAT))  status[27] = 1;
    if (CheckOutputs(EQ_HEAT)) status[28] = 1;

    if (CheckInputs(SW_FRESHAIR_AUTO))   status[29] = 1;
    if (CheckInputs(SW_FRESHAIR_MANUAL)) status[29] = 2;
    if (PulseDoorMove == 0) {
        status[30] = 0;
    } else {
        status[30] = (PulseDoor == EQ_PULSEDOOR_CLOSE) ? 1 : 2;
    }

    if (CheckInputs(EQ_CAVITY_HEAT))  status[31] = 1;
    if (CheckOutputs(EQ_CAVITY_HEAT)) status[32] = 1;

    /* Green light derived from start+fan state */
    if (CheckInputs(SW_START_STOP)
        && ((CheckInputs(SW_FAN_AUTO) && CheckOutputs(EQ_FAN))
            || CheckInputs(SW_FAN_MANUAL))) {
        status[33] = 1;
    }

    if (LightStatus.Yellow == LT_ON)    status[34] = 1;
    if (LightStatus.Yellow == LT_BLINK) status[34] = 2;
    if (LightStatus.Red    == LT_ON)    status[35] = 1;
    if (LightStatus.Red    == LT_BLINK) status[35] = 2;

    /* Bay lights — inverted input sense */
    if (!CheckInputs(EQ_LIGHTS1)) status[36] = 1;
    if (CheckOutputs(EQ_LIGHTS1)) status[37] = 1;
    if (!CheckInputs(EQ_LIGHTS2)) status[38] = 1;
    if (CheckOutputs(EQ_LIGHTS2)) status[39] = 1;

    /* Aux 1-8 */
    if (CheckInputs(EQ_AUX1))  status[40] = 1;
    if (CheckOutputs(EQ_AUX1)) status[41] = 1;
    if (CheckInputs(EQ_AUX2))  status[42] = 1;
    if (CheckOutputs(EQ_AUX2)) status[43] = 1;
    if (CheckInputs(EQ_AUX3))  status[44] = 1;
    if (CheckOutputs(EQ_AUX3)) status[45] = 1;
    if (CheckInputs(EQ_AUX4))  status[46] = 1;
    if (CheckOutputs(EQ_AUX4)) status[47] = 1;
    if (CheckInputs(EQ_AUX5))  status[48] = 1;
    if (CheckOutputs(EQ_AUX5)) status[49] = 1;
    if (CheckInputs(EQ_AUX6))  status[50] = 1;
    if (CheckOutputs(EQ_AUX6)) status[51] = 1;
    if (CheckInputs(EQ_AUX7))  status[52] = 1;
    if (CheckOutputs(EQ_AUX7)) status[53] = 1;
    if (CheckInputs(EQ_AUX8))  status[54] = 1;
    if (CheckOutputs(EQ_AUX8)) status[55] = 1;

    /* Third humidifier */
    if (CheckInputs(EQ_HUMID_HEAD3))  status[56] = 1;
    if (CheckOutputs(EQ_HUMID_HEAD3)) status[57] = 1;
    if (CheckOutputs(EQ_HUMID_PUMP3)) status[58] = 1;

    /* Refrig stage proving inputs */
    int i;
    for (i = 0; i < NUM_REFRIG_STAGES; i++) {
        status[59 + i] = CheckInputs((EQUIPMENT_IO)(EQ_REFRIG_STAGE1 + i));
    }
    for (i = 0; i < NUM_DEFROST_STAGES; i++) {
        status[67 + i] = CheckInputs((EQUIPMENT_IO)(EQ_REFRIG_DEFROST1 + i));
    }
}

void SetIoConfig(EQUIPMENT_IO eqIndex, char ioType, unsigned int ioPort)
{
    if (eqIndex >= SW_START_STOP) return;

    if (ioType == 'i') {
        Settings.EquipIo[eqIndex].Input = ioPort;
        return;
    }

    if (ioType == 'o') {
        int outputOn = 0;
        if (Settings.EquipIo[eqIndex].Output != ioPort) {
            outputOn = CheckOutputs(eqIndex);
            OutputOff(eqIndex);
        }

        Settings.EquipIo[eqIndex].Output = ioPort;
        Settings.EquipIo[eqIndex].Enabled = (ioPort == IO_UNDEFINED) ? 0 : 1;

        if (ioPort == IO_UNDEFINED) {
            OutputOff(eqIndex);
        } else if (outputOn) {
            OutputOn(eqIndex);
        }
    }
}

void OutputOff(EQUIPMENT_IO eqIndex)
{
    if (eqIndex >= SW_START_STOP) return;
    if (Settings.EquipIo[eqIndex].Output == IO_UNDEFINED) return;

    unsigned int board    = Settings.EquipIo[eqIndex].Output / SS_PORT_ID_MULTIPLIER;
    unsigned int output   = Settings.EquipIo[eqIndex].Output % SS_PORT_ID_MULTIPLIER;
    unsigned int checkBit = IoBoard[board].Output[output];

    IoBoard[board].OutputState &= ~checkBit;
}

void OutputOn(EQUIPMENT_IO eqIndex)
{
    /* Missing expansion board → warn */
    if (Settings.EquipIo[eqIndex].Enabled
        && Settings.EquipIo[eqIndex].Output == IO_UNDEFINED) {
        WarningsSet(WARN_SYSCONFIG_EQ, FM_ALARM, NA, eqIndex);
    }

    if (eqIndex >= SW_START_STOP) return;
    if (Settings.EquipIo[eqIndex].Output == IO_UNDEFINED) return;

    unsigned int board    = Settings.EquipIo[eqIndex].Output / SS_PORT_ID_MULTIPLIER;
    unsigned int output   = Settings.EquipIo[eqIndex].Output % SS_PORT_ID_MULTIPLIER;
    unsigned int checkBit = IoBoard[board].Output[output];

    IoBoard[board].OutputState |= checkBit;
}

int ClearBoardIo(SYSTEM_BOARDS board)
{
    int ioCleared = 0;
    int i, j;

    for (i = 0; i < SW_START_STOP; i++) {
        if (Settings.EquipIo[i].Input  >= (board * SS_PORT_ID_MULTIPLIER)
         && Settings.EquipIo[i].Input  <= (board * SS_PORT_ID_MULTIPLIER) + SS_EX_IO_INPUTS) {
            Settings.EquipIo[i].Input = IO_UNDEFINED;
            ioCleared++;
        }
        if (Settings.EquipIo[i].Output >= (board * SS_PORT_ID_MULTIPLIER)
         && Settings.EquipIo[i].Output <= (board * SS_PORT_ID_MULTIPLIER) + SS_EX_IO_OUTPUTS) {
            Settings.EquipIo[i].Output = IO_UNDEFINED;
            ioCleared++;
        }
    }

    for (i = 0; i < (int)IoBoard[board].NumPwms; i++) {
        for (j = 0; j < PWM_TOTAL_EQ; j++) {
            if (Settings.PWM[j].Channel == i + (board * 2)) {
                Settings.PWM[j].Channel = PWM_UNDEFINED;
                ioCleared++;
            }
        }
    }

    return ioCleared;
}

/*** End of file ***/
