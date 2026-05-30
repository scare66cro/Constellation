/**
 * protoStores.ts — Phase 0.7 of the proto-direct redesign.
 *
 * Typed Svelte readable stores backed by the singleton `/proto/stream`
 * WebSocket client. Each store:
 *
 *   - Lazily acquires its tag subscription on the first subscriber
 *     (the bridge replays the latest cached frame immediately, so the
 *     store yields a value within one round-trip — no polling needed).
 *   - Releases the subscription when the last subscriber unmounts.
 *   - Ignores frames for other tags.
 *
 * Add new stores using `createTagStore(TAG.X)` — one line per page.
 *
 * USAGE in a Svelte page:
 *
 *   <script lang="ts">
 *     import { equipmentStatus } from '$lib/business/protoStores';
 *   </script>
 *   {#if $equipmentStatus}
 *     <p>{$equipmentStatus.machineState}</p>
 *   {/if}
 *
 * The `{#if $store}` guard is required because the store is `null`
 * until the first frame arrives (which is fast — bridge replays on
 * subscribe — but is still asynchronous and SSR-safe).
 */

import { readable, derived, type Readable } from 'svelte/store';
import { protoStream } from './protoStreamClient.js';
import { TAG, decodeTag, type Tag, type TagPayload } from './protoTags.js';
import type { Equipment } from './equipmentStatus.js';

export type ProtoStore<K extends Tag> = Readable<TagPayload[K] | null>;

/**
 * Build a per-tag readable store. Subscriber lifecycle:
 *   start  → acquireTag(tag) + addListener(filtered)
 *   stop   → release listener + release tag
 */
export function createTagStore<K extends Tag>(tag: K): ProtoStore<K> {
	return readable<TagPayload[K] | null>(null, (set) => {
		const releaseTag = protoStream.acquireTag(tag);
		const releaseListener = protoStream.addListener((t, bytes) => {
			if (t !== tag) return;
			try {
				set(decodeTag(tag, bytes));
			} catch (err) {
				console.error(`[protoStores] decode failed for tag ${tag}:`, err);
			}
		});
		return () => {
			releaseListener();
			releaseTag();
		};
	});
}

// ─── Concrete stores ────────────────────────────────────────────────────
// Add one line here per page-migration. Keep alphabetical for sanity.

export const accountSettings = createTagStore(TAG.AccountSettings);
export const alertSettings = createTagStore(TAG.AlertSettings);
export const analogBoard = createTagStore(TAG.AnalogBoard);
export const auxSwitches = createTagStore(TAG.AuxSwitches);
export const basicSetup = createTagStore(TAG.BasicSetup);
export const burnerSettings = createTagStore(TAG.BurnerSettings);
export const co2Settings = createTagStore(TAG.Co2Settings);
export const cureSettings = createTagStore(TAG.CureSettings);
export const climacellSettings = createTagStore(TAG.ClimacellSettings);
export const climacellTimes = createTagStore(TAG.ClimacellTimes);
export const dateTime = createTagStore(TAG.DateTime);
export const doorSettings = createTagStore(TAG.DoorSettings);
export const emailSettings = createTagStore(TAG.EmailSettings);
export const failureSettings = createTagStore(TAG.FailureSettings);
export const failureSettings2 = createTagStore(TAG.FailureSettings2);
export const equipmentStatus = createTagStore(TAG.EquipmentStatus);
export const fanBoostSettings = createTagStore(TAG.FanBoostSettings);
export const fanRuntime = createTagStore(TAG.FanRuntime);
export const fanSpeedSettings = createTagStore(TAG.FanSpeedSettings);
export const humidCtrlSettings = createTagStore(TAG.HumidCtrlSettings);
export const ioConfig = createTagStore(TAG.IoConfig);
export const ioDefinition = createTagStore(TAG.IoDefinition);
export const loadMonitorSettings = createTagStore(TAG.LoadMonitorSettings);
export const masterSlaveSettings = createTagStore(TAG.MasterSlaveSettings);
export const miscSettings = createTagStore(TAG.MiscSettings);
export const networkConfig = createTagStore(TAG.NetworkConfig);
export const networkNodes = createTagStore(TAG.NetworkNodeSettings);
export const orbitStatus = createTagStore(TAG.OrbitStatus);

