import { loadIotData } from "$lib/business/util";

export async function load({ fetch }) {
  // Primary CO2 array (used by all rows on this page).
  // Refrigeration array is loaded alongside so the refrig-purge-mode + threshold
  // controls (formerly on /level2/refrigeration) can live here.  They save to
  // a separate IoT route via their own SaveButton.
  const [array, refrig] = await Promise.all([
    loadIotData('/iot/co2', fetch),
    loadIotData('/iot/refrigeration', fetch).catch(() => [] as string[]),
  ]);
  return { array, refrig };
}
