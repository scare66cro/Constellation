import { navigationStore, headersStore } from "$lib/store";
import { get } from "svelte/store";
import { getHttpUrl, safeJsonParse } from "./util";

export type NetworkNode = {
  text: string;
  value: string;
  id?: string;  // persistent Nova UUID — survives DHCP IP changes
};
export type NetworkPanel = {
  storage: string;
  mode: string;
  alarm: string;
  set: string;
  plen: string;
  tempColor: string;
  ret: string;
  hum: string;
  humidColor: string;
  fullAddress: string;
  panelName: string;
};
export type NetworkData = {
  NetworkMonitor: string[];
  ClientIpAdd: string[];
  LocalIpAdd: string[];
  LocalIpMask: string[];
};
/**
 * Processes NetworkMonitor data to extract network panels with subnet filtering
 */
export function processNetworkMonitorData(data: NetworkData): {
  nodes: NetworkNode[];
  panels: NetworkPanel[];
} {
  const networkData = data.NetworkMonitor;
  const ClientIpAdd = data.ClientIpAdd[0];
  const LocalIpAdd = data.LocalIpAdd[0];
  const LocalIpMask = data.LocalIpMask[0];
  
  if (!networkData || networkData.length === 0) {
    return { nodes: [], panels: [] };
  }
  const client = ClientIpAdd.split('.');
  const mask = LocalIpMask.split('.');
  const local = LocalIpAdd.split('.');
  
  const nodes: NetworkNode[] = [];
  const panels: NetworkPanel[] = [];
  
  // Create a copy and remove last empty element if it exists
  const processedNetworkData = [...networkData];
  if (processedNetworkData[processedNetworkData.length - 1] === '') {
    processedNetworkData.pop();
  }
  for (let i = 0; i < processedNetworkData?.length; i += 12) {
    let onSubnet = true;
    
    // Extract full address (ip:port) and IP parts for subnet checking
    const fullAddress = processedNetworkData[i];
    if (!fullAddress) continue;
    
    const ip = fullAddress.split(':')[0].split('.');
    
    // Check if on same subnet
    for (let m = 0; m < 4; m += 1) {
      if (mask[m] === '255' && local[m] !== ip[m]) {
        onSubnet = false;
        break;
      }
    }
    
    if (!onSubnet) {
      continue;
    }
    
    // Extract panel name and create display string
    const panelName = processedNetworkData[i + 1] || fullAddress;
    const displayAddress = fullAddress ? `(${fullAddress})` : '';
    
    // Create node for dropdown
    nodes.push({
      text: panelName,
      value: fullAddress
    });
    
    // Create panel data for network page
    panels.push({
      storage: `${panelName} ${displayAddress}`,
      mode: processedNetworkData[i + 2] || '',
      alarm: processedNetworkData[i + 3] || '',
      set: processedNetworkData[i + 4] || '',
      plen: processedNetworkData[i + 5] || '',
      tempColor: processedNetworkData[i + 6] || '',
      ret: processedNetworkData[i + 7] || '',
      hum: processedNetworkData[i + 8] || '',
      humidColor: processedNetworkData[i + 9] || '',
      fullAddress,
      panelName
    });
  }
  return { nodes, panels };
}
/**
 * Helper function to normalize addresses for comparison
 * Removes port 80 since it's the default and handles edge cases
 */
function normalizeAddress(address: string): string {
  if (!address) return '';
  if (address.includes(':')) {
    const [host, port] = address.split(':');
    return port === '80' ? host : address;
  }
  return address;
}

/**
 * Updates the navigation store with network nodes for header dropdown
 * Deduplicates nodes based on normalized address values
 * Also stores localIP for deduplication when on loopback panel
 */
export function updateNavigationNodes(nodes: NetworkNode[], localIP?: string): void {
  // Guard against undefined/null/non-array nodes (can happen when controller is in bootloader mode)
  if (!nodes || !Array.isArray(nodes)) {
    return;
  }
  
  // Deduplicate nodes based on normalized address values
  const seenAddresses = new Set<string>();
  const uniqueNodes: NetworkNode[] = [];
  
  for (const node of nodes) {
    const normalizedValue = normalizeAddress(node.value);
    if (!seenAddresses.has(normalizedValue)) {
      seenAddresses.add(normalizedValue);
      uniqueNodes.push(node);
    }
  }
  
  navigationStore.update((current) => {
    current.nodes = uniqueNodes;
    // Update localIP if provided
    if (localIP !== undefined) {
      current.localIP = localIP;
    }
    return current;
  });
}

export async function processNetworkNodes(nodes: string[], addOffset: number, ipVal?: string, portVal?: string): Promise<Array<{text: string, value: string}>> {
  // Handle missing or malformed node arrays gracefully (bootloader mode)
  if (!Array.isArray(nodes) || nodes.length === 0) {
    return get(navigationStore).nodes;
  }

  const ip = ipVal ?? await getIP();
  const port = portVal ?? await getPort();
  const navigation = get(navigationStore);
  const header = get(headersStore);
  const temp: Array<{text: string, value: string}> = [];
  const seenAddresses = new Set<string>(); // Track seen addresses to avoid duplicates
  
  for (let i = 0; i < nodes.length; i+=4) {
    const host = nodes[i + addOffset];
    const portFromNode = nodes[i + 2];
    if (!host || !portFromNode) continue; // skip incomplete entries

    const value = `${host}:${portFromNode}`;
    const normalizedValue = normalizeAddress(value);
    if (!seenAddresses.has(normalizedValue)) {
      seenAddresses.add(normalizedValue);
      temp.push({ text: `${nodes[i]}`, value });
    }
  }
  
  const currentPanelValue = `${ip}:${port}`;
  const normalizedCurrentPanel = normalizeAddress(currentPanelValue);
  if (!seenAddresses.has(normalizedCurrentPanel)) {
    temp.unshift({ text: header.PanelName, value: currentPanelValue });
  }
  
  navigationStore.update((current) => {
    current.nodes = temp;
    return current;
  });
  return navigation.nodes;
}

export async function getIP(): Promise<string> {
  try {
    const response = await fetch(getHttpUrl('/iot/network'));
    const parsed = await safeJsonParse(response);
    const display = parsed?.LocalIpAdd?.[0];
    return display ?? "";
  } catch (e) {
    console.error(e);
    return "";
  }
}

export async function getPort(): Promise<string> {
  try {
    const response = await fetch(getHttpUrl('/iot/port'));
    const parsed = await safeJsonParse(response);
    return parsed?.data ?? "80";
  } catch (e) {
    console.error(e);
    return "80";
  }
}
