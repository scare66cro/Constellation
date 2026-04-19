<script lang="ts">
	import { getAdornment, isHumidSensorType, isTempSensorType, sensorDisplayValue } from "$lib/business/util";
	import { parseSensorFeeds, type SensorInfo } from "$lib/business/sensorFeeds";
	import GellertPage from "$lib/components/GellertPage.svelte";
	import { navigationStore } from "$lib/store";
  	import { getHttpUrl } from "$lib/business/util";
	import Card from "$lib/ui/Card.svelte";
	import Column from "$lib/ui/Column.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
	import { cloneDeep } from "lodash-es";
	import { onDestroy, onMount } from "svelte";
	import { t } from "svelte-i18n";
	import WsClient from "$lib/business/wsClient";

	export let data: { sensors: SensorInfo[] };

	let title = $t('level1.pile.pile-sensors');
	let client: WsClient | undefined;

	$: ready = false;
	$: level = $navigationStore.level;
	$: sensors = [] as SensorInfo[];
	$: tempSensors = sensors.filter((s) => isTempSensorType(s.type)).sort((a, b) => a.id - b.id);
	$: humiditySensors = sensors.filter((s) => isHumidSensorType(s.type) && !isTempSensorType(s.type)).sort((a, b) => a.id - b.id);

	onMount(async () => {
		try {
			sensors = cloneDeep(data.sensors ?? []);
			$navigationStore.data = getHttpUrl('/iot/sensors');
		} catch (error) {
			console.log(error);
		}
		ready = true;

		client = new WsClient(
			getHttpUrl('/iot/ws'),
			'sensor-data',
			(incoming) => {
				const updated = parseSensorFeeds(incoming);
				if (updated.length > 0) {
					sensors = updated;
				}
			}
		);
		client.connect();
	});

	onDestroy(() => {
		client?.close();
		client = undefined;
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
