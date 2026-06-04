<script lang="ts">
  // Alarm audio + persistent banner.
  //
  // Triggers when `systemState` enters a "critical" set: ST_FAILURE
  // (11), ST_SHUTDOWN (33), ST_REMOTE_STANDBY (32 — operator-pushed
  // stop). Beeps once per state-change (not continuously) and shows
  // a fixed banner along the top of the viewport until state clears
  // or the operator mutes. Mute persists for the session via
  // localStorage; the banner stays visible (mute only silences the
  // sound).
  //
  // Audio: synthesized via the WebAudio API so we don't need to
  // ship a .mp3 asset. 880 Hz, square wave, 350 ms — bright "alarm
  // beep" timbre.
  import { onMount, onDestroy } from "svelte";

  export let systemState: number = 0;
  export let label: string = '';

  const CRITICAL = new Set([11, 33, 32, 20]); // FAILURE / SHUTDOWN / REMOTE_STANDBY / SYS_REMOTEOFF
  $: critical = CRITICAL.has(systemState);

  let muted = false;
  let lastState = -1;
  let audioCtx: AudioContext | null = null;

  onMount(() => {
    muted = localStorage.getItem('dashboard.alarm.muted') === '1';
  });

  onDestroy(() => {
    audioCtx?.close();
  });

  // State-change detector — beep once when entering critical.
  $: if (typeof window !== 'undefined' && systemState !== lastState) {
    if (CRITICAL.has(systemState) && !CRITICAL.has(lastState)) {
      void beep();
    }
    lastState = systemState;
  }

  async function beep() {
    if (muted) return;
    try {
      if (!audioCtx) audioCtx = new AudioContext();
      // Browsers require a user-gesture-resumed context. If we hit
      // suspended state, swallow silently — the visual banner still
      // shows, and the next operator interaction unblocks future beeps.
      if (audioCtx.state === 'suspended') {
        try { await audioCtx.resume(); } catch {}
      }
      const t0 = audioCtx.currentTime;
      // Three 100 ms blips so it sounds like an alarm chirp.
      for (let i = 0; i < 3; i++) {
        const osc = audioCtx.createOscillator();
        const gain = audioCtx.createGain();
        osc.type = 'square';
        osc.frequency.value = 880;
        gain.gain.value = 0.0;
        gain.gain.setValueAtTime(0.0, t0 + i * 0.15);
        gain.gain.linearRampToValueAtTime(0.18, t0 + i * 0.15 + 0.01);
        gain.gain.linearRampToValueAtTime(0.0, t0 + i * 0.15 + 0.1);
        osc.connect(gain).connect(audioCtx.destination);
        osc.start(t0 + i * 0.15);
        osc.stop(t0 + i * 0.15 + 0.12);
      }
    } catch {
      // Some browsers throw before any user gesture has occurred.
      // No-op: the visual banner is the source of truth.
    }
  }

  function toggleMute() {
    muted = !muted;
    try {
      localStorage.setItem('dashboard.alarm.muted', muted ? '1' : '0');
    } catch {}
  }
</script>

{#if critical}
  <div class="fixed top-0 left-0 right-0 z-[60] bg-red-600 text-white px-4 py-2 flex items-center justify-between shadow-lg"
       role="alert" aria-live="assertive">
    <div class="flex items-center gap-3">
      <span class="text-2xl alarm-pulse">⚠</span>
      <div>
        <div class="text-base font-bold leading-tight">ALARM · {label}</div>
        <div class="text-xs opacity-90">
          System has entered a critical state. Investigate equipment + clear from Equipment Control.
        </div>
      </div>
    </div>
    <button class="bg-red-800 hover:bg-red-900 px-3 py-1 rounded font-bold text-sm"
            on:click={toggleMute}
            title={muted ? 'Unmute alarm sound' : 'Mute alarm sound'}>
      {muted ? '🔇 Muted' : '🔊 Mute'}
    </button>
  </div>
{/if}

<style>
  .alarm-pulse {
    animation: alarm-pulse 0.8s ease-in-out infinite;
    display: inline-block;
  }
  @keyframes alarm-pulse {
    0%, 100% { transform: scale(1.0); opacity: 1.0; }
    50%      { transform: scale(1.25); opacity: 0.7; }
  }
</style>
