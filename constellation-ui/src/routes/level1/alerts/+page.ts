import { safeJsonParse, loadIotData } from "$lib/business/util";

/**
 * Loads alert enable flags and their labels. Accepts multiple backend shapes:
 *  - [ "1", "0", ... ]
 *  - { array: [ "1", "0", ... ] }
 *  - labels endpoint: [ "label1", ... ] OR { array: [ "label1", ... ] }
 * Falls back to empty arrays (never returns objects) so the Svelte page can
 * safely call array methods during SSR without type errors.
 */
export async function load({ fetch }) {
  try {
    const [alertsResponseRaw, labelsResponseRaw] = await Promise.all([
      loadIotData('/iot/alerts', fetch),
      loadIotData('/iot/alert/labels', fetch)
    ]);

    const toStringArray = (val: any): string[] => {
      if (Array.isArray(val)) return val as string[];
      if (val && typeof val === 'object' && Array.isArray(val.array)) return val.array as string[];
      return [];
    };

    const alertStrings = toStringArray(alertsResponseRaw);
    const alerts = alertStrings.map(a => a === '1');
    const labels = toStringArray(labelsResponseRaw);

    return { alerts, labels };
  } catch (err) {
    console.error('Error loading alerts data:', err);
    return { alerts: [], labels: [] };
  }
}