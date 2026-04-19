import { debugEntriesStore, type DebugEntry } from '$lib/store';
import { get } from 'svelte/store';

const DEBUG_EVENT = 'gellert-debug-feed';
const MAX_DEBUG_ENTRIES = 100;

// Track the next ID across the module
let nextId = 0;

// Initialize nextId from persisted store on module load
if (typeof window !== 'undefined') {
	try {
		const stored = get(debugEntriesStore) as DebugEntry[];
		if (stored && stored.length > 0) {
			nextId = Math.max(...stored.map(e => e.id)) + 1;
		}
	} catch {
		// Ignore errors during initialization
	}
}

export type DebugEventDetail = {
	message: string;
	context?: Record<string, unknown>;
};

export function emitDebug(message: string, context?: Record<string, unknown>): void {
	if (typeof window === 'undefined') {
		return;
	}
	
	const timestamp = new Date().toLocaleTimeString([], { hour12: false });
	const entry: DebugEntry = {
		id: nextId++,
		timestamp,
		message,
		context
	};
	
	// Persist to store
	debugEntriesStore.update((entries: DebugEntry[]) => {
		return [entry, ...entries].slice(0, MAX_DEBUG_ENTRIES);
	});
	
	// Also dispatch event for real-time UI updates
	const detail: DebugEventDetail = { message, context };
	window.dispatchEvent(new CustomEvent<DebugEventDetail>(DEBUG_EVENT, { detail }));
}

export function clearDebugEntries(): void {
	debugEntriesStore.set([]);
}

export { DEBUG_EVENT };
