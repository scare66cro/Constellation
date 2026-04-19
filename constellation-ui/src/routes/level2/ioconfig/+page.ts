import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  
  const result = await loadIotData('/iot/ioconfig', fetch);
  
  return {
    ioAvailable: result?.ioAvailable || [],
    config: {
      outputConfig: result?.config.outputConfig || [],
      inputConfig: result?.config.inputConfig || [],
    },
    ioNames: result?.ioNames || {},
    systemMode: result?.systemMode || '0',
  };
}

