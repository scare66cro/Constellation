import { get } from 'svelte/store';
import {
  keysStore,
  pidStore,
  navigationStore,
  rebootStore,
  frontMatterStore,
  alarmsStore,
  statusStore,
  cachedDataStore,
  datesStore,
  plotDataStore,
  createDefaultAlarmsState,
  createDefaultNavigation,
  createDefaultRebootState,
  createDefaultCachedData,
} from '../store';
import WsClient from './wsClient';
// Note: Avoid static importing 'elliptic' because it's CommonJS and breaks SSR bundling.
// We'll dynamically import it only when needed in the browser.
import CryptoJS from 'crypto-js';
import { SensorTypes } from './analog';
import { format, parseISO } from 'date-fns';
export { parseSensorFeeds, sensorDisplayValue, isTempSensorType, isHumidSensorType, type SensorInfo } from './sensorFeeds';
export type ArrayResponse = { array: string[] };

async function navigate(url: string): Promise<void> {
  const { goto } = await import('$app/navigation');
  await goto(url);
}
/**
 * Checks if the current page is being served from a loopback address.
 * This indicates the page is running in kiosk mode on the local device.
 * 
 * Used for behavioral decisions like:
 * - Whether to suppress IP change redirects (kiosk should stay on loopback)
 * - Whether to consider device "always reachable" (loopback never fails)
 * 
 * @returns true if running on 127.0.0.1 or localhost
 */
export function isLoopbackAccess(): boolean {
  if (typeof window === 'undefined') {
    return false;
  }
  return location.hostname === '127.0.0.1' || location.hostname === 'localhost';
}

/**
 * @deprecated Use isLoopbackAccess() instead. This function is kept for backward compatibility.
 * Now that the kiosk startup script uses loopback URL, this just checks if we're on loopback.
 */
export function shouldUseLoopback(): boolean {
  return isLoopbackAccess();
}

/**
 * @deprecated Use isLoopbackAccess() instead. This function is kept for backward compatibility.
 */
export function isKioskMode(): boolean {
  return isLoopbackAccess();
}

/**
 * Gets the current host from the page's location.
 * Since the kiosk now loads from loopback, this naturally returns the correct host.
 */
export function getCurrentHost(): string {
  if (typeof window === 'undefined') {
    return 'localhost';
  }
  return location.host;
}

/**
 * Fetch with timeout to prevent hanging on network issues
 * @param url - URL to fetch
 * @param options - Fetch options
 * @param timeoutMs - Timeout in milliseconds (default: 10000)
 */
export async function fetchWithTimeout(
  url: string,
  options: RequestInit = {},
  timeoutMs: number = 10000,
  fetchFn: typeof globalThis.fetch = fetch
): Promise<Response> {
  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), timeoutMs);

  try {
    const response = await fetchFn(url, {
      ...options,
      credentials: options.credentials ?? 'include',
      signal: controller.signal
    });
    clearTimeout(timeoutId);
    return response;
  } catch (error) {
    clearTimeout(timeoutId);
    // Check if it was a timeout/abort
    if (error instanceof Error && error.name === 'AbortError') {
      console.warn(`Fetch timeout after ${timeoutMs}ms: ${url}`);
    }
    throw error;
  }
}

/**
 * Gets the Polling URL with the correct protocol and port.
 * Simply uses the current page's host since kiosk now loads from loopback.
 */
export function getPollingUrl(path: string = '/iot/ws'): string {
  if (typeof window === 'undefined') {
    return `http://localhost${path}`;
  }
  
  const protocol = location.protocol === 'https:' ? 'https:' : 'http:';
  return `${protocol}//${location.host}${path}`;
}

/**
 * Gets the HTTP URL for API calls.
 * Simply uses the current page's origin since kiosk now loads from loopback.
 */
export function getHttpUrl(path: string): string {
  if (typeof window === 'undefined' || typeof location === 'undefined') {
    return `http://localhost${path}`;
  }
  return `${location.origin}${path}`;
}

