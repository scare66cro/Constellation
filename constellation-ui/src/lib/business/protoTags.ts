/**
 * protoTags.ts — Phase 0.7 of the proto-direct redesign.
 *
 * Single source of truth mapping numeric envelope tags to their ts-proto
 * decoder. Mirrors `proto/agristar/envelope.proto` (oneof field numbers)
 * and the bridge-side ranges in `constellation-ui/server/src/protoStream.ts`:
 *
 *   10–29   firmware status / config push
 *   30–39   firmware responses
 *   40–79   firmware settings pages
 *   90–99   bridge → firmware settings updates (UI does not subscribe)
 *   100–109 protocol control (UI does not subscribe)
 *   110–119 firmware update flow (UI does not subscribe)
 *   200–209 bridge-emitted, synthesized in protoStream.ts
 *
 * Add a tag here ONLY when a UI page actually consumes it. Keep the
 * registry tight so the TypeScript union remains useful.
 *
 * See docs/proto-direct-redesign-plan.md for the full migration plan.
 */

import type { MessageFns } from '$proto/agristar/vfd.js';
import { BinaryReader } from '@bufbuild/protobuf/wire';
import { SystemStatus, BasicSetup, NetworkConfig, DateTime, ServiceInfo, VersionInfo } from '$proto/agristar/system.js';
import { EquipmentStatus, AuxSwitches, FanRuntime, Runtimes } from '$proto/agristar/equipment.js';
import { SensorData, SensorLabels } from '$proto/agristar/sensors.js';
import { IoConfig, IoDefinition, AnalogBoard, IoNameUpdate } from '$proto/agristar/io.js';
import { CureSettings, MiscSettings, PlenumSettings, PwmChannelSettings, RefrigSettings, BurnerSettings, Co2Settings, MasterSlaveSettings, ClimacellSettings, ClimacellTimes, LoadMonitorSettings, DoorSettings, FanSpeedSettings, FanBoostSettings, RampRateSettings, HumidCtrlSettings, OutsideAirSettings, EmailSettings, TempAlarmSettings, UserLogSettings, PidLogSettings, AlertSettings, FailureSettings, FailureSettings2, AuxProgramSettings } from '$proto/agristar/settings.js';
import { VfdStatus } from '$proto/agristar/vfd.js';
import { WarningReport } from '$proto/agristar/alarms.js';
import { AccountSettings, NetworkNodeSettings } from '$proto/agristar/accounts.js';
import { OrbitStatus, OrbitSensorBank } from '$proto/agristar/orbit.js';

export const TAG = {
	SystemStatus: 10,
	EquipmentStatus: 11,
	WarningReport: 12,
	SensorData: 13,
	/* Runtimes: firmware emits RuntimeSettings on envelope tag 77 every
	 * 5 s (Nova_Firmware/lp_am2434/main.c, tick%50==28, prefix 0xEA 0x04
	 * = (77<<3)|2). Tag history: legacy `Runtimes` was tag 14 (Phase C
	 * empty-skeleton placeholder), then moved to 71, then to 77 when
	 * RuntimeSettings was relocated from equipment.proto to
	 * settings.proto. Wire format is identical across all three (just
	 * `repeated RuntimeEntry entries = 1`) so the Runtimes decoder
	 * handles tag-77 bytes unchanged. Save path is SettingsUpdate.field
	 * 18 (LpSettings_ApplyRuntime, main.c::bridge_rx_callback). */
	Runtimes: 77,
	AuxSwitches: 16,
	FanRuntime: 18,
	NetworkConfig: 19,
	BasicSetup: 20,
	DateTime: 21,
	VersionInfo: 22,
	ServiceInfo: 23,
	IoConfig: 24,
	IoDefinition: 25,
	AnalogBoard: 26,
	SensorLabels: 28,
	AccountSettings: 29,
	PlenumSettings: 40,
	FanSpeedSettings: 41,
	FanBoostSettings: 42,
	RampRateSettings: 43,
	RefrigSettings: 44,
	BurnerSettings: 45,
	Co2Settings: 46,
	CureSettings: 47,
	ClimacellSettings: 48,
	ClimacellTimes: 49,
	HumidCtrlSettings: 50,
	OutsideAirSettings: 51,
	MiscSettings: 52,
	FailureSettings: 53,
	FailureSettings2: 54,
	TempAlarmSettings: 55,
	DoorSettings: 56,
	LoadMonitorSettings: 57,
	UserLogSettings: 59,
	EmailSettings: 62,
	AlertSettings: 63,
	PwmChannelSettings: 64,
	NetworkNodeSettings: 65,
	MasterSlaveSettings: 66,
	PidLogSettings: 67,
	/* AuxProgramSettings: WRITE-only sentinel (158) — routes via
	 * SETTINGS_FIELD[TAG.AuxProgramSettings] = 21 to /proto/write/21,
	 * which the firmware bridge_rx_callback dispatches to
	 * LpSettings_ApplyAuxProgram. The 158 value intentionally falls
	 * outside any wire-emit range so a stray subscription would never
	 * receive a real frame.
	 *
	 * READ side lives at TAG.AuxProgramBundle (72) below — firmware
	 * emits all populated channels in a single envelope (field-1
	 * length-delim AuxProgramSettings submsgs, every 5 s). */
	AuxProgramSettings: 158,
	AuxProgramBundle: 72,
	/* IoNameUpdate: WRITE-only sentinel (159) — routes via
	 * SETTINGS_FIELD[TAG.IoNameUpdate] = 40 to /proto/write/40,
	 * dispatched by main.c to LpSettings_ApplyIoName. The 159 value
	 * intentionally falls outside any wire-emit range so no UI
	 * subscription would ever match. READ side is TAG.IoDefinition (25)
	 * which carries the full equipment table including any user renames. */
	IoNameUpdate: 159,
	OrbitStatus: 120,
	OrbitSensorBank: 124,
	VfdStatus: 200
} as const;

