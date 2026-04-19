import { loadIotData } from "$lib/business/util";
import { writable } from "svelte/store";

// Create a temporary store for managing wait state during loading
const waitStore = writable(false);

export async function load({fetch}) {
  
  let response: Response;
  try {
    // Set the initial wait state to true
    waitStore.set(true);
    
    response = await loadIotData('/iot/version', fetch, (isWaiting) => waitStore.set(isWaiting));

    return { ...response, status: response.status };
  } catch (error) {
    console.error("Error loading version data:", error);
    
    // Keep showing the spinner for a moment before giving up
    setTimeout(() => waitStore.set(false), 5000);
    
    return { status: 500 };
  }
}