/**
 * Initializes the navigation store with current browser connection details.
 * Call this on app startup to sync store with actual connection.
 * 
 * Note: The actual URL resolution (loopback vs network IP) is now handled
 * dynamically by shouldUseLoopback() and getHttpUrl() based on kiosk mode.
 * This function stores the original connection details for reference.
 */
export function initializeIotConnection(): void {
  if (typeof window === 'undefined') {
    return;
  }

  const defaultPort = location.protocol === 'https:' ? '443' : '80';
  const normalizedPort = location.port || defaultPort;

  navigationStore.update(nav => ({
    ...nav,
    iotHost: location.hostname,
    iotPort: normalizedPort,
    iotProtocol: location.protocol,
  }));
}



/**
 * Updates the IoT connection details in the navigation store
 * Call this when the user changes network settings
 */
export function updateIotConnection(host: string, port: string, protocol: string = 'http:'): void {
  navigationStore.update(nav => ({
    ...nav,
    iotHost: host,
    iotPort: port,
    iotProtocol: protocol,
  }));
}

/**
 * Initiates a  60 second reboot pause where backend fetch requests should be suppressed.
 * Optionally provide a custom duration (ms) for testing.
 */
export function startRebootPause(durationMs = 60000) {
  const endsAt = Date.now() + durationMs;
  rebootStore.update(r => ({ ...r, isRebooting: true, endsAt, remaining: Math.ceil(durationMs / 1000) }));
  // Proactively close all Polling connections so no traffic is attempted during reboot window
  try {
    WsClient.closeAll('Reboot pause');
  } catch (e) {
    console.warn('Failed to close polling connections on reboot start', e);
  }
  // Tick every second to update remaining time
  const interval = setInterval(() => {
    rebootStore.update(r => {
      if (!r.isRebooting) return r;
      const remaining = Math.max(0, Math.ceil((r.endsAt - Date.now()) / 1000));
      if (remaining === 0) {
        clearInterval(interval);
        return { ...r, isRebooting: false, remaining: 0 };
      }
      return { ...r, remaining };
    });
  }, 1000);
}

/** Returns true if currently within reboot pause window */
export function isRebooting(): boolean {
  let rebooting = false;
  rebootStore.update(r => {
    rebooting = r.isRebooting && Date.now() < r.endsAt;
    if (r.isRebooting && !rebooting) {
      return { ...r, isRebooting: false, remaining: 0 };
    }
    return r;
  });
  return rebooting;
}

async function generateDHKey() {
  try {
    // Dynamically import elliptic at runtime to avoid SSR named export issues
    const ellipticMod: any = await import('elliptic');
    const EC = ellipticMod.ec;
    if (!get(keysStore).ec) {
      get(keysStore).ec = new EC('p521');
    }
    get(keysStore).keyPair = get(keysStore).ec?.genKeyPair();
    // Send the publicKey to the server with timeout
    const response = await fetchWithTimeout(getHttpUrl(`/iot/dhkey`), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({ publicKey: get(keysStore).keyPair?.getPublic('hex') }),
    }, 5000); // 5 second timeout for key exchange
    const serverPublicKey = await safeJsonParse(response);
    if ('key' in serverPublicKey) {
      const ellipticPublicKey = get(keysStore).ec?.keyFromPublic(serverPublicKey.key, 'hex');

      if (ellipticPublicKey) {
        get(keysStore).secret = get(keysStore).keyPair?.derive(ellipticPublicKey.getPublic()).toString('hex', 132);
      }
    }
  } catch (error) {
    console.log((error as Error).message);
  }
}

export async function checkKeys(): Promise<boolean> {
  try {
    let secret = get(keysStore).secret;
    const response = await fetchWithTimeout(getHttpUrl(`/iot/checkkey`), {}, 5000);
    const checkKey = (await safeJsonParse(response)).data === 'true';
    if (!checkKey || !secret) {
      await generateDHKey();
    }
  } catch (err) {
    return false;
  }
  return true;
}

export function isJsonObject(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}

