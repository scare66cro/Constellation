<script lang="ts">
  // Remote Systems Setup Page
  //
  // Data path: REST /iot/remote-systems (UUID-keyed, DHCP-resilient,
  // bridge-managed at constellation-ui/server/src/remoteSystemsSync.ts).
  // Replaces the legacy `tcpip-data` WebSocket / `P2NodeSetupData` CSV
  // path that had no concept of persistent identity and silently broke
  // on DHCP changes or panel renames.

  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import Button from "$lib/ui/Button.svelte";
  import List from "$lib/ui/List.svelte";
  import Card from "$lib/ui/Card.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { onMount, onDestroy } from "svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { systemGroupsStore, loadGroupsFromServer, saveGroupsToServer, type SystemGroup } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { t } from "svelte-i18n";

  // ── Types matching the bridge response ────────────────────────────
  interface RemoteSystem {
    id: string;
    name: string;
    host: string;
    port: number;
    novaId: string;
    panelName: string;
    online: boolean;
    lastAttemptAt:       number | null;
    lastSuccessAt:       number | null;
    lastError:           string | null;
    consecutiveFailures: number;
    healedFromHost:      string | null;
  }
  interface DiscoveryCandidate {
    host: string;
    port: number;
    novaId: string;
    panelName: string;
  }

  let title = $t('level2.remote.remote-system-access-setup');
  let wait = false;
  let ready = false;

  // ── Remote systems list (polled) ──────────────────────────────────
  let systems: RemoteSystem[] = [];
  let pollHandle: ReturnType<typeof setInterval> | null = null;

  async function refreshSystems() {
    try {
      const res = await fetch(getHttpUrl('/iot/remote-systems'));
      if (!res.ok) return;
      const data = await res.json();
      if (Array.isArray(data?.systems)) systems = data.systems;
    } catch { /* keep last good */ }
  }

  // Identifier used for group membership: novaId when known (DHCP-
  // resilient), otherwise the bridge entry id (still stable).
  function systemIdentifier(s: RemoteSystem): string {
    return s.novaId || s.id;
  }

  // ── Manual add ────────────────────────────────────────────────────
  let newHost = '';
  let newName = '';
  let addError = '';
  async function addManual() {
    if (!newHost.trim()) return;
    addError = '';
    wait = true;
    try {
      const res = await fetch(getHttpUrl('/iot/remote-systems'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ host: newHost.trim(), name: newName.trim() || undefined }),
      });
      const j = await res.json();
      if (!res.ok || j?.error) {
        addError = j?.error ?? `HTTP ${res.status}`;
      } else {
        newHost = '';
        newName = '';
        await refreshSystems();
      }
    } catch (e) {
      addError = String(e);
    } finally {
      wait = false;
    }
  }

  async function removeSystem(id: string) {
    wait = true;
    try {
      await fetch(getHttpUrl(`/iot/remote-systems/${encodeURIComponent(id)}`), { method: 'DELETE' });
      await refreshSystems();
    } finally { wait = false; }
  }

  // Inline rename
  let renameId = '';
  let renameValue = '';
  function startRename(s: RemoteSystem) {
    renameId = s.id;
    renameValue = s.name;
  }
  async function commitRename() {
    if (!renameId || !renameValue.trim()) { renameId = ''; return; }
    wait = true;
    try {
      await fetch(getHttpUrl(`/iot/remote-systems/${encodeURIComponent(renameId)}`), {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: renameValue.trim() }),
      });
      renameId = '';
      await refreshSystems();
    } finally { wait = false; }
  }

  // ── Discovery sweep ────────────────────────────────────────────────
  let candidates: DiscoveryCandidate[] = [];
  let discoverState: 'idle' | 'searching' | 'done' = 'idle';
  async function discover() {
    discoverState = 'searching';
    candidates = [];
    try {
      const res = await fetch(getHttpUrl('/iot/remote-systems/discover'));
      if (res.ok) {
        const j = await res.json();
        if (Array.isArray(j?.candidates)) candidates = j.candidates;
      }
    } finally {
      discoverState = 'done';
    }
  }

  async function adoptCandidate(c: DiscoveryCandidate) {
    wait = true;
    try {
      await fetch(getHttpUrl('/iot/remote-systems'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ host: c.host, port: c.port, name: c.panelName }),
      });
      candidates = candidates.filter(x => x.novaId !== c.novaId);
      await refreshSystems();
    } finally { wait = false; }
  }

  // ── Status formatting helpers ─────────────────────────────────────
  function statusText(s: RemoteSystem): string {
    if (s.online) return $t('level2.remote.online');
    if (s.consecutiveFailures > 0) return `${$t('level2.remote.offline')} (${s.consecutiveFailures})`;
    return $t('level2.remote.offline');
  }
  function statusClass(s: RemoteSystem): string {
    return s.online ? 'text-green-600' : 'text-red-600';
  }
  function timeAgo(ts: number | null): string {
    if (!ts) return '—';
    const sec = Math.floor((Date.now() - ts) / 1000);
    if (sec < 5) return $t('level2.remote.just-now');
    if (sec < 60) return `${sec}s`;
    if (sec < 3600) return `${Math.floor(sec / 60)}m`;
    return `${Math.floor(sec / 3600)}h`;
  }

  // ── Groups (unchanged behaviour) ──────────────────────────────────
  let newGroupName = '';
  let editGroupId = '';
  let selectedAvailable = '';
  let selectedInGroup = '';

  $: groups = $systemGroupsStore.groups ?? [];
  $: selectedGroupId = $systemGroupsStore.selectedGroupId;

  $: groupSelectOptions = [
    { text: $t('level2.remote.all-systems'), value: '__all__' },
    ...groups.map((g: SystemGroup) => ({ text: g.name, value: g.id }))
  ];
  $: activeGroupValue = selectedGroupId ?? '__all__';

  $: editGroup = groups.find((g: SystemGroup) => g.id === editGroupId) ?? null;
  $: editGroupSelectOptions = groups.map((g: SystemGroup) => ({ text: g.name, value: g.id }));

  // Build the (text,value) options for available / in-group lists
  // straight from the live remote-systems list.
  $: allSystemsOptions = systems.map((s) => ({
    text: `${s.name}${s.panelName && s.panelName !== s.name ? ' \u2014 ' + s.panelName : ''} (${s.host}${s.port !== 9001 ? ':' + s.port : ''})`,
    value: systemIdentifier(s),
  }));
  $: editGroupSystems = editGroup ? editGroup.systems : [];
  $: availableSystems = editGroup
    ? allSystemsOptions.filter((s) => !editGroupSystems.includes(s.value))
    : [];
  $: groupSystemOptions = editGroup
    ? allSystemsOptions.filter((s) => editGroupSystems.includes(s.value))
    : [];

  function createGroup() {
    const trimmed = newGroupName.trim();
    if (!trimmed) return;
    const id = `grp_${Date.now()}`;
    systemGroupsStore.update((state) => ({
      ...state,
      groups: [...state.groups, { id, name: trimmed, systems: [] }],
    }));
    newGroupName = '';
    editGroupId = id;
  }
  function deleteGroup() {
    if (!editGroupId) return;
    systemGroupsStore.update((state) => ({
      ...state,
      groups: state.groups.filter((g: SystemGroup) => g.id !== editGroupId),
      selectedGroupId: state.selectedGroupId === editGroupId ? null : state.selectedGroupId,
    }));
    editGroupId = '';
  }
  function setActiveGroup() {
    const val = activeGroupValue === '__all__' ? null : activeGroupValue;
    systemGroupsStore.update((state) => ({ ...state, selectedGroupId: val }));
  }
  function addToGroup() {
    if (!editGroupId || !selectedAvailable) return;
    systemGroupsStore.update((state) => ({
      ...state,
      groups: state.groups.map((g: SystemGroup) =>
        g.id === editGroupId && !g.systems.includes(selectedAvailable)
          ? { ...g, systems: [...g.systems, selectedAvailable] }
          : g),
    }));
    selectedAvailable = '';
  }
  function removeFromGroup() {
    if (!editGroupId || !selectedInGroup) return;
    systemGroupsStore.update((state) => ({
      ...state,
      groups: state.groups.map((g: SystemGroup) =>
        g.id === editGroupId
          ? { ...g, systems: g.systems.filter((s: string) => s !== selectedInGroup) }
          : g),
    }));
    selectedInGroup = '';
  }

  // ── Lifecycle ─────────────────────────────────────────────────────
  let unsubGroups: (() => void) | null = null;
  onMount(() => {
    ready = true;
    void refreshSystems();
    pollHandle = setInterval(refreshSystems, 5000);
    loadGroupsFromServer();
    unsubGroups = systemGroupsStore.subscribe((state) => {
      saveGroupsToServer(state);
    });
  });
  onDestroy(() => {
    if (pollHandle) { clearInterval(pollHandle); pollHandle = null; }
    if (unsubGroups) { unsubGroups(); unsubGroups = null; }
  });
