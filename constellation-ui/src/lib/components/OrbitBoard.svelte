<script lang="ts">
  /**
   * OrbitBoard.svelte — Interactive SVG visualization of a Constellation Orbit I/O board
   *
   * Matches the physical Orbit board layout from the Constellation Concept PDF:
   *   • 5.5" × 4.5" PCB with AM2432 MCU
   *   • 10 Digital Inputs with indicator LEDs (right side)
   *   • 10 Digital Outputs with indicator LEDs + self-resetting fuses (left side)
   *   • 2 Analog Outputs (0-10V / 4-20mA, bottom right)
   *   • 4 × 24V DC 1A outputs (top center)
   *   • E-Stop input (right side, top)
   *   • 5 dipswitches for IP address (top center)
   *   • 2 RS-485 RTU ports with activity LEDs (bottom left)
   *   • 2 RJ45 Ethernet ports with link/activity LEDs (top corners)
   *
   * Props:
   *   status — live Orbit state from the API
   *   onDigitalInputToggle(index) — callback when user clicks a DI
   *   onEStopToggle() — callback when user clicks E-Stop
   */

  export let status: {
    id: number;
    ipAddress: string;
    firmwareVersion: string;
    uptime: number;
    cpuTemp: number;
    commLost: boolean;
    safeMode: boolean;
    eStop: boolean;
    digitalInputs: boolean[];
    digitalOutputs: boolean[];
    dc24vOutputs: boolean[];
    analogOutputs: number[];
    aoModes: ('voltage' | 'current')[];
    vfdActivity: boolean;
    sensorActivity: boolean;
  } = {
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
    aoModes: ['voltage', 'voltage'],
    vfdActivity: false,
    sensorActivity: false,
  };

  export let onDigitalInputToggle: (index: number) => void = () => {};
  export let onEStopToggle: () => void = () => {};

  // Board dimensions (SVG viewBox units, roughly mm scale)
  const W = 550;  // 5.5" ≈ 140mm → 550 svg units
  const H = 450;  // 4.5" ≈ 114mm → 450 svg units

  // Colors
  const PCB = '#1a5276';           // dark teal PCB
  const PCB_EDGE = '#0e3650';
  const COPPER = '#c59d5f';
  const LED_OFF = '#2c3e50';
  const LED_GREEN = '#2ecc71';
  const LED_RED = '#e74c3c';
  const LED_AMBER = '#f39c12';
  const LED_BLUE = '#3498db';
  const COMPONENT = '#34495e';
  const CONNECTOR = '#1c2833';
  const LABEL = '#ecf0f1';
  const SILK = '#d5dbdb';
  const FUSE_OK = '#27ae60';
  const FUSE_BLOWN = '#e74c3c';

  function formatUptime(s: number): string {
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    return `${h}h ${m}m`;
  }

  function formatAO(value: number, mode: 'voltage' | 'current'): string {
    if (mode === 'voltage') return `${(value / 1000).toFixed(1)}V`;
    return `${(value / 1000).toFixed(1)}mA`;
  }

  // Activity LED blink state
  let blinkOn = true;
  let blinkInterval: ReturnType<typeof setInterval>;
  import { onMount, onDestroy } from 'svelte';

  onMount(() => {
    blinkInterval = setInterval(() => { blinkOn = !blinkOn; }, 500);
  });
  onDestroy(() => { if (blinkInterval) clearInterval(blinkInterval); });

  // Dipswitch positions from board ID
  $: dipswitches = [
    (status.id >> 0) & 1,
    (status.id >> 1) & 1,
    (status.id >> 2) & 1,
    (status.id >> 3) & 1,
    (status.id >> 4) & 1,
  ];
</script>

