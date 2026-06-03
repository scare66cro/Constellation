/**
 * equipmentIds.ts — Authoritative equipment indices for EquipmentCmd.
 *
 * Mirrors the legacy AS2 firmware enum `EQUIPMENT_IO` in
 * `docs/legacy_AS2_reference/Application/SerialShift.h:51-113`.
 * These ordinal values are the wire-protocol contract between the
 * bridge and Nova firmware: the `MSG_EQUIPMENT_CMD` payload (proto
 * field literally named `eq_index`) addresses the LP firmware's
 * `s_data.remote_off.state[eq_index]` directly.  Do NOT renumber.
 *
 * State values for `state[i]` (matches `RemoteOffState` proto enum):
 *   0 = Auto       (controller decides — default)
 *   1 = Off        (forced off)
 *   2 = On/Manual  (forced on, replaces panel switch)
 *   3 = SysStop    (latching emergency stop, set by CMD_SYSTEM_STOP only)
 *
 * NOTE: pre-2026-05-03 this file used the legacy AS2 EQ_REMOTE_OFF
 * 20-entry table (RO_*).  That worked accidentally for FAN (RO_FAN=0
 * == EQ_FAN=0) and broke silently for everything else (e.g. RO_REFRIG=1
 * collided with EQ_DOORS=1).  The Constellation firmware stores remote-
 * off state by EQ index so 1:1 with the EquipState.eq_index it emits.
 */

export const EQ = {
  FAN:             0,   /* EQ_FAN              */
  DOORS:           1,   /* EQ_DOORS — operator Auto/Close/Open via doorBtn;
                         * mirrored into lp->remote_off.state[1] and read by
                         * build_switch_state to gate SW_FRESHAIR_AUTO/MANUAL
                         * (lp_engine_shim.c). Was excluded pre-2026-06-03
                         * under the assumption that the GDC owned doors —
                         * but the Equipment Status row does drive it. */
  REFRIGERATION:   2,   /* EQ_REFRIGERATION    */
  CLIMACELL:       3,   /* EQ_CLIMACELL        */
  HEAT:            4,   /* EQ_HEAT             */
  CAVITY_HEAT:     5,   /* EQ_CAVITY_HEAT      */
  BURNER:          6,   /* EQ_BURNER           */
  HUMIDIFIER1:     7,   /* EQ_HUMID_HEAD1 — head & pump share switch */
  HUMIDIFIER2:     9,   /* EQ_HUMID_HEAD2      */
  HUMIDIFIER3:    11,   /* EQ_HUMID_HEAD3      */
  /* REFRIG_STAGE1..8 = 13..20 — controlled via RefrigDiagCmd, not here */
  /* REFRIG_DEFROST1..2 = 21..22 — same */
  LIGHTS1:        23,   /* EQ_LIGHTS1          */
  LIGHTS2:        24,   /* EQ_LIGHTS2          */
  AUX1:           25,   /* EQ_AUX1             */
  AUX2:           26,
  AUX3:           27,
  AUX4:           28,
  AUX5:           29,
  AUX6:           30,
  AUX7:           31,
  AUX8:           32,
} as const;

/** Backwards-compat alias.  Some bridge code still imports `RO`; keep
 * the export so we can rip it out in a follow-up commit without a
 * thundering-herd compile break.  The values are identical to `EQ`. */
export const RO = EQ;

/** Storage capacity on the firmware side — `s_data.remote_off.state[]`
 * is sized LP_IO_ENTRIES_MAX (64).  Any eq_index ≥ 64 is rejected by
 * `LpSettings_SetRemoteOff` and silently dropped. */
export const NUM_REMOTE_OFF = 64;

export const REMOTE_AUTO    = 0;
export const REMOTE_OFF     = 1;
export const REMOTE_MANUAL  = 2;   /* aka "On" — forced output */
export const REMOTE_SYSSTOP = 3;   /* set only by CMD_SYSTEM_STOP */

/**
 * Map UI button tags → EQ slot.  Mirrors the `UI_EquipPosts[]`
 * table in `StorePostData.c` but expressed in the modern eq_index
 * domain (not the legacy RO_* table).  Tags ending in "Btn" that
 * map to a manual-override slot go here.  Refrig per-stage diag
 * buttons (refr1Btn..refr8Btn, defrost1Btn, defrost2Btn) use
 * `MSG_REFRIG_DIAG_CMD` instead and are NOT in this map.  Lights
 * buttons use a toggle action (see `lightsButtonNextState`) but
 * resolve to the same slot.
 */
export const BUTTON_TO_RO: Record<string, number> = {
  fanBtn:          EQ.FAN,
  doorBtn:         EQ.DOORS,
  refrigBtn:       EQ.REFRIGERATION,
  climacellBtn:    EQ.CLIMACELL,
  heatBtn:         EQ.HEAT,
  cavHeatBtn:      EQ.CAVITY_HEAT,
  burnerBtn:       EQ.BURNER,
  /* cureBtn — Cure has no real EQ slot in the legacy enum.  Use
   * virtual slot 63 in the 64-wide remote_off table; LP firmware
   * mirrors that slot to Settings.RemoteOff[RO_CURE] and gates
   * SW_CURE_AUTO on it.  AUTO/OFF only (no MANUAL — cure mode
   * changes the device UI, manual is meaningless). */
  cureBtn:         63,
  humid1PumpBtn:   EQ.HUMIDIFIER1,
  humid2PumpBtn:   EQ.HUMIDIFIER2,
  humid3PumpBtn:   EQ.HUMIDIFIER3,
  lights1Btn:      EQ.LIGHTS1,
  lights2Btn:      EQ.LIGHTS2,
  aux1Btn:         EQ.AUX1,
  aux2Btn:         EQ.AUX2,
  aux3Btn:         EQ.AUX3,
  aux4Btn:         EQ.AUX4,
  aux5Btn:         EQ.AUX5,
  aux6Btn:         EQ.AUX6,
  aux7Btn:         EQ.AUX7,
  aux8Btn:         EQ.AUX8,
};

/** Lights buttons are toggles in the legacy firmware.  Cycles 0→1→0
 * (Auto ↔ Off).  Manual mode is not exposed for lights. */
export function lightsButtonNextState(currentState: number): number {
  return currentState === REMOTE_OFF ? REMOTE_AUTO : REMOTE_OFF;
}

/** Decode UI POST value to RemoteOff state.  Mirrors `StoreRemoteOff`:
 *   "Off" → 1, "On" → 2, anything else (including "Auto") → 0. */
export function decodeRemoteOffValue(value: string): number {
  if (value === 'Off') return REMOTE_OFF;
  if (value === 'On')  return REMOTE_MANUAL;
  return REMOTE_AUTO;
}
