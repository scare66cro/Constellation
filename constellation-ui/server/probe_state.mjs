// Probe the controller's actual SystemStatus fields.
// Connects to the bridge's proto stream WS, waits for one tag-16 frame,
// decodes fields 18 (system_state), 19 (run_clock_mode), 20 (pwm_doors_pct),
// and a few sensors. Exits after first hit.

import WebSocket from 'ws';

const WS_URL = 'ws://10.47.27.108:9001/proto/stream';
const SYSTEM_STATUS_TAG = 10;
const EQUIPMENT_STATUS_TAG = 11;
const REFRIG_SETTINGS_TAG = 44;
const OUTSIDE_AIR_SETTINGS_TAG = 51;
const MISC_SETTINGS_TAG = 52;
const PLENUM_SETTINGS_TAG = 40;
const BASIC_SETUP_TAG = 20;

const ST = {
  0: 'STANDBY', 1: 'COOLING', 2: 'REFRIG', 3: 'COOLDEHUMID',
  4: 'REFRIGDEHUMID', 5: 'CURE', 6: 'BURNER_CURE', 7: 'HEATING',
  8: 'DEFROST', 9: 'CAVITYHEAT', 10: 'RECIRC', 11: 'FAILURE',
  12: 'FAN_OFF', 13: 'FAN_MANUAL', 14: 'FAN_REMOTEOFF', 15: 'FAN_BOOST',
  16: 'CURE_OFF', 17: 'CURE_MANUAL', 18: 'PURGE', 19: 'SHUTDOWN',
  20: 'SYSTEM_REMOTEOFF', 21: 'POWER_FAILURE', 30: 'REMOTE_STANDBY',
  31: 'REFRIG_STANDBY', 32: 'REFRIG_REMOTEOFF',
  // (may be incomplete — just for quick decoding)
};
const RC = { 1: 'COOLING', 2: 'RECIRC', 3: 'STANDBY', 4: 'REFRIG', 5: 'CURE' };

function decodeVarint(buf, pos) {
  let v = 0n; let shift = 0n;
  for (;;) {
    const b = buf[pos++];
    v |= BigInt(b & 0x7f) << shift;
    if ((b & 0x80) === 0) break;
    shift += 7n;
  }
  return [v, pos];
}

function decode(buf) {
  const fields = new Map();
  let pos = 0;
  while (pos < buf.length) {
    const [tag, p1] = decodeVarint(buf, pos); pos = p1;
    const fnum = Number(tag >> 3n);
    const wire = Number(tag & 7n);
    if (wire === 0) {
      const [val, p2] = decodeVarint(buf, pos); pos = p2;
      fields.set(fnum, { wire, val });
    } else if (wire === 1) { // fixed64
      pos += 8;
    } else if (wire === 2) {
      const [len, p2] = decodeVarint(buf, pos); pos = p2;
      const slice = buf.slice(pos, pos + Number(len));
      pos += Number(len);
      fields.set(fnum, { wire, slice });
    } else if (wire === 5) {
      const v = buf.readFloatLE(pos); pos += 4;
      fields.set(fnum, { wire, val: v });
    }
  }
  return fields;
}

