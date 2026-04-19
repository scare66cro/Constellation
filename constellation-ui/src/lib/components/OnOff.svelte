<script lang="ts">
	import { AdornmentType } from "$lib/business/adornmentType";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import TextField from "$lib/ui/TextField.svelte";
  import { t } from "svelte-i18n";

  export let on = '';
  export let off = '';
  export let edit = true;
  export let validationOn = '';
  export let validationOff = '';
</script>

<div class="flex flex-row items-center">
  <div class="mx-auto">
    {$t('global.on')}: <TextField size="xl" class="w-16 xl:w-24 3xl:w-36" bind:value={on} {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validationOn}/>
    &nbsp;
    {$t('global.off')}: <TextField size="xl" class="w-16 xl:24 3xl:w-36" bind:value={off} {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validationOff}/>
  </div>
</div>