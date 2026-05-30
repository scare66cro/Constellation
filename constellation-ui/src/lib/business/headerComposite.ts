/**
 * headerComposite.ts — Client-side replacement for the bridge's
 * `header-data` WebSocket channel (`dataCache.buildHeaderData()`).
 *
 * Derives the legacy `Headers` shape `{DateTime, CurrentMode, PanelName}`
 * from typed proto stores:
 *
 *   - `DateTime`   ← `dateTime` store (envelope tag 21) — formatted as
 *                    `[date_str, time_str, ampm_str]` to match the legacy
 *                    UI contract (GellertHeader/RunTime treat it as an
 *                    array even though `Headers.DateTime` is typed `string`).
 *                    When firmware hasn't pushed a DateTime yet we fall
 *                    back to the host clock so the header isn't blank.
 *   - `CurrentMode` ← `systemStatus.currentMode`. Nova firmware now
 *                     publishes UI_FAILURE (21) directly when ST_FAILURE
 *                     latches — including via the VFD-faulted path in
 *                     `nova_failures.c::FanFailChk` (which OR-s in
 *                     `nova_vfd_any_faulted()` and reuses the existing
 *                     `Settings.Failure[FAIL_FAN].Timer` escalation).
 *                     The bridge no longer overrides mode based on
 *                     `vfdStatus.anyFault`; the UI doesn't either.
 *   - `PanelName`   ← `basicSetup.storageName`, default 'Agristar Panel'.
 *
 * Wired in `+layout.svelte` (single-point bridge), so every component
 * that reads `$headersStore.*` automatically gets the proto-derived
 * value with no per-component changes.
 */

import { derived, type Readable } from 'svelte/store';
import { basicSetup, dateTime, systemStatus } from './protoStores.js';
import type { Headers } from '../store.js';

function fallbackDateTime(): [string, string, string] {
	const now = new Date();
	const h = now.getHours();
	const date = `${String(now.getMonth() + 1).padStart(2, '0')}/${String(now.getDate()).padStart(2, '0')}/${now.getFullYear()}`;
	const hh = h > 12 ? h - 12 : h || 12;
	const time = `${String(hh).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}:${String(now.getSeconds()).padStart(2, '0')}`;
	const ampm = h >= 12 ? '1' : '0';
	return [date, time, ampm];
}

export const headerComposite: Readable<Headers> = derived(
	[dateTime, systemStatus, basicSetup],
	([$dt, $ss, $bs]) => {
		// DateTime — proto-supplied, else host-clock fallback (matches
		// legacy `buildHeaderData()` behavior when ARM hasn't pushed yet).
		const dt: [string, string, string] = $dt
			? [$dt.dateStr ?? '', $dt.timeStr ?? '', String($dt.amPm ?? 0)]
			: fallbackDateTime();

		// CurrentMode — straight from firmware. Nova sets UI_FAILURE (21)
		// itself when ST_FAILURE latches; no client-side override needed.
		const mode = $ss?.currentMode ?? 0;

		const panelName = $bs?.storageName?.trim() || 'Agristar Panel';

		return {
			// DateTime is typed `string` in the legacy interface but the UI
			// (GellertHeader, RunTime) reads it as `string[]`. Cast through
			// unknown to keep the public type stable while preserving the
			// runtime array shape consumers depend on.
			DateTime: dt as unknown as string,
			CurrentMode: mode,
			PanelName: panelName,
			EStop: $ss?.estopActive ?? false
		};
	},
	{ DateTime: '' as unknown as string, CurrentMode: 0, PanelName: 'Agristar Panel' }
);