/**
 * `orbitSensorBanks` — per-slot aggregating store for `OrbitSensorBank`
 * frames (envelope tag 124). Each frame carries one slot's HR bank, and
 * firmware cycles slots round-robin, so a plain "last frame wins" store
 * would only ever expose one slot at a time. This store keeps a Map by
 * slot and emits a fresh snapshot whenever any slot updates.
 *
 * Bridge is a transparent gateway — values are raw uint16 holding-
 * register readings (no scaling, no unit conversion). Engineering-unit
 * conversion belongs in the consumer page using IoDefinition metadata.
 */
export const orbitSensorBanks: Readable<
	ReadonlyMap<number, TagPayload[typeof TAG.OrbitSensorBank]>
> = readable<ReadonlyMap<number, TagPayload[typeof TAG.OrbitSensorBank]>>(
	new Map(),
	(set) => {
		const banks = new Map<number, TagPayload[typeof TAG.OrbitSensorBank]>();
		const releaseTag = protoStream.acquireTag(TAG.OrbitSensorBank);
		const releaseListener = protoStream.addListener((t, bytes) => {
			if (t !== TAG.OrbitSensorBank) return;
			try {
				const bank = decodeTag(TAG.OrbitSensorBank, bytes);
				banks.set(bank.slot, bank);
				set(new Map(banks));
			} catch (err) {
				console.error('[protoStores] decode failed for OrbitSensorBank:', err);
			}
		});
		return () => {
			releaseListener();
			releaseTag();
			banks.clear();
		};
	}
);

export const outsideAirSettings = createTagStore(TAG.OutsideAirSettings);
export const pidLogSettings = createTagStore(TAG.PidLogSettings);
export const plenumSettings = createTagStore(TAG.PlenumSettings);
export const tempAlarmSettings = createTagStore(TAG.TempAlarmSettings);
export const userLogSettings = createTagStore(TAG.UserLogSettings);
export const pwmChannelSettings = createTagStore(TAG.PwmChannelSettings);
export const rampRateSettings = createTagStore(TAG.RampRateSettings);
export const refrigSettings = createTagStore(TAG.RefrigSettings);
export const runtimes = createTagStore(TAG.Runtimes);
export const sensorData = createTagStore(TAG.SensorData);
export const sensorLabels = createTagStore(TAG.SensorLabels);
export const serviceInfo = createTagStore(TAG.ServiceInfo);
export const systemStatus = createTagStore(TAG.SystemStatus);
export const versionInfo = createTagStore(TAG.VersionInfo);
export const vfdStatus = createTagStore(TAG.VfdStatus);
export const warningReport = createTagStore(TAG.WarningReport);

// ─── Composite stores ───────────────────────────────────────────────────
//
// The legacy bridge `equipment-data` WS channel hand-built a composite
// from 7 cached fields. We reproduce that shape here so the level1
// equipment page (and any other consumer of the `Equipment` type) can
// drop its WsClient and consume proto stores directly.
//
// All composition is pure (no bridge round-trip), so adding more
// consumers is free.

/**
 * Convert eq-indexed `IoConfig.outputMap` / `inputMap` (firmware view) →
 * port-indexed string array (UI view).
 *
 * Wire layout (per nova_messages.c::NovaMsg_SendIoConfig):
 *   outputMap[eqIdx] = portId  (or 0/255/0xffff sentinel for unassigned)
 *
 * UI shape: pages read `outputConfig[portId] = String(eqIdx)`, defaulting
 * to `'-1'` if no equipment is bound. So we invert: for every eqIdx with
 * a real portId, write `out[portId] = String(eqIdx)`.
 *
 * Distinct from the bridge helper `dataCache.portToEqIndexed()`, which
 * inverts the OPPOSITE direction (the bridge cache stores port-indexed
 * CSVs from the legacy ARM format).
 */
function eqMapToPortIndexed(outputMap: number[]): string[] {
	const maxEq = Math.max(outputMap.length, 60);
	const out = new Array<string>(maxEq).fill('-1');
	const UNASSIGNED = new Set([0, 255, 0xffff, 0xffffffff]);
	for (let eqIdx = 0; eqIdx < outputMap.length; eqIdx++) {
		const portId = outputMap[eqIdx];
		if (UNASSIGNED.has(portId)) continue;
		if (portId >= 0 && portId < maxEq) out[portId] = String(eqIdx);
	}
	return out;
}

