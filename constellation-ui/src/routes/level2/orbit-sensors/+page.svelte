<!--
  /level2/orbit-sensors — Phase E debug page.

  Shows raw orbit holding-register banks (HR[200..263] per orbit) as
  reported by the LP firmware via OrbitSensorBank (envelope tag 124).
  No engineering-unit conversion — values are uint16 raw. Used to verify
  the firmware → bridge → UI proto path is intact before wiring real
  sensor pages onto the bank.
-->
<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Card from "$lib/ui/Card.svelte";
  import Column from "$lib/ui/Column.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import { orbitSensorBanks, orbitStatus } from "$lib/business/protoStores";

  const title = "Orbit Sensor Banks (debug)";
  $: ready = $orbitSensorBanks.size > 0;

  // Sort by slot then hr_base for stable display. Phase 4b 2026-06-01:
  // one orbit may have multiple banks (sensor + role window), so the
  // tiebreaker shows them in ascending HR-address order.
  $: sortedBanks = Array.from($orbitSensorBanks.values()).sort(
    (a, b) => a.slot - b.slot || a.hrBase - b.hrBase
  );

  // Helper: lookup connected/role from OrbitStatus for context.
  $: boardBySlot = new Map(
    ($orbitStatus?.boards ?? []).map((b) => [b.slot, b])
  );

  function roleName(role: number | undefined): string {
    switch (role) {
      case 0: return "UNASSIGNED";
      case 1: return "STORAGE";
      case 2: return "DOOR";
      case 3: return "REFRIG";
      default: return `?${role}`;
    }
  }
</script>

<GellertPage {title} {ready} level={2} name="orbit-sensors">
  <ScrollableArea>
    <Column gap="0.5rem">
      {#each sortedBanks as bank (bank.slot)}
        {@const board = boardBySlot.get(bank.slot)}
        <Card>
          <header style="display:flex; gap:1rem; align-items:baseline;">
            <strong>Slot {bank.slot}</strong>
            <span>HR base {bank.hrBase}</span>
            <span>seq {bank.seq}</span>
            {#if board}
              <span>dip={board.dipswitchId}</span>
              <span>role={roleName(board.role)}</span>
              <span>{board.connected ? "✓ connected" : "✗ offline"}</span>
            {/if}
          </header>
          <!--
            Render values in 8 columns × 8 rows. Address column shows the
            absolute HR address so an operator can correlate with the
            orbit Modbus map (e.g. HR 200..209 = sensor reading 1..10).
          -->
          <table style="font-family:monospace; font-size:0.8rem; border-collapse:collapse;">
            <thead>
              <tr>
                <th style="text-align:right; padding:0 0.5em;">addr</th>
                {#each Array(8) as _, c}
                  <th style="text-align:right; padding:0 0.5em;">+{c}</th>
                {/each}
              </tr>
            </thead>
            <tbody>
              {#each Array(Math.ceil(bank.values.length / 8)) as _, row}
                <tr>
                  <td style="text-align:right; padding:0 0.5em; opacity:0.7;">
                    {bank.hrBase + row * 8}
                  </td>
                  {#each Array(8) as _, col}
                    {@const idx = row * 8 + col}
                    {#if idx < bank.values.length}
                      <td
                        style="text-align:right; padding:0 0.5em;"
                        class:zero={bank.values[idx] === 0}
                      >
                        {bank.values[idx]}
                      </td>
                    {:else}
                      <td></td>
                    {/if}
                  {/each}
                </tr>
              {/each}
            </tbody>
          </table>
        </Card>
      {:else}
        <Card>
          <p>Waiting for OrbitSensorBank frames (envelope tag 124)…</p>
        </Card>
      {/each}
    </Column>
  </ScrollableArea>
</GellertPage>

<style>
  .zero { opacity: 0.4; }
</style>
