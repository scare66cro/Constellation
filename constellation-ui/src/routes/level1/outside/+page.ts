import { loadIotData, parseSensorFeeds } from "$lib/business/util";

export async function load({fetch}) {
  const payload = await loadIotData('/iot/outside', fetch);
  const sensors = parseSensorFeeds((payload as any)?.sensors ?? payload);
  return { ...payload, sensors };
}