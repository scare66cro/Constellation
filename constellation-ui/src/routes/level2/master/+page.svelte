<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Card from "$lib/ui/Card.svelte";
  import Select from "$lib/ui/Select.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import Button from "$lib/ui/Button.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { onMount, onDestroy } from "svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { masterSlaveSettings } from "$lib/business/protoStores";
  import { useDraft } from "$lib/business/useDraft";
  import { TAG } from "$lib/business/protoTags";

  // Proto-direct page state. Replaces `masterData: string[2]` positional shape.
  // Now also surfaces the per-peer outside-air sync state from the
  // bridge's MasterSlaveSync module via /iot/master-slave/status,
  // polled every 5 s while the page is mounted.
  const ms = useDraft(masterSlaveSettings, TAG.MasterSlaveSettings);
  const { draft, hydrated, live } = ms;

  let level = 2;
  let title = $t('level2.master.master-slave-local-mode-setup');

  $: options = [
    { text: $t('level2.master.local-default'), value: 0 },
    { text: $t('level2.master.master'),        value: 1 },
    { text: $t('level2.master.slave'),         value: 2 },
  ];

  $: ready = false;
  $: wait = false;

  // Master dropdown options for slave mode are filtered to the
  // allowedMasters list — that list is the trust root, the dropdown
  // is just the active selection.
  $: masterOptions = ($draft.allowedMasters ?? []).filter(Boolean).map((ip) => ({
    text: ip,
    value: ip,
  }));

  // Defensive coercion: proto fields default to undefined, which
  // breaks .map / array spread. Always work with concrete arrays.
  function ensureArray(field: 'slaveIps' | 'allowedMasters'): string[] {
    if (!Array.isArray($draft[field])) $draft[field] = [];
    return $draft[field] as string[];
  }

  let newSlaveIp = '';
  let newAllowedMaster = '';
  let discovering = false;
  let discoverMsg = '';

  function addEntry(field: 'slaveIps' | 'allowedMasters', value: string) {
    const v = value.trim();
    if (!v) return;
    const list = ensureArray(field);
    if (list.includes(v)) return;
    $draft[field] = [...list, v];
  }
  function removeEntry(field: 'slaveIps' | 'allowedMasters', ip: string) {
    $draft[field] = ensureArray(field).filter((x) => x !== ip);
  }

  // LAN sweep — bridge probes every host on the local /24, returns
  // every peer reporting `mode==='slave'`. New IPs are appended to
  // the draft list (operator still has to Save). Existing IPs are
  // preserved so a Discover that returns fewer slaves doesn't drop
  // a configured peer that's temporarily offline.
  async function discoverSlaves() {
    discovering = true;
    discoverMsg = '';
    try {
      const res = await fetch(getHttpUrl('/iot/master-slave/discover'));
      if (!res.ok) { discoverMsg = `HTTP ${res.status}`; return; }
      const { peers } = await res.json() as { peers: { ip: string; panelName: string; mode: string }[] };
      const existing = new Set(ensureArray('slaveIps'));
      const added: string[] = [];
      for (const p of peers) {
        if (existing.has(p.ip)) continue;
        existing.add(p.ip);
        added.push(p.ip);
      }
      if (added.length > 0) {
        $draft.slaveIps = [...ensureArray('slaveIps'), ...added];
      }
      discoverMsg = added.length === 0
        ? `${peers.length} slave${peers.length === 1 ? '' : 's'} found, all already listed`
        : `Added ${added.length}: ${added.join(', ')}`;
    } catch (err: any) {
      discoverMsg = err?.message ?? String(err);
    } finally {
      discovering = false;
    }
  }

  // Live status from bridge — refreshed every 5 s, matches firmware
  // broadcast cadence so the UI never feels stale.
  type SlavePeer = {
    ip: string;
    lastAttemptAt: number | null;
    lastSuccessAt: number | null;
    lastError: string | null;
    lastTempX10: number;
    lastHumidX10: number;
  };
  type SyncStatus = {
    mode: 'standalone' | 'master' | 'slave';
    masterIp: string;
    slaveIps: string[];
    allowedMasters: string[];
    slavePeers: SlavePeer[];
    lastMasterSeenAt: number | null;
    lastMasterTempX10: number;
    lastMasterHumidX10: number;
  };
  let status: SyncStatus | null = null;
  let statusTimer: ReturnType<typeof setInterval> | null = null;

  async function refreshStatus() {
    try {
      const res = await fetch(getHttpUrl('/iot/master-slave/status'));
      if (res.ok) status = await res.json();
    } catch { /* network blip; keep last snapshot */ }
  }

  function relSecs(at: number | null): string {
    if (!at) return '—';
    const s = Math.max(0, Math.round((Date.now() - at) / 1000));
    if (s < 60)   return `${s}s`;
    if (s < 3600) return `${Math.round(s / 60)}m`;
    return `${Math.round(s / 3600)}h`;
  }
  function isFresh(at: number | null): boolean {
    return !!at && (Date.now() - at) < 30_000;
  }
  // INT16_MIN sentinel = invalid / unread sensor. Don't mislead the
  // operator by displaying "-3276.8°".
  function fmtTempX10(v: number): string {
    if (v === -32768 || v === 0 || !Number.isFinite(v)) return '—';
    return `${(v / 10).toFixed(1)}°C`;
  }
  function fmtHumidX10(v: number): string {
    if (v === -32768 || v === 0 || !Number.isFinite(v)) return '—';
    return `${(v / 10).toFixed(1)}%`;
  }

  onMount(async () => {
    $navigationStore.isDirty = () => !isEqual($draft, $live);
    ready = true;
    await refreshStatus();
    statusTimer = setInterval(refreshStatus, 5_000);
  });
  onDestroy(() => { if (statusTimer) clearInterval(statusTimer); });
