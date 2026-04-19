<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import TextField from "$lib/ui/TextField.svelte";
  import Select from "$lib/ui/Select.svelte";
	import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import DatePicker from "$lib/ui/DatePicker.svelte";
	import { format } from "date-fns";
	import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import { type ArrayResponse } from "$lib/business/util";

  export let data: ArrayResponse;

  let title = $t('level1.date.set-date-time');

  let timeOptions = [
    { value: '0', text: 'AM'},
    { value: '1', text: 'PM' },
  ];

  let saveDate = {}
  let date: string[] = ['', '', '0'];

  function getTime(t: string): string {
    let ret: string = t;
    if (!t) {
      return '';
    }
    if (t.indexOf(":") > -1) {
      // get rid of seconds
      ret = t.split(':').slice(0, 2).join(':');
    } else {
      if (date.length > 3 && t.length % 2 === 0) {
        ret = `${t.substring(0, 2)}:${t.substring(2, 4)}`;
      } else if (date.length > 2) {
        ret = `${t.substring(0, 1)}:${t.substring(1,3)}`;
      }
    }
    return ret;
  }

  let originalDate: { Date: string, Time: string, TimeType: string };
  let calendar = new Date();

  $: time = '';
  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  $: saveDate = {
    Date: format(calendar, "MM/dd/yyyy"),
    Time: getTime(time),
    TimeType: date[2],
  };

  function update() {
    if (date.length > 1) {
      let temp = time.replaceAll(':', '');
      time = temp.length === 4 ? `${temp.substring(0, 2)}:${temp.substring(2)}` : `${temp.substring(0, 1)}:${temp.substring(1)}`;
    }
  }

  function handleCalendarChange() {
    // Update the date array when calendar changes
    date[0] = format(calendar, "MM/dd/yyyy");
  }

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl('/iot/date');
      $navigationStore.isDirty = () => !isEqual(saveDate, originalDate);
      date = cloneDeep(data.array);
      
      // Initialize calendar from the data
      if (date?.length > 0 && date[0] !== '') {
        calendar = new Date(date[0]);
      }
      
      originalDate = {
        Date: format(calendar, "MM/dd/yyyy"),
        Time: getTime(date[1]),
        TimeType: date[2],
      };
      time = date[1]
    } catch (e) {
      console.error(e);
    }
		ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="date">
  <Card class="w-2/3 mx-auto mt-2 flex flex-col">
    <div class="shadow-xl shadow-gray-500 text-size-xl">
      <div class="flex flex-row items-center bg-surface-100 py-2">
        <span class="ml-auto mr-2">{ $t('global.date') }:</span>
        {#if edit}
          <DatePicker class="mr-auto w-96" bind:value={calendar} on:select={handleCalendarChange}/>
        {:else}
          <span class="mr-auto w-96 px-4 py-2 border border-gray-300 rounded bg-white">{format(calendar, "MM/dd/yyyy")}</span>
        {/if}
      </div>
      <div class="flex flex-row items-center bg-surface-100 py-2">
        <span class="ml-auto mr-2">{ $t('level1.date.time') }:</span>
        <TextField class="mx-2 w-fit" size="xl" bind:value={time} keyboardType={KeyboardTypes.Numeric} {edit} label="Time" on:change={update} />
        <Select bind:value={date[2]} class="w-64" size="xl" noeditclass="mr-auto" extended="mr-auto" options={timeOptions} {edit}/>
      </div>
    </div>
    <SaveButton {edit} bind:wait={wait} data={saveDate} bind:original={originalDate} route="date" autoSave />
  </Card>
</GellertPage>

