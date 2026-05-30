<script lang="ts">
  /*
   * Level 2 — PWM 4-20 mA Output Setup
   *
   * Programs which equipment each connected Orbit board's analog
   * outputs (AO1, AO2 → 0-10 V / 4-20 mA) drives. Persists to firmware
   * OSPI Settings.AoEquip[slot][channel].
   *
   * Data path:
   *   ─ Read:  orbitStatus proto stream (envelope tag 120). Each board
   *            carries `aoEquip: number[]` (length 2) populated by
   *            firmware nova_messages.c::encode_orbit_board_status
   *            from Settings.AoEquip[slot][0..1].
   *   ─ Write: POST /iot/orbits/aoequip {slot, channel, equip}.
   *            Bridge forwards via NovaSerialBridge.sendAoEquipAssign
   *            (MSG_AO_EQUIP_ASSIGN = 123). Firmware ack-saves to
   *            OSPI; nova_fan_output_tick picks up the new program
   *            on its next 1 Hz pass.
   *
   * Equipment selectors are forward-compatible: any operator pick
   * outside the listed set is accepted by firmware (unknown selectors
   * are silently ignored by nova_fan_output_tick), so adding a new
   * AO_EQUIP_* enum value only requires extending EQUIP_OPTIONS below.
   */
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Card from "$lib/ui/Card.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Select from "$lib/ui/Select.svelte";
  import { onMount } from "svelte";
  import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { t } from "svelte-i18n";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import { orbitStatus } from "$lib/business/protoStores";
  import { OrbitRole } from "$proto/agristar/orbit.js";

  // ao_equip_t (mirrors Nova_Firmware/Platform/nova_fan_output.h).
  // 0 is the default unprogrammed value — the operator may legitimately
  // re-pick it to clear an AO, so it MUST appear in the dropdown.
  // Each non-zero selector mirrors one of the four legacy PWM channels
  // (PWM_FAN/PWM_DOORS/PWM_REFRIGERATION/PWM_BURNER) onto the orbit AO
  // at the controller's PID update cadence (legacy default 3 s for
  // door/refrig/burner, FanSpeedSettings.UpdatePeriod hours for fan).
  const EQUIP_OPTIONS = [
    { text: '— unused —',     value: '0' },
    { text: 'Fan Speed',      value: '1' },
    { text: 'Doors',          value: '2' },
    { text: 'Refrigeration',  value: '3' },
    { text: 'Burner',         value: '4' },
  ];

  let edit = true;
  let title = $t('level2.pwm.4-20ma-pwm-output-setup');
  let ready = false;
  let wait = false;

  // Storage-role boards we render rows for. Each board contributes 2 AO
  // rows. We keep `slot` (firmware OSPI index) for the POST body; the
  // displayed "Orbit N" number is just the 1-based position in the list.
  type StorageBoard = { slot: number; dipswitchId: number; aoEquip: number[] };
  let boards: StorageBoard[] = [];

  onMount(() => {
    $navigationStore.data = '';
    // No SaveButton on this page — every dropdown change is its own
    // atomic POST. Mark page as never-dirty so unsaved-changes guard
    // doesn't fire on swipe-away.
    $navigationStore.isDirty = () => false;

    const unsub = orbitStatus.subscribe((view) => {
      if (!view) return;
      boards = view.boards
        .filter((b) => b.connected && b.role === OrbitRole.ORBIT_ROLE_STORAGE_PB)
        .sort((a, z) => a.slot - z.slot)
        .map((b) => ({
          slot: b.slot,
          dipswitchId: b.dipswitchId,
          // ts-proto repeated uint32 may decode as undefined when wire
          // length is 0 — guard with []. Pad to length 2 so we always
          // have AO1/AO2 entries to render.
          aoEquip: [
            (b.aoEquip?.[0] ?? 0) | 0,
            (b.aoEquip?.[1] ?? 0) | 0,
          ],
        }));
      ready = true;
    });
    return () => unsub();
  });

  async function onPick(slot: number, channel: number, ev: Event) {
    const equip = parseInt((ev.target as HTMLSelectElement).value, 10);
    if (!Number.isFinite(equip)) return;
    wait = true;
    try {
      const resp = await fetch(getHttpUrl('/iot/orbits/aoequip'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ slot, channel, equip }),
      });
      if (!resp.ok) {
        const err = await resp.text();
        console.error('[pwm] AO equip assign failed:', resp.status, err);
        // Revert UI state from the cached orbitStatus on failure.
        // Next firmware OrbitStatus broadcast will reconcile within ~1 s.
      }
    } catch (e) {
      console.error('[pwm] AO equip assign error:', e);
    } finally {
      wait = false;
    }
  }
</script>

<GellertPage {wait} {title} level={2} {ready} name="pwm">
  <Card class="xl:w-3/4 md:mx-2 xl:mx-auto flex flex-col">
    <ScrollableArea>
      {#if boards.length === 0}
        <div class="p-4 text-center text-gray-600">
          {$t('global.none')}
        </div>
      {:else}
        {#each boards as b, i (b.slot)}
          <Table class="mb-2 text-size-xl">
            <Row>
              <Column class="w-1/4 border-r border-gray-400 py-2 font-bold">
                Orbit {i + 1}
              </Column>
              <Column class="w-3/4 py-2 font-bold">
                {$t('level2.pwm.4-20-output')}
              </Column>
            </Row>
            {#each [0, 1] as ch}
              <Row>
                <Column class="w-1/4 my-2 border-r border-gray-400">
                  AO{ch + 1}
                </Column>
                <Column class="w-3/4 my-2">
                  <div class="px-2">
                    <Select
                      class="mx-8"
                      inline={false}
                      size="xl"
                      value={String(b.aoEquip[ch] ?? 0)}
                      options={EQUIP_OPTIONS}
                      {edit}
                      on:change={(ev) => onPick(b.slot, ch, ev)}
                    />
                  </div>
                </Column>
              </Row>
            {/each}
          </Table>
        {/each}
      {/if}
    </ScrollableArea>
  </Card>
</GellertPage>
