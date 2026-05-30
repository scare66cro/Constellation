/**
 * useDraft — proto-direct page state helper.
 *
 * Replaces the legacy "buildXxxData → string[N] → save by positional
 * encode" pattern (see `agristar-principles.md`). A draft holds an
 * editable copy of a typed proto message, tracks whether it diverges
 * from the live store frame, and ships the typed message back to the
 * bridge via `writeProto(TAG, draft)`.
 *
 * Usage in a +page.svelte:
 *
 *   const misc = useDraft(miscSettings, TAG.MiscSettings);
 *   $: ({ draft, dirty, hydrated, save, revert } = misc);
 *
 *   <Select bind:value={$draft.refrigMode} options={...} />
 *   <SaveButton autoSave data={$draft} original={$misc.live}
 *               onSave={() => $misc.save()} />
 *
 * Why this exists:
 * Positional `string[]` arrays caused cascading off-by-one bugs — a
 * single inserted slot shifted every following field, sometimes into
 * out-of-range firmware values. Binding to named typed fields makes
 * those drifts impossible: `draft.refrigMode` is unambiguous.
 *
 * Hydration policy:
 * The draft snapshots the first non-null store frame and ignores
 * subsequent live frames while the user is editing (dirty===true).
 * On a successful `save()`, the local edit is committed and the draft
 * snaps to the next live frame. `revert()` discards local edits and
 * resnaps to the current live value. This matches the behaviour
 * pages had with `cloneDeep`/`isEqual` against `original`, but
 * without the per-page boilerplate.
 *
 * See also: docs/proto-direct-redesign-plan.md, docs/proto-migration-pattern.md.
 */

import { derived, get, writable, type Readable, type Writable } from 'svelte/store';
import { cloneDeep, isEqual } from 'lodash-es';
import {
	writeProto,
	writeProtoRow,
	type ProtoRowCodec,
	type WriteProtoOptions
} from './protoWrite.js';
import type { Tag, TagPayload } from './protoTags.js';
export interface ProtoDraft<K extends Tag> {
	/** Editable typed message — bind UI inputs to its fields. */
	readonly draft: Writable<TagPayload[K]>;
	/** True once the first live frame has been snapshotted into draft. */
	readonly hydrated: Readable<boolean>;
	/** True when draft diverges from the last live frame. */
	readonly dirty: Readable<boolean>;
	/** Last live frame from the store (read-only snapshot for SaveButton.original). */
	readonly live: Readable<TagPayload[K] | null>;
	/** POST current draft via writeProto and resync on success. */
	save(opts?: WriteProtoOptions): Promise<void>;
	/** Discard local edits, snap draft back to current live frame. */
	revert(): void;
}

/**
 * Bind a typed proto store to an editable draft for a single page.
 *
 * @param store typed store from `protoStores.ts` (e.g. `miscSettings`)
 * @param tag matching envelope tag (e.g. `TAG.MiscSettings`)
 */
export function useDraft<K extends Tag>(
	store: Readable<TagPayload[K] | null>,
	tag: K
): ProtoDraft<K> {
	const draft: Writable<TagPayload[K]> = writable({} as TagPayload[K]);
	const hydrated = writable(false);
	const live = writable<TagPayload[K] | null>(null);

	// Snapshot the first non-null frame; once hydrated, only update
	// `live` so `dirty` can compare against the latest server truth.
	store.subscribe((frame) => {
		if (frame == null) return;
		live.set(frame);
		if (!get(hydrated)) {
			draft.set(cloneDeep(frame));
			hydrated.set(true);
		}
	});

	const dirty: Readable<boolean> = derived([draft, live], ([$d, $l]) => {
		if (!$l) return false;
		return !isEqual($d, $l);
	});

	async function save(opts?: WriteProtoOptions): Promise<void> {
		const current = get(draft);
		await writeProto(tag, current, opts);
		// Commit locally — the next live frame from the bridge will
		// replace this on its own. Setting `live` here keeps `dirty`
		// false until the firmware echo lands.
		live.set(cloneDeep(current));
	}

	function revert(): void {
		const l = get(live);
		if (l) draft.set(cloneDeep(l));
	}

	return { draft, hydrated, dirty, live, save, revert };
}

