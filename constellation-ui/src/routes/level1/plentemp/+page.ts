import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  const response = await loadIotData('/iot/plensetup', fetch);
  // Always return an object with the data nested under a key
  return { array: response };
}