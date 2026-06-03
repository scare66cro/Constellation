<script lang="ts">
	import type { IOConfigType } from "$lib/business/ioConfig";
	import Column from "$lib/ui/Column.svelte";
	import Select from "$lib/ui/Select.svelte";
  import { createEventDispatcher } from "svelte";
  import { t } from "svelte-i18n";
  import { mdiPencil } from '@mdi/js';
  import Icon from '$lib/ui/Icon.svelte';
  import { keyboardStore } from "$lib/store";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";

  const dispatch = createEventDispatcher();

  export let ioConfig: IOConfigType;
  export let i: number;
  export let j: number;
  export let outputList: { text: string, value: string }[];
  export let lights: { text: string, value: string }[];
  export let value: string;
  export let validation = '';

  const edit = true;
  let pid = (i * 12) + j;
  $: board = pid / 12;
  $: io = pid % 12;
  // Bay lights freely assignable to any output. AS2's legacy
  // `board >= 1 && io >= 7` restriction was an expansion-board-1
  // port-7/8 hardware constraint that doesn't apply on Constellation
  // (no panel switches; operator picks any DO).
  $: equip = [...outputList, ...lights];
  $: outputConfig = value;

  function dispatchChange(event: Event) {
    const eventDetail = {
      i,
      j,
      value: (event.target as HTMLSelectElement).value,
    };
    dispatch('change', eventDetail);
  }
</script>

<Column class="w-5/12 border-r border-gray-400 px-2 text-size-xl">
  <div class="flex flex-row w-full items-center">
    <div class="flex-1 flex justify-center relative">
      <div class="w-full">
        <Select class="mx-12" inline={false} size="xl" bind:value={outputConfig} options={equip} {edit} on:change={dispatchChange} validation={validation}/>
      </div>
      {#if ioConfig.ioNames[parseInt(ioConfig.config.outputConfig[pid],10)]?.renamable}
        <button
          class="absolute right-0 top-1/2 -translate-y-1/2 ml-2 text-sm hover:text-primary-500"
          on:click={() => {
            $keyboardStore.keyboardType = KeyboardTypes.Alpha;
            $keyboardStore.label = "Edit Equipment Name";
            $keyboardStore.start = ioConfig.ioNames[parseInt(ioConfig.config.outputConfig[pid],10)]?.name || '';
            $keyboardStore.inputType = 'text';
            $keyboardStore.resultReady = (name) => {
              dispatch('rename', { i, j, name, type: 0 });
            };
            $keyboardStore.hidden = false;
          }}
        >
          <Icon class="fill-gray-500 stroke-gray-500 bg-gray-300 bg-opacity-10" src={mdiPencil} />
        </button>
      {/if}
    </div>
  </div>
</Column>