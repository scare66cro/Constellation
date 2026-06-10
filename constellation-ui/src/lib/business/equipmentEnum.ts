// ════════════════════════════════════════════════════════════════════════
// equipmentEnum.ts — UI-side equipment index enum + input-feedback
// semantics. Mirrors the firmware EQUIPMENT_IO enum (see
// Nova_Firmware/lp_am2434/lp_settings.c::s_io_defaults[] and the
// canonical source docs/legacy_AS2_reference/Application/SerialShift.h).
//
// This file is the small UI-side counterpart of the bridge's
// constellation-ui/server/src/equipmentIds.ts. They MUST stay in sync.
//
// Why a separate UI file: the bridge module also carries server-only
// helpers (BUTTON_TO_RO mapping, light next-state logic) that have no
// place in browser bundles. Keeping just the enum + pure helpers here
// avoids dragging server deps into the SvelteKit bundle.
// ════════════════════════════════════════════════════════════════════════

/**
 * Equipment index — wire-compatible with `EquipState.eqIndex` on the
 * proto stream. Indices 0..42 are equipment, 43..58 are legacy switch
 * slots (now software-only — Constellation has no CPLD panel switches),
 * 59..60 are hardware-pinned safety inputs.
 */
export const EQ = Object.freeze({
	FAN: 0,
	DOORS: 1,
	REFRIGERATION: 2,
	CLIMACELL: 3,
	HEAT: 4,
	CAVITY_HEAT: 5,
	BURNER: 6,
	HUMID_HEAD1: 7,
	HUMID_PUMP1: 8,
	HUMID_HEAD2: 9,
	HUMID_PUMP2: 10,
	HUMID_HEAD3: 11,
	HUMID_PUMP3: 12,
	REFRIG_STAGE1: 13,
	REFRIG_STAGE2: 14,
	REFRIG_STAGE3: 15,
	REFRIG_STAGE4: 16,
	REFRIG_STAGE5: 17,
	REFRIG_STAGE6: 18,
	REFRIG_STAGE7: 19,
	REFRIG_STAGE8: 20,
	REFRIG_DEFROST1: 21,
	REFRIG_DEFROST2: 22,
	LIGHTS1: 23,
	LIGHTS2: 24,
	AUX1: 25,
	AUX2: 26,
	AUX3: 27,
	AUX4: 28,
	AUX5: 29,
	AUX6: 30,
	AUX7: 31,
	AUX8: 32,
	POWER: 33,
	REMOTE_STANDBY: 34,
	REFRIG_STANDBY: 35,
	AIR_FLOW: 36,
	REDLIGHT: 38,
	YELLOWLIGHT: 39,
	PULSEDOOR_POWER: 40,
	PULSEDOOR_OPEN: 41,
	PULSEDOOR_CLOSE: 42,
	AUX_LOW_LIMIT: 59,
	ESTOP: 60,
	/* CURE_VIRTUAL — not a wired EQ slot. The cure tile on Equipment
	 * Control writes/reads remote_off.state[63] only; firmware emits
	 * a synthetic EquipState{eq=63,remote_off} in EquipmentStatus when
	 * SystemMode==Onion so the UI tile reflects persisted state after
	 * reload. AUTO ↔ OFF only — cure mode itself changes the device UI
	 * (cure-mode display, burner schedule), so MANUAL is meaningless. */
	CURE_VIRTUAL: 63,
} as const);
export type EqIndex = typeof EQ[keyof typeof EQ];

/**
 * Mirror of `RemoteOffState` from generated proto — re-exported here
 * so consumers don't need to reach into `generated/ts/agristar/common`.
 */
export const REMOTE = Object.freeze({
	AUTO: 0,
	OFF: 1,
	MANUAL: 2,
	SYSSTOP: 3,
} as const);
export type RemoteOffValue = typeof REMOTE[keyof typeof REMOTE];

