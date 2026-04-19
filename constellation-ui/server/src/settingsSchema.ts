/**
 * Settings schema registry for the ARM simulator.
 *
 * Motivation
 * ──────────
 * The legacy firmware stores each settings tag as a fixed-layout CSV.  Over
 * the wire, however, the UI → bridge → ARM path uses three different
 * encodings:
 *
 *   1. Positional CSV:          "val0,val1,val2"
 *   2. Positional underscore:   "val0&_1=val1&_2=val2"   (bridge `pos()` helper)
 *   3. Named fields:            "val0&KeyName1=val1&KeyName2=val2"
 *
 * Mode-dependent tags (burner, fan boost, CO2 purge, …) further change
 * their wire layout based on field[0] (mode).  If the ARM simulator
 * blindly stores the wire body under the tag name, the next `sendAllSettings`
 * echo hands the UI a blob that it splits on `,` — and every page-level
 * setting winds up in slot [0], everything else defaults to '0' or ''.
 * That's the root cause of "corrupt default settings."
 *
 * This module solves that by:
 *
 *   • Declaring the canonical field layout + defaults for every persisted
 *     tag the UI cares about.
 *   • Providing {@link parseWireBody} which decodes any of the three wire
 *     formats (or a mix) into the canonical CSV, preserving prior state
 *     for any unmentioned field.
 *   • Supporting optional per-tag `decode` overrides for mode-dependent
 *     encodings (burner / boost / purge).
 *
 * The canonical CSV is what `sendAllSettings` echoes back to the UI and what
 * `dataCache.buildPageData` splits on comma — so as long as every tag
 * round-trips through {@link parseWireBody}, a bad input can at worst lose
 * a single field; it can never shift or corrupt the whole array.
 */

export interface FieldDef {
  /** Canonical name used for debugging + named-wire lookup. */
  name: string;
  /** Default value — seeded at factory-reset and used when a field is
   *  missing from the incoming wire body. */
  default: string;
  /** Additional aliases the wire body may use for this field. */
  aliases?: string[];
}

export interface TagSchema {
  fields: FieldDef[];
  /** If true, an initial `AS2` prefix on the wire body is stripped. */
  as2?: boolean;
  /** Optional custom decoder for mode-dependent or otherwise complex tags.
   *  Receives the (AS2-stripped, SessionID-stripped) body parts and the
   *  prior canonical CSV; must return the new canonical CSV. */
  decode?: (parts: string[], prior: string[], schema: TagSchema) => string[];
}

/** Build a quick index { fieldName → position } for a schema. */
function buildNameIndex(fields: FieldDef[]): Map<string, number> {
  const idx = new Map<string, number>();
  fields.forEach((f, i) => {
    idx.set(f.name, i);
    f.aliases?.forEach(a => idx.set(a, i));
  });
  return idx;
}

/**
 * Decode one wire-format body (named, _N-positional, bare positional, or
 * any mix) into a canonical CSV array.  Missing fields fall back to the
 * prior CSV, then to schema defaults.  Unknown keys are dropped.
 */
export function parseWireBody(schema: TagSchema, body: string, priorCsv?: string): string[] {
  const prior = priorCsv ? priorCsv.split(',') : [];
  const out: string[] = schema.fields.map((f, i) => prior[i] ?? f.default);

  // Strip SessionID and AS2 sentinel
  let parts = body.split('&').filter(p => !/^SessionID=/i.test(p));
  if (schema.as2 && parts[0] === 'AS2') parts = parts.slice(1);

  if (schema.decode) {
    return schema.decode(parts, out, schema);
  }

  const names = buildNameIndex(schema.fields);
  let positional = 0;

  for (const p of parts) {
    const eq = p.indexOf('=');
    if (eq < 0) {
      // bare value → next positional slot
      if (positional < out.length) out[positional] = p;
      positional++;
      continue;
    }
    const k = p.slice(0, eq);
    const v = p.slice(eq + 1);

    if (/^_\d+$/.test(k)) {
      const i = parseInt(k.slice(1), 10);
      if (i >= 0 && i < out.length) out[i] = v;
      continue;
    }

    const i = names.get(k);
    if (i !== undefined) out[i] = v;
    // otherwise ignore (session keys, unknown fields)
  }

  return out;
}