</script>

<GellertPage {wait} {ready} {title} level={2} name="remote">
  <ScrollableArea>

  <!-- Active Group Selection -->
  <Card class="mx-auto flex flex-col w-full md:w-3/4 mt-1">
    <Table class="text-size-large">
      <Row>
        <Column class="w-3/6 !py-0.5 border-r border-gray-400 font-bold">{ $t('level2.remote.active-group') }</Column>
        <Column class="w-3/6 !py-0.5">
          <Select class="w-full" size="lg" options={groupSelectOptions} bind:value={activeGroupValue} edit={true} on:change={setActiveGroup} />
        </Column>
      </Row>
    </Table>
  </Card>

  <!-- Defined Remote Systems -->
  <Card class="mx-auto flex flex-col w-full md:w-3/4 mt-1">
    <Table class="text-size-large">
      <Row>
        <Column class="w-full !py-0.5 font-bold" colspan={5}>{ $t('level2.remote.defined-controllers') }</Column>
      </Row>
      <Row>
        <Column class="w-3/12 !py-0.5 border-r border-gray-400 font-semibold">{ $t('level2.remote.controller-name') }</Column>
        <Column class="w-3/12 !py-0.5 border-r border-gray-400 font-semibold">{ $t('level2.remote.address-port') }</Column>
        <Column class="w-2/12 !py-0.5 border-r border-gray-400 font-semibold">{ $t('level2.remote.status') }</Column>
        <Column class="w-2/12 !py-0.5 border-r border-gray-400 font-semibold">{ $t('level2.remote.last-seen') }</Column>
        <Column class="w-2/12 !py-0.5 font-semibold text-center">{ $t('global.actions') }</Column>
      </Row>
      {#if systems.length === 0}
        <Row>
          <Column class="w-full !py-1 text-gray-500" colspan={5}>{ $t('level2.remote.no-remote-systems') }</Column>
        </Row>
      {/if}
      {#each systems as s (s.id)}
        <Row>
          <Column class="w-3/12 !py-0.5 border-r border-gray-400">
            {#if renameId === s.id}
              <TextField class="w-full" size="md" extended="w-full" bind:value={renameValue} edit={true} keyboardType={KeyboardTypes.Alpha} on:blur={commitRename} />
            {:else}
              <button type="button" class="text-left underline-offset-2 hover:underline w-full" on:click={() => startRename(s)}>{s.name}</button>
              {#if s.panelName && s.panelName !== s.name}
                <div class="text-xs text-gray-500">{s.panelName}</div>
              {/if}
            {/if}
          </Column>
          <Column class="w-3/12 !py-0.5 border-r border-gray-400">
            {s.host}{s.port !== 9001 ? ':' + s.port : ''}
            {#if s.healedFromHost}
              <div class="text-xs text-blue-600">{ $t('level2.remote.healed-from') } {s.healedFromHost}</div>
            {/if}
          </Column>
          <Column class="w-2/12 !py-0.5 border-r border-gray-400 {statusClass(s)}">{statusText(s)}</Column>
          <Column class="w-2/12 !py-0.5 border-r border-gray-400">{timeAgo(s.lastSuccessAt)}</Column>
          <Column class="w-2/12 !py-0.5 text-center">
            <Button size="md" class="w-24" on:click={() => removeSystem(s.id)}>{ $t('global.delete') }</Button>
          </Column>
        </Row>
      {/each}
    </Table>
  </Card>

  <!-- Add a system -->
  <Card class="mx-auto flex flex-col w-full md:w-3/4 mt-1">
    <Table class="text-size-large">
      <Row>
        <Column class="w-full !py-0.5 font-bold" colspan={5}>{ $t('level2.remote.add-system') }</Column>
      </Row>
      <Row>
        <Column class="w-2/12 !py-0.5 border-r border-gray-400">{ $t('level2.remote.address-port') }</Column>
        <Column class="w-3/12 !py-0.5 border-r border-gray-400 px-2"><TextField class="w-full" size="lg" extended="w-full" bind:value={newHost} edit={true} keyboardType={KeyboardTypes.Numeric} /></Column>
        <Column class="w-2/12 !py-0.5 border-r border-gray-400">{ $t('level2.remote.controller-name') }</Column>
        <Column class="w-3/12 !py-0.5 border-r border-gray-400 px-2"><TextField class="w-full" size="lg" extended="w-full" bind:value={newName} edit={true} keyboardType={KeyboardTypes.Alpha} /></Column>
        <Column class="w-2/12 !py-0.5 text-center"><Button size="lg" class="w-28" on:click={addManual}>{ $t('global.add') }</Button></Column>
      </Row>
      {#if addError}
        <Row><Column class="w-full !py-0.5 text-red-600" colspan={5}>{addError}</Column></Row>
      {/if}
      <Row>
        <Column class="w-5/6 !py-0.5 border-r border-gray-400" colspan={4}>{ $t('global.discover-agri-star-system-controllers') }</Column>
        <Column class="w-1/6 !py-0.5 text-center"><Button size="lg" class="w-24 3xl:w-48" on:click={discover}>{ $t('global.find') }</Button></Column>
      </Row>
      {#if discoverState === 'searching'}
        <Row><Column class="w-full !py-0.5 text-gray-500" colspan={5}>{ $t('level2.remote.searching') }</Column></Row>
      {:else if discoverState === 'done' && candidates.length === 0}
        <Row><Column class="w-full !py-0.5 text-gray-500" colspan={5}>{ $t('level2.remote.no-candidates') }</Column></Row>
      {/if}
      {#each candidates as c (c.novaId)}
        <Row>
          <Column class="w-5/6 !py-0.5 border-r border-gray-400" colspan={4}>
            <strong>{c.panelName || '(unnamed)'}</strong> &mdash; {c.host}{c.port !== 9001 ? ':' + c.port : ''}
            <div class="text-xs text-gray-500 font-mono">{c.novaId}</div>
          </Column>
          <Column class="w-1/6 !py-0.5 text-center"><Button size="md" class="w-24" on:click={() => adoptCandidate(c)}>{ $t('global.add') }</Button></Column>
        </Row>
      {/each}
    </Table>
  </Card>

  <!-- Group Management -->
  <Card class="mx-auto flex flex-col w-full md:w-3/4 mt-1">
    <Table class="text-size-large">
      <Row>
        <Column class="w-full !py-0.5 font-bold" colspan={3}>{ $t('level2.remote.groups') }</Column>
      </Row>
      <Row>
        <Column class="w-2/6 !py-0.5 border-r border-gray-400">{ $t('level2.remote.new-group-name') }</Column>
        <Column class="w-3/6 !py-0.5 px-2 border-r border-gray-400"><TextField class="w-full" size="lg" extended="w-full" bind:value={newGroupName} edit={true} keyboardType={KeyboardTypes.Alpha} /></Column>
        <Column class="w-1/6 !py-0.5"><Button size="lg" class="w-28 3xl:w-48" on:click={createGroup}>{ $t('level2.remote.create-group') }</Button></Column>
      </Row>
      <Row>
        <Column class="w-2/6 !py-0.5 border-r border-gray-400">{ $t('level2.remote.edit-group') }</Column>
        <Column class="w-3/6 !py-0.5 border-r border-gray-400">
          {#if groups.length > 0}
            <Select class="w-full" size="lg" options={editGroupSelectOptions} bind:value={editGroupId} edit={true} />
          {:else}
            <span class="text-gray-500 px-2">{ $t('level2.remote.no-groups') }</span>
          {/if}
        </Column>
        <Column class="w-1/6 !py-0.5">
          {#if editGroupId}
            <Button size="lg" class="w-28 3xl:w-48" on:click={deleteGroup}>{ $t('global.delete') }</Button>
          {/if}
        </Column>
      </Row>
    </Table>

    {#if editGroup}
      <Table class="text-size-large mt-1">
        <Row>
          <Column class="w-full !py-0.5 font-bold" colspan={3}>
            { $t('level2.remote.assign-systems-to') } "{editGroup.name}"
          </Column>
        </Row>
        <Row>
          <Column class="w-5/12 !py-0.5">
            <div class="font-semibold mb-1">{ $t('level2.remote.available-systems') }</div>
            <List class="w-full text-size-large" bind:value={selectedAvailable} options={availableSystems} edit={true} size={3} />
          </Column>
          <Column class="w-2/12 !py-0.5">
            <div class="flex flex-col items-center justify-center gap-1 h-full">
              <Button size="lg" class="w-14" on:click={addToGroup}>&rarr;</Button>
              <Button size="lg" class="w-14" on:click={removeFromGroup}>&larr;</Button>
            </div>
          </Column>
          <Column class="w-5/12 !py-0.5">
            <div class="font-semibold mb-1">{ $t('level2.remote.in-group') }</div>
            <List class="w-full text-size-large" bind:value={selectedInGroup} options={groupSystemOptions} edit={true} size={3} />
          </Column>
        </Row>
      </Table>
    {/if}
  </Card>

  </ScrollableArea>
</GellertPage>
