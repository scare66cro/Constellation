<script lang="ts">
	import GellertPage from "$lib/components/GellertPage.svelte";
	import Button from "$lib/ui/Button.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Column from "$lib/ui/Column.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
	import { onMount } from "svelte";
	import { Auxiliary } from "$lib/business/auxOptions";
  import Rule from "$lib/components/Rule.svelte";
	import Rule2 from "$lib/components/Rule2.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import TextField from "$lib/ui/TextField.svelte";
  import Select from "$lib/ui/Select.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { auxiliaryOptionsStore, navigationStore, heightsStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { getModalStore, type ModalSettings } from "@skeletonlabs/skeleton";
	import { safeJsonParse } from "$lib/business/util";
	import ScrollableArea from "$lib/components/ScrollableArea.svelte";

  const modalStore = getModalStore();

  let title = $t('level2.auxiliary.auxiliary-output-programming');
  let edit = true;
  let validation = clearValidation();

  $: sensorValidation = [
    validation.sen1,
    validation.sen2,
    validation.sen3,
    validation.sen4,
    validation.sen5,
    validation.sen6,
  ];

  $: diffValidation = [
    validation.diff1,
    validation.diff2,
    validation.diff3,
    validation.diff4,
    validation.diff5,
    validation.diff6,
  ];

  $: aux = undefined as Auxiliary | undefined;
  $: ready = false;
  $: wait = false;
  let saveHeight: number = 0;
  let headerHeight: number = 0;
  let dutyHeight: number = 0;
  let height = 768;
  $: if (typeof window !== 'undefined') {
    // Responsive offset similar to IO Config page
    const availableHeight = window.innerHeight;
    let offset = 5;
    if (availableHeight >= 1080) {
      const baseHeight = 1080;
      const baseOffset = 75;
      const maxOffset = 120;
      const heightRatio = Math.min((availableHeight - baseHeight) / (1440 - baseHeight), 1);
      offset = baseOffset + (maxOffset - baseOffset) * heightRatio;
    } else if (availableHeight >= 768) {
      const baseHeight = 768;
      const baseOffset = 35;
      const maxOffset = 75;
      const heightRatio = (availableHeight - baseHeight) / (1080 - baseHeight);
      offset = baseOffset + (maxOffset - baseOffset) * heightRatio;
    } else if (availableHeight >= 600) {
      offset = 25;
    }
    const minBottomMargin = 4;
    height = $heightsStore.main - $heightsStore.header - $heightsStore.footer - offset - minBottomMargin;
  }
  
  // Add data variable to hold fetched data
  let data: Auxiliary | undefined = undefined;
  
  // Use a key to track navigation changes
  let navigationKey = 0;

  let allAux: { auxProg: string[], rules: any[] }[] = [];
  let currentIndex = 0;
  let reset = false;

  onMount(async () => {
    try {
      ready = false; // Set ready to false while fetching
      await refresh();
      $navigationStore.data = getHttpUrl(`/iot/aux/all`);
      $navigationStore.isDirty = () => !isEqual(data, aux);
    } catch (error) {
      console.error((error as Error).message);
    }
    ready = true;
  });

  async function refresh(retry = true) {
    const response = await fetch(getHttpUrl('/iot/aux/all'));
    const result = await safeJsonParse(response);
    
    // If empty, server might be discovering. Wait and retry once.
    if ((!result.allAux || result.allAux.length === 0) && retry) {
        await new Promise(r => setTimeout(r, 3500));
        await refresh(false);
        return;
    }

    // result has common data + allAux
    if (result.allAux && Array.isArray(result.allAux)) {
        allAux = result.allAux;
    }
    
    // Construct base data from result (excluding allAux)
    const baseData = { ...result };
    delete baseData.allAux;

    if (currentIndex >= allAux.length) currentIndex = 0;
    
    // Find first valid auxiliary if current is empty
    let count = 0;
    let idx = currentIndex;
    while(count < allAux.length) {
       const item = allAux[idx];
       if (item && item.rules && item.rules.length > 0) {
          currentIndex = idx;
          break;
       }
       idx = (idx + 1) % allAux.length;
       count++;
    }
    
    data = createAuxiliary(baseData, allAux[currentIndex]);
    aux = cloneDeep(data);
    reset = true;
  }

  function createAuxiliary(base: any, item: { auxProg: string[], rules: any[] }) {
      if (!base || !item) return undefined;
      return { ...base, ...item } as Auxiliary;
  }

  async function handleSaveComplete(event: CustomEvent<{ success: boolean }>) {
      if (event.detail.success) {
          wait = true;
          try {
             await refresh();
          } catch(e) { console.error(e); }
          wait = false;
      }
  }

  function checkDirty(action: () => void) {
    if ($navigationStore.isDirty()) {
      const modal: ModalSettings = {
        type: 'confirm',
        // Data
        title: $t('global.confirm'),
        body: $t('global.are-you-sure'),
      };
      modal.buttonTextCancel=$t('global.no');
     	modal.buttonTextConfirm=$t('global.yes');
      // TRUE if confirm pressed, FALSE if cancel pressed
      modal.response = (r: boolean) => { if (r) {
        action();
      }};
      modalStore.trigger(modal);
    } else {
        action();
    }
  }

  function clearValidation() {
    return {
      sen1: '',
      sen2: '',
      sen3: '',
      sen4: '',
      sen5: '',
      sen6: '',
      diff1: '',
      diff2: '',
      diff3: '',
      diff4: '',
      diff5: '',
      diff6: '',
      dutyCycle: '',
      period: '',
    };
  }

  async function moveAux(dir: string) {
    checkDirty(async () => {
      // wait = true;
      if (!allAux || allAux.length === 0) return;

      let nextIndex = currentIndex;
      let count = 0;
      
      do {
        if (dir === 'Back') {
           nextIndex = (nextIndex - 1 + allAux.length) % allAux.length;
        } else {
           nextIndex = (nextIndex + 1) % allAux.length;
        }
        count++;
        // Check if item is valid
        const item = allAux[nextIndex];
        if (item && item.rules && item.rules.length > 0) {
           break;
        }
      } while (count < allAux.length);
      
      currentIndex = nextIndex;

      // Reconstruct data from current base properties + new item
      // We assume data has the common props.
      const baseData = { ...data };
      delete baseData.auxProg;
      delete baseData.rules;
      
      data = createAuxiliary(baseData, allAux[currentIndex]);
      aux = cloneDeep(data);
      
      navigationKey += 1; 
      validation = clearValidation();
      reset = true;
      // wait = false;
    });
  }
