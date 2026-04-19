import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  const result = await loadIotData('/iot/network', fetch);
  return { array: result };
}
