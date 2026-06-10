<script lang="ts">
  import { onMount } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Button from "$lib/ui/Button.svelte";
  import { getHttpUrl, safeJsonParse } from "$lib/business/util";

  // Account activity (audit) viewer — the sign-in / account-change log fetched
  // from /iot/audit. Moved out of AccountsForm 2026-06-10 into the History &
  // Logs hub (it's account *activity*, i.e. a log, not an account setting).
  // READ-ONLY no-save viewer: rendered as the dashboard 'accountactivity' modal
  // (MODAL_NOSAVE). Prop contract mirrors the other viewer forms
  // (AlarmHistoryForm). docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export async function flush(): Promise<void> {}   // read-only viewer

  interface AuditEntry { ts: string; kind: string; actor: string; slot: number | null; level: number; route?: string; detail?: string; ip?: string; }
  let auditEntries: AuditEntry[] = [];
  let auditLoading = false;

  function fmtTs(iso: string | null): string {
    if (!iso) return 'never';
    try { return new Date(iso).toLocaleString(); } catch { return iso; }
  }
  async function refreshAudit() {
    auditLoading = true;
    try {
      const j = await safeJsonParse(await fetch(getHttpUrl('/iot/audit?limit=50')));
      auditEntries = (j?.entries ?? []) as AuditEntry[];
    } catch (e) { console.error('Audit fetch failed', e); auditEntries = []; }
    finally { auditLoading = false; }
  }

  onMount(() => { refreshAudit().finally(() => { ready = true; }); });
</script>

<div class="pform pform--{theme}">
  <Card class="mx-auto w-full flex flex-col">
    <div class="flex items-center px-2 pt-2 pb-1">
      <div class="font-bold text-size-xl flex-1">Recent Activity</div>
      <Button size="md" class="w-28" on:click={refreshAudit}>Refresh</Button>
    </div>
    {#if auditLoading}
      <div class="text-size-base text-gray-500 px-2 pb-2">Loading…</div>
    {:else if auditEntries.length === 0}
      <div class="text-size-base text-gray-500 px-2 pb-2">No activity recorded yet.</div>
    {:else}
      <div class="px-2 pb-2">
        <Table>
          <Row class="text-size-base font-bold"><Column class="w-[22%]">When</Column><Column class="w-[18%]">Actor</Column><Column class="w-[12%]">Event</Column><Column class="w-[12%]">Route</Column><Column class="w-[36%]">Detail</Column></Row>
          {#each auditEntries as e}
            <Row class="text-size-base"><Column class="w-[22%]">{fmtTs(e.ts)}</Column><Column class="w-[18%]">{e.actor}{e.slot !== null ? ` (#${e.slot + 1})` : ''}</Column><Column class="w-[12%]">{e.kind}</Column><Column class="w-[12%]">{e.route ?? ''}</Column><Column class="w-[36%] truncate">{e.detail ?? ''}</Column></Row>
          {/each}
        </Table>
      </div>
    {/if}
  </Card>
</div>
