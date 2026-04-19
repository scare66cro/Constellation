import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  
  const result = await loadIotData('/iot/pid', fetch);

  return {
    pidWrap: result?.[0] || '',
    logDoors: result?.[1] || '',
    logRefrig: result?.[2] || ''
  };
}

