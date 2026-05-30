<script lang="ts">
  import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Card from "$lib/ui/Card.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { frontMatterStore, navigationStore } from "$lib/store";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { AdornmentType } from "$lib/business/adornmentType";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { isEqual } from "lodash-es";
  import { isValidSensor } from "$lib/business/analog";
  import { t } from "svelte-i18n";
  import { co2Settings, refrigSettings } from "$lib/business/protoStores";
  import { useDraft, numField } from "$lib/business/useDraft";
  import {
    writeProtoRaw,
    buildForceVarintBytes, buildForceFloatBytes,
  } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // Proto-direct page state. Replaces the legacy `co2: string[8]` and
  // `refrigeration: string[23]` (slots [16]/[17]) positional shapes.
  // CO2 fields go through useDraft → writeProto.
  // Refrig purge sub-form does a partial RefrigSettings write of just
  // fields 10 + 11 via writeProtoRaw (typed encoder would also try to
  // serialize the rest of refrig, owned by /level2/refrigeration).
  const co2 = useDraft(co2Settings, TAG.Co2Settings);
  const { draft, hydrated, live } = co2;

  const minTempStr  = numField(draft, 'minTemp',         'float');
  const maxTempStr  = numField(draft, 'maxTemp',         'float');
  const durationStr = numField(draft, 'durationMinutes', 'int');
  const cycleStr    = numField(draft, 'cycleOrSet',      'int');
  const fanOutStr   = numField(draft, 'fanOutput',       'int');
  const doorOutStr  = numField(draft, 'doorOutput',      'int');

  // Refrig purge sub-form: hydrate two fields locally; save them
  // independently. Avoids cross-coupling with the rest of refrigSettings.
  let purgeMode = 0;
  let purgeThresholdStr = '0';
  let purgeOriginal = { mode: 0, threshold: '0' };
  let purgeHydrated = false;

  $: if (!purgeHydrated && $refrigSettings) {
    purgeMode         = $refrigSettings.purge ?? 0;
    purgeThresholdStr = String($refrigSettings.purgeThreshold ?? 0);
    purgeOriginal = { mode: purgeMode, threshold: purgeThresholdStr };
    purgeHydrated = true;
  }
  $: purgeDirty = purgeHydrated &&
    (purgeMode !== purgeOriginal.mode || purgeThresholdStr !== purgeOriginal.threshold);

  let title = `${$t('level1.co2.high')} CO2 ${ $t('level1.co2.level-purge-control') }`;

  $: modeOptions = [
    { value: 0, text: $t('global.none-default') },
    { value: 1, text: $t('global.manual')       },
    { value: 2, text: $t('global.automatic')    },
  ];
  $: refrigerationOptions = [
    { value: 0, text: $t('level2.refrigeration.normal')    },
    { value: 1, text: $t('level2.refrigeration.pump-down') },
  ];

  let validation = {
    'minTemp': '', 'maxTemp': '', 'co2SetPoint': '', 'co2Target': '',
    'time': '', 'fanOutput': '', 'doorOutput': '', 'PurgeHours': ''
  };
  let refrigValidation: Record<string, string> = { PurgeThreshold: '' };

  $: ready = false;
  $: wait = false;
  $: refrigWait = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  $: current  = ($frontMatterStore.main as string[])?.[17];
  $: current2 = ($frontMatterStore.main as string[])?.[36];

  /**
   * CO2 save (settings field 7 / TAG.Co2Settings).
   * Field 5 (cycleOrSet) is mode-dispatched in firmware: mode==1 stores
   * to CycleTime, mode==2 stores to Set. UI binds a single $cycleStr to
   * whichever input is shown so there's no positional ambiguity.
   * Field 1 (mode=0/Off) is force-encoded — proto3 would otherwise drop it.
   */
  async function saveCo2(): Promise<void> {
    const v = buildForceVarintBytes({
      1: $draft.mode,            // 0 = Off, must encode
      4: $draft.durationMinutes,
      5: $draft.cycleOrSet,
      6: $draft.fanOutput,
      7: $draft.doorOutput,
    });
    const f = buildForceFloatBytes({
      2: $draft.minTemp,
      3: $draft.maxTemp,
    });
    const out = new Uint8Array(v.length + f.length);
    out.set(v, 0);
    out.set(f, v.length);
    await writeProtoRaw(TAG.Co2Settings, out);
  }

  /**
   * Partial refrig save: just fields 10 (purge mode) and 11 (purge
   * threshold). Force-varints because mode=0 (Normal) is meaningful.
   * /level2/refrigeration owns the rest of refrig settings.
   */
  async function saveRefrigPurge(): Promise<void> {
    const out = buildForceVarintBytes({
      10: purgeMode,
      11: parseInt(purgeThresholdStr, 10) || 0,
    });
    await writeProtoRaw(TAG.RefrigSettings, out);
    purgeOriginal = { mode: purgeMode, threshold: purgeThresholdStr };
  }

  onMount(() => {
    $navigationStore.isDirty = () => !isEqual($draft, $live) || purgeDirty;
    ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="co2">
  <Card class="mx-auto w-3/4 mt-2 flex flex-col">
    {#if $hydrated}
      <Table class="text-size-xl">
        <Row>
          <div class="m-2 text-center">
            CO<sub>2</sub> { $t('level1.co2.purge-control-mode') }
            <Select class="w-64 md:w-96 xl:w-128 ml-2" size="xl" bind:value={$draft.mode} options={modeOptions} {edit}/>
          </div>
        </Row>
        {#if $draft.mode !== 0}
          <Row>
            <div class="m-2 text-center">
              { $t('level1.co2.a') } CO<sub>2</sub> { $t('level1.co2.purge-will-occur-if') }
              {#if $draft.mode === 1}
                { $t('level1.co2.it-has-been') }
                <TextField class="w-36" size="xl" bind:value={$cycleStr} {edit} label="Last Purge" keyboardType={KeyboardTypes.Numeric} validation={validation.PurgeHours} />
                { $t('level1.co2.hours-since-the-last-purge') } <br />
              {:else if $draft.mode === 2}
                { $t('level1.co2.the-co') }<sub>2</sub> { $t('level1.co2.level-is-above') }
                <TextField class="w-36" size="xl" bind:value={$cycleStr} {edit} label="CO2 Level" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.CO2} validation={validation.co2SetPoint}/>
              {/if}
              { $t('level1.co2.and-the-outside-air-temperature-is-between-a-minumum-of') }
              <TextField class="w-36" size="xl" bind:value={$minTempStr} {edit} label="Minimum Temperature" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.minTemp}/>
              { $t('level1.co2.and-a-maximum-of') }
              <TextField class="w-36" size="xl" bind:value={$maxTempStr} {edit} label="Maximum Temperature" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.maxTemp}/>
              { $t('global.between-chinese')}
              <br />{ $t('level1.co2.the-purge-will-last-for') }
              <TextField class="w-36" size="xl" bind:value={$durationStr} {edit} label="Purge Time" keyboardType={KeyboardTypes.Numeric} validation={validation.time}/>
              { $t('level1.co2.minutes-with') } { $t('level1.co2.a-fan-output-of') }
              <TextField class="w-36" size="xl" bind:value={$fanOutStr} {edit} label="Fan Output" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.fanOutput}/>
              { $t('level1.co2.and-a-door-output-of') }
              <TextField class="w-36" size="xl" bind:value={$doorOutStr} {edit} label="Door Output" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.doorOutput}/>
              { $t('level1.co2.door-output') }
            </div>
          </Row>
        {/if}
        <Row>
          <div class="my-4 text-center">
            { $t('level1.co2.current') } CO<sub>2</sub> { $t('level1.co2.level') } <TextField class="w-48" size="xl" value={current} edit={false} label="Current CO2 Level" adornmentType={AdornmentType.CO2} />
              {#if isValidSensor(current2)}
                | <TextField class="w-48" size="xl" value={current2} edit={false} label="Current 2 CO2 Level" adornmentType={AdornmentType.CO2} />
              {/if}
          </div>
        </Row>
      </Table>
      <SaveButton {edit} bind:wait={wait} data={$draft} original={$live} bind:validation={validation} autoSave onSave={saveCo2} />
    {/if}
  </Card>

  <!--
    Refrigeration purge mode + threshold — relocated from /level2/refrigeration
    so all CO2-purge-related controls live on this page. Partial save
    (only fields 10/11 of RefrigSettings).
  -->
  {#if purgeHydrated}
    <Card class="mx-auto w-3/4 mt-2 flex flex-col">
      <Table class="text-size-xl">
        <Row>
          <div class="m-2 text-center">
            { $t('level2.refrigeration.refrigeration-purge-mode-is') }
            <Select class="w-64 md:w-96 ml-2" size="lg" bind:value={purgeMode} options={refrigerationOptions} {edit}/>
            { $t('level2.refrigeration.and-output-must-be-below') }
            <TextField class="w-24 ml-2" size="lg" bind:value={purgeThresholdStr} {edit} label="Output Below" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={refrigValidation.PurgeThreshold}/>
            { $t('level2.refrigeration.to-purge') }.
          </div>
        </Row>
      </Table>
      <SaveButton {edit} bind:wait={refrigWait}
                  data={{ mode: purgeMode, threshold: purgeThresholdStr }}
                  original={purgeOriginal}
                  bind:validation={refrigValidation} autoSave
                  onSave={saveRefrigPurge} />
    </Card>
  {/if}
</GellertPage>