let emptyCount = 0;
const seen = { refrig: false, outside: false, misc: false, plenum: false, basic: false, sysstat: false };
const ws = new WebSocket(WS_URL);
ws.binaryType = 'arraybuffer';
ws.on('open', () => {
  console.error('connected, subscribing');
  ws.send(JSON.stringify({ action: 'subscribe', all: true }));
});
ws.on('message', (data, isBinary) => {
  // Skip JSON ack/control messages.
  if (typeof data === 'string') return;
  const buf = Buffer.from(data);
  if (buf.length < 6) return;
  // Try first byte text — if it's '{', it's JSON status.
  if (buf[0] === 0x7b /* '{' */) return;
  // Binary frame format per protoStream.buildFrame:
  //   2 B tag (LE)  +  4 B payload length (LE)  +  payload
  const tag = buf.readUInt16LE(0);
  const WATCH = [SYSTEM_STATUS_TAG, EQUIPMENT_STATUS_TAG, REFRIG_SETTINGS_TAG, OUTSIDE_AIR_SETTINGS_TAG, MISC_SETTINGS_TAG, PLENUM_SETTINGS_TAG, BASIC_SETUP_TAG];
  if (!WATCH.includes(tag)) return;
  const len = buf.readUInt32LE(2);
  if (len === 0) return;
  const payload = buf.slice(6, 6 + len);
  const f = decode(payload);
  const gv = (n) => { const x = f.get(n); return x ? Number(x.val) : null; };
  const gf = (n) => { const x = f.get(n); return x ? f.get(n).val : null; };

  if (tag === REFRIG_SETTINGS_TAG) {
    console.log('=== REFRIG SETTINGS ===');
    console.log(`  refrigMode: ${gv(7)}   refrigLimit: ${gf(8)}`);
    seen.refrig = true;
  }
  if (tag === OUTSIDE_AIR_SETTINGS_TAG) {
    // proto field order: mode(1) | differential(2:float) | above_below(3) |
    // temp_ref(4) | calc_humid_max(5). Earlier labels confused 2/3/4.
    console.log('=== OUTSIDE AIR SETTINGS ===');
    console.log(`  mode: ${gv(1)}  differential: ${gf(2)}  aboveBelow: ${gv(3)}  tempRef: ${gv(4)}  calcHumidMax: ${gv(5)}`);
    seen.outside = true;
  }
  if (tag === MISC_SETTINGS_TAG) {
    console.log('=== MISC SETTINGS ===');
    for (const [k, v] of f) console.log(`  field ${k}: wire=${v.wire} val=${v.val ?? '<bytes>'}`);
    seen.misc = true;
  }
  if (tag === BASIC_SETUP_TAG) {
    console.log('=== BASIC SETUP ===');
    console.log(`  systemMode: ${gv(2)}  tempType: ${gv(3)}`);
    seen.basic = true;
  }
  if (tag === PLENUM_SETTINGS_TAG) {
    console.log('=== PLENUM SETTINGS ===');
    console.log(`  tempSet: ${gf(1)}`);
    seen.plenum = true;
  }

  if (tag === EQUIPMENT_STATUS_TAG) {
    // EquipmentStatus.items = repeated EquipState, field 1.
    // Manually walk the wire because `decode()` keeps only one entry
    // per field number (loses repeats). Collect every field-1 submessage.
    const items = [];
    let pos = 0;
    while (pos < payload.length) {
      const [t, p1] = decodeVarint(payload, pos); pos = p1;
      const fnum = Number(t >> 3n);
      const wire = Number(t & 7n);
      if (wire === 2) {
        const [ln, p2] = decodeVarint(payload, pos); pos = p2;
        const sub = payload.slice(pos, pos + Number(ln));
        pos += Number(ln);
        if (fnum === 1) {
          const ef = decode(sub);
          const efGv = (n) => { const x = ef.get(n); return x ? Number(x.val) : null; };
          const efGs = (n) => { const x = ef.get(n); return x && x.slice ? x.slice.toString('utf8') : null; };
          items.push({
            eqIndex:   efGv(1),
            outputOn:  efGv(2),
            remoteOff: efGv(3),
            alarm:     efGv(4),
            label:     efGs(5),
          });
        }
      } else if (wire === 0) {
        const [, p2] = decodeVarint(payload, pos); pos = p2;
      } else if (wire === 5) { pos += 4; } else if (wire === 1) { pos += 8; }
    }
    const EQ_NAMES = { 0: 'FAN', 1: 'DOORS', 2: 'REFRIGERATION', 3: 'CLIMACELL', 4: 'HEAT', 5: 'CAVITY_HEAT', 6: 'BURNER', 7: 'CLIMACELL_PUMP', 63: 'CURE_VIRTUAL' };
    console.log('=== EQUIPMENT STATUS ===');
    for (const it of items) {
      if (it.eqIndex === null) continue;
      const name = EQ_NAMES[it.eqIndex] ?? `eq${it.eqIndex}`;
      const ro = { 0:'AUTO', 1:'OFF', 2:'MANUAL', 3:'SYSSTOP' }[it.remoteOff] ?? `r${it.remoteOff}`;
      console.log(`  ${name.padEnd(20)} remote=${ro.padEnd(8)} output=${it.outputOn} alarm=${it.alarm}`);
    }
    return;
  }

  const fields = decode(payload);
  const get = (n) => fields.get(n);
  // Override the earlier-block helpers to point at SystemStatus fields.
  const sgv = (n) => { const x = get(n); if (!x) return null; return typeof x.val === 'bigint' ? Number(x.val) : x.val; };
  const sgf = (n) => { const x = get(n); if (!x) return null; return typeof x.val === 'bigint' ? Number(x.val) : x.val; };
  const sgs = (n) => { const x = get(n); return x && x.slice ? x.slice.toString('utf8') : null; };
  const state = sgv(18);
  const runclock = sgv(19);
  const doorsPct = sgv(20);
  const refrigPct = sgv(21);
  // Decode field 24 = packed refrig-gate trace
  const gateRaw = sgv(24);
  const gate = gateRaw === null ? null : {
    sw_refrig_auto:   (gateRaw >> 0)  & 1,
    sw_fan_auto:      (gateRaw >> 1)  & 1,
    sw_freshair_auto: (gateRaw >> 2)  & 1,
    refrig_standby:   (gateRaw >> 3)  & 1,
    alarm_refrig:     (gateRaw >> 4)  & 1,
    refrig_mode:      (gateRaw >> 5)  & 3,
    osa_ctrl_mode:    (gateRaw >> 7)  & 3,
    refrig_failmode:  (gateRaw >> 9)  & 3,
    osa_above_below:  (gateRaw >> 16) & 0xFF,
  };
  const psetX10 = sgv(25);
  const pdifX10 = sgv(26);
  console.log(JSON.stringify({
    system_state: state,
    system_state_name: ST[state] ?? `ST_${state}`,
    run_clock_mode: runclock,
    run_clock_name: RC[runclock] ?? `RC_${runclock}`,
    pwm_doors_pct: doorsPct,
    pwm_refrig_pct: refrigPct,
    outside_temp: sgf(3),
    plenum_temp: sgf(1),
    start_temp: sgf(7),
    fan_speed: sgs(11),
    cool_output: sgs(12),
    cool_label: sgs(13),
    cure_state: sgv(17),
    current_mode: sgv(16),
    gate_bits_raw: gateRaw,
    gate,
    plenum_set: psetX10 === null ? null : psetX10 / 10,
    osa_diff:   pdifX10 === null ? null : pdifX10 / 10,
  }, null, 2));
  seen.sysstat = true;
  if (Object.values(seen).every(Boolean)) process.exit(0);
});
ws.on('error', (e) => { console.error('ws error:', e.message); process.exit(1); });
setTimeout(() => { console.error('timeout'); process.exit(2); }, 5000);
