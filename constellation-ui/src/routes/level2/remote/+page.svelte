<script lang="ts">
  // Remote Systems Setup Page
  // Discovery + Group Management
  // Network data flows: Backend → GellertHeader (tcpip-data WebSocket) → navigationStore.nodes → this page
  
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
  import { onMount } from "svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { navigationStore, systemGroupsStore, loadGroupsFromServer, saveGroupsToServer, type SystemGroup, isNodeInGroup, getNodeIdentifier } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { t } from "svelte-i18n";

  let title = $t('level2.remote.remote-system-access-setup');
  let wait = false;
  let ready = false;
  let newGroupName = '';

  
  // Reactive: all known systems from WebSocket discovery
  // value is the persistent identifier (novaId or IP:port fallback)
  $: allSystems = $navigationStore.nodes.map((node) => ({ 
    text: `${node.text} (${node.value})`, 
    value: getNodeIdentifier(node)
  }));

  // Groups from persisted store
  $: groups = $systemGroupsStore.groups ?? [];
  $: selectedGroupId = $systemGroupsStore.selectedGroupId;

  // Group options for the active group selector (includes "All Systems")
  $: groupSelectOptions = [
    { text: $t('level2.remote.all-systems'), value: '__all__' },
    ...groups.map((g: SystemGroup) => ({ text: g.name, value: g.id }))
  ];
  $: activeGroupValue = selectedGroupId ?? '__all__';

  // Which group is selected for editing in the group management section
  let editGroupId = '';
  $: editGroup = groups.find((g: SystemGroup) => g.id === editGroupId) ?? null;
  $: editGroupSelectOptions = groups.map((g: SystemGroup) => ({ text: g.name, value: g.id }));

  // Systems in the currently-edited group
  $: editGroupSystems = editGroup ? editGroup.systems : [];

  // Systems NOT in the currently-edited group (available to add)
  $: availableSystems = editGroup
    ? allSystems.filter(s => !editGroupSystems.includes(s.value))
    : [];

  // Systems IN the currently-edited group (for display)
  $: groupSystemOptions = editGroup
    ? allSystems.filter(s => editGroupSystems.includes(s.value))
    : [];

  // Selected items in each list for add/remove operations
  let selectedAvailable = '';
  let selectedInGroup = '';

  // ── Discovery ──
  async function find() {
    wait = true;
    await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ tag: 'findNodes' }),
    });
    wait = false;
  }

  // ── Group CRUD ──
  function createGroup() {
    const trimmed = newGroupName.trim();
    if (!trimmed) return;
    const id = `grp_${Date.now()}`;
    systemGroupsStore.update((state: { groups: SystemGroup[]; selectedGroupId: string | null }) => ({
      ...state,
      groups: [...state.groups, { id, name: trimmed, systems: [] }]
    }));
    newGroupName = '';
    editGroupId = id;
  }

  function deleteGroup() {
    if (!editGroupId) return;
    systemGroupsStore.update((state: { groups: SystemGroup[]; selectedGroupId: string | null }) => ({
      ...state,
      groups: state.groups.filter((g: SystemGroup) => g.id !== editGroupId),
      // If the deleted group was selected, reset to all
      selectedGroupId: state.selectedGroupId === editGroupId ? null : state.selectedGroupId
    }));
    editGroupId = '';
  }

  function setActiveGroup() {
    const val = activeGroupValue === '__all__' ? null : activeGroupValue;
    systemGroupsStore.update((state: { groups: SystemGroup[]; selectedGroupId: string | null }) => ({ ...state, selectedGroupId: val }));
  }

  // ── System assignment ──
  function addToGroup() {
    if (!editGroupId || !selectedAvailable) return;
    systemGroupsStore.update((state: { groups: SystemGroup[]; selectedGroupId: string | null }) => ({
      ...state,
      groups: state.groups.map((g: SystemGroup) =>
        g.id === editGroupId && !g.systems.includes(selectedAvailable)
          ? { ...g, systems: [...g.systems, selectedAvailable] }
          : g
      )
    }));
    selectedAvailable = '';
  }

  function removeFromGroup() {
    if (!editGroupId || !selectedInGroup) return;
    systemGroupsStore.update((state: { groups: SystemGroup[]; selectedGroupId: string | null }) => ({
      ...state,
      groups: state.groups.map((g: SystemGroup) =>
        g.id === editGroupId
          ? { ...g, systems: g.systems.filter((s: string) => s !== selectedInGroup) }
          : g
      )
    }));
    selectedInGroup = '';
  }

  onMount(() => {
    ready = true;
    // Load groups from server (server wins over stale localStorage)
    loadGroupsFromServer();
    // Auto-save groups to server whenever the store changes
    const unsub = systemGroupsStore.subscribe((state: { groups: SystemGroup[]; selectedGroupId: string | null }) => {
      saveGroupsToServer(state);
    });
    return unsub;
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

  <!-- Group Management -->
  <Card class="mx-auto flex flex-col w-full md:w-3/4 mt-1">
    <Table class="text-size-large">
      <Row>
        <Column class="w-full !py-0.5 font-bold" colspan={3}>{ $t('level2.remote.groups') }</Column>
      </Row>
      <!-- Create group -->
      <Row>
        <Column class="w-2/6 !py-0.5 border-r border-gray-400">{ $t('level2.remote.new-group-name') }</Column>
        <Column class="w-3/6 !py-0.5 px-2 border-r border-gray-400"><TextField class="w-full" size="lg" extended="w-full" bind:value={newGroupName} edit={true} keyboardType={KeyboardTypes.Alpha} /></Column>
        <Column class="w-1/6 !py-0.5"><Button size="lg" class="w-28 3xl:w-48" on:click={createGroup}>{ $t('level2.remote.create-group') }</Button></Column>
      </Row>
      <!-- Select group to edit -->
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
      <!-- Find / scan for controllers -->
      <Row>
        <Column class="w-5/6 !py-0.5 border-r border-gray-400">{ $t('global.discover-agri-star-system-controllers') }</Column>
        <Column class="w-1/6 !py-0.5"><Button size="lg" class="w-24 3xl:w-48" on:click={find}>{ $t('global.find') }</Button></Column>
      </Row>
    </Table>

    <!-- System assignment (only if a group is selected for editing) -->
    {#if editGroup}
      <Table class="text-size-large mt-1">
        <Row>
          <Column class="w-full !py-0.5 font-bold" colspan={3}>
            { $t('level2.remote.assign-systems-to') } "{editGroup.name}"
          </Column>
        </Row>
        <Row>
          <!-- Available systems -->
          <Column class="w-5/12 !py-0.5">
            <div class="font-semibold mb-1">{ $t('level2.remote.available-systems') }</div>
            <List class="w-full text-size-large" bind:value={selectedAvailable} options={availableSystems} edit={true} size={3} />
          </Column>
          <!-- Add/Remove buttons -->
          <Column class="w-2/12 !py-0.5">
            <div class="flex flex-col items-center justify-center gap-1 h-full">
              <Button size="lg" class="w-14" on:click={addToGroup}>&rarr;</Button>
              <Button size="lg" class="w-14" on:click={removeFromGroup}>&larr;</Button>
            </div>
          </Column>
          <!-- Systems in group -->
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



