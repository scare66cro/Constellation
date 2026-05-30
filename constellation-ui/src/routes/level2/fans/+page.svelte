<script lang="ts">
	import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import { onMount, onDestroy } from "svelte";
	import { navigationStore, heightsStore } from "$lib/store";
	import { getHttpUrl } from "$lib/business/util";

	$: availableHeight = $heightsStore.main - $heightsStore.header - $heightsStore.footer - 20;
	// Phase 2 of proto-direct redesign: live drive metrics now arrive via
	// the bridge-emitted VfdStatus proto (envelope tag 200), eliminating
	// the 5 s `/vfd/fans` HTTP poll. Writes (control/param/meta/inject-fault)
	// still use the existing REST endpoints — Phase 3 will tackle those.
	import { vfdStatus, fanSpeedSettings as fanSpeedSettingsStore } from "$lib/business/protoStores";
	import type { VfdDrive } from "$proto/agristar/vfd.js";
	import { t } from "svelte-i18n";

	// Drives now arrive via the vfdStatus proto store; no SSR loader needed.

	type VFDManufacturer = 'abb-acs310' | 'abb-acs380' | 'phase-tech-dxl' | 'generic';

	interface Drive {
		unitId: number;
		online: boolean;
		label: string;
		manufacturer: VFDManufacturer;
		controlWord: number;
		speedRefPercent: number;
		statusWord: number;
		actualSpeedPercent: number;
		outputFreqHz: number;
		freqRefHz: number;
		motorSpeedRpm: number;
		motorCurrentA: number;
		motorTorquePercent: number;
		motorPowerkW: number;
		dcBusVoltage: number;
		outputVoltage: number;
		driveTemp: number;
		faultCode: number;
		minFreqHz: number;
		maxFreqHz: number;
		rampUpTime: number;
		rampDownTime: number;
		ratedCurrentA: number;
		ratedVoltage: number;
		ratedFreqHz: number;
		ratedSpeedRpm: number;
		ratedPowerkW: number;
		running: boolean;
		faulted: boolean;
		atReference: boolean;
		direction: number;
	}

	// ── Manufacturer metadata ──
	const MANUFACTURERS: { value: VFDManufacturer; label: string }[] = [
		{ value: 'abb-acs310', label: 'ABB ACS310' },
		{ value: 'abb-acs380', label: 'ABB ACS380' },
		{ value: 'phase-tech-dxl', label: 'Phase Technologies DXL' },
		{ value: 'generic', label: 'Generic / Demo' },
	];

	// ACS310 fault lookup
	const ABB_FAULTS: Record<number, string> = {
		1:  'Overcurrent',           2:  'DC Overvoltage',       3:  'Device Overtemp',
		4:  'Short Circuit',          5:  'Analog Input Loss',    6:  'Overacceleration',
		7:  'Motor Overtemp',         8:  'Motor Stall',          9:  'Underload',
		10: 'Output Phase Loss',      11: 'Input Phase Loss',     12: 'Earth Fault',
		13: 'Supply Phase Loss',      14: 'Charge Circuit',       15: 'Output Phase Fault',
		16: 'Safe Torque Off (STO)',   17: 'PTC Thermistor',      18: 'MCB Overtemp',
		20: 'Brake Chopper Short',    21: 'Brake Chopper Overload', 22: 'Power Unit',
		23: 'Surge / Thunderbolt',    25: 'Motor Controller',     26: 'IGBT Overtemp',
		28: 'Supply Overvoltage',     29: 'Ext Thermistor',       30: 'SMPS Fault',
		31: 'EEPROM Fault',           32: 'Thyristor / SCR',      33: 'Measurement Fault',
		34: 'Internal Bus Comm',      35: 'Application Fault',    36: 'Panel Comm Loss',
		37: 'Fieldbus Comm Loss',     38: 'Motor Phase Order',    39: 'Ext Device Comm Loss',
		40: 'Device Identification',  41: 'Internal Power',       42: 'Position Error',
		43: 'Encoder Fault',          44: 'Resolver Fault',       45: 'Overspeed',
		48: 'Power Limit',            50: 'External Fault 1',     51: 'External Fault 2',
		52: 'External Fault 3',       53: 'Modbus Comm Loss',     55: 'Feedback Loss',
		56: 'Fieldbus Comm Loss B',   57: 'Timer Fault',          58: 'STO Input Wiring',
		59: 'Safe Speed',             60: 'AO Supervision',       61: 'Speed Error',
		62: 'Torque Open Loop',       63: 'Brake Control',        64: 'Overcurrent (HW)',
		65: 'DC Overvoltage (HW)',    66: 'DC Undervoltage (HW)', 80: 'Drive Overtemp 2',
		81: 'Fan Fault',              90: 'ID Run Incomplete',    91: 'ID Run Warning',
		92: 'Motor Model Fault',      95: 'FW Update Error',
	};

	// DXL fault lookup
	const DXL_FAULTS: Record<number, string> = {
		1: 'Overcurrent',  2: 'Overvoltage',  3: 'Undervoltage',
		4: 'Overtemperature', 5: 'Input Phase Loss', 6: 'Voltage Imbalance',
		7: 'Ground Fault', 8: 'Motor Overload', 9: 'Communication Loss',
		10: 'External Trip',
	};

	// ── Metric definitions ──
	// Units after normalization: current=0.01A, torque=0.01%, power=0.01kW,
	// voltage=0.1V, dcBus=1V, temp=0.1°C, freq=0.1Hz, speed=0-10000 (0.01%)
	interface MetricDef {
		key: string;
		label: string;
		unit: string;
		format: (d: Drive) => string;
	}

	// 8 primary metrics — always visible on every drive card
	const PRIMARY_METRICS: MetricDef[] = [
		{ key: 'speed',   label: 'Speed',       unit: '%',   format: d => `${(d.actualSpeedPercent / 100).toFixed(1)}` },
		{ key: 'freq',    label: 'Frequency',   unit: 'Hz',  format: d => `${(d.outputFreqHz / 10).toFixed(1)}` },
		{ key: 'current', label: 'Current',     unit: 'A',   format: d => `${(d.motorCurrentA / 100).toFixed(2)}` },
		{ key: 'power',   label: 'Power',       unit: 'kW',  format: d => `${(d.motorPowerkW / 100).toFixed(2)}` },
		{ key: 'torque',  label: 'Torque',      unit: '%',   format: d => `${(d.motorTorquePercent / 100).toFixed(1)}` },
		{ key: 'outputV', label: 'Output V',    unit: 'V',   format: d => `${(d.outputVoltage / 10).toFixed(0)}` },
		{ key: 'dcBus',   label: 'DC Bus',      unit: 'V',   format: d => `${d.dcBusVoltage}` },
		{ key: 'temp',    label: 'Temperature',  unit: '°C',  format: d => `${(d.driveTemp / 10).toFixed(1)}` },
	];

	// Extra metrics — revealed per-card via "Show More"
	const EXTRA_METRICS: MetricDef[] = [
		{ key: 'direction', label: 'Direction',  unit: '',    format: d => d.direction ? 'REV' : 'FWD' },
		{ key: 'atRef',     label: 'At Ref',     unit: '',    format: d => d.atReference ? '✓' : '—' },
		{ key: 'rpm',       label: 'RPM',        unit: 'rpm', format: d => `${d.motorSpeedRpm}` },
		{ key: 'speedRef',  label: 'Speed Ref',  unit: '%',   format: d => `${(d.speedRefPercent / 100).toFixed(1)}` },
		{ key: 'faultCode', label: 'Fault',      unit: '',    format: d => d.faultCode ? faultName(d.faultCode, d.manufacturer || 'generic') : 'None' },
		{ key: 'cw',        label: 'Control Word',unit: '',   format: d => cwLabel(d.controlWord) },
		{ key: 'sw',        label: 'Status Word', unit: '',   format: d => swLabel(d.statusWord) },
		{ key: 'rampUp',    label: 'Accel Ramp', unit: 's',   format: d => `${(d.rampUpTime / 10).toFixed(1)}` },
		{ key: 'rampDown',  label: 'Decel Ramp', unit: 's',   format: d => `${(d.rampDownTime / 10).toFixed(1)}` },
		{ key: 'minFreq',   label: 'Min Freq',   unit: 'Hz',  format: d => `${(d.minFreqHz / 10).toFixed(1)}` },
		{ key: 'maxFreq',   label: 'Max Freq',   unit: 'Hz',  format: d => `${(d.maxFreqHz / 10).toFixed(1)}` },
		{ key: 'ratedP',    label: 'Rated Power', unit: 'HP',  format: d => `${(d.ratedPowerkW / 100).toFixed(2)}` },
		{ key: 'ratedV',    label: 'Rated V',     unit: 'V',   format: d => `${d.ratedVoltage}` },
		{ key: 'ratedA',    label: 'Rated A',     unit: 'A',   format: d => `${(d.ratedCurrentA / 100).toFixed(2)}` },
		{ key: 'ratedHz',   label: 'Rated Freq',  unit: 'Hz',  format: d => `${(d.ratedFreqHz / 10).toFixed(1)}` },
		{ key: 'ratedRpm',  label: 'Rated RPM',   unit: 'rpm', format: d => `${d.ratedSpeedRpm}` },
	];

	// Per-drive state for expanded metrics view
	let moreMetrics: Record<number, boolean> = {};

	let title = 'VFD Drives';
	let ready = false;
	let drives: Drive[] = [];
	let pollCount = 0;
	let lastPollTime = '';
	let sliderValues: Record<number, number> = {};
	let expandedDrives: Record<number, boolean> = {};
	let showSetAll = false;
	let showCalc = false;
	let costPerKwh = 0.11;
	let hoursPerDay = 24;
	let setAllSpeed = 5000;
	// Fan speed setpoints (max/min/refrig/recirc + current cooling %) come
	// from the FanSpeedSettings proto store (envelope tag 2). Reactive so
	// the buttons update without a manual refresh — the legacy /iot/fanspeed
	// fetch + Refresh/Load buttons have been retired.
	$: fanSpeedDisplay = $fanSpeedSettingsStore ? {
		max:     $fanSpeedSettingsStore.maxSpeed     ?? 0,
		min:     $fanSpeedSettingsStore.minSpeed     ?? 0,
		refrig:  $fanSpeedSettingsStore.refrigSpeed  ?? 0,
		recirc:  $fanSpeedSettingsStore.recircSpeed  ?? 0,
		current: $fanSpeedSettingsStore.prevSpeed    ?? 0,
	} : null;
	let paramsExpanded: Record<number, boolean> = {};
	let fanControlMode: 'legacy' | 'vfd' = 'legacy';

	function updateDrives(d: Drive[]) {
		drives = d;
		for (const drv of drives) {
			if (!(drv.unitId in sliderValues)) {
				sliderValues[drv.unitId] = drv.speedRefPercent;
			}
		}
	}

	// VfdDrive (proto) → Drive (page) adapter. The proto wire fields keep
	// integer ×10 / ×100 scaling so the wire payload is small; this maps
	// them back to the snapshot units the existing display logic expects.
	function protoToDrive(d: VfdDrive): Drive {
		return {
			unitId: d.address,
			online: d.connected !== 0,
			label: d.label,
			manufacturer: (d.manufacturer || 'generic') as VFDManufacturer,
			controlWord: d.controlWord,
			speedRefPercent: d.speedRefPercent,
			statusWord: d.statusWord,
			actualSpeedPercent: d.actualSpeedPercent,
			outputFreqHz: d.outputFreqHzX10,
			freqRefHz: d.freqRefHzX10,
			motorSpeedRpm: d.motorSpeedRpm,
			motorCurrentA: d.motorCurrentAX100,
			motorTorquePercent: d.motorTorquePctX100,
			motorPowerkW: d.motorPowerKwX100,
			dcBusVoltage: d.busVoltage,
			outputVoltage: d.outputVoltageX10,
			driveTemp: d.driveTempX10,
			faultCode: d.faultCode,
			minFreqHz: d.minFreqHzX10,
			maxFreqHz: d.maxFreqHzX10,
			rampUpTime: d.rampUpTimeX10,
			rampDownTime: d.rampDownTimeX10,
			ratedCurrentA: d.ratedCurrentAX100,
			ratedVoltage: d.ratedVoltage,
			ratedFreqHz: d.ratedFreqHzX10,
			ratedSpeedRpm: d.ratedSpeedRpm,
			ratedPowerkW: d.ratedPowerKwX100,
			running: d.running !== 0,
			faulted: d.faultActive !== 0,
			atReference: d.atReference !== 0,
			direction: d.direction,
		};
	}

	// React to every push from /proto/stream. The bridge emits whenever any
	// drive's state changes (vfdClient.onUpdate hook in protoStream.ts), so
	// updates land in the UI without polling.
	$: if ($vfdStatus) {
		updateDrives($vfdStatus.drives.map(protoToDrive));
		pollCount++;
		lastPollTime = new Date().toLocaleTimeString();
	}

	onMount(() => {
		$navigationStore.data = getHttpUrl('/vfd/fans');
		$navigationStore.isDirty = () => false;

		// Load fan control mode config (one-shot; not part of live status)
		fetch(getHttpUrl('/vfd/fans/config'))
			.then(r => r.json())
			.then(j => { if (j.mode) fanControlMode = j.mode; })
			.catch(() => {});

		ready = true;
	});

	onDestroy(() => { /* vfdStatus store auto-unsubscribes on component destroy */ });

	// ── Actions (manufacturer-agnostic) ──

	async function sendAction(unitId: number, action: string, speedRefPercent?: number) {
		try {
			await fetch(getHttpUrl('/vfd/fans/control'), {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ unitId, action, speedRefPercent }),
			});
		} catch (e) { console.error('VFD action error:', e); }
	}

	async function writeParam(unitId: number, param: string, value: number) {
		try {
			await fetch(getHttpUrl('/vfd/fans/param'), {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ unitId, param, value }),
			});
		} catch (e) { console.error('VFD param write error:', e); }
	}

	async function writeMeta(unitId: number, field: string, value: string) {
		try {
			await fetch(getHttpUrl('/vfd/fans/meta'), {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ unitId, [field]: value }),
			});
		} catch (e) { console.error('VFD meta write error:', e); }
	}

	function startDrive(unitId: number) {
		sendAction(unitId, 'start', sliderValues[unitId] ?? 0);
		commandedDrives.add(unitId);
		commandedDrives = commandedDrives; // trigger reactivity
	}

	function stopDrive(unitId: number) {
		sendAction(unitId, 'stop');
		commandedDrives.delete(unitId);
		commandedDrives = commandedDrives;
	}

	function resetFault(unitId: number) {
		sendAction(unitId, 'reset');
	}

	async function injectFault(unitId: number) {
		try {
			await fetch(getHttpUrl('/vfd/fans/inject-fault'), {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ unitId }),
			});
		} catch (e) { console.error('Inject fault error:', e); }
	}

	async function toggleFanControlMode() {
		const newMode = fanControlMode === 'vfd' ? 'legacy' : 'vfd';
		try {
			const res = await fetch(getHttpUrl('/vfd/fans/config'), {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ mode: newMode }),
			});
			const j = await res.json();
			if (j.ok) fanControlMode = newMode;
		} catch (e) { console.error('Fan control mode error:', e); }
	}

	function setSpeed(unitId: number, value: number) {
		sliderValues[unitId] = value;
		const drv = drives.find(d => d.unitId === unitId);
		if (drv?.running) sendAction(unitId, 'start', value);
	}

	function toggleDirection(unitId: number) {
		const drv = drives.find(d => d.unitId === unitId);
		if (!drv) return;
		sendAction(unitId, 'toggle-direction', sliderValues[unitId] ?? drv.speedRefPercent);
	}

	async function setAllDrives() {
		try {
			await fetch(getHttpUrl('/vfd/fans/set-all'), {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ action: 'start', speedRefPercent: setAllSpeed }),
			});
		} catch (e) { console.error('Set-all error:', e); }
	}

	async function stopAllDrives() {
		try {
			await fetch(getHttpUrl('/vfd/fans/set-all'), {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ action: 'stop' }),
			});
		} catch (e) { console.error('Stop-all error:', e); }
	}

	async function setAllParams(param: string, value: number) {
		try {
			await fetch(getHttpUrl('/vfd/fans/set-all'), {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ params: [{ param, value }] }),
			});
		} catch (e) { console.error('Set-all params error:', e); }
	}

	// Phase 5.1 cleanup: fetchFanSpeedSettings() removed — fanSpeedDisplay is
	// now reactively derived from the FanSpeedSettings proto store above.

	async function applyFanSpeedToAll(percent: number) {
		const ref = Math.round(percent * 100); // convert e.g. 25 → 2500
		try {
			await fetch(getHttpUrl('/vfd/fans/set-all'), {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ action: 'start', speedRefPercent: ref }),
			});
			// update local sliders
			for (const drv of drives) sliderValues[drv.unitId] = ref;
			sliderValues = sliderValues;
		} catch (e) { console.error('Apply fan speed error:', e); }
	}

	function faultName(code: number, mfg: VFDManufacturer): string {
		const table = mfg === 'phase-tech-dxl' ? DXL_FAULTS : ABB_FAULTS;
		return table[code] ?? `Code 0x${code.toString(16).padStart(4, '0')}`;
	}

	function cwLabel(cw: number): string {
		if (!cw) return 'None';
		const flags: string[] = [];
		if (cw & 0x0001) flags.push('RUN');
		if (cw & 0x0002) flags.push('OFF2');
		if (cw & 0x0004) flags.push('OFF3');
		if (cw & 0x0008) flags.push('ENA');
		if (cw & 0x0080) flags.push('RESET');
		if (cw & 0x0400) flags.push('EXT1');
		if (cw & 0x0800) flags.push('REV');
		return flags.join(' ') || `0x${cw.toString(16).padStart(4, '0')}`;
	}

	function swLabel(sw: number): string {
		if (!sw) return 'None';
		const flags: string[] = [];
		if (sw & 0x0001) flags.push('RDY');
		if (sw & 0x0002) flags.push('RUN_EN');
		if (sw & 0x0004) flags.push('RUN');
		if (sw & 0x0008) flags.push('FAULT');
		if (sw & 0x0010) flags.push('OFF2');
		if (sw & 0x0020) flags.push('OFF3');
		if (sw & 0x0100) flags.push('AT_REF');
		if (sw & 0x0200) flags.push('REMOTE');
		if (sw & 0x0800) flags.push('REV');
		if (sw & 0x1000) flags.push('DRV_OK');
		return flags.join(' ') || `0x${sw.toString(16).padStart(4, '0')}`;
	}

	function mfgLabel(mfg: VFDManufacturer): string {
		return MANUFACTURERS.find(m => m.value === mfg)?.label ?? mfg;
	}

	// Track which drives we've commanded from the UI
	let commandedDrives: Set<number> = new Set();

	function statusLabel(d: Drive): string {
		if (!d.online) return 'OFFLINE';
		if (d.faulted) return 'FAULT';
		const weControl = commandedDrives.has(d.unitId);
		if (d.running && weControl) return 'VFD ACTIVE';
		if (d.running) return 'MOTOR ON';
		if (weControl) return 'VFD STOPPING';
		return 'MOTOR OFF';
	}

	function statusColor(d: Drive): string {
		if (!d.online) return 'clr-gray';
		if (d.faulted) return 'clr-red';
		const weControl = commandedDrives.has(d.unitId);
		if (d.running && weControl) return 'clr-green';
		if (d.running) return 'clr-blue';
		return 'clr-amber';
	}

	function toggleMoreMetrics(unitId: number) {
		moreMetrics[unitId] = !moreMetrics[unitId];
		moreMetrics = moreMetrics;
	}

	// ── Power cost calculator (motorPowerkW is in 0.01kW) ──
	$: totalPowerKw = drives.reduce((sum, d) => sum + (d.running ? d.motorPowerkW / 100 : 0), 0);
	$: dailyKwh = totalPowerKw * hoursPerDay;
	$: monthlyKwh = dailyKwh * 30;
	$: monthlyCost = monthlyKwh * costPerKwh;
	$: perDriveCosts = drives.map(d => {
		const kw = d.running ? d.motorPowerkW / 100 : 0;
		const mKwh = kw * hoursPerDay * 30;
		return { unitId: d.unitId, label: d.label, kw, monthlyKwh: mKwh, monthlyCost: mKwh * costPerKwh, running: d.running };
	});

	// Speed vs. Cost curve (affinity law: P ∝ speed³)
	$: ratedKwPerDrive = drives.length > 0 ? (drives[0].ratedPowerkW / 100 * 0.7457) : 14.9;
	$: speedCurve = [20, 30, 40, 50, 60, 70, 80, 90, 100].map(pct => {
		const frac = pct / 100;
		const kw = ratedKwPerDrive * frac * frac * frac;
		const moKwh = kw * hoursPerDay * 30 * drives.length;
		return { pct, kw: kw * drives.length, moKwh, moCost: moKwh * costPerKwh };
	});
