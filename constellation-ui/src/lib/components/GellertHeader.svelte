<script lang="ts">
	import { headersStore, keysStore, navigationStore, pageTranslationsStore, modeToColorStore, keyboardStore, type Headers, frontMatterStore, systemGroupsStore, isNodeInGroup } from '$lib/store';
	import { onDestroy, onMount } from 'svelte';
	import Select from '$lib/ui/Select.svelte';
	import { goto } from '$app/navigation';
	import { toUpper } from 'lodash-es';
	import TextField from '$lib/ui/TextField.svelte';
	import Button from '$lib/ui/Button.svelte';
	import { getIP, getPort, updateNavigationNodes } from '$lib/business/network';
	import { t } from 'svelte-i18n';
	import type { Page } from "$lib/business/PageType";
	import { getFilteredPageList } from '$lib/business/paging';
	import { checkPassword, getHttpUrl, navigateToStoragePanel } from '$lib/business/util';
	import { KeyboardTypes } from '$lib/ui/Keyboard.svelte';
	import WsClient from '$lib/business/wsClient';

	export let height: number;

	let div: HTMLDivElement;

	let dateOutput: string = '';

	let client: WsClient | undefined;
	let networkClient: WsClient | undefined;


	// Long press and modal state
	let longPressTimer: number;
	let showSystemModal = false;
	let showConfirmModal = false;
	let confirmAction = '';
	let confirmMessage = '';
	let waitForAuth = false;
	let authError = false;
	let isLongPressing = false;

	$: selectedPanel = '';
	$: currentMode = $modeToColorStore[$headersStore?.CurrentMode];
	$: height = div?.clientHeight;
	$: {
		let d = $headersStore?.DateTime;
		let t = d?.[1]?.split(':');
		if (d && t && d.length === 3 && t.length === 3) {
			dateOutput = `${d[0]} ${t[0]}:${t[1]} ${d[2] === '0' ? 'AM' : 'PM'}`;
		} else {
			dateOutput = '';
		}
	}

	$: hasAux = $frontMatterStore.hasAux as string;
	$: pageList = [] as Page[];
	$: if ($pageTranslationsStore) {
		pageList = getFilteredPageList($navigationStore.level, $pageTranslationsStore, hasAux);
	}

	// Create fallback options when navigationStore.nodes is empty
	$: panelOptions = (() => {
		// Always ensure current panel is in the list
		if (typeof window !== 'undefined') {
			const currentPort = location.port || '80';
			const currentHost = location.hostname;
			const currentAddress = currentPort === '80' ? currentHost : `${currentHost}:${currentPort}`;
			const panelName = $headersStore?.PanelName || 'Gellert Agri-Star';
			
			// Check if we're on loopback (local panel display)
			const isOnLoopback = currentHost === 'localhost' || 
			                     currentHost === '127.0.0.1' || 
			                     currentHost.startsWith('127.');
			
			// Get machine's actual IP from navigationStore (sent by IoTClient via Polling)
			const machineLocalIP = $navigationStore.localIP || '';
			
			// Helper function to normalize addresses for comparison
			const normalizeAddress = (address: string) => {
				if (!address) return '';
				if (address.includes(':')) {
					const [host, port] = address.split(':');
					return port === '80' ? host : address;
				}
				return address;
			};
			
			// Helper to extract just the host/IP from an address (strip port)
			const extractHost = (address: string) => {
				if (!address) return '';
				if (address.includes(':')) {
					return address.split(':')[0];
				}
				return address;
			};
			
			const normalizedCurrentAddress = normalizeAddress(currentAddress);
			const normalizedCurrentHost = normalizeAddress(currentHost);
			
			// Helper to check if an address matches the current panel
			const isCurrentPanel = (address: string) => {
				const normalized = normalizeAddress(address);
				return normalized === normalizedCurrentAddress || normalized === normalizedCurrentHost;
			};
			
			// Helper to check if an address matches the machine's actual IP (for loopback deduplication)
			const matchesMachineIP = (address: string) => {
				if (!machineLocalIP) return false;
				const host = extractHost(address);
				return host === machineLocalIP;
			};
			
			// If we have network nodes, check if current panel is already in them
			if ($navigationStore.nodes && $navigationStore.nodes.length > 0) {
				const currentPanelInNodes = $navigationStore.nodes.find(node => isCurrentPanel(node.value));
				
				// Filter out current panel from nodes and deduplicate
				const seenAddresses = new Set<string>();
				const otherNodes: { text: string; value: string; id?: string }[] = [];
				
				for (const node of $navigationStore.nodes) {
					if (isCurrentPanel(node.value)) continue; // Skip current panel, we'll add it first
					
					// When on loopback, also filter out entries matching machine's actual IP
					// This prevents duplicate entries for the same panel (loopback vs actual IP)
					if (isOnLoopback && matchesMachineIP(node.value)) continue;
					
					const normalized = normalizeAddress(node.value);
					if (!seenAddresses.has(normalized)) {
						seenAddresses.add(normalized);
						otherNodes.push(node);
					}
				}
				
				// Update the current panel option with the network node's text if available
				const currentPanelOption = {
					text: currentPanelInNodes?.text || panelName,
					value: currentAddress
				};
				
				// Filter by selected group (if any)
				const groupId = $systemGroupsStore.selectedGroupId;
				const activeGroup = groupId
					? ($systemGroupsStore.groups ?? []).find((g: { id: string; systems: string[] }) => g.id === groupId)
					: null;
				const filteredNodes = activeGroup
					? otherNodes.filter(n => isNodeInGroup(n, activeGroup))
					: otherNodes;
				
				return [currentPanelOption, ...filteredNodes];
			}
			
			// If no network nodes, just return current panel
			const currentPanelOption = {
				text: panelName,
				value: currentAddress
			};
			return [currentPanelOption];
		}
		
		// SSR fallback
		return [{
			text: 'Loading...',
			value: 'loading'
		}];
	})();

	// Always keep current panel selected unless user actively changes it
	$: if (panelOptions.length > 0 && typeof window !== 'undefined') {
		const currentPort = location.port || '80';
		const currentHost = location.hostname;
		const currentAddress = currentPort === '80' ? currentHost : `${currentHost}:${currentPort}`;
		
		// Only set selectedPanel if it's not already set to current panel
		// This ensures we don't override user selections until they navigate away
		if (!selectedPanel || selectedPanel === 'loading' || 
			(!panelOptions.some(option => option.value === selectedPanel))) {
			selectedPanel = currentAddress;
		}
	}

	// Disable UI controls when local password is required but local access isn't allowed
	$: uiDisabled = $keysStore.localRequired && !$keysStore.localAllowed;

	// Multiview functionality removed

	async function gotoSelectedPage() {
		if (uiDisabled) return; // Prevent navigation when UI is disabled
		
		if ($navigationStore.dropDownPage === '' && $navigationStore.level <= 1) {
			$navigationStore.name = $navigationStore.dropDownPage;
			// Navigate to root while preserving redirect parameter
			let targetUrl = '/';
			if ($navigationStore.redirect) {
				targetUrl = '/?-Redirect';
			}
			await goto(targetUrl);
		} else if ($navigationStore.dropDownPage === 'refresh') {
			// stay on same page but refresh data unless rebooting
			$navigationStore.dropDownPage = $navigationStore.name;
			const { isRebooting } = await import('$lib/business/util');
			if (!isRebooting()) {
				$navigationStore.invalidate = true;
			}
		} else {
			$navigationStore.name = $navigationStore.dropDownPage;
			await goto(`/level${$navigationStore.level > 1 ? 2 : 1}/${$navigationStore.name}`);
		}
	}

	function backToHomePanel() {
		window.location.href = $navigationStore.homeUrl;
	}

	async function gotoSelectedPanel() {
		if (uiDisabled) return; // Prevent navigation when UI is disabled
		
		const target = panelOptions.find((i) => i.value === selectedPanel);
		if (target && target.text !== target.value && target.value !== location.hostname) {
			await navigateToStoragePanel(target.value, '/', true);
		}
	}
	// Long press functionality
	function handleLogoMouseDown(e: MouseEvent) {
		if (uiDisabled) return; // Prevent logo interactions when UI is disabled
		
		e.preventDefault(); // Prevent text selection and other default behaviors
		isLongPressing = true;
		longPressTimer = window.setTimeout(() => {
			if ($navigationStore.level >= 2) {
				showSystemModal = true;
			} else {
				requestPassword();
			}
		}, 1000); // 1 second long press
	}

	function handleLogoMouseUp(e: MouseEvent) {
		e.preventDefault();
		if (longPressTimer) {
			clearTimeout(longPressTimer);
		}
		// Reset the long press flag after a short delay to allow context menu prevention
		setTimeout(() => {
			isLongPressing = false;
		}, 100);
	}

	function handleLogoTouchStart(e: TouchEvent) {
		if (uiDisabled) return; // Prevent logo interactions when UI is disabled
		
		e.preventDefault(); // Prevent default touch behaviors
		handleLogoMouseDown(e as any);
	}
	
	function handleLogoTouchEnd(e: TouchEvent) {
		e.preventDefault();
		handleLogoMouseUp(e as any);
	}

	function handleLogoContextMenu(e: MouseEvent) {
		// Always prevent context menu on the logo, especially during/after long press
		e.preventDefault();
		e.stopPropagation();
		return false;
	}
	// Authentication for level 2 access
	function requestPassword() {
		waitForAuth = true;
		authError = false;
		$keyboardStore.keyboardType = KeyboardTypes.Alpha;
		$keyboardStore.label = 'Level 2 Password Required';
		$keyboardStore.start = '';
		$keyboardStore.resultReady = async (data: string) => {
			const user = data.split(':');
			await checkPassword('login', user[1] || data, 
				(value) => waitForAuth = value, 
				(value) => {
					authError = value;
					if (value) {
						// Re-show keyboard on error
						setTimeout(() => {
							$keyboardStore.hidden = false;
						}, 100);
					}
				}, 
				async (level) => {
					if (level >= 2) {
						authError = false;
						waitForAuth = false;
						showSystemModal = true;
					} else {
						authError = true;
						// Re-show keyboard on insufficient level
						setTimeout(() => {
							$keyboardStore.hidden = false;
						}, 100);
					}
				}
			);
		};
		$keyboardStore.inputType = 'loginPassword';
		$keyboardStore.hidden = false;
		$keyboardStore = $keyboardStore;
	}
	// System control functions
	async function performReboot() {
		try {
			const response = await fetch(getHttpUrl('/iot/reboot'), {
				method: 'POST',
				headers: {
					'Content-Type': 'application/json'
				}
			});
			
			if (response.ok) {
				// Logout and go to homepage
				await logoutAndGoHome();
			} else {
				console.error('Failed to initiate reboot');
			}
		} catch (error) {
			console.error('Error initiating reboot:', error);
		}
	}

	async function performShutdown() {
		try {
			const response = await fetch(getHttpUrl('/iot/shutdown'), {
				method: 'POST',
				headers: {
					'Content-Type': 'application/json'
				}
			});
			
			if (response.ok) {
				// Logout and go to homepage
				await logoutAndGoHome();
			} else {
				console.error('Failed to initiate shutdown');
			}
		} catch (error) {
			console.error('Error initiating shutdown:', error);
		}
	}

	async function logoutAndGoHome() {
		// Reset authentication state
		$keysStore.accessLevel = 0;
		$keysStore.localAllowed = false;
		
		// Reset navigation state
		$navigationStore.level = 0;
		$navigationStore.name = '';
		$navigationStore.dropDownPage = '';
		
		// Close any open modals
		cancelAction();
		
		// Navigate to homepage while preserving redirect parameter
		let targetUrl = '/';
		if ($navigationStore.redirect) {
			targetUrl = '/?-Redirect';
		}
		await goto(targetUrl);
	}

	function showRebootConfirm() {
		confirmAction = 'reboot';
		confirmMessage = $t('global.reboot-confirmation');
		showConfirmModal = true;
		showSystemModal = false;
	}

	function showShutdownConfirm() {
		confirmAction = 'shutdown';
		confirmMessage = $t('global.shutdown-confirmation');
		showConfirmModal = true;
		showSystemModal = false;
	}

	async function confirmSystemAction() {
		if (confirmAction === 'reboot') {
			await performReboot();
		} else if (confirmAction === 'shutdown') {
			await performShutdown();
		}
		showConfirmModal = false;
		confirmAction = '';
	}	function cancelAction() {
		showSystemModal = false;
		showConfirmModal = false;
		confirmAction = '';
		authError = false;
		waitForAuth = false;
		isLongPressing = false;
		if (longPressTimer) {
			clearTimeout(longPressTimer);
		}
	}

	function processNetworkNodesFromPolling(data: { nodes: Array<{text: string, value: string, id?: string}>, localIP?: string } | null | undefined) {
		// Guard against null/undefined data (can happen when controller is in bootloader mode)
		if (!data || typeof data !== 'object') {
			return;
		}
		// Data is already processed TCP/IP nodes, no need to call processNetworkMonitorData
		updateNavigationNodes(data.nodes, data.localIP);
		
		// Don't change selectedPanel here - let the reactive logic handle it
		// The current panel should always remain selected unless user actively changes it
	}