// ─────────────────────────────────────────────────────────────────────
// Input-feedback semantics
// ─────────────────────────────────────────────────────────────────────
//
// `EquipState.input_on` arrives on the wire as the **raw DI contact
// state** (true = contact closed, false = contact open) — firmware
// does NOT invert. The semantic meaning of "closed" / "open" depends
// on the equipment type:
//
//   PROVING        — closed = running OK; open = FAIL (no proof)
//                    (most equipment: fan, doors, refrig, humid, heat,
//                    burner, climacell, cavity, refrig stages, defrost)
//   CURRENT_SENSE  — closed = current detected (lights physically on);
//                    open = lights off. Informational; alarm/auto-off
//                    behaviour comes from settings, not the input bit.
//                    (LIGHTS1, LIGHTS2)
//   STANDBY_REQ    — closed = enter standby (system halts and waits);
//                    open = resume normal operation.
//                    (REFRIG_STANDBY, REMOTE_STANDBY)
//   SAFETY_FAIL_OPEN — closed = OK to run; open = immediate trip.
//                      Includes the hardware-pinned ESTOP chain.
//                      (AIR_FLOW, AUX_LOW_LIMIT, ESTOP)
//   SAFETY_FAIL_CLOSE — open = OK to run; closed = power-fail trip
//                       (kills all outputs).  (POWER)
//   AUX_CONFIG     — operator-configurable per-slot from
//                    Level 2 → Failures2; default is "closed = OK,
//                    open = alarm". (AUX1..AUX8)
//   NO_INPUT       — equipment has no DI contact (purely command-driven
//                    or output-only). UI should not show input status.
//                    (REDLIGHT, YELLOWLIGHT, PULSEDOOR_*, software-only
//                    switch slots, etc.)
//
// This category model REPLACES the legacy AS2 hand-rolled CSV byte
// inversions and per-equipment if/else chains in the old getStatus().
// One table here = one consistent UI behaviour everywhere.

export const InputCategory = Object.freeze({
	PROVING:           'proving',
	CURRENT_SENSE:     'currentSense',
	STANDBY_REQ:       'standbyReq',
	SAFETY_FAIL_OPEN:  'safetyFailOpen',
	SAFETY_FAIL_CLOSE: 'safetyFailClose',
	AUX_CONFIG:        'auxConfig',
	NO_INPUT:          'noInput',
} as const);
export type InputCategoryValue = typeof InputCategory[keyof typeof InputCategory];

const INPUT_CATEGORY: Readonly<Record<number, InputCategoryValue>> = Object.freeze({
	[EQ.FAN]:             InputCategory.PROVING,
	[EQ.DOORS]:           InputCategory.PROVING,
	[EQ.REFRIGERATION]:   InputCategory.PROVING,
	[EQ.CLIMACELL]:       InputCategory.PROVING,
	[EQ.HEAT]:            InputCategory.PROVING,
	[EQ.CAVITY_HEAT]:     InputCategory.PROVING,
	[EQ.BURNER]:          InputCategory.PROVING,
	[EQ.HUMID_HEAD1]:     InputCategory.PROVING,
	[EQ.HUMID_PUMP1]:     InputCategory.PROVING,
	[EQ.HUMID_HEAD2]:     InputCategory.PROVING,
	[EQ.HUMID_PUMP2]:     InputCategory.PROVING,
	[EQ.HUMID_HEAD3]:     InputCategory.PROVING,
	[EQ.HUMID_PUMP3]:     InputCategory.PROVING,
	[EQ.REFRIG_STAGE1]:   InputCategory.PROVING,
	[EQ.REFRIG_STAGE2]:   InputCategory.PROVING,
	[EQ.REFRIG_STAGE3]:   InputCategory.PROVING,
	[EQ.REFRIG_STAGE4]:   InputCategory.PROVING,
	[EQ.REFRIG_STAGE5]:   InputCategory.PROVING,
	[EQ.REFRIG_STAGE6]:   InputCategory.PROVING,
	[EQ.REFRIG_STAGE7]:   InputCategory.PROVING,
	[EQ.REFRIG_STAGE8]:   InputCategory.PROVING,
	[EQ.REFRIG_DEFROST1]: InputCategory.PROVING,
	[EQ.REFRIG_DEFROST2]: InputCategory.PROVING,
	[EQ.LIGHTS1]:         InputCategory.CURRENT_SENSE,
	[EQ.LIGHTS2]:         InputCategory.CURRENT_SENSE,
	[EQ.AUX1]:            InputCategory.AUX_CONFIG,
	[EQ.AUX2]:            InputCategory.AUX_CONFIG,
	[EQ.AUX3]:            InputCategory.AUX_CONFIG,
	[EQ.AUX4]:            InputCategory.AUX_CONFIG,
	[EQ.AUX5]:            InputCategory.AUX_CONFIG,
	[EQ.AUX6]:            InputCategory.AUX_CONFIG,
	[EQ.AUX7]:            InputCategory.AUX_CONFIG,
	[EQ.AUX8]:            InputCategory.AUX_CONFIG,
	[EQ.POWER]:           InputCategory.SAFETY_FAIL_CLOSE,
	[EQ.REMOTE_STANDBY]:  InputCategory.STANDBY_REQ,
	[EQ.REFRIG_STANDBY]:  InputCategory.STANDBY_REQ,
	[EQ.AIR_FLOW]:        InputCategory.SAFETY_FAIL_OPEN,
	[EQ.AUX_LOW_LIMIT]:   InputCategory.SAFETY_FAIL_OPEN,
	[EQ.ESTOP]:           InputCategory.SAFETY_FAIL_OPEN,
});

