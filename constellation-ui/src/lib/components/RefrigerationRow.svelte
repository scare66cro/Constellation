<script lang="ts">
	import { getHttpUrl } from "$lib/business/util";
	import Button from "$lib/ui/Button.svelte";
	import Column from "$lib/ui/Column.svelte";
	import Row from "$lib/ui/Row.svelte";
	import { onMount } from "svelte";
    import { t } from "svelte-i18n";

    export let rows: any[] = [];
    export let edit: boolean = false;
    export let wait: boolean = false;

    let refrigerationCount: number;

    async function postButton(remSwitchName: string, diagOn: boolean) {
        wait = true;
        const result = await fetch(getHttpUrl('/iot/button'), {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                tag: 'button2',
                [remSwitchName]: diagOn ? 'On' : 'Off',
            }),
        });
        wait = false;
    }

    onMount(() => {
        refrigerationCount = rows.length;
    });
</script>

{#each rows as row}
    <Row>
        <Column class="w-6/12 border-r border-gray-400 {row.outputColor}">
            {row.equipmentName}
        </Column>
        <Column class="w-2/12 border-r border-gray-400 {row.statusColor}">
            {row.equipmentStatus}
        </Column>
        {#if row.name === 'refrig1' && !edit}
            <Column class="w-4/12 {row.panelSwitchColor}" rowspan={refrigerationCount}>
                {row.panelSwitchStatus}
            </Column>
        {:else if edit}
            <Column class="w-4/12">
                {#if row.remSwitchName}
                    <Button size="xl" class="mr-2" on:click={() => postButton(row.remSwitchName, true)}>{$t('global.on')}</Button>
                    <Button size="xl" on:click={() => postButton(row.remSwitchName, false)}>{$t('global.off')}</Button>
                {/if}
            </Column>
        {/if}
    </Row>
{/each}