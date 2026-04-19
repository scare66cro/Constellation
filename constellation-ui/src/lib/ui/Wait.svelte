<script lang="ts">
	import { ProgressRadial } from "@skeletonlabs/skeleton";

  export let show = false;
  export let current: number | undefined = undefined;
  export let total: number | undefined = undefined;
  export let message: string | undefined = undefined;
  /**
   * Wait will display over the content (like a dialog box does)
  */
</script>

{#if show}
  <div
    class="min-w-screen h-screen animated fadeIn faster fixed left-0 top-0 flex justify-center items-center inset-0 z-50 outline-none focus:outline-none"
  >
    <div class="relative mx-auto my-auto flex flex-col items-center">
      <ProgressRadial />
      {#if current !== undefined && total !== undefined}
        <div class="text-white mt-2 text-center text-size-large">{current} of {total}</div>
      {/if}
      {#if message}
        <div class="text-white mt-4 text-center text-2xl font-bold">{message}</div>
      {/if}
    </div>
  </div>
  <div class="opacity-25 fixed inset-0 z-40 bg-black" />
{/if}
