<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Button from "$lib/ui/Button.svelte";
  import Card from "$lib/ui/Card.svelte";
  import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { onMount } from "svelte";
  import { keysStore, navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import CryptoJS from "crypto-js";
  import { checkKeys, safeJsonParse, type ArrayResponse } from "$lib/business/util";
  import { t } from "svelte-i18n";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import { accountSettings } from "$lib/business/protoStores";
  import { writeProtoRaw, buildForceVarintBytes, wrapAsLengthDelim } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // data.meta  = { factory, slots[], cloudLinks[], currentSession } from
  // /iot/accounts-meta. Username list (formerly data.array) now derives
  // from $accountSettings.users (proto envelope tag 34). Bridge GET
  // /iot/accounts is no longer consumed.
  export let data: { meta?: any };

  let title = `${$t('level2.accounts.set-level')} 1 ${$t('level2.accounts.user-accounts')}`;
  let edit = true;

  $: showPasswords = false;
  $: error = false;
  $: ready = false;
  $: wait = false;
  $: users = [] as string[];
  // hasLevel1Password is a property of the proto store (passwordDefined)
  // not of the username array; derive directly when accountSettings arrives.
  $: $keysStore.hasLevel1Password = !!$accountSettings?.passwordDefined;
  $: passwords = Array(10).fill('');

  // ── Role / metadata state (pass 1: local only) ──
  type SlotRole = 'disabled' | 'operator' | 'admin';
  interface SlotMeta {
    index: number;
    username: string;
    role: SlotRole;
    lastLogin: string | null;
    lastLoginIp: string | null;
    loginCount: number;
  }
  let slotMeta: SlotMeta[] = [];
  let factoryMeta: { lastLogin: string | null; loginCount: number } | null = null;
  let currentSession = { actor: 'anonymous', slot: null as number | null, level: 0 };

  // Audit log
  interface AuditEntry {
    ts: string;
    kind: 'login' | 'logout' | 'login_fail' | 'save' | 'level_change';
    actor: string;
    slot: number | null;
    level: number;
    route?: string;
    detail?: string;
    ip?: string;
  }
  let auditEntries: AuditEntry[] = [];
  let auditLoading = false;

  // ── Cloud identity state (pass 2) ──
  interface CloudLinkView {
    cloudUserId: string;
    username: string;
    displayName: string;
    role: SlotRole;
    slot: number | null;
    linkedAt: string;
    lastRemoteLogin: string | null;
  }
  let cloudLinks: CloudLinkView[] = [];
  let cloudLoading = false;
  let showLinkForm = false;
  let linkUsername = '';
  let linkPassword = '';
  let linkSlot: string = '-1';            // '-1' = cloud-only
  let linkRole: SlotRole = 'admin';
  let linkBusy = false;
  let linkError = '';

  const roleOptions = [
    { text: 'Disabled',  value: 'disabled' },
    { text: 'Operator',  value: 'operator' },
    { text: 'Admin',     value: 'admin' },
  ];

  function applyMeta(meta: any): void {
    if (!meta) return;
    slotMeta = (meta.slots ?? []) as SlotMeta[];
    factoryMeta = meta.factory ?? null;
    currentSession = meta.currentSession ?? currentSession;
  }

  function fmtTs(iso: string | null): string {
    if (!iso) return 'never';
    try { return new Date(iso).toLocaleString(); } catch { return iso; }
  }

  async function changeRole(slot: number, role: SlotRole) {
    try {
      const resp = await fetch(getHttpUrl('/iot/accounts-meta'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ slot, role }),
      });
      if (!resp.ok) {
        console.error('Role update failed', resp.status);
        return;
      }
      // Refresh meta so the UI reflects whatever the server now holds.
      const metaResp = await fetch(getHttpUrl('/iot/accounts-meta'));
      const metaJson = await safeJsonParse(metaResp);
      applyMeta(metaJson);
    } catch (e) {
      console.error('Role update error', e);
    }
  }

  async function onRoleChange(slot: number, event: Event) {
    const val = (event.target as HTMLSelectElement).value as SlotRole;
    await changeRole(slot, val);
  }

  async function refreshAudit() {
    auditLoading = true;
    try {
      const resp = await fetch(getHttpUrl('/iot/audit?limit=50'));
      const j = await safeJsonParse(resp);
      auditEntries = (j?.entries ?? []) as AuditEntry[];
    } catch (e) {
      console.error('Audit fetch failed', e);
      auditEntries = [];
    } finally {
      auditLoading = false;
    }
  }

  async function refreshCloudLinks() {
    cloudLoading = true;
    try {
      const resp = await fetch(getHttpUrl('/iot/cloud/links'));
      const j = await safeJsonParse(resp);
      cloudLinks = (j?.links ?? []) as CloudLinkView[];
    } catch (e) {
      console.error('Cloud links fetch failed', e);
      cloudLinks = [];
    } finally {
      cloudLoading = false;
    }
  }

  async function submitLink() {
    if (!linkUsername || !linkPassword) {
      linkError = 'Username and password required';
      return;
    }
    linkBusy = true;
    linkError = '';
    try {
      const slotNum = parseInt(linkSlot, 10);
      const body = {
        username: linkUsername,
        password: linkPassword,
        role: linkRole,
        ...(slotNum >= 0 && slotNum <= 9 ? { slot: slotNum } : {}),
      };
      const resp = await fetch(getHttpUrl('/iot/cloud/link'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });
      const j = await safeJsonParse(resp);
      if (!resp.ok) {
        linkError = j?.error ?? `Failed (${resp.status})`;
        return;
      }
      // Reset + close
      linkUsername = '';
      linkPassword = '';
      linkSlot = '-1';
      linkRole = 'admin';
      showLinkForm = false;
      await refreshCloudLinks();
      refreshAudit();
    } catch (e: any) {
      linkError = e?.message ?? 'Network error';
    } finally {
      linkBusy = false;
    }
  }

  async function unlinkCloud(cloudUserId: string, username: string) {
    if (!confirm(`Unlink cloud account "${username}"? They will no longer be able to sign in remotely.`)) return;
    try {
      const resp = await fetch(getHttpUrl('/iot/cloud/unlink'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cloudUserId }),
      });
      if (!resp.ok) {
        const j = await safeJsonParse(resp);
        console.error('Unlink failed', j);
        return;
      }
      await refreshCloudLinks();
      refreshAudit();
    } catch (e) {
      console.error('Unlink error', e);
    }
  }

  async function getPasswords() {
    wait = true;
    error = false;
    showPasswords = false;
    try {
      const valid = await checkKeys();
      if (valid) {
        const response = await fetch(getHttpUrl('/iot/button'), {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({tag: 'ShowPassword'}),
        });
        const result = await safeJsonParse(response);
        if ($keysStore.secret && result.data?.P2Password) {
          try {
            passwords = JSON.parse(CryptoJS.AES.decrypt(result.data.P2Password, $keysStore.secret).toString(CryptoJS.enc.Utf8));
            showPasswords = true;
          } catch (decryptError) {
            console.error('Failed to decrypt passwords:', decryptError);
            error = true;
          }
        } else {
          console.error('Failed to retrieve passwords');
          error = true;
        }
      } else {
        error = true;
      }
    } finally {
      wait = false;
    }
  }

  function update() {
    $navigationStore.invalidate = true;
  }

  function getEncryptedData(users: string[], passwords: string[], secret: string | undefined): { users: string, passwords: string } {
    if (!secret) return { users: '', passwords: '' };
    return {
      users: CryptoJS.AES.encrypt(JSON.stringify(users), secret as string).toString(),
      passwords: CryptoJS.AES.encrypt(JSON.stringify(passwords), secret as string).toString(),
    };
  }

  $: encryptedData = getEncryptedData(users, passwords, $keysStore.secret);

  /**
   * Phase 5.1 proto-direct save (settings field 34 / TAG.AccountSettings).
   *
   * Firmware apply_accounts (nova_dataexc.c):
   *   field 1 = repeated UserAccount { 1=slot, 2=id, 3=password }
   *   field 2 = login_pw string
   *
   * Constraint: this bridge does NOT have the DH shared secret, so the
   * AES-encrypted blobs (`users`, `passwords` produced by getEncryptedData)
   * are forwarded opaquely. Firmware will store them truncated to
   * PASSWORD_LEN — same legacy behaviour, just via the typed proto path.
   * Real per-slot id/pw extraction needs the DH secret; tracked as a
   * separate workstream (firmware-side decryption).
   */
  async function saveAccounts(d: { users: string, passwords: string }): Promise<void> {
    const usersBytes = new TextEncoder().encode(d?.users ?? '');
    const pwBytes    = new TextEncoder().encode(d?.passwords ?? '');
    // Single UserAccount entry at slot 0 carrying both encrypted blobs.
    // Slot 0 is force-encoded (proto3 would suppress 0).
    const slot = buildForceVarintBytes({ 1: 0 });
    const id   = wrapAsLengthDelim(2, usersBytes);
    const pw   = wrapAsLengthDelim(3, pwBytes);
    const userInner = new Uint8Array(slot.length + id.length + pw.length);
    userInner.set(slot, 0);
    userInner.set(id, slot.length);
    userInner.set(pw, slot.length + id.length);
    const userTLV = wrapAsLengthDelim(1, userInner);
    await writeProtoRaw(TAG.AccountSettings, userTLV);
  }

  onMount(async () => {
    try {
      applyMeta(data.meta);
      // Kick off audit fetch in parallel — failure is non-fatal.
      refreshAudit();
      refreshCloudLinks();
    } finally {
      ready = true;
    }
  });

  onMount(() => {
    // Live username hydration from the AccountSettings proto store. Slots
    // 0..9 are sparse — fill empty strings for missing slots so the
    // template's positional rendering stays stable.
    const unsub = accountSettings.subscribe((acct) => {
      if (!acct) return;
      const slots: string[] = Array(10).fill('');
      for (const u of acct.users ?? []) {
        if (typeof u?.slot === 'number' && u.slot >= 0 && u.slot < 10) {
          slots[u.slot] = u.userId ?? '';
        }
      }
      users = slots;
      if (!$navigationStore.isDirty?.()) {
        $navigationStore = { ...$navigationStore, invalidate: true };
      }
    });
    return () => unsub();
  });

