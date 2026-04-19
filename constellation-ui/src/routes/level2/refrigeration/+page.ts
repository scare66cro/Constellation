import { loadIotData, getHttpUrl } from "$lib/business/util";

export async function load({ fetch }) {
  const result = await loadIotData('/iot/refrigeration', fetch);

  // Triton-orbit list (used by the SCADA section at the bottom of the page).
  // Always returns 200 with { tritons: [...] } from the bridge — empty array
  // means no Tritons present and the SCADA section stays hidden.
  let tritons: { slot: number; connected: boolean; label: string }[] = [];
  try {
    if (typeof window !== 'undefined') {
      const resp = await fetch(getHttpUrl('/iot/triton/list'));
      if (resp.ok) {
        const data = await resp.json();
        if (Array.isArray(data?.tritons)) tritons = data.tritons;
      }
    }
  } catch { /* SCADA section will simply not render */ }

  return { array: result, tritons };
}
