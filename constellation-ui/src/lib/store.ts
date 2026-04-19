import { writable } from 'svelte/store';
import { KeyboardTypes } from './ui/Keyboard.svelte';
import type { ec as EC } from 'elliptic';
import { cloneDeep } from 'lodash-es';
import type { PageList } from './business/PageType';
import { getHttpUrl } from './business/util';

function createPersistedStore(key: string, startValue: any) {
	const { subscribe, set, update } = writable(startValue);

	// Helper to wire up localStorage on the client
	const useLocalStorage = () => {
		try {
			const json = localStorage.getItem(key);
			if (json) {
				set(JSON.parse(json));
			}
			// Persist on every change
			subscribe(current => {
				localStorage.setItem(key, JSON.stringify(current));
			});
		} catch (e) {
			// no-op if localStorage is unavailable (SSR or blocked)
		}
	};

	// Auto-enable persistence in the browser (SSR-safe)
	if (typeof window !== 'undefined' && typeof window.localStorage !== 'undefined') {
		useLocalStorage();
	}

	return { subscribe, set, update, useLocalStorage };
	}

	export interface Headers {
		DateTime: string;
		CurrentMode: number;
		PanelName: string;
	}

	const headers: Headers = { DateTime: '', CurrentMode: 0, PanelName: 'Agristar Panel' };

	export interface Navigation {
		level: number;
		name: string;
		data: string;
		invalidate: boolean;
		redirect: boolean;
		homeUrl: string;
		isDirty: () => boolean;
		nodes: { text: string, value: string }[];
		localIP: string; // Machine's actual IP for deduplication when on loopback
		dropDownPage: string;
		inLoad: boolean;
		setOptions: boolean;
		showNext: boolean;
		showPrev: boolean;
		lastPressedButton: string | null;
		runtimeSelection: string;
		showFullscreen: boolean;
		hasUserPressedFullScreen: boolean;
		iotHost: string;
		iotPort: string;
		iotProtocol: string;
	}

	export function createDefaultNavigation(): Navigation {
		return {
			level: 0,
			name: '',
			data: '',
			invalidate: false,
			redirect: false,
			homeUrl: '',
			isDirty: () => false,
			nodes: [],
			localIP: '', // Machine's actual IP - set from WebSocket tcpip-data
			dropDownPage: '',
			inLoad: false,
			setOptions: true,
			showNext: true,
			showPrev: false,
			lastPressedButton: null,
			runtimeSelection: '0',
			showFullscreen: false,
			hasUserPressedFullScreen: false,
			iotHost: 'localhost',
			iotPort: '80',
			iotProtocol: 'http:',
		};
	}

	const navigation: Navigation = createDefaultNavigation();

	export interface PIDNavigation {
		returnPage: string;
		endpoint: string;
		startLog: string;
		endLog: string;
		type: string;
	}

	const pidNavigation: PIDNavigation = { type: '', returnPage: '', endpoint: '', startLog: '0', endLog: '2000' };

	export interface DataSelection {
		selected: boolean[];
		selections: Array<{ text: string, value: string, label: string, units: string }>;
	}

	const dataSelection: DataSelection = { selected: [], selections: [] };

	const frontMatter: Record<string, string[] | string | number> = {};

interface Background {
    backgroundImage: string;
    images: { text: string, value: string }[],
    customImages: { text: string, value: string, id: string }[],
}

export const defaultImages = [
	{ text: 'Potatoes', value: '/background/Potatoes.jpg' },
	{ text: 'Potato Field 1', value: '/background/Potato Field 1.jpg' },
	{ text: 'Potato Field 2', value: '/background/Potato Field 2.jpg' },
	{ text: 'Potato in Hands', value: '/background/Potato in Hands.jpg' },
	{ text: 'Potato Loading', value: '/background/Potato Loading.jpg' },
	{ text: 'Potato Plant', value: '/background/Potato Plant.jpg' },
	{ text: 'Onion Half', value: '/background/Onion Half.jpg' },
	{ text: 'Onion Field 1', value: '/background/Onion Field 1.jpg' },
	{ text: 'Onion Field 2', value: '/background/Onion Field 2.jpg' },
	{ text: 'Onion in Hands', value: '/background/Onion in Hands.jpg' },
	{ text: 'Refrigeration', value: '/background/Refrigeration.jpg' },
	{ text: 'Storage 1', value: '/background/Storage 1.jpg' },
	{ text: 'Storage 2', value: '/background/Storage 2.jpg' },
	{ text: 'App', value: '/background/App.jpg' },
	{ text: 'Fan House', value: '/background/Fan House.jpg' },
	{ text: 'Climacell', value: '/background/ClimaCell.jpg' },
];

