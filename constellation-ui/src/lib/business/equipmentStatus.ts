// ════════════════════════════════════════════════════════════════════════
// equipmentStatus.ts — proto-direct equipment row builder
//
// History: this file used to read a 102-element flat CSV array
// (`Equipment.eqStatus[N]`) at hand-positioned legacy AS2 byte offsets
// (`eqStatus[37]` for fan remote, `eqStatus[79]` for refrig stage input,
// etc.). Those offsets only matched the AS2 wire format coincidentally —
// the proto bridge flattened typed `EquipState` items into uniform
// 3-tuples (outputOn/remoteOff/alarm), so most positional reads were
// silently incorrect.
//
// New design: pages bind directly to typed `EquipState` items via
// `Equipment.eqByIdx(EQ.X)`. Input semantics (closed=good vs
// open=good vs standby) come from the shared `interpretEquipmentInput`
// helper in `equipmentEnum.ts`. Software-switch state (Auto/Off/Manual)
// comes from `EquipState.remoteOff` directly — no CPLD panel switches
// in Constellation, so no synthesized switch byte either.
//
// Pages still receive the same render-shape they always did
// (`equipmentStatus`, `panelSwitchStatus`, `outputColor`, …) so the
// EquipmentRow / RefrigerationRow / HumidifierRow / DoorDiagRow
// components do not change.
// ════════════════════════════════════════════════════════════════════════

import { get } from 'svelte/store';
import { t } from 'svelte-i18n';
import type { IoEntry } from '$lib/business/ioConfig';
import type { EquipState } from '../../../../generated/ts/agristar/equipment.js';
import {
	EQ,
	REMOTE,
	interpretEquipmentInput,
	interpretRemoteState,
} from './equipmentEnum.js';

/**
 * Composite payload consumed by the equipment page and any other
 * surface that needs an "everything-equipment" snapshot. Built by
 * `equipmentComposite` in `protoStores.ts`.
 *
 *   eqItems     — typed EquipState array, one per firmware-emitted item
 *   eqByIdx     — accessor: returns the EquipState for an EQ enum value
 *                 (or undefined if firmware didn't emit one for that slot)
 *   pwmConfig   — legacy `eqIdx:enabled:portId:duty` strings (still CSV
 *                 because the PWM page hasn't been migrated yet)
 *   outputConfig — eq-indexed string array; '-1' when unassigned. Pages
 *                  test `outputConfig[eqIdx] !== '-1'` to gate row render.
 *   ioNames     — IoEntry[] for label resolution and renamable lookup
 *   auxSwitches — legacy CPLD aux switch state (carried for layout
 *                 decisions only; software switches replace functional use)
 *   miscData    — preserved 14-element legacy misc CSV (cavity / defrost)
 *   systemMode  — basicSetup.systemMode as string
 */
export type Equipment = {
	eqItems: EquipState[];
	eqByIdx: (idx: number) => EquipState | undefined;
	pwmConfig: string[];
	outputConfig: string[];
	ioNames: IoEntry[];
	auxSwitches: string[];
	miscData: string[];
	systemMode: string;
};

export function getOutputColor(on: boolean): string {
	return on ? 'text-green-700 font-bold' : 'text-red-500 font-bold';
}

/**
 * Whether an equipment row should render. For real equipment with an
 * IoEntry, the cell exists when its index is mapped to a wired output
 * (outConfig[idx] !== '-1'). Aggregate rows (cure, refrig master, etc.)
 * pass `undefined` and always render.
 */
export function exists(io: IoEntry | undefined, outConfig: string[]): boolean {
	if (!io) return true;
	return outConfig[io.index] !== '-1';
}

/**
 * Pick the user-renamed label when the firmware marks the entry as
 * renamable AND the user has actually changed it. Otherwise fall back
 * to the i18n default for the equipment row.
 */
export function renamedAs(io: IoEntry | undefined, defaultName: string): string {
	if (!io) return defaultName;
	return io.renamable && io.name && io.name !== defaultName ? io.name : defaultName;
}

/* ───────────────────────────────────────────────────────────────────── */
/* Internal helpers                                                       */
/* ───────────────────────────────────────────────────────────────────── */

/** Translate an i18n key through the global svelte-i18n store. */
function tr(key: string): string {
	const $t = get(t);
	return $t(key);
}

