import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  const result = await loadIotData('/iot/date', fetch);
  return { array: result || ['', '', '0'] };
}
