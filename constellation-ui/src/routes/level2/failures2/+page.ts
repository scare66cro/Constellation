import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  
  const result = await loadIotData('/iot/failures2', fetch);
  return { array: result };
}
