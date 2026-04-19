<script lang="ts">
	import { getHttpUrl, initializeIotConnection, checkPassword } from "$lib/business/util";
	import '../app.postcss';
	// Initialize i18n (single source in src/lib/locales)
	import '$lib/i18n';
	import { Modal, Toast, initializeStores, type ModalComponent, type ModalSettings, getModalStore, getToastStore, Drawer, getDrawerStore, storePopup } from '@skeletonlabs/skeleton';
	import { computePosition, autoUpdate, offset, shift, flip, arrow } from '@floating-ui/dom';
	import GellertHeader from '$lib/components/GellertHeader.svelte';
	import GellertFooter from '$lib/components/GellertFooter.svelte';
	import {
		localeStore, backgroundStore, keyboardStore, heightsStore, frontMatterStore,
		alarmsStore, dataSelectionStore, upgradeStore, navigationStore, keysStore,
		pageTranslationsStore, modeToColorStore, auxiliaryOptionsStore, equipmentOptionsStore,
		yesNoOptionsStore, failureOptionsStore,
		homePageStore,
	} from '$lib/store';
	import Keyboard from '$lib/ui/Keyboard.svelte';
	import { onDestroy, onMount } from 'svelte';
	import Alarms from '$lib/components/Alarms.svelte';
	import Button from '$lib/ui/Button.svelte';
	import Card from '$lib/ui/Card.svelte';
	import { getData } from '$lib/business/charting'
	import { mdiArrowRightThin } from '@mdi/js';
	import Icon from '$lib/ui/Icon.svelte';
	import { toggleSelection } from '$lib/business/charting';
	import { locale } from 'svelte-i18n';
	import { t } from "svelte-i18n";
	import type { PageList } from '$lib/business/PageType';
	import { type Equipment } from '$lib/business/equipmentStatus';
	import { defaultImages } from '$lib/store';
	import { getDropDownPage } from '$lib/business/paging';
	import { goto } from '$app/navigation';
	import { browser } from '$app/environment';
	import { KeyboardTypes } from '$lib/ui/Keyboard.svelte';
	import WsClient from "$lib/business/wsClient";
	import { enableFetchDebugLogging } from '$lib/business/debugFetch';


	if (browser) {
		enableFetchDebugLogging();
	}

	// Simple variables instead of writable stores - they only need to be set once

	// Authentication state
	let authError = false;
	let authWait = false;

	// Cloud sign-in state (pass 3)
	let showCloudLogin = false;
	let cloudUsername = '';
	let cloudPassword = '';
	let cloudError = '';
	let cloudWait = false;

	initializeStores();
	storePopup.set({ computePosition, autoUpdate, offset, shift, flip, arrow });

	const modalStore = getModalStore();
	const drawerStore = getDrawerStore();
	const toastStore = getToastStore();

	$: modeToColor = {
		0: { color: 'black', text: '' },
    1: { color: 'black', text: $t('mode.shutdown'), image: '/shutdown-50x50.png' },
    2: { color: '#cfbe00', text: $t('mode.standby'), image: '/standby-50x50.png' },
    3: { color: '#993333', text: $t('mode.remote-standby'), image: 'standby-remote-50x50.png' },
    4: { color: '#007a00', text: $t('mode.cooling'), image: 'door-50x50.png' },
    5: { color: '#126399', text: $t('mode.refrigeration'), image: '/refrig-anim-50x50.gif' },
    6: { color: '#5e3f99', text: $t('mode.recirculating'), image: '/recirc-anim-50x50.gif' },
    7: { color: '#cc0000', text: $t('mode.heating'), image: '/burner-anim-50x50.gif' },
    8: { color: '#0000ff', text: $t('mode.defrosting'), image: '/defrost-anim-50x50.gif' },
    9: { color: '#009900', text: $t('mode.purging-co2'), image: '/door-50x50.png' },
    10: { color: '#007a00', text: $t('mode.cooling-ramping'), image: 'door-50x50.png' },
    11: { color: '#126399', text: $t('mode.refrig-ramping'), image: '/refrig-anim-50x50.gif' },
    12: { color: 'black', text: $t('mode.fan-manual') },
    13: { color: '#7d5c00', text: $t('mode.fan-switch-off') },
    14: { color: '#7d5c00', text: $t('mode.fan-remote-off') },
    15: { color: '#7d5c00', text: $t('mode.refrig-remote-off') },
    16: { color: '#ff7f00', text: $t('mode.cure'), image: '/door-50x50.png' },
    17: { color: '#ff7f00', text: $t('mode.cure'), image: '/burner-anim-50x50.gif' },
    18: { color: '#007a00', text: $t('mode.cooling-dehumid'), image: '/door-50x50.png' },
    19: { color: '#126399', text: $t('mode.refrig-dehumid'), image: '/refrig-anim-50x50.gif' },
    20: { color: '#5c1212', text: $t('mode.remote-off') },
    21: { color: '#5c1212', text: $t('mode.failure') },
    22: { color: '#009900', text: $t('mode.fan-boost'), image: '/door-50x50.png' },
    23: { color: '#cc0000', text: $t('mode.heating-ramping'), image: '/burner-anim-50x50.gif' },
    24: { color: '#126399', text: $t('mode.refrig-enthalpy'), image: '/refrig-anim-50x50.gif' },
	} as  Record<number, { color: string, text: string, image?: string }>;

	$: pageTranslations = {
		level1Pages: [
			{ text: $t('system-monitor.system-monitor'), value: '', display: true, navigation: true },
			{ text: $t('level1.network-monitor.network-monitor'), value: 'network', display: true, navigation: false },
			{ text: $t('page-list.refresh-page'), value: 'refresh', display: true, navigation: false },
			{ text: $t('page-list.select-page'), value: 'history', display: false, navigation: false },
			{ text: 'Data Info', value: 'datainfo', display: false, navigation: false },
			{ text: $t('global.plenum-temperature'), value: 'plentemp', display: true, navigation: true },
			{ text: $t('level1.fanruntime.fan-runtimes'), value: 'fanruntime', display: true, navigation: true },
			{ text: $t('level1.pile.pile-sensors'), value: 'pile', display: true, navigation: true },
			{ text: $t('level1.outside.outside-air-control'), value: 'outside', display: true, navigation: true },
			{ text: $t('page-list.run-clock'), value: 'runclock', display: true, navigation: true },
			{ text: $t('page-list.fan-speed'), value: 'fanspeed', display: true, navigation: true },
			{ text: $t('page-list.fan-boost'), value: 'fanboost', display: true, navigation: true },
			{ text: $t('page-list.ramp-rate'), value: 'ramp', display: true, navigation: true },
			{ text: $t('level1.humidifier.humidifier-control'), value: 'humidifier', display: true, navigation: true },
			{ text: $t('level1.climacell.climacell-control'), value: 'climacell', display: true, navigation: true },
			{ text: $t('page-list.co2-purge'), value: 'co2', display: true, navigation: true },
			{ text: $t('page-list.equipment-control'), value: 'equipment', display: true, navigation: true },
			{ text: $t('level1.lights.bay-light-control'),  value: 'lights', display: true, navigation: true },
			{ text: $t('page-list.miscellaneous'), value: 'miscellaneous', display: true, navigation: true },
			{ text: $t('page-list.preferences'), value: 'preferences', display: true, navigation: true },
			{ text: $t('level1.email.email-alerts'), value: 'email', display: true, navigation: true },
			{ text: $t('page-list.alert-setup'), value: 'alerts', display: true, navigation: true },
			{ text: $t('level1.date.set-date-time'), value: 'date', display: true, navigation: true },
			{ text: $t('page-list.software-version'), value: 'version', display: true, navigation: true },
			{ text: $t('page-list.service-information'), value: 'service', display: true, navigation: true },
		],
		level2Pages: [
			{ text: $t('page-list.refresh-page'), value: 'refresh', display: true, navigation: false },
			{ text: $t('page-list.basic'), value: 'basic', display: true, navigation: true },
			{ text: $t('level2.log.log-settings'), value: 'log', display: true, navigation: true },
			{ text: $t('level2.accounts.user-accounts'), value: 'accounts', display: true, navigation: true },
			{ text: $t('page-list.save-settings'), value: 'settings', display: true, navigation: true },
			{ text: $t('global.analog-boards'), value: 'analog', display: true, navigation: true },
			{ text: $t('page-list.fresh-air-door'), value: 'door', display: true, navigation: true },
			{ text: $t('page-list.select-page'), value: 'pid', display: false, navigation: false },
			{ text: $t('page-list.select-page'), value: 'pidlog', display: false, navigation: false },
			{ text: $t('page-list.select-page'), value: 'table', display: false, navigation: false },
			{ text: $t('page-list.select-page'), value: 'graph', display: false, navigation: false },
			{ text: $t('page-list.select-page'), value: 'download', display: false, navigation: false },
			{ text: $t('page-list.burner'), value: 'burner', display: true, navigation: true },
			{ text: $t('global.refrigeration'), value: 'refrigeration', display: true, navigation: true },
			{ text: 'ClimaCell', value: 'climacell', display: true, navigation: true },
			{ text: $t('page-list.io-config'), value: 'ioconfig', display: true, navigation: true },
			{ text: $t('page-list.4-20-outputs'), value: 'pwm', display: true, navigation: true },
			{ text: $t('global.auxiliary'), value: 'auxiliary', display: true, navigation: true },
			{ text: $t('page-list.failures-1'), value: 'failures1', display: true, navigation: true },
			{ text: $t('page-list.failures-2'), value: 'failures2', display: true, navigation: true },
			{ text: $t('page-list.remote-systems'), value: 'remote', display: true, navigation: true },
			{ text: $t('page-list.master-slave'), value: 'master', display: true, navigation: true },
			{ text: $t('page-list.tcpip-setup'), value: 'tcpip', display: true, navigation: true },
			{ text: $t('page-list.iot-config'), value: 'iotclient', display: true, navigation: true },
			{ text: $t('page-list.sales-service'), value: 'service', display: true, navigation: true },
			{ text: $t('page-list.vfd-drives'), value: 'fans', display: true, navigation: true },
		],
	} as PageList;

	$: auxiliaryOptions = {
		unitOptions: [{ value: '0', text: $t('global.minutes') }, { value: '1', text: $t('global.hours') }],
		onOffOptions: [{ value: '0', text: $t('global.off') }, { value: '1', text: $t('global.on') }],
		typeOptions: [
			{ value: '0', text: $t('global.manual') },
			{ value: '1', text: $t('global.output') },
			{ value: '2', text: $t('global.input') },
			{ value: '3', text: $t('global.switch') },
			{ value: '4', text: $t('global.sensor') },
			{ value: '5', text: $t('global.mode') },
			{ value: '255', text: '' },
		],
		opOptions: [
			{ value: '0', text: $t('level2.auxiliary.EQ') },
			{ value: '1', text: $t('level2.auxiliary.GT') },
			{ value: '2', text: $t('level2.auxiliary.LT') },
		],
		andOrOptions: [
			{ value: '0', text: $t('level2.auxiliary.AND') },
			{ value: '1', text: $t('level2.auxiliary.OR') },
			{ value: '255', text: $t('level2.auxiliary.END') },
		],
		auxProgOptions: [
			{ value: '255', text: $t('level2.auxiliary.option') },
			{ value: '0', text: $t('level2.auxiliary.value') },
			{ value: '1', text: $t('level2.auxiliary.reference') },
		],
		modeOptions: [
			{ value: '0', text: $t('global.cooling') },
			{ value: '1', text: $t('global.refrigeration') },
			{ value: '2', text: $t('global.recirculation') },
			{ value: '3', text: $t('level2.auxiliary.heating') },
			{ value: '4', text: $t('level2.auxiliary.purging-co2') },
			{ value: '5', text: $t('level2.auxiliary.defrosting') },
			{ value: '6', text: $t('global.standby') },
			{ value: '7', text: $t('level2.auxiliary.shutdown') },
		],
		availSensors,
	};

	const equipmentOptions = {
		getStatus,
		getRefrigStatus,
		getAuxSwitch,
		getSwitchStatus,
		getDoorDiagStatus,
	}

	$: yesNoOptions = [
		{ text: $t('global.no'), value: '0' },
		{ text: $t('global.yes'), value: '1' },
	];

	$: failureOptions = {
		modeOptions: [
			{ text: $t('global.none'), value: '0' },
			{ text: $t('global.alarm'), value: '1' },
			{ text: $t('global.fail'), value: '2' },
		],
		LightsOptions: [
			{ text: $t('global.minutes'), value: '1' },
			{ text: $t('global.hours'), value: '60' },
		],
		modeLightOptions: [
			{ text: $t('global.none'), value: '0' },
			{ text: $t('global.alarm'), value: '1' },
			{ text: $t('level2.failures1.lights-off'), value: '3' },
		],
		refridgeRunOptions,
	}

	let myDiv: HTMLDivElement;
	let client: WsClient | undefined;
	let vfdAlarmPoll: ReturnType<typeof setInterval> | undefined;
	let hideCursorTimeout: ReturnType<typeof setTimeout>;
	let showCursor: (() => void) | undefined;

	const alarmComponent: ModalComponent = {
		ref: Alarms,
	};

	const modal: ModalSettings = {
		type: 'component',
		component: alarmComponent,
	};
	function normalizeBackground(src: string): string {
		if (!src) return src;
		try {
			// Build URL against current origin (handles relative paths)
			const u = new URL(src, location.origin);
			// If host changed (e.g. after TCP/IP update), rebase to new host
			if (u.host !== location.host) {
				u.host = location.host;
				u.protocol = location.protocol;
				return u.toString();
			}
			return u.toString();
		} catch { return src; }
	}
	// Background image reliability: retain last good image and retry loading if it 404s during reboot/network change
	let resolvedImageUrl: string = '';
	let bgLoadError = false;
	let bgRetryAttempts = 0;
	const maxBgRetries = 8;
	const isBrowser = typeof window !== 'undefined';

	function preloadBackground(url: string) {
		if (!isBrowser || !url) return; // SSR guard
		try {
			// @ts-ignore runtime guard for SSR
			if (typeof Image === 'undefined') return; // Extra safety
			const testUrl = url;
			const img = new Image();
			img.onload = () => {
				bgLoadError = false;
				bgRetryAttempts = 0;
			};
			img.onerror = () => {
				bgLoadError = true;
				if (bgRetryAttempts < maxBgRetries) {
					const retryDelay = Math.min(1000 * Math.pow(1.5, bgRetryAttempts), 8000);
					bgRetryAttempts += 1;
					setTimeout(() => {
						preloadBackground(testUrl.split('?')[0] + `?r=${Date.now()}`);
					}, retryDelay);
				}
			};
			img.src = testUrl;
		} catch (e) {
			// Ignore SSR related errors
		}
	}

	$: if (isBrowser) {
		const candidate = normalizeBackground($backgroundStore.backgroundImage || resolvedImageUrl || '/background/Potatoes.jpg');
		if (candidate && candidate !== resolvedImageUrl) {
			resolvedImageUrl = candidate;
			preloadBackground(resolvedImageUrl);
		}
	} else {
		// On server just set a normalized path (no preload)
		resolvedImageUrl = $backgroundStore.backgroundImage || '/background/Potatoes.jpg';
	}
	$: imageUrl = bgLoadError ? '/background/Potatoes.jpg' : resolvedImageUrl;
	$: $locale = $localeStore;
	$: if (($frontMatterStore?.AlarmData as string[])?.length > 0) {
		// Check for WARN_SLAVENOBROADCAST in any alarm entry
		const hasSlaveBroadcastWarning = ($frontMatterStore.AlarmData as string[]).some(alarm => 
			alarm.includes('WARN_SLAVENOBROADCAST')
		);
		$alarmsStore.slaveBroadcastWarning = hasSlaveBroadcastWarning;
			
		if ($alarmsStore.canShowAlarm && !$alarmsStore.isShowingAlarms
			&& (!$upgradeStore || $navigationStore.level !== 1 || $navigationStore.name !== 'version')) {
				modalStore.trigger(modal);
				$alarmsStore.isShowingAlarms = true;
		}
	} else {
		// Clear the warning when no alarms are present
		$alarmsStore.slaveBroadcastWarning = false;
	}
	$: $pageTranslationsStore = pageTranslations;
	$: $modeToColorStore = modeToColor;
	$: $auxiliaryOptionsStore = auxiliaryOptions;
	$: $equipmentOptionsStore = equipmentOptions;
	$: $yesNoOptionsStore = yesNoOptions;
	$: $failureOptionsStore = failureOptions;
	$: if (!$alarmsStore.canShowAlarm) {
		if ($alarmsStore.handle !== undefined) {
			clearTimeout($alarmsStore.handle);
			$alarmsStore.handle = undefined;
		}
		// wait 5 minutes before we can show alarms again
		$alarmsStore.handle = setTimeout(() => {
			// Clear alarm data after cooldown
			$frontMatterStore.AlarmData = [];
			$alarmsStore.canShowAlarm = true;
		}, 300000);
	}

	// Global local password authentication function
	async function enterLocalPassword() {
		$keyboardStore.keyboardType = KeyboardTypes.Alpha;
		$keyboardStore.label = 'Password';
		$keyboardStore.start = '';
		$keyboardStore.resultReady = async (data: string) => {
			authWait = true;
			authError = false; // Reset error state when starting new attempt
			
			try {
				const user = data.split(':');
				await checkPassword('login', user[1], (_) => {}, (value) => {
					authError = value;
					if (value) {
						// If there's an error, stop waiting immediately
						authWait = false;
					}
				}, async (level) => {
					if (level === 2) {
						$navigationStore.level = 2;
						$navigationStore.name = 'basic';
						await goto('/level2/basic');
						$navigationStore.dropDownPage = 'basic';
					}
					authWait = false;
				});
			} catch (error) {
				// Handle any unexpected errors
				console.error('Password check failed:', error);
				authError = true;
				authWait = false;
			}
		};
		$keyboardStore.inputType = 'loginPassword';
		$keyboardStore.hidden = false;
		$keyboardStore = $keyboardStore;
	}

	// Sign in with a Django cloud account (pass 3)
	async function submitCloudLogin() {
		if (!cloudUsername || !cloudPassword) {
			cloudError = 'Username and password required';
			return;
		}
		cloudWait = true;
		cloudError = '';
		try {
			const resp = await fetch(getHttpUrl('/iot/cloud/password-login'), {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ username: cloudUsername, password: cloudPassword }),
			});
			const j = await resp.json().catch(() => ({}));
			if (!resp.ok || !j?.ok) {
				cloudError = j?.error || `Sign-in failed (${resp.status})`;
				return;
			}
			$keysStore.localAllowed = true;
			$keysStore.accessLevel = j.level;
			cloudUsername = '';
			cloudPassword = '';
			showCloudLogin = false;
			if (j.level === 2) {
				$navigationStore.level = 2;
				$navigationStore.name = 'basic';
				await goto('/level2/basic');
				$navigationStore.dropDownPage = 'basic';
			}
		} catch (e: any) {
			cloudError = e?.message ?? 'Network error';
		} finally {
			cloudWait = false;
		}
	}

	onMount(async () => {
		if (browser) {
			// Register service worker for offline support and error interception
			// This is critical for loopback mode - without SW, browser shows default "no internet" page
			if ('serviceWorker' in navigator) {
				try {
					const registration = await navigator.serviceWorker.register('/service-worker.js', {
						scope: '/'
					});
					console.log('[SW] Service worker registered successfully:', registration.scope);
					
					// Listen for diagnostic messages from service worker
					navigator.serviceWorker.addEventListener('message', (event) => {
						if (event.data?.type === 'SW_DIAGNOSTIC') {
							console.log('[SW Diagnostic]', event.data.payload);
							// Store in sessionStorage for error page to display
							try {
								const logs = JSON.parse(sessionStorage.getItem('sw_diagnostics') || '[]');
								logs.push({ ...event.data.payload, timestamp: Date.now() });
								// Keep only last 50 entries
								if (logs.length > 50) logs.shift();
								sessionStorage.setItem('sw_diagnostics', JSON.stringify(logs));
							} catch (e) {
								console.warn('[SW] Failed to store diagnostic:', e);
							}
						}
					});
				} catch (error) {
					console.warn('[SW] Service worker registration failed:', error);
				}
			}

			// Initialize IoT connection details and determine access mode (loopback vs network)
			// This sets isLocalAccess in navigationStore and stickToLoopback flag
			try {
				await initializeIotConnection();
			} catch (error) {
				console.warn('[access-mode] Initialization failed, defaulting to network mode:', error);
			}
			const hideCursor = () => {
				document.body.classList.remove('show-cursor');
			};

			showCursor = () => {
				document.body.classList.add('show-cursor');
				clearTimeout(hideCursorTimeout);
				hideCursorTimeout = setTimeout(hideCursor, 3000); // Hide after 3 seconds
			};

			document.body.addEventListener('mousemove', showCursor);

			// Initially hide the cursor
			hideCursor();
		}

		$heightsStore.main = myDiv.clientHeight;
		client = new WsClient(
			getHttpUrl('/iot/ws'),
			'frontmatter-data',
			(fms) => {
				// Handle toast notifications from backend (e.g. save failures)
				const data = fms as any;
				if (data.type === 'notification') {
					toastStore.trigger({
						message: data.message,
						background: data.level === 'error' ? 'variant-filled-error' : 'variant-filled-success',
						timeout: 5000
					});
					return;
				}

				const frontmattter = fms as Record<string, string | string[]>;
				try {
					if ($navigationStore.inLoad) {
						const alarmData = (frontmattter.AlarmData as string[]).filter((alarm) => 
							!alarm.toLowerCase().includes('system controller is not responding')
						);
						$frontMatterStore = { ...frontmattter, AlarmData: alarmData };
					} else {
						$keysStore.localRequired = frontmattter.localLogin === 'true';
						$keysStore.hasLevel1Password = frontmattter.hasLevel1Password === 'true';
						$frontMatterStore = frontmattter;
					}
				} catch (err) {
					console.error('Failed to parse Polling data:', err); 
				}
			}
		);
		
		client.connect();

		// ── VFD Alarm Poller ──
		// Poll /vfd/alarms every 3s and merge VFD fault entries into AlarmData
		// so they appear in the global alarm modal alongside ARM alarms.
		vfdAlarmPoll = setInterval(async () => {
			try {
				const resp = await fetch(getHttpUrl('/vfd/alarms'), { cache: 'no-store' });
				if (!resp.ok) return;
				const data = await resp.json();
				const vfdAlarms: string[] = Array.isArray(data.alarms) ? data.alarms : [];
				// Merge: replace any existing WARN_VFD entries, keep all others
				const current = ($frontMatterStore?.AlarmData as string[]) ?? [];
				const nonVfd = current.filter((a: string) => !a.startsWith('WARN_VFD='));
				if (vfdAlarms.length > 0 || nonVfd.length !== current.length) {
					$frontMatterStore = { ...$frontMatterStore, AlarmData: [...nonVfd, ...vfdAlarms] };
				}
			} catch {
				// VFD server unreachable — ignore
			}
		}, 3000);

		 // Add error handling for unauthorized responses
		const handleUnauthorized = async (response: Response) => {
			if (response.status === 401) {
				await fetch(getHttpUrl('/iot/logout'), {
					method: 'POST',
					headers: {
						'Content-Type': 'application/json'
					}
				});
				$navigationStore = { ...$navigationStore, level: 0, name: '', data: '', invalidate: false };
				$keysStore.accessLevel = 0;
				
				// Preserve redirect parameter when navigating to home page
				let targetUrl = $homePageStore.page;
				if ($navigationStore.redirect && targetUrl === '/') {
					targetUrl = '/?-Redirect';
				}
				goto(targetUrl);
				$navigationStore.dropDownPage = getDropDownPage($homePageStore.page);
				$navigationStore.lastPressedButton = null;
				return true;
			}
			return false;
		};

		// Add to window global for use in routes
		if (typeof window !== 'undefined') {
			window.handleUnauthorized = handleUnauthorized;
		}

		// Preserve user's background selection while updating image list
		// Use persisted store (do NOT remove existing localStorage entry so custom images persist)
		backgroundStore.useLocalStorage();
		// Ensure images array includes any previously saved custom images plus defaults
		backgroundStore.update(bgState => {
			// Rebuild images from defaults + existing customImages (persisted)
			const custom = Array.isArray((bgState as any).customImages) ? (bgState as any).customImages : [];
			bgState.images = [...defaultImages, ...custom];
			// If current backgroundImage missing or no longer present, default it
			if (!bgState.backgroundImage || !bgState.images.some((img: { value: any; }) => img.value === bgState.backgroundImage)) {
				bgState.backgroundImage = custom[0]?.value || defaultImages[0].value;
			}
			// Rebase any stored custom image URLs to new host after network change
			try {
				const rebasedImages = bgState.images.map((img: any) => {
					try {
						if (typeof img.value === 'string' && img.value.includes('/iot/background-pictures/file/')) {
							const u = new URL(img.value, location.origin);
							if (u.host !== location.host) {
								u.host = location.host; u.protocol = location.protocol; return { ...img, value: u.toString() };
							}
						}
					} catch {/* ignore single image errors */}
					return img;
				});
				bgState.images = rebasedImages;
				// If selected background uses old host, normalize it (preserve original path segment)
				bgState.backgroundImage = normalizeBackground(bgState.backgroundImage);
			} catch {/* ignore rebase issues */}
			return bgState;
		});
		// Load pictures from backend (async) to refresh custom images; keep current selection if still valid
		backgroundStore.loadPictures?.();

		localeStore.useLocalStorage();

	});

	onDestroy(() => {
		if (browser) {
			clearTimeout(hideCursorTimeout);
			if (showCursor) {
				document.body.removeEventListener('mousemove', showCursor);
			}
			// Ensure the class is removed on component destruction
			document.body.classList.remove('show-cursor');
		}
		client?.close();
		client = undefined;
		if (vfdAlarmPoll) { clearInterval(vfdAlarmPoll); vfdAlarmPoll = undefined; }
		modalStore.clear();
	});

  function handleResize() {
		$heightsStore.main = myDiv.clientHeight;
	}

	function availSensors(ref: boolean): { text: string, value: string }[] {
		const sensors: { text: string, value: string }[] = [];

		if (ref) {
			sensors.push({ text: `${$t('level2.auxiliary.temp-setpoint')} #1`, value: '-2' });
			sensors.push({ text: `${$t('level2.auxiliary.temp-setpoint')} #2`, value: '-8' });
		}

		sensors.push({ text: $t('level2.auxiliary.plenum-temp'), value: '-1' });
		sensors.push({ text: $t('level2.auxiliary.outside-temp'), value: '2' });
		sensors.push({ text: `${$t('level2.auxiliary.return-temp')} #1`, value: '3' });
		sensors.push({ text: `${$t('level2.auxiliary.return-temp')} #2`, value: '10' });

		if (ref) {
			sensors.push({ text: $t('level2.auxiliary.humidity-setpoint'), value: '-3' });
		}

		sensors.push({ text: $t('global.plenum-humidity'), value: '5' });
		sensors.push({ text: $t('level2.auxiliary.outside-humidity'), value: '4' });
		sensors.push({ text: `${$t('global.return-humidity')} #1`, value: '6' });
		sensors.push({ text: `${$t('global.return-humidity')} #2`, value: '8'});

		if (!ref) {
			sensors.push({ text: $t('level2.auxiliary.fan-speed'), value: '-4' });
			sensors.push({ text: $t('level2.auxiliary.refrig-output'), value: '-5' });
			sensors.push({ text: $t('system-monitor.cooling-output'), value: '-6' });
		} else {
			sensors.push({ text: $t('level2.auxiliary.cooling-available'), value: '-7' });
			sensors.push({ text: $t('level2.auxiliary.co2-setpoint'), value: '-9' });
		}

		sensors.push({ text: `${$t('level2.auxiliary.co2-level')} #1`, value: '7' });
		sensors.push({ text: `${$t('level2.auxiliary.co2-level')} #2`, value: '9' });

		return sensors;
	}

	function getStatus(equipment: Equipment, eq: string, remote: string, input: string, output: string): { status: string, color: string } {
		if (eq === 'door') {
			return { status: input, color: 'black' }; 
		}
		if (eq === 'refrig') {
			const diag = [41, 42, 43, 44, 45, 46, 89, 90, 91, 92].reduce((acc, val) => (equipment.eqStatus[val] === '2' && acc !== '2') ? '2' : '0', '0');
			return getRefrigStatus(remote, input, output, diag);
		}
		if (remote === '1') {
			return { status: $t('global.rem-off'), color: 'text-red-500 font-bold' };
		} else if (input === '0')
			return { status: $t('global.on'), color: 'text-green-700 font-bold' };
		else {
			return { status: $t('global.off'), color: 'text-red-500 font-bold' };
		}
	}

	function getDoorDiagStatus(input: string, output: string, diag: string): { status: string, color: string } {
		const status = { status: $t('global.off'), color: 'text-red-500 font-bold' };
		if (diag === '2') {
				status.status = $t('global.open');
				status.color = 'text-blue-500 font-bold';
		} else if (diag === '1') {
			status.status = $t('global.close');
			status.color = 'text-red-700 font-bold';
		} else if (diag === '0') {
			status.status = $t('global.diag-off');
			status.color = 'text-black font-bold';
		}
		if (input === '1') {
			status.status = $t('global.off');
			status.color = 'text-red-500 font-bold';
		}
		return status;
	}

	function getRefrigStatus(remote: string, input: string, output: string, diag: string): { status: string, color: string } {
		const status = { status: $t('global.off'), color: 'text-red-500 font-bold' };
		if (diag === '2') {
				status.status = $t('global.diag-on');
				status.color = 'text-blue-500';
		}
		else if (output === '1') {
			status.status = $t('global.on');
			status.color = 'text-green-700 font-bold';
		}
		if (input === '1') {
			status.status = $t('global.off');
			status.color = 'text-red-500 font-bold';
		}
		if (remote === '1') {
			status.status = $t('global.rem-off');
			status.color = 'text-red-500 font-bold';
		}
		return status;
	}

	function getAuxSwitch(edit: boolean, status: string, switch1: string[], switch2: string[], auxiliary: number): { status: string, color: string } {
		if (auxiliary > -1 && switch1[auxiliary] !== '5') {
			return getSwitchStatus(switch1[auxiliary]);
		} else if (auxiliary > -1 && switch2[auxiliary] !== '5') {
			return getSwitchStatus(switch2[auxiliary]);
		} else if (!edit) {
			if (status === '0') {
				return { status: $t('global.on'), color: 'text-green-700 font-bold' };
			} else {
				return { status: $t('global.off'), color: 'text-red-500 font-bold' };
			}
		} else {
			return { status: $t('global.off'), color: 'text-red-500 font-bold' };
		}
	}

	function getSwitchStatus(value: string): { status: string, color: string } {
		switch (value) {
			case '1':
				return { status: $t('global.auto'), color: 'text-green-700 font-bold' };
			case '2':
				return { status: $t('global.manual'), color: 'text-black' };
			case '3':
				return { status: $t('global.on'), color: 'text-green-700 font-bold' };
			case '4':
			default:
				return { status: $t('global.off'), color: 'text-red-500 font-bold' };
		}
	}
	function refridgeRunOptions(boardType: string, controllerVersion: string): { text: string, value: string }[] {
		const RefridgeRun: { text: string, value: string }[] = [
			{ text: ' ', value: '255' },
			{ text: $t('level2.failures2.recirculate'), value: '0' },
			{ text: $t('global.standby'), value: '1' },
			{ text: $t('global.refrigeration'), value: '2' }
		];

		if (boardType === 'AS1' || (boardType === 'AS2' && (controllerVersion === '1.05' || parseFloat(controllerVersion) > 1.05))) {
			RefridgeRun.push({ value: '2', text: $t('global.refrigeration') });
		}
		return RefridgeRun;
	}
