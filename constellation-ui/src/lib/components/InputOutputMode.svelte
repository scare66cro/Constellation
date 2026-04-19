<script lang="ts">
	import { type Auxiliary, type Rule } from "$lib/business/auxOptions";
	import Column from "$lib/ui/Column.svelte";
  import Select from "$lib/ui/Select.svelte";
  import { auxiliaryOptionsStore } from "$lib/store";

  export let rule: Rule;
  export let aux: Auxiliary;

  const potatoMode = aux.systemMode === '0';
  const pecanMode = aux.systemMode === '3';
  const onionMode = aux.systemMode === '1';
  const beeMode = aux.systemMode === '2';

  const edit = true;

  function availEquip(type: string): { text: string, value: string }[] {
    const equip: { text: string, value: string }[] = [];
    const ioConfig = type === 'input' ? aux.InputConfig : aux.OutputConfig;

    let listInfo: string[];

    for (let i = 0; i < ioConfig.length; i += 1) {
        if (ioConfig[i] !== '-1') {
            listInfo = aux.IoNames[i].split(':');
            if (((potatoMode || pecanMode) && listInfo[1] === '1')
                || (onionMode && listInfo[1] === '2')
                || (beeMode && (listInfo[1] === '4' || listInfo[1] === '5'))
                || listInfo[1] === '4' || listInfo[1] === '7') {
                if ((type === 'output' && listInfo[2] === '0')
                    || (type === 'input' && listInfo[2] === '1')
                    || listInfo[2] === '2') {
                    equip.push({ text: listInfo[0], value: listInfo[4]});
                }
            }
        }
    }
    if (type === 'output' && ioConfig[40] === '1') {
        listInfo = aux.IoNames[41].split(':');
        equip.push({ text: listInfo[0], value: listInfo[4]});
        listInfo = aux.IoNames[42].split(':');
        equip.push({ text: listInfo[0], value: listInfo[4]});
    }

    return equip;
  }

  function availSwitch(): { text: string, value: string }[] {
    const switches: { text: string, value: string }[] = [];
    for (let i = 0; i < aux.IoNames.length; i += 1) {
      var listInfo = aux.IoNames[i].split(':');
      if ((listInfo[2] === '3') && (
        ((potatoMode || pecanMode) && listInfo[1] === '1')
        || (onionMode && listInfo[1] === '2')
        || (beeMode && (listInfo[1] === '4' || listInfo[1] === '5'))
        || listInfo[1] === '4' || listInfo[1] === '7')
      ) {
        switches.push({ text: listInfo[0], value: listInfo[4]});
      }
    }
    return switches;
  }
</script>

{#if rule.type === '1'}
  <Column class="px-1">
    <Select class="w-96 3xl:w-144" size="xl" bind:value={rule.io} options={availEquip('output')} {edit} />
  </Column>
  <Column class="px-1">
    <Select class="w-36 3xl:w-64" size="xl" bind:value={rule.st} options={$auxiliaryOptionsStore.onOffOptions} {edit} />
  </Column>
{:else if rule.type === '2'}
  <Column class="px-1">
    <Select class="w-96 3xl:w-144" size="xl" bind:value={rule.io} options={availEquip('input')} {edit} />
  </Column>
  <Column class="px-1">
    <Select class="w-36 3xl:w-64" size="xl" bind:value={rule.st} options={$auxiliaryOptionsStore.onOffOptions} {edit} />
  </Column>
{:else if rule.type === '3'}
  <Column class="px-1">
    <Select class="w-96 3xl:w-144" size="xl" bind:value={rule.io} options={availSwitch()} {edit} />
  </Column>
  <Column class="px-1">
    <Select class="w-36 3xl:w-64" size="xl" bind:value={rule.st} options={$auxiliaryOptionsStore.onOffOptions} {edit} />
  </Column>
{:else if rule.type === '4'}
  <Column class="px-1">
    <Select class="w-96 3xl:w-144" size="xl" bind:value={rule.io} options={$auxiliaryOptionsStore.availSensors(false)} {edit} />
  </Column>
  <Column class="px-1">
    <Select class="w-64 3xl:w-96" size="xl" bind:value={rule.st} options={$auxiliaryOptionsStore.auxProgOptions} {edit} on:change/>
  </Column>
{:else if rule.type === '5'}
  <Column class="px-1">
    <Select class="w-64 3xl:w-96" size="xl" bind:value={rule.io} options={$auxiliaryOptionsStore.modeOptions} {edit} />
  </Column>
{/if}