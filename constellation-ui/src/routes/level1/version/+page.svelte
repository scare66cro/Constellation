<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
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

  // ─── Page state ─────────────────────────────────────────────────────
  // AS2 legacy state (controllerList / displayList / upgradeStatusStore /
  // upgradePhase / etc.) was ripped 2026-05-19. The page now does ONE
  // thing in edit mode — upload + install a `.cfu` firmware bundle — and
  // displays orbit firmware versions in view mode. See
  // memories/repo/cfu-firmware-bundle-design.md for the architecture.

  let title = $t('level1.version.system-software-versions');
  let version: { controller: string[], webserver: string, ui: string } = {
    controller: [], webserver: '', ui: '',
  };
  let boards: { name: string, version: string }[] = [];

  // ─── Orbit OTA — Phase 1A version reporting ─────────────────────────
  // Polls GET /iot/orbit/ota/version?slot=N for each known orbit slot.
  // Backed by lp_ota_task on the LP firmware (TCP :5503). Read-only
  // listing; flash writes for orbits now go through the .cfu install
  // path (POST /iot/firmware/install).
  //
  // Slot→label map matches orbit_role.h on the firmware side. Slot 0 =
  // STORAGE (refrig), 1 = GDC (grain drying), 2 = TRITON (an alternate
  // refrig variant). Future per-customer deploys are handled by the
  // controller's IO Config — see manifest design.
  type OrbitOtaRow = {
    slot: number;
    label: string;
    host: string;
    reachable: boolean;
    version: string;
    error: string;
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
        // 503 (orbit unreachable) is an expected state — surface honestly,
        // don't throw. fetchWithTimeout caps the wait.
        const res = await fetchWithTimeout(
          getHttpUrl(`/iot/orbit/ota/version?slot=${slot}`),
          { method: 'GET' },
          5000,
        );
        const body = await res.json().catch(() => ({}));
        if (res.ok && body?.reachable) {
          return {
            slot, label,
            host: body.host ?? '',
            reachable: true,
            version: body.bankInfo?.bankAVersion ?? '',
            error: '',
          };
        }
        return {
          slot, label,
          host: body?.host ?? '',
          reachable: false,
          version: '',
          error: body?.error ?? `HTTP ${res.status}`,
        };
      } catch (err) {
        return {
          slot, label, host: '', reachable: false, version: '',
          error: (err as Error).message ?? 'fetch failed',
        };
      }
    }));
    orbitOtaRows = next;
  }

  // ─── Constellation Firmware Update (.cfu) — Phase 1B/3 ─────────────
  // New OTA flow that replaces the legacy AS2 .rpi path. Uploads a
  // signed-zip firmware bundle, then triggers a server-side install
  // that pushes per-orbit images to each LP via orbitOtaPush.
  // Backend endpoints: /iot/firmware/upload, /iot/firmware/install,
  // /iot/firmware/status. Progress on WS channel `firmware-progress`.
  type CfuComponentState =
    | 'pending' | 'pushing' | 'rebooting' | 'verifying' | 'done' | 'skipped' | 'failed';
  type CfuComponentProgress = {
    name: string;
    targetIp?: string;
    slot?: number;
    state: CfuComponentState;
    bytesWritten?: number;
    totalSize?: number;
    errorMessage?: string;
  };
  type CfuInstallProgress = {
    bundleVersion: string;
    overallState: 'idle' | 'starting' | 'installing' | 'done' | 'failed';
    failureReason?: string;
    components: CfuComponentProgress[];
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
  // Dry-run mode: streams image bytes to each LP, runs all gates (role,
  // downgrade, sha-recheck, per-chunk CRC, full Bank-B CRC verify) but
  // sends FwActivateBank with reboot=0 — so Bank A stays untouched and
  // the LP keeps running its current firmware. Used to validate the
  // full bridge↔LP push pipeline without committing to a stage-copy.
  // Forwarded to the bridge via /iot/firmware/install { skipReboot: true }.
  let cfuDryRun = false;
  $: cfuOverallActive =
       cfuProgress != null
       && cfuProgress.overallState !== 'done'
       && cfuProgress.overallState !== 'failed';

  // ─── GellertPage props ─────────────────────────────────────────────
  let ready = false;
  let wait = false;
  let waitMessage = '';

  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;

  $: if ($versionInfo) refreshVersion($versionInfo);

  function refreshVersion(info: {
    armVersion: string;
    bootloaderVersion: string;
    boards: { address: number; version: string; type: number }[];
  }) {
    boards = [];
    // Legacy template renders `v{version.controller[0]}`; bridge emits
    // `"<arm>,<bootloader>"` so preserve the same display format.
    const head = info.bootloaderVersion
      ? `${info.armVersion},${info.bootloaderVersion}`
      : info.armVersion;
    version = {
      controller: [head],
      webserver: '1.0.0-bridge',
      ui: uiversion,
    };
    for (const b of info.boards ?? []) {
      boards.push({ name: `Board 0x${b.address.toString(16)}`, version: b.version });
    }
  }

  function retryLoadVersion() {
    wait = false;
    ready = true;
    cfuError = '';
  }

  // ─── .cfu firmware bundle handlers ─────────────────────────────────

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
      const resp = await fetch(getHttpUrl('/iot/firmware/upload'), {
        method: 'POST',
        body: formData,
      });
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
    if (!cfuStagingId) {
      cfuError = 'No bundle uploaded';
      return;
    }
    cfuError = '';
    cfuInstalling = true;
    cfuProgress = null;

    if (cfuWsClient) {
      try { cfuWsClient.close(); } catch {}
    }
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
      // Server returned initial snapshot — show it immediately while we
      // wait for WS pushes to take over.
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

  // ─── Lifecycle ──────────────────────────────────────────────────────
  onMount(async () => {
    ready = true;
    // Kick off orbit OTA polling (Phase 1A version reporting). Cheap —
    // one TCP probe per orbit per cycle, 30 s cadence is plenty.
    void refreshOrbitOta();
    orbitOtaTimer = setInterval(() => { void refreshOrbitOta(); }, 30_000);
  });

  onDestroy(() => {
    if (orbitOtaTimer) {
      clearInterval(orbitOtaTimer);
      orbitOtaTimer = undefined;
    }
    if (cfuWsClient) {
      try { cfuWsClient.close(); } catch {}
      cfuWsClient = undefined;
    }
  });
</script>

<GellertPage {ready} {title} {level} name="version" {wait} {waitMessage} retryCallback={retryLoadVersion}>
  <Card class="container-wide mt-2 mb-0 flex flex-col">
    {#if !edit}
      <!-- ─── View mode: version info + orbit firmware status ──────── -->
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
            <div class="h-36 overflow-y-auto"
              data-touch-interactive
              style="touch-action: pan-y; -ms-touch-action: pan-y; -webkit-overflow-scrolling: touch; overscroll-behavior: contain;"
            >
              {#each boards as board}
                <div>
                  {board.name} - v{board.version}
                </div>
              {/each}
            </div>
          </Column>
        </Row>
        <Row>
          <Column class="w-1/3 border-r border-gray-400"><div class="py-2">Orbit firmware</div></Column>
          <Column class="w-2/3 max-h-36">
            <div class="h-36 overflow-y-auto"
              data-touch-interactive
              style="touch-action: pan-y; -ms-touch-action: pan-y; -webkit-overflow-scrolling: touch; overscroll-behavior: contain;"
            >
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
      <!-- ─── Edit mode: Constellation Firmware Update (.cfu) ──────── -->
      <Table class="mb-3 text-size-xl border-2 border-primary-500">
        <Row>
          <Column colspan={3} class="font-bold py-1 bg-primary-500/10">
            Firmware update (.cfu bundle)
          </Column>
        </Row>
        <Row>
          <Column class="w-1/3 py-1">
            <input
              type="file"
              bind:this={cfuFileInput}
              on:change={handleCfuFileSelected}
              accept=".cfu,.cfu.dev"
              class="hidden"
            />
            <Button size="sm" on:click={triggerCfuFile} disabled={cfuUploading || cfuInstalling}>
              {cfuUploading ? 'Uploading…' : 'Select .cfu'}
            </Button>
          </Column>
          <Column class="w-1/3 py-1">
            {#if cfuStagedFilename}
              <div class="text-size opacity-80">
                {cfuStagedFilename}
                <span class="opacity-60">({(cfuStagedSize / 1024).toFixed(1)} KB)</span>
              </div>
            {:else}
              <div class="opacity-50">No bundle uploaded</div>
            {/if}
          </Column>
          <Column class="w-1/3 py-1">
            <label class="flex items-center gap-2 text-size mb-1 opacity-80">
              <input
                type="checkbox"
                bind:checked={cfuDryRun}
                disabled={cfuInstalling || cfuOverallActive}
              />
              Dry-run (skip reboot)
            </label>
            <Button
              size="sm"
              on:click={installCfu}
              disabled={!cfuStagingId || cfuInstalling || cfuOverallActive}>
              {cfuInstalling || cfuOverallActive
                ? 'Installing…'
                : (cfuDryRun ? 'Install (dry-run)' : 'Install')}
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
                v{cfuProgress.bundleVersion}
                — state: <span class="font-mono">{cfuProgress.overallState}</span>
                {#if cfuProgress.failureReason}
                  <span class="text-error-500">— {cfuProgress.failureReason}</span>
                {/if}
              </div>
              {#each cfuProgress.components as comp (comp.name)}
                <div class="flex items-baseline gap-2 text-size">
                  <div class="w-28 font-mono">{comp.name}</div>
                  <div class="w-20 opacity-70">{comp.state}</div>
                  {#if comp.totalSize}
                    <div class="w-12 opacity-70">{fmtPct(comp)}</div>
                  {/if}
                  {#if comp.targetIp}
                    <div class="opacity-50">{comp.targetIp}</div>
                  {/if}
                  {#if comp.errorMessage}
                    <div class="text-error-500">{comp.errorMessage}</div>
                  {/if}
                </div>
              {/each}
            </Column>
          </Row>
        {/if}
      </Table>
    {/if}
  </Card>
</GellertPage>