/** Render-shape returned by every `getEquipment` case. Matches the
 * props historically expected by the EquipmentRow / RefrigerationRow /
 * HumidifierRow / DoorDiagRow components. */
type RowShape = {
	exists: boolean;
	name: string;
	equipmentName: string;
	equipmentStatus: string | undefined;
	panelSwitchStatus: string | undefined;
	equipOn: boolean;
	diagOn?: boolean;
	remoteStatus?: string;
	remSwitchName: string;
	outputColor: string;
	statusColor: string | undefined;
	panelSwitchColor: string | undefined;
	edit: boolean;
	target?: string;
	/** Hides the MANUAL option in EquipmentRow's mode dropdown.
	 * Used by 2-way switches (currently only cure). */
	allowManual?: boolean;
	/** Per-row override of the OFF / MANUAL dropdown labels. Doors
	 * use "Close" / "Open" instead of "Off" / "Manual" — same wire
	 * values, more intuitive for an actuator. */
	offLabel?: string;
	manualLabel?: string;
	/** Bay-light boolean status (LightsRow only): TRUE = current-sense
	 * relay indicates lights are physically on (`EquipState.inputOn`).
	 * Drives the colored On/Off label. The Toggle button under it
	 * posts `lightsXBtn=Toggle` independently of this — the wire is
	 * a toggle command, the status reads the actual hardware. */
	statusOn?: boolean;
};

/** Standard equipment row builder. Reads input semantics from the
 * shared category table and software-switch state from `remoteOff`. */
function buildRow(
	equipment: Equipment,
	rowName: string,
	defaultLabel: string,
	btn: string,
	io: IoEntry | undefined,
	state: EquipState | undefined,
	edit: boolean,
): RowShape {
	const remote = state?.remoteOff ?? REMOTE.AUTO;
	const inputOn = state?.inputOn ?? false;
	const outputOn = state?.outputOn ?? false;
	const eqIdx = state?.eqIndex ?? io?.index ?? -1;

	const sw = interpretRemoteState(remote);
	const inSt = interpretEquipmentInput(eqIdx, inputOn);

	return {
		exists: exists(io, equipment.outputConfig),
		name: rowName,
		equipmentName: renamedAs(io, defaultLabel),
		equipmentStatus: tr(inSt.statusKey),
		panelSwitchStatus: tr(sw.statusKey),
		equipOn: remote !== REMOTE.OFF,
		remoteStatus: String(remote),
		remSwitchName: btn,
		outputColor: getOutputColor(outputOn),
		statusColor: inSt.color,
		panelSwitchColor: sw.color,
		edit,
	};
}

/** Refrigeration-stage / master row. Adds the `diagOn` boolean used by
 * RefrigerationRow to render the diagnostic indicator (alarm severity
 * critical = stage in diag mode). */
function buildRefrigRow(
	equipment: Equipment,
	rowName: string,
	defaultLabel: string,
	btn: string,
	io: IoEntry | undefined,
	state: EquipState | undefined,
	masterRemote: number,
	edit: boolean,
): RowShape {
	// Refrig stages display the master refrigeration's remoteOff in the
	// switch column (per AS2 convention) — individual stage cannot be
	// turned off from the UI; user toggles the master.
	const sw = interpretRemoteState(masterRemote);
	const inputOn = state?.inputOn ?? false;
	const outputOn = state?.outputOn ?? false;
	const alarm = state?.alarm ?? 0;
	const eqIdx = state?.eqIndex ?? io?.index ?? -1;
	const diagOn = alarm === 2;

	const inSt = interpretEquipmentInput(eqIdx, inputOn);
	// Diagnostic mode wins the status display.
	const status = diagOn
		? { statusKey: 'global.diag-on', color: 'text-blue-500' }
		: { statusKey: inSt.statusKey, color: inSt.color };

	return {
		exists: exists(io, equipment.outputConfig),
		name: rowName,
		equipmentName: renamedAs(io, defaultLabel),
		equipmentStatus: tr(status.statusKey),
		panelSwitchStatus: tr(sw.statusKey),
		equipOn: masterRemote !== REMOTE.OFF,
		diagOn,
		remoteStatus: String(masterRemote),
		remSwitchName: btn,
		outputColor: getOutputColor(outputOn),
		statusColor: status.color,
		panelSwitchColor: sw.color,
		edit,
	};
}

