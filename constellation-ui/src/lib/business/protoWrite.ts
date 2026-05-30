/**
 * protoWrite.ts — Phase 0.4 of the proto-direct redesign.
 *
 * Browser-side write helper. Encodes a typed proto message via ts-proto
 * and POSTs the raw bytes to the bridge's `/proto/write/:settingsField`
 * endpoint. The bridge wraps the bytes as field `:settingsField` of a
 * SettingsUpdate envelope and forwards through the firmware's
 * NovaProto_SendRaw mutex.
 *
 * USAGE:
 *
 *   import { writeProto } from '$lib/business/protoWrite';
 *   import { TAG } from '$lib/business/protoTags';
 *
 *   await writeProto(TAG.AlertSettings, { alertFlags: [0,1,3,7] });
 *
 * The firmware reply (an Ack) is consumed by the bridge; the UI never
 * sees it. Instead, the firmware's subsequent settings re-broadcast
 * (next periodic UI_Send* tick) lands as a /proto/stream frame, which
 * the per-tag store re-emits to the page. Pages should rely on that
 * push for confirmation, not on the POST response.
 *
 * NOTE on field numbers: the wire-level field number is decided by the
 * firmware's `handle_settings_update()` switch in
 * `Nova_Firmware/Platform/nova_dataexc.c`, NOT by the proto
 * `SettingsUpdate.payload` oneof in `proto/agristar/settings.proto`.
 * Those two layouts have diverged. The map below mirrors the firmware
 * switch — the proto oneof is informational only.
 */

import { TAG, type Tag, TAG_DECODERS, type TagPayload } from './protoTags.js';
import { getHttpUrl } from './util.js';
import { resolveForceFields } from './forceFieldRegistry.js';

/**
 * Envelope tag → firmware SettingsUpdate field number.
 * Source of truth: `Nova_Firmware/lp_am2434/main.c bridge_rx_callback`
 * — the chain of `if (sfield == NN && swire == 2U)` branches around
 * line 400-1240 that dispatches to LpSettings_Apply*().
 *
 * Mirror of `proto/agristar/settings.proto :: SettingsUpdate` oneof
 * field numbers (the firmware switch is the canonical proto layout).
 *
 * **Hardware reality check (2026-04-29):** the field map below was
 * briefly aligned to `Platform/nova_dataexc.c::handle_settings_update`,
 * but that file is the R5F dispatcher and is NOT what runs on the
 * RPi5-attached LP-AM2434. The LP runs `lp_am2434/main.c` which has
 * its own dispatch table with DIFFERENT field numbers for
 * Runtimes/AccountSettings/IoConfig/AlertSettings/MasterSlave/
 * AuxProgram/PwmChannel/AnalogBoard. Those entries below are the
 * LP values; do NOT re-align to nova_dataexc.c without first
 * verifying which firmware actually responds on the wire.
 *
 * Add an entry here only after the corresponding LpSettings_Apply*()
 * handler is verified round-trip (see docs/firmware-bridge-protocol.md
 * smoke-test pattern).
 */
export const SETTINGS_FIELD: Partial<Record<Tag, number>> = {
	[TAG.PlenumSettings]: 1,
	[TAG.FanSpeedSettings]: 2,
	[TAG.FanBoostSettings]: 3,
	[TAG.RampRateSettings]: 4,
	[TAG.RefrigSettings]: 5,
	[TAG.BurnerSettings]: 6,
	[TAG.Co2Settings]: 7,
	[TAG.CureSettings]: 8,
	[TAG.ClimacellSettings]: 9,
	[TAG.ClimacellTimes]: 10,
	[TAG.HumidCtrlSettings]: 11,
	[TAG.OutsideAirSettings]: 12,
	[TAG.MiscSettings]: 13,
	[TAG.FailureSettings]: 14,
	[TAG.FailureSettings2]: 15,
	[TAG.TempAlarmSettings]: 16,
	/* CureLimitSettings (firmware field 17) — LP exposes it but legacy
	 * cure low/high limits also flow via TempAlarmSettings fields 5,6.
	 * Not currently wired from the UI. */
	[TAG.Runtimes]: 18,
	[TAG.DoorSettings]: 19,
	[TAG.LoadMonitorSettings]: 20,
	[TAG.AuxProgramSettings]: 21,
	[TAG.UserLogSettings]: 22,
	/* PidSettings (firmware field 23) — UI not yet wired. */
	/* GraphFavorites (firmware field 24) — UI not yet wired. */
	[TAG.MasterSlaveSettings]: 25,
	[TAG.DateTime]: 26,
	[TAG.EmailSettings]: 27,
	[TAG.AlertSettings]: 28,
	[TAG.AnalogBoard]: 29,
	[TAG.PwmChannelSettings]: 30,
	[TAG.ServiceInfo]: 31,
	[TAG.BasicSetup]: 32,
	[TAG.PidLogSettings]: 37,
	[TAG.AccountSettings]: 38,
	[TAG.IoConfig]: 39,
	/* IoNameUpdate (firmware field 40, see lp_am2434/main.c:1221) — LP
	 * supports it, but the UI currently still routes IO rename through
	 * the legacy /iot/ioconfig/:idx/:name REST route. Wire here once
	 * the page migrates to /proto/write. */
};

