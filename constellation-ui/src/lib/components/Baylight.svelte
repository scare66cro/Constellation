<script lang="ts">
	import { EQUIP_NOT_DEFINED, INPUT_GOOD } from "$lib/business/mode";

    export let boardType: string;
    export let bayLight1Equipment: string;
    export let bayLight1Input: string;
    export let bayLight2Equipment: string;
    export let bayLight2Input: string;
</script>

{#if boardType === 'Agri-Star' ||
    (boardType === 'AS2' && (
        bayLight1Equipment !== EQUIP_NOT_DEFINED ||
        bayLight2Equipment !== EQUIP_NOT_DEFINED
    ))
}
    {#if bayLight1Input === INPUT_GOOD || bayLight2Input === INPUT_GOOD}
        <div class="flex h-full w-1/6">
            <button on:click>
                <img src="/bulb-50x50.png" alt="Bay Lights On" class="lg:h-16 lg:w-16 3xl:h-24 3xl:w-24"/>
            </button>
        </div>
    {/if}
{/if}