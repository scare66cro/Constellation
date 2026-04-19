<script lang="ts">
  import { twMerge } from '$lib/utils/twMerge';
  
  export let colspan: number | undefined = undefined;
  export let rowspan: number | undefined = undefined;
  
  let className = '';
  export { className as class };
  
  $: mergedClasses = twMerge('py-1 xl:py-2 xl3:py-3 text-center', className);
</script>

<td class={mergedClasses} style={$$props.style} {colspan} {rowspan}><slot /></td>
