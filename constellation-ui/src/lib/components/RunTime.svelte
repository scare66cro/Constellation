<script lang="ts">
	import { headersStore } from "$lib/store";
	import Button from "$lib/ui/Button.svelte";
	import Column from "$lib/ui/Column.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
	import { createEventDispatcher } from 'svelte';

	export let edit: boolean;
  export let runtimes: string[];
  export let isAM = true;
  export let currentRunOperation = '3';
  export let getOperationColor: (value: string, doHighLight: boolean) => string;

  const dispatch = createEventDispatcher();

  let times = [
    '12:00', '12:30', '1:00', '1:30', '2:00', '2:30', '3:00', '3:30', '4:00', '4:30', '5:00', '5:30', 
    '6:00', '6:30', '7:00', '7:30', '8:00', '8:30','9:00', '9:30', '10:00', '10:30', '11:00', '11:30',
  ];

  let currentTime = '';
  let isCurrentTimeAM = true;
  let touchStartX = 0;
  let touchStartY = 0;
  let touchStartTime = 0;

  $: if ($headersStore?.DateTime && $headersStore?.DateTime !== '') {
    const t = $headersStore?.DateTime?.[1]?.split(':');
    const m = t ? parseInt(t[1]) : new Date().getMinutes();
    const h = t ? t[0] : new Date().getHours();
    currentTime = `${h}:${m < 30 ? '00' : '30'}`;
    isCurrentTimeAM = $headersStore?.DateTime?.[2] === '0';
  }

  $: index = isAM ? 0 : 24;

  function changeRunOperation(index: number) {
    if (runtimes.length > index) {
      runtimes[index] = currentRunOperation;
    }
  }

  function handleSwipeLeft() {
    // Swipe left: go to next page
    dispatch('nextPage');
  }

  function handleSwipeRight() {
    // Swipe right: go to previous page  
    dispatch('previousPage');
  }

  function handleTouchStart(e: TouchEvent) {
    if (e.touches.length !== 1) return;
    const touch = e.touches[0];
    touchStartX = touch.clientX;
    touchStartY = touch.clientY;
    touchStartTime = Date.now();
  }

  function handleTouchEnd(e: TouchEvent) {
    if (e.changedTouches.length !== 1) return;
    
    const touch = e.changedTouches[0];
    const deltaTime = Date.now() - touchStartTime;
    const deltaX = touch.clientX - touchStartX;
    const deltaY = touch.clientY - touchStartY;
    
    // Only process swipe if it's quick enough and meets distance thresholds
    if (deltaTime <= 300) {
      const absX = Math.abs(deltaX);
      const absY = Math.abs(deltaY);
      
      // Check if horizontal movement is sufficient and vertical movement is restrained
      if (absX >= 50 && absY <= 100) {
        if (deltaX > 0) {
          // Swipe right
          handleSwipeRight();
        } else {
          // Swipe left
          handleSwipeLeft();
        }
      }
    }
  }
</script>

<div 
  class="select-none"
  on:touchstart={handleTouchStart}
  on:touchend={handleTouchEnd}
>
<Table>
  <Row>
    {#each runtimes?.slice(index, index + 12) as runtime, i}
      <Column class="w-1/12 !m-0 p-0">
        <Button class="p-1 3xl:py-2 !my-0 mx-1 w-10 md:w-12 xl:w-20 2xl:w-24 3xl:w-32 !{getOperationColor(runtime, (isAM === isCurrentTimeAM) && currentTime === times[i])}" size="xl" on:click={() => changeRunOperation(isAM ? i : i + 24)} disabled={!edit}>
          {times[i]}
        </Button>
      </Column>
    {/each}
  </Row>
  <Row>
    {#each runtimes?.slice(index + 12, index + 24) as runtime, i}
      <Column class="w-1/12 !m-0 p-0">
        <Button class="p-1 3xl:py-2 !my-0 mx-1 w-10 md:w-12 xl:w-20 2xl:w-24 3xl:w-32 !{getOperationColor(runtime, (isAM === isCurrentTimeAM) && currentTime === times[i + 12])}" size="xl" on:click={() => changeRunOperation(isAM ? i + 12 : i + 36)} disabled={!edit}>
          {times[i + 12]}
        </Button>
      </Column>
    {/each}
  </Row>
</Table>
</div>
