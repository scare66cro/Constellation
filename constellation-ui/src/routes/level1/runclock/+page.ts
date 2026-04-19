import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  const response = await loadIotData('/iot/runtimes', fetch);
  return { array: response ? Object.values(response) : [] };
}