export class ProtoWriteError extends Error {
	constructor(message: string, public readonly status?: number) {
		super(message);
		this.name = 'ProtoWriteError';
	}
}

/** Encode a varint into a growing number[]. */
function writeVarintInto(out: number[], v: number): void {
	let n = v >>> 0;
	while (n > 0x7f) {
		out.push((n & 0x7f) | 0x80);
		n >>>= 7;
	}
	out.push(n & 0x7f);
}

/**
 * Build the wire bytes for a list of forced varint scalar fields.
 * Each entry produces `tag-byte(s) || varint(value)` where tag = (fnum<<3)|0.
 *
 * Use case: proto3 encoders skip fields whose value is the type default
 * (0 for numeric, "" for string). When 0 is *meaningful* (e.g. mode=OFF,
 * index=0, "StartTLS"=0) the firmware decoder needs the field on the
 * wire anyway. Append the result of this helper to a ts-proto-encoded
 * buffer, or pass via `writeProto({forceVarints})`.
 */
export function buildForceVarintBytes(fields: Record<number, number>): Uint8Array {
	const buf: number[] = [];
	for (const [fnumStr, value] of Object.entries(fields)) {
		const fnum = parseInt(fnumStr, 10);
		writeVarintInto(buf, (fnum << 3) | 0);
		writeVarintInto(buf, value);
	}
	return Uint8Array.from(buf);
}

/**
 * Build the wire bytes for a list of forced float (32-bit) scalar fields.
 * Each entry produces `tag-byte(s) || float32-LE(value)` where tag = (fnum<<3)|5.
 *
 * Same proto3 zero-suppression rationale as buildForceVarintBytes — use
 * when 0.0 is a meaningful sensor value, threshold, or coefficient that
 * the firmware decoder must observe on the wire.
 */
export function buildForceFloatBytes(fields: Record<number, number>): Uint8Array {
	const parts: Uint8Array[] = [];
	for (const [fnumStr, value] of Object.entries(fields)) {
		const fnum = parseInt(fnumStr, 10);
		const tag: number[] = [];
		writeVarintInto(tag, (fnum << 3) | 5);
		const buf = new Uint8Array(tag.length + 4);
		buf.set(tag, 0);
		new DataView(buf.buffer, tag.length, 4).setFloat32(0, value, true);
		parts.push(buf);
	}
	let total = 0;
	for (const p of parts) total += p.length;
	const out = new Uint8Array(total);
	let off = 0;
	for (const p of parts) { out.set(p, off); off += p.length; }
	return out;
}

/**
 * Wrap an inner submessage's pre-encoded bytes as field `fnum` of a
 * length-delimited (wire type 2) outer message. Used for hand-building
 * `repeated SubMsg` fields where the inner submessage needs forced
 * zero-valued fields that ts-proto would suppress.
 */
export function wrapAsLengthDelim(fnum: number, inner: Uint8Array): Uint8Array {
	const tagAndLen: number[] = [];
	writeVarintInto(tagAndLen, (fnum << 3) | 2);
	writeVarintInto(tagAndLen, inner.length);
	const out = new Uint8Array(tagAndLen.length + inner.length);
	out.set(tagAndLen, 0);
	out.set(inner, tagAndLen.length);
	return out;
}

