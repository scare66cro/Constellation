/**
 * alertMetadata.ts — client-side derivation of alert labels + indices
 *
 * Previously served by the bridge endpoints `/iot/alert/labels` and
 * `/iot/alert/indices`, which were pure static derivations of the
 * `WARNING_KEYS` / `DEFAULT_WARNING_TEXT` tables (already mirrored
 * in `warningKeys.ts` / `warningText.ts`) plus a board-type filter.
 *
 * Constellation hardware is always Agri-Star (no AS2 / MiniIO expansion
 * board), so we only need the AGRISTAR exclude set. The legacy AS2
 * branch lives in the bridge for reference but is unreachable here.
 *
 * Lists must stay in lock-step with the canonical bridge sets in
 * `apiRoutes.ts::AGRISTAR_EXCLUDES` + `NON_ALERT_KEYS`.
 */
import { WARNING_KEYS } from './warningKeys.js';
import { DEFAULT_WARNING_TEXT } from './warningText.js';

const AGRISTAR_EXCLUDES = new Set<string>([
	'WARN_REFRIG_DEFROST',
	'WARN_REFRIG_PWM',
	'WARN_REFRIG_STAGE',
	'WARN_HUMIDIFIER',
	'WARN_AUX',
	'WARN_SYSCONFIG_EQ',
	'WARN_NO_OUTPUT',
	'WARN_EXPANSIONBOARD',
	'WARN_LIGHTS'
]);

const NON_ALERT_KEYS = new Set<string>(['WARN_ALARMS_FILE', 'WARN_EQUIPDESC_FILE']);

const WARN_NEWBOARD = WARNING_KEYS.indexOf('WARN_NEWBOARD');

/** Full-bitmap WARNING_KEYS indices that map to user-toggleable alerts. */
export function getIncludedAlertIndices(): number[] {
	const indices: number[] = [];
	for (let i = 0; i < WARNING_KEYS.length; i++) {
		const key = WARNING_KEYS[i];
		if (NON_ALERT_KEYS.has(key)) continue;
		if (AGRISTAR_EXCLUDES.has(key)) continue;
		indices.push(i);
	}
	return indices;
}

/**
 * Human-readable labels for each visible (filtered) alert, in the same
 * order as `getIncludedAlertIndices()`. The sentinel `'group2'` is
 * inserted before WARN_NEWBOARD to split primary vs secondary alerts
 * in the UI.
 */
export function getFilteredAlertLabels(): string[] {
	const labels: string[] = [];
	const included = getIncludedAlertIndices();
	for (const idx of included) {
		if (idx === WARN_NEWBOARD) labels.push('group2');
		const key = WARNING_KEYS[idx] ?? `WARN_UNKNOWN_${idx}`;
		labels.push(DEFAULT_WARNING_TEXT[key] ?? key);
	}
	return labels;
}
