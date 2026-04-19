<script lang="ts">
  export let height: number | undefined = undefined;
  export let width: number | undefined = undefined;

  let table: HTMLTableElement;

  $: if (height) height = table?.clientHeight;
  $: if (width) width = table?.clientWidth;
</script>

<table class="table flex-1 shadow-md md:shadow-xl rounded-none shadow-gray-500 {$$props.class ?? ''}" style={$$props.style ?? ''} bind:this={table}>
  <slot />
</table>