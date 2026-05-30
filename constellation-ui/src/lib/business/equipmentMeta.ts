// ════════════════════════════════════════════════════════════════════════
// equipmentMeta.ts — single source of truth for the AS2 EquipIo enum
// in modern Constellation TypeScript. The firmware seed table at
// Nova_Firmware/lp_am2434/lp_settings.c::s_io_defaults[] mirrors this
// order; AS2 SerialShift.h::EQUIPMENT_IO is the historical authority.
//
// AS2 design: firmware never owned localized labels — the Lantronix UI
// pushed them in via StoreIoTranslate (StorePostData.c:1242). Modern
// Constellation does the same: firmware emits IoEntry{ index, mode,
// io_type, … } with empty `name`, and the UI renders the label from
// the i18n bundle keyed by `EQ_LABEL_KEYS[index]`. When a renamable
// slot has a user-customized name, firmware sends that string and the
// UI prefers it over the i18n default.
// ════════════════════════════════════════════════════════════════════════

/**
 * AS2 SYSTEM_MODE enum values (Settings.h::SYSTEM_MODE). Firmware
 * lp_settings.c uses these same numeric values for the `mode` field of
 * each IoEntry. The IO Config page filters dropdown options by which
 * value matches the active Settings.SystemMode.
 */
export const SystemModeEnum = Object.freeze({
	NONE:   0,
	POTATO: 1,
	ONION:  2,
	BEE:    3,
	ALL:    4,
} as const);
export type SystemModeValue = typeof SystemModeEnum[keyof typeof SystemModeEnum];

/**
 * AS2 IO_OPTION enum values (SerialShift.h). Firmware writes this into
 * IoEntry.io_type with proto3-force encoding (0=OUTPUT is meaningful).
 */
export const IoOptionEnum = Object.freeze({
	OUTPUT: 0,
	INPUT:  1,
	BOTH:   2,
	SWITCH: 3,
	NONE:   4,
} as const);
export type IoOptionValue = typeof IoOptionEnum[keyof typeof IoOptionEnum];

/**
 * AS2 SystemMode (the user-selected operating profile from BasicSetup,
 * NOT the same as IoEntry.mode above). Values match legacy SM_* in
 * Settings.h: SM_POTATO=0, SM_ONION=1, SM_PECAN=3.
 *
 * For dropdown filtering we ask: "does this entry's mode apply when
 * the system is configured for X?" — see {@link entryAppliesToSystem}.
 */
export const ActiveSystemMode = Object.freeze({
	POTATO: 0,
	ONION:  1,
	BEE:    2,
	PECAN:  3,
} as const);
export type ActiveSystemModeValue = typeof ActiveSystemMode[keyof typeof ActiveSystemMode];

/**
 * Predicate mirroring AS2 UI_SendIoConfig filter logic: an IoEntry is
 * shown for the active system mode iff its `mode` field matches.
 * MODE_ALL applies everywhere; MODE_POTATO applies in POTATO + PECAN
 * (legacy AS2 grouped pecan with potato); MODE_ONION applies only when
 * the system is in onion mode; MODE_BEE applies only in bee mode;
 * MODE_NONE entries (POWER/REDLIGHT/etc.) never appear in the user-
 * facing assignment dropdowns.
 */
export function entryAppliesToSystem(entryMode: number, systemMode: number): boolean {
	if (entryMode === SystemModeEnum.ALL)    return true;
	if (entryMode === SystemModeEnum.NONE)   return false;
	if (entryMode === SystemModeEnum.POTATO) return systemMode === ActiveSystemMode.POTATO || systemMode === ActiveSystemMode.PECAN;
	if (entryMode === SystemModeEnum.ONION)  return systemMode === ActiveSystemMode.ONION;
	if (entryMode === SystemModeEnum.BEE)    return systemMode === ActiveSystemMode.BEE;
	return false;
}

/**
 * Index → i18n key, ordered to match the AS2 EQUIPMENT_IO enum.
 * Indices 0..42 are equipment, 43..58 are switches. When firmware adds
 * a new index, append the corresponding key here AND add the string to
 * `equipment.*` in the locale bundles.
 *
 * Wires to: Nova_Firmware/lp_am2434/lp_settings.c::s_io_defaults[]
 * Authority: docs/legacy_AS2_reference/Application/SerialShift.h:51-113
 */
