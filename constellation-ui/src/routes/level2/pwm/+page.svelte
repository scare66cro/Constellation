<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Column from "$lib/ui/Column.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
  import Select from "$lib/ui/Select.svelte";
  import { onMount } from "svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import ScrollableArea from "$lib/components/ScrollableArea.svelte";

  type PWM = {
    pwmConfig: string[],
    pwmChannels: string[],
    ioAvailable: string[],
    systemMode: string,
  };
  export let data: PWM;

  let edit = true;
  let title = $t('level2.pwm.4-20ma-pwm-output-setup');
  let ioInfo: Array<Array<string>> = [];
  let potatoMode: boolean;
  let onionMode: boolean;
  let pecanMode: boolean;

  $: availPwmList = [{ text: $t('global.none'), value: '-1' }];

  $: ready = false;
  $: wait = false;
  $: pwm = { pwmConfig: [], pwmChannels: [], ioAvailable: [], systemMode: '' } as PWM;
  $: {
    pwm.ioAvailable.pop();
    pwm.ioAvailable.map((io, i) => {
      ioInfo[i] = io.split(':');
    });
    potatoMode = pwm.systemMode === '0';
    onionMode = pwm.systemMode === '1';
    pecanMode = pwm.systemMode === '3';
    pwm.pwmConfig
      .filter((item, index) => (
        index !== 2 && // skip fan
        (((potatoMode || pecanMode) && item[1] === '1')
        || (onionMode && item[1] === '2')
        || item[1] === '4'
        || item[1] === '6'
        || item[1] === '7')))
      .forEach((item) => {
        const index = parseInt(item[3], 10);
        availPwmList.push({ text: pwm.pwmConfig[index][0], value: pwm.pwmConfig[index][3] });
      });
  }

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl(`/iot/pwm`);
      $navigationStore.isDirty = () => !isEqual(pwm.pwmChannels, data.pwmChannels);
      pwm = cloneDeep(data);
    } catch (error) {
      console.error(error);
    }
    ready = true;
  });

  function updatePwm(event: Event, i: number, j: number) {
    const value = (event.target as HTMLSelectElement).value;
    const index = pwm.pwmChannels.indexOf((i*2 + j).toString());
    if (index !== -1) {
      pwm.pwmChannels[index] = '-1';
    }
    if (value !== '-1') {
      pwm.pwmChannels[parseInt(value, 10)] = (i*2 + j).toString();
    }
    pwm = pwm;
  }
</script>

<GellertPage {wait} {title} level={2} {ready} name="pwm">
  <Card class="xl:w-3/4 md:mx-2 xl:mx-auto flex flex-col">
    <ScrollableArea>
    {#each ioInfo as io, i}
      {#if io[0].indexOf('none') === -1}
        <Table class="mb-2 text-size-xl">
          <Row>
            <Column class="w-1/4 border-r border-gray-400 py-2 font-bold"># {io[0]}</Column>
            <Column class="w-3/4 py-2 font-bold">{ $t('level2.pwm.4-20-output') }</Column>
          </Row>
          {#each Array.from({ length: parseInt(io[3]) }, (_, j) => j) as j}
            <Row>
              <Column class="w-1/4 my-2 border-r border-gray-400">{j + 1}</Column>
              <Column class="w-3/4 my-2">
                {#if i === 0 && j === 1}
                  <div class="my-2">{pwm.pwmConfig[2][0]} *</div>
                {:else}
                  <div class="px-2">
                    <Select class="mx-8" inline={false} size="xl" value={pwm.pwmChannels.indexOf((i*2 + j).toString()).toString()}
                      options={availPwmList}
                      {edit}
                      on:change={(event) => updatePwm(event, i, j)}
                    />
                  </div>
                {/if}
              </Column>
            </Row>
          {/each}
        </Table>
      {/if}
    {/each}
    <SaveButton slot="footer-center" {edit} bind:wait={wait} data={pwm.pwmChannels} bind:original={data.pwmChannels} route="pwm" autoSave />
    </ScrollableArea>
  </Card>
</GellertPage>


