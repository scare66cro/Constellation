import { loadIotData } from "$lib/business/util";

/**
 * Phase 5.1: username list now hydrates from the AccountSettings proto
 * store ($accountSettings.users). This loader only fetches the
 * bridge-side metadata sidecar (per-slot role, last-login, cloud links,
 * current-session info) which is not part of the firmware proto surface.
 */
export async function load({ fetch }) {
  const meta = await loadIotData("/iot/accounts-meta", fetch).catch(() => null);
  return { meta };
}