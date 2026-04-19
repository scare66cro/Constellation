<script lang="ts">
  import type { IOConfigType } from "$lib/business/ioConfig";
	import Column from "$lib/ui/Column.svelte";
	import Select from "$lib/ui/Select.svelte";
  import Icon from '$lib/ui/Icon.svelte';
  import { mdiPencil } from '@mdi/js';
  import { keyboardStore } from "$lib/store";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { t } from "svelte-i18n";
  import { createEventDispatcher } from "svelte";

  export let ioConfig: IOConfigType;
  export let i: number;
  export let j: number;
  export let inputList: { text: string, value: string }[];
  export let lights: { text: string, value: string }[];
  export let ioInfo: string[];
  export let value: string;
  export let validation = '';

  const edit = true;

  const dispatch = createEventDispatcher();

  let pid = (i * 12) + j;
  $: board = pid / 12;
  $: io = pid % 12;
  let numInputs = parseInt(ioInfo[2], 10);
  $: equip = board >= 1 && io >= 7 ? [ ...inputList, ...lights ]: inputList;

  $: inputConfig = value;

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
  {#if j <= numInputs}
    {#if i === 0 && j === 1}
      <div class="my-2">
        {ioConfig.ioNames[parseInt(ioConfig.config.inputConfig[pid],10)]?.split(':')[0] ?? $t('global.none')}*
      </div>
    {:else}
      <div class="flex flex-row w-full items-center">
        <div class="flex-1 flex justify-center relative">
          <div class="w-full">
            <Select class="mx-12" inline={false} size="xl" bind:value={inputConfig} options={equip} {edit} on:change={dispatchChange} validation={validation}/>
          </div>
          {#if ioConfig.ioNames[parseInt(ioConfig.config.inputConfig[pid],10)]?.split(':')[3] === '1'}
            <button
              class="absolute right-0 top-1/2 -translate-y-1/2 ml-2 text-sm hover:text-primary-500"
              on:click={() => {
                $keyboardStore.keyboardType = KeyboardTypes.Alpha;
                $keyboardStore.label = "Edit Input Name";
                $keyboardStore.start = ioConfig.ioNames[parseInt(ioConfig.config.inputConfig[pid],10)]?.split(':')[0] || '';
                $keyboardStore.inputType = 'text';
                $keyboardStore.resultReady = (name) => {
                  dispatch('rename', { i, j, name, type: 1 });
                };
                $keyboardStore.hidden = false;
              }}
            >
              <Icon class="fill-gray-500 stroke-gray-500 bg-gray-300 bg-opacity-10" src={mdiPencil} />
            </button>
          {/if}
        </div>
      </div>
    {/if}
  {:else}
    <div class="my-2">N/A</div>
  {/if}
</Column>