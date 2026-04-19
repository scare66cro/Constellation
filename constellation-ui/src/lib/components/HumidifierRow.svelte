<script lang="ts">
	import { getHttpUrl } from "$lib/business/util";
	import Column from "$lib/ui/Column.svelte";
	import Row from "$lib/ui/Row.svelte";
	import { t } from "svelte-i18n";

    export let edit: boolean = false;
    export let wait: boolean = false;
    export let rows: any[] = [];
    export let pump: any[] = [];

    // Derive mode per humidifier from remoteStatus: '0'=auto, '1'=off, '2'=manual
    let modes: ('auto' | 'off' | 'manual')[] = [];
    $: modes = rows.map((row) =>
        row.remoteStatus === '2' ? 'manual' : row.remoteStatus === '1' ? 'off' : 'auto'
    );

    let humidifierCount: number;
    $: humidifierCount = rows.length;

    async function onModeChange(index: number) {
        wait = true;
        const value = modes[index] === 'auto' ? 'Auto' : modes[index] === 'manual' ? 'On' : 'Off';
        await fetch(getHttpUrl('/iot/button'), {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                tag: 'button2',
                [`humid${index + 1}PumpBtn`]: value,
            }),
        });
        wait = false;
    }

    function modeColor(mode: string): string {
        return mode === 'auto' ? 'text-green-700 font-bold'
             : mode === 'manual' ? 'text-blue-700 font-bold'
             : 'text-red-500 font-bold';
    }
</script>
{#each rows as row, index}
    <Row>
        <Column class="border-r border-gray-400 {row.outputColor}">
            Humidifier {index + 1}{!edit ? ' - Head' : ''}
        </Column>
        <Column class="border-r border-gray-400 {row.statusColor}" rowspan={edit ? 1 : 2}>
            {row.equipmentStatus}
        </Column>
        {#if index === 0}
            <Column class="{row.panelSwitchColor}" rowspan={(edit ? 1 : 2) * humidifierCount}>
                {#if edit && row.remSwitchName}
                    {#each rows as _, i}
                        <select
                            class="w-full text-center text-size-xl py-1 rounded border border-gray-300 {modeColor(modes[i])}"
                            bind:value={modes[i]}
                            on:change={() => onModeChange(i)}
                        >
                            <option value="auto">{$t('global.auto')}</option>
                            <option value="off">{$t('global.off')}</option>
                            <option value="manual">{$t('global.manual')}</option>
                        </select>
                    {/each}
                {:else}
                    {row.panelSwitchStatus}
                {/if}
            </Column>
        {/if}
    </Row>
    {#if !edit}
        <Row>
            <Column class="border-r border-gray-400 {pump[index].outputColor}">
                Humidifier {index + 1} - Pump
            </Column>
        </Row>
    {/if}
{/each}
