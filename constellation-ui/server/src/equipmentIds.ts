/**
 * equipmentIds.ts — Authoritative RemoteOff equipment indices.
 *
 * Mirrors the legacy AS2 firmware enum `EQ_REMOTE_OFF` in
 * `Mini_IO/Application/SettingsTypes.h`.  These ordinal values are the
 * wire-protocol contract between the bridge and Nova firmware: the
 * `MSG_EQUIPMENT_CMD` payload addresses `Settings.RemoteOff[eq_index]`
 * directly, and `UI_EquipPosts[]` in the legacy code binds each button
 * tag to the matching slot.  Do NOT renumber.
 *
 * State values for `RemoteOff[i]` (matches `StoreRemoteOff` in legacy):
 *   0 = Auto       (controller decides — default)
 *   1 = Off        (forced off)
 *   2 = On/Manual  (forced on, replaces panel switch)
 *   3 = SysStop    (latching emergency stop, set by CMD_SYSTEM_STOP only)
 */

export const RO = {
  FAN:           0,
  REFRIGERATION: 1,
  CLIMACELL:     2,
  HEAT:          3,
  CAVITY_HEAT:   4,
  BURNER:        5,
  CURE:          6,
  HUMIDIFIER1:   7,
  HUMIDIFIER2:   8,
  HUMIDIFIER3:   9,
  LIGHTS1:      10,
  LIGHTS2:      11,
  AUX1:         12,
  AUX2:         13,
  AUX3:         14,
  AUX4:         15,
  AUX5:         16,
  AUX6:         17,
  AUX7:         18,
  AUX8:         19,
} as const;

export const NUM_REMOTE_OFF = 20;

export const REMOTE_AUTO    = 0;
export const REMOTE_OFF     = 1;
export const REMOTE_MANUAL  = 2;   /* aka "On" — forced output */
export const REMOTE_SYSSTOP = 3;   /* set only by CMD_SYSTEM_STOP */

/**
 * Map UI button tags → RemoteOff slot.  Mirrors the `UI_EquipPosts[]`
 * table in `StorePostData.c`.  Tags ending in "Btn" that map to
 * `StoreRemoteOff` go here.  Refrig per-stage diag buttons
 * (refr1Btn..refr8Btn, defrost1Btn, defrost2Btn) use `MSG_REFRIG_DIAG_CMD`
 * instead and are NOT in this map.  Light buttons use a toggle action
 * (see `lightsButtonNextState`) but resolve to the same RO slot.
 */
export const BUTTON_TO_RO: Record<string, number> = {
  fanBtn:          RO.FAN,
  refrigBtn:       RO.REFRIGERATION,
  climacellBtn:    RO.CLIMACELL,
  heatBtn:         RO.HEAT,
  cavHeatBtn:      RO.CAVITY_HEAT,
  burnerBtn:       RO.BURNER,
  cureBtn:         RO.CURE,
  humid1PumpBtn:   RO.HUMIDIFIER1,
  humid2PumpBtn:   RO.HUMIDIFIER2,
  humid3PumpBtn:   RO.HUMIDIFIER3,
  lights1Btn:      RO.LIGHTS1,
  lights2Btn:      RO.LIGHTS2,
  aux1Btn:         RO.AUX1,
  aux2Btn:         RO.AUX2,
  aux3Btn:         RO.AUX3,
  aux4Btn:         RO.AUX4,
  aux5Btn:         RO.AUX5,
  aux6Btn:         RO.AUX6,
  aux7Btn:         RO.AUX7,
  aux8Btn:         RO.AUX8,
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
