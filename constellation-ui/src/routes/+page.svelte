<script lang="ts">
  import Climacell from '../lib/components/Climacell.svelte';
	import { keysStore, frontMatterStore, headersStore, navigationStore, keyboardStore, modeToColorStore, homePageStore, alarmsStore } from '$lib/store';
	import Humidifier from '$lib/components/Humidifier.svelte';
	import Baylight from '$lib/components/Baylight.svelte';
	import GellertPage from '$lib/components/GellertPage.svelte';
	import Card from '$lib/ui/Card.svelte';
	import Table from '$lib/ui/Table.svelte';
	import Row from '$lib/ui/Row.svelte';
	import Column from '$lib/ui/Column.svelte';
	import Select from '$lib/ui/Select.svelte';
	import Button from '$lib/ui/Button.svelte';
	import { onMount } from 'svelte';
	import { goto } from '$app/navigation';
	import { get } from 'svelte/store';
	import { KeyboardTypes } from '$lib/ui/Keyboard.svelte';
	import { checkPassword, checkKeys, safeJsonParse, getHttpUrl, type ArrayResponse } from '$lib/business/util';
	import { isValidSensor, SensorTypes } from '$lib/business/analog';
	import { t } from 'svelte-i18n';
	import { getDropDownPage, getHomePage } from '$lib/business/paging';

	export let data: ArrayResponse;

	// Initialize runtime selection
	let runtime = '0';
	
	// Function to sync runtime with navigationStore
	function syncRuntimeToStore() {
		$navigationStore.runtimeSelection = runtime;
	}
	
	let route = {
		RUNCLOCK: '/level1/runclock',
		FANSPEED: '/level1/fanspeed',
		BAYLIGHTS: '/level1/lights',
		CLIMACELL: '/level1/climacell',
		HUMIDIFIER: '/level1/humidifier',
	};
	$: ready = $frontMatterStore !== undefined && $frontMatterStore?.main !== undefined;
	$: main = $frontMatterStore.main as string[];
	$: panel = $frontMatterStore.panel as string[];
	$: animations = $frontMatterStore.animations as string;
	$: wait = false;
	$: error = false;
	$: P2BurnerData = undefined as string[] | undefined;
	$: currentMode = $modeToColorStore[$headersStore?.CurrentMode];
	$: boardType = ($frontMatterStore as any)?.boardType ?? ($frontMatterStore?.misc as string[])?.[0];
	$: startStop = true;
	$: outsideColor = outsideTempColor(($frontMatterStore?.main as string[])?.[7]);
	$: plenumColor = plenumTempColor(($frontMatterStore?.main as string[])?.[2]);
	$: humidityColor = plenumHumidColor(($frontMatterStore?.main as string[])?.[5]);
	$: co2Color = co2GetColor(($frontMatterStore?.main as string[])?.[17]);
	$: co2Color2 = co2GetColor(($frontMatterStore?.main as string[])?.[36]);
	$: returnHumidColor = returnHumidGetColor();
	$: onionMode = ($frontMatterStore?.panel as string[])?.[8] === '1';
	$: cureOn = onionMode && (($frontMatterStore?.main as string[]))?.[24] === '1' && (($frontMatterStore?.main as string[]))?.[25] === '0';
	$: burnerMode = P2BurnerData?.[6];
	
	async function remoteOff() {
		startStop = false;
		wait = true;
		await fetch(getHttpUrl('/iot/button'), {
			method: 'POST',
			headers: {
				'Content-Type': 'application/json',
			},
			body: JSON.stringify({
				tag: 'button2',
				remoteStop: $headersStore?.CurrentMode === 20 ? 'Start' : 'Stop',
			}),
		});
		await fetch(getHttpUrl('/iot/logout'), {
			method: 'POST',
			headers: {
				'Content-Type': 'application/json'
			}
		});
		$navigationStore = { ...$navigationStore, level: 0, name: 'version', data: '', invalidate: false, inLoad: false };
		$keysStore.accessLevel = 0;
		setTimeout(() => {
			startStop = true;
			wait = false;
		}, 5000);
	}

	function displayNumber(value?: string) {
		if (value === 'Off') {
			return $t('global.off');
		} else if (value === 'dis') {
			return '*';
		}
		// Handle undefined, null, or empty string as '--'
		return (value && value.trim() !== '') ? value : '--';
	}

	// Helper to produce display text and disabled state ('*' -> disabled/gray)
	function asDisplay(value?: string) {
		const text = displayNumber(value);
		return { text, disabled: text === '*' };
	}

	$: v_plenumTemp = asDisplay(main?.[2]);
	$: v_outsideTemp = asDisplay(main?.[7]);
	$: v_blendAvail = asDisplay(panel?.[0]);
	$: v_returnTemp1 = asDisplay(main?.[9]);
	$: v_returnTemp2 = asDisplay(main?.[37]);
	$: v_plenumHumid = asDisplay(main?.[5]);
	$: v_outsideHumid = asDisplay(main?.[8]);
	$: v_returnHumid1 = asDisplay(main?.[10]);
	$: v_returnHumid2 = asDisplay(main?.[35]);
	$: v_returnHumidCalc = asDisplay(main?.[31]);
	$: v_co2_1 = asDisplay(main?.[17]);
	$: v_co2_2 = asDisplay(main?.[36]);
	$: v_fanSpeed = asDisplay(main?.[14]);
	$: v_dailyFanRuntime = asDisplay(panel?.[1]);
	$: v_moisture1 = asDisplay(main?.[38]);
	$: v_moisture2 = asDisplay(main?.[39]);

	// Row-level disabled states for combined rows
	$: returnTempRowDisabled = v_returnTemp1.disabled || (isValidSensor(main?.[37]) && v_returnTemp2.disabled);
	$: returnHumidRowDisabled = onionMode
		? v_returnHumidCalc.disabled
		: (v_returnHumid1.disabled || (isValidSensor(main?.[35]) && v_returnHumid2.disabled));
	$: co2RowDisabled = v_co2_1.disabled || (isValidSensor(main?.[36]) && v_co2_2.disabled);

	// Consolidated output (cooling/refrigeration/burner) value with disabled state
	$: v_output = (() => {
		if (currentMode?.text?.startsWith('REFRIG') || currentMode?.text?.startsWith($t('mode.refrigeration'))) {
			return asDisplay(main?.[34]);
		} else if (cureOn) {
			if (panel?.[10] === '0' && burnerMode === '0') {
				return { text: '*', disabled: true };
			}
			return asDisplay(main?.[18]);
		}
		return asDisplay(main?.[15]);
	})();

	// Unit helpers
	$: showFanSpeedUnit = !v_fanSpeed.disabled && main?.[14] !== 'Off';

	async function loadInitialData() {
		try {
			wait = true;
			const url = new URL(window.location.href);
			const params = url.href.split('?');
			$navigationStore.redirect = false;
			$navigationStore.isDirty = () => false;
			if (params.length > 1) {
				if (params[1].endsWith('Load')) {
					$keysStore.accessLevel = 1;
					$navigationStore.name = 'version';
					$navigationStore.level = 1;
					$navigationStore.inLoad = true;
					await goto("/level1/version");
				} else if (params[1].endsWith('Redirect')) {
					$navigationStore.redirect = true;
					$navigationStore.homeUrl = document.referrer;
				}
				}
			
			// Use safeJsonParse with wait state handling for burner data
			const response = await fetch(getHttpUrl('/iot/burner'));
			P2BurnerData = await safeJsonParse(response, (isWaiting) => wait = isWaiting);
			
			if (!$homePageStore.initialized) {
				const basicResponse = await fetch(getHttpUrl('/iot/basic/home'));
				if (!basicResponse.ok) {
					throw new Error('Failed to fetch basic home data');
					}
				
				// Use safeJsonParse for home page data
				const homeData = await safeJsonParse(basicResponse, (isWaiting) => wait = isWaiting);
				$homePageStore.initialized = true;
				$homePageStore.page = getHomePage(homeData.data);
				if ($homePageStore.page !== '/') {
					await goto($homePageStore.page);
					$navigationStore.dropDownPage = getDropDownPage($homePageStore.page);
					}
				}
			
				wait = false;
			} catch (err) {
				console.error("Error loading initial data:", err);
				error = true;
				// Keep showing spinner for a moment, then set ready to show the app in a degraded state
				setTimeout(() => {
					wait = false;
					ready = true;
				}, 5000);
			}
		}

	// Function to retry loading initial data
	function retryLoadData() {
		ready = false;
		error = false;
		loadInitialData();
	}

	onMount(() => {
		// Initialize runtime from navigationStore if available
		if ($navigationStore.runtimeSelection) {
			runtime = $navigationStore.runtimeSelection;
		} else {
			// Set initial value in store
			syncRuntimeToStore();
		}
		
		// Load initial page data
		loadInitialData();
	});

	function plenumTempColor(plenumTemp: string | undefined) {
		if (!plenumTemp) {
			return 'text-black';
		}
		const valueFloat = parseFloat(plenumTemp);
		const setpointFloat = parseFloat(($frontMatterStore?.main as string[])?.[3]);
		let color = 'text-green-700 font-bold';
		if (valueFloat < setpointFloat - 0.5 || valueFloat > setpointFloat + 0.5) {
				color =  'text-red-500 font-bold';
		} else if (valueFloat < setpointFloat - 0.2 || valueFloat > setpointFloat + 0.2) {
				color = 'text-yellow-500 font-bold';
		}
		return color;
	}

	function outsideTempColor(outsideTemp: string | undefined) {
		if (!outsideTemp) {
			return 'text-black';
		}
		const valueFloat = parseFloat(outsideTemp);
		const startTemp = parseFloat(($frontMatterStore.panel as string[])?.[0]);
		if (valueFloat > startTemp) {
			return 'text-red-500 font-bold';
		}
		return 'text-green-700 font-bold';
	}

	function plenumHumidColor(plenumHumid: string | undefined): string {
		if (!plenumHumid) {
			return 'text-black';
		}
		const valueFloat = parseFloat(plenumHumid);
		const setpointFloat = parseFloat(($frontMatterStore?.main as string[])?.[6]);
		let color = 'text-green-700 font-bold';
		if (valueFloat < setpointFloat - 4) {
				color =  'text-red-500 font-bold';
		}
		return color;
	}

	function co2GetColor(co2: string | undefined): string {
		const size = isValidSensor(main?.[36]) ? 'text-size-large' : 'text-size-xl';
		if (!co2) {
			return `text-black ${size}`;
		}
		const valueFloat = parseFloat(co2);
		const setpointFloat = parseFloat(($frontMatterStore?.main as string[])?.[33]);
		if (($frontMatterStore?.main as string[])?.[32] === '2') {
			if (valueFloat > setpointFloat) {
				return `text-red-500 font-bold ${size}`;
			} else {
				return `text-green-700 font-bold ${size}`;
			}
		}
		return `text-black font-bold ${size}`;
	}

	function returnHumidGetColor(): string {
		let color = 'text-black font-bold';
		const returnHumidValue = parseFloat(($frontMatterStore?.main as string[])?.[10]);
		const humidSetValue = parseFloat(($frontMatterStore?.main as string[])?.[6]);
		if (cureOn && ($frontMatterStore?.main as string[])?.[29] === '1') {
			if (returnHumidValue > humidSetValue - 4) {
				color ='text-green-700 font-bold';
			} else {
				color = 'text-red-500 font-bold';
			}
		}
		return color;
	}

	async function onClick(route: string, command: (p: string) => Promise<void>) {
		if ($keysStore.hasLevel1Password && $keysStore.accessLevel < 1) {
			$keyboardStore.keyboardType = KeyboardTypes.Alpha;
			$keyboardStore.label = 'Password';
			$keyboardStore.start = '';
			$keyboardStore.resultReady = async (data: string) => {
						let user = data.split(':');
					await checkPassword(user.length > 1 ? user[0] : '', user.length > 1 ? user[1] : user[0], (value) => wait = value, (value) => error = value, async (level) => {
							// Ensure DH secret is established before navigating to level2
							if (level === 2 && !get(keysStore).secret) {
								await checkKeys();
							}
							await command(route);
						});
					};
			$keyboardStore.inputType = 'loginPassword';
			$keyboardStore.hidden = false;
			$keyboardStore = $keyboardStore;
			} else {
				await checkPassword('DEFAULT', '', (value) => wait = value, (value) => error = value, async (level) => {
					// Ensure DH secret is established before navigating to level2
					if (level === 2 && !get(keysStore).secret) {
						await checkKeys();
					}
					await command(route);
				});
    	}
	}

	function findSensorLabelByID(id: number): string {
		const values = Object.values(data.array ?? [])
		// Stride is 6 (Label, SID, Type, Value, Offset, Disabled)
		const index = id * 6;
		if (index > values.length) {
			return '';
		}
		const label = values[index];
		// Return empty string if label is blank (factory default may leave labels blank)
		return (label && label.trim() !== '') ? label : '';
	}

	function findSensorLabelByType(type: string): string {
		const values = Object.values(data?.array ?? [])
		// Stride is 6 (Label, SID, Type, Value, Offset, Disabled)
		for (let i = 0; i < values.length; i += 6) {
			if (values[i + 2] === type) {
				// Only return the label if it's non-empty (factory default may leave labels blank)
				const label = values[i];
				if (label && label.trim() !== '') {
					return label;
				}
				break; // Found matching type but label is empty, fall through to defaults
			}
		}
		switch (type) {
			case SensorTypes.SENSOR_CO2_1: return 'CO2 #1';
			case SensorTypes.SENSOR_CO2_2: return 'CO2 #2';
			case SensorTypes.SENSOR_RETURN_HUMID_1: return `${$t('global.return-humidity')} #1`;
			case SensorTypes.SENSOR_RETURN_HUMID_2: return `${$t('global.return-humidity')} #2`;
			case SensorTypes.SENSOR_RETURN_TEMP_1: return `${$t('level2.auxiliary.return-temp')} #1`;
			case SensorTypes.SENSOR_RETURN_TEMP_2: return `${$t('level2.auxiliary.return-temp')} #2`;
			default: return '';
		}
	}

	// Get plenum temperature label using sensor label from data
	function getPlenumTempLabel(): string {
		const plenumLabel = findSensorLabelByID(0);
		if (plenumLabel) {
			return plenumLabel;
		}
		return $t('global.plenum-temperature');
	}

	// Get outside temperature label using sensor label from data (IR sensor)
	function getOutsideTempLabel(): string {
		const outsideLabel = findSensorLabelByID(2);
		if (outsideLabel) {
			return outsideLabel;
		}
		return $t('system-monitor.outside-air-temperature');
	}

	// Get plenum humidity label using sensor label from data
	function getPlenumHumidLabel(): string {
		const humidLabel = findSensorLabelByID(5);
		if (humidLabel) {
			return humidLabel;
		}
		return $t('global.plenum-humidity');
	}

	// Get outside humidity label using sensor label from data
	function getOutsideHumidLabel(): string {
		const humidLabel = findSensorLabelByID(4);
		if (humidLabel) {
			return humidLabel;
		}
		return $t('system-monitor.outside-air-humidity');
	}

	// Get plenum temperature setpoint label using sensor label from data
	// Format: "Plenum 1 Setpoint" (uses first plenum temp sensor label + Setpoint)
	function getPlenumSetpointLabel(): string {
		const plenumLabel = findSensorLabelByID(0);
		if (plenumLabel) {
			return `${plenumLabel} ${$t('global.setpoint')}`;
		}
		return $t('global.plenum-temperature-setpoint');
	}

	// Get plenum humidity setpoint label using sensor label from data
	// Format: "Plenum Humid Setpoint" (uses plenum humidity sensor label + Setpoint)
	function getPlenumHumidSetpointLabel(): string {
		const humidLabel = findSensorLabelByID(5);
		if (humidLabel) {
			return `${humidLabel} ${$t('global.setpoint')}`;
		}
		return $t('global.plenum-humidity-setpoint');
	}

	function getMasterSlaveStatus(value: string): string {
		switch (value) {
			case '1': return $t('level2.master.master');
			case '2': return $t('level2.master.slave');
			default: return '';
		}
	}
