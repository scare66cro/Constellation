/**
 * forceFieldRegistry — declarative "this scalar field must always be on
 * the wire" map, consulted by writeProto / writeProtoRow.
 *
 * proto3 encoders skip scalar fields whose value equals the type default
 * (0, 0.0, "", false). Most settings tolerate this fine, but a chunk of
 * firmware fields treat 0 as a meaningful operating value (mode=OFF,
 * idx=0, threshold=0, "stop fan"). For those, the UI must force the
 * value onto the wire even when it equals the default.
 *
 * Before this registry, every page carried inline literals like
 *   forceVarints: { 2: $draft.port, 7: $draft.enabled, 8: $draft.authType }
 * at the save site, sprinkling field-number magic numbers across 13
 * pages. Now the page just calls `writeProto(TAG.X, $draft)` and the
 * encoder consults this single source of truth.
 *
 * Wire layout, schema field numbers, and the firmware decoder semantics
 * are still authoritative — this file only encodes which of those
 * fields need force-emit. When firmware grows a new zero-meaningful
 * field, add it here, not at the call site.
 *
 * See docs/proto-force-zero-design.md for the design rationale and the
 * future Approach-A migration path (proto-level `(force_zero) = true`
 * field option).
 */

import type { Tag, TagPayload } from './protoTags.js';
import { TAG } from './protoTags.js';

/** One forced field: the wire field number + the message property name. */
export interface ForceField<K extends Tag> {
	/** Wire field number (must match the .proto definition). */
	num: number;
	/** Property name on the typed message. */
	name: keyof TagPayload[K] & string;
}

export interface ForceSpec<K extends Tag> {
	varint?: ForceField<K>[];
	float?:  ForceField<K>[];
}

/**
 * Declarative force-emit map. Only list a field here when 0 is a
 * meaningful operating value in firmware — listing a field whose 0
 * value would naturally be ignored just wastes 1-2 wire bytes per save.
 *
 * Field numbers come from the corresponding `.proto` definition under
 * `proto/agristar/`. Verify by inspecting the firmware apply_*()
 * handler in `Nova_Firmware/Platform/nova_dataexc.c`.
 *
 * NOTE on coverage: pages that build their own bytes via writeProtoRaw
 * (humidifier, accounts, auxiliary, analog, etc.) declare their force
 * fields inline because they're operating below the typed-encoder
 * layer. Those don't belong here. Only top-level typed sends flow
 * through this registry.
 */
