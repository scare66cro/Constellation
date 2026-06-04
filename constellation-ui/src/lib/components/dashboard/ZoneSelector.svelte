<script lang="ts">
  // Multi-zone selector (Tier A): treats each Constellation panel on
  // the network as a "zone". This Pi5 is "Local"; remote panels come
  // from the bridge's /iot/remote-systems registry (UUID-keyed,
  // DHCP-resilient). Picking a remote zone opens that panel's
  // dashboard / orbit page in a new tab — no proto-store rewiring
  // (that's Tier B). Operators get a single jumping-off point for
  // every storage room at the site.
  //
  // Listed inside the preview banner so the operator always knows
  // they can navigate cross-panel without losing the local view.
  import { onMount, onDestroy } from "svelte";
  import { getHttpUrl } from "$lib/business/util";

  interface RemoteSystem {
    id: string;
    name: string;
    host: string;
    port: number;
    novaId: string;
    panelName: string;
    online: boolean;
    lastSuccessAt: number | null;
    lastError: string | null;
  }

  let systems: RemoteSystem[] = [];
  let pollHandle: ReturnType<typeof setInterval> | null = null;
  let menuOpen = false;
  let loading = true;
  let error = '';

  async function refresh() {
    try {
      const res = await fetch(getHttpUrl('/iot/remote-systems'));
      if (!res.ok) {
        error = `HTTP ${res.status}`;
        return;
      }
      const data = await res.json();
      systems = Array.isArray(data?.systems) ? data.systems : [];
      error = '';
    } catch (e: any) {
      error = e?.message ?? 'fetch failed';
    } finally {
      loading = false;
    }
  }

  onMount(() => {
    void refresh();
    pollHandle = setInterval(refresh, 10_000);
    document.addEventListener('click', closeOnOutsideClick);
  });

  onDestroy(() => {
    if (pollHandle) clearInterval(pollHandle);
    document.removeEventListener('click', closeOnOutsideClick);
  });

  let containerEl: HTMLDivElement | null = null;
  function closeOnOutsideClick(e: MouseEvent) {
    if (menuOpen && containerEl && !containerEl.contains(e.target as Node)) {
      menuOpen = false;
    }
  }

  function urlFor(s: RemoteSystem, path: string): string {
    const port = s.port === 80 ? '' : `:${s.port}`;
    return `http://${s.host}${port}${path}`;
  }

  function openRemote(s: RemoteSystem, path: string) {
    const url = urlFor(s, path);
    window.open(url, '_blank', 'noopener,noreferrer');
    menuOpen = false;
  }

  $: onlineCount = systems.filter(s => s.online).length;
</script>

<div bind:this={containerEl} class="relative">
  <button
    class="flex items-center gap-1 px-2 py-0.5 rounded border bg-white hover:bg-gray-50 text-xs font-bold border-gray-300"
    on:click={() => menuOpen = !menuOpen}
    title="Switch storage zone / browse other panels"
  >
    <span class="text-base leading-none">🏭</span>
    <span>Zone: <strong>Local</strong></span>
    {#if systems.length > 0}
      <span class="ml-1 text-gray-500 font-normal">
        ({onlineCount}/{systems.length} remote)
      </span>
    {/if}
    <span class="text-gray-400 ml-0.5">▾</span>
  </button>

  {#if menuOpen}
    <div class="absolute right-0 top-full mt-1 w-80 bg-white border border-gray-300 rounded shadow-lg z-50">
      <!-- Local entry (always at top, always "online" — this is us) -->
      <div class="px-3 py-2 border-b border-gray-200 bg-blue-50">
        <div class="flex items-center justify-between">
          <div class="flex items-center gap-2">
            <span class="w-2 h-2 bg-green-500 rounded-full"></span>
            <div>
              <div class="text-sm font-bold">Local (this panel)</div>
              <div class="text-[10px] text-gray-500">Currently viewing</div>
            </div>
          </div>
          <span class="text-[10px] text-blue-700 font-bold">✓ HERE</span>
        </div>
      </div>

      {#if loading && systems.length === 0}
        <div class="px-3 py-4 text-center text-sm text-gray-500">Loading remote panels…</div>
      {:else if systems.length === 0}
        <div class="px-3 py-4 text-center text-sm text-gray-500">
          No remote panels configured.
          <div class="text-[10px] mt-1">
            Add panels under Level 2 → Remote Systems.
          </div>
        </div>
      {:else}
        <div class="max-h-80 overflow-y-auto">
          {#each systems as s (s.id)}
            <div class="px-3 py-2 border-b border-gray-100 last:border-b-0 hover:bg-gray-50">
              <div class="flex items-center justify-between">
                <div class="flex items-center gap-2 min-w-0">
                  <span class="w-2 h-2 rounded-full {s.online ? 'bg-green-500' : 'bg-red-400'}"></span>
                  <div class="min-w-0">
                    <div class="text-sm font-bold truncate" title={s.name}>{s.name}</div>
                    <div class="text-[10px] text-gray-500 truncate" title="{s.host}:{s.port}">
                      {s.host}:{s.port}{s.panelName && s.panelName !== s.name ? ` · ${s.panelName}` : ''}
                    </div>
                    {#if !s.online && s.lastError}
                      <div class="text-[10px] text-red-600 truncate" title={s.lastError}>
                        ⚠ {s.lastError}
                      </div>
                    {/if}
                  </div>
                </div>
              </div>
              <div class="flex gap-1 mt-1">
                <button
                  class="flex-1 text-[11px] px-2 py-0.5 rounded {s.online ? 'bg-blue-500 hover:bg-blue-600 text-white' : 'bg-gray-200 text-gray-500 cursor-not-allowed'}"
                  on:click={() => s.online && openRemote(s, '/dashboard')}
                  disabled={!s.online}
                  title={s.online ? 'Open remote dashboard in new tab' : 'Remote panel offline'}
                >
                  Dashboard ↗
                </button>
                <button
                  class="flex-1 text-[11px] px-2 py-0.5 rounded {s.online ? 'bg-gray-600 hover:bg-gray-700 text-white' : 'bg-gray-200 text-gray-500 cursor-not-allowed'}"
                  on:click={() => s.online && openRemote(s, '/orbit/')}
                  disabled={!s.online}
                  title="Browse this panel's orbits"
                >
                  Orbits ↗
                </button>
                <button
                  class="text-[11px] px-2 py-0.5 rounded bg-white hover:bg-gray-100 border border-gray-300"
                  on:click={() => openRemote(s, '/')}
                  title="Open the panel's home page"
                >
                  Home ↗
                </button>
              </div>
            </div>
          {/each}
        </div>
      {/if}

      {#if error}
        <div class="px-3 py-2 bg-red-50 border-t border-red-200 text-[10px] text-red-700">
          Registry fetch error: {error}
        </div>
      {/if}

      <div class="px-3 py-1.5 bg-gray-50 border-t border-gray-200 text-[10px] text-gray-500">
        Tier A: opens remote dashboards in new tabs.
        Tier B (live federation) ships when you're ready.
      </div>
    </div>
  {/if}
</div>
