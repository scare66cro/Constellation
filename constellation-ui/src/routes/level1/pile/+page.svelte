<script lang="ts">
	import { getAdornment, isHumidSensorType, isTempSensorType, sensorDisplayValue, type SensorInfo } from "$lib/business/util";
	import { SensorTypes } from "$lib/business/analog";
	import GellertPage from "$lib/components/GellertPage.svelte";
	import { navigationStore } from "$lib/store";
	import Card from "$lib/ui/Card.svelte";
	import Column from "$lib/ui/Column.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
	import { onMount } from "svelte";
	import { t } from "svelte-i18n";
	// Phase 1 of proto-direct redesign: this page reads SensorData (tag 13)
	// + SensorLabels (tag 28) directly from /proto/stream. No CGI/CSV.
	// See docs/proto-direct-redesign-plan.md.
	import { sensorData, sensorLabels } from "$lib/business/protoStores";

	let title = $t('level1.pile.pile-sensors');

	$: ready = false;
	$: level = $navigationStore.level;

	// Build a Map<index,label> per kind from the SensorLabels proto so we
	// can render labels alongside live readings without polling a second
	// endpoint. proto3 zero-suppression means an absent label_X array just
	// yields an empty list — handled naturally by the .get() fallback.
	$: tempLabelMap = new Map<number, string>(
		($sensorLabels?.tempLabels ?? []).map((l) => [l.index, l.label])
	);
	$: humidLabelMap = new Map<number, string>(
		($sensorLabels?.humidLabels ?? []).map((l) => [l.index, l.label])
	);

	// SensorReading → SensorInfo adapter. The proto already partitions by
	// kind (temperatures vs humidities), so we synthesize a sensible type
	// string for the existing UI helpers (sensorDisplayValue / getAdornment).
	// `valid: false` from firmware → display as "dis" via the disabled flag.
	function toSensorInfo(
		readings: Array<{ index: number; value: number; valid: boolean }>,
		labels: Map<number, string>,
		type: SensorTypes
	): SensorInfo[] {
		return readings
			.map((r) => ({
				id: r.index,
				label: labels.get(r.index) ?? '',
				type: type as string,
				value: r.valid ? r.value.toFixed(1) : '--',
				offset: '0.0',
				disabled: !r.valid,
			}))
			.sort((a, b) => a.id - b.id);
	}

	$: tempSensors = toSensorInfo(
		$sensorData?.temperatures ?? [],
		tempLabelMap,
		SensorTypes.SENSOR_PILE_TEMP
	).filter((s) => isTempSensorType(s.type));

	$: humiditySensors = toSensorInfo(
		$sensorData?.humidities ?? [],
		humidLabelMap,
		SensorTypes.SENSOR_PILE_HUMID
	).filter((s) => isHumidSensorType(s.type) && !isTempSensorType(s.type));

	$: sensors = [...tempSensors, ...humiditySensors];

	onMount(() => {
		ready = true;
	});
</script>

<GellertPage {ready} {title} {level} name="pile">
	{#if sensors.length === 0}
		<Card class="w-1/2 mt-12 p-4 flex-col mx-auto">
			<p class="text-size-xl font-bold text-center">{ $t('level1.pile.no-pile-sensors-found') }</p>
		</Card>
	{:else}
		<div class="flex flex-row">
			<Card class="mt-2 w-1/2 p-4 flex-col mx-1">
				<p class="text-size-xl font-bold text-center mb-3">{ $t('global.pile-temperature') }</p>
				<Table class="text-size-xl">
					{#each tempSensors as sensor}
						<Row><Column class="border-r border-gray-400">{sensor.label}</Column><Column class="tablevalue font-bold">{sensorDisplayValue(sensor)}{@html getAdornment(sensor.type, sensorDisplayValue(sensor))}</Column></Row>
					{/each}
				</Table>
			</Card>
			<Card class="mt-2 w-1/2 p-4 flex-col mx-1">
				<p class="text-size-xl font-bold text-center mb-3">{ $t('global.pile-humidity') }</p>
				<Table class="text-size-xl">
					{#each humiditySensors as sensor}
						<Row><Column class="border-r border-gray-400">{sensor.label}</Column><Column class="tablevalue font-bold">{sensorDisplayValue(sensor)}{@html getAdornment(sensor.type, sensorDisplayValue(sensor))}</Column></Row>
					{/each}
				</Table>
			</Card>
		</div>
	{/if}
</GellertPage>
