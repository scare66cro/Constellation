import { getHttpUrl } from "$lib/business/util";

// /iot/door has been migrated to the typed proto store `$doorSettings`
// (TAG.DoorSettings = 56). This loader only fetches GDC orbit data,
// which has no proto representation yet (separate /iot/gdc orbit flow).
export async function load({fetch}) {
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

  return { gdc };
}
