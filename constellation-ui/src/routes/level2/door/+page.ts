import { loadIotData, getHttpUrl } from "$lib/business/util";

export async function load({fetch}) {
  
  const result = await loadIotData('/iot/door', fetch);

  // Fetch GDC orbit data (may not be present)
  let gdc = null;
  try {
    if (typeof window !== 'undefined') {
      const resp = await fetch(getHttpUrl('/iot/gdc'));
      if (resp.ok) {
        const data = await resp.json();
        if (data.present) gdc = data;
      }
    }
  } catch { /* GDC not available */ }

  return { array: result, gdc };
}