export async function checkPassword(type: string, data: string, wait: (value: boolean) => void, error: (value: boolean) => void, navigate: (level: number) => Promise<void>) {
  wait(true);
  error(false);

  // Mimic the manual refresh recovery: reset session + DH keys before a retry
  const resetAuthState = async () => {
    try {
      await fetchWithTimeout(getHttpUrl('/iot/logout'), {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
      }, 3000);
    } catch (logoutErr) {
      console.warn('Auth reset during retry failed', logoutErr);
    }

    const ks = get(keysStore);
    ks.secret = undefined;
    ks.keyPair = undefined;
    ks.ec = undefined;
  };

  const maxAttempts = isLoopbackAccess() ? 2 : 1; // On loopback, auto-retry once to avoid manual refresh after network hiccups
  let attempt = 0;
  let done = false;

  while (attempt < maxAttempts && !done) {
    try {
      let secret = get(keysStore).secret;
      const response = await fetchWithTimeout(getHttpUrl(`/iot/checkkey`), {}, 5000);
      const checkKey = (await safeJsonParse(response)).data === 'true';
      if (!checkKey || !secret) {
        await generateDHKey();
        secret = get(keysStore).secret;
      }

      if (secret) {
        const encryptedData = CryptoJS.AES.encrypt(
          data, secret,
        );
        const data1 = CryptoJS.AES.encrypt(type, secret);
        const data2 = CryptoJS.AES.encrypt(CryptoJS.lib.WordArray.random(8).toString(CryptoJS.enc.Base64), secret);
        const data3 = CryptoJS.AES.encrypt(CryptoJS.lib.WordArray.random(9).toString(CryptoJS.enc.Base64), secret);
        const response = await fetchWithTimeout(getHttpUrl(`/iot/dlr`), {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json'
          },
          body: JSON.stringify({ dlr: data1.toString(), dlr1: encryptedData.toString(), dlr2: data2.toString(), dlr3: data3.toString() }),
        }, 30000); // 30 second timeout for password check
        const result = (await safeJsonParse(response)).data;
        const navigation = get(navigationStore);

        if (navigation?.name === 'version'
          && navigation.level === 0
          && ((isJsonObject(result) && ('detail' in result || result.data === 'busy'))
            || result === 'busy')) {
          // assume controller not responding so allow access to version page
          get(keysStore).accessLevel = 1;
          await navigate(1);
          done = true;
        } else if (result === '-2' || result === 'busy' || result === 'Unauthorized' || (isJsonObject(result) && 'detail' in result) || response.status !== 200) {
          // Retry once on loopback after resetting auth/session instead of requiring a manual refresh
          if (attempt + 1 < maxAttempts) {
            await resetAuthState();
            attempt += 1;
            error(false);
            continue;
          }
          error(true);
          done = true;
        } else {
          const accessLevel = parseInt(result, 10);
          switch (type) {
            case 'login':
              if (accessLevel >= 0) {
                get(keysStore).localAllowed = true;
                get(keysStore).accessLevel = accessLevel;
              }
              break;
            case 'DEFAULT':
            default:
              get(keysStore).accessLevel = accessLevel;
              break;
          }
          await navigate(accessLevel);
          done = true;
        }
      } else {
        // No secret generated; bail out
        if (attempt + 1 < maxAttempts) {
          await resetAuthState();
          attempt += 1;
          error(false);
          continue;
        }
        error(true);
        done = true;
      }
    } catch (err) {
      console.log((err as Error).message);
      if (attempt + 1 < maxAttempts) {
        await resetAuthState();
        attempt += 1;
        error(false);
        continue;
      }
      error(true);
      done = true;
    }
  }

  wait(false);
}

export function getAdornment(sensorType: string, sensorValue?: string, allowHtml = true): string {
  // Don't show adornments for "Off" values
  if (sensorValue === 'Off') {
    return '';
  }

  switch (sensorType) {
    case SensorTypes.SENSOR_TEMP_IR:
    case SensorTypes.SENSOR_TEMP:
    case SensorTypes.SENSOR_RETURN_TEMP_1:
    case SensorTypes.SENSOR_RETURN_TEMP_2:
    case SensorTypes.SENSOR_PILE_TEMP:
      return '°';
    case SensorTypes.SENSOR_CO2_1:
    case SensorTypes.SENSOR_CO2_2:
      return allowHtml ? '<span class="text-base md:text-xl">ppm</span>' : 'ppm';
    case SensorTypes.SENSOR_STATIC_PRESS:
      return allowHtml ? '<span class="text-base md:text-xl">"wc</span>' : '"wc';
    case SensorTypes.SENSOR_HUMID:
    case SensorTypes.SENSOR_PILE_HUMID:
    case SensorTypes.SENSOR_RETURN_HUMID_1:
    case SensorTypes.SENSOR_RETURN_HUMID_2:
      return ' %';
    default: return '';
  }
}

