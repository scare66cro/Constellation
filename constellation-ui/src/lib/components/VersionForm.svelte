<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Button from "$lib/ui/Button.svelte";
  import uiversion from "$lib/business/uiversion";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import { navigationStore } from "$lib/store";
  import { getHttpUrl, fetchWithTimeout } from "$lib/business/util";
  import { t } from "svelte-i18n";
  import WsClient from "$lib/business/wsClient";
  import { versionInfo } from "$lib/business/protoStores";

  // Shared body of the System Software Versions page (level1/version): version
  // readout (controller / web / UI / analog boards / orbit firmware) in view
  // mode, and the Constellation Firmware Update (.cfu) uploader/installer when
  // editable. Rendered from ONE source of truth — the classic page AND a
  // no-save dashboard modal (⚙ Setup → System → Software Version). `edit`
  // (= canEdit) switches view ↔ updater. Prop contract mirrors FanSpeedForm.
  // docs/spatial-ui-page-migration.md ; memories/repo/cfu-firmware-bundle-design.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  $: edit = canEdit ?? ($navigationStore.level > 0);

  let version: { controller: string[], webserver: string, ui: string } = {
    controller: [], webserver: '', ui: '',
  };
  let boards: { name: string, version: string }[] = [];

  type OrbitOtaRow = {
    slot: number; label: string; host: string;
    reachable: boolean; version: string; error: string;
  };
  const ORBIT_OTA_SLOTS: { slot: number; label: string }[] = [
    { slot: 0, label: 'STORAGE' },
    { slot: 1, label: 'GDC' },
    { slot: 2, label: 'TRITON' },
  ];
  let orbitOtaRows: OrbitOtaRow[] = ORBIT_OTA_SLOTS.map(({ slot, label }) => ({
    slot, label, host: '', reachable: false, version: '', error: 'querying…',
  }));
  let orbitOtaTimer: ReturnType<typeof setInterval> | undefined;

  async function refreshOrbitOta(): Promise<void> {
    const next = await Promise.all(ORBIT_OTA_SLOTS.map(async ({ slot, label }) => {
      try {
        const res = await fetchWithTimeout(
          getHttpUrl(`/iot/orbit/ota/version?slot=${slot}`), { method: 'GET' }, 5000,
        );
        const body = await res.json().catch(() => ({}));
        if (res.ok && body?.reachable) {
          return { slot, label, host: body.host ?? '', reachable: true,
            version: body.bankInfo?.bankAVersion ?? '', error: '' };
        }
        return { slot, label, host: body?.host ?? '', reachable: false,
          version: '', error: body?.error ?? `HTTP ${res.status}` };
      } catch (err) {
        return { slot, label, host: '', reachable: false, version: '',
          error: (err as Error).message ?? 'fetch failed' };
      }
    }));
    orbitOtaRows = next;
  }

  type CfuComponentState =
    | 'pending' | 'pushing' | 'rebooting' | 'verifying' | 'done' | 'skipped' | 'failed';
  type CfuComponentProgress = {
    name: string; targetIp?: string; slot?: number; state: CfuComponentState;
    bytesWritten?: number; totalSize?: number; errorMessage?: string;
  };
  type CfuInstallProgress = {
    bundleVersion: string;
    overallState: 'idle' | 'starting' | 'installing' | 'done' | 'failed';
    failureReason?: string; components: CfuComponentProgress[];
  };

  let cfuFileInput: HTMLInputElement;
  let cfuUploading = false;
  let cfuInstalling = false;
  let cfuStagingId = '';
  let cfuStagedFilename = '';
  let cfuStagedSize = 0;
  let cfuError = '';
  let cfuProgress: CfuInstallProgress | null = null;
  let cfuWsClient: WsClient | undefined;
  let cfuDryRun = false;
  $: cfuOverallActive =
       cfuProgress != null
       && cfuProgress.overallState !== 'done'
       && cfuProgress.overallState !== 'failed';

  $: if ($versionInfo) refreshVersion($versionInfo);

  function refreshVersion(info: {
    armVersion: string; bootloaderVersion: string;
    boards: { address: number; version: string; type: number }[];
  }) {
    boards = [];
    const head = info.bootloaderVersion
      ? `${info.armVersion},${info.bootloaderVersion}` : info.armVersion;
    version = { controller: [head], webserver: '1.0.0-bridge', ui: uiversion };
    for (const b of info.boards ?? []) {
      boards.push({ name: `Board 0x${b.address.toString(16)}`, version: b.version });
    }
  }

  function triggerCfuFile(): void {
    cfuError = '';
    cfuStagingId = '';
    cfuStagedFilename = '';
    cfuStagedSize = 0;
    if (cfuFileInput) cfuFileInput.value = '';
    cfuFileInput?.click();
  }

  async function handleCfuFileSelected(): Promise<void> {
    if (!cfuFileInput?.files || cfuFileInput.files.length === 0) {
      cfuError = 'No file selected';
      return;
    }
    const file = cfuFileInput.files[0];
    const nameLower = file.name.toLowerCase();
    if (!nameLower.endsWith('.cfu') && !nameLower.endsWith('.cfu.dev')) {
      cfuError = `File must have .cfu or .cfu.dev extension; got "${file.name}"`;
      return;
    }
    cfuError = '';
    cfuUploading = true;
    try {
      const formData = new FormData();
      formData.append('file', file);
      const resp = await fetch(getHttpUrl('/iot/firmware/upload'), { method: 'POST', body: formData });
      const body = await resp.json().catch(() => ({}));
      if (!resp.ok) {
        cfuError = body?.error ?? `Upload failed: HTTP ${resp.status}`;
        return;
      }
      cfuStagingId     = body.stagingId ?? '';
      cfuStagedFilename = body.filename ?? file.name;
      cfuStagedSize    = body.sizeBytes ?? file.size;
      console.log(`[cfu] uploaded staging=${cfuStagingId} file=${cfuStagedFilename} bytes=${cfuStagedSize}`);
    } catch (e) {
      cfuError = `Upload failed: ${(e as Error).message ?? e}`;
    } finally {
      cfuUploading = false;
      if (cfuFileInput) cfuFileInput.value = '';
    }
  }

  async function installCfu(): Promise<void> {
    if (!cfuStagingId) { cfuError = 'No bundle uploaded'; return; }
    cfuError = '';
    cfuInstalling = true;
    cfuProgress = null;
    if (cfuWsClient) { try { cfuWsClient.close(); } catch {} }
    cfuWsClient = new WsClient(getHttpUrl('/iot/ws'), 'firmware-progress', (data: any) => {
      if (data && typeof data === 'object' && data.overallState) {
        cfuProgress = data as CfuInstallProgress;
        if (cfuProgress.overallState === 'done' || cfuProgress.overallState === 'failed') {
          cfuInstalling = false;
          try { cfuWsClient?.close(); } catch {}
          cfuWsClient = undefined;
        }
      }
    });
    try {
      const resp = await fetch(getHttpUrl('/iot/firmware/install'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ stagingId: cfuStagingId, skipReboot: cfuDryRun }),
      });
      const body = await resp.json().catch(() => ({}));
      if (!resp.ok) {
        cfuError = body?.error ?? `Install start failed: HTTP ${resp.status}`;
        cfuInstalling = false;
        return;
      }
      if (body.progress) cfuProgress = body.progress as CfuInstallProgress;
      console.log(`[cfu] install requested, stagingId=${cfuStagingId}`);
    } catch (e) {
      cfuError = `Install start failed: ${(e as Error).message ?? e}`;
      cfuInstalling = false;
    }
  }

  function fmtPct(comp: CfuComponentProgress): string {
    if (!comp.totalSize) return '';
    const pct = (comp.bytesWritten ?? 0) / comp.totalSize * 100;
    return `${pct.toFixed(0)}%`;
  }

  // No-save modal — nothing to flush (firmware install fires imperatively).
  export async function flush(): Promise<void> {}

  onMount(async () => {
    ready = true;
    void refreshOrbitOta();
    orbitOtaTimer = setInterval(() => { void refreshOrbitOta(); }, 30_000);
  });

  onDestroy(() => {
    if (orbitOtaTimer) { clearInterval(orbitOtaTimer); orbitOtaTimer = undefined; }
    if (cfuWsClient) { try { cfuWsClient.close(); } catch {} cfuWsClient = undefined; }
  });