/** Door diagnostic row. Door % open comes from
 * `SystemStatus.pwmDoorsPct` (typed proto field 20) — same source as the
 * regular door status row. The legacy `main[15]` slot was filled from
 * `SystemStatus.coolOutput` which is the ACTIVE mode's output (refrig %
 * when in ST_REFRIG, doors % when in ST_COOLING) — that mislabelled
 * 100% in the doors-diag row whenever the system escalated to refrig
 * even though the damper was actually closed at PWM_MIN. The diag
 * state itself still lives in the door equipment alarm. */
function buildDoorDiagRow(
	equipment: Equipment,
	rowName: string,
	defaultLabel: string,
	btn: string,
	io: IoEntry | undefined,
	state: EquipState | undefined,
	doorPct: number | undefined,
	edit: boolean,
	target: string,
): RowShape {
	const remote = state?.remoteOff ?? REMOTE.AUTO;
	const outputOn = state?.outputOn ?? false;
	const alarm = state?.alarm ?? 0;
	const diagOn = alarm === 2;

	const sw = interpretRemoteState(remote);
	// Diag mode shows "open"/"close" depending on the alarm code; if the
	// door equipment is healthy the row falls back to the % open string.
	let statusKey = 'global.off';
	let color = 'text-red-500 font-bold';
	if (diagOn) {
		statusKey = 'global.open';
		color = 'text-blue-500 font-bold';
	} else if (alarm === 1) {
		statusKey = 'global.close';
		color = 'text-red-700 font-bold';
	} else if (alarm === 0) {
		statusKey = 'global.diag-off';
		color = 'text-black font-bold';
	}

	return {
		exists: exists(io, equipment.outputConfig),
		name: rowName,
		equipmentName: renamedAs(io, defaultLabel),
		equipmentStatus: doorPct !== undefined ? `${doorPct}%` : tr(statusKey),
		panelSwitchStatus: tr(sw.statusKey),
		equipOn: remote !== REMOTE.OFF,
		diagOn,
		remSwitchName: btn,
		outputColor: getOutputColor(outputOn),
		statusColor: color,
		panelSwitchColor: sw.color,
		edit,
		target,
	};
}

/** Bay-light row. 2026-06-03 consolidation: the dedicated Level 1
 * Bay Lights page is gone; its Toggle button + on/off status pattern
 * lands here, replacing the AUTO/OFF/MANUAL dropdown that EquipmentRow
 * builds for everything else. Bridge dispatches `lightsXBtn=Toggle`
 * via `lightsButtonNextState` anyway (toggle is the only valid action
 * for lights), so the dropdown was misleading. Renaming flows
 * through IO Config now that EQ_LIGHTS1/2 are flagged renamable in
 * lp_settings.c (firmware 0.A.228). */
function buildLightsRow(
	equipment: Equipment,
	rowName: string,
	defaultLabel: string,
	btn: string,
	io: IoEntry | undefined,
	state: EquipState | undefined,
	edit: boolean,
): RowShape {
	const inputOn = state?.inputOn ?? false;
	const outputOn = state?.outputOn ?? false;
	return {
		exists: exists(io, equipment.outputConfig),
		name: rowName,
		equipmentName: renamedAs(io, defaultLabel),
		equipmentStatus: undefined,
		panelSwitchStatus: undefined,
		equipOn: inputOn,
		remSwitchName: btn,
		outputColor: getOutputColor(outputOn),
		statusColor: inputOn
			? 'text-green-700 font-bold'
			: 'text-red-500 font-bold',
		panelSwitchColor: undefined,
		edit,
		statusOn: inputOn,
	};
}

/* ───────────────────────────────────────────────────────────────────── */
/* Public entry point                                                     */
/* ───────────────────────────────────────────────────────────────────── */

/**
 * Build the render-shape for a single equipment row. The page calls
 * this once per row name, both at first paint and on every
 * `equipmentComposite` update.
 *
 * `eq` is the row identifier (matches the legacy AS2 string keys
 * 'fan', 'climacell', 'refrig', 'refrig1'..'refrig8', 'humid1', etc.)
 * — kept as strings so the equipment page's existing layout-decision
 * code (`addEquipment(equipment, 'climacell', counts)`) does not need
 * to change.
 */