/**
 * Keep the proto's eq-indexed `output_map[]` (or `input_map[]`) as an
 * **eq-indexed** string array:  out[eqIdx] = String(portId), '-1' when
 * unassigned. The equipment status page and failures page check
 * `outputConfig[eqIdx] !== '-1'` to decide which equipment rows exist.
 */
const EQ_UNASSIGNED = new Set([0, 255, 0xffff, 0xffffffff]);
function eqMapToEqIndexed(outputMap: number[]): string[] {
	const maxEq = Math.max(outputMap.length, 60);
	const out = new Array<string>(maxEq).fill('-1');
	for (let eqIdx = 0; eqIdx < outputMap.length; eqIdx++) {
		const portId = outputMap[eqIdx];
		if (EQ_UNASSIGNED.has(portId)) continue;
		out[eqIdx] = String(portId);
	}
	return out;
}

/**
 * Build a typed `EquipState[]` indexed by `eqIndex`, with sparse holes
 * filled by a synthetic empty entry. This replaces the legacy
 * `buildEqStatusFlat` 102-slot CSV — pages now read
 * `equipment.eqByIdx(EQ.FAN).inputOn` (etc.) rather than
 * `equipment.eqStatus[37]`.
 *
 * Firmware only emits items for equipment that has an output mapping
 * in `IoConfig.outputMap`, so the dense `items[]` array is keyed by
 * `EquipState.eqIndex` (not by position). Indexing by eqIndex via the
 * returned accessor avoids relying on item order.
 */
function buildEqByIdx(
	items: TagPayload[typeof TAG.EquipmentStatus]['items']
): (idx: number) => TagPayload[typeof TAG.EquipmentStatus]['items'][number] | undefined {
	const byIdx = new Map<number, TagPayload[typeof TAG.EquipmentStatus]['items'][number]>();
	for (const it of items) byIdx.set(it.eqIndex, it);
	return (idx: number) => byIdx.get(idx);
}

/**
 * Build the legacy `pwmConfig` array from `PwmChannelSettings`. The
 * equipment page reads `pwmConfig[i].split(':')[2]` per channel,
 * expecting the historical AS2 format `eqIdx:enabled:portId:duty`.
 *
 * Returns `['0']` when no channels are present (matches the legacy
 * empty-state the page already tolerates — `pwmConfig.length === 1`
 * with the single sentinel `'0'`).
 */
function buildPwmConfig(
	channels: TagPayload[typeof TAG.PwmChannelSettings]['channels']
): string[] {
	if (!channels.length) return ['0'];
	return channels.map((c) => `${c.index}:${c.enabled}:${c.port}:${c.duty}`);
}

/**
 * Build the 14-element legacy miscData string array from the typed
 * MiscSettings proto. The miscellaneous page and the equipment/front-
 * matter composites both consume this layout (preserved from AS2 CSV
 * positional indexing).
 *
 * Slot map:
 *   [0]=refrigMode [1]=defrostInterval [2]=defrostDuration
 *   [3]=heatTempThresh [4]=cavityMode [5]=cavityMode (legacy mirror)
 *   [6]=cavityDiff [7]=cavityDutyOrSensor [8]=kbPref
 *   [9]='0' (locale reserved) [10]=cavityTarget [11]='0' (reserved)
 *   [12]=enthalpyOffPct [13]=cavityStandbyOn
 */
export function buildMiscData(
	mc: TagPayload[typeof TAG.MiscSettings]
): string[] {
	return [
		String(mc.refrigMode ?? 0),
		String(mc.defrostInterval ?? 0),
		String(mc.defrostDuration ?? 0),
		String(mc.heatTempThresh ?? 0),
		String(mc.cavityMode ?? 1),
		String(mc.cavityMode ?? 1),
		String(mc.cavityDiff ?? 0),
		String(mc.cavityDutyOrSensor ?? 0),
		String(mc.kbPref ?? 0),
		'0',
		String(mc.cavityTarget ?? 0),
		'0',
		String(mc.enthalpyOffPct ?? 0),
		String(mc.cavityStandbyOn ?? 0),
	];
}

/**
 * Derived composite — null until every constituent proto store has
 * produced its first frame. The bridge replays cached frames immediately
 * on subscribe so this typically settles within one round-trip.
 */