export type Tag = (typeof TAG)[keyof typeof TAG];

// ─── AuxProgramBundle synthetic message ─────────────────────────────────
//
// Firmware emits TAG.AuxProgramBundle (72) every 5 s with
// `BuildAuxProgramBody` in `Nova_Firmware/lp_am2434/lp_settings.c` — wire
// layout is a bare concatenation of `field-1 length-delim AuxProgramSettings`
// submsgs, one per populated channel. Equivalent to a hypothetical
// `message AuxProgramBundle { repeated AuxProgramSettings programs = 1; }`,
// but defined client-side to avoid a schema change. The decoder walks the
// bytes, decoding each field-1 submsg with the real `AuxProgramSettings`
// decoder; unknown fields are skipped via `BinaryReader.skip`.
//
// Declared before TAG_DECODERS to avoid a const-init TDZ at module load.
export interface AuxProgramBundle {
	programs: AuxProgramSettings[];
}

export const AuxProgramBundle: MessageFns<AuxProgramBundle> = {
	encode(_msg, _writer?): any {
		throw new Error('AuxProgramBundle is decode-only (firmware-emitted, never sent by UI)');
	},
	decode(input: Uint8Array | BinaryReader, length?: number): AuxProgramBundle {
		const reader = input instanceof BinaryReader ? input : new BinaryReader(input);
		const end = length === undefined ? reader.len : reader.pos + length;
		const out: AuxProgramBundle = { programs: [] };
		while (reader.pos < end) {
			const tag = reader.uint32();
			const field = tag >>> 3;
			if (field === 1 && (tag & 7) === 2) {
				out.programs.push(AuxProgramSettings.decode(reader, reader.uint32()));
			} else {
				reader.skip(tag & 7);
			}
		}
		return out;
	},
	fromJSON(_object: any): AuxProgramBundle { return { programs: [] }; },
	toJSON(message: AuxProgramBundle): unknown { return { programs: message.programs }; },
	create(base?: any): AuxProgramBundle { return { programs: base?.programs ?? [] }; },
	fromPartial(object: any): AuxProgramBundle { return { programs: object?.programs ?? [] }; }
};

/**
 * Maps a tag to the decoded message shape. Extend as new pages migrate.
 * The compiler enforces that any decoder added to TAG_DECODERS has a
 * corresponding entry here.
 */