</script>

<GellertPage {title} {ready} level={2} name="fans">
	<div class="fans-page" style="max-height: {availableHeight}px">

		<!-- ═══ Top toolbar ═══ -->
		<div class="toolbar">
			<button class="tbtn" on:click={() => showSetAll = !showSetAll}>
				📋 {showSetAll ? 'Hide' : 'Set All Drives'}
			</button>
			<button class="tbtn tbtn-calc" on:click={() => showCalc = !showCalc}>
				💰 {showCalc ? 'Hide' : 'Power Cost'}
			</button>
			<button class="tbtn {fanControlMode === 'vfd' ? 'tbtn-vfd-active' : 'tbtn-vfd'}" on:click={toggleFanControlMode}>
				🔌 Fan Control: {fanControlMode === 'vfd' ? 'VFD Modbus' : 'Legacy I/O'}
			</button>
			<span class="toolbar-info">{drives.length} drive{drives.length !== 1 ? 's' : ''} discovered</span>
			<span class="toolbar-live" class:live-pulse={pollCount > 0}>🟢 {lastPollTime || '...'} (#{pollCount})</span>
		</div>

		<!-- ═══ Set-all panel ═══ -->
		{#if showSetAll}
			<Card>
				<div class="set-all-panel">
					<h3>Set All Drives</h3>
					<div class="set-all-row">
						<!-- svelte-ignore a11y-label-has-associated-control -->
						<label>Speed: {(setAllSpeed / 100).toFixed(1)}%</label>
						<input type="range" min="0" max="10000" step="100"
							bind:value={setAllSpeed} class="speed-slider" />
					</div>
					<div class="set-all-buttons">
						<button class="btn btn-start" on:click={setAllDrives}>▶ Start All</button>
						<button class="btn btn-stop" on:click={stopAllDrives}>⏹ Stop All</button>
					</div>

					<!-- Fan Speed Page integration -->
					<div class="fanspeed-section">
						<h4>Apply from Fan Speed Page</h4>
						{#if fanSpeedDisplay}
							<div class="fanspeed-grid">
								<button class="fanspeed-btn" on:click={() => applyFanSpeedToAll(fanSpeedDisplay?.current ?? 0)}>
									<span class="fs-value">{fanSpeedDisplay.current}%</span>
									<span class="fs-label">Current Cooling</span>
								</button>
								<button class="fanspeed-btn" on:click={() => applyFanSpeedToAll(fanSpeedDisplay?.max ?? 0)}>
									<span class="fs-value">{fanSpeedDisplay.max}%</span>
									<span class="fs-label">Cooling Max</span>
								</button>
								<button class="fanspeed-btn" on:click={() => applyFanSpeedToAll(fanSpeedDisplay?.min ?? 0)}>
									<span class="fs-value">{fanSpeedDisplay.min}%</span>
									<span class="fs-label">Cooling Min</span>
								</button>
								<button class="fanspeed-btn" on:click={() => applyFanSpeedToAll(fanSpeedDisplay?.refrig ?? 0)}>
									<span class="fs-value">{fanSpeedDisplay.refrig}%</span>
									<span class="fs-label">Refrigeration</span>
								</button>
								<button class="fanspeed-btn" on:click={() => applyFanSpeedToAll(fanSpeedDisplay?.recirc ?? 0)}>
									<span class="fs-value">{fanSpeedDisplay.recirc}%</span>
									<span class="fs-label">Recirculation</span>
								</button>
							</div>
						{:else}
							<p class="text-sm opacity-70">Waiting for fan speed settings…</p>
						{/if}
					</div>
					<div class="set-all-params">
						<h4>Apply Parameter to All</h4>
						<div class="set-all-row">
							<!-- svelte-ignore a11y-label-has-associated-control -->
							<label>Accel Ramp:</label>
							<select on:change={(e) => { if (e.currentTarget.value) setAllParams('rampUp', Number(e.currentTarget.value)); }}>
								<option value="">— choose —</option>
								<option value="20">2s</option>
								<option value="50">5s</option>
								<option value="100">10s</option>
								<option value="200">20s</option>
								<option value="300">30s</option>
							</select>
						</div>
						<div class="set-all-row">
							<!-- svelte-ignore a11y-label-has-associated-control -->
							<label>Decel Ramp:</label>
							<select on:change={(e) => { if (e.currentTarget.value) setAllParams('rampDown', Number(e.currentTarget.value)); }}>
								<option value="">— choose —</option>
								<option value="20">2s</option>
								<option value="50">5s</option>
								<option value="100">10s</option>
								<option value="200">20s</option>
								<option value="300">30s</option>
							</select>
						</div>
						<div class="set-all-row">
							<!-- svelte-ignore a11y-label-has-associated-control -->
							<label>Max Freq:</label>
							<select on:change={(e) => { if (e.currentTarget.value) setAllParams('maxFreq', Number(e.currentTarget.value)); }}>
								<option value="">— choose —</option>
								<option value="300">30 Hz</option>
								<option value="500">50 Hz</option>
								<option value="600">60 Hz</option>
								<option value="900">90 Hz</option>
								<option value="1200">120 Hz</option>
							</select>
						</div>
					</div>
				</div>
			</Card>
		{/if}

		<!-- ═══ Power cost calculator ═══ -->
		{#if showCalc}
			<Card>
				<div class="calc-panel">
					<h3>⚡ Power Cost Estimator</h3>
					<div class="calc-inputs">
						<div class="calc-field">
							<!-- svelte-ignore a11y-label-has-associated-control -->
							<label>Rate ($/kWh)</label>
							<input type="number" step="0.01" min="0" bind:value={costPerKwh} class="calc-input" />
						</div>
						<div class="calc-field">
							<!-- svelte-ignore a11y-label-has-associated-control -->
							<label>Hours/Day</label>
							<input type="number" step="1" min="0" max="24" bind:value={hoursPerDay} class="calc-input" />
						</div>
						<div class="calc-field">
							<span class="calc-label">Current Draw</span>
							<span class="calc-value">{totalPowerKw.toFixed(1)} kW</span>
						</div>
						<div class="calc-field">
							<span class="calc-label">Daily</span>
							<span class="calc-value">{dailyKwh.toFixed(1)} kWh</span>
						</div>
						<div class="calc-field calc-highlight">
							<span class="calc-label">Monthly (30d)</span>
							<span class="calc-value">{monthlyKwh.toFixed(0)} kWh</span>
						</div>
						<div class="calc-field calc-highlight calc-total">
							<span class="calc-label">Monthly Cost</span>
							<span class="calc-value calc-cost">${monthlyCost.toFixed(2)}</span>
						</div>
					</div>
					{#if drives.length > 1}
						<div class="calc-breakdown">
							<h4>Per-Drive Breakdown</h4>
							<table class="calc-table">
								<thead><tr><th>Drive</th><th>Power</th><th>kWh/mo</th><th>$/mo</th></tr></thead>
								<tbody>
									{#each perDriveCosts as pc}
										<tr class:calc-off={!pc.running}>
											<td>{pc.label || `Unit ${pc.unitId}`}</td>
											<td>{pc.kw.toFixed(1)} kW</td>
											<td>{pc.monthlyKwh.toFixed(0)}</td>
											<td>${pc.monthlyCost.toFixed(2)}</td>
										</tr>
									{/each}
								</tbody>
							</table>
						</div>
					{/if}
					<!-- Affinity law speed vs cost curve -->
					<div class="calc-breakdown">
						<h4>⚡ Fan Affinity Law — Speed vs. Cost (P ∝ speed³)</h4>
						<p class="calc-note">Slowing fans from 100% to 80% cuts power nearly in half. The last 20% of speed costs as much as the first 80%.</p>
						<table class="calc-table curve-table">
							<thead><tr><th>Speed</th><th>Power ({drives.length} drives)</th><th>kWh/mo</th><th>$/mo</th><th class="curve-bar-hdr">Relative Cost</th></tr></thead>
							<tbody>
								{#each speedCurve as sc}
									<tr class:curve-current={Math.abs(sc.pct - (drives[0]?.actualSpeedPercent ?? 0) / 100) < 6}>
										<td class="curve-pct">{sc.pct}%</td>
										<td>{sc.kw.toFixed(1)} kW</td>
										<td>{sc.moKwh.toFixed(0)}</td>
										<td>${sc.moCost.toFixed(2)}</td>
										<td class="curve-bar-cell"><div class="curve-bar" style="width: {sc.pct ** 3 / 10000}%"></div></td>
									</tr>
								{/each}
							</tbody>
						</table>
					</div>
				</div>
			</Card>
		{/if}

		<!-- ═══ No drives message ═══ -->
		{#if drives.length === 0}
			<Card>
				<div class="no-drives">
					<p>No VFD drives connected.</p>
					<p class="hint">The VFD Modbus TCP simulator must be running on the RS485 responder (port 5020).</p>
				</div>
			</Card>
		{/if}

		<!-- ═══ Drive cards — 2-column grid ═══ -->
		<div class="drives-grid">
			{#each drives as drv (drv.unitId)}
				<Card>
					<!-- Status banner — large and unmissable -->
					<div class="status-banner {statusColor(drv)}-bg">
						<span class="status-icon">{drv.faulted ? '⚠️' : drv.running ? (commandedDrives.has(drv.unitId) ? '▶' : '🔄') : '⏹'}</span>
						<span class="status-text">{statusLabel(drv)}</span>
						{#if drv.running}
							<span class="status-detail">{(drv.actualSpeedPercent / 100).toFixed(1)}% — {(drv.outputFreqHz / 10).toFixed(1)} Hz</span>
						{:else}
							<span class="status-detail">0 Hz</span>
						{/if}
					</div>

					<!-- Header -->
					<div class="drive-header">
						<div class="drive-title">
							<span class="drive-icon">⚙️</span>
							<span class="drive-name">{drv.label || `VFD Unit ${drv.unitId}`}</span>
						</div>
						<div class="drive-subtitle">
							<span class="mfg-badge">{mfgLabel(drv.manufacturer || 'generic')}</span>
							<span class="unit-badge">Unit {drv.unitId}</span>
						</div>
						{#if drv.faulted}
							<div class="fault-banner">⚠️ {drv.faultCode ? faultName(drv.faultCode, drv.manufacturer || 'generic') : 'Drive Fault — Recovering fault code...'}</div>
						{/if}
					</div>

					<!-- Primary metrics grid (8 items, 4×2) -->
					<div class="metrics-grid" style="grid-template-columns: repeat(4, 1fr)">
						{#each PRIMARY_METRICS as m (m.key)}
							<div class="metric">
								<span class="metric-value">{m.format(drv)}</span>
								<span class="metric-label">{m.label} <span class="metric-unit">{m.unit}</span></span>
							</div>
						{/each}
					</div>

					<!-- Show More toggle -->
					<button class="btn-more" on:click={() => toggleMoreMetrics(drv.unitId)}>
						{moreMetrics[drv.unitId] ? '▴ Less' : '▾ More Motor Data'}
					</button>

					<!-- Expanded extra metrics -->
					{#if moreMetrics[drv.unitId]}
						<div class="metrics-grid metrics-extra" style="grid-template-columns: repeat(4, 1fr)">
							{#each EXTRA_METRICS as m (m.key)}
								<div class="metric metric-secondary">
									<span class="metric-value">{m.format(drv)}</span>
									<span class="metric-label">{m.label} {#if m.unit}<span class="metric-unit">{m.unit}</span>{/if}</span>
								</div>
							{/each}
						</div>
					{/if}

					<!-- Speed control -->
					<div class="speed-control">
						<!-- svelte-ignore a11y-label-has-associated-control -->
						<label class="speed-label">
							Speed Ref:
							<span class="speed-val">{((sliderValues[drv.unitId] ?? 0) / 100).toFixed(1)}%</span>
						</label>
						<input type="range" min="0" max="10000" step="100"
							value={sliderValues[drv.unitId] ?? 0}
							on:input={(e) => setSpeed(drv.unitId, Number(e.currentTarget.value))}
							class="speed-slider" />
					</div>

					<!-- Buttons -->
					<div class="drive-buttons">
						<button class="btn btn-start" on:click={() => startDrive(drv.unitId)}
							disabled={drv.faulted || !drv.online}>▶ Start</button>
						<button class="btn btn-stop" on:click={() => stopDrive(drv.unitId)}
							disabled={!drv.online}>⏹ Stop</button>
						<button class="btn btn-dir" on:click={() => toggleDirection(drv.unitId)}
							disabled={!drv.online}>🔄 {drv.direction ? 'REV→FWD' : 'FWD→REV'}</button>
						{#if drv.faulted}
							<button class="btn btn-reset" on:click={() => resetFault(drv.unitId)}>
								🔄 Reset</button>
						{/if}
						<button class="btn btn-fault" on:click={() => injectFault(drv.unitId)}
							disabled={!drv.online || drv.faulted}>⚡ Inject Fault</button>
						<button class="btn btn-edit"
							on:click={() => { paramsExpanded[drv.unitId] = !paramsExpanded[drv.unitId]; paramsExpanded = paramsExpanded; }}>
							✏️ {paramsExpanded[drv.unitId] ? 'Close Editor' : 'Edit Parameters'}</button>
					</div>

					<!-- Expandable parameters editor -->
					{#if paramsExpanded[drv.unitId]}
						<div class="params-panel">
							<div class="params-grid">
								<!-- Manufacturer selector -->
								<div class="param-group">
									<h4>Drive Config</h4>
									<div class="param-row">
										<!-- svelte-ignore a11y-label-has-associated-control -->
										<label>Manufacturer</label>
										<select class="param-select"
											value={drv.manufacturer || 'generic'}
											on:change={(e) => writeMeta(drv.unitId, 'manufacturer', e.currentTarget.value)}>
											{#each MANUFACTURERS as m}
												<option value={m.value}>{m.label}</option>
											{/each}
										</select>
									</div>
								</div>
								<div class="param-group">
									<h4>Ramp Times</h4>
									<div class="param-row">
										<!-- svelte-ignore a11y-label-has-associated-control -->
										<label>Accel: {(drv.rampUpTime / 10).toFixed(1)}s</label>
										<input type="range" min="1" max="600" step="1"
											value={drv.rampUpTime}
											on:change={(e) => writeParam(drv.unitId, 'rampUp', Number(e.currentTarget.value))} />
									</div>
									<div class="param-row">
										<!-- svelte-ignore a11y-label-has-associated-control -->
										<label>Decel: {(drv.rampDownTime / 10).toFixed(1)}s</label>
										<input type="range" min="1" max="600" step="1"
											value={drv.rampDownTime}
											on:change={(e) => writeParam(drv.unitId, 'rampDown', Number(e.currentTarget.value))} />
									</div>
								</div>
								<div class="param-group">
									<h4>Frequency Limits</h4>
									<div class="param-row">
										<!-- svelte-ignore a11y-label-has-associated-control -->
										<label>Min: {(drv.minFreqHz / 10).toFixed(1)} Hz</label>
										<input type="range" min="0" max={drv.maxFreqHz} step="10"
											value={drv.minFreqHz}
											on:change={(e) => writeParam(drv.unitId, 'minFreq', Number(e.currentTarget.value))} />
									</div>
									<div class="param-row">
										<!-- svelte-ignore a11y-label-has-associated-control -->
										<label>Max: {(drv.maxFreqHz / 10).toFixed(1)} Hz</label>
										<input type="range" min="10" max="1200" step="10"
											value={drv.maxFreqHz}
											on:change={(e) => writeParam(drv.unitId, 'maxFreq', Number(e.currentTarget.value))} />
									</div>
								</div>
								<div class="param-group">
									<h4>Motor Nameplate</h4>
									<div class="param-info">
										<span>{(drv.ratedPowerkW / 100).toFixed(2)} HP</span>
										<span>{drv.ratedVoltage} V</span>
										<span>{(drv.ratedCurrentA / 100).toFixed(2)} A</span>
										<span>{(drv.ratedFreqHz / 10).toFixed(1)} Hz</span>
										<span>{drv.ratedSpeedRpm} RPM</span>
									</div>
								</div>
							</div>
						</div>
					{/if}
				</Card>
			{/each}
		</div>
	</div>
</GellertPage>

<style>
	.fans-page {
		padding: 1rem;
		display: flex;
		flex-direction: column;
		gap: 1rem;
		overflow-y: auto;
		overflow-x: hidden;
	}

	/* ── Toolbar ── */
	.toolbar {
		display: flex;
		gap: 0.5rem;
		align-items: center;
		flex-wrap: wrap;
		padding: 0.5rem 0.75rem;
		background: rgba(59, 130, 246, 0.08);
		border: 1px solid rgba(59, 130, 246, 0.2);
		border-radius: 8px;
	}
	.tbtn {
		padding: 8px 16px;
		border: 1px solid rgba(59, 130, 246, 0.4);
		border-radius: 6px;
		background: rgba(59, 130, 246, 0.15);
		color: #93bbfc;
		font-size: 0.95rem;
		font-weight: 600;
		cursor: pointer;
	}
	.tbtn:hover { background: rgba(59, 130, 246, 0.25); }
	.toolbar-info {
		margin-left: auto;
		font-size: 0.95rem;
		opacity: 0.8;
	}

	/* ── Settings panel ── */
	.set-all-panel h3 {
		margin: 0 0 0.75rem 0;
		font-size: 1.1rem;
	}
	.set-all-panel { max-width: 500px; }
	.set-all-row {
		display: flex;
		align-items: center;
		gap: 0.75rem;
		margin-bottom: 0.5rem;
	}
	.set-all-row label { min-width: 90px; font-size: 0.95rem; }
	.set-all-row select {
		flex: 1;
		padding: 4px 8px;
		border-radius: 4px;
		border: 1px solid rgba(128,128,128,0.3);
		background: rgba(128,128,128,0.05);
		color: inherit;
		font-size: 0.95rem;
	}
	.set-all-buttons {
		display: flex;
		gap: 0.5rem;
		margin: 0.75rem 0;
	}
	.set-all-params h4 {
		margin: 0.75rem 0 0.5rem 0;
		font-size: 0.95rem;
		opacity: 0.8;
		text-transform: uppercase;
		letter-spacing: 0.5px;
	}

	/* ── Fan speed section ── */
	.fanspeed-section {
		margin-top: 1rem;
		padding-top: 0.75rem;
		border-top: 1px solid rgba(128,128,128,0.2);
	}
	.fanspeed-section h4 {
		margin: 0 0 0.5rem 0;
		font-size: 0.95rem;
		opacity: 0.8;
		text-transform: uppercase;
		letter-spacing: 0.5px;
	}
	.fanspeed-grid {
		display: flex;
		gap: 0.4rem;
		flex-wrap: wrap;
		margin-bottom: 0.5rem;
	}
	.fanspeed-btn {
		display: flex;
		flex-direction: column;
		align-items: center;
		padding: 8px 14px;
		border: 1px solid rgba(128,128,128,0.25);
		border-radius: 6px;
		background: rgba(128,128,128,0.06);
		color: inherit;
		cursor: pointer;
		transition: background 0.15s;
		min-width: 80px;
	}
	.fanspeed-btn:hover { background: rgba(59,130,246,0.15); border-color: rgba(59,130,246,0.4); }
	.fs-value { font-size: 1.15rem; font-weight: 700; }
	.fs-label { font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.3px; opacity: 0.8; margin-top: 2px; }
	.btn-load, .btn-refresh {
		padding: 5px 12px;
		border: 1px solid rgba(128,128,128,0.3);
		border-radius: 5px;
		background: rgba(128,128,128,0.1);
		color: inherit;
		font-size: 0.9rem;
		cursor: pointer;
	}
	.btn-load:hover, .btn-refresh:hover { background: rgba(128,128,128,0.2); }
	.btn-load:disabled { opacity: 0.4; cursor: not-allowed; }

	/* ── 2-column drives grid ── */
	.drives-grid {
		display: grid;
		grid-template-columns: repeat(2, 1fr);
		gap: 1rem;
	}
	@media (max-width: 900px) {
		.drives-grid { grid-template-columns: 1fr; }
	}

	.no-drives {
		text-align: center;
		padding: 2rem;
		color: var(--text-muted, #999);
	}
	.no-drives .hint { font-size: 0.95rem; margin-top: 0.5rem; opacity: 0.85; }

	/* ── Drive card ── */
	.drive-header { margin-bottom: 0.75rem; }
	.drive-title {
		display: flex;
		align-items: center;
		gap: 0.4rem;
		font-size: 1.15rem;
		font-weight: 600;
	}
	.drive-icon { font-size: 1.2rem; }
	.drive-name { flex: 1; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
	.drive-status {
		font-size: 0.85rem;
		font-weight: 700;
		padding: 2px 8px;
		border-radius: 10px;
		background: rgba(128,128,128,0.15);
		white-space: nowrap;
	}
	.drive-subtitle {
		display: flex;
		gap: 0.4rem;
		margin-top: 4px;
	}
	.mfg-badge, .unit-badge {
		font-size: 0.8rem;
		padding: 1px 6px;
		border-radius: 4px;
		background: rgba(128,128,128,0.1);
		opacity: 0.85;
	}

	.clr-green { color: #22c55e; }
	.clr-red { color: #ef4444; }
	.clr-amber { color: #f59e0b; }
	.clr-blue { color: #60a5fa; }
	.clr-gray { color: #6b7280; }

	/* ── Status banner — big, unmissable on/off indicator ── */
	.status-banner {
		display: flex;
		align-items: center;
		gap: 0.75rem;
		padding: 0.75rem 1rem;
		border-radius: 8px 8px 0 0;
		font-weight: 700;
		font-size: 1.3rem;
	}
	.status-icon {
		font-size: 1.5rem;
	}
	.status-detail {
		margin-left: auto;
		font-size: 1.1rem;
		font-weight: 600;
		opacity: 0.9;
	}
	.clr-green-bg {
		background: rgba(34, 197, 94, 0.2);
		color: #22c55e;
		border: 2px solid rgba(34, 197, 94, 0.4);
	}
	.clr-blue-bg {
		background: rgba(96, 165, 250, 0.15);
		color: #60a5fa;
		border: 2px solid rgba(96, 165, 250, 0.3);
	}
	.clr-red-bg {
		background: rgba(239, 68, 68, 0.2);
		color: #ef4444;
		border: 2px solid rgba(239, 68, 68, 0.4);
	}
	.clr-amber-bg {
		background: rgba(245, 158, 11, 0.15);
		color: #f59e0b;
		border: 2px solid rgba(245, 158, 11, 0.3);
	}
	.clr-gray-bg {
		background: rgba(107, 114, 128, 0.15);
		color: #6b7280;
		border: 2px solid rgba(107, 114, 128, 0.3);
	}

	/* ── Live poll indicator ── */
	.toolbar-live {
		font-size: 0.85rem;
		padding: 2px 10px;
		border-radius: 10px;
		background: rgba(34, 197, 94, 0.1);
		color: #86efac;
		transition: opacity 0.3s;
	}
	.live-pulse {
		animation: pulse-live 1s ease-in-out infinite;
	}
	@keyframes pulse-live {
		0%, 100% { opacity: 1; }
		50% { opacity: 0.5; }
	}
	.register-debug { display: none; }

	.fault-banner {
		margin-top: 0.4rem;
		padding: 4px 10px;
		background: rgba(239, 68, 68, 0.15);
		color: #f87171;
		border-radius: 6px;
		font-size: 0.95rem;
		font-weight: 600;
	}

	/* ── Metrics grid ── */
	.metrics-grid {
		display: grid;
		gap: 0.35rem;
		margin-bottom: 0.75rem;
	}
	.metric {
		text-align: center;
		padding: 0.35rem;
		background: rgba(128,128,128,0.08);
		border-radius: 5px;
	}
	.metric-value {
		display: block;
		font-size: 1.1rem;
		font-weight: 700;
		font-variant-numeric: tabular-nums;
	}
	.metric-label {
		display: block;
		font-size: 0.75rem;
		text-transform: uppercase;
		letter-spacing: 0.4px;
		opacity: 0.75;
		margin-top: 1px;
	}
	.metric-unit {
		opacity: 0.6;
		font-size: 0.7rem;
	}

	/* ── Show More button ── */
	.btn-more {
		display: block;
		width: 100%;
		padding: 4px 0;
		margin-bottom: 0.5rem;
		border: 1px dashed rgba(128,128,128,0.25);
		border-radius: 5px;
		background: transparent;
		color: #93bbfc;
		font-size: 0.85rem;
		font-weight: 600;
		cursor: pointer;
		text-align: center;
		transition: background 0.15s, border-color 0.15s;
	}
	.btn-more:hover {
		background: rgba(59, 130, 246, 0.08);
		border-color: rgba(59, 130, 246, 0.35);
	}

	/* ── Secondary (expanded) metrics ── */
	.metrics-extra {
		margin-top: -0.4rem;
		margin-bottom: 0.75rem;
		padding-top: 0.35rem;
		border-top: 1px dashed rgba(128,128,128,0.15);
	}
	.metric-secondary {
		background: rgba(128,128,128,0.04);
	}
	.metric-secondary .metric-value {
		font-size: 0.95rem;
	}

	/* ── Speed control ── */
	.speed-control { margin-bottom: 0.75rem; }
	.speed-label {
		display: flex;
		justify-content: space-between;
		font-size: 0.95rem;
		margin-bottom: 3px;
	}
	.speed-val { font-weight: 700; font-variant-numeric: tabular-nums; }
	.speed-slider {
		width: 100%;
		height: 5px;
		-webkit-appearance: none;
		appearance: none;
		background: rgba(128,128,128,0.25);
		border-radius: 3px;
		outline: none;
		cursor: pointer;
	}
	.speed-slider::-webkit-slider-thumb {
		-webkit-appearance: none;
		width: 16px;
		height: 16px;
		border-radius: 50%;
		background: #3b82f6;
		cursor: pointer;
	}

	/* ── Buttons ── */
	.drive-buttons {
		display: flex;
		gap: 0.35rem;
		flex-wrap: wrap;
	}
	.btn {
		padding: 6px 12px;
		border: none;
		border-radius: 5px;
		font-size: 0.9rem;
		font-weight: 600;
		cursor: pointer;
		transition: opacity 0.2s;
	}
	.btn:disabled { opacity: 0.4; cursor: not-allowed; }
	.btn-start { background: #22c55e; color: white; }
	.btn-stop { background: #6b7280; color: white; }
	.btn-reset { background: #f59e0b; color: white; }
	.btn-fault { background: #ef4444; color: white; }
	.btn-dir { background: #6366f1; color: white; }
	.btn-edit {
		background: rgba(99, 102, 241, 0.15);
		color: #818cf8;
		border: 1px solid rgba(99, 102, 241, 0.3);
		margin-left: auto;
	}
	.btn-edit:hover { background: rgba(99, 102, 241, 0.25); }

	/* ── Parameters panel ── */
	.params-panel {
		margin-top: 0.75rem;
		padding: 0.75rem;
		background: rgba(128,128,128,0.06);
		border-radius: 6px;
		border: 1px solid rgba(128,128,128,0.15);
	}
	.params-grid {
		display: grid;
		grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
		gap: 0.75rem;
	}
	.param-group h4 {
		margin: 0 0 0.4rem 0;
		font-size: 0.85rem;
		text-transform: uppercase;
		letter-spacing: 0.5px;
		opacity: 0.7;
	}
	.param-row { margin-bottom: 0.4rem; }
	.param-row label {
		display: block;
		font-size: 0.9rem;
		margin-bottom: 2px;
		font-variant-numeric: tabular-nums;
	}
	.param-row input[type="range"] {
		width: 100%;
		height: 3px;
		-webkit-appearance: none;
		appearance: none;
		background: rgba(128,128,128,0.25);
		border-radius: 2px;
		outline: none;
		cursor: pointer;
	}
	.param-row input[type="range"]::-webkit-slider-thumb {
		-webkit-appearance: none;
		width: 12px;
		height: 12px;
		border-radius: 50%;
		background: #6366f1;
		cursor: pointer;
	}
	.param-select {
		width: 100%;
		padding: 4px 6px;
		border-radius: 4px;
		border: 1px solid rgba(128,128,128,0.3);
		background: rgba(128,128,128,0.05);
		color: inherit;
		font-size: 0.9rem;
	}
	.param-info {
		display: flex;
		flex-wrap: wrap;
		gap: 0.35rem;
		font-size: 0.9rem;
	}
	.param-info span {
		padding: 2px 6px;
		background: rgba(128,128,128,0.1);
		border-radius: 3px;
		font-variant-numeric: tabular-nums;
	}

	/* ── VFD fan control toggle ── */
	.tbtn-vfd { border-color: rgba(251, 191, 36, 0.4); background: rgba(251, 191, 36, 0.12); color: #fbbf24; }
	.tbtn-vfd:hover { background: rgba(251, 191, 36, 0.25); }
	.tbtn-vfd-active { border-color: rgba(16, 185, 129, 0.5); background: rgba(16, 185, 129, 0.2); color: #34d399; }
	.tbtn-vfd-active:hover { background: rgba(16, 185, 129, 0.3); }

	/* ── Power cost calculator ── */
	.tbtn-calc { border-color: rgba(16, 185, 129, 0.4); background: rgba(16, 185, 129, 0.15); color: #34d399; }
	.tbtn-calc:hover { background: rgba(16, 185, 129, 0.25); }
	.calc-panel h3 {
		margin: 0 0 0.75rem 0;
		font-size: 0.95rem;
	}
	.calc-inputs {
		display: flex;
		flex-wrap: wrap;
		gap: 0.75rem;
		align-items: flex-end;
	}
	.calc-field {
		display: flex;
		flex-direction: column;
		gap: 2px;
		min-width: 100px;
	}
	.calc-field label, .calc-label {
		font-size: 0.8rem;
		text-transform: uppercase;
		letter-spacing: 0.4px;
		opacity: 0.8;
	}
	.calc-input {
		width: 90px;
		padding: 4px 8px;
		border: 1px solid rgba(128,128,128,0.3);
		border-radius: 4px;
		background: rgba(128,128,128,0.05);
		color: inherit;
		font-size: 0.9rem;
		font-variant-numeric: tabular-nums;
	}
	.calc-value {
		font-size: 1rem;
		font-weight: 700;
		font-variant-numeric: tabular-nums;
	}
	.calc-highlight {
		padding: 6px 10px;
		background: rgba(16, 185, 129, 0.08);
		border-radius: 6px;
	}
	.calc-total {
		background: rgba(16, 185, 129, 0.15);
		border: 1px solid rgba(16, 185, 129, 0.3);
	}
	.calc-cost {
		color: #10b981;
		font-size: 1.2rem;
	}
	.calc-breakdown {
		margin-top: 0.75rem;
		padding-top: 0.75rem;
		border-top: 1px solid rgba(128,128,128,0.15);
	}
	.calc-breakdown h4 {
		margin: 0 0 0.5rem 0;
		font-size: 0.85rem;
		text-transform: uppercase;
		letter-spacing: 0.5px;
		opacity: 0.7;
	}
	.calc-table {
		width: 100%;
		max-width: 500px;
		border-collapse: collapse;
		font-size: 0.92rem;
		font-variant-numeric: tabular-nums;
	}
	.calc-table th {
		text-align: left;
		padding: 4px 8px;
		font-size: 0.8rem;
		text-transform: uppercase;
		letter-spacing: 0.4px;
		opacity: 0.75;
		border-bottom: 1px solid rgba(128,128,128,0.2);
	}
	.calc-table td {
		padding: 4px 8px;
	}
	.calc-table tr:nth-child(even) { background: rgba(128,128,128,0.04); }
	.calc-off { opacity: 0.4; }
	.calc-note {
		font-size: 0.88rem;
		opacity: 0.8;
		margin: 0 0 0.5rem 0;
		font-style: italic;
	}
	.curve-table { max-width: 650px; }
	.curve-pct { font-weight: 700; }
	.curve-bar-hdr { min-width: 120px; }
	.curve-bar-cell { padding: 4px 8px; }
	.curve-bar {
		height: 14px;
		background: linear-gradient(90deg, #10b981, #f59e0b, #ef4444);
		border-radius: 3px;
		min-width: 2px;
		transition: width 0.3s;
	}
	.curve-current {
		background: rgba(59, 130, 246, 0.15) !important;
		font-weight: 600;
	}

	@media (max-width: 600px) {
		.metrics-grid { grid-template-columns: repeat(2, 1fr) !important; }
	}
</style>