export function getDate(date: string) {
  try {
    return format(parseISO(date as string), 'MM/dd/yyyy HH:mm:ss');
  } catch (error) {
    return date;
  }
}

export async function loadData() {
  const pid = get(pidStore);
  const url = new URL(getHttpUrl(`/iot/pids`));
  if (pid.type === '') {
    return {};
  }
  const params: Record<string, string> = {
    type: pid.type,
    start: pid.startLog,
    end: pid.endLog
  };
  Object.keys(params).forEach((key) => url.searchParams.append(key, params[key]))
  const response = await fetchWithTimeout(url.toString(), {}, 15000); // 15 second timeout for data loading
  return await safeJsonParse(response);
}

/**
 * Server-side safe function to load IoT data with absolute URLs
 * Handles both browser and SSR contexts properly
 */
export async function loadIotData(endpoint: string, fetch: typeof globalThis.fetch, setWaitState?: (isWaiting: boolean) => void): Promise<any> {
  try {
    // Skip IoT data loading during SSR
    if (typeof window === 'undefined') {
      return {};
    }
    // Suppress requests during reboot pause
    if (isRebooting()) {
      return { status: 'rebooting' };
    }

    const url = getHttpUrl(endpoint);
    const response = await fetchWithTimeout(url, {}, 10000, fetch); // 10 second timeout
    return await safeJsonParse(response, setWaitState);
  } catch (error) {
    console.error(`Error loading IoT data from ${endpoint}:`, error);
    // Return empty object instead of throwing to prevent SSR crashes
    return {};
  }
}

export async function safeJsonParse(response: Response, setWaitState?: (isWaiting: boolean) => void): Promise<any> {
  try {
    // If we have a wait state setter, set it to true while processing
    if (setWaitState) setWaitState(true);

    // If rebooting, short-circuit (caller likely will ignore data)
    if (isRebooting()) {
      if (setWaitState) setWaitState(false);
      return { status: 'rebooting' };
    }

    if (response.status === 401) {
      return { status: 401, data: 'Unauthorized' };
    }

    // Check if fetch was successful
    if (!response.ok) {
      console.warn(`HTTP error: ${response.status} ${response.statusText}`);
      if (setWaitState) setWaitState(false);
      return {};
    }

    // Check content type
    const contentType = response.headers.get('content-type');
    if (!contentType || !contentType.includes('application/json')) {
      console.warn('Unexpected content type:', contentType);
      const textContent = await response.text();
      console.warn(textContent);
      // Return empty object instead of throwing to prevent UI breakage
      if (setWaitState) setWaitState(false);
      return {};
    }

    // Try to parse as JSON
    try {
      // If successful, clear wait state and return the parsed JSON
      const parsedData = await response.json();
      if (setWaitState) setWaitState(false);
      return parsedData;
    } catch (e) {
      console.error('Failed to parse JSON response:', e);
      // Return empty object instead of throwing to prevent UI breakage
      if (setWaitState) setWaitState(false);
      return {};
    }
  } catch (error) {
    console.error('Error processing response:', error);
    // Clear the wait state and return empty object to prevent UI hanging
    if (setWaitState) setWaitState(false);
    return {};
  }
}

/**
 * Checks if a host is available with a timeout
 */
export async function checkHostAvailable(url: string): Promise<boolean> {
  try {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 2000); // 2 second timeout

    const response = await fetch(`http://${url}`, {
      mode: 'no-cors',
      method: 'HEAD',
      signal: controller.signal
    });

    clearTimeout(timeoutId);
    return true;
  } catch {
    return false;
  }
}

