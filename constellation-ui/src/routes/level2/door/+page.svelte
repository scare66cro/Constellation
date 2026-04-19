<script lang="ts">
	import { goto } from "$app/navigation";
	import GellertPage from "$lib/components/GellertPage.svelte";
	import PIDU from "$lib/components/PIDU.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import ScrollableArea from "$lib/components/ScrollableArea.svelte";
	import { navigationStore, pidStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import Button from "$lib/ui/Button.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Column from "$lib/ui/Column.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { onMount } from "svelte";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import type { ArrayResponse } from "$lib/business/util";

  export let data: ArrayResponse & { gdc: any };

  let edit = true;
  let title = $t('level2.door.fresh-air-door-setup');

  let validation = {
    PAirValue: '',
    IAirValue: '',
    DAirValue: '',
    UAirValue: '',
    ActuatorTimes: '',
    CoolAirCycle: ''
  };

  $: door = [] as string[];
  $: ready = false;
  $: wait = false;

  // ── GDC state ──
  interface GDCStageEdit {
    stageNum: number;
    label: string;
    doors: boolean[];  // 5 checkboxes (door 1-5)
  }

  let gdcPresent = false;
  let gdcStages: GDCStageEdit[] = [];
  let gdcActuators: any[] = [];
  let gdcTravelTime = '';
  let gdcTotalCapacity = 0;
  let gdcSaving = false;
  let gdcCalibrating = false;

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
      // Fallback: check actuator stageAssignment
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
    gdcStages = gdcStages.filter((_, i) => i !== idx)
      .map((s, i) => ({ ...s, stageNum: i + 1 }));
  }

  async function saveGDCStages() {
    gdcSaving = true;
    try {
      const payload = {
        stages: gdcStages.map(s => ({
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
        // Refresh actuator data from response
        if (result.actuators) gdcActuators = result.actuators;
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

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl(`/iot/door`);
      $navigationStore.isDirty = () => !isEqual(door, data.array);
      door = cloneDeep(data.array);
      initGDCFromData(data.gdc);
    } catch (error) {
      console.error(error);
    }
    ready = true;
  });

  function gotoLogs() {
    $pidStore.returnPage = '/level2/door';
    goto('/level2/pid');
  }
</script>

<GellertPage {wait} {title} {ready} level={2} name="door">
  <ScrollableArea>
  <Card class="md:mx-2 xl:mx-auto flex flex-col container-wide 3xl:container-standard">
    <Table class="text-size-xl">
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('global.pidu-values') }</Column>
        <Column class="w-1/2">
          <PIDU bind:p={door[0]} bind:i={door[1]} bind:d={door[2]} bind:u={door[3]} {edit} pvalid={validation.PAirValue} ivalid={validation.IAirValue} dvalid={validation.DAirValue} uvalid={validation.UAirValue} />
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('level2.door.total-open-time-for-all-actuator-stages') }</Column>
        <Column class="w-1/2 items-center">
          <p class="text-center">
            <TextField class="w-36 mr-2" size="xl" bind:value={door[4]} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.ActuatorTimes}/> {$t('level2.door.seconds')}
          </p>
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('level2.door.cooling-air-short-cycle-timer') }</Column>
        <Column class="w-1/2 items-center">
          <p class="text-center">
            <TextField class="w-36 mr-2" size="xl" bind:value={door[5]} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.CoolAirCycle}/> {$t('global.minutes')}
          </p>
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('global.pid-controller-output-logging-options') }</Column>
        <Column class="w-1/2">
          <Button size="xl" class="w-fit" on:click={gotoLogs}>{ $t('global.logs') }</Button>
        </Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={door} bind:original={data.array} route="door" bind:validation={validation} autoSave/>
  </Card>

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

      <!-- Stage Configuration -->
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

      <!-- Actuator Positions (read-only) -->
      {#if gdcActuators.length > 0}
        <div class="px-4 py-2 border-t border-gray-200">
          <h3 class="text-size-large font-semibold mb-2">{ $t('level2.door.actuator-positions') }</h3>
          <div class="flex gap-4 flex-wrap">
            {#each gdcActuators as act, i}
              <div class="border border-gray-300 rounded px-3 py-2 min-w-[120px] text-center">
                <div class="text-sm text-gray-500">{act.label || `Door ${i+1}`}</div>
                <div class="text-size-xl font-mono">{Math.round(act.position)}%</div>
                {#if act.moving}
                  <div class="text-xs text-blue-600">{ $t('level2.door.moving') }</div>
                {/if}
                {#if act.calibrated}
                  <div class="text-xs text-green-600">{ $t('level2.door.calibrated') }</div>
                {/if}
              </div>
            {/each}
          </div>
        </div>
      {/if}

      <!-- Save / Calibrate buttons -->
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
  {/if}
  </ScrollableArea>
</GellertPage>