</script>

<GellertPage {wait} {ready} {title} {level} name="master">
  <Card class="text-xl mx-auto flex flex-col w-3/4 mt-2">
    {#if $hydrated}
    <Table class="mb-2 text-size-xl">
      <Row>
        <Column class="border-r border-gray-400">{ $t('global.mode') }:</Column>
        <Column class="px-2"><Select class="w-128" size="xl" {options} bind:value={$draft.mode} edit={true}/></Column>
      </Row>
    </Table>

    {#if $draft.mode === 1}
      <!-- Master view: edit slave IP list + show per-peer push state. -->
      <Card class="mb-2">
        <div class="font-semibold mb-2">{ $t('level2.master.slave-list') }</div>
        {#each ensureArray('slaveIps') as ip (ip)}
          <Row>
            <Column class="px-2 font-mono">{ip}</Column>
            <Column class="px-2 text-sm">
              {#if status}
                {@const peer = status.slavePeers.find((p) => p.ip === ip)}
                {#if peer}
                  <span class:text-green-700={isFresh(peer.lastSuccessAt)}
                        class:text-red-700={!isFresh(peer.lastSuccessAt)}>
                    ● { $t('level2.master.last-ok') }: { relSecs(peer.lastSuccessAt) }
                  </span>
                  {#if peer.lastError}
                    <span class="ml-2 text-red-700">({peer.lastError})</span>
                  {/if}
                {:else}
                  <span class="text-gray-500">{ $t('level2.master.not-yet-pushed') }</span>
                {/if}
              {/if}
            </Column>
            <Column class="px-2">
              <Button size="lg" on:click={() => removeEntry('slaveIps', ip)}>
                { $t('global.remove') }
              </Button>
            </Column>
          </Row>
        {/each}
        <Row>
          <Column colspan={2}>
            <TextField edit={true} bind:value={newSlaveIp} label="10.47.27.x" />
          </Column>
          <Column>
            <Button size="lg" on:click={() => { addEntry('slaveIps', newSlaveIp); newSlaveIp = ''; }}>
              { $t('global.add') }
            </Button>
          </Column>
        </Row>
        <Row>
          <Column colspan={2} class="px-2 text-sm text-gray-700">{ discoverMsg }</Column>
          <Column>
            <Button size="lg" on:click={discoverSlaves} disabled={discovering}>
              { discovering ? $t('global.loading') : $t('level2.master.discover') }
            </Button>
          </Column>
        </Row>
      </Card>
    {:else if $draft.mode === 2}
      <!-- Slave view: edit allowedMasters list + select active master. -->
      <Card class="mb-2">
        <div class="font-semibold mb-2">{ $t('level2.master.allowed-masters') }</div>
        {#each ensureArray('allowedMasters') as ip (ip)}
          <Row>
            <Column class="px-2 font-mono">{ip}</Column>
            <Column class="px-2">
              <Button size="lg" on:click={() => removeEntry('allowedMasters', ip)}>
                { $t('global.remove') }
              </Button>
            </Column>
          </Row>
        {/each}
        <Row>
          <Column>
            <TextField edit={true} bind:value={newAllowedMaster} label="10.47.27.x" />
          </Column>
          <Column>
            <Button size="lg" on:click={() => { addEntry('allowedMasters', newAllowedMaster); newAllowedMaster = ''; }}>
              { $t('global.add') }
            </Button>
          </Column>
        </Row>
      </Card>

      <Table class="mb-2 text-size-xl">
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.master.active-master') }:</Column>
          <Column class="px-2">
            <Select size="xl" extended="w-full" options={masterOptions}
                    bind:value={$draft.masterIp} edit={true}
                    disabled={masterOptions.length === 0}/>
          </Column>
        </Row>
      </Table>

      {#if status}
        <Card class="mb-2 text-base">
          <Row>
            <Column class="border-r border-gray-400">{ $t('level2.master.last-master-seen') }:</Column>
            <Column class="px-2">
              <span class:text-green-700={isFresh(status.lastMasterSeenAt)}
                    class:text-red-700={!isFresh(status.lastMasterSeenAt)}>
                ● { relSecs(status.lastMasterSeenAt) }
              </span>
            </Column>
          </Row>
          <Row>
            <Column class="border-r border-gray-400">{ $t('level2.master.last-temp') }:</Column>
            <Column class="px-2">{ fmtTempX10(status.lastMasterTempX10) }</Column>
          </Row>
          <Row>
            <Column class="border-r border-gray-400">{ $t('level2.master.last-humid') }:</Column>
            <Column class="px-2">{ fmtHumidX10(status.lastMasterHumidX10) }</Column>
          </Row>
        </Card>
      {/if}
    {/if}

    <SaveButton edit={true} bind:wait={wait} data={$draft} original={$live} autoSave onSave={() => ms.save()} />
    {/if}
  </Card>
</GellertPage>
