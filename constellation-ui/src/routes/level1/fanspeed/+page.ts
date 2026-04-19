import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  const result = await loadIotData('/iot/fanspeed', fetch);
  return result;
}