/**
 * Navigates to a storage panel by IP address or hostname
 * Handles same-panel navigation and remote panel navigation with availability checks
 * 
 * @param targetIp - The IP address or hostname:port of the target panel
 * @param fallbackUrl - Optional fallback URL to redirect to if target is unavailable
 * @param addRedirectParam - Whether to add ?-Redirect parameter (for GellertHeader style navigation)
 * @param onSamePanel - Optional callback for when navigating to the same panel
 */
export async function navigateToStoragePanel(
  targetIp: string,
  fallbackUrl: string = '/',
  addRedirectParam: boolean = false,
  onSamePanel?: () => Promise<void>
): Promise<void> {
  if (!targetIp) return;

  // Check if it's the current panel by comparing IPs
  const currentLocation = getCurrentHost();
  const isCurrentPanel = targetIp === currentLocation ||
    targetIp === location.hostname ||
    targetIp === `${location.hostname}:${location.port}`;

  if (isCurrentPanel) {
    // Same panel - use callback or default to home navigation
    if (onSamePanel) {
      await onSamePanel();
    } else {
      // Default behavior: reset navigation and go to home
      const navStore = get(navigationStore);
      navigationStore.set({
        ...navStore,
        level: 0,
        name: '',
        dropDownPage: ''
      });
      // Use goto for same-panel navigation to maintain SPA behavior while preserving redirect
      let targetUrl = '/';
      if (navStore.redirect) {
        targetUrl = '/?-Redirect';
      }
      await navigate(targetUrl);
    }
    return;
  }

  // Different panel - check availability and navigate
  try {
    const isAvailable = await checkHostAvailable(targetIp);

    if (!isAvailable) {
      // Target is not available, redirect to fallback
      window.location.href = getHttpUrl(fallbackUrl);
      return;
    }

    // Build target URL
    const targetUrl = addRedirectParam
      ? `http://${targetIp}?-Redirect`
      : `http://${targetIp}`;

    // For GellertHeader style navigation with redirect handling
    if (addRedirectParam) {
      const redirectTimeout = setTimeout(() => {
        window.location.href = get(navigationStore).homeUrl || getHttpUrl('/');
      }, 3000);

      window.onbeforeunload = () => {
        clearTimeout(redirectTimeout);
      };
    }

    // Navigate to the target panel
    window.location.href = targetUrl;

  } catch (error) {
    console.error('Failed to reach target panel:', targetIp, error);
    // Could show a toast notification here that the panel is unreachable
  }
}

/**
 * Extracts storage name and IP from network monitor format
 * Example: "Storage Name (192.168.1.100)" -> { name: "Storage Name", ip: "192.168.1.100" }
 */
export function extractStorageInfo(storageString: string): { name: string; ip: string } {
  const ipMatch = storageString.match(/\(([^)]+)\)$/);
  const ip = ipMatch ? ipMatch[1] : '';
  const name = storageString.replace(/\s*\([^)]+\)$/, '').trim();
  return { name, ip };
}

export function getSizeClass(size: string): string {
  switch (size) {
    case 'lg':
      return 'text-size-large';
    case 'xl':
      return 'text-size-xl';
    case '':
    default:
      return 'text-size';
  }
}

export function getOffset(availableHeight: number): number {
  let offset = 5;
  if (availableHeight >= 1080) {
    // For tall screens (1080px and above), scale the offset based on height
    // Base offset of 75px for 1080px, scaling up to 120px for very tall screens
    const baseHeight = 1080;
    const baseOffset = 75;
    const maxOffset = 120;
    const heightRatio = Math.min((availableHeight - baseHeight) / (1440 - baseHeight), 1);
    offset = baseOffset + (maxOffset - baseOffset) * heightRatio;
  } else if (availableHeight >= 768) {
    // For medium height screens (768px-1079px)
    const baseHeight = 768;
    const baseOffset = 35;
    const maxOffset = 75;
    const heightRatio = (availableHeight - baseHeight) / (1080 - baseHeight);
    offset = baseOffset + (maxOffset - baseOffset) * heightRatio;
  } else if (availableHeight >= 600) {
    // For smaller screens (600px-767px)
    offset = 25;
  }

  return offset;
}