/**
 * numField — two-way string↔number bridge for a single numeric proto
 * field driving a string-typed input (TextField, etc.).
 *
 * Returns a writable<string> whose set() writes back through the draft
 * as a number (via parseFloat for floats, parseInt for ints). Initial
 * value mirrors the current draft field. NaN inputs land as 0.
 *
 *   const defrostStr = numField(draft, 'defrostInterval', 'int');
 *   <TextField bind:value={$defrostStr} />
 *
 * Replaces the per-page boilerplate:
 *
 *   let defrostStr = '0';
 *   $: if (hydrated) defrostStr = String($draft.defrostInterval);
 *   $: if (hydrated) $draft.defrostInterval = parseInt(defrostStr) || 0;
 */
export function numField<T extends object>(
	draft: Writable<T>,
	key: keyof T,
	kind: 'int' | 'float' = 'int'
): Writable<string> {
	const parse = kind === 'float' ? parseFloat : (s: string) => parseInt(s, 10);

	const init = String((get(draft) as any)[key] ?? 0);
	const str = writable(init);

	// Keep string in sync when the draft is replaced wholesale (e.g.
	// `revert()`). Skip when the draft change came from us writing
	// through `str.set()` (last-write equality check covers it).
	draft.subscribe((d) => {
		const v = (d as any)[key];
		const next = v == null ? '0' : String(v);
		if (get(str) !== next && parse(get(str)) !== v) str.set(next);
	});

	return {
		subscribe: str.subscribe,
		set: (v: string) => {
			str.set(v);
			const n = parse(v);
			draft.update((d) => ({ ...d, [key]: Number.isFinite(n) ? n : 0 }));
		},
		update: (fn) => {
			str.update((cur) => {
				const next = fn(cur);
				const n = parse(next);
				draft.update((d) => ({ ...d, [key]: Number.isFinite(n) ? n : 0 }));
				return next;
			});
		}
	};
}

// ────────────────────────────────────────────────────────────────────
// useDraftRepeated — per-row editing of a `repeated SubMsg` settings
// page. Edits one row at a time; saves just that row wrapped as
// `repeated R rows = N` (length-delimited) via writeProtoRow.
//
// Why a dedicated hook: pages like humidifier / accounts pick a single
// row from a repeated submsg, edit it, and ship only that row — the
// firmware merges by `index`. The boilerplate (encode → forced bytes →
// length-delim wrap → POST) was duplicated at every call site. This
// hook owns it; the page only declares the codec, the parent field
// number, and which fields need force-emit.
//
// Hydration policy mirrors useDraft: snapshot the selected row from
// the first non-null live frame; ignore subsequent live frames while
// dirty; on save() commit locally and let the next firmware echo
// resync. Switching `select(idx)` while dirty discards the in-progress
// edit (the page is responsible for prompting the user if needed —
// `dirty` is exposed for that).
// ────────────────────────────────────────────────────────────────────

export interface ProtoDraftRow<R> {
	/** Editable typed copy of the currently selected row. */
	readonly draft: Writable<R>;
	/** True once the first live frame has been snapshotted. */
	readonly hydrated: Readable<boolean>;
	/** True when draft diverges from the last live row at this index. */
	readonly dirty: Readable<boolean>;
	/** Last live row from the store, or null if none at this index. */
	readonly live: Readable<R | null>;
	/** Currently selected row index (0-based). */
	readonly selectedIndex: Readable<number>;
	/** Switch to a different row. Discards any in-progress edit. */
	select(index: number): void;
	/** POST current draft as a single repeated-row update. */
	save(opts?: WriteProtoOptions): Promise<void>;
	/** Discard local edits, snap draft back to current live row. */
	revert(): void;
}

