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

    for (let i = 0; i < ioConfig.length; i += 1) {
        if (ioConfig[i] !== '-1') {
            const entry = aux.IoNames[i];
            if (!entry) continue;
            const idStr = String(entry.index);
            if (((potatoMode || pecanMode) && entry.mode === 1)
                || (onionMode && entry.mode === 2)
                || (beeMode && (entry.mode === 4 || entry.mode === 5))
                || entry.mode === 4 || entry.mode === 7) {
                if ((type === 'output' && entry.ioType === 0)
                    || (type === 'input' && entry.ioType === 1)
                    || entry.ioType === 2) {
                    equip.push({ text: entry.name, value: idStr});
                }
            }
        }
    }
    if (type === 'output' && ioConfig[40] === '1') {
        const e41 = aux.IoNames[41];
        if (e41) equip.push({ text: e41.name, value: String(e41.index) });
        const e42 = aux.IoNames[42];
        if (e42) equip.push({ text: e42.name, value: String(e42.index) });
    }

    return equip;
  }

  function availSwitch(): { text: string, value: string }[] {
    const switches: { text: string, value: string }[] = [];
    for (let i = 0; i < aux.IoNames.length; i += 1) {
      const entry = aux.IoNames[i];
      if (entry.ioType === 3 && (
        ((potatoMode || pecanMode) && entry.mode === 1)
        || (onionMode && entry.mode === 2)
        || (beeMode && (entry.mode === 4 || entry.mode === 5))
        || entry.mode === 4 || entry.mode === 7)
      ) {
        switches.push({ text: entry.name, value: String(entry.index) });
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