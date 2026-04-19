<script lang="ts">
	import { type Auxiliary, type Rule } from "$lib/business/auxOptions";
	import { auxiliaryOptionsStore } from "$lib/store";
  import Column from "$lib/ui/Column.svelte";
	import Select from "$lib/ui/Select.svelte";
  import InputOutputMode from "./InputOutputMode.svelte";

  export let rule: Rule;
  export let index: number;
  export let name: string;
  export let aux: Auxiliary;

  const edit = true;

  function onAndOrChange() {
    if (index < 5 && rule.andOr !== '255' && aux.rules[index + 1].andOr === '256') {
      aux.rules[index + 1].andOr = '255';
      aux.rules[index].st = '255';
      aux = aux;
    }
    if (rule.andOr === '255') {
      rule.first = true;
      for (let i = index + 1; i < aux.rules.length; i++) {
        aux.rules[i].andOr = '256';
      }
      aux = aux;
    }
  }
</script>

{#if index === 0}
  <Column class="border-r border-gray-400" rowspan={rule.first ? 1 : 2}>{name}</Column>
{:else}
  <Column class="px-1 border-r border-gray-400" rowspan={rule.first ? 1 : 2}>
    <Select extended="w-64 3xl:w-96" size="xl" bind:value={rule.andOr} options={$auxiliaryOptionsStore.andOrOptions} {edit} on:change={onAndOrChange}/>
  </Column>
{/if}
{#if rule.andOr !== '255' || index === 0}
  <Column class="px-1 border-r border-gray-400">
    <Select extended="w-64 3xl:w-96" size="xl" bind:value={rule.type} options={$auxiliaryOptionsStore.typeOptions} {edit} />
  </Column>
  <InputOutputMode {rule} {aux} on:change/>
{:else}
  <Column colspan={3}></Column>
{/if}