export interface TagPayload {
	[TAG.SystemStatus]: SystemStatus;
	[TAG.EquipmentStatus]: EquipmentStatus;
	[TAG.WarningReport]: WarningReport;
	[TAG.SensorData]: SensorData;
	[TAG.Runtimes]: Runtimes;
	[TAG.AuxSwitches]: AuxSwitches;
	[TAG.FanRuntime]: FanRuntime;
	[TAG.NetworkConfig]: NetworkConfig;
	[TAG.BasicSetup]: BasicSetup;
	[TAG.DateTime]: DateTime;
	[TAG.VersionInfo]: VersionInfo;
	[TAG.ServiceInfo]: ServiceInfo;
	[TAG.IoConfig]: IoConfig;
	[TAG.IoDefinition]: IoDefinition;
	[TAG.AnalogBoard]: AnalogBoard;
	[TAG.SensorLabels]: SensorLabels;
	[TAG.AccountSettings]: AccountSettings;
	[TAG.PlenumSettings]: PlenumSettings;
	[TAG.FanSpeedSettings]: FanSpeedSettings;
	[TAG.FanBoostSettings]: FanBoostSettings;
	[TAG.RampRateSettings]: RampRateSettings;
	[TAG.RefrigSettings]: RefrigSettings;
	[TAG.BurnerSettings]: BurnerSettings;
	[TAG.Co2Settings]: Co2Settings;
	[TAG.CureSettings]: CureSettings;
	[TAG.ClimacellSettings]: ClimacellSettings;
	[TAG.ClimacellTimes]: ClimacellTimes;
	[TAG.HumidCtrlSettings]: HumidCtrlSettings;
	[TAG.OutsideAirSettings]: OutsideAirSettings;
	[TAG.MiscSettings]: MiscSettings;
	[TAG.FailureSettings]: FailureSettings;
	[TAG.FailureSettings2]: FailureSettings2;
	[TAG.TempAlarmSettings]: TempAlarmSettings;
	[TAG.DoorSettings]: DoorSettings;
	[TAG.LoadMonitorSettings]: LoadMonitorSettings;
	[TAG.UserLogSettings]: UserLogSettings;
	[TAG.EmailSettings]: EmailSettings;
	[TAG.AlertSettings]: AlertSettings;
	[TAG.PwmChannelSettings]: PwmChannelSettings;
	[TAG.NetworkNodeSettings]: NetworkNodeSettings;
	[TAG.MasterSlaveSettings]: MasterSlaveSettings;
	[TAG.PidLogSettings]: PidLogSettings;
	[TAG.AuxProgramSettings]: AuxProgramSettings;
	[TAG.AuxProgramBundle]: AuxProgramBundle;
	[TAG.IoNameUpdate]: IoNameUpdate;
	[TAG.OrbitStatus]: OrbitStatus;
	[TAG.OrbitSensorBank]: OrbitSensorBank;
	[TAG.VfdStatus]: VfdStatus;
}

/** Registry of decoders keyed by tag. */
export const TAG_DECODERS: { [K in Tag]: MessageFns<TagPayload[K]> } = {
	[TAG.SystemStatus]: SystemStatus,
	[TAG.EquipmentStatus]: EquipmentStatus,
	[TAG.WarningReport]: WarningReport,
	[TAG.SensorData]: SensorData,
	[TAG.Runtimes]: Runtimes,
	[TAG.AuxSwitches]: AuxSwitches,
	[TAG.FanRuntime]: FanRuntime,
	[TAG.NetworkConfig]: NetworkConfig,
	[TAG.BasicSetup]: BasicSetup,
	[TAG.DateTime]: DateTime,
	[TAG.VersionInfo]: VersionInfo,
	[TAG.ServiceInfo]: ServiceInfo,
	[TAG.IoConfig]: IoConfig,
	[TAG.IoDefinition]: IoDefinition,
	[TAG.AnalogBoard]: AnalogBoard,
	[TAG.SensorLabels]: SensorLabels,
	[TAG.AccountSettings]: AccountSettings,
	[TAG.PlenumSettings]: PlenumSettings,
	[TAG.FanSpeedSettings]: FanSpeedSettings,
	[TAG.FanBoostSettings]: FanBoostSettings,
	[TAG.RampRateSettings]: RampRateSettings,
	[TAG.RefrigSettings]: RefrigSettings,
	[TAG.BurnerSettings]: BurnerSettings,
	[TAG.Co2Settings]: Co2Settings,
	[TAG.CureSettings]: CureSettings,
	[TAG.ClimacellSettings]: ClimacellSettings,
	[TAG.ClimacellTimes]: ClimacellTimes,
	[TAG.HumidCtrlSettings]: HumidCtrlSettings,
	[TAG.OutsideAirSettings]: OutsideAirSettings,
	[TAG.MiscSettings]: MiscSettings,
	[TAG.FailureSettings]: FailureSettings,
	[TAG.FailureSettings2]: FailureSettings2,
	[TAG.TempAlarmSettings]: TempAlarmSettings,
	[TAG.DoorSettings]: DoorSettings,
	[TAG.LoadMonitorSettings]: LoadMonitorSettings,
	[TAG.UserLogSettings]: UserLogSettings,
	[TAG.EmailSettings]: EmailSettings,
	[TAG.AlertSettings]: AlertSettings,
	[TAG.PwmChannelSettings]: PwmChannelSettings,
	[TAG.NetworkNodeSettings]: NetworkNodeSettings,
	[TAG.MasterSlaveSettings]: MasterSlaveSettings,
	[TAG.PidLogSettings]: PidLogSettings,
	[TAG.AuxProgramSettings]: AuxProgramSettings,
	[TAG.AuxProgramBundle]: AuxProgramBundle,
	[TAG.IoNameUpdate]: IoNameUpdate,
	[TAG.OrbitStatus]: OrbitStatus,
	[TAG.OrbitSensorBank]: OrbitSensorBank,
	[TAG.VfdStatus]: VfdStatus
};

export function decodeTag<K extends Tag>(tag: K, bytes: Uint8Array): TagPayload[K] {
	return TAG_DECODERS[tag].decode(bytes);
}

export function isKnownTag(tag: number): tag is Tag {
	return tag in TAG_DECODERS;
}
