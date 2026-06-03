/**
 * frontMatterComposite.ts — Phase 3 Proto-Direct Migration
 *
 * Client-side reconstruction of the legacy `frontMatterStore` shape
 * (`{main, panel, misc, AlarmData, refrigData, animations, localLogin,
 * hasLevel1Password, boardType, hasAux}`) from typed proto stores.
 *
 * Why this exists: the bridge's `buildFrontmatterData()` aggregates 13+
 * firmware proto messages into legacy positional CSV arrays that 10+
 * level1 pages read by fixed index (e.g. `$frontMatterStore.main[17]`).
 * Migrating those pages one-at-a-time off their legacy slots would
 * multiply work; instead this derived store reproduces the same shape
 * directly on the client, letting pages migrate incrementally to
 * typed fields without an intermediate WS round-trip.
 *
 * Same pattern as `equipmentComposite` (Phase 3 first move). Every
 * field read from a proto store is documented alongside its legacy
 * position so the bridge-side aggregate can be retired safely
 * (Phase 5 cleanup target: `dataCache.buildFrontmatterData()`).
 *
 * Sim-only overlays kept:
 *  - Sensor fallback: when SystemStatus fields arrive as '--' from
 *    QEMU (no real RS-485), use SensorData readings by SID.
 *  - VFD failure overlay: zero all equipment outputs when a VFD fault
 *    delay has elapsed (driven by VfdStatus + rs485Panel flag).
 *  - VFD alarms: merged in by the +layout.svelte poller, not here.
 *    AlarmData from this composite only covers firmware WarningReport.
 */

import { derived, type Readable } from 'svelte/store';
import {
	accountSettings,
	auxSwitches,
	basicSetup,
	co2Settings,
	cureSettings,
	equipmentStatus,
	failureSettings,
	fanRuntime,
	ioConfig,
	ioDefinition,
	masterSlaveSettings,
	plenumSettings,
	refrigSettings,
	runtimes,
	sensorData,
	sensorLabels,
	systemStatus,
	vfdStatus,
	warningReport
} from './protoStores.js';
import type { TagPayload } from './protoTags.js';
import { TAG } from './protoTags.js';
import { WARNING_KEYS } from './warningKeys.js';
import { ALARM_FAIL, DEFAULT_WARNING_TEXT } from './warningText.js';

// ─── FrontMatterShape ───────────────────────────────────────────────────
// Legacy shape that Svelte pages read by positional index. Documented
// slot-by-slot in dataCache.buildFrontmatterData() comments; this type
// exists so new consumers can see the contract at a glance.
export interface FrontMatterShape {
	main: string[];          // 40 elements — temps, setpoints, outputs, sensors
	panel: string[];         // 31 elements — runtimes, equipment status, mode
	misc: string[];          // board-type + misc flags
	AlarmData: string[];     // ['WARN_KEY=Human text', ...]
	refrigData: string[];    // raw refrig stage CSV (Phase 5: switch to proto)
	animations: string;      // 'true' | 'false'
	localLogin: string;      // 'true' | 'false'
	hasLevel1Password: string; // 'true' | 'false'
	hasAux: string;          // 'true' | 'false'
	boardType: string;       // 'Agri-Star' (always, in Constellation)
}

// Helper — SystemStatus floats may be NaN/0 when firmware has no reading;
// we render those as 'dis' to match legacy behaviour.
function fmtFloat(v: number | undefined, digits = 1): string {
	if (v === undefined || v === null || Number.isNaN(v)) return 'dis';
	// Firmware sends 0 for "no reading" as well — but we can't distinguish
	// a real 0 from a missing value here, so leave 0 alone and let the
	// sensor fallback overlay fix missing plenum/return/outside values.
	return v.toFixed(digits);
}

// Helper — render a numeric setpoint that the legacy bridge passes through
// as a raw string. Whole numbers stay as ints ('0', '95'), fractional values
// keep one decimal ('46.0', '50.7'). Matches `dataCache.getVal()` behaviour.
function fmtSet(v: number | undefined): string {
	if (v === undefined || v === null || Number.isNaN(v)) return '0';
	return Number.isInteger(v) ? String(v) : v.toFixed(1);
}