onMount(async () => {
	let ip: string;
	let port: string;
	client = new WsClient(getHttpUrl('/iot/ws'), 'header-data', async (data) => {
		$headersStore = data as Headers;
		if ($navigationStore.setOptions) {
			$navigationStore.setOptions = false;
			ip = await getIP();
			port = await getPort();
			// Set current panel as first option
			selectedPanel = `${ip}:${port}`;
		}
	});
	client.connect();

	// Set up TCP/IP data Polling listener to populate dropdown (lightweight updates)
	networkClient = new WsClient(getHttpUrl('/iot/ws'), 'tcpip-data', (data) => {
		processNetworkNodesFromPolling(data as unknown as { nodes: Array<{text: string, value: string}> });
	});
	networkClient.connect();
});

onDestroy(() => {
	client?.close();
	client = undefined;
	networkClient?.close();
	networkClient = undefined;
	if (longPressTimer) {
		clearTimeout(longPressTimer);
	}
	// Clean up any authentication state
	waitForAuth = false;
	authError = false;
	isLongPressing = false;
});
</script>

<div class="bg-gray-300 bg-opacity-50 p-2 sm:py-4 sm:px-8 rounded-t-md sm:rounded-t-3xl w-full flex flex-col" bind:this={div}>
	<div class="w-full flex mb-4">
		<div class="w-1/4">
			<!-- svelte-ignore a11y-no-noninteractive-element-interactions -->
			<img 
				class="h-full bg-opacity-100 {uiDisabled ? 'opacity-50 cursor-not-allowed' : 'cursor-pointer'} select-none" 
				src="/gellert-logo.png" 
				alt="gellert logo"
				on:mousedown={handleLogoMouseDown}
				on:mouseup={handleLogoMouseUp}
				on:mouseleave={handleLogoMouseUp}
				on:touchstart={handleLogoTouchStart}
				on:touchend={handleLogoTouchEnd}
				on:touchcancel={handleLogoTouchEnd}
				on:contextmenu={handleLogoContextMenu}
				draggable="false"
			/>
		</div>
		<div class="w-1/2 flex flex-col text-center">
			{#if $navigationStore.redirect}
				<div class="flex flex-row items-center justify-center"><TextField size="xl" edit={false} value={$headersStore.PanelName} /> <Button class="ml-2" on:click={backToHomePanel}>Close</Button></div>
			{:else}
				<Select class="w-[90%] font-bold text-black leading-snug" size="xl" extended="w-full" options={panelOptions} bind:value={selectedPanel} edit={!uiDisabled}
					on:change={gotoSelectedPanel}/>
			{/if}
			<h2 class="text-size-xl font-bold"
				style="color: {currentMode?.color ?? '#ddddc8'}"
			>
				{$t('global.current-mode')}: {toUpper(currentMode?.text) ?? "UNKNOWN MODE"}
			</h2>
		</div>
		<div class="w-1/4 flex flex-col text-size-large">
			<span class="ml-auto mr-2">{dateOutput}</span>
			<Select class="w-full mx-auto" size="lg" options={pageList} bind:value={$navigationStore.dropDownPage} edit={!uiDisabled} on:change={gotoSelectedPage} />
		</div>
	</div>
</div>

<!-- System Control Modal -->
{#if showSystemModal}
	<div class="fixed inset-0 bg-black bg-opacity-50 flex items-center justify-center z-50">
		<div class="bg-white rounded-lg shadow-lg w-11/12 sm:w-1/3">
			<div class="bg-primary-500 text-white py-2 rounded-t-lg">
				<h3 class="text-size-xl font-semibold text-center">{$t('global.system-control')}</h3>
			</div>
			<div class="flex flex-col space-y-4 p-4">
				<Button size="lg"on:click={showRebootConfirm}>{$t('global.reboot-pi')}</Button>
				<Button size="lg" on:click={showShutdownConfirm}>{$t('global.shutdown-pi')}</Button>
				<Button size="lg" on:click={cancelAction}>{$t('global.cancel')}</Button>
			</div>
		</div>
	</div>
{/if}

<!-- Confirmation Modal -->
{#if showConfirmModal}
	<div class="fixed inset-0 bg-black bg-opacity-50 flex items-center justify-center z-50">
		<div class="bg-white rounded-lg shadow-lg w-11/12 sm:w-1/3">
			<!-- Title Bar with Background -->
			<div class="bg-primary-500 text-white py-2 rounded-t-lg">
				<h3 class="text-size-xl font-semibold text-center">{$t('global.confirm-action')}</h3>
			</div>
			<!-- Modal Content -->
			<div class="p-6">
				<p class="text-center mb-6 text-size-xl">{confirmMessage}</p>
				<div class="flex justify-center space-x-4 text-size-xl">
					<Button size="lg" on:click={confirmSystemAction}>
						{$t('global.yes')}
					</Button>
					<Button size="lg" on:click={cancelAction}>
						{$t('global.no')}
					</Button>
				</div>
			</div>
		</div>
	</div>
{/if}