/**
 * Serialize a canonical CSV from an array of field values.  Any missing
 * trailing fields are filled with schema defaults so downstream splitters
 * always see the correct length.
 */
export function toCsv(schema: TagSchema, values: string[]): string {
  const out = schema.fields.map((f, i) => values[i] ?? f.default);
  return out.join(',');
}

/** Build the canonical CSV from scratch using schema defaults. */
export function defaultCsv(schema: TagSchema): string {
  return schema.fields.map(f => f.default).join(',');
}

/* ─────────────────────────────────────────────────────────────────
 *  Mode-dependent decoders
 * ─────────────────────────────────────────────────────────────── */

/** Bridge sends burner body in one of three mode layouts:
 *    mode 0 (Off):      [mode, alt, altUnit]
 *    mode 1 (Manual):   [mode, manual, alt, altUnit]
 *    mode 2/3 (Econ):   [mode, on, low, P, I, D, U, alt, altUnit]
 * Canonical CSV order:  [on, low, P, I, D, U, mode, manual, alt, altUnit]
 */
function decodeBurner(parts: string[], prior: string[]): string[] {
  const pos: string[] = [];
  let nextBare = 0;
  for (const p of parts) {
    const eq = p.indexOf('=');
    if (eq < 0) { pos[nextBare++] = p; continue; }
    const k = p.slice(0, eq); const v = p.slice(eq + 1);
    if (/^_\d+$/.test(k)) pos[parseInt(k.slice(1), 10)] = v;
  }
  const mode = parseInt(pos[0] ?? prior[6] ?? '0', 10) || 0;
  const out = [...prior];
  out[6] = String(mode);
  if (mode === 0) {
    out[8] = pos[1] ?? prior[8]; out[9] = pos[2] ?? prior[9];
  } else if (mode === 1) {
    out[7] = pos[1] ?? prior[7]; out[8] = pos[2] ?? prior[8]; out[9] = pos[3] ?? prior[9];
  } else {
    out[0] = pos[1] ?? prior[0]; out[1] = pos[2] ?? prior[1];
    out[2] = pos[3] ?? prior[2]; out[3] = pos[4] ?? prior[3];
    out[4] = pos[5] ?? prior[4]; out[5] = pos[6] ?? prior[5];
    out[8] = pos[7] ?? prior[8]; out[9] = pos[8] ?? prior[9];
  }
  return out;
}

/** Fan boost:
 *    mode 0 (Off):          [mode]
 *    mode 1 (Temperature):  [mode, speed, temp, interval, duration]
 *    mode 2 (Runtime):      [mode, speed, interval, duration]
 * Canonical CSV order:      [mode, speed, interval, duration, temp]
 */
function decodeFanBoost(parts: string[], prior: string[]): string[] {
  const pos: string[] = [];
  let nextBare = 0;
  for (const p of parts) {
    const eq = p.indexOf('=');
    if (eq < 0) { pos[nextBare++] = p; continue; }
    const k = p.slice(0, eq); const v = p.slice(eq + 1);
    if (/^_\d+$/.test(k)) pos[parseInt(k.slice(1), 10)] = v;
  }
  const mode = parseInt(pos[0] ?? prior[0] ?? '0', 10) || 0;
  const out = [...prior];
  out[0] = String(mode);
  if (mode === 1) {
    out[1] = pos[1] ?? prior[1];   // speed
    out[4] = pos[2] ?? prior[4];   // temp
    out[2] = pos[3] ?? prior[2];   // interval
    out[3] = pos[4] ?? prior[3];   // duration
  } else if (mode === 2) {
    out[1] = pos[1] ?? prior[1];
    out[2] = pos[2] ?? prior[2];
    out[3] = pos[3] ?? prior[3];
  }
  return out;
}

/** CO2 purge:
 *    mode 0 (Off):     [mode]
 *    mode 1 (Manual):  [mode, cycleTime, minTemp, maxTemp, duration, fanOut, doorOut]
 *    mode 2 (Auto):    [mode, co2Set,    minTemp, maxTemp, duration, fanOut, doorOut]
 * Canonical CSV order (matches dataCache 'Co2PurgeData'):
 *                      [mode, minTemp, maxTemp, duration, setOrCycle, fanOut, doorOut]
 */