// ─── main[40] builder ───────────────────────────────────────────────────
// Note: no client-side sensor fallback. The firmware's
// `nova_thread_overrides.c::ReadAnalogBoards()` is the single source of
// truth for MainData values; when an RS-485 board is absent the firmware
// itself populates SystemStatus from internal SensorData, so we don't
// need to re-do that translation here.
function buildMain(
	ss: TagPayload[typeof TAG.SystemStatus] | null,
	plenum: TagPayload[typeof TAG.PlenumSettings] | null,
	cure: TagPayload[typeof TAG.CureSettings] | null,
	co2: TagPayload[typeof TAG.Co2Settings] | null,
	vfd: TagPayload[typeof TAG.VfdStatus] | null
): string[] {
	const main = new Array<string>(40).fill('dis');

	// Raw values from SystemStatus (1:1 with proto fields).
	const plenumT  = fmtFloat(ss?.plenumTemp);
	const plenumH  = fmtFloat(ss?.plenumHumid);
	const outsideT = fmtFloat(ss?.outsideTemp);
	const outsideH = fmtFloat(ss?.outsideHumid);
	const returnT  = fmtFloat(ss?.returnTemp);
	const returnH  = fmtFloat(ss?.returnHumid);
	const co2Read  = fmtFloat(ss?.co2Level, 0);

	main[0]  = plenumT;                                 // plenum temp raw
	main[1]  = plenumH;                                 // plenum humid raw
	main[2]  = plenumT;                                 // plenum temp (again — legacy slot)
	// main[3] = plenum temp setpoint (PgmData[0] = PlenumSettings.tempSetpoint).
	// Bridge always serialises with one decimal (`tempSetpoint.toFixed(1)`),
	// so mirror that here even for whole-number setpoints.
	main[3]  = plenum ? plenum.tempSetpoint.toFixed(1) : 'dis';
	main[4]  = '0';
	main[5]  = plenumH;                                 // plenum humid
	// main[6] = humidity setpoint (PgmData[1] = PlenumSettings.humidSetpoint, integer %).
	main[6]  = plenum ? String(plenum.humidSetpoint) : 'dis';
	main[7]  = outsideT;
	main[8]  = outsideH;
	main[9]  = returnT;
	main[10] = returnH;
	// main[11] = cure start temp (rawMain[6] in legacy = SystemStatus.startTemp).
	main[11] = fmtFloat(ss?.startTemp);
	// main[12] = cure start humidity (rawMain[5] in legacy = SystemStatus.remoteHumid).
	main[12] = fmtFloat(ss?.remoteHumid);
	main[13] = '0';
	main[14] = ss?.fanSpeed ?? 'Off';
	main[15] = ss?.coolOutput ?? '0';
	main[16] = ss?.coolLabel === 'Auto' ? '0' : (ss?.coolLabel ?? '0');
	main[17] = co2Read;
	main[18] = ss?.burnerOutput ?? '0';
	main[19] = '0'; main[20] = '0'; main[21] = '0'; main[22] = '0'; main[23] = '0';
	main[24] = '0'; main[25] = '0';
	main[26] = '0'; main[27] = '0'; main[28] = '0';
	main[29] = '0';
	main[30] = '0';
	// main[31] = return humid calc (rawMain[14] = SystemStatus.calcHumid).
	// Legacy bridge passes the raw firmware string through; firmware emits
	// integer values so render as int.
	main[31] = fmtSet(ss?.calcHumid);
	main[32] = co2?.mode && co2.mode !== 0 ? '1' : '0';
	// main[33] = CO2 setpoint (Co2PurgeData[4] = Co2Settings.cycleOrSet).
	// Falsy when proto has not arrived yet — keep legacy fallback of 1000.
	main[33] = String(co2?.cycleOrSet ?? 1000);
	main[34] = ss?.coolLabel === '1' ? (ss?.coolOutput ?? '0') : '0';
	main[35] = 'dis';
	main[36] = 'dis';
	main[37] = 'dis';
	main[38] = '--';
	main[39] = '--';

	// CureSettings overlay deliberately omitted: the legacy bridge composer
	// uses SystemStatus.startTemp / remoteHumid for main[11/12] (which the
	// firmware overwrites with the cure setpoints when air cure runs). The
	// dedicated CureSettings proto carries the *programmed* values, which
	// drift from what's currently active. Pages that need the programmed
	// values should subscribe to `cureSettings` directly.

	// VFD failure overlay: if any drive has a fault active, zero the
	// fan/cooling/burner outputs to match firmware failure mode.
	const vfdFault = !!vfd?.drives?.some((d) => d.faultActive);
	if (vfdFault) {
		main[14] = 'Off';
		main[15] = '0';
		main[18] = '0';
	}

	return main;
}

