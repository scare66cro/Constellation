<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Button from "$lib/ui/Button.svelte";
	import { frontMatterStore, heightsStore, navigationStore } from "$lib/store";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import EquipmentRow from "$lib/components/EquipmentRow.svelte";
	import { getEquipment } from "$lib/business/equipmentStatus";
  import type { Equipment } from "$lib/business/equipmentStatus";
	import HumidifierRow from "$lib/components/HumidifierRow.svelte";
	import RefrigerationRow from "$lib/components/RefrigerationRow.svelte";
	import { onDestroy, onMount } from "svelte";
  import { t } from "svelte-i18n";
	import DoorDiagRow from "$lib/components/DoorDiagRow.svelte";
	import LightsRow from "$lib/components/LightsRow.svelte";
	import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import { getHttpUrl } from "$lib/business/util";
  import { equipmentComposite, systemStatus } from "$lib/business/protoStores";

  type Counts = {
    leftRows: number,
    rightRows: number,
    reserved: number,
    refrigAdded: number,
    auxAdded: number,
  }

  let title = $t('level1.equipment.equipment-status');

  let equipment: Equipment | undefined;

  // Reactive subscription to the proto composite store. The bridge
  // replays cached frames immediately on subscribe, so this typically
  // settles within one round-trip — same effective behaviour as the
  // legacy `equipment-data` WS channel, minus the bridge CSV layer.
  $: if ($equipmentComposite) {
    if (!equipment) {
      equipment = $equipmentComposite;
    } else {
      updateRows($equipmentComposite);
      equipment = $equipmentComposite;
    }
  }

  let switch1Use: string[] = [];
  let switch2Use: string[] = [];
  let refrigStages = 0;
  let auxOutputs = 0;
  let currentLevel = -1;
  let height = 768;

  // equipment will be set on first poll response; do not reset reactively each cycle

  $: leftRows = [] as any[];
  $: rightRows = [] as any[];

  $: level = $navigationStore?.level;
  $: if (typeof window !== 'undefined') {
    height = $heightsStore.main - $heightsStore.header - $heightsStore.footer - (window.innerWidth < 680 ? 25 : 100);
  }

  // readiness + waiting flags (were incorrectly reactive-assigned each cycle before)
  let ready = false;
  let wait = false;

  $: edit = $navigationStore?.level > 0;

  $: hasRightSide = (edit && refrigStages > 0) || rightRows.length > 0;

  $: if (currentLevel !== $navigationStore?.level && $frontMatterStore?.main !== undefined && equipment !== undefined) {
    refrigStages = 0;
    auxOutputs = 0;
    leftRows = [];
    rightRows = [];
    currentLevel = $navigationStore?.level;
    loadData(equipment);
    ready = true;
  }

  onMount(() => {
    // No-op: subscription is wired via the reactive `$equipmentComposite`
    // block above. Kept for parity with other pages so future side-effects
    // (one-shot REST fetches, etc.) have an obvious home.
  });

	onDestroy(() => {
    // No-op: the proto store auto-unsubscribes when the last consumer
    // detaches; nothing page-local to tear down.
	});

  function updateRows(equipment: Equipment): void {
    leftRows.forEach((row) => {
      const eq = getEquipment(equipment, row.name, edit, $frontMatterStore?.main as string[], doorPct);
      if (eq) {
        row.equipmentStatus = eq.equipmentStatus;
        row.panelSwitchStatus = eq.panelSwitchStatus;
        row.outputColor = eq.outputColor;
        row.statusColor = eq.statusColor;
        row.panelSwitchColor = eq.panelSwitchColor;
        row.equipOn = eq.equipOn;
        if ('remoteStatus' in eq) row.remoteStatus = eq.remoteStatus;
      }
    });
    rightRows.forEach((row) => {
      const eq = getEquipment(equipment, row.name, edit, $frontMatterStore?.main as string[], doorPct);
      if (eq) {
        row.equipmentStatus = eq.equipmentStatus;
        row.panelSwitchStatus = eq.panelSwitchStatus;
        row.outputColor = eq.outputColor;
        row.statusColor = eq.statusColor;
        row.panelSwitchColor = eq.panelSwitchColor;
        row.equipOn = eq.equipOn;
        if ('remoteStatus' in eq) row.remoteStatus = eq.remoteStatus;
      }
    });
    leftRows = leftRows;
    rightRows = rightRows;
  }

  // Reactively refresh row data when SystemStatus updates so the
  // commanded door % in the status column tracks the firmware's
  // PWM_DOORS.Output as the PID ramps. Without this the door row would
  // only refresh when equipmentComposite changes (which is much less
  // frequent than SystemStatus emits).
  $: if (equipment && $systemStatus !== null) {
    updateRows(equipment);
  }

  async function loadData(equipment: Equipment): Promise<void> {
    if (equipment === undefined) return;
    const counts: Counts = {
      leftRows: 0,
      rightRows: 0,
      reserved: refrigStages+auxOutputs,
      refrigAdded: 0,
      auxAdded: 0,
    };
		try {
      switch1Use = equipment.auxSwitches?.[0]?.split(':');
      switch2Use = equipment.auxSwitches?.[1]?.split(':');
      const potatoMode = equipment.systemMode === '0';
      const onionMode = equipment.systemMode === '1';

      for (let i = 0; i < 10; i += 1) {
        if (equipment.outputConfig[i + 13] !== '-1') {
          refrigStages += 1;
        }
      }
      if (level >= 1) {
        for (let i = 1; i <= 8; i += 1) {
          if (equipment.outputConfig[i + 24] !== '-1' && switch1Use[i] === '5' && switch2Use[i] === '5') {
            auxOutputs += 1;
          }
        }
        // heat output
        if (equipment.outputConfig[4] !== '-1') {
          auxOutputs += 1;
        }
      }
      if (level < 1) {
        counts.rightRows = 1;
        counts.leftRows = 1;
      }

      addEquipment(equipment, 'fan', counts);

      // potato mode
      if (potatoMode) {
        if (equipment.outputConfig[3] !== '-1') {
          addEquipment(equipment, 'climacell', counts);
        }
        if (equipment.outputConfig[7] !== '-1') {
          addEquipment(equipment, 'humid1', counts, true);
          addEquipment(equipment, 'pump1', counts, true);
        }
        if (equipment.outputConfig[9] !== '-1') {
          addEquipment(equipment, 'humid2', counts, true);
          addEquipment(equipment, 'pump2', counts, true);
        }
        if (equipment.outputConfig[11] !== '-1') {
          addEquipment(equipment, 'humid3', counts, true);
          addEquipment(equipment, 'pump3', counts, true);
        }
        if (equipment.outputConfig[4] !== '-1') {
          if (level < 1) {
            addEquipment(equipment, 'heat', counts, true);
          }
        }
      }
      else if (onionMode) {
        addEquipment(equipment, 'cure', counts);
        if (equipment.outputConfig[6] !== '-1') {
          addEquipment(equipment, 'burner', counts);
        }
      }

      if (equipment.outputConfig[5] !== '-1') {
        if (equipment.miscData[10] === '0') {
          addEquipment(equipment, 'cavity', counts);
        } else {
          addEquipment(equipment, 'pile', counts);
        }
      }
      // Bay Lights (eq 23 = Bay Lights 1, eq 24 = Bay Lights 2)
      if (equipment.outputConfig[23] !== '-1') {
        addEquipment(equipment, 'lights1', counts);
      }
      if (equipment.outputConfig[24] !== '-1') {
        addEquipment(equipment, 'lights2', counts);
      }
      if (level >= 1) {
        if (equipment.outputConfig[13] !== '-1' || equipment.pwmConfig[1]?.split(':')[2] !== '-1') {
          addEquipment(equipment, 'refrig', counts);
        }
        // Fresh-air doors operator selector. Always show at level≥1
        // because the row's purpose is to drive the state machine
        // (SW_FRESHAIR_AUTO gate in SetStateCooling), not just a
        // physical output. The legacy CPLD economizer auto-bypass was
        // removed in fw 0.A.46 so this is now the sole mechanism for
        // the operator to enable cooling-mode entry. Renders even when
        // no DOORS output is mapped — picking AUTO with no wiring is
        // harmless (engine just has nothing to drive).
        addEquipment(equipment, 'door', counts);
      } else {
        if (equipment.outputConfig[13] === '-1' && equipment.pwmConfig[1]?.split(':')[2] !== '-1') {
          addEquipment(equipment, 'refrig', counts);
        }
        if (onionMode && equipment.pwmConfig[3]?.split(':')[2] !== '-1') {
          addEquipment(equipment, 'burner', counts);
        }
        if (equipment.pwmConfig[0]?.split(':')[2] !== '-1') {
          addEquipment(equipment, 'door', counts);
        }
      }

      auxiliaryAdd(equipment, counts);

      if (counts.refrigAdded == 0) {
        refrigStagesAdd(equipment, counts);
        counts.refrigAdded = 1;
      }

      if (level >= 1) {
          rightSideAdd(equipment, 'doordiag', counts);
      }

      if (potatoMode && equipment.outputConfig[4] !== '-1') {
        if (level >= 1) {
          leftSideAdd(equipment, 'heat', counts);
        }
      }

      auxiliaryAdd(equipment, counts);

      if (counts.rightRows === 0) {
        // TODO disable io refrig diag (ioDisabled2)
      }
		} catch (error) {
		}
    ready = true;
  }

  async function clear() {
    wait = true;
    const result = await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        tag: 'button2',
        ClearDiag: 'Clear',
      }),
    });
    wait = false;
  }

  function addEquipment(equipment: Equipment, equipmentName: string, counts: Counts, forceLeft = false): void {
    if (level >= 1 || counts.leftRows < counts.reserved || counts.leftRows <= counts.rightRows || forceLeft) {
      leftSideAdd(equipment, equipmentName, counts);
    } else {
      rightSideAdd(equipment, equipmentName, counts);
    }

    if (counts.refrigAdded === 0 && counts.leftRows > counts.reserved) {
      refrigStagesAdd(equipment, counts);
      counts.refrigAdded = 1;
    }
  }

  function auxiliaryAdd(equipment: Equipment, counts: Counts): void {
    if (counts.auxAdded === 0) {
      if (level < 1) {
        for (let i = 1; i <= 8; i += 1) {
          if (equipment.outputConfig[i + 24] !== '-1') {
            addEquipment(equipment, `aux${i}Switch`, counts);
          }
        }
      } else {
        for (let i = 1; i <= 8; i += 1) {
          if (equipment.outputConfig[i+24] !== '-1') {
            if (switch1Use[i] !== '5' || switch2Use[i] !== '5') {
              leftSideAdd(equipment, `aux${i}Switch`, counts);
            }
          }
        }
      }
      counts.auxAdded = 1;
    } else if (level >= 1) {
      for (let i = 1; i <= 8; i += 1) {
        if (equipment.outputConfig[i+24] !== '-1') {
          if (switch1Use[i] === '5' && switch2Use[i] === '5') {
            rightSideAdd(equipment, `aux${i}Switch`, counts);
          }
        }
      }
    }
  }

  function refrigStagesAdd(equipment: Equipment, counts: Counts): void {
    for (let i = 1; i <= 8; i += 1) {
      if (equipment.outputConfig[i+12] !== '-1') {
        rightSideAdd(equipment, 'refrig'+i, counts);
      }
    }
    if (equipment.outputConfig[21] !== '-1') {
      rightSideAdd(equipment, 'defrost1', counts);
    }
    if (equipment.outputConfig[22] !== '-1') {
      rightSideAdd(equipment, 'defrost2', counts);
    }
  }

  // pwm_doors_pct is field 20 on SystemStatus — always populated by
  // firmware (every system_status emit, regardless of mode) as the
  // commanded door PWM %. Used by `getEquipment('door', …)` to fill
  // the status column. Operators without DI feedback on the fresh-air
  // actuators (the common Constellation install) otherwise see "Off"
  // for the door row even while the door PID is actively driving the
  // damper — see equipmentStatus.ts::getEquipment 'door' case.
  $: doorPct = $systemStatus?.pwmDoorsPct;

  function rightSideAdd(equipment: Equipment, equipmentName: string, counts: Counts): void {
    const eq = getEquipment(equipment, equipmentName, edit, $frontMatterStore?.main as string[], doorPct);
    if (eq) {
      rightRows.push(eq);
      counts.rightRows += 1;
    }
  }

  function leftSideAdd(equipment: Equipment, equipmentName: string, counts: Counts): void {
    const eq = getEquipment(equipment, equipmentName, edit, $frontMatterStore?.main as string[], doorPct);
    if (eq) {
      leftRows.push(eq);
      counts.leftRows += 1;
    }
  }

  function humidRows(rows: any[]): any[] {
    return rows.filter((row) => row.name.startsWith('humid'));
  }
  function pumpRows(rows: any[]): any[] {
    return rows.filter((row) => row.name.startsWith('pump'));
  }
  function refrigRows(rows: any[]): any[] {
    return rows.filter((row) => row.name.startsWith('refrig') || row.name.startsWith('defrost'));
  }
