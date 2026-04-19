<script lang="ts">
  export let height: number | undefined = undefined;

  let myDiv: HTMLDivElement;

  $: if (height !== undefined && height > 0 && myDiv) myDiv.style.height = `${height}px`;
</script>

<div
  class="card bg-gray-300 opacity-80 p-1 md:p-2 shadow-md md:shadow-lg shadow-gray-500 text-xs md:text-lg xl:text-xl {$$props.class ?? ''}"
  bind:this={myDiv}
>
  <slot />
</div>