export interface WriteProtoOptions {
	/**
	 * Top-level varint fields that must be emitted even when value is 0.
	 * Map of `{fieldNumber: value}`. Appended after ts-proto encoding.
	 * proto3 parsers accept duplicate scalar fields and take the last,
	 * so it's safe to list a field whose value happens to be non-zero
	 * (the duplicate is just 1-2 wasted bytes); only varint-encoded
	 * scalar types (uint32/int32/bool/enum) are supported.
	 */
	forceVarints?: Record<number, number>;
	/**
	 * Top-level float (32-bit) fields that must be emitted even when value
	 * is 0.0. Same semantics as forceVarints — duplicates are safe; only
	 * `float` scalar fields are supported (use forceVarints for ints).
	 */
	forceFloats?: Record<number, number>;
}

async function postProtoBytes(
	envelopeTag: Tag,
	bytes: Uint8Array
): Promise<{ ok: true; field: number; bytes: number }> {
	const field = SETTINGS_FIELD[envelopeTag];
	if (field === undefined) {
		throw new ProtoWriteError(
			`No SettingsUpdate field mapped for envelope tag ${envelopeTag} ` +
				`— add to SETTINGS_FIELD in protoWrite.ts after firmware apply_*() is verified`
		);
	}
	const url = getHttpUrl(`/proto/write/${field}`);
	// Fail-fast on transport hangs (bridge wedged, firmware not ACKing).
	// Without this, fetch() waits indefinitely and the SaveButton sits on
	// "Saving…" forever, masking the real problem. 8 s comfortably covers
	// the bridge's own 3 s ACK timeout plus network latency.
	const ac = new AbortController();
	const timer = setTimeout(() => ac.abort(), 8000);
	let resp: Response;
	try {
		resp = await fetch(url, {
			method: 'POST',
			headers: { 'Content-Type': 'application/octet-stream' },
			// Cast: lib.dom's BodyInit no longer admits a generic
			// Uint8Array<ArrayBufferLike>; the runtime accepts it fine.
			body: bytes as BodyInit,
			signal: ac.signal
		});
	} catch (e: any) {
		clearTimeout(timer);
		if (e?.name === 'AbortError') {
			throw new ProtoWriteError(
				`writeProto(${envelopeTag}/${field}) timed out after 8s — bridge may be wedged`
			);
		}
		throw new ProtoWriteError(
			`writeProto(${envelopeTag}/${field}) network error: ${e?.message ?? e}`
		);
	}
	clearTimeout(timer);

	if (!resp.ok) {
		let detail = '';
		try {
			const j = await resp.json();
			detail = j?.error ?? '';
		} catch {
			/* body wasn't JSON; ignore */
		}
		throw new ProtoWriteError(
			`writeProto(${envelopeTag}/${field}) failed: HTTP ${resp.status}${detail ? ' — ' + detail : ''}`,
			resp.status
		);
	}

	return resp.json();
}

/**
 * Encode a typed proto message and POST it to the bridge.
 * Resolves on bridge acknowledgement; rejects on transport error.
 *
 * NOTE: success here means "bridge forwarded to firmware and got an ACK".
 * To confirm the value actually landed in firmware state, await the next
 * frame on the corresponding /proto/stream tag.
 */
