<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import Select from "$lib/ui/Select.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { frontMatterStore, navigationStore } from "$lib/store";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import {
    plenumSettings,
    tempAlarmSettings,
    rampRateSettings,
    sensorList as sensorListStore,
    systemStatus,
  } from "$lib/business/protoStores";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";
  import { SensorTypes } from "$lib/business/analog";

  // READ via PlenumSettings (tag 40) + TempAlarmSettings (tag 55) +
  // RampRateSettings (tag 43) proto stores. Save still POSTs to
  // /iot/plensetup → legacyShim (one round-trip pushes both PlenumStore
  // + TempDevAlarmStore). Ramp save uses direct proto write.
  // Ramp section was promoted from /level1/ramp — see git history for the
  // standalone page if you need to compare.

  let title = $t('level1.plentemp.plenum-setpoints-and-alarms');

  let validation = {
    'PlenumTempSet': '',
    'PlenumHumidSet': '',
    'CureTempLowLimit': '',
    'AlarmTempLow': '',
    'AlarmMinLow': '',
    'CureTimeHighLimit': '',
    'AlarmTempHigh': '',
    'AlarmMinHigh': '',
    'updTemp': '',
    'rampUpdateHours': '',
    'rampTempDiff': '',
    'targetTemp': ''
  };

  // Plain `let` (NOT `$:`) for state that callbacks/bindings mutate.
  // `$: plensetup = []` etc. is a Svelte 5 footgun: the reactive statement
  // can re-fire (or be invalidated by an upstream dep change) and silently
  // reset the array back to empty mid-edit, which surfaces as "I edited a
  // value, hit save, came back, and my edits are gone." `wait` is bound
  // back from SaveButton; `ready` is set in onMount; `plensetup` is the
  // edit buffer for the form. None of them are derivations.
  let plensetup: string[] = [];
  let original: string[] = [];
  let ready = false;
  let wait = false;
  $: level = $navigationStore.level;
  $: edit = level > 0;
  $: onionMode = ($frontMatterStore?.panel as string[])?.[8] === '1';

  // ─── Ramp section state (promoted from /level1/ramp) ────────────
  // NB: `let ramp = ...` (not `$:`) — the `$:` form makes Svelte 5's
  // compiler treat `ramp.rate?.length === N` as compile-time constant
  // false (since ramp.rate is initialized empty), which silently
  // eliminates the entire `{#if ... ramp.rate?.length === 5}` block
  // from the SSR template. Plain `let` keeps the field reactive through
  // mutations in hydrateRamp/onMount without that aggressive folding.
  let ramp = { rate: [] as string[], plenum: '', pile: [] as string[] };
  let originalRate: string[] = [];
  let rampWait = false;
  // Checkbox toggle: when true, the plenum-temp setpoint input is grayed
  // (the ramp will manage it) and the inline ramp sentence is shown
  // between the plenum table and the alarm card. When false, the ramp
  // section is hidden — the underlying RampRateSettings stays in OSPI
  // unchanged; this is purely a UI affordance.
  //
  // Initial value is firmware-driven: if SystemStatus.current_mode is
  // UI_RAMPCOOL (10) or UI_RAMPREFRIG (11) at page-load time, the box
  // starts checked so the operator sees the ramping panel without
  // having to click first. After that initial sync, the operator's
  // explicit toggle wins until the firmware exits ramp mode and a
  // fresh page-load happens. (Don't auto-track every SystemStatus
  // push — that would re-check the box right after a user uncheck
  // because firmware takes a beat to wind ramping down.)
  let useRamp = false;
  let _useRampSeeded = false;
  $: if (!_useRampSeeded && $systemStatus && $systemStatus.currentMode !== undefined) {
    const m = $systemStatus.currentMode;
    if (m === 10 || m === 11) {
      useRamp = true;
    }
    _useRampSeeded = true;
  }
  // `_rampMoved` arms the auto-uncheck-on-completion logic. We only
  // auto-disable the checkbox once the live setpoint has actually
  // *moved* away from the target and then re-converged — otherwise
  // checking the box while already at target would instantly uncheck
  // it. Reset on every off→on transition.
  let _rampMoved = false;
  const defaultTempOptions = [
    { text: $t('global.return-air-temp-default'), value: '255' },
  ];
  let rampOptions: { text: string, value: string }[] = [];

  // pile[] (label/value pairs) from the aggregated sensor list —
  // enabled return-temp / pile-temp sensors only.
  $: ramp.pile = ($sensorListStore ?? [])
    .filter((s) => !s.disabled
      && (s.type === SensorTypes.SENSOR_RETURN_TEMP_2
        || s.type === SensorTypes.SENSOR_PILE_TEMP))
    .flatMap((s) => [s.label, String(s.id)]);

  $: {
    rampOptions = [];
    rampOptions.push(...defaultTempOptions);
    ramp.pile?.forEach((item, index) => {
      if (index % 2 == 0) rampOptions.push({ text: item, value: ramp.pile?.[index + 1] });
    });
  };

  // Subscribe (not `$:`) + dirty gate so SaveButton's bind:original
  // write-through doesn't retrigger and clobber the freshly saved
  // baseline (humidifier-style spurious-unsaved-changes bug). Two
  // independent dirty checks because ramp + main are separate sub-forms.
  function isRampDirty(): boolean {
    return !!(originalRate.length && !isEqual(ramp.rate, originalRate));
  }
  function isMainDirty(): boolean {
    return !!(original.length && !isEqual(plensetup, original));
  }
  // Round a float to a sane display precision, then strip trailing zeros.
  // Firmware stores temps as float32, which can't represent 0.3 exactly
  // (rounds to 0.30000001192...). Without this format step, those bits
  // round-trip into the UI and show up in the operator's setpoint cells.
  function fmtFloat(v: any, decimals: number = 2): string {
    const n = Number(v);
    if (v == null || isNaN(n)) return '0';
    return String(+n.toFixed(decimals));
  }

  function hydrateRamp(r: any) {
    if (!r || isRampDirty()) return;
    const period = r.updatePeriod ?? 0;
    const periodStr = period === 255 ? $t('global.automatically') : String(period);
    const next = [
      fmtFloat(r.ratePerDay ?? 0.5),
      periodStr,
      fmtFloat(r.tempDiff ?? 1.0),
      String(r.tempRef ?? 0),
      fmtFloat(r.targetTemp ?? 7.0),
    ];
    if (!isEqual(next, originalRate)) {
      // We already returned early if dirty, so we can update both
      // baseline and edit buffer in lockstep. Past versions checked
      // `isEqual(ramp.rate, originalRate)` AFTER updating originalRate,
      // which is broken — once originalRate is the new value, the
      // comparison always fails on the second hydrate call (when the
      // proto store transitions null → real value), leaving the edit
      // buffer stuck on the stale `null` snapshot.
      originalRate = [...next];
      ramp = { ...ramp, rate: [...next] };
    }
  }
  function hydrateMain(p: any, a: any) {
    if (isMainDirty()) return;
    const next = new Array(11).fill('0');
    if (p) {
      next[0] = fmtFloat(p.tempSetpoint ?? 0);
      next[1] = String(p.humidSetpoint ?? 0);
      next[2] = String(p.humidSetpointRef ?? 0);
      next[3] = fmtFloat(p.burnerTempSetpoint ?? 0);
      next[4] = fmtFloat(p.burnerThreshold ?? 0);
    }
    if (a) {
      next[5] = fmtFloat(a.lowTemp ?? 0);
      next[6] = String(a.lowTimer ?? 0);
      next[7] = fmtFloat(a.highTemp ?? 0);
      next[8] = String(a.highTimer ?? 0);
    }
    if (!isEqual(next, original)) {
      // Same fix as hydrateRamp: dirty already gates entry, so the
      // edit buffer always tracks the freshly-arrived baseline.
      original = [...next];
      plensetup = [...next];
    }
  }

  // ramp.plenum is a read-only echo of the live plenum setpoint; safe as
  // a plain reactive (no `original` involved, never feeds dirty check).
  // Format through fmtFloat so float32 quantization (e.g. 50.0 → 49.9999…)
  // doesn't surface in the live readout.
  $: if ($plenumSettings) {
    const p = fmtFloat($plenumSettings.tempSetpoint ?? 0);
    if (p !== ramp.plenum) ramp = { ...ramp, plenum: p };
  }

  // Reset the "ramp has moved" arm whenever the user toggles ramp off.
  $: if (!useRamp) _rampMoved = false;

  // Idle the firmware ramp by re-saving the current plenum tempSetpoint.
  // The firmware snap-guard in LpSettings_ApplyPlenum forces
  // ramp.target_temp = tempSetpoint, which fails the activation gate
  // (target == setpoint) and flips Ramp.Status to RAMP_OFF on the next
  // tick. Other plenum fields are echoed back unchanged from the proto
  // store so we don't accidentally clobber them.
  async function deactivateRampOnFirmware() {
    const ps = $plenumSettings;
    if (!ps) return;
    try {
      await writeProto(TAG.PlenumSettings, {
        tempSetpoint:       ps.tempSetpoint ?? 0,
        humidSetpoint:      ps.humidSetpoint ?? 0,
        humidSetpointRef:   ps.humidSetpointRef ?? 0,
        burnerTempSetpoint: ps.burnerTempSetpoint ?? 0,
        burnerThreshold:    ps.burnerThreshold ?? 0,
      });
    } catch (e) {
      console.error('[plentemp] deactivateRampOnFirmware failed:', e);
    }
  }

  // Auto-uncheck once the ramp has run and converged on the target.
  // Compare live setpoint to the FIRMWARE-SAVED target (originalRate[4]),
  // NOT the in-flight edit buffer ramp.rate[4] — otherwise typing a new
  // target value can pass the convergence test mid-edit and uncheck the
  // box while the operator is still entering data.
  // Tolerance: ramp moved away by ≥ 1° once, then within 0.1° of target.
  // Tight 0.1° threshold so float32 round-trip jitter doesn't trigger.
  $: if (useRamp && ramp.plenum && originalRate.length === 5) {
    const live = parseFloat(ramp.plenum);
    const target = parseFloat(originalRate[4] ?? '0');
    if (!isNaN(live) && !isNaN(target)) {
      const diff = Math.abs(live - target);
      if (diff >= 1) _rampMoved = true;
      else if (_rampMoved && diff < 0.1) {
        useRamp = false;
        _rampMoved = false;
      }
    }
  }

  onMount(() => {
    try {
      // Both sub-forms must be clean for the page to be considered clean.
      $navigationStore.isDirty = () => isMainDirty() || isRampDirty();
    } catch (e) {
      console.error(e);
    }
    const u1 = rampRateSettings.subscribe(hydrateRamp);
    const u2 = plenumSettings.subscribe((p) => hydrateMain(p, $tempAlarmSettings));
    const u3 = tempAlarmSettings.subscribe((a) => hydrateMain($plenumSettings, a));
		ready = true;
    return () => { u1(); u2(); u3(); };
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="plentemp">
  <ScrollableArea>
  <Card class="mt-2 flex flex-col container-wide">
    {#if plensetup.length > 8}
    <Table class="mb-2">
      <Row>
        <Column class="text-size-xl border-r border-gray-400">
          <div class="flex flex-col items-center gap-2">
            <span>{ onionMode ? $t('level1.plentemp.plenum-temperature-setpoint-for-burner-cure') : $t('global.plenum-temperature-setpoint') }</span>
            {#if !onionMode}
              <label class="flex items-center gap-2 text-size-base font-normal cursor-pointer">
                <input type="checkbox" class="checkbox w-6 h-6"
                  checked={useRamp}
                  on:change={(e) => {
                    const next = e.currentTarget.checked;
                    /* Manual uncheck while ramp was on → tell the
                     * firmware to idle. Re-saving the current plenum
                     * setpoint triggers the snap-guard which forces
                     * target == setpoint and the activation gate fails.
                     * Auto-uncheck (when live converges to target) goes
                     * through the reactive that sets useRamp directly,
                     * not through this handler — which is correct,
                     * because at that point target == live already and
                     * the firmware ramp has already self-idled. */
                    if (useRamp && !next) deactivateRampOnFirmware();
                    useRamp = next;
                  }}
                  disabled={!edit} />
                <span>{ $t('level1.ramp.use-ramp-rate') }</span>
              </label>
            {/if}
          </div>
        </Column>
        <Column class="text-size-xl">
          {#if onionMode}
            <TextField class="w-36 border-none rounded-none" size="xl" bind:value={plensetup[3]} {edit} label="Plenum Temperature Setpoint"  keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature}/>
          {:else if useRamp}
            <!-- Live ramp readout: shows the firmware's current plenum
                 setpoint as the ramp drives it toward target. Read-only
                 (edit=false) — TextField renders a <b> tag in this mode. -->
            <span class="opacity-50">
              <TextField class="w-36 border-none rounded-none" size="xl"
                value={ramp.plenum} edit={false}
                label="Plenum Temperature Setpoint"
                adornmentType={AdornmentType.Temperature}/>
            </span>
          {:else}
            <TextField class="w-36 border-none rounded-none" size="xl" bind:value={plensetup[0]} {edit} label="Plenum Temperature Setpoint"  keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.PlenumTempSet}/>
          {/if}
         </Column>
     </Row>
      <Row>
        <Column class="text-size-xl border-r border-gray-400">
          { onionMode ? $t('level1.plentemp.burner-output-threshold-for-maximum-cure') : $t('global.plenum-humidity-setpoint') }
        </Column>
        <Column class="text-size-xl">
          {#if onionMode}
            <TextField class="w-36 border-none rounded-none" size="xl" bind:value={plensetup[4]} {edit} label="Plenum Humidity Setpoint"  keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Humidity}/>
          {:else}
            <TextField class="w-36 border-none rounded-none" size="xl" bind:value={plensetup[1]} {edit} label="Plenum Humidity Setpoint"  keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Humidity} validation={validation.PlenumHumidSet}/>
          {/if}
        </Column>
      </Row>
    </Table>

    <!-- ─── Inline Ramp sentence (only when "Use ramp rate" is checked) ── -->
    <!--
      Sentence layout matches the alarm card below: text-size-xl, inline
      TextFields/Select inside a single <p>, sentence wording flows from
      i18n keys. Composes from the same `ramp.rate[]` array as the
      sentence-style ramp page that this UI replaced.
    -->
    {#if !onionMode && useRamp}
      <Card class="w-full mx-auto my-2 flex flex-col bg-surface-100">
        {#if ramp.plenum}
          <p class="text-center text-size-base mb-1 text-gray-700">
            { $t('level1.ramp.current-plenum-setpoint-is') } <b>{ramp.plenum}°</b>
          </p>
        {/if}
        <p class="text-center text-size-xl">
          { $t('level1.ramp.plenum-setpoint-will-change') }
          <TextField class="w-28" size="xl" bind:value={ramp.rate[0]} {edit}
            label="Setpoint Change Rate" keyboardType={KeyboardTypes.Float}
            adornmentType={AdornmentType.Temperature} validation={validation.updTemp}/>
          {#if ramp.rate[1] === $t('global.automatically')}
            <TextField class="w-56 3xl:w-80" size="xl" bind:value={ramp.rate[1]} {edit}
              label="Update Time" keyboardType={KeyboardTypes.Auto}
              validation={validation.rampUpdateHours}/>
            { $t('level1.ramp.as-a-temperature-differential-of') }
            <TextField class="w-28" size="xl" bind:value={ramp.rate[2]} {edit}
              label="Temp Differential" keyboardType={KeyboardTypes.Float}
              adornmentType={AdornmentType.Temperature}
              validation={validation.rampTempDiff}/>
            { $t('level1.ramp.between-plenum-setpoint-and') }
            <Select class="text-center" size="xl" bind:value={ramp.rate[3]}
              options={rampOptions} {edit}/>
          {:else}
            { $t('level1.ramp.every') }
            <TextField class="w-28" size="xl" bind:value={ramp.rate[1]} {edit}
              label="Update Time" keyboardType={KeyboardTypes.Auto}
              validation={validation.rampUpdateHours}/>
            { $t('level1.ramp.hours-of-cooling-or-refrigeration-runtime') }
          {/if}
          { $t('level1.ramp.until-plenum-setpoint-equals') }
          <TextField class="w-28" size="xl" bind:value={ramp.rate[4]} {edit}
            label="Target Temperature" keyboardType={KeyboardTypes.Float}
            adornmentType={AdornmentType.Temperature} validation={validation.targetTemp}/>.
        </p>
        <SaveButton {edit} bind:wait={rampWait} data={ramp.rate}
          bind:original={originalRate} bind:validation={validation} autoSave
          onSave={async (d) => {
            const autoStr = $t('global.automatically');
            const periodRaw = d[1] ?? '0';
            const period = periodRaw === autoStr ? 255 : (parseInt(periodRaw, 10) || 0);
            const tempRef = parseInt(d[3] ?? '0', 10) || 0;
            await writeProto(TAG.RampRateSettings, {
              ratePerDay:   parseFloat(d[0] ?? '0') || 0,
              updatePeriod: period,
              tempDiff:     parseFloat(d[2] ?? '0') || 0,
              tempRef,
              targetTemp:   parseFloat(d[4] ?? '0') || 0,
            });
          }}/>
      </Card>
    {/if}
    <Card class="w-full mx-auto my-2 flex flex-col bg-surface-100">
      {#if onionMode}
        <p class="text-center text-size-xl">
          { $t('level1.plentemp.system-will-alarm-if') } { $t('level1.plentemp.plenum-temperature-is-below') } <TextField class="w-36" size="xl" bind:value={plensetup[9]} {edit} label="Cure Temperature Low" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.CureTempLowLimit}/>
          { $t('level1.plentemp.for') } <TextField class="w-36" size="xl" bind:value={plensetup[6]} {edit} label="Alarm Temperature Low Duration" keyboardType={KeyboardTypes.Numeric} validation={validation.AlarmMinLow}/> { $t('level1.plentemp.minutes-or') } 
          { $t('level1.plentemp.plenum-temperature-is-above') } <TextField class="w-36" size="xl" bind:value={plensetup[10]} {edit} label="Cure Temperature High" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.CureTimeHighLimit}/>
          { $t('level1.plentemp.for') } <TextField class="w-36" size="xl" bind:value={plensetup[8]} {edit} label="Alarm Temperature High Duration" keyboardType={KeyboardTypes.Numeric} validation={validation.AlarmMinHigh}/> { $t('global.minutes') }.
        </p>
      {:else}
        <p class="text-center text-size-xl">
          { $t('level1.plentemp.system-will-alarm-if') } { $t('level1.plentemp.plenum-temperature-is')} <TextField class="w-36" size="xl" bind:value={plensetup[5]} {edit} label="Alarm Temperature Low" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.AlarmTempLow}/>
          { $t('level1.plentemp.below-plenum-setpoint-for') } <TextField class="w-36" size="xl" bind:value={plensetup[6]} {edit} label="Alarm Temperature Low Duration" keyboardType={KeyboardTypes.Numeric} validation={validation.AlarmMinLow}/> { $t('level1.plentemp.minutes-or') } 
          { $t('level1.plentemp.plenum-temperature-is') } <TextField class="w-36" size="xl" bind:value={plensetup[7]} {edit} label="Alarm Temperature High" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.AlarmTempHigh}/>
          { $t('level1.plentemp.above-plenum-setpoint-for') } <TextField class="w-36" size="xl" bind:value={plensetup[8]} {edit} label="Alarm Temperature High Duration" keyboardType={KeyboardTypes.Numeric} validation={validation.AlarmMinHigh}/> { $t('global.minutes') }.
        </p>
      {/if}
    </Card>
    <SaveButton {edit} bind:wait={wait} data={plensetup} bind:original={original} bind:validation={validation} autoSave
      onSave={async (d: string[]) => {
        // Multi-tag save: PlenumSettings (1) + TempAlarmSettings (16).
        // Cure limits live in TempAlarmSettings fields 5,6 and the firmware
        // apply_temp_alarms() handler writes them straight into
        // Settings.Cure.TempLowLimit/HighLimit — apply_cure_limits (field 17)
        // is redundant. Single TempAlarmSettings write covers both cases;
        // when not in onion mode the page just leaves cure[9]/[10] empty.
        const humidSet = parseInt(d[1], 10) || 0;
        const humidRef = parseInt(d[2], 10) || 0;
        // Zero-meaningful humid fields registered in forceFieldRegistry.ts.
        await writeProto(TAG.PlenumSettings, {
          tempSetpoint:       parseFloat(d[0]) || 0,
          humidSetpoint:      humidSet,
          humidSetpointRef:   humidRef,
          burnerTempSetpoint: parseFloat(d[3]) || 0,
          burnerThreshold:    parseFloat(d[4]) || 0,
        });

        const lowTimer  = parseInt(d[6], 10) || 0;
        const highTimer = parseInt(d[8], 10) || 0;
        const onion = d.length > 10 && d[9] && d[10];
        // lowTimer/highTimer are zero-meaningful; registered in registry.
        await writeProto(TAG.TempAlarmSettings, {
          lowTemp:   parseFloat(d[5]) || 0,
          lowTimer,
          highTemp:  parseFloat(d[7]) || 0,
          highTimer,
          cureLow:   onion ? (parseFloat(d[9])  || 0) : 0,
          cureHigh:  onion ? (parseFloat(d[10]) || 0) : 0,
        });
      }} />
    {/if}
  </Card>
  </ScrollableArea>
</GellertPage>



