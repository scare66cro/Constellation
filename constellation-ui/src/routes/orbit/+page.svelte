<script lang="ts">
  /**
   * Orbit Simulator Page
   *
   * Interactive graphical simulation of a Constellation Orbit I/O board.
   * Polls the Orbit simulator REST API for live state and renders the
   * OrbitBoard SVG component. Provides controls for:
   *   - Toggling digital inputs (click DI LEDs)
   *   - E-Stop activation (click red button)
   *   - Viewing real-time DO/AO state driven by Nova firmware
   *   - RS-485 bus activity indicators
   */

  import { onMount, onDestroy } from 'svelte';
  import OrbitBoard from '$lib/components/OrbitBoard.svelte';
  import NovaBoard from '$lib/components/NovaBoard.svelte';

  const ORBIT_API = 'http://localhost:9010';
  let pollInterval: ReturnType<typeof setInterval>;
  let connected = false;
  let error = '';

  // Activity tracking for blinking indicators
  let tcpActive = false;
  let uartActive = false;
  let tcpTimeout: ReturnType<typeof setTimeout>;
  let uartTimeout: ReturnType<typeof setTimeout>;

  function flashTcp() {
    tcpActive = true;
    clearTimeout(tcpTimeout);
    tcpTimeout = setTimeout(() => { tcpActive = false; }, 1200);
  }

  function flashUart() {
    uartActive = true;
    clearTimeout(uartTimeout);
    uartTimeout = setTimeout(() => { uartActive = false; }, 1200);
  }

  let status = {
    id: 2,
    ipAddress: '10.47.27.2',
    firmwareVersion: '1.0.0',
    uptime: 0,
    cpuTemp: 42,
    commLost: true,
    safeMode: false,
    eStop: false,
    digitalInputs: new Array(10).fill(false),
    digitalOutputs: new Array(10).fill(false),
    dc24vOutputs: new Array(4).fill(false),
    analogOutputs: [0, 0],
    aoModes: ['voltage', 'voltage'] as ('voltage' | 'current')[],
    vfdActivity: false,
    sensorActivity: false,
  };

  async function fetchStatus() {
    try {
      const res = await fetch(`${ORBIT_API}/api/status`);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data = await res.json();
      status = { ...status, ...data };
      connected = true;
      error = '';
      // Flash indicators whenever we get data from the orbit
      if (!status.commLost) {
        flashTcp();
        flashUart();
      }
    } catch (e: any) {
      connected = false;
      error = `Orbit API unreachable: ${e.message}`;
    }
  }

  async function toggleDI(index: number) {
    try {
      await fetch(`${ORBIT_API}/api/di`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ index, value: !status.digitalInputs[index] }),
      });
      // Optimistic update
      status.digitalInputs[index] = !status.digitalInputs[index];
      status = status; // trigger reactivity
    } catch { /* next poll will sync */ }
  }

  async function toggleEStop() {
    try {
      await fetch(`${ORBIT_API}/api/estop`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ active: !status.eStop }),
      });
      status.eStop = !status.eStop;
      status = status;
    } catch { /* next poll will sync */ }
  }

  async function resetOrbit() {
    try {
      await fetch(`${ORBIT_API}/api/reset`, { method: 'POST' });
      await fetchStatus();
    } catch { /* ignore */ }
  }

  onMount(() => {
    fetchStatus();
    pollInterval = setInterval(fetchStatus, 500); // 2 Hz polling
  });

  onDestroy(() => {
    if (pollInterval) clearInterval(pollInterval);
  });
</script>

<svelte:head>
  <title>Orbit Simulator — {status.ipAddress}</title>
</svelte:head>

