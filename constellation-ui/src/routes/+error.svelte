<script lang="ts">
  import { onMount } from 'svelte';
  import { ProgressRadial } from '@skeletonlabs/skeleton';
  import { isLoopbackAccess, getHttpUrl } from '$lib/business/util';
  // SvelteKit supplies these
  export let error: any = undefined; // eslint-disable-line @typescript-eslint/no-explicit-any
  export let status: number = 500;

  let attempts = 0;
  let maxAttempts = 24; // ~2 minutes if 5s cadence (adjusted by backoff)
  let nextDelay = 5000; // start at 5s
  let stopped = false;
  let lastMessage = '';
  // In loopback mode, we don't care about internet connectivity - local backend is always reachable
  let isLoopback = typeof window !== 'undefined' && isLoopbackAccess();
  let online = isLoopback ? true : (typeof navigator !== 'undefined' ? navigator.onLine : true);
  let timer: ReturnType<typeof setTimeout> | null = null;
  // Spinner gate: show spinner for first 10s before revealing error UI
  let showSpinner = true;
  let spinnerTimer: ReturnType<typeof setTimeout> | null = null;
  
  // Diagnostic logs from service worker
  let swDiagnostics: Array<{type: string; url?: string; error?: string; isLoopback?: boolean; timestamp: number}> = [];
  let showDiagnostics = false;

  const HEALTH_ENDPOINTS = ['/iot/config', '/']; // try these in order

  function log(msg: string) {
    lastMessage = msg;
    console.debug('[kiosk-error]', msg);
  }

  function loadDiagnosticsFromStorage() {
    try {
      const stored = sessionStorage.getItem('sw_diagnostics');
      if (stored) {
        swDiagnostics = JSON.parse(stored);
      }
    } catch (e) {
      console.warn('Failed to load SW diagnostics from storage:', e);
    }
  }

  function requestSwDiagnostics() {
    if (navigator.serviceWorker?.controller) {
      navigator.serviceWorker.controller.postMessage({ type: 'GET_DIAGNOSTICS' });
    }
  }

  function formatTimestamp(ts: number): string {
    return new Date(ts).toLocaleTimeString();
  }

  async function probe() {
    attempts++;
    // In loopback mode, always consider ourselves online (local network works without internet)
    online = isLoopback ? true : (typeof navigator !== 'undefined' ? navigator.onLine : true);
    if (!online) {
      log('Offline – waiting for network to return');
      schedule();
      return;
    }
    for (const endpoint of HEALTH_ENDPOINTS) {
      const url = getHttpUrl(endpoint);
      try {
        // Use getHttpUrl to get the correct host for loopback/network scenarios
        const res = await fetch(url, {
          method: 'GET',
          cache: 'no-store',
          credentials: 'include'
        });
        if (res.ok) {
          log(`Recovery success via ${url}; reloading UI`);
          // Full reload to reset any bad state
          window.location.replace('/');
          return;
        } else {
          log(`Probe ${url} -> ${res.status}`);
        }
      } catch (e) {
        log(`Probe ${url} failed: ${(e as Error).message}`);
      }
    }
    if (attempts >= maxAttempts) {
      stopped = true;
      log('Max auto-retry attempts reached');
      return;
    }
    // Exponential-ish backoff capped at 30s to reduce hammering when backend down
    nextDelay = Math.min(30000, Math.round(nextDelay * 1.3));
    schedule();
  }

  function schedule() {
    if (stopped) return;
    if (timer) clearTimeout(timer);
    timer = setTimeout(probe, nextDelay);
  }

  function manualRetry() {
    if (stopped) {
      // Give user another burst of attempts
      attempts = 0;
      maxAttempts = 12; // shorter second window
      nextDelay = 5000;
      stopped = false;
    }
    probe();
  }

  onMount(() => {
    log(`Kiosk recovery page mounted (loopback=${isLoopback})`);
    // Load any stored diagnostics from service worker
    loadDiagnosticsFromStorage();
    // Request fresh diagnostics from service worker
    requestSwDiagnostics();
    
    // Listen for diagnostic responses from service worker
    const handleSwMessage = (event: MessageEvent) => {
      if (event.data?.type === 'SW_DIAGNOSTICS_RESPONSE') {
        swDiagnostics = event.data.payload || [];
      } else if (event.data?.type === 'SW_DIAGNOSTIC') {
        // Real-time diagnostic update
        swDiagnostics = [...swDiagnostics, event.data.payload];
        if (swDiagnostics.length > 50) swDiagnostics = swDiagnostics.slice(-50);
      }
    };
    
    if (navigator.serviceWorker) {
      navigator.serviceWorker.addEventListener('message', handleSwMessage);
    }
    
    // Small initial delay so any transient restart finishes
    timer = setTimeout(probe, 2500);
    // Show a friendly spinner for 10 seconds before showing details
    spinnerTimer = setTimeout(() => (showSpinner = false), 10000);
    // In loopback mode, skip online/offline listeners - they reflect internet status, not local network
    if (!isLoopback) {
      window.addEventListener('online', manualRetry);
      window.addEventListener('offline', () => (online = false));
    }
    return () => {
      if (timer) clearTimeout(timer);
      if (spinnerTimer) clearTimeout(spinnerTimer);
      if (!isLoopback) {
        window.removeEventListener('online', manualRetry);
      }
      if (navigator.serviceWorker) {
        navigator.serviceWorker.removeEventListener('message', handleSwMessage);
      }
    };
  });
</script>

<svelte:head>
  <title>{status} Error – Attempting Auto-Recovery</title>
  <meta name="robots" content="noindex" />
</svelte:head>

