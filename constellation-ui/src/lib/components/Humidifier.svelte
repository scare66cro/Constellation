<script lang="ts">
	import { EQUIP_NOT_DEFINED, INPUT_GOOD, OUTPUT_ON, SWITCH_AUTO, SWITCH_MANUAL, SWITCH_OFF } from "$lib/business/mode";

    export let boardType: string;
    export let humidifierSwitch: string;
    export let humidifierEquipment: string;
    export let humidifierInput: string;
    export let humidifierHeadOutput: string;
    export let humidifierPumpOutput: string;
    export let humidifierNumber: number;
</script>

{#if boardType === 'Agri-Star' || (boardType === 'AS2' && humidifierEquipment !== EQUIP_NOT_DEFINED)}
<div class="flex h-full w-1/6">
    <button on:click>
        {#if humidifierInput === INPUT_GOOD && humidifierSwitch === SWITCH_AUTO && humidifierHeadOutput === OUTPUT_ON}
            {#if humidifierPumpOutput === OUTPUT_ON}
                <img src="/humid-anim-50x50.gif" alt="Humidifier {humidifierNumber} On" class="lg:h-16 lg:w-16 3xl:h-24 3xl:w-24"/>
            {:else}
                <img src="/humid-off-50x50.png" alt="Humidifier {humidifierNumber} Off" class="lg:h-16 lg:w-16 3xl:h-24 3xl:w-24"/>
            {/if}
        {:else if humidifierInput === INPUT_GOOD && humidifierSwitch === SWITCH_MANUAL}
            <img src="/humid-anim-50x50.gif" alt="Humidifier {humidifierNumber} On" class="lg:h-16 lg:w-16 3xl:h-24 3xl:w-24"/>
        {:else if humidifierInput === INPUT_GOOD && humidifierSwitch === SWITCH_AUTO && humidifierHeadOutput !== OUTPUT_ON}
            <img src="/humid-off-50x50.png" alt="Humidifier {humidifierNumber} Off" class="lg:h-16 lg:w-16 3xl:h-24 3xl:w-24"/>
        {:else if humidifierInput === INPUT_GOOD && humidifierSwitch === SWITCH_OFF}
            <img src="/humid-off-50x50.png" alt="Humidifier {humidifierNumber} Off" class="lg:h-16 lg:w-16 3xl:h-24 3xl:w-24"/>
        {/if}
    </button>
</div>
{/if}