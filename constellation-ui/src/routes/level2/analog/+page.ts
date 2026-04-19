import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  const [allBoards, unifiedSensors] = await Promise.all([
    loadIotData('/iot/analog/all', fetch),
    loadIotData('/iot/sensors/unified', fetch)
  ]);
  return { allBoards: allBoards as string[][], unifiedSensors: unifiedSensors as any };
}
