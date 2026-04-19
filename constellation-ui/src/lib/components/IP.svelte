<script lang="ts">
  import TextField from "$lib/ui/TextField.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";

  export let ip: string[] = Array(4).fill('');
  export let port: string = '';
  export let width = 'w-36';
  export let edit = true;

  $: mr = port === '' ? 'mr-auto' : '';
</script>

<div class="flex flex-row {$$props.class ?? ''}">
  <TextField class="py-0" size="xl" extended="ml-auto {width}" bind:value={ip[0]} keyboardType={KeyboardTypes.Numeric} {edit}/>
  .
  <TextField class="py-0 {width}" size="xl" bind:value={ip[1]} keyboardType={KeyboardTypes.Numeric} {edit}/>
  .
  <TextField class="py-0 {width}" size="xl" bind:value={ip[2]} keyboardType={KeyboardTypes.Numeric} {edit}/>
  .
  <TextField class="py-0 {width}" size="xl" extended="{mr}" bind:value={ip[3]} keyboardType={KeyboardTypes.Numeric} {edit}/>
  {#if port !== ''}
    : <TextField class="py-0 mr-auto {width}" size="xl" bind:value={port} keyboardType={KeyboardTypes.Numeric} {edit}/>
  {/if}
</div>