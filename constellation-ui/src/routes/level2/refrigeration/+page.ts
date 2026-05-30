import { getHttpUrl } from "$lib/business/util";

/**
 * Phase 5.1: refrig settings now hydrate from the RefrigSettings proto
 * store ($refrigSettings). This loader only fetches the bridge-side
 * Triton sidecar (orbit-only metadata, no proto representation).
 */
export async function load({ fetch }) {
  let tritons = [];
  try {
    if (typeof window !== "undefined") {
      const resp = await fetch(getHttpUrl("/iot/triton/list"));
      if (resp.ok) {
        const j = await resp.json();
        if (Array.isArray(j?.tritons)) tritons = j.tritons;
      }
    }
  } catch { /* SCADA section will simply not render */ }
  return { tritons };
}