<div class="orbit-board-container">
  <svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg" class="orbit-board">

    <!-- PCB Background -->
    <rect x="0" y="0" width={W} height={H} rx="8" ry="8"
          fill={PCB} stroke={PCB_EDGE} stroke-width="3"/>

    <!-- Mounting holes (4 corners) -->
    {#each [[18,18],[W-18,18],[18,H-18],[W-18,H-18]] as [cx,cy]}
      <circle {cx} {cy} r="8" fill="none" stroke={COPPER} stroke-width="2"/>
      <circle {cx} {cy} r="3" fill={PCB_EDGE}/>
    {/each}

    <!-- ═══════════════════ TOP ROW ═══════════════════ -->

    <!-- RJ45 #1 (left) — TCP/IP Modbus -->
    <rect x="40" y="5" width="55" height="35" rx="3" fill={CONNECTOR}
          stroke="#555" stroke-width="1"/>
    <text x="67" y="25" text-anchor="middle" fill={LABEL} font-size="7"
          font-family="monospace">RJ45</text>
    <text x="67" y="50" text-anchor="middle" fill={SILK} font-size="6">TCP/IP</text>
    <!-- Link LED -->
    <circle cx="50" cy="14" r="3"
            fill={status.commLost ? LED_OFF : LED_GREEN}/>
    <!-- Activity LED -->
    <circle cx="83" cy="14" r="3"
            fill={!status.commLost && blinkOn ? LED_AMBER : LED_OFF}/>

    <!-- 24V DC Outputs 5-8 (top center) -->
    {#each [0,1,2,3] as i}
      {@const x = 160 + i * 55}
      <rect x={x} y="8" width="42" height="28" rx="2"
            fill={CONNECTOR} stroke="#555" stroke-width="1"/>
      <text x={x + 21} y="26" text-anchor="middle" fill={LABEL}
            font-size="8" font-weight="bold">{i + 5}</text>
      <!-- Status LED -->
      <circle cx={x + 21} cy="42" r="4"
              fill={status.dc24vOutputs[i] ? LED_GREEN : LED_OFF}/>
    {/each}
    <text x="270" y="55" text-anchor="middle" fill={SILK} font-size="6">
      24V DC 1A Outputs
    </text>

    <!-- RJ45 #2 (right) — TCP/IP Modbus -->
    <rect x={W - 95} y="5" width="55" height="35" rx="3" fill={CONNECTOR}
          stroke="#555" stroke-width="1"/>
    <text x={W - 67} y="25" text-anchor="middle" fill={LABEL} font-size="7"
          font-family="monospace">RJ45</text>
    <text x={W - 67} y="50" text-anchor="middle" fill={SILK} font-size="6">TCP/IP</text>
    <circle cx={W - 85} cy="14" r="3"
            fill={status.commLost ? LED_OFF : LED_GREEN}/>
    <circle cx={W - 52} cy="14" r="3"
            fill={!status.commLost && blinkOn ? LED_AMBER : LED_OFF}/>

    <!-- 24V DC In (top right corner) -->
    <rect x={W - 40} y="55" width="30" height="50" rx="2"
          fill={CONNECTOR} stroke={COPPER} stroke-width="1"/>
    <text x={W - 25} y="78" text-anchor="middle" fill={LED_AMBER}
          font-size="6" font-weight="bold">24V</text>
    <text x={W - 25} y="88" text-anchor="middle" fill={LED_AMBER}
          font-size="5">DC in</text>

    <!-- ═══════════════════ LEFT SIDE — Digital Outputs 1-10 ═══════════════════ -->

    <text x="8" y="78" fill={SILK} font-size="7" font-weight="bold">DO</text>
    {#each Array(10) as _, i}
      {@const y = 85 + i * 33}
      <!-- Output terminal -->
      <rect x="8" y={y} width="30" height="22" rx="2"
            fill={CONNECTOR} stroke="#555" stroke-width="1"/>
      <text x="23" y={y + 15} text-anchor="middle" fill={LABEL}
            font-size="8" font-weight="bold">{i + 1}</text>
      <!-- Status LED + fuse indicator -->
      <circle cx="48" cy={y + 11} r="5"
              fill={status.digitalOutputs[i] ? LED_GREEN : LED_OFF}
              stroke="#333" stroke-width="1"/>
      <!-- Self-resetting fuse (small bar) -->
      <rect x="56" y={y + 6} width="12" height="10" rx="1"
            fill={FUSE_OK} stroke="#333" stroke-width="0.5"/>
      <text x="62" y={y + 14} text-anchor="middle" fill="#fff"
            font-size="5">F</text>
    {/each}

    <!-- ═══════════════════ RIGHT SIDE — E-Stop + Digital Inputs 1-10 ═══════════════════ -->

    <!-- E-Stop Button -->
    <g class="estop-btn" on:click={onEStopToggle} on:keydown={onEStopToggle}
       role="button" tabindex="0" style="cursor: pointer;">
      <circle cx={W - 25} cy="125" r="18"
              fill={status.eStop ? LED_RED : '#7b241c'} stroke="#222" stroke-width="2"/>
      <circle cx={W - 25} cy="125" r="12"
              fill={status.eStop ? '#ff4444' : '#922b21'} stroke="#333" stroke-width="1"/>
      <text x={W - 25} y="121" text-anchor="middle" fill="#fff"
            font-size="6" font-weight="bold">E</text>
      <text x={W - 25} y="131" text-anchor="middle" fill="#fff"
            font-size="5">STOP</text>
    </g>
    {#if status.eStop}
      <text x={W - 25} y="152" text-anchor="middle" fill={LED_RED}
            font-size="6" font-weight="bold">ACTIVE</text>
    {/if}

    <!-- DI 1-10 -->
    <text x={W - 8} y="168" text-anchor="end" fill={SILK} font-size="7"
          font-weight="bold">DI</text>
    {#each Array(10) as _, i}
      {@const y = 175 + i * 25}
      <!-- Input terminal (clickable) -->
      <g class="di-btn" on:click={() => onDigitalInputToggle(i)}
         on:keydown={() => onDigitalInputToggle(i)}
         role="button" tabindex="0" style="cursor: pointer;">
        <rect x={W - 38} y={y} width="30" height="18" rx="2"
              fill={status.digitalInputs[i] ? '#1a6b3c' : CONNECTOR}
              stroke={status.digitalInputs[i] ? LED_GREEN : '#555'}
              stroke-width="1.5"/>
        <text x={W - 23} y={y + 13} text-anchor="middle" fill={LABEL}
              font-size="8" font-weight="bold">{i + 1}</text>
      </g>
      <!-- Status LED -->
      <circle cx={W - 48} cy={y + 9} r="4"
              fill={status.digitalInputs[i] ? LED_GREEN : LED_OFF}
              stroke="#333" stroke-width="0.5"/>
    {/each}

    <!-- ═══════════════════ CENTER — Dipswitches ═══════════════════ -->

    <rect x="175" y="68" width="120" height="28" rx="3"
          fill="#2c3e50" stroke={COPPER} stroke-width="1"/>
    <text x="235" y="83" text-anchor="middle" fill={SILK} font-size="6">
      DIPSWITCHES
    </text>
    {#each dipswitches as sw, i}
      {@const sx = 188 + i * 22}
      <rect x={sx} y="72" width="10" height="20" rx="1"
            fill="#1a252f" stroke="#555" stroke-width="0.5"/>
      <!-- Switch position: ON=up, OFF=down -->
      <rect x={sx + 1} y={sw ? 73 : 83} width="8" height="8" rx="1"
            fill={sw ? LED_AMBER : '#555'}/>
      <text x={sx + 5} y="99" text-anchor="middle" fill={SILK} font-size="5">
        {i + 1}
      </text>
    {/each}
    <text x="235" y="108" text-anchor="middle" fill={SILK} font-size="5.5">
      IP: {status.ipAddress}
    </text>

    <!-- ═══════════════════ CENTER — AM2432 MCU ═══════════════════ -->

    <rect x="160" y="130" width="150" height="90" rx="4"
          fill="#1c2833" stroke={COPPER} stroke-width="1.5"/>
    <!-- Pin rows (decorative) -->
    {#each Array(16) as _, i}
      <rect x={163 + i * 9} y="126" width="4" height="4" fill={COPPER}/>
      <rect x={163 + i * 9} y="220" width="4" height="4" fill={COPPER}/>
    {/each}
    {#each Array(10) as _, i}
      <rect x="156" y={134 + i * 8} width="4" height="4" fill={COPPER}/>
      <rect x={310} y={134 + i * 8} width="4" height="4" fill={COPPER}/>
    {/each}
    <text x="235" y="160" text-anchor="middle" fill={LABEL}
          font-size="10" font-weight="bold">AM2432</text>
    <text x="235" y="175" text-anchor="middle" fill={SILK} font-size="7">
      {status.safeMode ? 'SAFE MODE' : `v${status.firmwareVersion}`}
    </text>
    <text x="235" y="188" text-anchor="middle" fill={SILK} font-size="6">
      {status.cpuTemp}°C | {formatUptime(status.uptime)}
    </text>
    <!-- Status indicator -->
    <circle cx="295" cy="145" r="5"
            fill={status.commLost ? (blinkOn ? LED_RED : LED_OFF) :
                  status.safeMode ? LED_AMBER : LED_GREEN}/>

    <!-- ═══════════════════ CENTER — Analog Hard Connection ═══════════════════ -->

    <rect x="160" y="240" width="150" height="45" rx="3"
          fill={COMPONENT} stroke={COPPER} stroke-width="1"
          stroke-dasharray="4,2"/>
    <text x="235" y="260" text-anchor="middle" fill={SILK} font-size="7">
      Analog Hard Connection
    </text>
    <text x="235" y="273" text-anchor="middle" fill={SILK} font-size="5.5">
      Stackable
    </text>

    <!-- ═══════════════════ BOTTOM — RS-485 Ports ═══════════════════ -->

    <!-- RS-485 Port 1 — VFD RTU -->
    <rect x="80" y={H - 65} width="70" height="35" rx="3"
          fill={CONNECTOR} stroke="#555" stroke-width="1"/>
    <text x="115" y={H - 44} text-anchor="middle" fill={LABEL}
          font-size="7" font-weight="bold">1</text>
    <text x="115" y={H - 24} text-anchor="middle" fill={SILK}
          font-size="5.5">VFD RTU</text>
    <!-- Activity LED -->
    <circle cx="138" cy={H - 53} r="3"
            fill={status.vfdActivity && blinkOn ? LED_BLUE : LED_OFF}/>

    <!-- RS-485 Port 2 — Sensor/Contactor RTU -->
    <rect x="170" y={H - 65} width="70" height="35" rx="3"
          fill={CONNECTOR} stroke="#555" stroke-width="1"/>
    <text x="205" y={H - 44} text-anchor="middle" fill={LABEL}
          font-size="7" font-weight="bold">2</text>
    <text x="205" y={H - 24} text-anchor="middle" fill={SILK}
          font-size="5.5">Sensor RTU</text>
    <circle cx="228" cy={H - 53} r="3"
            fill={status.sensorActivity && blinkOn ? LED_BLUE : LED_OFF}/>

    <!-- ═══════════════════ BOTTOM — Analog Outputs ═══════════════════ -->

    {#each [0, 1] as ch}
      {@const ax = 320 + ch * 95}
      <rect x={ax} y={H - 70} width="80" height="45" rx="3"
            fill={COMPONENT} stroke={COPPER} stroke-width="1"/>
      <text x={ax + 40} y={H - 52} text-anchor="middle" fill={LABEL}
            font-size="7" font-weight="bold">AO {ch + 1}</text>
      <!-- Value bar -->
      {@const maxVal = status.aoModes[ch] === 'voltage' ? 10000 : 20000}
      {@const pct = Math.min(100, (status.analogOutputs[ch] / maxVal) * 100)}
      <rect x={ax + 5} y={H - 44} width="70" height="8" rx="2"
            fill="#0d1117"/>
      <rect x={ax + 5} y={H - 44} width={Math.max(0, 70 * pct / 100)}
            height="8" rx="2" fill={LED_GREEN}/>
      <text x={ax + 40} y={H - 28} text-anchor="middle" fill={SILK}
            font-size="6">
        {formatAO(status.analogOutputs[ch], status.aoModes[ch])}
        ({status.aoModes[ch] === 'voltage' ? '0-10V' : '4-20mA'})
      </text>
    {/each}

    <!-- ═══════════════════ SILKSCREEN LABELS ═══════════════════ -->

    <text x={W / 2} y={H - 8} text-anchor="middle" fill={SILK}
          font-size="6" opacity="0.6">
      Constellation Orbit — {status.ipAddress}
    </text>

  </svg>
</div>

<style>
  .orbit-board-container {
    width: 100%;
    max-width: 560px;
    margin: 0 auto;
  }

  .orbit-board {
    width: 100%;
    height: auto;
    filter: drop-shadow(0 4px 12px rgba(0, 0, 0, 0.4));
    border-radius: 8px;
  }

  .estop-btn:hover circle {
    filter: brightness(1.3);
  }

  .di-btn:hover rect {
    filter: brightness(1.3);
  }

  /* LED glow effects */
  circle[fill="#2ecc71"] {
    filter: drop-shadow(0 0 4px #2ecc71);
  }
  circle[fill="#e74c3c"], circle[fill="#ff4444"] {
    filter: drop-shadow(0 0 4px #e74c3c);
  }
  circle[fill="#f39c12"] {
    filter: drop-shadow(0 0 3px #f39c12);
  }
  circle[fill="#3498db"] {
    filter: drop-shadow(0 0 3px #3498db);
  }
</style>
