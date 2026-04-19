import { loadIotData } from "$lib/business/util";
import type { PageLoad } from "./$types";

export const load: PageLoad = async ({ fetch }) => {
  // Fetch all sensors including boards 1 & 2 for system monitor display
  const response = await loadIotData('/iot/sensors/all', fetch);
  return { array: response ? Object.values(response) : [] };
};