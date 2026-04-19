<script lang="ts">
	import { AdornmentType } from "$lib/business/adornmentType";
	import Button from "$lib/ui/Button.svelte";
	import Column from "$lib/ui/Column.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import Row from "$lib/ui/Row.svelte";
	import TextField from "$lib/ui/TextField.svelte";
    import { t } from "svelte-i18n";
	import EquipmentRow from "./EquipmentRow.svelte";
	import { getHttpUrl } from "$lib/business/util";

    export let row: EquipmentRow;
    export let wait: boolean = false;
    let error: boolean = false;

    let validation: Record<string, string> = {
        'target': ''
    };

    async function postButton(diagOn: boolean) {
        Object.keys(validation).forEach((key) => validation[key] = '');

        const remSwitchName = row.remSwitchName;
        wait = true;
        error = false;
        const result = await fetch(getHttpUrl('/iot/button'), {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                tag: 'button2',
                [remSwitchName]: diagOn ? 'Open' : 'Close',
                target: row.target,
            }),
        });
        const json = await result.json();
        if (json.data?.Type === 'Validation') {
            error = true;
            if (json.data?.Type === 'Validation') {
                Object.keys(json.data?.errors).forEach((key) => {
                    const error = (json.data.errors[key] as string[])[0];
                    if (error.indexOf(':') > -1) {
                        validation[key] = error.split(':').slice(1).join(' ');
                    } else {
                        validation[key] = error;
                    }
                });
            }
        }
        wait = false;
    }

    async function clear() {
        wait = true;
        const result = await fetch(getHttpUrl('/iot/button'), {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            tag: 'button2',
            ClearDoorDiag: 'Clear',
        }),
        });
        wait = false;
    }
</script>

<Row>
    <Column class="border-r border-gray-400 {row.outputColor}">
        {row.equipmentName}
    </Column>
    <Column class="border-r border-gray-400 {row.statusColor}">
        {row.equipmentStatus}
    </Column>
    <Column>
        {#if row.remSwitchName}
            {$t('level1.equipment.target')} <TextField class="w-16 3xl:w-36 mx-2" size="lg" bind:value={row.target} keyboardType={KeyboardTypes.Numeric} edit={true} adornmentType={AdornmentType.Percent} validation={validation.target}/>
            <Button size="lg" class="mr-1" on:click={() => postButton(true)}>{$t('global.open')}</Button>
            <Button size="lg" on:click={() => postButton(false)}>{$t('global.close')}</Button>
        {/if}
    </Column>
</Row>
<Row>
    <Column colspan={3}>
        {$t('level1.equipment.clear-door-diagnostics')} <Button size="xl" on:click={clear}>{$t('global.clear')}</Button>
    </Column>
</Row>