</script>

<GellertPage {wait} {ready} {title} level={2} name="auxiliary">
  <Card class="w-[98%] mx-2 flex flex-col" {height}>
    {#if !data || aux === undefined || aux.rules.length === 0}
      <span class="text-size-xl">{ $t('level2.auxiliary.no-auxiliary-output-defined') }.</span>
    {:else}
  {#key navigationKey}
    <!-- Fixed header -->
    <div bind:clientHeight={headerHeight}>
      <Table class="text-size-xl">
        <Row>
          <Column class="font-bold border-r border-gray-400">{ $t('global.output') }</Column>
          <Column class="font-bold" colspan={3}>{ $t('level2.auxiliary.rule') }</Column>
        </Row>
      </Table>
    </div>

    <!-- Scrollable rules -->
    <ScrollableArea>
      <Table class="text-size-xl">
        {#each aux.rules as rule, index}
          {#if rule.andOr !== '256'}
            <Row>
              <Rule {index} name={aux.IoNames?.[parseInt(aux.auxProg[0], 10)]?.split(':')[0] || `Auxiliary ${parseInt(aux.auxProg[0], 10) - 24}`}
                bind:rule={rule} bind:aux={aux} on:change={() => rule = rule}/>
            </Row>
            {#if rule.type === '4' && rule.st !== '255' && data?.rules[index]}
              <Row>
                <Rule2 bind:rule={rule} bind:data={data.rules[index]} sensorValidation={sensorValidation[index]} diffValidation={diffValidation[index]}/>
              </Row>
            {/if}
          {/if}
        {/each}
      </Table>
    </ScrollableArea>
    <!-- Fixed duty cycle -->
    <div bind:clientHeight={dutyHeight}>
      <Table class="text-size-xl">
        <Row>
          <Column colspan={4}>
            { $t('global.duty-cycle') }:
            <TextField
              size="xl"
              class="w-36"
              bind:value={aux.auxProg[1]}
              label="Duty Cycle"
              keyboardType={KeyboardTypes.Numeric}
              adornmentType={AdornmentType.Percent}
              {edit}
              validation={validation.dutyCycle}
            />
            <span class="ml-4">{ $t('level2.auxiliary.period') }:</span>
            <TextField
              size="xl"
              class="w-24"
              bind:value={aux.auxProg[2]}
              label="Period"
              keyboardType={KeyboardTypes.Numeric}
              {edit}
              validation={validation.period}
            />
            <Select class="w-64" size="xl" bind:value={aux.auxProg[3]} options={$auxiliaryOptionsStore.unitOptions} {edit} />
          </Column>
        </Row>
      </Table>
    </div>
  {/key}

  <div class="flex flex-row items-center" bind:clientHeight={saveHeight}>
        <Button size="xl" class="ml-auto mr-2 mb-0" on:click={() => moveAux('Back')}>{ $t('global.back') }</Button>
        <SaveButton {edit} bind:wait={wait} data={aux} bind:original={data} route="aux" bind:validation={validation} bind:reset={reset} on:complete={handleSaveComplete} autoSave />
        <Button size="xl" class="mr-auto mb-0" on:click={() => moveAux('Next')}>{ $t('global.next') }</Button>
      </div>
    {/if}
  </Card>
</GellertPage>



