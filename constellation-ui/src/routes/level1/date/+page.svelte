<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import TextField from "$lib/ui/TextField.svelte";
  import Select from "$lib/ui/Select.svelte";
	import { navigationStore } from "$lib/store";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import DatePicker from "$lib/ui/DatePicker.svelte";
	import { format } from "date-fns";
	import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { dateTime } from "$lib/business/protoStores";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

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
  let hydrated = false;

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

  // Hydrate from the typed proto store on first frame; ignore subsequent
  // firmware updates so the user's in-progress edits aren't clobbered.
  $: if (!hydrated && $dateTime) {
    date = [
      $dateTime.dateStr ?? '',
      $dateTime.timeStr ?? '',
      String($dateTime.amPm ?? 0),
    ];
    if (date[0]) {
      try { calendar = new Date(date[0]); } catch { /* keep default */ }
    }
    time = date[1];
    originalDate = {
      Date: format(calendar, "MM/dd/yyyy"),
      Time: getTime(date[1]),
      TimeType: date[2],
    };
    hydrated = true;
    ready = true;
  }

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

  onMount(() => {
    $navigationStore.isDirty = () => !isEqual(saveDate, originalDate);
    if (hydrated) ready = true;
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
    <SaveButton
      {edit}
      bind:wait={wait}
      data={saveDate}
      bind:original={originalDate}
      onSave={async (d) => {
        // Phase 5.1 — direct proto write. SETTINGS_FIELD[TAG.DateTime] = 26.
        // Wire-compatible: DateTime (read) and DateTimeUpdate (write) share
        // field numbers {1=dateStr, 2=timeStr, 3=amPm}.
        await writeProto(TAG.DateTime, {
          dateStr: d.Date ?? '',
          timeStr: d.Time ?? '',
          amPm: parseInt(d.TimeType ?? '0', 10) || 0,
        });
      }}
      autoSave
    />
  </Card>
</GellertPage>

