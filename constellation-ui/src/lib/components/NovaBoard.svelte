<script lang="ts">
  /**
   * NovaBoard.svelte — SVG visualization of the Constellation Nova controller
   *
   * Physical board: 4.5" × 3.5"
   *   • AM2434 MCU (center)
   *   • RPi 5 with SSD Hat (left side)
   *   • RJ45 TCP/IP Modbus (top-left, connects to Orbit network)
   *   • Debug port (top-center)
   *   • JTAG (right side)
   *   • CR2032 RTC (center-right)
   *   • DC In/Out (right side)
   *   • UART serial link to RPi5 (internal)
   */

  export let tcpActive = false;
  export let uartActive = false;
  export let connected = false;
  export let firmwareVersion = '1.0.0';
  export let uptime = 0;

  // Board dimensions (SVG units ≈ 10× mils)
  const W = 450;  // 4.5"
  const H = 350;  // 3.5"

  // Colors — match Orbit palette
  const PCB = '#1a5276';
  const PCB_EDGE = '#0e3650';
  const COPPER = '#c59d5f';
  const LED_OFF = '#2c3e50';
  const LED_GREEN = '#2ecc71';
  const LED_RED = '#e74c3c';
  const LED_AMBER = '#f39c12';
  const COMPONENT = '#34495e';
  const CONNECTOR = '#1c2833';
  const LABEL = '#ecf0f1';
  const SILK = '#d5dbdb';

  import { onMount, onDestroy } from 'svelte';

  let blinkOn = true;
  let blinkInterval: ReturnType<typeof setInterval>;

  onMount(() => {
    blinkInterval = setInterval(() => { blinkOn = !blinkOn; }, 500);
  });
  onDestroy(() => { if (blinkInterval) clearInterval(blinkInterval); });

  function formatUptime(s: number): string {
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    return `${h}h ${m}m`;
  }
</script>