</script>

<GellertPage {wait} {title} {ready} level={2} name="accounts">
  <Card class="xl:w-11/12 md:mx-2 xl:mx-auto flex flex-col">
    <ScrollableArea>
      <!-- ── Current session banner ── -->
      <div class="px-2 py-1 text-size-large bg-surface-200 border-b border-gray-400">
        Signed in as
        <span class="font-bold">{currentSession.actor}</span>
        (Level {currentSession.level}{#if currentSession.slot !== null}, slot {currentSession.slot + 1}{/if})
      </div>

      <!-- ── Local accounts table ── -->
      <div class="table-container px-2 pt-2">
        <div class="font-bold text-size-xl pb-1">Local Accounts (no internet required)</div>
        <Table>
          <Row class="text-size-large font-bold">
            <Column class="w-8">#</Column>
            <Column class="w-[22%] border-r border-gray-400">{ $t('level2.accounts.user-names') }</Column>
            <Column class="w-[22%] border-r border-gray-400">{ $t('level2.accounts.passwords') }</Column>
            <Column class="w-[18%] border-r border-gray-400">Role</Column>
            <Column class="w-[22%] border-r border-gray-400">Last Login</Column>
            <Column class="w-[8%]">Logins</Column>
          </Row>
          {#each users as _, i}
            {@const m = slotMeta[i]}
            <Row class="text-size-large">
              <Column class="w-8">{i + 1}</Column>
              <Column class="w-[22%] border-r border-gray-400 !py-1">
                <div class="px-2">
                  <TextField size="lg" extended="w-full" bind:value={users[i]} {edit} keyboardType={KeyboardTypes.Alpha} />
                </div>
              </Column>
              <Column class="w-[22%] border-r border-gray-400 !py-1">
                <div class="px-2">
                  <TextField size="lg" extended="w-full" bind:value={passwords[i]} {edit} keyboardType={KeyboardTypes.Alpha} />
                </div>
              </Column>
              <Column class="w-[18%] border-r border-gray-400 !py-1">
                <div class="px-2">
                  <Select
                    size="lg"
                    extended="w-full"
                    value={m?.role ?? 'operator'}
                    options={roleOptions}
                    edit={true}
                    on:change={(e) => onRoleChange(i, e)}
                  />
                </div>
              </Column>
              <Column class="w-[22%] border-r border-gray-400 text-size-base">
                {fmtTs(m?.lastLogin ?? null)}
                {#if m?.lastLoginIp}
                  <div class="text-size-small text-gray-500">{m.lastLoginIp}</div>
                {/if}
              </Column>
              <Column class="w-[8%] text-size-base">{m?.loginCount ?? 0}</Column>
            </Row>
          {/each}
        </Table>
      </div>

      <!-- ── Factory/installer row ── -->
      <div class="px-2 pt-3">
        <Table>
          <Row class="text-size-large">
            <Column class="w-[44%] font-bold">Factory / Installer Password</Column>
            <Column class="w-[18%]">Level 2 (always)</Column>
            <Column class="w-[22%]">Last used: {fmtTs(factoryMeta?.lastLogin ?? null)}</Column>
            <Column class="w-[16%]">Uses: {factoryMeta?.loginCount ?? 0}</Column>
          </Row>
        </Table>
      </div>

      <!-- ── Cloud identity ── -->
      <div class="px-2 pt-3">
        <div class="flex items-center pb-1">
          <div class="font-bold text-size-xl flex-1">Cloud Accounts (Agristar sign-in)</div>
          <Button size="md" class="w-40" on:click={() => { showLinkForm = !showLinkForm; linkError = ''; }}>
            {showLinkForm ? 'Cancel' : 'Link Account'}
          </Button>
        </div>

        {#if showLinkForm}
          <div class="border border-gray-400 rounded p-2 mb-2 bg-surface-100">
            <div class="text-size-base text-gray-700 pb-2">
              Link a Django (Agristar cloud) user account so they can sign in
              remotely without the legacy remote-login password.
            </div>
            <Table>
              <Row class="text-size-base">
                <Column class="w-32 font-bold">Django Username</Column>
                <Column>
                  <div class="px-2">
                    <TextField size="md" extended="w-full" bind:value={linkUsername} edit={true} keyboardType={KeyboardTypes.Alpha} />
                  </div>
                </Column>
              </Row>
              <Row class="text-size-base">
                <Column class="w-32 font-bold">Password</Column>
                <Column>
                  <div class="px-2">
                    <TextField size="md" extended="w-full" bind:value={linkPassword} edit={true} keyboardType={KeyboardTypes.Alpha} />
                  </div>
                </Column>
              </Row>
              <Row class="text-size-base">
                <Column class="w-32 font-bold">Bind to Slot</Column>
                <Column>
                  <div class="px-2">
                    <Select
                      size="md"
                      extended="w-full"
                      bind:value={linkSlot}
                      options={[
                        { text: 'Cloud-only admin (no local slot)', value: '-1' },
                        ...Array.from({ length: 10 }, (_, i) => ({
                          text: `Slot ${i + 1}${users[i] ? ` — ${users[i]}` : ''}`,
                          value: String(i),
                        })),
                      ]}
                      edit={true}
                    />
                  </div>
                </Column>
              </Row>
              <Row class="text-size-base">
                <Column class="w-32 font-bold">Role</Column>
                <Column>
                  <div class="px-2">
                    <Select
                      size="md"
                      extended="w-full"
                      bind:value={linkRole}
                      options={[
                        { text: 'Admin (Level 2)',    value: 'admin' },
                        { text: 'Operator (Level 1)', value: 'operator' },
                      ]}
                      edit={true}
                    />
                  </div>
                </Column>
              </Row>
            </Table>
            {#if linkError}
              <div class="text-size-base text-red-600 pt-1">{linkError}</div>
            {/if}
            <div class="flex justify-end pt-2">
              <Button size="md" class="w-28" on:click={submitLink} disabled={linkBusy}>
                {linkBusy ? 'Linking…' : 'Link'}
              </Button>
            </div>
          </div>
        {/if}

        {#if cloudLoading}
          <div class="text-size-base text-gray-500">Loading…</div>
        {:else if cloudLinks.length === 0}
          <div class="text-size-base text-gray-500 italic">
            No cloud accounts linked. Link an Agristar account above to enable
            remote sign-in without a shared password.
          </div>
        {:else}
          <Table>
            <Row class="text-size-base font-bold">
              <Column class="w-[22%]">Username</Column>
              <Column class="w-[26%]">Name</Column>
              <Column class="w-[12%]">Role</Column>
              <Column class="w-[10%]">Slot</Column>
              <Column class="w-[20%]">Last Remote Login</Column>
              <Column class="w-[10%]">&nbsp;</Column>
            </Row>
            {#each cloudLinks as l}
              <Row class="text-size-base">
                <Column class="w-[22%]">{l.username}</Column>
                <Column class="w-[26%]">{l.displayName}</Column>
                <Column class="w-[12%]">{l.role}</Column>
                <Column class="w-[10%]">{l.slot !== null ? l.slot + 1 : '—'}</Column>
                <Column class="w-[20%]">{fmtTs(l.lastRemoteLogin)}</Column>
                <Column class="w-[10%] !py-1">
                  <div class="px-1">
                    <Button size="sm" class="w-full !variant-ghost-error" on:click={() => unlinkCloud(l.cloudUserId, l.username)}>
                      Unlink
                    </Button>
                  </div>
                </Column>
              </Row>
            {/each}
          </Table>
        {/if}
      </div>

      <!-- ── Recent activity ── -->
      <div class="px-2 pt-3 pb-2">
        <div class="flex items-center pb-1">
          <div class="font-bold text-size-xl flex-1">Recent Activity</div>
          <Button size="md" class="w-28" on:click={refreshAudit}>Refresh</Button>
        </div>
        {#if auditLoading}
          <div class="text-size-base text-gray-500">Loading…</div>
        {:else if auditEntries.length === 0}
          <div class="text-size-base text-gray-500">No activity recorded yet.</div>
        {:else}
          <Table>
            <Row class="text-size-base font-bold">
              <Column class="w-[22%]">When</Column>
              <Column class="w-[18%]">Actor</Column>
              <Column class="w-[12%]">Event</Column>
              <Column class="w-[12%]">Route</Column>
              <Column class="w-[36%]">Detail</Column>
            </Row>
            {#each auditEntries as e}
              <Row class="text-size-base">
                <Column class="w-[22%]">{fmtTs(e.ts)}</Column>
                <Column class="w-[18%]">{e.actor}{e.slot !== null ? ` (#${e.slot + 1})` : ''}</Column>
                <Column class="w-[12%]">{e.kind}</Column>
                <Column class="w-[12%]">{e.route ?? ''}</Column>
                <Column class="w-[36%] truncate">{e.detail ?? ''}</Column>
              </Row>
            {/each}
          </Table>
        {/if}
      </div>

      <SaveButton slot="footer-center" edit={showPasswords} bind:wait={wait} data={encryptedData} original={[]} on:complete={update} onSave={saveAccounts}/>
      <Button slot="footer-right" size="xl" class="w-24 md:w-64 xl:w-5/6 {error ? '!variant-ghost-error' : ''}" on:click={getPasswords}>{error ? $t('global.retry') : $t('global.show-passwords')}</Button>
    </ScrollableArea>
  </Card>
</GellertPage>

<style>
  .table-container {
    /* Add margin areas for swipe gestures */
    position: relative;
  }
</style>