export const equipmentComposite: Readable<Equipment | null> = derived(
	[equipmentStatus, auxSwitches, basicSetup, ioConfig, ioDefinition, miscSettings, pwmChannelSettings],
	([$eq, $aux, $basic, $ioCfg, $ioDef, $misc, $pwm]) => {
		if (!$eq || !$aux || !$basic || !$ioCfg || !$ioDef || !$misc || !$pwm) return null;
		return {
			eqItems: $eq.items,
			eqByIdx: buildEqByIdx($eq.items),
			pwmConfig: buildPwmConfig($pwm.channels),
			outputConfig: eqMapToEqIndexed($ioCfg.outputMap),
			ioNames: $ioDef.entries.map((e) => ({
				index: e.index,
				name: e.name,
				mode: e.mode,
				ioPin: e.ioPin,
				renamable: e.renamable,
				visible: e.visible,
				ioType: e.ioType
			})),
			auxSwitches: $aux.switchState.map(String),
			miscData: buildMiscData($misc),
			systemMode: String($basic.systemMode)
		} satisfies Equipment;
	}
);

// ─── failures1 page composite ──────────────────────────────────────────
//
// Replaces the bridge's `/iot/failures1` GET (deleted in S9f). The
// failures1 page reads a composite of:
//   • InputConfig / OutputConfig — eq-indexed (port-id per equipment slot)
//   • PwmConfig    — legacy `eqIdx:enabled:portId:duty` strings
//   • systemMode   — basicSetup.systemMode (potato vs onion)
//   • boardType / controllerVersion — historical AS2 fixed values
//   • failures     — 22-slot CSV preserving page-level positional indexing
//
// FailureSettings wire layout (per nova_messages.c::
// NovaMsg_SendFailureSettings, post-S9f proto rename):
//   1=fanMode 2=fanTimer 3=heatMode 4=heatTimer
//   5=refrigMode 6=refrigTimer 7=refrigFailMode
//   8=burnerMode 9=burnerTimer 10=humidTimer 11=climacellTimer
// The 22-slot page array reorders these into AS2 SaveFailures1 layout
// (see novaAdapter::dataStore.on('failureSettings') for the inverse).

export interface Failures1View {
	InputConfig: string[];
	OutputConfig: string[];
	PwmConfig: string[];
	systemMode: string;
	boardType: string;
	controllerVersion: string;
	failures: string[];
}

function buildFailures22Slot(fs: TagPayload[typeof TAG.FailureSettings]): string[] {
	const slots = new Array(22).fill('0');
	slots[0]  = String(fs.fanMode           ?? 0);  // [0]  FanMode
	slots[1]  = String(fs.fanTimer          ?? 0);  // [1]  FanTimer
	slots[2]  = String(fs.climacellMode     ?? 0);  // [2]  ClimacellMode
	slots[3]  = String(fs.climacellTimer    ?? 0);  // [3]  ClimacellTimer
	slots[4]  = String(fs.refrigMode        ?? 0);  // [4]  RefridgeMode
	slots[5]  = String(fs.refrigTimer       ?? 0);  // [5]  RefridgeTimer
	slots[6]  = String(fs.refrigFailMode    ?? 0);  // [6]  RefridgeRun
	slots[7]  = String(fs.refrigStagesMode  ?? 0);  // [7]  RefrStagesMode
	slots[8]  = String(fs.refrigStagesTimer ?? 0);  // [8]  RefrStagesTimer
	slots[9]  = String(fs.humidMode         ?? 0);  // [9]  HumidifiersMode
	slots[10] = String(fs.humidTimer        ?? 0);  // [10] HumidifiersTimer
	slots[11] = String(fs.auxMode           ?? 0);  // [11] AuxMode
	slots[12] = String(fs.auxTimer          ?? 0);  // [12] AuxTimer
	slots[13] = String(fs.heatMode          ?? 0);  // [13] HeatMode
	slots[14] = String(fs.heatTimer         ?? 0);  // [14] HeatTimer
	slots[15] = String(fs.cavityHeatMode    ?? 0);  // [15] CavityHeatMode
	slots[16] = String(fs.cavityHeatTimer   ?? 0);  // [16] CavityHeatTimer
	slots[17] = String(fs.burnerMode        ?? 0);  // [17] BurnerMode
	slots[18] = String(fs.burnerTimer       ?? 0);  // [18] BurnerTimer
	slots[19] = String(fs.lightsMode        ?? 0);  // [19] LightsMode
	slots[20] = String(fs.lightsTimer       ?? 0);  // [20] LightsTimer
	slots[21] = String(fs.lightsUnits       ?? 0);  // [21] LightsUnits (1=mins, 60=hours)
	return slots;
}