</script>

<div class="pform pform--{theme}">
  <Card class="container-wide mt-2 mb-0 flex flex-col">
    {#if !edit}
      <Table class="mb-0 text-size-xl">
        <Row>
          <Column class="w-1/3 border-r border-gray-400"><div class="py-1">{ $t('level1.version.controller') }</div></Column>
          <Column class="w-2/3"><b>v{version.controller?.[0] ?? ''}</b></Column>
        </Row>
        <Row>
          <Column class="w-1/3 border-r border-gray-400"><div class="py-1">{ $t('level1.version.web-server') }</div></Column>
          <Column class="w-2/3"><b>v{version.webserver}</b></Column>
        </Row>
        <Row>
          <Column class="w-1/3 border-r border-gray-400"><div class="py-1">{ $t('level1.version.user-interface') }</div></Column>
          <Column class="w-2/3"><b>v{version.ui}</b></Column>
        </Row>
        <Row>
          <Column class="w-1/3 border-r border-gray-400"><div class="py-2">{ $t('global.analog-boards') }</div></Column>
          <Column class="w-2/3 max-h-36">
            <div class="h-36 overflow-y-auto" data-touch-interactive
              style="touch-action: pan-y; -ms-touch-action: pan-y; -webkit-overflow-scrolling: touch; overscroll-behavior: contain;">
              {#each boards as board}
                <div>{board.name} - v{board.version}</div>
              {/each}
            </div>
          </Column>
        </Row>
        <Row>
          <Column class="w-1/3 border-r border-gray-400"><div class="py-2">Orbit firmware</div></Column>
          <Column class="w-2/3 max-h-36">
            <div class="h-36 overflow-y-auto" data-touch-interactive
              style="touch-action: pan-y; -ms-touch-action: pan-y; -webkit-overflow-scrolling: touch; overscroll-behavior: contain;">
              {#each orbitOtaRows as orbit}
                <div>
                  {orbit.label} (slot {orbit.slot})
                  {#if orbit.reachable}
                    — v{orbit.version}{orbit.host ? ` @ ${orbit.host}` : ''}
                  {:else}
                    — <span class="opacity-70">offline{orbit.error ? `: ${orbit.error}` : ''}</span>
                  {/if}
                </div>
              {/each}
            </div>
          </Column>
        </Row>
      </Table>
    {:else}
      <Table class="mb-3 text-size-xl border-2 border-primary-500">
        <Row>
          <Column colspan={3} class="font-bold py-1 bg-primary-500/10">Firmware update (.cfu bundle)</Column>
        </Row>
        <Row>
          <Column class="w-1/3 py-1">
            <input type="file" bind:this={cfuFileInput} on:change={handleCfuFileSelected} accept=".cfu,.cfu.dev" class="hidden"/>
            <Button size="sm" on:click={triggerCfuFile} disabled={cfuUploading || cfuInstalling}>
              {cfuUploading ? 'Uploading…' : 'Select .cfu'}
            </Button>
          </Column>
          <Column class="w-1/3 py-1">
            {#if cfuStagedFilename}
              <div class="text-size opacity-80">{cfuStagedFilename}
                <span class="opacity-60">({(cfuStagedSize / 1024).toFixed(1)} KB)</span></div>
            {:else}
              <div class="opacity-50">No bundle uploaded</div>
            {/if}
          </Column>
          <Column class="w-1/3 py-1">
            <label class="flex items-center gap-2 text-size mb-1 opacity-80">
              <input type="checkbox" bind:checked={cfuDryRun} disabled={cfuInstalling || cfuOverallActive}/>
              Dry-run (skip reboot)
            </label>
            <Button size="sm" on:click={installCfu} disabled={!cfuStagingId || cfuInstalling || cfuOverallActive}>
              {cfuInstalling || cfuOverallActive ? 'Installing…' : (cfuDryRun ? 'Install (dry-run)' : 'Install')}
            </Button>
          </Column>
        </Row>
        {#if cfuError}
          <Row><Column colspan={3} class="text-error-500 py-1 text-size">{cfuError}</Column></Row>
        {/if}
        {#if cfuProgress}
          <Row>
            <Column colspan={3} class="py-1">
              <div class="text-size font-bold mb-1">
                v{cfuProgress.bundleVersion} — state: <span class="font-mono">{cfuProgress.overallState}</span>
                {#if cfuProgress.failureReason}<span class="text-error-500">— {cfuProgress.failureReason}</span>{/if}
              </div>
              {#each cfuProgress.components as comp (comp.name)}
                <div class="flex items-baseline gap-2 text-size">
                  <div class="w-28 font-mono">{comp.name}</div>
                  <div class="w-20 opacity-70">{comp.state}</div>
                  {#if comp.totalSize}<div class="w-12 opacity-70">{fmtPct(comp)}</div>{/if}
                  {#if comp.targetIp}<div class="opacity-50">{comp.targetIp}</div>{/if}
                  {#if comp.errorMessage}<div class="text-error-500">{comp.errorMessage}</div>{/if}
                </div>
              {/each}
            </Column>
          </Row>
        {/if}
      </Table>
    {/if}
  </Card>
</div>
