<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Button from "$lib/ui/Button.svelte";
  import Card from "$lib/ui/Card.svelte";
  import { format } from "date-fns";
  import { t } from "svelte-i18n";
  import { safeJsonParse, getHttpUrl } from "$lib/business/util";

  // The page exposes five settings-persistence operations:
  //
  //   - Download to file: GET /iot/settings/export → save JSON blob
  //     locally. The blob contains the latest broadcast bytes for each
  //     round-trippable settings group (see SETTINGS_EXPORT_TABLE in
  //     apiRoutes.ts). Suitable for off-panel backup.
  //   - Restore from file: POST /iot/settings/import with a previously
  //     downloaded blob. Each entry is replayed via SettingsUpdate
  //     writes through the same /proto/write/<n> path the per-page
  //     saves use.
  //   - Save current as panel default: CMD_SET_DEFAULT (firmware
  //     persists the current bank as the panel-default OSPI blob).
  //   - Restore panel default: CMD_PANEL_DEFAULT (replay).
  //   - Restore factory default: CMD_FACTORY_DEFAULT (wipes user
  //     settings, re-runs s_io_defaults / engine cold-start path).

  let wait = false;
  let statusMsg = '';
  let statusError = false;
  let restoreInput: HTMLInputElement;

  function showStatus(msg: string, error: boolean) {
    statusMsg = msg;
    statusError = error;
    setTimeout(() => { statusMsg = ''; }, 6000);
  }

  async function postButton(payload: Record<string, string>) {
    wait = true;
    try {
      const response = await fetch(getHttpUrl('/iot/button'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ tag: 'button2', ...payload }),
      });
      const json = await safeJsonParse(response);
      if (typeof window !== 'undefined' && window.handleUnauthorized) {
        await window.handleUnauthorized(json);
      }
    } catch (error) {
      console.error('Settings button POST failed:', error);
    } finally {
      wait = false;
    }
  }

  const settingsToPanelDefault   = () => postButton({ SetDefault:    'Save'    });
  const restoreToPanelDefault    = () => postButton({ PanelDefault:  'Restore' });
  const restoreToFactoryDefault  = () => postButton({ FactoryDefault:'Restore' });

  async function downloadSettings() {
    wait = true;
    try {
      const response = await fetch(getHttpUrl('/iot/settings/export'));
      if (!response.ok) {
        const txt = await response.text();
        throw new Error(`HTTP ${response.status}: ${txt}`);
      }
      const blob = await response.blob();
      const json = JSON.parse(await blob.text());
      const fname = `Constellation-Settings_${json.panel ?? 'panel'}_${format(new Date(), 'yyyy-MM-dd_HH-mm')}.json`;
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = fname;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
      showStatus(`Downloaded ${json.fields?.length ?? 0} settings groups`, false);
    } catch (error: any) {
      console.error('Settings download failed:', error);
      showStatus($t('level2.settings.download-error', { values: { error: error.message } }), true);
    } finally {
      wait = false;
    }
  }

  function pickRestoreFile() {
    restoreInput?.click();
  }

  async function onRestoreFile(ev: Event) {
    const file = (ev.target as HTMLInputElement)?.files?.[0];
    if (!file) return;
    if (!confirm($t('level2.settings.restore-confirm'))) {
      restoreInput.value = '';
      return;
    }
    wait = true;
    try {
      const text = await file.text();
      const blob = JSON.parse(text);
      const response = await fetch(getHttpUrl('/iot/settings/import'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(blob),
      });
      const result = await safeJsonParse(response);
      if (!response.ok || !result?.ok) {
        throw new Error(result?.error ?? `HTTP ${response.status}`);
      }
      showStatus($t('level2.settings.restore-success', { values: { ok: result.applied, total: result.total } }), false);
    } catch (error: any) {
      console.error('Settings restore failed:', error);
      showStatus($t('level2.settings.restore-error', { values: { error: error.message } }), true);
    } finally {
      restoreInput.value = '';
      wait = false;
    }
  }
</script>
<GellertPage title="{ $t('level2.settings.save-restore-panel-settings') }" name="settings" ready={true} level={2} {wait}>
  <Card class="mx-auto w-11/12 flex flex-col">
    <Table class="mb-2 text-size-xl">
      <Row>
        <Column class="w-2/3 items-center border-r border-gray-400">{ $t('level2.settings.download-current-system-settings-to-file') }</Column>
        <Column class="w-1/3 items-center"><Button class="w-48 xl:w-64" size="xl" on:click={downloadSettings}>{ $t('global.download') }</Button></Column>
      </Row>
      <Row>
        <Column class="w-2/3 items-center border-r border-gray-400">{ $t('level2.settings.restore-system-settings-from-file') }</Column>
        <Column class="w-1/3 items-center"><Button class="w-48 xl:w-64" size="xl" on:click={pickRestoreFile}>{ $t('level2.settings.restore') }</Button></Column>
      </Row>
      <Row>
        <Column class="w-2/3 items-center border-r border-gray-400">{ $t('level2.settings.save-current-system-settings-as-panel-default') }</Column>
        <Column class="w-1/3 items-center"><Button class="w-48 xl:w-64" size="xl" on:click={settingsToPanelDefault}>{ $t('global.save') }</Button></Column>
      </Row>
      <Row>
        <Column class="w-2/3 items-center border-r border-gray-400">{ $t('level2.settings.restore-to-panel-default-settings') }</Column>
        <Column class="w-1/3 items-center"><Button class="w-48 xl:w-64" size="xl" on:click={restoreToPanelDefault}>{ $t('level2.settings.restore') }</Button></Column>
      </Row>
      <Row>
        <Column class="w-2/3 items-center border-r border-gray-400">{ $t('level2.settings.restore-to-factory-default-settings') }</Column>
        <Column class="w-1/3 items-center"><Button class="w-48 xl:w-64" size="xl" on:click={restoreToFactoryDefault}>{ $t('level2.settings.restore') }</Button></Column>
      </Row>
    </Table>
    {#if statusMsg}
      <div class="mt-2 mx-auto px-4 py-2 rounded-md text-size-large text-white {statusError ? 'bg-red-700' : 'bg-green-700'}">
        {statusMsg}
      </div>
    {/if}
    <input
      bind:this={restoreInput}
      type="file"
      accept="application/json,.json"
      class="hidden"
      on:change={onRestoreFile}
    />
  </Card>
</GellertPage>