export const failures1Composite: Readable<Failures1View | null> = derived(
	[failureSettings, ioConfig, pwmChannelSettings, basicSetup],
	([$fs, $ioCfg, $pwm, $basic]) => {
		if (!$fs || !$ioCfg || !$pwm || !$basic) return null;
		return {
			InputConfig:  eqMapToEqIndexed($ioCfg.inputMap),
			OutputConfig: eqMapToEqIndexed($ioCfg.outputMap),
			PwmConfig:    buildPwmConfig($pwm.channels),
			systemMode:   String($basic.systemMode),
			boardType:    'AS2',
			controllerVersion: '3',
			failures:     buildFailures22Slot($fs),
		};
	}
);

// ─── network page composite ────────────────────────────────────────────
//
// Replaces the bridge `/iot/network` GET + `network-data` WS channel
// (deleted in S9g). The bridge composite was a stub anyway — Nova doesn't
// emit live peer telemetry, so the `NetworkMonitor` stride-12 array only
// ever contained {addr, name} pairs with empty live fields. We replicate
// the exact same shape from the typed `networkConfig` + `networkNodes`
// proto stores; once firmware adds peer-status broadcast we can extend
// the live fields here without touching the page.
//
// SIM-vs-PROD: peer telemetry (mode/alarm/setpoint/plenum/return/humid)
// is still empty here. Production will fill these in once Nova adds an
// equivalent of the legacy AS2 multi-panel discovery + status sync.

export interface NetworkData {
	NetworkMonitor: string[];
	ClientIpAdd: string[];
	LocalIpAdd: string[];
	LocalIpMask: string[];
}

export const networkComposite: Readable<NetworkData | null> = derived(
	[networkConfig, networkNodes],
	([$cfg, $nodes]) => {
		if (!$cfg) return null;
		const localIp   = $cfg.ipAddr || '127.0.0.1';
		const localMask = $cfg.ipMask || '255.255.255.0';
		const monitor: string[] = [];
		for (const n of ($nodes?.nodes ?? [])) {
			if (!n.ipAddr) continue;
			// stride-12: [addr, name, mode, alarm, set, plen, tempColor,
			//             ret, hum, humidColor, _, _]
			monitor.push(n.ipAddr, n.name ?? '', '', '', '', '', '', '', '', '', '', '');
		}
		return {
			NetworkMonitor: monitor,
			ClientIpAdd:   [localIp],
			LocalIpAdd:    [localIp],
			LocalIpMask:   [localMask],
		};
	}
);

// ─── ioconfig page composite ───────────────────────────────────────────
//
// Replaces the bridge's `/iot/ioconfig` GET (still served for the sim
// rs485Panel test fixture, but the SvelteKit UI now consumes this
// composite directly). Mirrors the bridge case shape from
// `dataCache.buildPageData('ioconfig')`:
//   • ioAvailable — "Orbit N:numOut:numIn:boardType" per storage-role
//     orbit board, sourced from the bridge-corrected OrbitStatus
//     frame (tag 120). In QEMU dev mode the bridge overrides
//     firmware's `connected` flags with REST-probe results before
//     re-emitting the tag — see novaAdapter::publishCorrectedOrbitStatus.
//   • config.outputConfig / inputConfig — port-indexed string arrays
//     (portId → eqIdx, or '-1' for unassigned), built from
//     IoConfig.outputMap/inputMap (which are eq-indexed on the wire).
//   • ioNames — IoEntry[] passed through from IoDefinition.
//   • systemMode — basicSetup.systemMode as string.

import type { IOConfigType } from './ioConfig.js';
import { OrbitRole } from '$proto/agristar/orbit.js';

