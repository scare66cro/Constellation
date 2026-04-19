import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  const result = await loadIotData('/iot/basic', fetch);
  return { array: result };
}