// ─── panel[31] builder ──────────────────────────────────────────────────
// Front-matter panel reads typed `EquipState` items via `eqIndex`,
// not via positional CSV slots. The legacy bridge flattened items into
// a 102-byte AS2-style array, which made every slot read coincidentally
// correct (and most of them weren't). Now: one EquipState per eqIndex,
// looked up directly. Switches that used to be synthesized from byte
// offsets are now read straight off `EquipState.remoteOff`.
import { EQ, REMOTE } from './equipmentEnum.js';

function buildPanel(
	ss: TagPayload[typeof TAG.SystemStatus] | null,
	eq: TagPayload[typeof TAG.EquipmentStatus] | null,
	ioCfg: TagPayload[typeof TAG.IoConfig] | null,
	basic: TagPayload[typeof TAG.BasicSetup] | null,
	fr: TagPayload[typeof TAG.FanRuntime] | null,
	rt: TagPayload[typeof TAG.Runtimes] | null,
	ms: TagPayload[typeof TAG.MasterSlaveSettings] | null,
	vfd: TagPayload[typeof TAG.VfdStatus] | null,
	fs: TagPayload[typeof TAG.FailureSettings] | null
): string[] {
	const panel = new Array<string>(31).fill('0');

	// Build a quick eqIndex → EquipState lookup; entries that firmware
	// did not emit return undefined and the panel slot stays '0'.
	const byIdx = new Map<number, NonNullable<typeof eq>['items'][number]>();
	if (eq) for (const it of eq.items) byIdx.set(it.eqIndex, it);
	const outOn = (i: number): string => (byIdx.get(i)?.outputOn ? '1' : '0');
	const inOn  = (i: number): string => (byIdx.get(i)?.inputOn  ? '1' : '0');
	// Software-switch render key for the panel (legacy mapping):
	//   off   → '0' (panel shows OFF in red)
	//   manual→ '2' (panel shows MANUAL)
	//   auto  → '1' (panel shows AUTO)
	const swKey = (i: number): string => {
		if (!isConfigured(i)) return '0';
		const r = byIdx.get(i)?.remoteOff ?? REMOTE.AUTO;
		return r === REMOTE.OFF ? '0' : r === REMOTE.MANUAL ? '2' : '1';
	};
	const UNASSIGNED_PORTS = new Set([0, 255, 0xffff, 0xffffffff]);
	function isConfigured(eqIdx: number): boolean {
		const p = ioCfg?.outputMap[eqIdx];
		return p !== undefined && !UNASSIGNED_PORTS.has(p);
	}

	// [0] — blend/available temperature (SystemStatus.startTemp, legacy MainData[6])
	panel[0] = fmtFloat(ss?.startTemp);

	// [1] — daily fan runtime in hours (formatted as "H.M" tenths-of-hour).
	// Legacy bridge composer reads cache key `DailyFanRun` which `novaAdapter`
	// never writes (it stores the data under `FanRunTimeData` instead), so
	// the bridge falls back to literal '0.0' for every frame today. We pull
	// directly from the FanRuntime proto (dailyHours + dailyMins / 60) and
	// emit the same "X.Y" format the home page expects. Falls back to '0.0'
	// when the proto hasn't arrived yet (matches legacy behaviour).
	if (fr) {
		const tenths = Math.floor(((fr.dailyMins ?? 0) % 60) / 6);
		panel[1] = `${fr.dailyHours ?? 0}.${tenths}`;
	} else {
		panel[1] = '0.0';
	}

	// [2..7] — total runtimes: fan, refrig, cool, recirc, cure, standby.
	// Fan total hours live in the FanRuntime proto (separate broadcast).
	// The other slots are per-mode accumulators the firmware does not
	// currently expose — fill zeroes (same as QEMU today).
	// The Runtimes proto carries the 48-slot RUN-MODE SCHEDULE
	// (slot/mode), not equipment hour counters — do not consume `rt` here.
	panel[2] = String(fr?.totalHours ?? 0);
	panel[3] = '0'; panel[4] = '0'; panel[5] = '0'; panel[6] = '0'; panel[7] = '0';
	void rt;

	// [8] — system mode (0=potato, 1=onion)
	panel[8] = String(basic?.systemMode ?? 0);

	// [9..12] — climacell equipment, indexed by EQ enum:
	//   panel[9]  = fan software-switch state
	//   panel[10] = climacell software-switch state
	//   panel[11] = climacell input contact (raw DI)
	//   panel[12] = climacell output (DO readback)
	panel[9]  = swKey(EQ.FAN);
	panel[10] = isConfigured(EQ.CLIMACELL) || isConfigured(EQ.BURNER) ? swKey(EQ.CLIMACELL) : '0';
	// panel[11] = climacell "proved" input. The home-page Climacell
	// component renders the running graphic only when this is
	// INPUT_GOOD ('1' = DI active/high — modern active-high
	// convention, see mode.ts where the legacy '0=good' was flipped
	// 2026-06-03). When the operator's Level 2 Failures 1
	// ClimacellMode is 'None' (0) we force INPUT_GOOD so the graphic
	// tracks output+switch alone. Any other failure mode passes the
	// real DI through so a mapped proving contact (active-high when
	// closed/proving good) gates the graphic correctly.
	const climacellProveDisabled = (fs?.climacellMode ?? 0) === 0;
	panel[11] = climacellProveDisabled ? '1' : inOn(EQ.CLIMACELL);
	panel[12] = outOn(EQ.CLIMACELL);
	// [13] = humid software-switch (head1 stands in as the family proxy)
	panel[13] =
		isConfigured(EQ.HUMID_HEAD1) || isConfigured(EQ.HUMID_HEAD2) || isConfigured(EQ.HUMID_HEAD3)
			? swKey(EQ.HUMID_HEAD1)
			: '0';

	// [14..17] humid #1, [18..21] humid #2, [22..25] humid #3.
	// [14/18/22] = port assignment for the head equipment ('-1' if unassigned),
	// other slots = head/pump output and input states.
	const portForEq = (eqId: number): string => {
		const p = ioCfg?.outputMap[eqId];
		if (p === undefined || UNASSIGNED_PORTS.has(p)) return '-1';
		return String(p);
	};
	panel[14] = portForEq(EQ.HUMID_HEAD1);
	panel[15] = inOn(EQ.HUMID_HEAD1);
	panel[16] = outOn(EQ.HUMID_HEAD1);
	panel[17] = outOn(EQ.HUMID_PUMP1);

	panel[18] = portForEq(EQ.HUMID_HEAD2);
	panel[19] = inOn(EQ.HUMID_HEAD2);
	panel[20] = outOn(EQ.HUMID_HEAD2);
	panel[21] = outOn(EQ.HUMID_PUMP2);

	panel[22] = portForEq(EQ.HUMID_HEAD3);
	panel[23] = inOn(EQ.HUMID_HEAD3);
	panel[24] = outOn(EQ.HUMID_HEAD3);
	panel[25] = outOn(EQ.HUMID_PUMP3);

	// [26..29] — bay lights.
	// panel[28]/[29] feed the home-page Baylight icon. Bay lights are
	// wired "3-way switch" style: firmware drives an output coil
	// (toggle command), and the light starter's current-sensing
	// relay reports the ACTUAL state through the INPUT. So the bulb
	// icon should track `inOn` (current is flowing — lights really
	// on) not `outOn` (coil just commanded). If a bulb burns out or
	// the breaker trips, output stays high while inputOn drops to
	// false and the icon correctly hides.
	panel[26] = portForEq(EQ.LIGHTS1);
	panel[27] = portForEq(EQ.LIGHTS2);
	panel[28] = inOn(EQ.LIGHTS1);
	panel[29] = inOn(EQ.LIGHTS2);

	// [30] — master/slave (0=none, 1=master, 2=slave)
	panel[30] = String(ms?.mode ?? 0);

	// VFD failure overlay — zero humidifier/light outputs too.
	const vfdFault = !!vfd?.drives?.some((d) => d.faultActive);
	if (vfdFault) {
		panel[10] = '0'; panel[12] = '0'; panel[13] = '0';
		panel[16] = '0'; panel[17] = '0';
		panel[20] = '0'; panel[21] = '0';
		panel[24] = '0'; panel[25] = '0';
	}

	return panel;
}