export const ioConfigComposite: Readable<IOConfigType | null> = derived(
	[ioConfig, ioDefinition, basicSetup, orbitStatus],
	([$io, $def, $basic, $orbit]) => {
		if (!$io || !$def || !$basic) return null;
		const ioAvailable: string[] = [];
		if ($orbit) {
			const storage = $orbit.boards
				.filter((b) => b.connected && b.role === OrbitRole.ORBIT_ROLE_STORAGE_PB)
				.sort((a, z) => a.slot - z.slot);
			for (let i = 0; i < storage.length; i++) {
				// LP-AM2434 STORAGE board hardware: 10 DO + 11 DI (DI11 is
				// hardware-pinned to E-Stop in firmware). Same physical board
				// family as the CONTROLLER. Adding a second STORAGE just shows
				// up here as another row — each is its own independent 10x11
				// board.
				ioAvailable.push(`Orbit ${i + 1}:10:11:1`);
			}
		}
		return {
			ioAvailable,
			config: {
				outputConfig: eqMapToPortIndexed($io.outputMap),
				inputConfig:  eqMapToPortIndexed($io.inputMap),
			},
			ioNames: $def.entries.map((e) => ({
				index: e.index,
				name: e.name,
				mode: e.mode,
				ioPin: e.ioPin,
				renamable: e.renamable,
				visible: e.visible,
				ioType: e.ioType,
			})),
			systemMode: String($basic.systemMode),
		};
	}
);

// ─── basic page composite ──────────────────────────────────────────────
//
// Replaces the bridge's `/iot/basic` GET. Mirrors the legacy
// P2BasicSetupData CSV layout (which novaAdapter::on('basicSetup')
// joins from the BasicSetup proto), so the page's positional indexing
// (`basic[0]` storageName, `basic[1]` tempType, …) keeps working
// without any template changes:
//   [0]  storageName
//   [1]  tempType
//   [2]  mode
//   [3]  homePage
//   [4]  systemMode
//   [5]  language
//   [6]  masterIp
//   [7]  multiView
//   [8]  loginPw  (AES-encrypted with DH session secret)
//   [9]  localLogin
//   [10] animations
// Wrapped as `{ array }` to match the page's existing `ArrayResponse`
// prop type.
export interface BasicArrayView {
	array: string[];
}

export const basicComposite: Readable<BasicArrayView | null> = derived(
	basicSetup,
	($b) => {
		if (!$b) return null;
		const array = [
			$b.storageName ?? '',
			String($b.tempType ?? 0),
			String($b.mode ?? 0),
			$b.homePage ?? '',
			String($b.systemMode ?? 0),
			String($b.language ?? 0),
			$b.masterIp ?? '',
			String($b.multiView ?? 0),
			$b.loginPw ?? '',
			String($b.localLogin ?? 0),
			String($b.animations ?? 0),
		];
		return { array };
	}
);

// ─── Aux Program aggregator + auxiliary page composite ─────────────────
//
// Replaces the bridge `/iot/aux/all` GET (deleted alongside this
// migration) and its NextAux discovery walker. The firmware emits
// TAG.AuxProgramBundle (72) every 5 s containing every populated aux
// channel (one field-1 length-delim AuxProgramSettings submsg per
// channel). The bundle decoder lives in protoTags.ts; this store just
// re-keys the array by `auxIndex` so the page can splice in saves
// without index drift.
//
// Each new bundle frame is the FULL set — we replace the cached map
// rather than merge, so a channel that disappears upstream (e.g. the
// operator unassigned its IO mapping) drops out cleanly.

export const auxProgramsByIndex: Readable<
	ReadonlyMap<number, TagPayload[typeof TAG.AuxProgramBundle]['programs'][number]>
> = readable<ReadonlyMap<number, AuxProgramSettings>>(new Map(), (set) => {
	const releaseTag = protoStream.acquireTag(TAG.AuxProgramBundle);
	const releaseListener = protoStream.addListener((t, bytes) => {
		if (t !== TAG.AuxProgramBundle) return;
		try {
			const bundle = decodeTag(TAG.AuxProgramBundle, bytes);
			const next = new Map<number, AuxProgramSettings>();
			for (const p of bundle.programs) {
				if (p && typeof p.auxIndex === 'number') next.set(p.auxIndex, p);
			}
			set(next);
		} catch (err) {
			console.error('[protoStores] auxProgramsByIndex decode failed:', err);
		}
	});
	return () => {
		releaseListener();
		releaseTag();
	};
});

// Page-level composite: produces the same shape the legacy `/iot/aux/all`
// GET returned, so the auxiliary page renders without a template change.
//
//   InputConfig / OutputConfig — eq-indexed string arrays (port-id per
//                                 equipment slot); '-1' means unassigned.
//   IoNames                    — IoEntry[] passed through from IoDefinition.
//   systemMode                 — basicSetup.systemMode as string.
//   allAux                     — { auxProg: string[4], rules: Rule[6] }[]
//                                 one entry per defined aux output (eq 25..32),
//                                 in the same legacy CSV-flavored shape
//                                 the bridge's parseAuxProgram() returned.
//
// The "defined" filter mirrors apiRoutes.ts: aux N is included when its
// EQ_AUX1+N OutputConfig slot is not '-1'. A defined-but-uncached aux
// gets the same default-rules fallback the bridge used (RT_MANUAL on
// rule 0, RT_UNDEFINED on the rest).

