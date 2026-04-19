<script lang="ts">
	import type { PageType } from "$lib/business/PageType";

  export let edit: boolean;
  export let options: PageType[] | undefined = undefined;
  export let value: string | string[];
  export let multiple = false;
  export let size = 1;
</script>

{#if edit}
  {#if multiple}
    <select
      class="select bg-white text-center px-4 md:px-8 {$$props.class ?? ''}"
      style={$$props.style ?? ''}
      bind:value={value}
      on:change
      multiple
      {size}
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
      class="select bg-white text-center px-4 md:px-8 {$$props.class ?? ''}"
      style={$$props.style ?? ''}
      bind:value={value}
      on:change
      {size}
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
{:else}
  <b class="text-size-xl">{options?.find((i) => i.value === value)?.text}</b>
{/if}

<style lang="postcss">
  .select option {
    @apply text-2xl !text-black;
    background: white;
  }
  .select option:checked {
    @apply text-2xl !text-white;
    background: rgb(var(--color-primary-900) / 1);
  }
  .select option:checked:focus {
    @apply text-2xl !text-white;
    background: rgb(var(--color-primary-900) / 1) !important;
    outline: 2px solid rgb(var(--color-primary-600) / 1);
    outline-offset: 2px;
  }
  .select option:hover {
    @apply text-2xl !text-white;
    background: rgb(var(--color-primary-100) / 1);
  }
  .select option:focus {
    @apply text-2xl !text-white;
    background: white !important;
    outline: 2px solid rgb(var(--color-primary-600) / 1);
    outline-offset: 2px;
  }
  .select option:active {
    @apply text-2xl !text-white;
    background: rgb(var(--color-primary-600) / 1);
  }
</style>