// ─── AlarmData builder ──────────────────────────────────────────────────
// Mirrors the bridge pipeline: WarningReport → novaAdapter filter →
// translateAlarmData. We collapse those two steps into one here.
//
//   1. Drop orbit-domain warnings (code >= 0x100). Bridge does this in
//      `novaAdapter.ts::dataStore.on('warningReport')`.
//   2. Drop severity == 0 (FM_NONE — not an active alarm).
//   3. Drop WARN_MODECHANGE (email-only in legacy GellertServerD).
//   4. Drop WARN_SDCARD* (no SD card on Constellation; logging is on the Pi5).
//   5. Look up the human-readable text from DEFAULT_WARNING_TEXT, falling
//      back to the key itself when missing.
//   6. Bitmapped warnings (WARN_NEWBOARD, WARN_COMMERR, WARN_SYSCONFIG_EQ,
//      WARN_AUX, WARN_NO_OUTPUT, etc.) are NOT yet expanded here — the
//      proto field carries `eqIndex` rather than the legacy bitmap fields,
//      so the per-bit fan-out is a Phase 5 concern.  The few alarms today
//      that need it (e.g. WARN_NEWBOARD: 1) currently render as the bare
//      key=text pair, which is good enough for the home page badge counter
//      and the alarm list overlay.
//
// Fan-failure alarms originating from VFD-controlled fans arrive here
// through the standard WARN_FAN path: Nova firmware
// (nova_failures.c::FanFailChk + nova_vfd.c) raises WARN_FAN with the
// faulted drive index in the eqIo slot. No client-side merge needed.
function buildAlarmData(wr: TagPayload[typeof TAG.WarningReport] | null): string[] {
	if (!wr) return [];
	const out: string[] = [];
	for (const w of wr.warnings) {
		if (w.code >= 0x100) continue;
		if (!w.severity) continue;
		const key = WARNING_KEYS[w.code] ?? `WARN_UNKNOWN_${w.code}`;
		if (key === 'WARN_MODECHANGE') continue;
		if (key.startsWith('WARN_SDCARD')) continue;
		const text = w.message || DEFAULT_WARNING_TEXT[key] || key;
		out.push(`${key}=${text}`);
	}
	// Use ALARM_FAIL exposure to satisfy the linter without forcing it into
	// every alarm string — it's part of the public legacy contract and will
	// be needed when bitmapped alarm expansion lands.
	void ALARM_FAIL;
	return out;
}