<div class="nova-board-container">
  <svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg" class="nova-board">

    <!-- PCB Background -->
    <rect x="0" y="0" width={W} height={H} rx="8" ry="8"
          fill={PCB} stroke={PCB_EDGE} stroke-width="3"/>

    <!-- Mounting holes -->
    {#each [[16,16],[W-16,16],[16,H-16],[W-16,H-16]] as [cx,cy]}
      <circle {cx} {cy} r="7" fill="none" stroke={COPPER} stroke-width="2"/>
      <circle {cx} {cy} r="2.5" fill={PCB_EDGE}/>
    {/each}

    <!-- ═══════════ TOP ROW ═══════════ -->

    <!-- RJ45 TCP/IP Modbus (top-left) — link to Orbit network -->
    <rect x="35" y="5" width="55" height="35" rx="3" fill={CONNECTOR}
          stroke="#555" stroke-width="1"/>
    <text x="62" y="18" text-anchor="middle" fill={LABEL} font-size="7"
          font-family="monospace">RJ45</text>
    <text x="62" y="28" text-anchor="middle" fill={SILK} font-size="5.5">TCP/IP</text>
    <!-- Link LED -->
    <circle cx="45" cy="12" r="3"
            fill={connected ? LED_GREEN : LED_OFF}/>
    <!-- Activity LED — blinks on Modbus TCP traffic -->
    <circle cx="79" cy="12" r="3"
            fill={tcpActive && blinkOn ? LED_AMBER : LED_OFF}/>
    <text x="62" y="48" text-anchor="middle" fill={SILK} font-size="5">
      Modbus TCP
    </text>

    <!-- Debug Port (top-center) -->
    <rect x="160" y="8" width="80" height="25" rx="3" fill={CONNECTOR}
          stroke="#555" stroke-width="1"/>
    <text x="200" y="25" text-anchor="middle" fill={LABEL} font-size="7">Debug</text>

    <!-- JTAG (top-right) -->
    <rect x={W - 75} y="8" width="45" height="30" rx="2" fill={CONNECTOR}
          stroke={COPPER} stroke-width="1"/>
    <text x={W - 52} y="27" text-anchor="middle" fill={LABEL} font-size="7">JTAG</text>

    <!-- ═══════════ LEFT SIDE — RPi 5 with SSD Hat ═══════════ -->

    <rect x="30" y="70" width="130" height="160" rx="5"
          fill="#0d1117" stroke={COPPER} stroke-width="1.5"/>
    <!-- Decorative GPIO pins -->
    {#each Array(20) as _, i}
      <rect x={33 + i * 6} y="66" width="3" height="4" fill={COPPER}/>
    {/each}
    <text x="95" y="120" text-anchor="middle" fill={LABEL}
          font-size="11" font-weight="bold">RPi 5</text>
    <text x="95" y="138" text-anchor="middle" fill={SILK} font-size="7">
      SSD Hat
    </text>
    <text x="95" y="155" text-anchor="middle" fill={SILK} font-size="6">
      BTRFS RAID1
    </text>

    <!-- UART link indicator (between RPi5 and AM2434) -->
    <line x1="160" y1="150" x2="190" y2="150" stroke={COPPER}
          stroke-width="1.5" stroke-dasharray="4,2"/>
    <!-- UART activity LED -->
    <circle cx="175" cy="142" r="3.5"
            fill={uartActive && blinkOn ? LED_GREEN : LED_OFF}/>
    <text x="175" y="138" text-anchor="middle" fill={SILK} font-size="4.5">
      UART
    </text>

    <!-- ═══════════ CENTER — AM2434 MCU ═══════════ -->

    <rect x="195" y="80" width="140" height="100" rx="4"
          fill="#1c2833" stroke={COPPER} stroke-width="1.5"/>
    <!-- Pin rows -->
    {#each Array(14) as _, i}
      <rect x={198 + i * 9.5} y="76" width="4" height="4" fill={COPPER}/>
      <rect x={198 + i * 9.5} y="180" width="4" height="4" fill={COPPER}/>
    {/each}
    {#each Array(10) as _, i}
      <rect x="191" y={84 + i * 9.5} width="4" height="4" fill={COPPER}/>
      <rect x="335" y={84 + i * 9.5} width="4" height="4" fill={COPPER}/>
    {/each}
    <text x="265" y="118" text-anchor="middle" fill={LABEL}
          font-size="12" font-weight="bold">AM2434</text>
    <text x="265" y="136" text-anchor="middle" fill={SILK} font-size="7">
      v{firmwareVersion}
    </text>
    <text x="265" y="150" text-anchor="middle" fill={SILK} font-size="6">
      {formatUptime(uptime)}
    </text>
    <!-- Heartbeat LED -->
    <circle cx="320" cy="92" r="4"
            fill={connected ? (blinkOn ? LED_GREEN : '#1a6b3c') : LED_RED}/>

    <!-- ═══════════ RIGHT SIDE ═══════════ -->

    <!-- CR2032 RTC (circle) -->
    <circle cx={W - 55} cy="110" r="28" fill="#2c3e50" stroke={COPPER} stroke-width="1.5"/>
    <circle cx={W - 55} cy="110" r="22" fill="#1a252f" stroke="#555" stroke-width="0.5"/>
    <text x={W - 55} y="107" text-anchor="middle" fill={LABEL} font-size="7"
          font-weight="bold">CR2032</text>
    <text x={W - 55} y="118" text-anchor="middle" fill={SILK} font-size="6">RTC</text>

    <!-- DC In/Out (right edge) -->
    <rect x={W - 40} y="155" width="30" height="60" rx="2"
          fill={CONNECTOR} stroke={COPPER} stroke-width="1"/>
    <text x={W - 25} y="178" text-anchor="middle" fill={LED_AMBER}
          font-size="6" font-weight="bold">DC</text>
    <text x={W - 25} y="190" text-anchor="middle" fill={LED_AMBER}
          font-size="5">In/Out</text>

    <!-- ═══════════ BOTTOM AREA ═══════════ -->

    <!-- OSPI Firmware Store -->
    <rect x="195" y="200" width="140" height="40" rx="3"
          fill={COMPONENT} stroke={COPPER} stroke-width="1"/>
    <text x="265" y="218" text-anchor="middle" fill={SILK} font-size="6.5">
      OSPI Firmware Store
    </text>
    <text x="265" y="230" text-anchor="middle" fill={SILK} font-size="5">
      A/B Failover
    </text>

    <!-- QSPI Settings Store -->
    <rect x="195" y="248" width="140" height="35" rx="3"
          fill={COMPONENT} stroke={COPPER} stroke-width="1"/>
    <text x="265" y="264" text-anchor="middle" fill={SILK} font-size="6.5">
      QSPI Settings
    </text>
    <text x="265" y="275" text-anchor="middle" fill={SILK} font-size="5">
      Sector A/B + CRC32
    </text>

    <!-- ═══════════ BOTTOM LABEL ═══════════ -->

    <text x={W / 2} y={H - 12} text-anchor="middle" fill={SILK}
          font-size="7" opacity="0.6">
      Constellation Nova — 10.47.27.1
    </text>

  </svg>
</div>

<style>
  .nova-board-container {
    width: 100%;
    max-width: 380px;
    margin: 0 auto;
  }

  .nova-board {
    width: 100%;
    height: auto;
    filter: drop-shadow(0 4px 12px rgba(0, 0, 0, 0.4));
    border-radius: 8px;
  }

  /* LED glow effects */
  circle[fill="#2ecc71"] {
    filter: drop-shadow(0 0 4px #2ecc71);
  }
  circle[fill="#e74c3c"] {
    filter: drop-shadow(0 0 4px #e74c3c);
  }
  circle[fill="#f39c12"] {
    filter: drop-shadow(0 0 3px #f39c12);
  }
</style>