function createBackgroundStore() {
	const background: Background = {
		backgroundImage: '/background/Potatoes.jpg',
		images: cloneDeep(defaultImages),
		customImages: [],
	};

	const store = createPersistedStore('background', background);

	// Initialize images array when store loads
	let initialized = false;
	const storeWithMethods = {
		...store,
		subscribe: (run: any) => {
			return store.subscribe((value) => {
				// Ensure customImages is always an array
				if (!Array.isArray(value.customImages)) {
					value.customImages = [];
				}

				// Always reconstruct images array from default + custom images
				value.images = [...defaultImages, ...value.customImages];

				if (!initialized) {
					initialized = true;
				}
				run(value);
			});
		},
		// Load all background pictures from IoT API
		async loadPictures() {
			try {
				const response = await fetch(getHttpUrl('/iot/background-pictures'), { credentials: 'include' });
				if (response.ok) {
					const data = await response.json();
					if (data && Array.isArray(data)) {
						store.update(bg => {
							// Get custom images from the API using the backend API endpoint
							const customImages = data
								.map((pic: any) => ({ 
									text: pic.displayName, 
									value: getHttpUrl(`/iot/background-pictures/file/${pic.filename}`), 
									id: pic.id 
								}));
							// Update custom images and rebuild the full images array
							bg.customImages = customImages;
							bg.images = [...defaultImages, ...customImages];

							// Normalize persisted selection to one of the current options by pathname
							const getPath = (u: string) => {
								try {
									return new URL(u, 'http://localhost').pathname;
								} catch {
									return u || '';
								}
							};
							const currentPath = getPath(bg.backgroundImage);
							const match = bg.images.find((img: { value: string }) => getPath(img.value) === currentPath);
							if (match) {
								bg.backgroundImage = match.value;
							} else if (currentPath) {
								// If the current selection isn't in the options, insert a temporary option so the UI shows it
								const tempLabel = 'Current background';
								bg.images = [{ text: tempLabel, value: bg.backgroundImage }, ...bg.images];
							}

							return bg;
						});
					}
				} else {
					console.error('Failed to load background pictures from IoT API');
				}
			} catch (error) {
				console.error('Error loading background pictures:', error);
			}
		},
		addCustomImage: (name: string, file: File) => {
			store.update(bg => {
				// Ensure customImages is always an array
				if (!Array.isArray(bg.customImages)) {
					bg.customImages = [];
				}

				const id = `custom_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
				const safeFileName = `${id}_${file.name.replace(/[^a-zA-Z0-9.-]/g, '_')}`;
				// Use the backend API endpoint for custom images
				const imageUrl = getHttpUrl(`/iot/background-pictures/file/${safeFileName}`);
				const customImage = { text: name, value: imageUrl, id };

				bg.customImages = [...bg.customImages, customImage];
				bg.images = [...defaultImages, ...bg.customImages];

				return bg;
			});
			return Promise.resolve();
		},

		removeCustomImage: (id: string) => {
			store.update(bg => {
				// Ensure customImages is always an array
				if (!Array.isArray(bg.customImages)) {
					bg.customImages = [];
				}

				bg.customImages = bg.customImages.filter((img: { text: string, value: string, id: string }) => img.id !== id);
				bg.images = [...defaultImages, ...bg.customImages];
				return bg;
			});
		},

		renameCustomImage: (id: string, newName: string) => {
			store.update(bg => {
				// Ensure customImages is always an array
				if (!Array.isArray(bg.customImages)) {
					bg.customImages = [];
				}

				const imageIndex = bg.customImages.findIndex((img: { text: string, value: string, id: string }) => img.id === id);
				if (imageIndex !== -1) {
					bg.customImages[imageIndex].text = newName;
					bg.images = [...defaultImages, ...bg.customImages];
				}
				return bg;
			});
		}
	};

	return storeWithMethods;
}

class KeyboardStore {
	constructor() {
		this._hidden = true;
		this.start = '';
		this.keyboardType = KeyboardTypes.Alpha;
		this.resultReady = (val: string) => { };
		this.label = 'Keyboard';
		this.inputType = 'text';
		this.canShow = true;
	}
	public set hidden(val: boolean) {
		if (!val && this.canShow) {
			this._hidden = val;
		}
		if (val) {
			this._hidden = val;
			this.canShow = false;
			setTimeout(() => {
				this.canShow = true;
			}, 1000);
		}
	}
	public get hidden() {
		return this._hidden;
	}
	private canShow: boolean;
	private _hidden: boolean;
	start: string;
	keyboardType: KeyboardTypes;
	resultReady: (val: string) => void;
	label: string;
	inputType: string;
}
const keyboard = new KeyboardStore();

const heights = {
	header: 0,
	footer: 0,
	main: 0,
}

export interface AlarmsState {
	canShowAlarm: boolean;
	isShowingAlarms: boolean;
	handle: NodeJS.Timeout | undefined;
	slaveBroadcastWarning: boolean;
}

export function createDefaultAlarmsState(): AlarmsState {
	return {
		canShowAlarm: true,
		isShowingAlarms: false,
		handle: undefined,
		slaveBroadcastWarning: false,
	};
}

const alarms: AlarmsState = createDefaultAlarmsState();

export function createDefaultCachedData(): Record<string, Record<string, string[] | number[] | number[][]>> {
	return {
		User: {},
		Activity: {},
	};
}

const cachedData: Record<string, Record<string, string[] | number[] | number[][]>> = createDefaultCachedData();
const dates: string[] = [];
const plotData: Record<string, string[] | number[] | number[][]> = {};

export interface KeyStore {
	keyPair?: EC.KeyPair;
	secret?: string;
	ec?: EC;
	accessLevel: number;
	localRequired: boolean;
	localAllowed: boolean;
	hasLevel1Password: boolean;
}

export function createDefaultKeyStore(): KeyStore {
	return {
		keyPair: undefined,
		secret: undefined,
		ec: undefined,
		accessLevel: 0,
		localRequired: false,
		localAllowed: false,
		hasLevel1Password: true,
	};
}

const keys: KeyStore = createDefaultKeyStore();

let upgrade = false;

let locale = "en";

let pageTranslations = undefined as PageList | undefined;

let modeToColor = {} as  Record<number, { color: string, text: string, image?: string }>;

type AuxiliaryOptionsType = {
	unitOptions: Array<{value: string, text: string}>,
	onOffOptions: Array<{value: string, text: string}>,
	typeOptions: Array<{value: string, text: string}>,
	opOptions: Array<{value: string, text: string}>,
	andOrOptions: Array<{value: string, text: string}>,
	auxProgOptions: Array<{value: string, text: string}>,
	modeOptions: Array<{value: string, text: string}>,
	availSensors: (ref: boolean) => { text: string, value: string }[],
};

let auxiliaryOptions: AuxiliaryOptionsType = {
	unitOptions: [],
	onOffOptions: [],
	typeOptions: [],
	opOptions: [],
	andOrOptions: [],
	auxProgOptions: [],
	modeOptions: [],
	availSensors: (ref) => { return [] },
};

let equipmentOptions = {
	getStatus: (equipment: any, eq: string, remote: string, input: string, output: string) => ({ status: '', color: '' }),
	getRefrigStatus: (remote: string, input: string, output: string, diag: string) => ({ status: '', color: '' }),
	getAuxSwitch: (edit: boolean, status: string, switch1: string[], switch2: string[], auxiliary: number) => ({ status: '', color: '' }),
	getSwitchStatus: (value: string) => ({ status: '', color: '' }),
	getDoorDiagStatus: (input: string, output: string, diag: string) => ({ status: '', color: '' }),
};

let yesNoOptions: Array<{ text: string, value: string }> = [];

let failureOptions: {
	modeOptions: Array<{ text: string, value: string }>,
	LightsOptions: Array<{ text: string, value: string}>,
	refridgeRunOptions: (boardType: string, controllerVersion: string) => ({ text: string, value: string }[]),
	modeLightOptions: Array<{ text: string, value: string}>,
} = { modeOptions: [], LightsOptions: [], refridgeRunOptions: (boardType: string, controllerVersion: string) => [], modeLightOptions: []};

export type LabelIndex = { availLabels: string[], availEquip: number[] };

let equipList: LabelIndex = { availLabels: [], availEquip: [] };
let remoteList: LabelIndex = { availLabels: [], availEquip: [] };

export type HomePage = { page: string, initialized: boolean };
let homePage = {
	page: '/',
	initialized: false,
};

interface HistoryState {
    showLog: boolean;
    showData: boolean;
    showRange: boolean;
    showDownload: boolean;
    showMain: boolean;
		logType: string;
		startDate: Date;
		endDate: Date;
		start: string;
		end: string;
		type: string;
		inSequence: boolean; // true - regular log UI sequence, false - main change range request
		display: string;
}

const history: HistoryState = {
    showLog: true,
    showData: false,
    showRange: false,
    showDownload: false,
    showMain: false,
		logType: '',
		startDate: new Date(),
		endDate: new Date(),
		start: '1',
		end: '200',
		type: 'User',
		inSequence: true,
		display: '',
	};

export const backgroundStore = createBackgroundStore();
export const localeStore = createPersistedStore('locale', locale)
export const headersStore = writable(headers);
export const navigationStore = writable(navigation);
export const frontMatterStore = writable(frontMatter);
export const keyboardStore = writable(keyboard);
export const heightsStore = writable(heights);
export const alarmsStore = writable(alarms);
export const pidStore = writable(pidNavigation);
export const cachedDataStore = writable(cachedData);
export const datesStore = writable(dates);
export const plotDataStore = writable(plotData);
export const dataSelectionStore = writable(dataSelection);
export const upgradeStore = writable(upgrade);
export const keysStore = writable(keys);
export const pageTranslationsStore = writable(pageTranslations);
export const modeToColorStore = writable(modeToColor);
export const auxiliaryOptionsStore = writable(auxiliaryOptions);
export const equipmentOptionsStore = writable(equipmentOptions);
export const yesNoOptionsStore = writable(yesNoOptions);
export const failureOptionsStore = writable(failureOptions);
export const equipListStore = writable(equipList);
export const remoteListStore = writable(remoteList);
export const homePageStore = writable(homePage);
export const historyStore = writable(history);
// Reboot state store to coordinate temporary suppression of backend requests
export interface RebootState {
	isRebooting: boolean;      // true while backend is restarting
	endsAt: number;            // epoch ms when reboot window ends
	remaining: number;         // seconds remaining (updated each second)
}

export function createDefaultRebootState(): RebootState {
	return { isRebooting: false, endsAt: 0, remaining: 0 };
}

const reboot: RebootState = createDefaultRebootState();
export const rebootStore = writable(reboot);
// Global controller/webserver status code store (HTTP or heartbeat status)
// Previously some pages attempted to read frontMatterStore.status which does not exist.
// Use this store instead to expose numeric status codes from backend health endpoints.
export const statusStore = writable<{ status?: number }>({ status: undefined });

// Network error store for user-friendly error recovery UI
export interface NetworkErrorState {
	isVisible: boolean;          // Show the error overlay
	errorType: 'timeout' | 'connection' | 'server' | 'unknown' | null;
	message: string;             // User-friendly error message
	retryCount: number;          // Number of retry attempts
	maxRetries: number;          // Maximum retries before showing manual action
	isRecovering: boolean;       // Currently attempting auto-recovery
	lastErrorTime: number;       // Timestamp of last error
}

export function createDefaultNetworkErrorState(): NetworkErrorState {
	return {
		isVisible: false,
		errorType: null,
		message: '',
		retryCount: 0,
		maxRetries: 5,
		isRecovering: false,
		lastErrorTime: 0,
	};
}

const networkError: NetworkErrorState = createDefaultNetworkErrorState();
export const networkErrorStore = writable(networkError);

// Debug feed persisted store to preserve debug messages across page refreshes/reconnections
export interface DebugEntry {
	id: number;
	timestamp: string;
	message: string;
	context?: Record<string, unknown>;
}

const MAX_DEBUG_ENTRIES = 100;

const debugEntries: DebugEntry[] = [];
export const debugEntriesStore = createPersistedStore('debugEntries', debugEntries);

// System groups store — organizes discovered remote systems into named groups
// Persisted to localStorage so group assignments survive page refreshes
// Systems are identified by their persistent novaId (UUID) so groups survive DHCP IP changes.
// Legacy groups that stored IP:port strings are matched by fallback address comparison.
export interface SystemGroup {
	id: string;
	name: string;
	systems: string[];  // array of system identifiers (novaId preferred, IP:port fallback)
}

export interface SystemGroupsState {
	groups: SystemGroup[];
	selectedGroupId: string | null;  // null = show all systems
}

export function createDefaultSystemGroupsState(): SystemGroupsState {
	return { groups: [], selectedGroupId: null };
}

const systemGroups: SystemGroupsState = createDefaultSystemGroupsState();
export const systemGroupsStore = createPersistedStore('systemGroups', systemGroups);

/** Fetch groups from the server and update the store (server wins) */
export async function loadGroupsFromServer(): Promise<void> {
	try {
		const res = await fetch(getHttpUrl('/iot/groups'), { credentials: 'include' });
		if (res.ok) {
			const data: SystemGroupsState = await res.json();
			if (data && Array.isArray(data.groups)) {
				systemGroupsStore.set(data);
			}
		}
	} catch {
		// Server unavailable — keep localStorage data
	}
}

/** Save the current groups state to the server */
export async function saveGroupsToServer(state: SystemGroupsState): Promise<void> {
	try {
		await fetch(getHttpUrl('/iot/groups'), {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			credentials: 'include',
			body: JSON.stringify(state),
		});
	} catch {
		// Server unavailable — localStorage still has the data
	}
}

/**
 * Check if a network node is a member of a group.
 * Matches by novaId first (DHCP-resilient), falls back to address (legacy groups).
 */
export function isNodeInGroup(
	node: { value: string; id?: string },
	group: SystemGroup,
): boolean {
	if (node.id && group.systems.includes(node.id)) return true;
	return group.systems.includes(node.value);
}

/**
 * Get the best identifier for a node (novaId preferred, address fallback).
 */
export function getNodeIdentifier(node: { value: string; id?: string }): string {
	return node.id || node.value;
}