export const FORCE_FIELDS: Partial<{ [K in Tag]: ForceSpec<K> }> = {
	[TAG.EmailSettings]: {
		varint: [
			{ num: 2, name: 'port' },     // 0 = unset
			{ num: 7, name: 'enabled' },  // 0 = on
			{ num: 8, name: 'authType' }, // 0 = StartTLS
		],
	},
	[TAG.FanSpeedSettings]: {
		varint: [
			{ num: 1,  name: 'maxSpeed' },
			{ num: 2,  name: 'minSpeed' },
			{ num: 3,  name: 'refrigSpeed' },
			{ num: 4,  name: 'recircSpeed' },
			{ num: 5,  name: 'updatePeriod' },
			{ num: 7,  name: 'tempRef1' },
			{ num: 8,  name: 'tempRef2' },
			{ num: 9,  name: 'prevSpeed' },   // current cooling % — 0 = stop fan
			{ num: 10, name: 'updateMode' },
		],
		float: [
			{ num: 6, name: 'tempDiff' },
		],
	},
	[TAG.MiscSettings]: {
		varint: [
			{ num: 1,  name: 'refrigMode' },        // 0 = econ
			{ num: 2,  name: 'defrostInterval' },   // 0 = disabled
			{ num: 3,  name: 'defrostDuration' },
			{ num: 5,  name: 'cavityTarget' },      // 0 = cavity heat
			{ num: 6,  name: 'cavityMode' },
			{ num: 8,  name: 'cavityDutyOrSensor' },
			{ num: 9,  name: 'cavityStandbyOn' },
			{ num: 10, name: 'kbPref' },            // 0 = standard
		],
	},
	[TAG.OutsideAirSettings]: {
		varint: [
			{ num: 1, name: 'mode' },
			{ num: 3, name: 'aboveBelow' },
			{ num: 4, name: 'tempRef' },
			{ num: 5, name: 'calcHumidMax' },
		],
	},
	[TAG.CureSettings]: {
		varint: [
			{ num: 2, name: 'humidRef' },
		],
	},
	[TAG.ClimacellSettings]: {
		varint: [
			{ num: 2, name: 'altitude' },
			{ num: 3, name: 'altUnits' },
		],
	},
	[TAG.PidLogSettings]: {
		varint: [
			{ num: 1, name: 'wrap' },
			{ num: 2, name: 'logDoors' },
			{ num: 3, name: 'logRefrig' },
		],
	},
	[TAG.BasicSetup]: {
		varint: [
			{ num: 2,  name: 'tempType' },
			{ num: 5,  name: 'systemMode' },
			{ num: 8,  name: 'multiView' },
			{ num: 10, name: 'localLogin' },
			{ num: 11, name: 'animations' },
		],
	},
	[TAG.IoNameUpdate]: {
		varint: [
			{ num: 1, name: 'index' },  // slot 0 must reach the wire
		],
	},
	// PlenumSettings, TempAlarmSettings, RampRateSettings, FailureSettings,
	// FailureSettings2: these pages currently call writeProto with the full
	// message and an inline forceVarints map per save. Move them into the
	// registry below as part of the sweep when the page is touched next —
	// the multi-tag plentemp page especially benefits from removing the
	// per-handler magic numbers.
	[TAG.PlenumSettings]: {
		varint: [
			{ num: 2, name: 'humidSetpoint' },
			{ num: 3, name: 'humidSetpointRef' },
		],
	},
	[TAG.TempAlarmSettings]: {
		varint: [
			{ num: 2, name: 'lowTimer' },
			{ num: 4, name: 'highTimer' },
		],
	},
	[TAG.RampRateSettings]: {
		varint: [
			{ num: 2, name: 'updatePeriod' },
			{ num: 4, name: 'tempRef' },
		],
	},
	[TAG.FailureSettings]: {
		varint: [
			{ num: 1,  name: 'fanMode' },
			{ num: 2,  name: 'fanTimer' },
			{ num: 3,  name: 'heatMode' },
			{ num: 4,  name: 'heatTimer' },
			{ num: 5,  name: 'refrigMode' },
			{ num: 6,  name: 'refrigTimer' },
			{ num: 7,  name: 'refrigFailMode' },
			{ num: 8,  name: 'burnerMode' },
			{ num: 9,  name: 'burnerTimer' },
			{ num: 10, name: 'humidTimer' },
			{ num: 11, name: 'climacellTimer' },
			{ num: 12, name: 'lightsMode' },
			{ num: 13, name: 'lightsTimer' },
			{ num: 14, name: 'lightsUnits' },
			{ num: 15, name: 'climacellMode' },
			{ num: 16, name: 'refrigStagesMode' },
			{ num: 17, name: 'refrigStagesTimer' },
			{ num: 18, name: 'humidMode' },
			{ num: 19, name: 'auxMode' },
			{ num: 20, name: 'auxTimer' },
			{ num: 21, name: 'cavityHeatMode' },
			{ num: 22, name: 'cavityHeatTimer' },
		],
	},
	[TAG.FailureSettings2]: {
		varint: [
			{ num: 1,  name: 'outAirMode' },
			{ num: 2,  name: 'outAirTimer' },
			{ num: 3,  name: 'outHumidMode' },
			{ num: 4,  name: 'outHumidTimer' },
			{ num: 5,  name: 'highCo2Mode' },
			{ num: 6,  name: 'highCo2Timer' },
			{ num: 7,  name: 'co2Setpt' },
			{ num: 8,  name: 'lowHumidMode' },
			{ num: 9,  name: 'lowHumidTimer' },
			{ num: 10, name: 'lowHumidSet' },
			{ num: 11, name: 'plenSenMode' },
			{ num: 12, name: 'plenSenTimer' },
		],
	},
};

/**
 * Read the registered force fields for a tag, look up their values on
 * the message, and return the merged maps for buildForceVarintBytes /
 * buildForceFloatBytes. Returns `undefined` for either entry when the
 * tag has nothing registered or the message is missing — callers may
 * pass `undefined` straight into WriteProtoOptions without branching.
 */
export function resolveForceFields<K extends Tag>(
	tag: K,
	msg: Partial<TagPayload[K]> | TagPayload[K]
): { forceVarints?: Record<number, number>; forceFloats?: Record<number, number> } {
	const spec = FORCE_FIELDS[tag] as ForceSpec<K> | undefined;
	if (!spec) return {};
	const out: { forceVarints?: Record<number, number>; forceFloats?: Record<number, number> } = {};
	if (spec.varint?.length) {
		const m: Record<number, number> = {};
		for (const f of spec.varint) {
			const v = (msg as any)[f.name];
			m[f.num] = typeof v === 'number' ? v : 0;
		}
		out.forceVarints = m;
	}
	if (spec.float?.length) {
		const m: Record<number, number> = {};
		for (const f of spec.float) {
			const v = (msg as any)[f.name];
			m[f.num] = typeof v === 'number' ? v : 0;
		}
		out.forceFloats = m;
	}
	return out;
}
