import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  
  const result = await loadIotData('/iot/master', fetch);
  return { array: result };
}