import type { AuxProgramSettings } from '$proto/agristar/settings.js';
import type { Rule } from './auxOptions.js';

const EQ_AUX1 = 25;
const NUM_AUX = 8;

function buildAuxProg(p: AuxProgramSettings | undefined, eqIdx: number): string[] {
	if (!p) return [String(eqIdx), '100', '1', '1'];
	return [
		String(p.eqIndex || eqIdx),
		String(p.dutyCycle ?? 100),
		String(p.period ?? 1),
		String(p.units ?? 1),
	];
}

function buildRules(p: AuxProgramSettings | undefined): Rule[] {
	const rules: Rule[] = [];
	let showNext = true; // rule 0 is always visible
	for (let i = 0; i < 6; i++) {
		const r = p?.rules?.[i];
		const type      = r ? String(r.type)     : (i === 0 ? '0' : '255');
		const io        = r ? String(r.ioIndex)  : '0';
		const st        = r ? String(r.state)    : '0';
		const op        = r ? String(r.op)       : '0';
		const sensorVal = r ? String(r.sensorVal) : '0';
		const andOrWire = r ? String(r.andOr)    : (i === 0 ? '255' : '255');
		const ref       = r ? String(r.refIndex) : '0';
		// For sensor rules (type=4), split sensorValue into sen/diff based on state.
		// st='0' = absolute mode → sen=sensorValue, diff='0'
		// st='1' = reference mode → diff=sensorValue, sen='255'
		let sen = sensorVal;
		let diff = '0';
		if (type === '4' && st === '1') {
			diff = sensorVal;
			sen = '255';
		}
		const andOr: string = showNext ? andOrWire : '256';
		rules.push({
			type, io, st, op, sen, diff, andOr, ref,
			first: i === 0,
			sensorOption: '',
		});
		// Next rule visible only if current chains (AND=0 or OR=1).
		showNext = (andOr === '0' || andOr === '1');
	}
	return rules;
}

export interface AuxiliaryView {
	InputConfig: string[];
	OutputConfig: string[];
	IoNames: ReturnType<typeof identityIoEntry>[];
	systemMode: string;
	allAux: { auxProg: string[]; rules: Rule[] }[];
}

// Keep the IoEntry shape in sync with the existing pages (failures1,
// equipment, ioconfig — same one-liner on each composite).
function identityIoEntry(e: TagPayload[typeof TAG.IoDefinition]['entries'][number]) {
	return {
		index: e.index,
		name: e.name,
		mode: e.mode,
		ioPin: e.ioPin,
		renamable: e.renamable,
		visible: e.visible,
		ioType: e.ioType,
	};
}

export const auxiliaryComposite: Readable<AuxiliaryView | null> = derived(
	[auxProgramsByIndex, ioConfig, ioDefinition, basicSetup],
	([$aux, $ioCfg, $ioDef, $basic]) => {
		if (!$ioCfg || !$ioDef || !$basic) return null;
		const OutputConfig = eqMapToEqIndexed($ioCfg.outputMap);
		const InputConfig  = eqMapToEqIndexed($ioCfg.inputMap);
		const allAux: { auxProg: string[]; rules: Rule[] }[] = [];
		for (let i = 0; i < NUM_AUX; i++) {
			const eqIdx = EQ_AUX1 + i;
			if (eqIdx >= OutputConfig.length || OutputConfig[eqIdx] === '-1') continue;
			const p = $aux.get(i);
			allAux.push({
				auxProg: buildAuxProg(p, eqIdx),
				rules:   buildRules(p),
			});
		}
		return {
			InputConfig,
			OutputConfig,
			IoNames: $ioDef.entries.map(identityIoEntry),
			systemMode: String($basic.systemMode),
			allAux,
		};
	}
);

// ─── Aggregating analog-board store ─────────────────────────────────────
//
// The single-tag `analogBoard` store above only ever holds the most
// recent broadcast — but the firmware sends one AnalogBoard frame per
// physical board. Pages that need a full sensor list (outside, ramp,
// fanspeed) used to fall back to the legacy `/iot/<page>` endpoint to
// get the bridge's pre-aggregated SensorListData. This store does the
// same aggregation client-side, keyed by `address`, so all consumers
// get a complete `AnalogBoard[]` without a server round-trip.