// ─── refrigData builder ─────────────────────────────────────────────────
// The legacy `refrigData` was an 8-element string array; entry i held the
// IO-port assignment for refrig stage i+1, or '-1' if the stage had no
// output configured. Pages (level2/refrigeration, paging.ts) test
// `refrigData[i] !== '-1'` to decide whether to render that stage.
//
// Source of truth in production: `Settings.EquipIo[EQ_REFRIG_STAGE1..8].Output`,
// which Nova firmware encodes into `IoConfig.outputMap` field 1 (eq-indexed).
// EQ_REFRIG_STAGE1..8 occupy enum indices 13..20 (see legacy
// SerialShift.h::equipment_io). IO_UNDEFINED is encoded as 0xFFFFFFFF.
//
// Note: the firmware uses non-`_force` `pb_uint32` for the outputMap, so
// any stage assigned to port 0 (DO0) will be missing from the wire payload
// — which is fine, because port 0 is reserved for EQ_FAN's green light
// in the seeded defaults and is never assigned to a refrig stage.
const EQ_REFRIG_STAGE1_INDEX = 13;
const REFRIG_STAGE_COUNT = 8;
const IO_UNDEFINED = 0xFFFFFFFF;

function buildRefrigData(io: TagPayload[typeof TAG.IoConfig] | null): string[] {
	const out: string[] = [];
	const map = io?.outputMap ?? [];
	for (let i = 0; i < REFRIG_STAGE_COUNT; i++) {
		const port = map[EQ_REFRIG_STAGE1_INDEX + i];
		const assigned = port !== undefined && port !== IO_UNDEFINED;
		out.push(assigned ? String(port) : '-1');
	}
	return out;
}