export const EQ_LABEL_KEYS: readonly string[] = Object.freeze([
	/*  0 EQ_FAN              */ 'equipment.fan-green-light',
	/*  1 EQ_DOORS            */ 'equipment.doors',
	/*  2 EQ_REFRIGERATION    */ 'equipment.refrigeration',
	/*  3 EQ_CLIMACELL        */ 'equipment.climacell',
	/*  4 EQ_HEAT             */ 'equipment.heat',
	/*  5 EQ_CAVITY_HEAT      */ 'equipment.cavity-heat',
	/*  6 EQ_BURNER           */ 'equipment.burner',
	/*  7 EQ_HUMID_HEAD1      */ 'equipment.humidifier-1-head',
	/*  8 EQ_HUMID_PUMP1      */ 'equipment.humidifier-1-pump',
	/*  9 EQ_HUMID_HEAD2      */ 'equipment.humidifier-2-head',
	/* 10 EQ_HUMID_PUMP2      */ 'equipment.humidifier-2-pump',
	/* 11 EQ_HUMID_HEAD3      */ 'equipment.humidifier-3-head',
	/* 12 EQ_HUMID_PUMP3      */ 'equipment.humidifier-3-pump',
	/* 13 EQ_REFRIG_STAGE1    */ 'equipment.refrigeration-stage-1',
	/* 14 EQ_REFRIG_STAGE2    */ 'equipment.refrigeration-stage-2',
	/* 15 EQ_REFRIG_STAGE3    */ 'equipment.refrigeration-stage-3',
	/* 16 EQ_REFRIG_STAGE4    */ 'equipment.refrigeration-stage-4',
	/* 17 EQ_REFRIG_STAGE5    */ 'equipment.refrigeration-stage-5',
	/* 18 EQ_REFRIG_STAGE6    */ 'equipment.refrigeration-stage-6',
	/* 19 EQ_REFRIG_STAGE7    */ 'equipment.refrigeration-stage-7',
	/* 20 EQ_REFRIG_STAGE8    */ 'equipment.refrigeration-stage-8',
	/* 21 EQ_REFRIG_DEFROST1  */ 'equipment.defrost-1',
	/* 22 EQ_REFRIG_DEFROST2  */ 'equipment.defrost-2',
	/* 23 EQ_LIGHTS1          */ 'equipment.bay-lights-1',
	/* 24 EQ_LIGHTS2          */ 'equipment.bay-lights-2',
	/* 25 EQ_AUX1             */ 'equipment.auxiliary-1',
	/* 26 EQ_AUX2             */ 'equipment.auxiliary-2',
	/* 27 EQ_AUX3             */ 'equipment.auxiliary-3',
	/* 28 EQ_AUX4             */ 'equipment.auxiliary-4',
	/* 29 EQ_AUX5             */ 'equipment.auxiliary-5',
	/* 30 EQ_AUX6             */ 'equipment.auxiliary-6',
	/* 31 EQ_AUX7             */ 'equipment.auxiliary-7',
	/* 32 EQ_AUX8             */ 'equipment.auxiliary-8',
	/* 33 EQ_POWER            */ 'equipment.power',
	/* 34 EQ_REMOTE_STANDBY   */ 'equipment.remote-standby',
	/* 35 EQ_REFRIG_STANDBY   */ 'equipment.refrigeration-standby',
	/* 36 EQ_AIR_FLOW         */ 'equipment.air-flow',
	/* 37 EQ_LOW_TEMP         */ '', /* deprecated; superseded by hardware-pinned EQ_AUX_LOW_LIMIT (59) on DI1 */
	/* 38 EQ_REDLIGHT         */ 'equipment.red-light',
	/* 39 EQ_YELLOWLIGHT      */ 'equipment.yellow-light',
	/* 40 EQ_PULSEDOOR_POWER  */ 'equipment.pulse-door-power',
	/* 41 EQ_PULSEDOOR_OPEN   */ 'equipment.pulse-door-open',
	/* 42 EQ_PULSEDOOR_CLOSE  */ 'equipment.pulse-door-close',
	/* 43 SW_START_STOP       */ 'equipment.start-stop',
	/* 44 SW_FAN_AUTO         */ 'equipment.fan-auto',
	/* 45 SW_FAN_MANUAL       */ 'equipment.fan-manual',
	/* 46 SW_FRESHAIR_AUTO    */ 'equipment.fresh-air-auto',
	/* 47 SW_FRESHAIR_MANUAL  */ 'equipment.fresh-air-manual',
	/* 48 SW_CLIMACELL_AUTO   */ 'equipment.climacell-auto',
	/* 49 SW_CLIMACELL_MANUAL */ 'equipment.climacell-manual',
	/* 50 SW_HUMID_AUTO       */ 'equipment.humidifier-auto',
	/* 51 SW_HUMID_MANUAL     */ 'equipment.humidifier-manual',
	/* 52 SW_REFRIG_AUTO      */ 'equipment.refrigeration-auto',
	/* 53 SW_CURE_AUTO        */ 'equipment.cure-auto',
	/* 54 SW_BURNER_AUTO      */ 'equipment.burner-auto',
	/* 55 SW_AUX1_AUTO        */ 'equipment.auxiliary-1-auto',
	/* 56 SW_AUX1_MANUAL      */ 'equipment.auxiliary-1-manual',
	/* 57 SW_AUX2_AUTO        */ 'equipment.auxiliary-2-auto',
	/* 58 SW_AUX2_MANUAL      */ 'equipment.auxiliary-2-manual',
	/* 59 EQ_AUX_LOW_LIMIT    */ 'equipment.aux-low-limit',
	/* 60 EQ_ESTOP            */ 'equipment.e-stop',
] as const);

/**
 * Resolve the user-visible label for an IoEntry. Renamable user-set
 * names from firmware win; otherwise the localized i18n default for
 * that index. Empty key fallback mirrors a fresh slot AS2 would have
 * called "IO N".
 *
 * @param entry IoEntry as decoded from IoDefinition envelope
 * @param translate caller-provided `$t`-equivalent (svelte-i18n)
 */
export function resolveEquipmentLabel(
	entry: { index: number; name: string },
	translate: (key: string) => string,
): string {
	if (entry.name && entry.name.trim().length > 0) return entry.name;
	const key = EQ_LABEL_KEYS[entry.index];
	if (!key) return `IO ${entry.index}`;
	return translate(key);
}