export function getEquipment(
	equipment: Equipment,
	eq: string,
	edit: boolean,
	main: string[] | undefined,
	doorPct?: number,
): RowShape | undefined {
	const e = equipment.eqByIdx;
	const refrigMaster = e(EQ.REFRIGERATION)?.remoteOff ?? REMOTE.AUTO;

	switch (eq) {
		case 'fan':
			return buildRow(equipment, eq, tr('level2.failures1.fan'), 'fanBtn',
				undefined, e(EQ.FAN), edit);
		case 'climacell':
			return buildRow(equipment, eq, tr('equipment.climacell'), 'climacellBtn',
				equipment.ioNames[EQ.CLIMACELL], e(EQ.CLIMACELL), edit);
		case 'heat':
			return buildRow(equipment, eq, tr('equipment.heat'), 'heatBtn',
				equipment.ioNames[EQ.HEAT], e(EQ.HEAT), edit);
		case 'cavity':
			return buildRow(equipment, eq, tr('equipment.cavity-heat'), 'cavHeatBtn',
				equipment.ioNames[EQ.CAVITY_HEAT], e(EQ.CAVITY_HEAT), edit);
		case 'pile':
			return buildRow(equipment, eq, tr('equipment.cavity-heat'), 'cavHeatBtn',
				undefined, e(EQ.CAVITY_HEAT), edit);
		case 'burner':
			return buildRow(equipment, eq, tr('equipment.burner'), 'burnerBtn',
				equipment.ioNames[EQ.BURNER], e(EQ.BURNER), edit);

		case 'aux1Switch':
			return buildRow(equipment, eq, tr('equipment.auxiliary-1'), 'aux1Btn',
				equipment.ioNames[EQ.AUX1], e(EQ.AUX1), edit);
		case 'aux2Switch':
			return buildRow(equipment, eq, tr('equipment.auxiliary-2'), 'aux2Btn',
				equipment.ioNames[EQ.AUX2], e(EQ.AUX2), edit);
		case 'aux3Switch':
			return buildRow(equipment, eq, tr('equipment.auxiliary-3'), 'aux3Btn',
				equipment.ioNames[EQ.AUX3], e(EQ.AUX3), edit);
		case 'aux4Switch':
			return buildRow(equipment, eq, tr('equipment.auxiliary-4'), 'aux4Btn',
				equipment.ioNames[EQ.AUX4], e(EQ.AUX4), edit);
		case 'aux5Switch':
			return buildRow(equipment, eq, tr('equipment.auxiliary-5'), 'aux5Btn',
				equipment.ioNames[EQ.AUX5], e(EQ.AUX5), edit);
		case 'aux6Switch':
			return buildRow(equipment, eq, tr('equipment.auxiliary-6'), 'aux6Btn',
				equipment.ioNames[EQ.AUX6], e(EQ.AUX6), edit);
		case 'aux7Switch':
			return buildRow(equipment, eq, tr('equipment.auxiliary-7'), 'aux7Btn',
				equipment.ioNames[EQ.AUX7], e(EQ.AUX7), edit);
		case 'aux8Switch':
			return buildRow(equipment, eq, tr('equipment.auxiliary-8'), 'aux8Btn',
				equipment.ioNames[EQ.AUX8], e(EQ.AUX8), edit);

		case 'refrig': {
			// Aggregate refrigeration row. Output color tracks "any stage
			// running"; diag tracks "any stage in critical alarm".
			let anyOn = false;
			let anyDiag = false;
			for (let i = 0; i < 8; i++) {
				const st = e(EQ.REFRIG_STAGE1 + i);
				if (!st) continue;
				if (st.outputOn) anyOn = true;
				if (st.alarm === 2) anyDiag = true;
			}
			for (let i = 0; i < 2; i++) {
				const st = e(EQ.REFRIG_DEFROST1 + i);
				if (st?.alarm === 2) anyDiag = true;
			}
			const sw = interpretRemoteState(refrigMaster);
			const status = anyDiag
				? { statusKey: 'global.diag-on', color: 'text-blue-500' }
				: anyOn
					? { statusKey: 'global.on', color: 'text-green-700 font-bold' }
					: { statusKey: 'global.off', color: 'text-red-500 font-bold' };
			return {
				exists: true,
				name: eq,
				equipmentName: tr('equipment.refrigeration'),
				equipmentStatus: tr(status.statusKey),
				panelSwitchStatus: tr(sw.statusKey),
				equipOn: refrigMaster !== REMOTE.OFF,
				diagOn: anyDiag,
				remoteStatus: String(refrigMaster),
				remSwitchName: 'refrigBtn',
				outputColor: anyOn ? 'text-green-700 font-bold' : 'text-red-500 font-bold',
				statusColor: status.color,
				panelSwitchColor: sw.color,
				edit,
			};
		}
		case 'refrig1':
			return buildRefrigRow(equipment, eq, tr('equipment.refrigeration-stage-1'), 'refr1Btn',
				equipment.ioNames[EQ.REFRIG_STAGE1], e(EQ.REFRIG_STAGE1), refrigMaster, edit);
		case 'refrig2':
			return buildRefrigRow(equipment, eq, tr('equipment.refrigeration-stage-2'), 'refr2Btn',
				equipment.ioNames[EQ.REFRIG_STAGE2], e(EQ.REFRIG_STAGE2), refrigMaster, edit);
		case 'refrig3':
			return buildRefrigRow(equipment, eq, tr('equipment.refrigeration-stage-3'), 'refr3Btn',
				equipment.ioNames[EQ.REFRIG_STAGE3], e(EQ.REFRIG_STAGE3), refrigMaster, edit);
		case 'refrig4':
			return buildRefrigRow(equipment, eq, tr('equipment.refrigeration-stage-4'), 'refr4Btn',
				equipment.ioNames[EQ.REFRIG_STAGE4], e(EQ.REFRIG_STAGE4), refrigMaster, edit);
		case 'refrig5':
			return buildRefrigRow(equipment, eq, tr('equipment.refrigeration-stage-5'), 'refr5Btn',
				equipment.ioNames[EQ.REFRIG_STAGE5], e(EQ.REFRIG_STAGE5), refrigMaster, edit);
		case 'refrig6':
			return buildRefrigRow(equipment, eq, tr('equipment.refrigeration-stage-6'), 'refr6Btn',
				equipment.ioNames[EQ.REFRIG_STAGE6], e(EQ.REFRIG_STAGE6), refrigMaster, edit);
		case 'refrig7':
			return buildRefrigRow(equipment, eq, tr('equipment.refrigeration-stage-7'), 'refr7Btn',
				equipment.ioNames[EQ.REFRIG_STAGE7], e(EQ.REFRIG_STAGE7), refrigMaster, edit);
		case 'refrig8':
			return buildRefrigRow(equipment, eq, tr('equipment.refrigeration-stage-8'), 'refr8Btn',
				equipment.ioNames[EQ.REFRIG_STAGE8], e(EQ.REFRIG_STAGE8), refrigMaster, edit);
		case 'defrost1':
			return buildRefrigRow(equipment, eq, tr('equipment.defrost-1'), 'defrost1Btn',
				equipment.ioNames[EQ.REFRIG_DEFROST1], e(EQ.REFRIG_DEFROST1), refrigMaster, edit);
		case 'defrost2':
			return buildRefrigRow(equipment, eq, tr('equipment.defrost-2'), 'defrost2Btn',
				equipment.ioNames[EQ.REFRIG_DEFROST2], e(EQ.REFRIG_DEFROST2), refrigMaster, edit);

		case 'humid1':
			return buildRow(equipment, eq, tr('equipment.humidifier-1-head'), 'humid1PumpBtn',
				equipment.ioNames[EQ.HUMID_HEAD1], e(EQ.HUMID_HEAD1), edit);
		case 'pump1':
			return buildRow(equipment, eq, tr('equipment.humidifier-1-pump'), 'humid1PumpBtn',
				equipment.ioNames[EQ.HUMID_PUMP1], e(EQ.HUMID_PUMP1), edit);
		case 'humid2':
			return buildRow(equipment, eq, tr('equipment.humidifier-2-head'), 'humid2PumpBtn',
				equipment.ioNames[EQ.HUMID_HEAD2], e(EQ.HUMID_HEAD2), edit);
		case 'pump2':
			return buildRow(equipment, eq, tr('equipment.humidifier-2-pump'), 'humid2PumpBtn',
				equipment.ioNames[EQ.HUMID_PUMP2], e(EQ.HUMID_PUMP2), edit);
		case 'humid3':
			return buildRow(equipment, eq, tr('equipment.humidifier-3-head'), 'humid3PumpBtn',
				equipment.ioNames[EQ.HUMID_HEAD3], e(EQ.HUMID_HEAD3), edit);
		case 'pump3':
			return buildRow(equipment, eq, tr('equipment.humidifier-3-pump'), 'humid3PumpBtn',
				equipment.ioNames[EQ.HUMID_PUMP3], e(EQ.HUMID_PUMP3), edit);

		case 'door':
			// Force exists=true: the row's purpose is to drive the state
			// machine (SW_FRESHAIR_AUTO gate) regardless of whether a
			// physical DOORS output is wired. Replaces the legacy CPLD
			// economizer auto-bypass removed in fw 0.A.46.
			//
			// Operator-friendly labels: doors are an actuator, so the
			// MANUAL/OFF semantics map to OPEN/CLOSE on the wire (same
			// remote_off values 2/1 — no protocol change). Firmware
			// forces PWM_DOORS to MAX/MIN respectively in lp_engine_tick.
			//
			// Status column shows the COMMANDED door % from
			// SystemStatus.pwm_doors_pct (field 20) — always populated by
			// firmware regardless of system mode (cool / refrig / standby /
			// shutdown). Operators with no DI feedback wired (the common
			// case on Constellation installs — no limit switches on the
			// fresh-air actuators) were otherwise stuck seeing "Off" at
			// all times even while the door PID was actively driving the
			// damper to 50%+. Pulling the typed proto field gives the
			// commanded position the operator can actually verify against
			// the physical actuator.
			return {
				...buildRow(equipment, eq, tr('level2.pid.fresh-air-doors'), 'doorBtn',
					equipment.ioNames[EQ.DOORS], e(EQ.DOORS), edit),
				exists: true,
				equipmentStatus: doorPct !== undefined ? `${doorPct}%` : tr('global.off'),
				offLabel: tr('global.close'),
				manualLabel: tr('global.open'),
			};
		case 'doordiag':
			return buildDoorDiagRow(equipment, eq, tr('level2.pid.fresh-air-doors'), 'doorDiag',
				equipment.ioNames[EQ.DOORS], e(EQ.DOORS),
				doorPct, edit, '');

		case 'cure': {
			// Cure is operator-controlled via virtual remote_off slot
			// EQ.CURE_VIRTUAL=63 (no real wired equipment, no input
			// proving, AUTO/OFF only — MANUAL is meaningless because
			// cure mode itself reshapes the device UI). LP firmware
			// emits a synthetic EquipState{eq=63,remote_off} when
			// SystemMode==Onion so the tile reflects persisted state
			// after reload. Live activity proxy: fan running while cure
			// is enabled (until a dedicated CureStatus proto exists).
			const cure = e(EQ.CURE_VIRTUAL);
			const remote = cure?.remoteOff ?? REMOTE.AUTO;
			const sw = interpretRemoteState(remote);
			const fanOn = e(EQ.FAN)?.outputOn ?? false;
			const cureRunning = fanOn && remote !== REMOTE.OFF;
			const status = cureRunning
				? { statusKey: 'global.on', color: 'text-green-700 font-bold' }
				: { statusKey: 'global.off', color: 'text-red-500 font-bold' };
			return {
				exists: true,
				name: eq,
				equipmentName: tr('global.cure'),
				equipmentStatus: tr(status.statusKey),
				panelSwitchStatus: tr(sw.statusKey),
				equipOn: remote !== REMOTE.OFF,
				remoteStatus: String(remote),
				remSwitchName: 'cureBtn',
				outputColor: getOutputColor(cureRunning),
				statusColor: status.color,
				panelSwitchColor: sw.color,
				allowManual: false,
				edit,
			};
		}

		case 'lights1':
			return buildLightsRow(equipment, eq, tr('equipment.bay-lights-1'),
				'lights1Btn', equipment.ioNames[EQ.LIGHTS1], e(EQ.LIGHTS1), edit);
		case 'lights2':
			return buildLightsRow(equipment, eq, tr('equipment.bay-lights-2'),
				'lights2Btn', equipment.ioNames[EQ.LIGHTS2], e(EQ.LIGHTS2), edit);
	}
	return undefined;
}
