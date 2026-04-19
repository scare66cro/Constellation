import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  const result = await loadIotData('/vfd/fans', fetch);
  return { fans: result };
}