export async function writeProto<K extends Tag>(
	envelopeTag: K,
	msg: Partial<TagPayload[K]>,
	opts?: WriteProtoOptions
): Promise<{ ok: true; field: number; bytes: number }> {
	const decoder = TAG_DECODERS[envelopeTag];
	// ts-proto encoders test `if (message.x !== 0)` / `!== ""` per field.
	// A bare partial leaves omitted scalars as `undefined`, which passes
	// those checks and causes `writer.uint32(undefined)` → "invalid uint32".
	// fromPartial() fills in proto3 defaults (0 / "" / []) so suppression
	// works correctly. See docs/firmware-bridge-protocol.md.
	const filled = decoder.fromPartial(msg as any);
	let bytes: Uint8Array = decoder.encode(filled).finish();
	// Pull declared force-emit fields from the registry; explicit opts
	// override per-field so legacy call sites still work during the sweep.
	const reg = resolveForceFields(envelopeTag, filled);
	const mergedVarints = (reg.forceVarints || opts?.forceVarints)
		? { ...(reg.forceVarints ?? {}), ...(opts?.forceVarints ?? {}) }
		: undefined;
	const mergedFloats = (reg.forceFloats || opts?.forceFloats)
		? { ...(reg.forceFloats ?? {}), ...(opts?.forceFloats ?? {}) }
		: undefined;
	if (mergedVarints) {
		const extra = buildForceVarintBytes(mergedVarints);
		if (extra.length) {
			const merged = new Uint8Array(bytes.length + extra.length);
			merged.set(bytes, 0);
			merged.set(extra, bytes.length);
			bytes = merged;
		}
	}
	if (mergedFloats) {
		const extra = buildForceFloatBytes(mergedFloats);
		if (extra.length) {
			const merged = new Uint8Array(bytes.length + extra.length);
			merged.set(bytes, 0);
			merged.set(extra, bytes.length);
			bytes = merged;
		}
	}
	return postProtoBytes(envelopeTag, bytes);
}

/**
 * Lower-level escape hatch: POST raw pre-encoded inner bytes to the
 * bridge under the given envelope tag's SettingsUpdate field. Use when
 * the typed encoder cannot express the required wire layout (e.g.
 * `repeated SubMsg` where the inner submessage needs forced
 * zero-valued fields, or mode-positional encoders).
 */
export async function writeProtoRaw(
	envelopeTag: Tag,
	rawInnerBytes: Uint8Array
): Promise<{ ok: true; field: number; bytes: number }> {
	return postProtoBytes(envelopeTag, rawInnerBytes);
}

/**
 * Minimal ts-proto codec shape — `encode(msg).finish()` plus optional
 * `fromPartial(p)` for proto3-default backfill. Both are emitted by
 * ts-proto on every message type.
 */
export interface ProtoRowCodec<R> {
	encode(message: R): { finish(): Uint8Array };
	fromPartial?(object: any): R;
}

/**
 * Encode one typed submessage `row`, append `forceVarints` /
 * `forceFloats` for any zero-meaningful fields, wrap as
 * `repeated R rows = repeatedFieldNum` (length-delimited), and POST.
 *
 * Replaces the per-page byte-merging dance:
 *
 *   const inner = Codec.encode(row).finish();
 *   const v = buildForceVarintBytes({...});
 *   const merged = new Uint8Array(inner.length + v.length);
 *   merged.set(inner, 0); merged.set(v, inner.length);
 *   const wrapped = wrapAsLengthDelim(N, merged);
 *   await writeProtoRaw(TAG.X, wrapped);
 *
 * with:
 *
 *   await writeProtoRow(TAG.X, N, Codec, row, { forceVarints: {...} });
 */
export async function writeProtoRow<K extends Tag, R>(
	envelopeTag: K,
	repeatedFieldNum: number,
	rowCodec: ProtoRowCodec<R>,
	row: R,
	opts?: WriteProtoOptions
): Promise<{ ok: true; field: number; bytes: number }> {
	const filled = rowCodec.fromPartial ? rowCodec.fromPartial(row as any) : row;
	let inner: Uint8Array = rowCodec.encode(filled).finish();
	// Per-row force fields are caller-declared (the registry is keyed by
	// envelope-level Tag, not by row codec). Apply only opts.* here.
	if (opts?.forceVarints) {
		const extra = buildForceVarintBytes(opts.forceVarints);
		if (extra.length) {
			const merged = new Uint8Array(inner.length + extra.length);
			merged.set(inner, 0);
			merged.set(extra, inner.length);
			inner = merged;
		}
	}
	if (opts?.forceFloats) {
		const extra = buildForceFloatBytes(opts.forceFloats);
		if (extra.length) {
			const merged = new Uint8Array(inner.length + extra.length);
			merged.set(inner, 0);
			merged.set(extra, inner.length);
			inner = merged;
		}
	}
	return postProtoBytes(envelopeTag, wrapAsLengthDelim(repeatedFieldNum, inner));
}