export function inputCategoryFor(eqIndex: number): InputCategoryValue {
	return INPUT_CATEGORY[eqIndex] ?? InputCategory.NO_INPUT;
}

/**
 * Render-ready interpretation of the raw DI bit for a given equipment.
 * Pages should consume `{ healthy, statusKey, color }` directly:
 *
 *   `healthy` — true when the input matches the safe / normal state
 *               for this equipment's category. False means the UI
 *               should reflect a fault, alarm, or standby condition.
 *   `statusKey` — i18n key the page can pass to `$t(...)`.
 *   `color`   — Tailwind class string matching legacy AS2 colors so
 *               the visual treatment carries over for operators.
 *
 * For `NO_INPUT` equipment (lights' command-only siblings, software
 * switches) this returns `healthy:true` so the page falls back to
 * showing remoteOff state without flagging a phantom failure.
 */
export interface InputInterpretation {
	healthy: boolean;
	statusKey: string;
	color: string;
}

const COLOR_GOOD = 'text-green-700 font-bold';
const COLOR_BAD  = 'text-red-500 font-bold';
const COLOR_INFO = 'text-blue-500 font-bold';
const COLOR_NEUTRAL = 'text-black';

export function interpretEquipmentInput(eqIndex: number, inputOn: boolean): InputInterpretation {
	const cat = inputCategoryFor(eqIndex);
	switch (cat) {
		case InputCategory.PROVING:
			// AS2 ground truth (Failures.c:443, SerialShift.c:99-112): the
			// equipment proving DI is an active-high FAULT contact — the field
			// device ASSERTS it (DI high → input_on true) to report a failure;
			// a healthy, proving fan leaves CheckInputs(EQ)==0 (DI low). So
			// healthy = !input_on. (This case was previously inverted, which
			// disagreed with the firmware/injector and showed healthy as red;
			// fixed 2026-06-09 — see memories/repo INDEX.)
			return inputOn
				? { healthy: false, statusKey: 'global.off', color: COLOR_BAD  }
				: { healthy: true,  statusKey: 'global.on',  color: COLOR_GOOD };
		case InputCategory.CURRENT_SENSE:
			return inputOn
				? { healthy: true,  statusKey: 'global.on',  color: COLOR_GOOD }
				: { healthy: true,  statusKey: 'global.off', color: COLOR_NEUTRAL };
		case InputCategory.STANDBY_REQ:
			return inputOn
				? { healthy: false, statusKey: 'global.standby', color: COLOR_INFO }
				: { healthy: true,  statusKey: 'global.on',      color: COLOR_GOOD };
		case InputCategory.SAFETY_FAIL_OPEN:
			return inputOn
				? { healthy: true,  statusKey: 'global.on',  color: COLOR_GOOD }
				: { healthy: false, statusKey: 'global.off', color: COLOR_BAD  };
		case InputCategory.SAFETY_FAIL_CLOSE:
			return inputOn
				? { healthy: false, statusKey: 'global.off', color: COLOR_BAD  }
				: { healthy: true,  statusKey: 'global.on',  color: COLOR_GOOD };
		case InputCategory.AUX_CONFIG:
			// Default behaviour: closed=OK, open=alarm. A future revision
			// can take a per-slot Failures2 setting and either invert this
			// or downgrade the color from BAD to a warning shade.
			return inputOn
				? { healthy: true,  statusKey: 'global.on',  color: COLOR_GOOD }
				: { healthy: false, statusKey: 'global.off', color: COLOR_BAD  };
		case InputCategory.NO_INPUT:
		default:
			return { healthy: true, statusKey: 'global.on', color: COLOR_NEUTRAL };
	}
}

/**
 * Status text + color for the software-switch state (Auto / Off /
 * Manual / SysStop). Consumers that previously read the AS2 panel-
 * switch CSV slot now get the same render shape from this helper,
 * driven directly by `EquipState.remoteOff`.
 *
 * Returns localized i18n keys; the page resolves them through `$t`.
 */
export function interpretRemoteState(remote: RemoteOffValue | number): InputInterpretation {
	switch (remote) {
		case REMOTE.AUTO:
			return { healthy: true,  statusKey: 'global.auto',    color: COLOR_GOOD };
		case REMOTE.MANUAL:
			return { healthy: true,  statusKey: 'global.manual',  color: COLOR_NEUTRAL };
		case REMOTE.OFF:
			return { healthy: false, statusKey: 'global.rem-off', color: COLOR_BAD };
		case REMOTE.SYSSTOP:
			return { healthy: false, statusKey: 'global.off',     color: COLOR_BAD };
		default:
			return { healthy: false, statusKey: 'global.off',     color: COLOR_BAD };
	}
}
