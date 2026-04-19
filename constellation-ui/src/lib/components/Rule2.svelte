<script lang="ts">
	import { type Rule } from "$lib/business/auxOptions";
  import Column from "$lib/ui/Column.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { auxiliaryOptionsStore } from "$lib/store";

  export let rule: Rule;
  export let data: Rule;
  export let sensorValidation: string = '';
  export let diffValidation: string = '';

  const edit = true;

  $: if (rule.st === '0') {
    data.diff = rule.diff = '255';
    data.ref =rule.ref = '255';

    if (rule.sen === '255') {
      data.sen =rule.sen = '';
    }
  } else if (rule.st === '1') {
    data.sen = rule.sen = '255';
    if (rule.diff === '255') {
      data.diff =rule.diff = '';
    }
  } else {
    data.sen = rule.sen = '255';
    data.diff = rule.diff = '255';
    data.ref = rule.ref = '255';
  }

</script>
<Column colspan={4}>
  <Select size="xl" extended="mx-auto w-36 3xl:w-64" bind:value={rule.op} options={$auxiliaryOptionsStore.opOptions} {edit} />
  {#if rule.st === '0'}
    <TextField 
      size="xl"
      extended="w-36"
      bind:value={rule.sen}
      keyboardType={KeyboardTypes.Numeric}
      {edit}
      validation={sensorValidation}
    />
  {/if}
  {#if rule.st === '1'}
    <Select size="xl" extended="w-96 3xl:w-144" bind:value={rule.ref} options={$auxiliaryOptionsStore.availSensors(true)} {edit} />
    <TextField size="xl" extended="w-28 3xl:w-36" bind:value={rule.diff} keyboardType={KeyboardTypes.Numeric} {edit} validation={diffValidation}/>
  {/if}
</Column>