export interface UseDraftRepeatedOpts<S, R> {
	/** Property name on the parent message holding the repeated rows. */
	rowsKey: keyof S;
	/** ts-proto codec for one row (e.g. `HumidCtrlEntry`). */
	rowCodec: ProtoRowCodec<R>;
	/** Field number on the parent for `repeated R rows = N`. */
	repeatedFieldNum: number;
	/** Initial row index to select (default 0). */
	initialIndex?: number;
	/** Factory for an empty row when the live frame has no entry at idx. */
	defaultRow: (index: number) => R;
	/** Function returning a row's logical index (default: `(r) => r.index`). */
	rowKey?: (row: R) => number;
	/** Per-row force-varints map (called with current row at save time). */
	forceVarints?: (row: R) => Record<number, number>;
	/** Per-row force-floats map (called with current row at save time). */
	forceFloats?: (row: R) => Record<number, number>;
}

export function useDraftRepeated<K extends Tag, R>(
	store: Readable<TagPayload[K] | null>,
	tag: K,
	opts: UseDraftRepeatedOpts<TagPayload[K], R>
): ProtoDraftRow<R> {
	const rowKey = opts.rowKey ?? ((r: R) => (r as any).index as number);
	const selectedIndex = writable<number>(opts.initialIndex ?? 0);
	const draft = writable<R>(opts.defaultRow(get(selectedIndex)));
	const hydrated = writable(false);
	const live = writable<R | null>(null);

	function findRow(frame: TagPayload[K] | null, idx: number): R | null {
		if (!frame) return null;
		const arr = (frame as any)[opts.rowsKey] as R[] | undefined;
		if (!arr) return null;
		return arr.find((r) => rowKey(r) === idx) ?? null;
	}

	function snapshot(idx: number, frame: TagPayload[K] | null): void {
		const found = findRow(frame, idx);
		live.set(found);
		draft.set(found ? cloneDeep(found) : opts.defaultRow(idx));
	}

	// Track latest store frame so select() can resnap without waiting
	// for another push.
	let latestFrame: TagPayload[K] | null = null;

	store.subscribe((frame) => {
		latestFrame = frame;
		if (frame == null) return;
		const idx = get(selectedIndex);
		const found = findRow(frame, idx);
		live.set(found);
		if (!get(hydrated)) {
			draft.set(found ? cloneDeep(found) : opts.defaultRow(idx));
			hydrated.set(true);
		}
	});

	const dirty: Readable<boolean> = derived([draft, live], ([$d, $l]) => {
		if (!$l) {
			// No live row yet — treat any non-default draft as dirty so
			// the user can save a brand-new row.
			return !isEqual($d, opts.defaultRow(get(selectedIndex)));
		}
		return !isEqual($d, $l);
	});

	function select(idx: number): void {
		selectedIndex.set(idx);
		snapshot(idx, latestFrame);
	}

	async function save(extra?: WriteProtoOptions): Promise<void> {
		const row = get(draft);
		const merged: WriteProtoOptions = {
			forceVarints: {
				...(opts.forceVarints ? opts.forceVarints(row) : {}),
				...(extra?.forceVarints ?? {})
			},
			forceFloats: {
				...(opts.forceFloats ? opts.forceFloats(row) : {}),
				...(extra?.forceFloats ?? {})
			}
		};
		await writeProtoRow(tag, opts.repeatedFieldNum, opts.rowCodec, row, merged);
		// Commit locally — next firmware echo will replace.
		live.set(cloneDeep(row));
	}

	function revert(): void {
		const idx = get(selectedIndex);
		snapshot(idx, latestFrame);
	}

	return {
		draft,
		hydrated,
		dirty,
		live,
		selectedIndex,
		select,
		save,
		revert
	};
}