function decodePurge(parts: string[], prior: string[]): string[] {
  const pos: string[] = [];
  let nextBare = 0;
  for (const p of parts) {
    const eq = p.indexOf('=');
    if (eq < 0) { pos[nextBare++] = p; continue; }
    const k = p.slice(0, eq); const v = p.slice(eq + 1);
    if (/^_\d+$/.test(k)) pos[parseInt(k.slice(1), 10)] = v;
  }
  const mode = parseInt(pos[0] ?? prior[0] ?? '0', 10) || 0;
  const out = [...prior];
  out[0] = String(mode);
  if (mode === 1 || mode === 2) {
    // wire[1] is the mode-specific "set" (cycleTime for manual, co2Set for auto)
    out[4] = pos[1] ?? prior[4];
    out[1] = pos[2] ?? prior[1];   // minTemp
    out[2] = pos[3] ?? prior[2];   // maxTemp
    out[3] = pos[4] ?? prior[3];   // duration
    out[5] = pos[5] ?? prior[5];   // fanOut
    out[6] = pos[6] ?? prior[6];   // doorOut
  }
  return out;
}

/* ─────────────────────────────────────────────────────────────────
 *  Tag registry
 * ─────────────────────────────────────────────────────────────── */

const F = (name: string, def: string, aliases?: string[]): FieldDef => ({ name, default: def, aliases });

