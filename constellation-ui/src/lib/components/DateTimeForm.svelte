<script lang="ts">
  import { onMount } from "svelte";
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

  // Shared body of the Set Date & Time page (level1/date): date picker + time
  // + AM/PM, proto-direct save (TAG.DateTime). Rendered on the classic page
  // AND as the dashboard date/time modal (opened from the titlebar clock).
  // Prop contract mirrors PlenumSetpointsForm. docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  let timeOptions = [
    { value: '0', text: 'AM' },
    { value: '1', text: 'PM' },
  ];

  let date: string[] = ['', '', '0'];

  function getTime(t: string): string {
    let ret: string = t;
    if (!t) return '';
    if (t.indexOf(":") > -1) {
      ret = t.split(':').slice(0, 2).join(':');
    } else {
      if (date.length > 3 && t.length % 2 === 0) {
        ret = `${t.substring(0, 2)}:${t.substring(2, 4)}`;
      } else if (date.length > 2) {
        ret = `${t.substring(0, 1)}:${t.substring(1, 3)}`;
      }
    }
    return ret;
  }

  let originalDate: { Date: string, Time: string, TimeType: string };
  let calendar = new Date();
  let hydrated = false;
  let calOpen = false;   // DatePicker open state — drives the modal spacer

  $: time = '';
  $: level = $navigationStore.level;
  $: edit = canEdit ?? (level > 0);
  $: saveDate = {
    Date: format(calendar, "MM/dd/yyyy"),
    Time: getTime(time),
    TimeType: date[2],
  };

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

  // Enter time as bare digits — auto-format right-to-left, inserting the colon
  // two from the right (the last two digits are always minutes). e.g.
  // "3"→"3", "30"→"30", "230"→"2:30", "1230"→"12:30". No colon key needed.
  function update() {
    const d = (time ?? '').replace(/\D/g, '').slice(-4);
    time = d.length <= 2 ? d : `${d.slice(0, d.length - 2)}:${d.slice(-2)}`;
  }

  function handleCalendarChange() {
    date[0] = format(calendar, "MM/dd/yyyy");
  }

  // SaveButton ref so the modal can flush on close-unless-cancel.
  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    if (originalDate && !isEqual(saveDate, originalDate) && saveBtn) await saveBtn.save();
  }

  onMount(() => {
    if (!embedded) $navigationStore.isDirty = () => !isEqual(saveDate, originalDate);
    if (hydrated) ready = true;
  });
</script>

<div class="pform pform--{theme}">
  <Card class="w-2/3 mx-auto mt-2 flex flex-col">
    <div class="shadow-xl shadow-gray-500 text-size-xl">
      <div class="flex flex-row items-center bg-surface-100 py-2">
        <span class="ml-auto mr-2">{ $t('global.date') }:</span>
        {#if edit}
          <DatePicker class="mr-auto w-96" bind:value={calendar} bind:isOpen={calOpen} on:select={handleCalendarChange}/>
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
      bind:this={saveBtn}
      {edit}
      bind:wait={wait}
      data={saveDate}
      bind:original={originalDate}
      onSave={async (d) => {
        await writeProto(TAG.DateTime, {
          dateStr: d.Date ?? '',
          timeStr: d.Time ?? '',
          amPm: parseInt(d.TimeType ?? '0', 10) || 0,
        });
      }}
      autoSave
    />
  </Card>
  {#if embedded && calOpen}
    <!-- Reserve room below ONLY while the calendar is open, so the dropdown
         (absolutely positioned, ~330px tall) isn't clipped by the modal's
         bottom edge — and the modal doesn't carry blank space when it's shut.
         Modal-only; the classic page already has full-height room. -->
    <div style="height:330px" aria-hidden="true"></div>
  {/if}
</div>