</script>

<svelte:window on:resize={handleResize} />

<Modal
	buttonNeutral="text-white bg-primary-800 hover:bg-primary-700 text-xl md:text-2xl xl:text-4xl m-0 rounded-none"
	buttonPositive="text-white bg-primary-900 hover:bg-primary-700 text-xl md:text-2xl xl:text-4xl m-0 rounded-none"
	regionHeader="text-xl md:text-2xl xl:text-4xl bg-primary-900 text-white p-2"
	regionBody="text-2xl md:text-3xl xl:text-5xl m-2"
	regionFooter="px-4 py-3 flex items-center justify-end gap-3"
	spacing="!p-0"
/>

<Drawer on:backdrop={getData}>
	{#if $drawerStore.id === 'UserLog'}
		<Button class="!my-1 float-right mr-4" on:click={() => { drawerStore.close(); getData(); }}>
			<Icon src={mdiArrowRightThin} class="w-6 h-6 fill-white stroke-white" />
		</Button>
		{#each $dataSelectionStore.selections as item, index}
			<Button class="mx-2 !my-1 w-[95%] {$dataSelectionStore.selected[index] ? '!bg-primary-500 hover:!bg-primary-600 focus:!bg-primary-800 focus:hover:!bg-primary-700 text-white' : 'focus:!bg-primary-500'}" noFocus={true} on:click={() => toggleSelection(index)}>{item.label}</Button>
		{/each}
	{/if}
</Drawer>

<div class="bg-no-repeat bg-cover h-screen flex flex-col min-w-[625] min-h-[480]" style="background-image: url({imageUrl}); " bind:this={myDiv}>
	{#if $keysStore.localRequired && !$keysStore.localAllowed}
		<div class="fixed inset-0 bg-black bg-opacity-75 z-40 flex items-center justify-center">
			<Card class="w-full max-w-md mx-4 p-8 flex flex-col items-center">
				<h2 class="text-2xl font-bold mb-8 text-center">{$t('system-monitor.system-login')}</h2>
				{#if !showCloudLogin}
					<Button class="mx-auto mb-3" size="xl" disabled={authWait} on:click={enterLocalPassword}>
						{authError ? $t('global.retry') : $t('system-monitor.enter-password')}
					</Button>
					<Button class="mx-auto" size="xl" disabled={authWait} on:click={() => { showCloudLogin = true; cloudError = ''; }}>
						Sign in with cloud account
					</Button>
					{#if authWait}
						<div class="mt-4 text-center">
							<div class="inline-block animate-spin rounded-full h-8 w-8 border-b-2 border-primary-500"></div>
						</div>
					{/if}
				{:else}
					<div class="w-full flex flex-col gap-3">
						<label class="flex flex-col text-sm">
							<span class="mb-1">Cloud username</span>
							<input type="text" autocomplete="username" class="border rounded px-3 py-2 text-lg"
								bind:value={cloudUsername} disabled={cloudWait} />
						</label>
						<label class="flex flex-col text-sm">
							<span class="mb-1">Cloud password</span>
							<input type="password" autocomplete="current-password" class="border rounded px-3 py-2 text-lg"
								bind:value={cloudPassword} disabled={cloudWait}
								on:keydown={(e) => { if (e.key === 'Enter') submitCloudLogin(); }} />
						</label>
						{#if cloudError}
							<div class="text-red-600 text-sm">{cloudError}</div>
						{/if}
						<div class="flex gap-2 mt-2">
							<Button class="flex-1" size="lg" disabled={cloudWait} on:click={submitCloudLogin}>
								{cloudWait ? 'Signing in…' : 'Sign in'}
							</Button>
							<Button class="flex-1" size="lg" disabled={cloudWait} on:click={() => { showCloudLogin = false; cloudError = ''; }}>
								Cancel
							</Button>
						</div>
					</div>
				{/if}
			</Card>
		</div>
	{:else}
		<div class="bg-white bg-opacity-30 rounded-3xl absolute inset-0 sm:inset-1 md:inset-2 flex flex-col">
			<GellertHeader bind:height={$heightsStore.header} />
			<slot></slot>
			<GellertFooter bind:height={$heightsStore.footer} />
		</div>
	{/if}
	<Toast position="tr" />
	<Keyboard
		label={$keyboardStore.label}
		hidden={$keyboardStore.hidden}
		start={$keyboardStore.start}
		keyboardType={$keyboardStore.keyboardType}
		inputType={$keyboardStore.inputType}
		on:close-keyboard={() => {
			$keyboardStore.hidden = true;
		}}
		on:result-available={(input) => {
			const result = input.detail;
			$keyboardStore.resultReady(result);
			$keyboardStore.hidden = true;
		}}
		on:cloud-login={() => {
			$keyboardStore.hidden = true;
			showCloudLogin = true;
			cloudError = '';
		}}
	/>

	<!-- Standalone cloud sign-in modal (pass 3) — accessible from overlay or keyboard. -->
	{#if showCloudLogin && !($keysStore.localRequired && !$keysStore.localAllowed)}
		<div class="fixed inset-0 bg-black bg-opacity-75 z-50 flex items-center justify-center">
			<Card class="w-full max-w-md mx-4 p-8 flex flex-col">
				<h2 class="text-2xl font-bold mb-6 text-center">Sign in with cloud account</h2>
				<div class="w-full flex flex-col gap-3">
					<label class="flex flex-col text-sm">
						<span class="mb-1">Cloud username</span>
						<input type="text" autocomplete="username" class="border rounded px-3 py-2 text-lg"
							bind:value={cloudUsername} disabled={cloudWait} />
					</label>
					<label class="flex flex-col text-sm">
						<span class="mb-1">Cloud password</span>
						<input type="password" autocomplete="current-password" class="border rounded px-3 py-2 text-lg"
							bind:value={cloudPassword} disabled={cloudWait}
							on:keydown={(e) => { if (e.key === 'Enter') submitCloudLogin(); }} />
					</label>
					{#if cloudError}
						<div class="text-red-600 text-sm">{cloudError}</div>
					{/if}
					<div class="flex gap-2 mt-2">
						<Button class="flex-1" size="lg" disabled={cloudWait} on:click={submitCloudLogin}>
							{cloudWait ? 'Signing in…' : 'Sign in'}
						</Button>
						<Button class="flex-1" size="lg" disabled={cloudWait} on:click={() => { showCloudLogin = false; cloudError = ''; cloudPassword = ''; }}>
							Cancel
						</Button>
					</div>
				</div>
			</Card>
		</div>
	{/if}
</div>

<!--
{#if $rebootStore.isRebooting}
	<div class="fixed inset-0 bg-black/80 z-[9999] flex flex-col items-center justify-center text-white">
		<div class="text-3xl font-semibold mb-4">{$t('global.system-restarting') || 'System Restarting'}</div>
		<div class="text-6xl font-bold tabular-nums">{$rebootStore.remaining}s</div>
		<div class="mt-6 text-xl opacity-80 max-w-md text-center">{$t('level1.version.please-wait') || 'Please wait while the controller reboots. UI requests are paused.'}</div>
	</div>
{/if}
-->
