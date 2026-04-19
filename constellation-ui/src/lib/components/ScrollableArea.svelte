<script lang="ts">
  import { heightsStore } from "$lib/store";
  import { getOffset } from "$lib/business/util";

  let myDiv: HTMLDivElement;
  let saveHeight = 0;
  let height = 768;

  // Compute available height using shared heightsStore + viewport offset
  $: if (typeof window !== 'undefined') {
    const availableHeight = window.innerHeight;
    const offset = getOffset(availableHeight);
    const minBottomMargin = 4;
    height = $heightsStore.main - $heightsStore.header - $heightsStore.footer - offset - minBottomMargin;
  }

  // Set exact scrollable area height minus footer area
  $: if (height !== undefined && height > 0 && myDiv) {
  // Account for the bottom margin on the scroll area so total height incl. margin doesn't overflow
  const marginBottom = parseFloat(getComputedStyle(myDiv).marginBottom || '0') || 0;
  const exactScrollHeight = Math.max(0, height - saveHeight - marginBottom - 15);
  myDiv.style.height = `${exactScrollHeight}px`;
  }
</script>

<div
  class="overflow-y-auto shadow-lg shadow-gray-500 mb-2"
  bind:this={myDiv}
  data-touch-interactive
  style="touch-action: pan-y; -ms-touch-action: pan-y; -webkit-overflow-scrolling: touch; overscroll-behavior: contain;"
>
  <slot />
</div>

<div class="w-full flex flex-row text-center mt-2 mb-0" bind:clientHeight={saveHeight}>
  <div class="w-1/3">
    <slot name="footer-left" />
  </div>
  <div class="w-1/3">
    <slot name="footer-center" />
  </div>
  <div class="w-1/3">
    <slot name="footer-right" />
  </div>
</div>
