<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Button from "$lib/ui/Button.svelte";
	import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import { safeJsonParse, type ArrayResponse } from "$lib/business/util";

  export let data: ArrayResponse;

  let title = $t('level1.email.email-alerts');

  let emailOptions = [
    { text: $t('global.enable'), value: '0'},
    { text: $t('level1.email.disable'), value: '1'},
  ];

  let authOptions = [
    { text: 'StartTLS', value: '0'},
    { text: 'TLS-SSL', value: '1'},
    { text: $t('global.none'), value: '2'},
  ];

  let validation = {
    'emailTo': '',
    'emailFrom': '',
    'emailPort': '',
    'emailAccount': '',
    'emailPassword': '',
    'emailServer': ''
  }

  let defaultDisplay = { text: $t('global.none'), value: 'not selected' };

  $: ready = false;

  $: wait = false;

  $: displayOptions = [] as { text: string, value: string }[];

  $: edit = $navigationStore.level > 0;

  $: level = $navigationStore.level;

  $: email = [] as string[];

  $: textSize = edit ? 'text-size-large' : 'text-size-xl';
  $: compSize = edit ? 'lg' : 'xl';

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl('/iot/email');
      $navigationStore.isDirty = () => !isEqual(email, data.array)
      email = cloneDeep(data.array);

      if (data.array[0] === '0') {
        ready = true;
        await findDisplays();
      }
    } catch (e) {
      console.error(e);
    }
    ready = true;
  });

  async function findDisplays() {
    wait = true;
    displayOptions = [defaultDisplay];
    const result = await fetch(getHttpUrl('/iot/displays'));
    try {
      const displays = await safeJsonParse(result);
      for (let i = 0; i < displays.data.DisplayList?.length; i += 5) {
        displayOptions.push({ text: `${displays.data.DisplayList[i]} ${displays.data.DisplayList[i + 2]}`, value: displays.data.DisplayList[i] });
      }
      displayOptions = displayOptions;
      if (email.length > 6) {
        email[6] = displays.data.LocalIpAdd[0].split(':')[0];
      }
    } catch (error) {
      console.error(error);
    }
    wait = false;
  }

  async function sendTestEmail() {
    if (email[0] === '0') {
      wait = true;
      try {
        const response = await fetch(getHttpUrl('/iot/email/test'), {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
        });
      } catch (error) {
        console.error('Error sending test email:', error);
      } finally {
        wait = false;
      }
    }
  }
</script>

<GellertPage {wait} {ready} {title} {level} name="email">
  <Card class="w-3/4 mx-auto mt-1 flex flex-col">
    {#if email.length > 7}
    <Table class="{textSize}">
      <Row>
        <Column class="xl:py-1 items-center">
          <p class="text-center">
            { $t('level1.email.send-email-alerts') }:
            <Select bind:value={email[0]} class="w-96" size={compSize} options={emailOptions} {edit}/>
          </p>
        </Column>
      </Row>
      {#if email[0] === '0'}
        <Row>
          <Column class="xl:py-1 items-center">
            <p class="text-center">
              { $t('level1.email.to') }:
              <TextField class="w-full leading-snug" size={compSize} extended="w-1/2" bind:value={email[7]} label="To" keyboardType={KeyboardTypes.Alpha} {edit} validation={validation.emailTo}/>
            </p>
          </Column>
        </Row>
        <Row>
          <Column class="xl:py-1 items-center">
            <p class="text-center">
              { $t('level1.email.from') }:
              <TextField class="w-full leading-snug" size={compSize} extended="w-1/2" bind:value={email[8]} label="From" keyboardType={KeyboardTypes.Alpha} {edit} validation={validation.emailFrom}/>
            </p>
          </Column>
        </Row>
      {/if}
    </Table>
    {#if email[0] === '0'}
      <Table class="test-size-xl">
        <Row>
          <Column class="xl:py-1 items-center">
            <p class="text-center {textSize}">
              { $t('level1.email.email-server') }:
              <TextField class="w-full leading-snug" size={compSize} extended="w-1/2" bind:value={email[1]} label="Email Server" keyboardType={KeyboardTypes.Alpha} {edit} validation={validation.emailServer}/>
            </p>
          </Column>
        </Row>
        <Row>
          <Column class="xl:py-1 items-center">
            <p class="text-center {textSize}">
              { $t('level1.email.authentication-type') }:
              <Select bind:value={email[2]} class="w-96 mr-4" size={compSize} options={authOptions} {edit}/>
              { $t('global.port') }:
              <TextField class="w-48" size={compSize} bind:value={email[3]} label="Port" keyboardType={KeyboardTypes.Numeric} {edit} validation={validation.emailPort}/>
            </p>
          </Column>
        </Row>
        <Row>
          <Column class="xl:py-1 items-center">
            <p class="text-center {textSize}">
              { $t('level1.email.account') }:
              <TextField class="w-full leading-snug" size={compSize} extended="w-1/3 mr-4" bind:value={email[4]} label="Account" keyboardType={KeyboardTypes.Alpha} {edit} validation={validation.emailAccount}/>
              { $t('global.password') }:
              <TextField type="password" class="w-full leading-snug" size={compSize} extended="w-1/3" bind:value={email[5]} label="Password" keyboardType={KeyboardTypes.Alpha} {edit} validation={validation.emailPassword}/>
            </p>
          </Column>
        </Row>
      </Table>
    {/if}
    <Table class="mb-1 {textSize}">
      <Row>
        <Column class="xl:py-1 items-center">
          <p class="text-center">
            { $t('level1.email.send-from-display') }:
            <Select class="w-full" size={compSize} extended="w-1/2 mr-4" bind:value={email[6]} options={displayOptions} {edit} disabled={email[0] === '1'}/>
            {#if edit}
              <Button size={compSize} on:click={findDisplays}>{ $t('global.find') }</Button>
            {/if}
          </p>
        </Column>
      </Row>
    </Table>
    <div class="flex flex-row">
      <div class="w-1/3">
      </div>
      <div class="w-1/3 flex justify-center">
        <SaveButton {edit} bind:wait={wait} data={email} bind:original={data.array} route="email" bind:validation={validation} autoSave />
      </div>
      <div class="w-1/3 flex justify-end">
        {#if edit && email[0] === '0'}
          <Button class="mr-4 !mb-1" size={compSize} on:click={sendTestEmail}>{ $t('level1.email.send-test-email') }</Button>
        {/if}
      </div>
    </div>
  {/if}
  </Card>
</GellertPage>