{#if showSpinner}
  <div class="min-h-screen w-full flex flex-col items-center justify-center p-6 gap-4 text-center select-none">
    <ProgressRadial />
  </div>
{:else}
<div class="min-h-screen w-full flex flex-col items-center justify-center bg-neutral-950 text-neutral-100 p-6 gap-8 text-center select-none">
  <div class="max-w-xl flex flex-col gap-4">
    <h1 class="text-4xl font-bold tracking-tight">{status}</h1>
    <p class="text-lg font-medium">
      {#if isLoopback}
        Local service temporarily unavailable
      {:else}
        Network connection issue
      {/if}
    </p>
    {#if status === 503}
      <p>
        {#if isLoopback}
          The local backend isn't responding. This typically happens during system restarts.
        {:else}
          Unable to reach the storage controller. Please check network connectivity.
        {/if}
      </p>
    {:else}
      <p>
        {#if isLoopback}
          An unexpected error occurred. Auto-recovery is in progress.
        {:else}
          Connection to the storage controller was lost. Check that you're connected to the same network as the controller.
        {/if}
      </p>
    {/if}
    {#if error?.message}
      <pre class="text-xs bg-neutral-800/60 p-3 rounded overflow-auto max-h-40 whitespace-pre-wrap">{error.message}</pre>
    {/if}
    <div class="text-sm text-neutral-400 space-y-1">
      <p>Attempt {attempts} {attempts > 0 ? `(next in ${(nextDelay/1000).toFixed(0)}s)` : ''}</p>
      <p class:text-amber-400={!online}>
        {#if isLoopback}
          <span class="text-green-400">●</span> Kiosk Mode (local access via 127.0.0.1)
        {:else if online}
          <span class="text-yellow-400">●</span> Remote Access Mode (network IP)
        {:else}
          <span class="text-red-400">●</span> No network connection detected
        {/if}
      </p>
      <p>{lastMessage}</p>
    </div>
    <div class="flex flex-col sm:flex-row gap-3 justify-center mt-2">
      <button on:click={manualRetry} class="px-6 py-3 rounded bg-primary-500 hover:bg-primary-600 active:bg-primary-700 text-white font-semibold disabled:opacity-50" disabled={!online && !isLoopback}>Retry Now</button>
      <button on:click={() => window.location.replace('/')} class="px-6 py-3 rounded bg-neutral-700 hover:bg-neutral-600 font-semibold">Go Home</button>
    </div>
    {#if stopped}
      <div class="mt-4 text-amber-300 text-sm">
        {#if isLoopback}
          Automatic retries paused. The backend may need manual restart. Press Retry Now to continue.
        {:else}
          Automatic retries paused. Check network connection or contact support. Press Retry Now to continue.
        {/if}
      </div>
    {/if}
    {#if !isLoopback && !online}
      <div class="mt-4 p-4 bg-amber-900/30 border border-amber-700 rounded text-amber-200 text-sm">
        <strong>Troubleshooting tips:</strong>
        <ul class="list-disc list-inside mt-2 text-left">
          <li>Check that your device is connected to the same network as the controller</li>
          <li>Verify the storage controller is powered on</li>
          <li>Try refreshing the page once network is restored</li>
        </ul>
      </div>
    {/if}
    
    <!-- Service Worker Diagnostics Panel -->
    <div class="mt-4">
      <button 
        on:click={() => { showDiagnostics = !showDiagnostics; if (showDiagnostics) requestSwDiagnostics(); }}
        class="text-xs text-neutral-500 hover:text-neutral-300 underline"
      >
        {showDiagnostics ? 'Hide' : 'Show'} Network Diagnostics ({swDiagnostics.length})
      </button>
      
      {#if showDiagnostics && swDiagnostics.length > 0}
        <div class="mt-2 p-3 bg-neutral-900 border border-neutral-700 rounded text-xs text-left max-h-48 overflow-auto">
          <div class="flex justify-between items-center mb-2">
            <strong class="text-neutral-300">Service Worker Logs</strong>
            <button 
              on:click={() => { swDiagnostics = []; sessionStorage.removeItem('sw_diagnostics'); }}
              class="text-red-400 hover:text-red-300"
            >Clear</button>
          </div>
          {#each swDiagnostics.slice(-20).reverse() as log}
            <div class="py-1 border-b border-neutral-800 last:border-0">
              <span class="text-neutral-500">{formatTimestamp(log.timestamp)}</span>
              <span class:text-red-400={log.type.includes('ERROR') || log.type.includes('FAILURE')}
                    class:text-yellow-400={log.type.includes('503')}
                    class:text-green-400={log.type === 'INSTALL' || log.type === 'ACTIVATE'}
                    class="ml-2 font-mono">{log.type}</span>
              {#if log.isLoopback !== undefined}
                <span class="ml-2 px-1 rounded text-[10px] {log.isLoopback ? 'bg-green-900 text-green-300' : 'bg-blue-900 text-blue-300'}">
                  {log.isLoopback ? 'LOOPBACK' : 'NETWORK'}
                </span>
              {/if}
              {#if log.url}
                <div class="text-neutral-400 truncate ml-4">{log.url}</div>
              {/if}
              {#if log.error}
                <div class="text-red-300 ml-4">{log.error}</div>
              {/if}
            </div>
          {/each}
        </div>
      {:else if showDiagnostics}
        <div class="mt-2 p-3 bg-neutral-900 border border-neutral-700 rounded text-xs text-neutral-500">
          No diagnostic logs captured yet. The service worker will log network failures here.
        </div>
      {/if}
    </div>
  </div>
  <footer class="text-xs text-neutral-600">
    {#if isLoopback}
      Kiosk Auto-Recovery Active
    {:else}
      Remote Access Auto-Recovery Active
    {/if}
  </footer>
</div>
{/if}

<style>
  :global(html, body) { margin: 0; }
</style>