import type { AnalogBoard, AnalogSensor } from '$proto/agristar/io.js';
import type { SensorInfo } from './analog.js';

export const analogBoards: Readable<AnalogBoard[]> = readable<AnalogBoard[]>([], (set) => {
	const map = new Map<number, AnalogBoard>();
	const releaseTag = protoStream.acquireTag(TAG.AnalogBoard);
	const releaseListener = protoStream.addListener((t, bytes) => {
		if (t !== TAG.AnalogBoard) return;
		try {
			const b = decodeTag(TAG.AnalogBoard, bytes) as AnalogBoard;
			if (!b || typeof b.address !== 'number') return;
			map.set(b.address, b);
			set(Array.from(map.values()).sort((a, z) => a.address - z.address));
		} catch (err) {
			console.error('[protoStores] analogBoards aggregator decode failed:', err);
		}
	});
	return () => {
		releaseListener();
		releaseTag();
	};
});

/**
 * Flattened sensor list derived from `analogBoards`. Mirrors the
 * bridge's `SensorListData` stride-6 layout but as typed `SensorInfo[]`.
 * `id` follows the same convention as `novaAdapter.rebuildAnalogAggregates`:
 *   id = (address - 1) * 4 + slot
 *
 * Disabled sensors are INCLUDED — consumers filter by `disabled` if needed
 * (matches legacy behaviour: outside dropdown shows all, ramp/fanspeed
 * dropdowns filter `!disabled` themselves).
 */
export const sensorList: Readable<SensorInfo[]> = derived(analogBoards, ($boards) => {
	const out: SensorInfo[] = [];
	for (const b of $boards) {
		const base = Math.max(0, (b.address ?? 0) - 1) * 4;
		for (const s of (b.sensors ?? []) as AnalogSensor[]) {
			out.push({
				id: base + (s.slot ?? 0),
				label: s.label ?? '',
				type: String(s.type ?? 255),
				value: Number.isFinite(s.value) ? s.value.toFixed(1) : null,
				offset: Number.isFinite(s.offset) ? String(s.offset) : '0.0',
				disabled: !!s.disabled,
			});
		}
	}
	return out;
});

/**
 * Legacy 25-element row representation of every present AnalogBoard,
 * derived from the typed `analogBoards` aggregator. Layout matches the
 * old `/iot/analog/all` + `/iot/sensors/unified` merge that
 * `level2/analog` consumed:
 *
 *   [0]=address  [1]=type  [2]=label  [3]=version  [4]=disabled
 *   per slot s in 0..3, base = 5 + s*5:
 *     [base+0]=type   [base+1]=label  [base+2]=offset(1dp)
 *     [base+3]=disabled  [base+4]=value(1dp or 'dis'/'--')
 *
 * Disabled sensors emit value 'dis'; missing/non-finite values emit '--'
 * (matches the old bridge convention). Boards are sorted by address.
 * Empty board list yields []; consumers should treat that as "not yet
 * hydrated" and stay in `wait`.
 */
export const analogBoardArrays: Readable<string[][]> = derived(analogBoards, ($boards) => {
	const rows: string[][] = [];
	for (const b of $boards) {
		const row: string[] = [
			String(b.address ?? 0),
			String(b.type ?? 0),
			String(b.label ?? ''),
			String(b.version ?? ''),
			b.disabled ? '1' : '0',
		];
		const slots: AnalogSensor[] = [];
		for (const s of (b.sensors ?? []) as AnalogSensor[]) {
			const idx = s.slot ?? 0;
			if (idx >= 0 && idx < 4) slots[idx] = s;
		}
		for (let i = 0; i < 4; i++) {
			const s = slots[i];
			if (!s) {
				row.push('255', '', '0.0', '0', '--');
				continue;
			}
			const dis = s.disabled ? '1' : '0';
			let val = '--';
			if (dis === '1') {
				val = 'dis';
			} else if (Number.isFinite(s.value)) {
				val = (s.value as number).toFixed(1);
			}
			const offset = Number.isFinite(s.offset) ? (s.offset as number).toFixed(1) : '0.0';
			row.push(String(s.type ?? 255), s.label ?? '', offset, dis, val);
		}
		rows.push(row);
	}
	return rows;
});
