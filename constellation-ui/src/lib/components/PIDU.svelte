<script lang="ts">
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import TextField from "$lib/ui/TextField.svelte";

  export let p = '0';
  export let i = '0';
  export let d = '0';
  export let u = '0';
  export let edit = true;
  export let pvalid = '';
  export let ivalid = '';
  export let dvalid = '';
  export let uvalid = '';
</script>

<div class="flex flex-row items-center">
  <div class="mx-auto text-size-xl">
    P <TextField class="xl:w-28 3xl:w-36 text-size-xl" bind:value={p} {edit} keyboardType={KeyboardTypes.Numeric} validation={pvalid}/>
    I <TextField class="xl:w-28 3xl:w-36 text-size-xl" bind:value={i} {edit} keyboardType={KeyboardTypes.Numeric} validation={ivalid}/>
    D <TextField class="xl:w-28 3xl:w-36 text-size-xl" bind:value={d} {edit} keyboardType={KeyboardTypes.Numeric} validation={dvalid}/>
    U <TextField class="xl:w-28 3xl:w-36 text-size-xl" bind:value={u} {edit} keyboardType={KeyboardTypes.Numeric} validation={uvalid}/>
  </div>
</div>