</script>

<GellertPage
	{ready}
	title={$t('system-monitor.system-monitor')}
	level={0}
	{wait}
	name=""
	left={getMasterSlaveStatus(panel?.[30])}
>
	<div class="flex flex-col h-full">
			<div class="flex flex-row h-1/2 p-1">
				<Card class="w-1/2 mr-1 flex flex-col">
					<Table class="mr-1 mb-1 md:mr-2 md:mb-2 text-size-xl">
						<Row>
							<Column class="w-3/4 border-r border-gray-400 !py-1">
								<span class="text-size-xl">{cureOn ? $t('system-monitor.air-cure-start-temperature') : getPlenumSetpointLabel()}</span>
							</Column>
							<Column class="w-1/4 !py-1">
								<span class="text-size-xl">{displayNumber(cureOn ? main?.[11] : main?.[3])}°</span>
							</Column>
						</Row>
					</Table>
					<Table class="mr-1 mb-1 md:mr-2 md:mb-2 text-size-xl">
						<Row class="{v_plenumTemp.disabled ? 'text-gray-500' : ''}">
							<Column class="w-3/4 border-r border-gray-400 !py-1"><span class="text-size-xl">{ getPlenumTempLabel() }</span></Column>
							<Column class="w-1/4 !py-1">
								<span class="text-size-xl {v_plenumTemp.disabled ? '' : plenumColor}">{v_plenumTemp.text}{v_plenumTemp.disabled ? '' : '°'}</span>
							</Column>
						</Row>
					</Table>
					<Table class="mr-1 mb-1 md:mr-2 md:mb-2 text-size-xl">
						<Row class="{v_outsideTemp.disabled ? 'text-gray-500' : ''}">
							<!-- Outside Temperature -->
							<Column class="w-3/4 border-r border-gray-400 !py-1">
								<span class="text-size-xl">{getOutsideTempLabel()}</span>
								{#if panel?.[30] === '2'}
									<span class="text-base font-bold">({$alarmsStore.slaveBroadcastWarning ? $t('global.local') : $t('global.remote')})</span>
								{/if}
							</Column>
							<Column class="w-1/4 !py-1">
								<span class="text-size-xl {v_outsideTemp.disabled ? '' : outsideColor}">{v_outsideTemp.text}{v_outsideTemp.disabled ? '' : '°'}</span>
							</Column>
						</Row>
					</Table>
					<Table class="mr-1 mb-1 md:mr-2 md:mb-2 text-size-xl">
						<Row class="{v_blendAvail.disabled ? 'text-gray-500' : ''}">
							<Column class="w-3/4 border-r border-gray-400 !py-1">
								<span class="text-size-xl">{cureOn ? $t('system-monitor.outside-air-blend') : $t('system-monitor.cooling-available-temperature')}</span>
							</Column>
							<Column class="w-1/4 !py-1"><span class="text-size-xl">{v_blendAvail.text}{v_blendAvail.disabled ? '' : (cureOn ? '%' : '°')}</span></Column></Row>
					</Table>
					<Table class="mr-1 md:mr-2 text-size-xl">
						<Row class="{returnTempRowDisabled ? 'text-gray-500' : ''}">
							{#if !isValidSensor(main?.[37])}
								<Column class="w-3/4 border-r border-gray-400 !py-1"><span class="text-size-xl">{findSensorLabelByType(SensorTypes.SENSOR_RETURN_TEMP_1)}</span></Column>
								<Column class="w-1/4 !py-1"><span class="text-size-xl">{v_returnTemp1.text}{v_returnTemp1.disabled ? '' : '°'}</span></Column>
							{:else}
								<Column class="w-3/4 border-r border-gray-400 !py-1"><span class="text-size-large">{findSensorLabelByType(SensorTypes.SENSOR_RETURN_TEMP_1)} | {findSensorLabelByType(SensorTypes.SENSOR_RETURN_TEMP_2)}</span></Column>
								<Column class="w-1/4 !py-1"><span class="text-size-large">{v_returnTemp1.text}{v_returnTemp1.disabled ? '' : '°'} | {v_returnTemp2.text}{v_returnTemp2.disabled ? '' : '°'}</span></Column>
							{/if}
						</Row>
					</Table>
				</Card>
				<Card class="w-1/2 ml-1 flex flex-col">
					<Table class="mr-1 mb-1 md:mr-2 md:mb-2 text-size-xl">
						<Row>
							<Column class="w-3/4 border-r border-gray-400 !py-1">
								<span class="text-size-xl">{cureOn ? $t('system-monitor.air-cure-start-humidity') : getPlenumHumidSetpointLabel()}</span>
							</Column>
							<Column class="w-1/4 !py-1">
								<span class="text-size-xl">{displayNumber(cureOn ? main?.[12] : main?.[6])}%</span>
							</Column>
						</Row>
					</Table>
					<Table class="mr-1 mb-1 md:mr-2 md:mb-2 text-size-xl">
						<Row class="{v_plenumHumid.disabled ? 'text-gray-500' : ''}">
							<Column class="w-3/4 border-r border-gray-400 !py-1"><span class="text-size-xl">{getPlenumHumidLabel()}
							</span></Column>
							<Column class="w-1/4 !py-1"><span class="text-size-xl {v_plenumHumid.disabled ? '' : humidityColor}">{v_plenumHumid.text}{v_plenumHumid.disabled ? '' : '%'}</span></Column>
						</Row>
					</Table>
					<Table class="mr-1 mb-1 md:mr-2 md:mb-2 text-size-xl">
						<Row class="{v_outsideHumid.disabled ? 'text-gray-500' : ''}">
							<Column class="w-3/4 border-r border-gray-400 !py-1">
								<span class="text-size-xl">{ getOutsideHumidLabel() }</span>
								{#if panel?.[30] === '2'}
									<span class="text-base font-bold">({$alarmsStore.slaveBroadcastWarning ? $t('global.local') : $t('global.remote')})</span>
								{/if}
							</Column>
							<Column class="w-1/4 !py-1">
								<span class="text-size-xl">{v_outsideHumid.text}{v_outsideHumid.disabled ? '' : '%'}</span>
							</Column>
						</Row>
					</Table>
					<Table class="mr-1 mb-1 md:mr-2 md:mb-2 text-size-xl">
						<Row class="{returnHumidRowDisabled ? 'text-gray-500' : ''}">
							<Column class="w-3/4 border-r border-gray-400 !py-1">
								{#if onionMode || !isValidSensor(main?.[35])}
									<!-- Outside air / Return Air -->
									<span class="text-size-xl">{onionMode ? $t('system-monitor.outside-air-humidity') : findSensorLabelByType(SensorTypes.SENSOR_RETURN_HUMID_1)}</span>
									{#if onionMode}<span class="text-base">({$t('global.calculated')})</span>{/if}
								{:else if isValidSensor(main?.[35])}
									<span class="text-size-large">{findSensorLabelByType(SensorTypes.SENSOR_RETURN_HUMID_1)} | {findSensorLabelByType(SensorTypes.SENSOR_RETURN_HUMID_2)}</span>
								{/if}
							</Column>
							<Column class="w-1/4 !py-1">
								{#if onionMode || !isValidSensor(main?.[35])}
									<span class="{returnHumidRowDisabled ? '' : returnHumidColor}">{onionMode ? v_returnHumidCalc.text : v_returnHumid1.text}{(onionMode ? v_returnHumidCalc.disabled : v_returnHumid1.disabled) ? '' : '%'}</span>
								{:else if isValidSensor(main?.[35])}
									<span class="text-size-large">{v_returnHumid1.text}{v_returnHumid1.disabled ? '' : '%'} | {v_returnHumid2.text}{v_returnHumid2.disabled ? '' : '%'}</span>
								{/if}
							</Column>
						</Row>
					</Table>
					<Table class="mr-1 md:mr-2 text-size-xl">
						<Row class="{onionMode ? (v_returnHumid1.disabled ? 'text-gray-500' : '') : (co2RowDisabled ? 'text-gray-500' : '')}">
							<Column class="{!onionMode ? 'w-1/2' : 'w-2/3'} border-r border-gray-400 !py-1">
								{#if onionMode}
									<span class="text-size-xl">{ $t('system-monitor.return-air-humidity') }</span>
								{:else}
									{#if isValidSensor(main?.[36])}
										<span class="text-size-large">{findSensorLabelByType(SensorTypes.SENSOR_CO2_1)} | {findSensorLabelByType(SensorTypes.SENSOR_CO2_2)}</span>
									{:else}
										<span class="text-size-xl">CO<sub>2</sub></span>
									{/if}
								{/if}
							</Column>
							<Column class="{!onionMode ? 'w-1/2' : 'w-1/3'} !py-1">
								{#if onionMode}
									<span class="{v_returnHumid1.disabled ? '' : returnHumidColor}">{v_returnHumid1.text}{v_returnHumid1.disabled ? '' : '%'}</span>
								{:else}
									<span class={co2RowDisabled ? '' : co2Color}>{v_co2_1.text}{v_co2_1.disabled ? '' : ' '}<b class="text-base xl:text-lg">{v_co2_1.disabled ? '' : 'ppm'}</b></span>
									{#if isValidSensor(main?.[36])}
										| <span class={co2RowDisabled ? '' : co2Color2}>{v_co2_2.text}{v_co2_2.disabled ? '' : ' '}<b class="text-base xl:text-lg">{v_co2_2.disabled ? '' : 'ppm'}</b></span>
									{/if}
								{/if}
							</Column>
						</Row>
					</Table>
				</Card>
			</div>
			<div class="flex flex-row h-1/5 p-1">
				<Card class="w-1/2 mr-1 flex flex-col md:!py-1">
					<Table class="mr-1 mb-1 md:mr-2 text-size-xl">
						<Row class="{v_fanSpeed.disabled ? 'text-gray-500' : ''}"><Column class="text-size-xl w-3/4 border-r border-gray-400 !py-1"><span class="text-size-xl">{ $t('system-monitor.current-fan-speed') }</span></Column><Column class="w-1/4"><span class="text-size-xl">{v_fanSpeed.text}{showFanSpeedUnit ? '%' : ''}</span></Column></Row>
					</Table>
					<Table class="mr-1 md:mr-2">
						<Row class="{v_output.disabled ? 'text-gray-500' : ''}">
							<Column class="w-3/4 border-r border-gray-400 !py-1">
								<span class="text-size-xl">
									{#if main?.[16] === '1'}
										{ $t('system-monitor.refrigeration-output') }
									{:else if main?.[16] === '2'}
										{ $t('global.burner-output') }
									{:else if currentMode?.text === $t('mode.heating-ramping')}
										{ $t('level2.pid.fresh-air-doors')}
									{:else if main?.[16] === '3'}
										{ $t('level2.pid.fresh-air-doors')} ({ $t('system-monitor.diagnostics')})
									{:else}
										{ $t('system-monitor.cooling-output') }
									{/if}
								</span>
							</Column>
							<Column class="w-1/4 !py-1">
								<span class="text-size-xl {v_output.disabled ? 'text-gray-500' : ''}">{v_output.text}{v_output.disabled ? '' : '%'}</span>
							</Column>
						</Row>
					</Table>
				</Card>
				<Card class="w-1/2 ml-1 flex flex-col md:!py-1">
					<Table class="mr-1 mb-1 md:mr-2 text-size-xl">
						<Row class="{v_dailyFanRuntime.disabled ? 'text-gray-500' : ''}">
							<Column class="w-3/4 border-r border-gray-400 !py-1"><span class="text-size-xl">{ $t('system-monitor.daily-fan-runtime') } <span class="text-base xl:text-lg">({ $t('system-monitor.since-noon') })</span></span></Column>
							<Column class="w-1/4 !py-1"><span class="text-size-xl">{v_dailyFanRuntime.text} <b class="text-base xl:text-lg">{v_dailyFanRuntime.disabled ? '' : 'hrs'}</b></span></Column>
						</Row>
					</Table>
					<Select class="mr-1 md:mr-2 rounded-none shadow:md md:shadow-xl shadow-gray-500 !py-1" size="xl" bind:value={runtime} edit={true} on:change={syncRuntimeToStore}>
						<option value="0">{ $t('global.total-fan-runtime') } - {panel?.[2] ?? '--'} { $t('system-monitor.hrs') }</option>
						<option value="1">{ $t('system-monitor.refrigeration-runtime') } - {panel?.[3] ?? '--'} { $t('system-monitor.hrs') }</option>
						<option value="2">{ $t('system-monitor.cooling-runtime') } - {panel?.[4] ?? '--'} { $t('system-monitor.hrs') }</option>
						<option value="3">{ $t('system-monitor.recirculation-runtime') } - {panel?.[5] ?? '--'} { $t('system-monitor.hrs') }</option>
						<option value="4">{ $t('system-monitor.standby-runtime') } - {panel?.[7] ?? '--'} { $t('system-monitor.hrs') }</option>
					</Select>
				</Card>
			</div>
			{#if !onionMode}
			<div class="flex flex-row h-[10%] p-1">
				{#if main?.[38] !== '--'}
				<Card class="w-1/2 mr-1 flex flex-col md:!py-1">
					<Table class="mr-1 mb-1 md:mr-2 text-size-xl">
						<Row class="{v_moisture1.disabled ? 'text-gray-500' : ''}">
							<Column class="w-3/4 border-r border-gray-400 !py-1">
								<span class="text-size-xl">{$t('sensor-list.moisture-loss')} 1</span>
							</Column>
							<Column class="w-1/4 !py-1">
								<span class="text-size-xl">{v_moisture1.text}{v_moisture1.disabled ? '' : ' mi'}</span>
							</Column>
						</Row>
					</Table>
				</Card>
				{/if}
				{#if main?.[39] !== '--'}
				<Card class="w-1/2 ml-1 flex flex-col md:!py-1">
					<Table class="mr-1 mb-1 md:mr-2 text-size-xl">
						<Row class="{v_moisture2.disabled ? 'text-gray-500' : ''}">
							<Column class="w-3/4 border-r border-gray-400 !py-1">
								<span class="text-size-xl">{$t('sensor-list.moisture-loss')} 2</span>
							</Column>
							<Column class="w-1/4 !py-1">
								<span class="text-size-xl">{v_moisture2.text}{v_moisture2.disabled ? '' : ' mi'}</span>
							</Column>
						</Row>
					</Table>
				</Card>
				{/if}
			</div>
			{/if}
			<div class="flex flex-row h-1/6 p-1">
				<Card class="flex flex-row w-full">
					<div class="flex w-3/5">
						{#if animations === 'true'}
							<div class="flex h-full w-1/6">
							{#if currentMode?.image}
								<button on:click={() => onClick(route.RUNCLOCK, async (r) => { $navigationStore.level = 1; goto(r); })}>
									<img src={currentMode.image} alt="Current Mode" class="ml-4 lg:h-16 lg:w-16 3xl:h-24 3xl:w-24" />
								</button>
							{/if}
							</div>
							<div class="flex h-full w-1/6">
								<button on:click={() => onClick(route.FANSPEED, async (r) => { $navigationStore.level = 1; goto(r); })}>
									{#if main?.[14] === 'Off'}
										<img src="fan-off-50x50.png" alt="Fan Off" class="lg:h-16 lg:w-16 3xl:h-24 3xl:w-24"/>
									{:else}
										<img src="fan-anim-50x50.gif" alt="Fan On" class="lg:h-16 lg:w-16 3xl:h-24 3xl:w-24"/>
									{/if}
								</button>
							</div>
							<Climacell
								{boardType}
								systemMode={panel?.[8]}
								climacellEquipment={panel?.[9]}
								climacellSwitch={panel?.[10]}
								climacellInput={panel?.[11]}
								climacellOutput={panel?.[12]}
								on:click={() => onClick(route.CLIMACELL, async (r) => { $navigationStore.level = 1; goto(r); })}
							/>
							<Humidifier 
								{boardType}
								humidifierSwitch={panel?.[13]}
								humidifierEquipment={panel?.[14]}
								humidifierInput={panel?.[15]}
								humidifierHeadOutput={panel?.[16]}
								humidifierPumpOutput={panel?.[17]}
								humidifierNumber={1}
								on:click={() => onClick(route.HUMIDIFIER, async (r) => { $navigationStore.level = 1; goto(r); })}
							/>
							<Humidifier
								{boardType}
								humidifierSwitch={panel?.[13]}
								humidifierEquipment={panel?.[18]}
								humidifierInput={panel?.[19]}
								humidifierHeadOutput={panel?.[20]}
								humidifierPumpOutput={panel?.[21]}
								humidifierNumber={2}
								on:click={() => onClick(route.HUMIDIFIER, async (r) => { $navigationStore.level = 1; goto(r); })}
							/>
							<Humidifier
								{boardType}
								humidifierSwitch={panel?.[13]}
								humidifierEquipment={panel?.[22]}
								humidifierInput={panel?.[23]}
								humidifierHeadOutput={panel?.[24]}
								humidifierPumpOutput={panel?.[25]}
								humidifierNumber={3}
								on:click={() => onClick(route.HUMIDIFIER, async (r) => { $navigationStore.level = 1; goto(r); })}
							/>
							<Baylight
								{boardType}
								bayLight1Equipment={panel?.[26]}
								bayLight2Equipment={panel?.[27]}
								bayLight1Input={panel?.[28]}
								bayLight2Input={panel?.[29]}
								on:click={() => onClick(route.BAYLIGHTS, async (r) => { $navigationStore.level = 1; goto(r); })}
							/>
						{/if}
					</div>
					<div class="flex flex-row w-2/5 items-center text-size-xl">
						<span class="ml-auto text-size-xl">{ $t('system-monitor.system-start-stop') }</span>
						<Button class="mx-2" size="xl" on:click={() => onClick('', async (_) => remoteOff())} disabled={!startStop}>{$headersStore?.CurrentMode === 20 ? $t('system-monitor.start') : $t('system-monitor.stop')}</Button>
					</div>
				</Card>
			</div>
		</div>
</GellertPage>

