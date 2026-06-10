<script lang="ts">
  import { onMount } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Button from "$lib/ui/Button.svelte";
  import { navigationStore } from "$lib/store";
  import { getHttpUrl, safeJsonParse } from "$lib/business/util";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { emailSettings } from "$lib/business/protoStores";
  import { useDraft, numField } from "$lib/business/useDraft";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // Shared body of Email Alerts setup (level1/email): SMTP server + recipients
  // + test send. Classic page AND the "Email Server" tab of the dashboard
  // Alerts modal. docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  $: emailOptions = [
    { text: $t('global.enable'),        value: 0 },
    { text: $t('level1.email.disable'), value: 1 },
  ];
  $: authOptions = [
    { text: 'StartTLS',        value: 0 },
    { text: 'TLS-SSL',         value: 1 },
    { text: $t('global.none'), value: 2 },
  ];
  let validation = { emailTo: '', emailFrom: '', emailPort: '', emailAccount: '', emailPassword: '', emailServer: '' };
  $: defaultDisplay = { text: $t('global.none'), value: 'not selected' };

  let displayOptions = [] as { text: string, value: string }[];
  $: edit = canEdit ?? ($navigationStore.level > 0);

  const em = useDraft(emailSettings, TAG.EmailSettings);
  const { draft, live, hydrated } = em;
  const portStr = numField(draft, 'port', 'int');
  $: textSize = edit ? 'text-size-large' : 'text-size-xl';
  $: compSize = edit ? 'lg' : 'xl';

  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    if (!isEqual($draft, $live) && saveBtn) await saveBtn.save();
  }

  onMount(async () => {
    try {
      if (!embedded) $navigationStore.isDirty = () => !isEqual($draft, $live);
      ready = true;
      if ($draft.enabled === 0) await findDisplays();
    } catch (e) { console.error(e); }
  });

  async function findDisplays() {
    wait = true;
    displayOptions = [defaultDisplay];
    try {
      const displays = await safeJsonParse(await fetch(getHttpUrl('/iot/displays')));
      for (let i = 0; i < displays.data.DisplayList?.length; i += 5) {
        displayOptions.push({ text: `${displays.data.DisplayList[i]} ${displays.data.DisplayList[i + 2]}`, value: displays.data.DisplayList[i] });
      }
      displayOptions = displayOptions;
      if (!$draft.displayId) $draft.displayId = displays.data.LocalIpAdd[0].split(':')[0];
    } catch (error) { console.error(error); }
    wait = false;
  }

  async function sendTestEmail() {
    if ($draft.enabled !== 0) return;
    wait = true;
    try {
      await fetch(getHttpUrl('/iot/email/test'), { method: 'POST', headers: { 'Content-Type': 'application/json' } });
    } catch (error) { console.error('Error sending test email:', error); }
    finally { wait = false; }
  }
</script>

<div class="pform pform--{theme}">
  <Card class="w-3/4 mx-auto mt-1 flex flex-col">
    {#if $hydrated}
    <Table class="{textSize}">
      <Row>
        <Column class="xl:py-1 items-center"><p class="text-center">{ $t('level1.email.send-email-alerts') }: <Select bind:value={$draft.enabled} class="w-96" size={compSize} options={emailOptions} {edit}/></p></Column>
      </Row>
      {#if $draft.enabled === 0}
        <Row><Column class="xl:py-1 items-center"><p class="text-center">{ $t('level1.email.to') }: <TextField class="w-full leading-snug" size={compSize} extended="w-1/2" bind:value={$draft.toAddr} label="To" keyboardType={KeyboardTypes.Alpha} {edit} validation={validation.emailTo}/></p></Column></Row>
        <Row><Column class="xl:py-1 items-center"><p class="text-center">{ $t('level1.email.from') }: <TextField class="w-full leading-snug" size={compSize} extended="w-1/2" bind:value={$draft.fromAddr} label="From" keyboardType={KeyboardTypes.Alpha} {edit} validation={validation.emailFrom}/></p></Column></Row>
      {/if}
    </Table>
    {#if $draft.enabled === 0}
      <Table class="test-size-xl">
        <Row><Column class="xl:py-1 items-center"><p class="text-center {textSize}">{ $t('level1.email.email-server') }: <TextField class="w-full leading-snug" size={compSize} extended="w-1/2" bind:value={$draft.server} label="Email Server" keyboardType={KeyboardTypes.Alpha} {edit} validation={validation.emailServer}/></p></Column></Row>
        <Row><Column class="xl:py-1 items-center"><p class="text-center {textSize}">{ $t('level1.email.authentication-type') }: <Select bind:value={$draft.authType} class="w-96 mr-4" size={compSize} options={authOptions} {edit}/> { $t('global.port') }: <TextField class="w-48" size={compSize} bind:value={$portStr} label="Port" keyboardType={KeyboardTypes.Numeric} {edit} validation={validation.emailPort}/></p></Column></Row>
        <Row><Column class="xl:py-1 items-center"><p class="text-center {textSize}">{ $t('level1.email.account') }: <TextField class="w-full leading-snug" size={compSize} extended="w-1/3 mr-4" bind:value={$draft.username} label="Account" keyboardType={KeyboardTypes.Alpha} {edit} validation={validation.emailAccount}/> { $t('global.password') }: <TextField type="password" class="w-full leading-snug" size={compSize} extended="w-1/3" bind:value={$draft.password} label="Password" keyboardType={KeyboardTypes.Alpha} {edit} validation={validation.emailPassword}/></p></Column></Row>
      </Table>
    {/if}
    <Table class="mb-1 {textSize}">
      <Row>
        <Column class="xl:py-1 items-center"><p class="text-center">{ $t('level1.email.send-from-display') }: <Select class="w-full" size={compSize} extended="w-1/2 mr-4" bind:value={$draft.displayId} options={displayOptions} {edit} disabled={$draft.enabled === 1}/>{#if edit}<Button size={compSize} on:click={findDisplays}>{ $t('global.find') }</Button>{/if}</p></Column>
      </Row>
    </Table>
    <div class="flex flex-row">
      <div class="w-1/3"></div>
      <div class="w-1/3 flex justify-center">
        <SaveButton bind:this={saveBtn} {edit} bind:wait={wait} data={$draft} original={$live} bind:validation={validation} autoSave onSave={() => em.save()} />
      </div>
      <div class="w-1/3 flex justify-end">
        {#if edit && $draft.enabled === 0}
          <Button class="mr-4 !mb-1" size={compSize} on:click={sendTestEmail}>{ $t('level1.email.send-test-email') }</Button>
        {/if}
      </div>
    </div>
    {/if}
  </Card>
</div>