</script>

<GellertPage {wait} {ready} {title} {level} name="equipment">
  <Card class="mt-2 flex flex-col mx-2" {height}>
    <ScrollableArea>
      <div class="flex flex-row mb-2" style="height: {height}">
      <Table class="{hasRightSide ? 'w-1/2' : ''} mr-2 text-size-xl">
        <Row>
          <Column class="w-1/2">{ $t('global.output') }</Column>
          <Column class="w-1/4">{ $t('global.status') }</Column>
          <Column class="w-1/4">{ $t('global.switch') }</Column>
        </Row>
        {#each leftRows as row}
          {#if row.name.startsWith('humid') || row.name.startsWith('pump')}
            {#if row.name === 'humid1'}
              <HumidifierRow rows={humidRows(leftRows)} pump={pumpRows(leftRows)} {edit} bind:wait={wait} />
            {/if}
          {:else if row.name === 'lights1' || row.name === 'lights2'}
            <LightsRow bind:wait={wait} {...row} />
          {:else}
            <EquipmentRow bind:wait={wait} {...row} />
          {/if}
        {/each}
      </Table>
      {#if hasRightSide}
        <Table class="w-1/2 text-size-xl">
          {#if edit && refrigStages > 0}
            <Row>
              <Column colspan={3}>{ $t('level1.equipment.refrigeration-diagnostics') }</Column>
            </Row>
          {/if}
          <Row>
            <Column class="w-1/2">{ $t('global.output') }</Column>
            <Column class="w-1/4">{ $t('global.status') }</Column>
            <Column class="w-1/4">{ $t('global.switch') }</Column>
          </Row>
          <RefrigerationRow bind:wait={wait} rows={refrigRows(rightRows)} {edit} />
          {#if edit && refrigStages > 0}
            <Row>
              <Column colspan={2}>{ $t('level1.equipment.diagnostics-mode-clears-automatically-in-60-min') }</Column>
              <Column><Button size="xl" on:click={clear} {edit}>{ $t('global.clear') }</Button></Column>
            </Row>
          {/if}
          {#each rightRows as row}
            {#if row.name === 'doordiag'}
              <DoorDiagRow bind:wait={wait} {row} />
            {:else if row.name === 'lights1' || row.name === 'lights2'}
              <LightsRow bind:wait={wait} {...row} />
            {:else if !row.name.startsWith('refrig') && !row.name.startsWith('defrost')}
              <EquipmentRow bind:wait={wait} {...row} />
            {/if}
          {/each}
        </Table>
      {/if}
      </div>
    </ScrollableArea>
  </Card>
</GellertPage>