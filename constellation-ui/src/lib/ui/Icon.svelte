<script lang="ts">
  export let src: string;
  export let size = 24;
</script>

<svg
  class="w-8 h-8 inline-block stroke-current {$$restProps.class}"
  viewBox="0 0 {size} {size}"
  fill-rule="evenodd"
  clip-rule="evenodd"
  class:disabled={$$restProps.disabled}
>
  <path d={src} />
</svg>

<style lang="postcss">
  svg.disabled {
    @apply text-gray-100;
  }
</style>
