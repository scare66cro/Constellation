import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {

  const response = await loadIotData('/iot/service', fetch);
  return { array: response };
}