export const SCHEMAS: Record<string, TagSchema> = {

  // ── Basic setup (page: /level2/basic) ──
  StorageName: {
    fields: [
      F('Name',         'Gellert Nova'),
      F('TempType',     '0'),                 // 0=°F, 1=°C
      F('OutputType',   '0'),
      F('HomePage',     'mnMainData.htm'),
      F('SystemMode',   '0'),
      F('Language',     '0'),
      F('MasterIp',     '0'),
      F('MultiView',    '0'),
      F('dlr0',         ''),                  // remote-login password is AES-encrypted; blank forces user to set one
      F('loginSecure',  '0'),
      F('Animations',   '1'),
    ],
  },

  // ── Plenum setup (page: /level2/plensetup) ──
  //   PlenumHumidRef: '0'=Plenum (default), '1'=Return Air (see AS2FormP1Plenum.js)
  p1Plenum: {
    as2: true,
    fields: [
      F('PlenumTempSet',    '46.0'),
      F('PlenumHumidSet',   '95'),
      F('selHumSetpointRef','0'),              // 0=Plenum humidity sensor
      F('BurnerTempSet',    '90.0'),
      F('BurnerThreshold',  '50'),
    ],
  },
  p2Plenum: {
    fields: [
      F('TempSet',        '46.0'),
      F('HumidSet',       '95'),
      F('HumidRef',       '0'),
      F('BurnerTempSet',  '90.0'),
      F('BurnerThreshold','50'),
      F('AlarmTempLow',   '5.0'),
      F('AlarmMinLow',    '10'),
      F('AlarmTempHigh',  '5.0'),
      F('AlarmMinHigh',   '10'),
      F('CureTempLow',    '65.0'),
      F('CureTempHigh',   '95.0'),
    ],
  },
  AlarmTempLow: {
    fields: [
      F('AlarmTempLow',  '5.0'),
      F('AlarmMinLow',   '10'),
      F('AlarmTempHigh', '5.0'),
      F('AlarmMinHigh',  '10'),
    ],
  },
  CureTempLowLimit: {
    fields: [
      F('CureTempLow',  '65.0'),
      F('CureMinLow',   '10'),
      F('CureTempHigh', '95.0'),
      F('CureMinHigh',  '10'),
    ],
  },

  // ── Outside air (page: /level2/outside) ──
  //   ctrlMode canonical/echo layout is field-position-dependent in legacy
  //   firmware and not stabilized across the wire path — leave it to the
  //   generic store to avoid changing existing echo behavior.

  // ── Fan speed (page: /level2/fanspeed) ──
  maxFanSpeed: {
    fields: [
      F('MaxSpeed',      '100'),
      F('CurrentSpeed',  '75'),
      F('RefrigSpeed',   '80'),
      F('RecircSpeed',   '30'),
      F('UpdatePeriod',  '5'),
      F('TempDiff',      '1.5'),
      F('TempRef1',      '0'),
      F('TempRef2',      '255'),
    ],
  },
  p2FanSpeed: {
    fields: [
      F('MinSpeed',     '25'),
      F('MaxSpeed',     '100'),
      F('RecircSpeed',  '30'),
      F('UpdatePeriod', '5'),
      F('TempDiff',     '1.5'),
      F('TempRef1',     '0'),
      F('TempRef2',     '255'),
      F('UpdateMode',   '0'),
    ],
  },

  // ── Fan boost (page: /level2/fanboost) — mode-dependent ──
  selBoostMode: {
    fields: [
      F('Mode',     '0'),        // 0=Off, 1=Temperature, 2=Runtime
      F('Speed',    '100'),
      F('Interval', '8'),
      F('Duration', '30'),
      F('Temp',     '5.0'),
    ],
    decode: decodeFanBoost,
  },
  p2FanBoost: {
    fields: [
      F('Mode',     '0'),
      F('Speed',    '100'),
      F('Interval', '8'),
      F('Duration', '30'),
      F('Temp',     '5.0'),
    ],
  },

  // ── Ramp rate (page: /level2/ramp) ──
  //   Canonical CSV matches what the UI reads from RampRateData:
  //     [0]=ChangeAmt (0.5 °/hr), [1]=Rate (Automatically), [2]=TempDiff,
  //     [3]=TempRef, [4]=TargetTemp
  updTemp: {
    fields: [
      F('ChangeAmt',        '0.5'),
      F('rampUpdateHours',  'Automatically'),
      F('rampTempDiff',     '1.5'),
      F('selTemp',          '0'),
      F('targetTemp',       '46.0'),
    ],
  },
  p2Ramp: {
    fields: [
      F('ChangeAmt',  '0.5'),
      F('Rate',       'Automatically'),
      F('TempDiff',   '1.5'),
      F('TargetTemp', '46.0'),
    ],
  },

  // ── Humidifier (page: /level2/humidifier) ──
  //   The system supports up to THREE humidifiers.  Each humidifier owns
  //   7 consecutive fields: Mode, then 6 cycle-duration values
  //   (CoolOn, CoolOff, RecircOn, RecircOff, RefrigOn, RefrigOff).
  //   Total = 3 × 7 = 21 fields.  HumidCtrlData (CGI var read by the
  //   humidifier page) and the UI loader both expect this exact length.
  //
  //   The save POST is positional with 8 wire fields
  //   (Index, Mode, D1..D6) — only ONE humidifier at a time — but the
  //   stored canonical CSV is the full 21-field bank.  The merge logic
  //   in armSimulator.ts case 'selHumidType' splices the 7-field row
  //   into the correct offset (Index*7) and re-emits the full 21-CSV.
  selHumidType: {
    // Custom decoder: the UI POST wire body is per-humidifier (8 fields:
    // index + mode + 6 durations) but the stored canonical CSV is the
    // full 21-field bank.  The naive positional decode would stomp the
    // wrong slots, so we IGNORE the wire body here and let the
    // armSimulator switch-case 'selHumidType' splice the row into the
    // bank using p2Humid as the source of truth.  Returning the prior
    // unchanged keeps savedData consistent across the schema pass.
    fields: Array.from({ length: 21 }, (_, i) => {
      const isMode = (i % 7) === 0;
      return F(isMode ? `H${Math.floor(i/7)+1}_Mode` : `H${Math.floor(i/7)+1}_D${(i%7)}`,
               isMode ? '1' : '60');
    }),
    decode: (_parts, prior) => prior, // bank merge is done in armSimulator
  },
  p2Humid: {
    fields: Array.from({ length: 21 }, (_, i) => {
      const isMode = (i % 7) === 0;
      return F(`H${i}`, isMode ? '1' : '60');
    }),
  },

  // ── CO2 purge (page: /level2/co2) — mode-dependent ──
  //   Canonical CSV matches dataCache 'Co2PurgeData':
  //     [0]=Mode, [1]=MinTemp, [2]=MaxTemp, [3]=Duration,
  //     [4]=SetOrCycle (co2Set when auto, cycleTime when manual),
  //     [5]=FanOutput, [6]=DoorOutput
  selPurgeMode: {
    fields: [
      F('Mode',       '0'),           // 0=Off, 1=Manual, 2=Auto
      F('MinTemp',    '40'),
      F('MaxTemp',    '60'),
      F('Duration',   '20'),
      F('SetOrCycle', '2500'),         // co2 ppm when auto; cycle hours when manual
      F('FanOutput',  '100'),
      F('DoorOutput', '100'),
    ],
    decode: decodePurge,
  },
  p2Co2: {
    fields: [
      F('Mode','0'), F('Co2Set','2500'), F('CycleTime','24'),
      F('MaxTemp','60'), F('MinTemp','40'),
      F('Duration','20'), F('FanOutput','100'), F('DoorOutput','100'),
    ],
  },

  // ── Misc (page: /level2/misc) ──
  //   NOTE: some misc fields have different semantics between potato mode
  //   and onion mode (e.g. selRefrMode becomes a burner-control selector in
  //   onion mode).  Defaults here target potato mode.
  p1Misc: {
    as2: true,
    fields: [
      F('selRefrMode',          '0'),    // 0=Economy refrig (potato)
      F('defrostInterval',      '4'),    // hrs between defrost cycles
      F('defrostTime',          '20'),   // defrost duration in minutes
      F('tempThresh',           '10'),
      F('selCtrlMode',          '0'),
      F('selCavityCtrl',        '1'),    // 1=Off
      F('cavityDiff',           '5'),
      F('cavityDutyCycle',      '100'),
      F('selCavityCtrlSensor',  '4'),
      F('kbPref',               '0'),
      F('cavStandbyOn',         '0'),
    ],
  },
  p2Misc: {
    fields: [
      F('RefrMode','0'), F('DefrostPeriod','4'), F('DefrostDuration','20'),
      F('HeatThresh','10'), F('RefrigLimit','27'), F('CavityHeatMode','1'),
      F('KbPref','Off'),
    ],
  },

  // ── Refrigeration (page: /level2/refrigeration) ──
  //   Canonical CSV layout (23 fields) mirroring what the UI reads:
  //     [0..11]  : Stage1-6 On/Off pairs
  //     [12..15] : P, I, D, U
  //     [16]     : RefrigerationPurge mode
  //     [17]     : PurgeThreshold
  //     [18]     : reserved
  //     [19..22] : Stage7/Stage8 On/Off pairs
  p2Refrigeration: {
    as2: true,
    fields: [
      F('Stage1On','20'), F('Stage1Off','10'),
      F('Stage2On','30'), F('Stage2Off','20'),
      F('Stage3On','40'), F('Stage3Off','30'),
      F('Stage4On','50'), F('Stage4Off','40'),
      F('Stage5On','60'), F('Stage5Off','50'),
      F('Stage6On','70'), F('Stage6Off','60'),
      F('PRefrValue','5'), F('IRefrValue','15'),
      F('DRefrValue','2'), F('URefrValue','3'),
      F('RefrigerationPurge','1'),                // 1=Purge enabled
      F('PurgeThreshold','100'),                  // fan must be at 100% before purge stage engages
      F('_reserved','0'),
      F('Stage7On','0'),  F('Stage7Off','0'),
      F('Stage8On','0'),  F('Stage8Off','0'),
    ],
  },

  // ── Climacell (page: /level2/climacell) ──
  //   Canonical CSV order matches the UI read: efficiency, altitude, altUnit,
  //   then PIDU.  Altitude unit defaults to '0' (feet).
  ClimacellEff: {
    fields: [
      F('Efficiency','90'), F('Altitude','3500'), F('AltUnit','0'),
      F('P','5'), F('I','15'), F('D','2'), F('U','3'),
    ],
  },

  // ── Burner (page: /level2/burner) — mode-dependent ──
  //   Canonical CSV layout mirrors how the UI binds burner[0..9]:
  //     [0]=On%, [1]=Low%, [2..5]=P,I,D,U, [6]=Mode, [7]=Manual%, [8]=Altitude, [9]=AltUnit
  selBurnerMode: {
    fields: [
      F('On','100'),   F('Low','25'),   F('P','5'),     F('I','15'),
      F('D','2'),      F('U','3'),      F('Mode','2'),  F('Manual','100'),
      F('Altitude','3500'), F('AltUnit','0'),
    ],
    decode: decodeBurner,
  },

  // ── Failures 1 (page: /level2/failures1) ──
  //   All default modes set to 1 (Alarm) with a 1-minute delay so any
  //   failure surfaces quickly out of the box.  Aux and Lights stay off
  //   because they're installation-specific.
  FanMode: {
    fields: [
      F('FanMode','1'),
      F('FanTimer','1'),
      F('ClimacellMode','1'),     F('ClimacellTimer','1'),
      F('RefridgeMode','1'),      F('RefridgeTimer','1'),
      F('RefridgeRun','1'),
      F('RefrStagesMode','60'),   F('RefrStagesTimer','0'),
      F('HumidifiersMode','1'),   F('HumidifiersTimer','1'),
      F('AuxMode','0'),           F('AuxTimer','0'),
      F('HeatMode','1'),          F('HeatTimer','1'),
      F('CavityHeatMode','0'),    F('CavityHeatTimer','0'),
      F('BurnerMode','1'),        F('BurnerTimer','1'),
      F('LightsMode','0'),        F('LightsTimer','0'),
      F('LightsUnits','0'),
    ],
  },

  // ── Failures 2 (page: /level2/failures2) ──
  //   All default modes set to 1 (Alarm) with a 1-minute delay.
  //   HighCo2 defaults to alarm at 4000 ppm after 20 min; plenum-sensor
  //   failure alarms on a 2°F deviation held for 10 min.
  OutAirMode: {
    fields: [
      F('OutAirMode','1'),
      F('OutAirTimer','1'),
      F('OutHumidMode','1'),   F('OutHumidTimer','1'),
      F('HighCo2Mode','1'),    F('HighCo2Timer','20'),
      F('Co2Setpt','4000'),
      F('LowHumidMode','0'),   F('LowHumidTimer','0'),
      F('LowHumidSet','0'),
      F('PlenSenMode','1'),    F('PlenSenTimer','10'),
      F('PlenSenDiff','2.0'),
    ],
  },

  // ── Door (page: /level2/door) ──
  //   ActuatorTime = full-travel time in seconds (180s).  CoolAirCycle is
  //   in minutes.  PAirValue is 5/15/2/3 for the fresh-air PID loop.
  PAirValue: {
    fields: [
      F('P','5'), F('I','15'), F('D','2'), F('U','3'),
      F('ActuatorTime','180'), F('CoolAirCycle','1'),
    ],
  },
  DoorData: {
    fields: [F('P','5'), F('I','15'), F('D','2'), F('U','3')],
  },
  p2DoorData: {
    fields: [
      F('P','5'),F('I','15'),F('D','2'),F('U','3'),
      F('ActuatorTime','180'), F('CoolAirCycle','1'),
    ],
  },

  // ── Service (page: /level2/service) ──
  dealerName: {
    fields: [
      F('DealerName','Agristar Service'),
      F('DealerPhone','555-0100'),
      F('TechName','Tech Support'),
      F('TechPhone','555-0101'),
    ],
  },

  // ── Master/Slave (page: /level2/master) ──
  selMasterSlaveMode: {
    fields: [F('Mode','0'), F('MasterIp','')],
  },

  // ── Email (page: /level2/email) ──
  //   selEmailAlert is write-only (no echo path); let it store generically
  //   rather than duplicating EmailConfig's layout.  EmailConfig is the
  //   server-echoed tag that dataCache reads as EmailConfigData.
  EmailConfig: {
    fields: [
      F('Enable','0'),
      F('Server','mail.smtp2go.com'),
      F('AuthType','1'),
      F('Port','465'),
      F('Account','agristar.alerts'),
      F('Password','4gri*st4r4l3rts'),
      F('LocalIp',''),
      F('ToAddr','youraccount@gmail.com'),
      F('FromAddr','agristar.alerts@gellert.com'),
    ],
  },

  // ── Log (page: /level2/log) ──
  recInterval: {
    fields: [F('Interval','30'), F('Wrap','1')],
  },
  p2Log: {
    fields: [F('Interval','30'), F('Wrap','1')],
  },

  // ── Accounts (page: /level2/accounts) ──
  AcctId0: {
    fields: [F('Users','DEFAULT'), F('Passwords','GELLERT'), F('_2',''), F('_3','')],
  },

  // ── PID (page: /level2/pid) ──
  pidWrap: {
    fields: [F('Wrap','0')],
  },

  // ── PWM (page: /level2/pwm) ──
  p2PwmOutputs: {
    as2: true,
    fields: [
      F('Door_en','1'),F('Door_ch','0'),F('Refr_en','0'),F('Refr_ch','255'),
      F('Fan_en','1'),F('Fan_ch','1'),F('Burn_en','0'),F('Burn_ch','255'),
    ],
    /** PWM body arrives as "AS2&0=v0&1=v1&..." — bare numeric keys, not `_N`. */
    decode: (parts, prior, s) => {
      const out = [...prior];
      for (const p of parts) {
        const eq = p.indexOf('=');
        if (eq < 0) continue;
        const k = p.slice(0, eq); const v = p.slice(eq + 1);
        const i = /^\d+$/.test(k) ? parseInt(k, 10) : -1;
        if (i >= 0 && i < s.fields.length) out[i] = v;
      }
      return out;
    },
  },

  // ── IO config (page: /level2/ioconfig) ──
  // p2IoConfig is stored as InputConfig + OutputConfig CSVs, not a single CSV;
  // it is decoded separately in applyPostData (special-case retained).

  // ── Analog board (page: /level2/analog) ──
  BAdd: {
    fields: [
      F('BoardAddr','0'),
      F('_1','0'),
      F('BdLbl',''),
      F('_3','0'),
      F('BDis','0'),
      F('_5','0'),
      F('Sen1Lbl',''), F('Sen1Off','0'), F('Sen1Dis','0'),
      F('_9','0'), F('_10','0'),
      F('Sen2Lbl',''), F('Sen2Off','0'), F('Sen2Dis','0'),
      F('_14','0'), F('_15','0'),
      F('Sen3Lbl',''), F('Sen3Off','0'), F('Sen3Dis','0'),
      F('_19','0'), F('_20','0'),
      F('Sen4Lbl',''), F('Sen4Off','0'), F('Sen4Dis','0'),
      F('_24','0'),
    ],
  },

  // ── Aux program (page: /level2/aux) — variable rule count ──
  AuxProgram: {
    fields: [
      F('Equip','0'), F('DutyCycle','0'), F('Period','Off'), F('Units','Off'),
      F('_4','Off'), F('_5','Off'),
    ],
    /** Aux body has dynamic `type1`/`io1`/... rules.  Preserve the full body
     *  verbatim in field[0] so firmware echo can re-assemble it; UI reads
     *  AuxProgram as a special structured cache entry (not raw CSV). */
    decode: (parts, prior) => {
      const out = [...prior];
      if (parts[0] && !parts[0].includes('=')) out[0] = parts[0];
      // named trailing fields
      for (const p of parts) {
        const eq = p.indexOf('=');
        if (eq < 0) continue;
        const k = p.slice(0, eq); const v = p.slice(eq + 1);
        if (k === 'dutyCycle') out[1] = v;
        else if (k === 'period') out[2] = v;
        else if (k === 'units')  out[3] = v;
      }
      return out;
    },
  },

  // ── Alerts (page: /level2/alerts) ──
  // AlertSetup is a single bit-string; it is stored as-is by armSimulator
  // (not a CSV of fields) and handled by its own path.

  // ── Climacell times / runtimes ──
  // These are already comma-separated arrays of 48 elements and round-trip
  // cleanly through the generic positional path; no special schema needed.
};

/**
 * Return a schema if one exists for the given tag.  Unknown tags fall
 * through to the ARM simulator's legacy generic store path.
 */
export function getSchema(tag: string): TagSchema | undefined {
  return SCHEMAS[tag];
}
