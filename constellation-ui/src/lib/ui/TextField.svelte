<script lang="ts">
	import { AdornmentType } from "$lib/business/adornmentType";
	import { keyboardStore } from "$lib/store";
	import { createEventDispatcher } from "svelte";
	import { KeyboardTypes } from "./Keyboard.svelte";
  import { getSizeClass } from "$lib/business/util";

  export let edit: boolean;

  export let value: string;

  export let label = 'Edit value';

  export let keyboardType: KeyboardTypes = KeyboardTypes.None;

  export let adornmentType: AdornmentType = AdornmentType.None;

  export let type = 'text';

  export let validation = '';

  export let size = '';
  export let disabled = false;

  $: isValid = validation === '' || validation === undefined;

  $: val = value ?? '--';

  const dispatch = createEventDispatcher();

  function onClick(): void {
    $keyboardStore.keyboardType = keyboardType;
    $keyboardStore.label = label;
    $keyboardStore.start = `${val}`;
    $keyboardStore.resultReady = (data: unknown) => { updateValue(data) };
    $keyboardStore.inputType = type;
    $keyboardStore.hidden = false;
    $keyboardStore = $keyboardStore;
  }

  function updateValue(data: unknown): void {
    value = data as string;
    dispatch('change', value);
  }

  function getAdornment(adornment: AdornmentType): string {
    if (adornment === AdornmentType.Temperature) {
      return '°';
    } else if (adornment === AdornmentType.Humidity || adornment === AdornmentType.Percent) {
      return '%';
    }
    return '';
  }
</script>

{#if edit}
  <div class="relative inline-block {$$props.extended ?? ''}">
    <form autocomplete="off">
    {#if type === 'password'}
      <input
        type="password"
        name="nope"
        class="input rounded-md text-center p-1 my-1 leading-snug {getSizeClass(size)} {!isValid ? 'border-2 border-red-700 bg-red-300' : (disabled ? 'bg-gray-200' : 'bg-white')} {$$props.class ?? ''}"
        bind:value={val}
        on:click={() => { if (!disabled) onClick(); }}
        on:blur={(e) => { if (!disabled) updateValue(e.currentTarget.value); }}
        autocomplete="new-password"
        autocapitalize="off"
        autocorrect="off"
        spellcheck="false"
        {disabled}
        readonly={!disabled}
        on:focus={(e) => { if (!disabled) e.currentTarget.removeAttribute('readonly'); }}
      />
    {:else}
      <input
        type="text"
        name="nope"
        class="input rounded-md text-center p-1 my-1 leading-snug {getSizeClass(size)} {!isValid ? 'border-2 border-red-700 bg-red-300' : (disabled ? 'bg-gray-200' : 'bg-white')} {$$props.class ?? ''}"
        bind:value={val}
        on:click={() => { if (!disabled) onClick(); }}
        on:blur={(e) => { if (!disabled) updateValue(e.currentTarget.value); }}
        autocomplete="off"
        autocapitalize="off"
        autocorrect="off"
        spellcheck="false"
        {disabled}
        readonly={!disabled}
        on:focus={(e) => { if (!disabled) e.currentTarget.removeAttribute('readonly'); }}
      />{getAdornment(adornmentType)}
    {/if}
    {#if !isValid}
      <div class="absolute -bottom-4"><p class="overflow-x-visible whitespace-nowrap text-base xl:text-lg text-red-700">{validation}</p></div>
    {/if}
    </form>
  </div>
{:else}
  <b class="{getSizeClass(size)} {$$props.class ?? ''} {$$props.extended ?? ''}">{type === 'password' ? '*********' : val}{getAdornment(adornmentType)}</b>
{/if}

{#if adornmentType === AdornmentType.Pressure}
  <span class="text-xs md:text-sm xl:text-3xl"><b>psi</b></span>
{:else if adornmentType === AdornmentType.CO2}
  <span class="text-xs md:text-sm xl:text-3xl"><b>ppm</b></span>
{:else if adornmentType === AdornmentType.Hours}
  <span class="text-xs md:text-sm xl:text-3xl"><b>hrs</b></span>
{:else if adornmentType === AdornmentType.StaticPressure}
  <b class="text-xs md:text-sm xl:text-3xl">"wc</b>
{/if}
