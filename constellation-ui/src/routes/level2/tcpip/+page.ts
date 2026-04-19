import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  return await loadIotData('/iot/tcpip', fetch);
}
