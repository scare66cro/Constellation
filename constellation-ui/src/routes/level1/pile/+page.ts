import { loadIotData, parseSensorFeeds } from "$lib/business/util";

export async function load({fetch}) {
  // Fetch sensor data for pile sensors (boards 3+)
  // /iot/sensors endpoint already returns boards 3+ only (boards 1 & 2 excluded)
  const response = await loadIotData('/iot/sensors', fetch);
  const sensors = parseSensorFeeds(response);
  return { sensors };
}
