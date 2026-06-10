<script lang="ts">
  import { onMount } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Button from "$lib/ui/Button.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { getHttpUrl } from "$lib/business/util";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";

  // Shared body of the GDC stage configuration (level2/door, GDC card):
  // travel time + per-stage door assignment + actuator positions, saved over
  // the /iot/gdc REST flow (no proto representation yet). Rendered on the
  // classic page AND as the "GDC Stages" tab of the dashboard door modal.
  // GDC orbit data is fetched CLIENT-SIDE here (the route loader's data.gdc
  // doesn't run on the dashboard). docs/spatial-ui-page-migration.md
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  // GDC is a level-2 config; default to read-only unless the host grants edit.
  $: edit = canEdit ?? false;

  interface GDCStageEdit { stageNum: number; label: string; doors: boolean[]; }

  let loaded = false;
  let gdcPresent = false;
  let gdcStages: GDCStageEdit[] = [];
  let gdcActuators: any[] = [];
  let gdcTravelTime = '';
  let gdcTotalCapacity = 0;
  let gdcSaving = false;
  let gdcCalibrating = false;
  // Snapshot of the loaded stage layout so flush() only writes when dirty.
  let originalStages = '';

  function snapshot(): string {
    return JSON.stringify(gdcStages.map((s) => ({ stageNum: s.stageNum, doors: s.doors })));
  }

  function initGDCFromData(gdc: any) {
    if (!gdc?.present) { gdcPresent = false; return; }
    gdcPresent = true;
    gdcTravelTime = String(gdc.actuatorTravelTime ?? 90);
    gdcTotalCapacity = gdc.totalCapacity ?? 0;
    gdcActuators = gdc.actuators ?? [];
    gdcCalibrating = gdc.calibrating ?? false;

    const stages = gdc.stages ?? [];
    gdcStages = stages.map((s: any) => {
      const doorFlags = [false, false, false, false, false];
      if (Array.isArray(s.doors)) {
        for (const d of s.doors) {
          const idx = (d.index ?? d) - 1;
          if (idx >= 0 && idx < 5) doorFlags[idx] = true;
        }
      }
      if (!s.doors?.length && gdcActuators.length) {
        for (let i = 0; i < 5; i++) {
          if (gdcActuators[i]?.stageAssignment === s.stageNum) doorFlags[i] = true;
        }
      }
      return {
        stageNum: s.stageNum,
        label: s.stageNum === 1 ? $t('level2.door.control-door') : `${$t('level2.door.volume-group')} ${s.stageNum - 1}`,
        doors: doorFlags,
      };
    });
    originalStages = snapshot();
  }

  function addStage() {
    if (gdcStages.length >= 5) return;
    const num = gdcStages.length + 1;
    gdcStages = [...gdcStages, {
      stageNum: num,
      label: num === 1 ? $t('level2.door.control-door') : `${$t('level2.door.volume-group')} ${num - 1}`,
      doors: [false, false, false, false, false],
    }];
  }

  function removeStage(idx: number) {
    if (gdcStages.length <= 1) return;
    gdcStages = gdcStages.filter((_, i) => i !== idx).map((s, i) => ({ ...s, stageNum: i + 1 }));
  }

  async function saveGDCStages() {
    gdcSaving = true;
    try {
      const payload = {
        stages: gdcStages.map((s) => ({
          stageNum: s.stageNum,
          label: s.label,
          doors: s.doors.reduce((acc: number[], v, i) => { if (v) acc.push(i + 1); return acc; }, []),
        })),
      };
      const resp = await fetch(getHttpUrl('/iot/gdc/stages'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
      const result = await resp.json();
      if (result.ok) {
        if (result.actuators) gdcActuators = result.actuators;
        originalStages = snapshot();
      }
    } catch (err) {
      console.error('GDC save failed:', err);
    }
    gdcSaving = false;
  }

  async function startCalibration() {
    try {
      gdcCalibrating = true;
      await fetch(getHttpUrl('/iot/gdc/calibrate'), { method: 'POST' });
    } catch (err) {
      console.error('GDC calibration failed:', err);
      gdcCalibrating = false;
    }
  }

  // flush() autosaves the stage layout if the operator changed it (modal
  // close-unless-cancel contract). The /iot/gdc save is otherwise imperative.
  export async function flush(): Promise<void> {
    if (gdcPresent && snapshot() !== originalStages) await saveGDCStages();
  }

  onMount(async () => {
    try {
      const resp = await fetch(getHttpUrl('/iot/gdc'));
      if (resp.ok) {
        const data = await resp.json();
        initGDCFromData(data);
      }
    } catch { /* GDC not available */ }
    loaded = true;
  });
</script>

{#if loaded && (gdcPresent || embedded)}
<div class="pform pform--{theme}">
  {#if gdcPresent}
    <Card class="md:mx-2 xl:mx-auto mt-4 flex flex-col container-wide 3xl:container-standard">
      <h2 class="text-size-xl font-bold px-4 py-2 bg-gray-100 border-b border-gray-300">
        { $t('level2.door.gdc-stage-configuration') }
      </h2>
      <Table class="text-size-xl">
        <Row>
          <Column class="w-1/2 border-r border-gray-400">{ $t('level2.door.default-travel-time') }</Column>
          <Column class="w-1/2 items-center">
            <p class="text-center">
              <TextField class="w-36 mr-2" size="xl" bind:value={gdcTravelTime} {edit} keyboardType={KeyboardTypes.Numeric}/> {$t('level2.door.seconds')}
            </p>
          </Column>
        </Row>
        <Row>
          <Column class="w-1/2 border-r border-gray-400">{ $t('level2.door.total-system-capacity') }</Column>
          <Column class="w-1/2 items-center">
            <p class="text-center text-size-xl font-mono">{gdcTotalCapacity} {$t('level2.door.seconds')}</p>
          </Column>
        </Row>
      </Table>

      <div class="px-4 py-2">
        <div class="flex items-center justify-between mb-2">
          <h3 class="text-size-large font-semibold">{ $t('level2.door.stages') }</h3>
          {#if edit && gdcStages.length < 5}
            <Button size="lg" class="w-fit" on:click={addStage}>+ { $t('level2.door.add-stage') }</Button>
          {/if}
        </div>

        {#each gdcStages as stage, idx}
          <div class="border border-gray-300 rounded mb-2 p-3">
            <div class="flex items-center justify-between mb-2">
              <span class="font-semibold text-size-large">
                {stage.stageNum === 1 ? $t('level2.door.control-door') : `${$t('level2.door.volume-group')} ${stage.stageNum - 1}`}
              </span>
              {#if edit && gdcStages.length > 1}
                <Button size="sm" class="w-fit text-red-600" on:click={() => removeStage(idx)}>✕</Button>
              {/if}
            </div>
            <div class="flex items-center gap-4 flex-wrap">
              <span class="text-size-base text-gray-500">{ $t('level2.door.priority') }: {stage.stageNum}</span>
              <div class="flex items-center gap-3 flex-wrap">
                <span>{ $t('level2.door.doors') }:</span>
                {#each [0,1,2,3,4] as di}
                  <label class="flex items-center gap-1">
                    <input type="checkbox" bind:checked={stage.doors[di]} disabled={!edit} class="w-5 h-5"/>
                    <span>{di + 1}</span>
                  </label>
                {/each}
              </div>
            </div>
          </div>
        {/each}
      </div>

      {#if gdcActuators.length > 0}
        <div class="px-4 py-2 border-t border-gray-200">
          <h3 class="text-size-large font-semibold mb-2">{ $t('level2.door.actuator-positions') }</h3>
          <div class="flex gap-4 flex-wrap">
            {#each gdcActuators as act, i}
              <div class="border border-gray-300 rounded px-3 py-2 min-w-[120px] text-center">
                <div class="text-sm text-gray-500">{act.label || `Door ${i+1}`}</div>
                <div class="text-size-xl font-mono">{Math.round(act.position)}%</div>
                {#if act.moving}<div class="text-xs text-blue-600">{ $t('level2.door.moving') }</div>{/if}
                {#if act.calibrated}<div class="text-xs text-green-600">{ $t('level2.door.calibrated') }</div>{/if}
              </div>
            {/each}
          </div>
        </div>
      {/if}

      <div class="flex gap-3 px-4 py-3 border-t border-gray-200">
        {#if edit}
          <Button size="xl" class="w-fit" on:click={saveGDCStages} disabled={gdcSaving}>
            {gdcSaving ? $t('level2.door.saving') : $t('level2.door.save-stages')}
          </Button>
          <Button size="xl" class="w-fit" on:click={startCalibration} disabled={gdcCalibrating}>
            {gdcCalibrating ? $t('level2.door.calibrating') : $t('level2.door.calibrate-doors')}
          </Button>
        {/if}
      </div>
    </Card>
  {:else}
    <!-- embedded modal tab with no GDC orbit present -->
    <p class="text-center opacity-60 py-8">{ $t('level2.door.gdc-stage-configuration') } — no GDC actuators configured.</p>
  {/if}
</div>
{/if}
