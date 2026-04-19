import { loadIotData } from "$lib/business/util";

export async function load({fetch}) {
  const [users, meta] = await Promise.all([
    loadIotData('/iot/accounts', fetch),
    loadIotData('/iot/accounts-meta', fetch).catch(() => null),
  ]);
  return { array: users, meta };
}
