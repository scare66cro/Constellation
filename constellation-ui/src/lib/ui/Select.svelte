<script lang="ts">
	import { type PageType } from "$lib/business/PageType";
	import { getSizeClass } from "$lib/business/util";

  export let edit: boolean;
  export let options: PageType[] | undefined = undefined;
  export let value: string | number | string[] | undefined | null;
  export let multiple = false;
  export let disabled = false;
  export let validation = '';
  export let size = '';
  export let inline = true;

  $: isValid = validation === '' || validation === undefined;
</script>

{#if edit}
  <div class="{inline ? 'inline-block relative' : 'flex flex-row'} {$$props.extended ?? ''}">
    {#if multiple}
      <select
        id={$$props.id ?? Date.now()}
        class="select bg-white text-center px-4 md:px-8 py-2 my-1 leading-snug {getSizeClass(size)} {!isValid ? 'border-2 border-red-700 bg-red-300' : 'bg-white'} {$$props.class ?? ''}"
        bind:value={value}
        on:change
        multiple
        disabled={disabled}
      >
        {#if options}
          {#each options as option}
            {#if option.display === undefined || option.display}
              <option value={option.value}>{option.text}</option>
            {/if}
          {/each}
        {:else}
          <slot />
        {/if}
      </select>
    {:else}
      <select
        id={$$props.id ?? Date.now()}
        class="select bg-white text-center px-4 md:px-8 py-1 my-1 leading-snug {getSizeClass(size)} {!isValid ? 'border-2 border-red-700 bg-red-300' : 'bg-white'} {$$props.class ?? ''}"
        bind:value={value}
        on:change
        disabled={disabled}
      >
        {#if options}
          {#each options as option}
            {#if option.display === undefined || option.display}
              <option value={option.value}>{option.text}</option>
            {/if}
          {/each}
        {:else}
          <slot />
        {/if}
      </select>
    {/if}
    {#if !isValid}
      <div class="absolute left-8 -bottom-4"><p class="overflow-x-visible whitespace-nowrap text-base xl:text-lg text-red-700">{validation}</p></div>
    {/if}
  </div>
{:else}
  <b class="py-1 {size === 'xl' ? 'text-size-xl' : 'text-size-large'} {$$props.noeditclass ?? ''}">{options?.find((i) => i.value === value)?.text ?? ''}</b>
{/if}

<style lang="postcss">
  option {
    @apply !bg-white !text-black;
  }
</style>