<div class="orbit-page p-4 md:p-8 max-w-7xl mx-auto">
  <!-- Header -->
  <div class="flex items-center justify-between mb-6">
    <div>
      <h1 class="text-2xl font-bold text-white">Constellation System</h1>
      <p class="text-sm text-surface-400">
        Nova → Orbit #{status.id} ({status.ipAddress}) —
        {#if connected}
          <span class="text-success-400">Connected</span>
        {:else}
          <span class="text-error-400">{error || 'Disconnected'}</span>
        {/if}
      </p>
    </div>
    <div class="flex gap-2">
      <button class="btn variant-ghost-surface btn-sm" on:click={resetOrbit}>
        Reset Board
      </button>
      <a href="/" class="btn variant-soft-primary btn-sm">
        ← Back to Dashboard
      </a>
    </div>
  </div>

  <!-- Board Visualizations — Nova + Cable + Orbit -->
  <div class="boards-row mb-6">
    <!-- Nova Board -->
    <div class="board-cell nova-cell">
      <NovaBoard
        {tcpActive}
        {uartActive}
        connected={connected && !status.commLost}
        uptime={status.uptime}
      />
    </div>

    <!-- Ethernet Cable Connection -->
    <div class="cable-cell">
      <svg viewBox="0 0 80 60" class="cable-svg" xmlns="http://www.w3.org/2000/svg">
        <!-- Cable line -->
        <line x1="0" y1="30" x2="80" y2="30"
              stroke={connected && !status.commLost ? '#3498db' : '#555'}
              stroke-width="3" stroke-linecap="round"/>
        <!-- RJ45 plug left -->
        <rect x="0" y="22" width="12" height="16" rx="2"
              fill="#1c2833" stroke="#555" stroke-width="1"/>
        <!-- RJ45 plug right -->
        <rect x="68" y="22" width="12" height="16" rx="2"
              fill="#1c2833" stroke="#555" stroke-width="1"/>
        <!-- Data packets animation -->
        {#if tcpActive}
          <circle r="3" fill="#f39c12" opacity="0.9">
            <animateMotion dur="0.6s" repeatCount="indefinite"
                           path="M12,30 L68,30"/>
          </circle>
          <circle r="3" fill="#3498db" opacity="0.9">
            <animateMotion dur="0.6s" repeatCount="indefinite"
                           path="M68,30 L12,30"/>
          </circle>
        {/if}
        <!-- Label -->
        <text x="40" y="12" text-anchor="middle" fill="#aaa" font-size="7"
              font-family="monospace">
          Modbus TCP
        </text>
        <text x="40" y="52" text-anchor="middle" fill="#888" font-size="6">
          10.47.27.x
        </text>
      </svg>
    </div>

    <!-- Orbit Board -->
    <div class="board-cell orbit-cell">
      <OrbitBoard
        {status}
        onDigitalInputToggle={toggleDI}
        onEStopToggle={toggleEStop}
      />
    </div>
  </div>

  <!-- Status Panels -->
  <div class="grid grid-cols-1 md:grid-cols-3 gap-4 mb-6">
    <!-- Digital Inputs -->
    <div class="card p-4 variant-soft-surface">
      <h3 class="font-bold text-sm mb-2 text-surface-300">Digital Inputs</h3>
      <div class="grid grid-cols-5 gap-2">
        {#each status.digitalInputs as val, i}
          <button
            class="btn btn-sm {val ? 'variant-filled-success' : 'variant-ghost-surface'}"
            on:click={() => toggleDI(i)}
          >
            DI{i + 1}
          </button>
        {/each}
      </div>
    </div>

    <!-- Digital Outputs (read-only, driven by Nova) -->
    <div class="card p-4 variant-soft-surface">
      <h3 class="font-bold text-sm mb-2 text-surface-300">Digital Outputs</h3>
      <div class="grid grid-cols-5 gap-2">
        {#each status.digitalOutputs as val, i}
          <div class="text-center px-2 py-1 rounded text-xs font-mono
                      {val ? 'bg-success-500/20 text-success-300' : 'bg-surface-700 text-surface-400'}">
            DO{i + 1}
          </div>
        {/each}
      </div>
    </div>

    <!-- Analog Outputs -->
    <div class="card p-4 variant-soft-surface">
      <h3 class="font-bold text-sm mb-2 text-surface-300">Analog Outputs</h3>
      {#each status.analogOutputs as val, i}
        {@const maxVal = status.aoModes[i] === 'voltage' ? 10000 : 20000}
        <div class="flex items-center gap-2 mb-2">
          <span class="text-xs text-surface-400 w-10">AO{i + 1}</span>
          <div class="flex-1 h-4 bg-surface-700 rounded-full overflow-hidden">
            <div class="h-full bg-primary-500 rounded-full transition-all"
                 style="width: {Math.min(100, (val / maxVal) * 100)}%"></div>
          </div>
          <span class="text-xs text-surface-300 w-16 text-right font-mono">
            {status.aoModes[i] === 'voltage'
              ? `${(val / 1000).toFixed(1)}V`
              : `${(val / 1000).toFixed(1)}mA`}
          </span>
        </div>
      {/each}
    </div>
  </div>

  <!-- Status Bar -->
  <div class="card p-3 variant-soft-surface flex flex-wrap gap-4 text-xs text-surface-400">
    <span>FW: {status.firmwareVersion}</span>
    <span>CPU: {status.cpuTemp}°C</span>
    <span>Uptime: {Math.floor(status.uptime / 3600)}h {Math.floor((status.uptime % 3600) / 60)}m</span>
    <span class={status.commLost ? 'text-error-400 font-bold' : 'text-success-400'}>
      Nova: {status.commLost ? 'COMM LOST' : 'OK'}
    </span>
    {#if status.safeMode}
      <span class="text-warning-400 font-bold">⚠ SAFE MODE</span>
    {/if}
    {#if status.eStop}
      <span class="text-error-400 font-bold animate-pulse">🛑 E-STOP ACTIVE</span>
    {/if}
    <span>VFD: {status.vfdActivity ? '⚡' : '—'}</span>
    <span>Sensor: {status.sensorActivity ? '⚡' : '—'}</span>
  </div>
</div>

<style>
  .boards-row {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 0;
    flex-wrap: wrap;
  }

  .board-cell {
    flex-shrink: 0;
  }

  .nova-cell {
    max-width: 380px;
    width: 35%;
    min-width: 260px;
  }

  .orbit-cell {
    max-width: 560px;
    width: 50%;
    min-width: 340px;
  }

  .cable-cell {
    width: 80px;
    flex-shrink: 0;
    display: flex;
    align-items: center;
    justify-content: center;
  }

  .cable-svg {
    width: 80px;
    height: 60px;
  }

  /* Stack vertically on narrow screens */
  @media (max-width: 900px) {
    .boards-row {
      flex-direction: column;
      gap: 1rem;
    }
    .cable-cell {
      transform: rotate(90deg);
    }
    .nova-cell, .orbit-cell {
      width: 100%;
      max-width: 560px;
    }
  }
</style>
