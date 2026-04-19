<script lang="ts">
	import { EQUIP_NOT_DEFINED, INPUT_GOOD, OUTPUT_ON, SWITCH_AUTO, SWITCH_MANUAL, SWITCH_OFF, SYSTEM_MODE } from '$lib/business/mode';

	export let boardType: string;
	export let systemMode: string;
	export let climacellEquipment: string;
	export let climacellInput: string;
	export let climacellSwitch: string;
	export let climacellOutput: string;
</script>

<div class="flex h-full w-1/6">
	{#if systemMode === SYSTEM_MODE.POTATO_MODE && (
		boardType === 'Agri-Star' || (boardType === 'AS2' && climacellEquipment !== EQUIP_NOT_DEFINED)
	) && climacellInput === INPUT_GOOD &&
	((climacellSwitch === SWITCH_AUTO && climacellOutput === OUTPUT_ON) || climacellSwitch === SWITCH_MANUAL)}
		<button on:click>
			<img src="climacell-anim-50x50.gif" alt="Climacell" class="lg:h-16 lg:w-16 3xl:h-24 3xl:w-24"/>
		</button>
	{/if}
</div>