// ─── Derived composite ──────────────────────────────────────────────────
// Null until every required store has produced its first frame. Optional
// stores (VfdStatus) are allowed to remain null.
export const frontMatterComposite: Readable<FrontMatterShape | null> = derived(
	[
		systemStatus,
		equipmentStatus,
		sensorData,
		sensorLabels,
		auxSwitches,
		basicSetup,
		ioConfig,
		ioDefinition,
		plenumSettings,
		cureSettings,
		co2Settings,
		refrigSettings,
		masterSlaveSettings,
		accountSettings,
		fanRuntime,
		runtimes,
		warningReport,
		vfdStatus,
		failureSettings
	],
	([
		$ss,
		$eq,
		_sd,
		_sl,
		$aux,
		$basic,
		$ioCfg,
		_ioDef,
		$plenum,
		$cure,
		$co2,
		$refrig,
		$master,
		$account,
		$fr,
		$rt,
		$warn,
		$vfd,
		$fs
	]) => {
		// Gate on the set of stores the legacy WS channel carried
		// unconditionally. Absent optional stores don't block emission.
		if (!$ss || !$eq || !$basic || !$ioCfg) return null;

		const main = buildMain($ss, $plenum, $cure, $co2, $vfd);
		const panel = buildPanel($ss, $eq, $ioCfg, $basic, $fr, $rt, $master, $vfd, $fs);

		// `misc` is consumed by `routes/+page.svelte` only as `misc[0]` for
		// boardType. Constellation hardware is always Agri-Star (the legacy
		// AS2/Agri-Star split was about the absent MiniIO expansion board),
		// so we publish a one-element array regardless of $misc availability.
		const miscArr = ['Agri-Star'];

		// hasAux: the legacy bridge synthesizes EquipAuxData as a colon-joined
		// per-aux assignment matrix ("0:5:5:5:5:5:5:5:5,...") and tests with
		// `split(',').some(v => v !== '0')` — which is ALWAYS truthy because
		// of the colons. We mirror that quirk so the home page renders the
		// same icon set; pages that need the real "any aux configured" answer
		// should consult `$auxSwitches` + `$ioConfig.outputMap` directly.
		// Suppress unused-import warnings — `$aux` is what proves AuxSwitches
		// is actually arriving on the wire, even though we don't read it.
		void $aux;
		const hasAux = 'true';
		const animations = $basic.animations ? 'true' : 'false';
		const localLogin = $basic.localLogin ? 'true' : 'false';
		// hasLevel1Password: the legacy bridge composer reads cache key
		// `UserAccounts[11]` which `novaAdapter` never writes (it stores the
		// data under `AcctData` instead), so the bridge always reports
		// 'false'. Earlier `frontMatterComposite` mirrored that bug for
		// parity-test purposes — but it's a SECURITY regression: the home
		// page +layout.svelte:478 writes this into `$keysStore.hasLevel1Password`,
		// which gates password prompts everywhere. Mirroring 'false'
		// disabled all prompts until the user happened to open the accounts
		// page (which independently sets the flag from `passwordDefined`).
		// Apr 20 2026: source from the real proto (`AccountSettings.passwordDefined`).
		const hasLevel1Password = $account?.passwordDefined ? 'true' : 'false';

		return {
			main,
			panel,
			misc: miscArr,
			AlarmData: buildAlarmData($warn),
			refrigData: buildRefrigData($ioCfg),
			animations,
			localLogin,
			hasLevel1Password,
			hasAux,
			boardType: 'Agri-Star'
		};
	}
);
