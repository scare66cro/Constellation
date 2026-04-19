<script lang="ts">
	import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Column from "$lib/ui/Column.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Select from "$lib/ui/Select.svelte";
	import Table from "$lib/ui/Table.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { onMount } from "svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { homePageStore, keysStore, navigationStore, yesNoOptionsStore } from "$lib/store";
  import { checkKeys, getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
	import CryptoJS from "crypto-js";
  import { t } from "svelte-i18n";
	import { getHomePage } from "$lib/business/paging";
	import type { ArrayResponse } from "$lib/business/util";
	
  export let data: ArrayResponse;

  let title = $t('level2.basic.basic-setup');
  let edit = true;

  let homePageOptions = [
    { text:$t('system-monitor.system-monitor'), value: 'mnMainData.htm' },
    { text:$t('level2.basic.pile-temperatures'), value: 'mnPileTemps.htm' },
    { text:$t('level2.basic.pile-humidities'), value: 'mnPileHumids.htm' },
    { text:$t('level1.runclock.system-run-clock'), value: 'mnRunTimes.htm' },
    { text:$t('level1.fanspeed.fan-speed-control'), value: 'mnFreqCtrl.htm' },
    { text:$t('level2.basic.ramp-rate'), value: 'mnRampRate.htm' },
    { text:$t('level2.basic.plenum-humidity-control'), value: 'mnHumidCtrl.htm' },
    { text:$t('level1.climacell.climacell-control'), value: 'mnClimacellTimes.htm' },
    { text:`CO2 ${$t('level2.basic.purge-control')}`, value: 'mnCo2Purge.htm' },
    { text:$t('level1.equipment.equipment-status'), value: 'mnEquipStatus.htm' },
    { text:$t('page-list.panel-switches'), value: 'mnPanelSwitches.htm' },
    { text:$t('level1.network.network-monitor'), value: 'mnNetMonitor.htm' },
  ];

  let temperatureOptions = [
    { text: $t('level2.basic.fahrenheit'), value: '0' },
    { text: $t('level2.basic.celsius'), value: '1' },
  ];

  let modeOptions = [
    { text: $t('level2.basic.potato'), value: '0' },
    { text: $t('level2.basic.onion'), value: '1' },
  ];

  let validation = {
    'MultiView': ''
  };

  $: ready = true;
  $: wait = false;
  let basic: string[] = [];
  let passwordReady = false;
  // Track the last successfully decrypted data source to detect stale decryption
  let lastDecryptedSource: string[] | null = null;
  $: $keysStore.localRequired = (data.array?.length > 0 && data.array[9] === '1') ? true : false;
  $: encryptedData = encryptData(basic);
  
  // Reactive statement to handle secret key changes or data changes
  // Re-decrypt when secret becomes available OR when it changes and we haven't processed this data yet
  $: if ($keysStore.secret && data.array && data.array.length > 8) {
    // Check if we need to decrypt: either basic is empty, or we have a new secret, or data source changed
    const needsDecrypt = basic.length === 0 || lastDecryptedSource !== data.array || (basic[8] === '' && data.array[8]);
    if (needsDecrypt) {
      lastDecryptedSource = data.array;
      const decrypted = decryptData(data.array);
      basic = decrypted;
      passwordReady = true;
    }
  }

  function encryptData(values: string[]) {
    const retVal = cloneDeep(values);
    if ($keysStore.secret && retVal[8]) {
      retVal[8] = CryptoJS.AES.encrypt(retVal[8], $keysStore.secret).toString();
    }
    return retVal;
  }

  async function decryptDataWithRecover(inputs: string[]) {
    const values = cloneDeep(inputs);
    const tryDecrypt = (secret: string | undefined) => {
      if (secret && values[8]) {
        try {
          const decrypted = CryptoJS.AES.decrypt(values[8], secret).toString(CryptoJS.enc.Utf8);
          values[8] = decrypted || '';
        } catch (error) {
          values[8] = '';
        }
      } else {
        values[8] = values[8] || '';
      }
    };
    tryDecrypt($keysStore.secret);
    // If we failed to decrypt but had an encrypted-looking value, refresh keys once and retry
    if (inputs[8] && values[8] === '' && $keysStore.secret) {
      await checkKeys();
      // Re-read the secret after checkKeys refreshes it
      tryDecrypt($keysStore.secret);
    }
    return values;
  }

  // Backwards-compatible wrapper to keep existing calls working
  function decryptData(inputs: string[]) {
    // Fire and forget recovery; callers expecting sync can still get initial best-effort value
    // but we also update state when async recovery produces a better result.
    const initial = cloneDeep(inputs);
    // Kick off async recovery; when done, update basic if we are showing same record
    decryptDataWithRecover(inputs).then((recovered) => {
      // Only update if recovered password differs from current and is not blank
      // Also verify we're still looking at the same data source
      if (lastDecryptedSource === inputs && recovered[8] !== '' && basic[8] !== recovered[8]) {
        basic = recovered;
      }
    });
    // Quick first pass using current secret
    if ($keysStore.secret && initial[8]) {
      try {
        const decrypted = CryptoJS.AES.decrypt(initial[8], $keysStore.secret).toString(CryptoJS.enc.Utf8);
        initial[8] = decrypted || '';
      } catch (error) {
        initial[8] = '';
      }
    } else {
      initial[8] = initial[8] || '';
    }
    return initial;
  }
  function update() {
    // After save completes, decrypt the password again
    if (data.array && data.array.length > 8) {
      // Reset the tracking to force re-decryption
      lastDecryptedSource = null;
      basic = decryptData(data.array);
      lastDecryptedSource = data.array;
    }
    $navigationStore.invalidate = true;
  }

  onMount(async () => {
		try {
      // Ensure our DH secret matches the controller (Pi). After a reboot the server key resets.
      await checkKeys();
      
      // Retry logic if secret is not yet available (max 5 attempts with 100ms delays)
      let retries = 0;
      while (!$keysStore.secret && retries < 5) {
        await new Promise(resolve => setTimeout(resolve, 100));
        retries++;
      }
      
      if (!$keysStore.secret) {
        console.error('DH secret not available after retries');
      }
      
      $navigationStore.data = getHttpUrl(`/iot/basic`);
      $navigationStore.isDirty = () => {
        const decrypted = decryptData(data.array);
        return !isEqual(basic, decrypted);
      };
      
      // Ensure data.array exists and has proper structure
      if (data.array && data.array.length > 8) {
        // Force re-decryption now that we have verified the keys
        lastDecryptedSource = null;
        basic = decryptData(data.array);
        lastDecryptedSource = data.array;
        passwordReady = true;
      } else {
        // Initialize with empty array if data is missing
        basic = new Array(11).fill('');
        passwordReady = true;
      }
      
      $homePageStore.page = getHomePage(basic[3]);
		} catch (error) {
      console.error('Error initializing basic setup:', error);
      // Fallback initialization
      basic = new Array(11).fill('');
      passwordReady = true;
		}
		ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} level={2} name="basic">
  <Card class="md:mx-2 xl:mx-auto flex flex-col container-wide 3xl:container-standard">
    <Table class="text-size-xl">
      <Row>
        <Column class="w-1/2 border-r border-gray-400 !py-1">{ $t('level2.basic.storage-name') }</Column>
        <Column class="w-1/2 !py-1">
          <TextField class="w-[95%] mx-auto" size="xl" bind:value={basic[0]} {edit} label="Storage Name" keyboardType={KeyboardTypes.Alpha}/>
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400 !py-1">{ $t('level2.basic.home-page') }</Column>
        <Column class="w-1/2 !py-1">
          <Select class="xl:w-128 3xl:w-144" size="lg" bind:value={basic[3]} {edit} options={homePageOptions} />
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400 !py-1">Remote Access</Column>
        <Column class="w-1/2 !py-1">
          <div class="flex flex-col gap-1 py-1">
            <span class="text-base">
              Managed on the
              <a href="/level2/accounts" class="text-primary-700 underline hover:text-primary-900">Accounts</a>
              page via linked cloud users.
            </span>
            <span class="text-sm text-gray-600">
              Login required: <strong>{ basic[9] === '1' ? 'Yes' : 'No' }</strong>
              &nbsp;&middot;&nbsp; Legacy shared password:
              <strong>{ passwordReady ? (basic[8] ? 'Set' : 'Not set') : '…' }</strong>
            </span>
          </div>
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400 !py-1">{ $t('level2.basic.temperature-type') }</Column>
        <Column class="w-1/2 !py-1">
          <Select class="w-96" size="xl" extended="w-full" bind:value={basic[1]} {edit} options={temperatureOptions} />
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400 !py-1">{ $t('global.mode') }</Column>
        <Column class="w-1/2 !py-1">
          <Select class="w-1/2" size="xl" extended="w-full" bind:value={basic[4]} {edit} options={modeOptions} />
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400 !py-1">{ $t('level2.basic.display-equipment-animations') }</Column>
        <Column class="w-1/2 !py-1">
          <Select class="w-64" size="xl" extended="w-full" bind:value={basic[10]} {edit} options={$yesNoOptionsStore} />
        </Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={encryptedData} bind:original={data.array} route="basic" bind:validation={validation} on:complete={update}/>
  </Card>
</GellertPage>
