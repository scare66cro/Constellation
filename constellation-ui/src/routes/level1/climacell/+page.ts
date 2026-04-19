import { safeJsonParse, loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  const result = await loadIotData('/iot/climacelltimes', fetch);
  return { runtimes: result ? Object.values(result) : [] };
}