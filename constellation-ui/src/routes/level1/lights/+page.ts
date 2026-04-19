import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  const response = await loadIotData('/iot/lights', fetch);
  return response;
}