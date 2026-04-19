<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import Select from "$lib/ui/Select.svelte";

  export let data: { rate: string[], plenum: string, pile: string[] };

  let title = $t('level1.ramp.plenum-setpoint-ramp-rate');

  let validation = {
    'updTemp': '',
    'rampUpdateHours': '',
    'rampTempDiff': '',
    'targetTemp': ''
  };

  const defaultTemp = [
    { text: $t('global.return-air-temp-default'), value: '255' },
  ]

  let rampOptions: {text: string, value: string}[] = [];

  $: ramp = {} as { rate: string[], plenum: string, pile: string[] };
  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  $: {
    rampOptions = [];
    rampOptions.push(...defaultTemp);
    ramp.pile?.forEach((item, index) => { if (index % 2 == 0) rampOptions.push({ text: item, value: ramp.pile?.[index + 1] }); });
  };


  onMount(async () => {
		try {
      $navigationStore.data = getHttpUrl('/iot/ramp');
      $navigationStore.isDirty = () => !isEqual(ramp, data);
      ramp = cloneDeep(data);
		} catch (error) {
      console.error(error);
		}
		ready = true;
  });

</script>

<GellertPage {wait} {ready} {title} {level} name="ramp">
  <Card class="w-3/4 mx-auto mt-2 flex flex-col">
    {#if ramp.rate && ramp.plenum && ramp.pile}
    <Table class="mb-2">
      <Row>
        <Column class="text-size-xl">
          <p class="text-center mb-2 border-b-1 border-gray-400">{ $t('level1.ramp.current-plenum-setpoint-is') } <b>{ramp.plenum}°</b></p>
          <hr class="mt-t border-t-8 bg-primary-700" />
          <p class="text-center mt-2">{ $t('level1.ramp.plenum-setpoint-will-change') } 
            <TextField class="w-28 3xl:w-36" size="xl" bind:value={ramp.rate[0]} {edit} label="Setpoint Change Rate" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.updTemp}/>
            {#if ramp.rate[1] === $t('global.automatically')}
              <TextField class="w-52 3xl:w-64" size="xl" bind:value={ramp.rate[1]} {edit} label="Update Time" keyboardType={KeyboardTypes.Auto} validation={validation.rampUpdateHours}/>
              { $t('level1.ramp.as-a-temperature-differential-of') }
              <TextField class="w-28 3xl:w-36" size="xl" bind:value={ramp.rate[2]} {edit} label="Temp Differential" keyboardType={KeyboardTypes.Numeric} validation={validation.rampTempDiff} />
              { $t('level1.ramp.is-reached') } { $t('level1.ramp.between-plenum-setpoint-and') }
              <Select class="w-48 md:w-96 text-center" size="xl" bind:value={ramp.rate[3]} options={rampOptions} {edit} />
              { $t('global.between-chinese')}
            {:else}
              { $t('level1.ramp.every') } <TextField class="w-28 3xl:w-36" size="xl" bind:value={ramp.rate[1]} {edit} label="Update Time" keyboardType={KeyboardTypes.Auto} validation={validation.rampUpdateHours}/>
              { $t('level1.ramp.hours-of-cooling-or-refrigeration-runtime') }
            {/if}
            { $t('level1.ramp.until-plenum-setpoint-equals') }
            <TextField class="w-28 3xl:w-36" size="xl" bind:value={ramp.rate[4]} {edit} label="Target Temperature" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.targetTemp}/>
          </p>
        </Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={ramp.rate} bind:original={data.rate} route="ramp" bind:validation={validation} autoSave/>
    {/if}
  </Card>
</GellertPage>



