<script lang="ts">
  import Select from "$lib/ui/Select.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import TextField from "$lib/ui/TextField.svelte";
  import { t } from "svelte-i18n";
	
  export let mode: string;
  export let timer: string;
  export let showMinutes: boolean = false;
  export let modeOptions: Array<{text: string, value: string}> = [];
  export let validation = '';
  
  const edit = true;
</script>
<div class="mx-auto items-center text-size-xl">
  {$t('global.mode')}:
  <Select class="mx-2 w-36 md:w-48 xl:w-64 3xl:w-96" size="xl" bind:value={mode} {edit} options={modeOptions}/>
  {$t('global.timer')}:
  <TextField class="mx-2 w-16 md:w-24 xl:w-28 3xl:w-36" size="xl" bind:value={timer} {edit} keyboardType={KeyboardTypes.Numeric} label="Timer" {validation}/>
  {#if showMinutes}
    {$t('global.minutes')}
  {/if}
  <slot></slot>
</div>
