<script lang="ts">
	import { onMount, onDestroy } from 'svelte';
	import { DEBUG_EVENT, clearDebugEntries } from '$lib/business/debugFeed';

	import { getCurrentHost } from '$lib/business/util';
	import { navigationStore, debugEntriesStore, type DebugEntry } from '$lib/store';

	let entries: DebugEntry[] = [];
	let isOpen = true;
	let unsubscribe: (() => void) | null = null;

	// Reactive current backend host
	$: currentBackendHost = $navigationStore ? getCurrentHost() : 'unknown';

	$: currentBackendURL = (() => {
		if (typeof window === 'undefined') return 'unknown';
		const protocol = window.location.protocol;
		const port = window.location.port ? `:${window.location.port}` : '';
		// If loopback, we might want to show the full 127.0.0.1:port
		return `${protocol}//${currentBackendHost}`;
	})();

	$: isUsingLoopback =
		currentBackendHost.includes('127.0.0.1') || currentBackendHost.includes('localhost');

	onMount(() => {
		if (typeof window === 'undefined') {
			return;
		}
		// Subscribe to the persisted store - this will load from localStorage on init
		// and update in real-time as new debug messages are emitted
		unsubscribe = debugEntriesStore.subscribe((storedEntries: DebugEntry[]) => {
			const wasEmpty = entries.length === 0;
			entries = storedEntries || [];
			// Auto-open if we just got our first entry
			if (wasEmpty && entries.length > 0) {
				isOpen = true;
			}
		});
	});

	onDestroy(() => {
		if (unsubscribe) {
			unsubscribe();
		}
	});

	function clearEntries() {
		clearDebugEntries();
	}

	function formatContext(context?: Record<string, unknown>): string {
		if (!context || Object.keys(context).length === 0) {
			return '';
		}
		try {
			return JSON.stringify(context, null, 2);
		} catch (error) {
			return 'Unable to format context';
		}
	}

	function getContextColor(context?: Record<string, unknown>): string {
		if (!context) return 'text-gray-200';

		// Check if backendHost is loopback (green) or network IP (yellow/amber)
		const backendHost = context.backendHost as string;
		if (backendHost) {
			if (backendHost === '127.0.0.1' || backendHost.startsWith('127.0.0.1:')) {
				return 'text-green-300'; // Loopback - green
			} else if (backendHost !== 'unknown') {
				return 'text-amber-300'; // Network IP - amber
			}
		}

		return 'text-gray-200'; // Default
	}
</script>

<div class="mt-2 w-full">
	<!-- Summary Header -->
	<div
		class="mb-2 rounded bg-gray-800 px-3 py-2 font-mono text-xs {isUsingLoopback
			? 'text-green-300'
			: 'text-amber-300'}"
	>
		<strong>IoT Backend:</strong> <span class="font-bold">{isUsingLoopback ? '127.0.0.1' : currentBackendURL}</span>
		{#if isUsingLoopback}
			<span class="ml-2 text-green-400">✓ Loopback</span>
		{:else}
			<span class="ml-2 text-amber-400">⚠ Network IP</span>
		{/if}
	</div>

	<div class="flex items-center gap-2">
		<button
			type="button"
			class="rounded bg-gray-200 px-2 py-1 text-xs font-semibold uppercase tracking-wide text-gray-800 hover:bg-gray-300 focus:outline-none focus:ring-2 focus:ring-primary-400"
			on:click={() => (isOpen = !isOpen)}
		>
			Debug Feed ({entries.length}) {isOpen ? '▲' : '▼'}
		</button>
		<button
			type="button"
			class="rounded bg-gray-200 px-2 py-1 text-xs font-semibold uppercase tracking-wide text-gray-800 hover:bg-gray-300 focus:outline-none focus:ring-2 focus:ring-primary-400 disabled:cursor-not-allowed disabled:opacity-50"
			on:click={clearEntries}
			disabled={entries.length === 0}
		>
			Clear
		</button>
	</div>
	{#if isOpen}
		<div
			class="mt-2 max-h-36 w-full overflow-y-auto rounded border border-gray-400 bg-gray-900 p-2 font-mono text-[11px] text-green-200"
		>
			{#if entries.length === 0}
				<div>No debug messages yet.</div>
			{:else}
				{#each entries as entry (entry.id)}
					<div class="mb-2">
						<div>[{entry.timestamp}] {entry.message}</div>
						{#if entry.context}
							{#if formatContext(entry.context)}
								<pre
									class="mt-1 whitespace-pre-wrap break-words rounded bg-gray-800 p-2 text-[10px] {getContextColor(
										entry.context
									)}">{formatContext(entry.context)}</pre>
							{/if}
						{/if}
					</div>
				{/each}
			{/if}
		</div>
	{/if}
</div>
