<script lang="ts">
	import { getSizeClass } from "$lib/business/util";
	import { keyboardStore } from "$lib/store";

    export let size='';
    export let noFocus = false;
    
    function handleClick(event: MouseEvent) {
        // Remove focus after click to prevent focus state
        if (noFocus) {
            (event.target as HTMLButtonElement).blur();
        }
        // Propagate the click event to parent components
        return true;
    }
    let isDisabledDueToKeyboard = false;
    let keyboardHideTimeout: NodeJS.Timeout;

  // Watch for keyboard visibility changes

  $: {
    if (!$keyboardStore.hidden) {
      // Keyboard is visible - disable immediately
      isDisabledDueToKeyboard = true;
      // Clear any existing timeout
      if (keyboardHideTimeout) {
        clearTimeout(keyboardHideTimeout);
      }
    } else if (isDisabledDueToKeyboard) {
      // Keyboard just became hidden - keep disabled for a short period
      keyboardHideTimeout = setTimeout(() => {
        isDisabledDueToKeyboard = false;
      }, 300); // 300ms delay to prevent click propagation
    }
  }
</script>

<button type="button"
    id={$$props.id ?? Date.now()}
    class="btn cursor-inherit {getSizeClass(size)} ring-1 ring-[#003151] text-white bg-primary-900 hover:bg-primary-700 active:scale-95 transform transition-transform duration-100 shadow-md md:shadow-lg shadow-gray-500 rounded-md md:rounded-lg py-2 px-4 my-2 {$$props.class ?? ''} disabled:pointer-events-none disabled:opacity-10"
    on:click={handleClick}
    on:click
    disabled={$$props.disabled || isDisabledDueToKeyboard}
>
    <slot></slot>
</button>
