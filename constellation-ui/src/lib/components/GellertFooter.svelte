<script lang="ts">
	import { goto } from '$app/navigation';
    import Button from '$lib/ui/Button.svelte';
    import Wait from '$lib/ui/Wait.svelte';
    import { DirectionEnum, getDropDownPage } from '$lib/business/paging';
    import { keysStore, keyboardStore, homePageStore, historyStore, cachedDataStore, datesStore } from '$lib/store';
	import { KeyboardTypes } from '$lib/ui/Keyboard.svelte';
    import { checkPassword, getHttpUrl, safeJsonParse, isLoopbackAccess } from '$lib/business/util';
    import { getModalStore, type ModalSettings } from '@skeletonlabs/skeleton';
    import screenfull from 'screenfull';
    import { t } from 'svelte-i18n';
    import { navigationStore } from '$lib/store';
    import { page } from '$app/stores';
    import { getIP } from "$lib/business/network";
	import { format } from 'date-fns';
    import { getData } from "$lib/business/charting";
	import { onMount, onDestroy } from 'svelte';
    import { FooterNavigationAdapter } from '$lib/utils/footerNavigationAdapter';
	
    export let height: number;

    let div: HTMLDivElement;
    let isFullscreenActive = false;

    const modalStore = getModalStore();

    // Handle fullscreen changes
    function handleFullscreenChange() {
        if (typeof window !== 'undefined' && screenfull.isEnabled) {
            isFullscreenActive = screenfull.isFullscreen;
        }
    }

    onMount(() => {
        if (typeof window !== 'undefined' && screenfull.isEnabled) {
            screenfull.on('change', handleFullscreenChange);
            // Initial state
            isFullscreenActive = screenfull.isFullscreen;
        }
    });

    onDestroy(() => {
        if (typeof window !== 'undefined' && screenfull.isEnabled) {
            screenfull.off('change', handleFullscreenChange);
        }
    });

    function checkDirty(action: () => void) {
        if ($navigationStore.isDirty()) {
            const modal: ModalSettings = {
                type: 'confirm',
                // Data
                title: $t('global.confirm'),
                body: $t('global.are-you-sure'),
            };
            modal.buttonTextCancel=$t('global.no');
        	modal.buttonTextConfirm=$t('global.yes');

            // TRUE if confirm pressed, FALSE if cancel pressed
            modal.response = (r: boolean) => { if (r) {
                action();
                $navigationStore.isDirty = () => false;
            }};
            modalStore.trigger(modal);
        } else {
            action();
        }
    }

    $: error = false;
    $: wait = false;
    $: programText = error ?  $t('global.retry') : ($navigationStore.level <= 1 ? $t('global.program') : $t('global.pgm-l1'));
    $: homeText = $navigationStore.level < 1 ? $t('global.home') : $t('global.exit');
    $: height = div?.clientHeight;
    $: showPrevious = isInUserLogSequence()
        ? !hasPreviousLogState()
        : !FooterNavigationAdapter.canNavigatePrevious($navigationStore.level, $navigationStore.name);
    $: showNext = isInUserLogSequence()
        ? !hasNextLogState()
        : !FooterNavigationAdapter.canNavigateNext($navigationStore.level, $navigationStore.name);
    
    // Enhanced fullscreen logic
    $: {
        if (typeof window !== 'undefined') {
            const screenfullEnabled = screenfull.isEnabled;
            const inKioskMode = isLoopbackAccess();
            // Show fullscreen button if:
            // 1. Screenfull is enabled AND
            // 2. Not in kiosk mode (loopback access) AND
            // 3. Either not in fullscreen OR user manually entered fullscreen (so they can exit)
            $navigationStore.showFullscreen = screenfullEnabled && !inKioskMode && (!isFullscreenActive || $navigationStore.hasUserPressedFullScreen);
        } else {
            $navigationStore.showFullscreen = false;
        }
    }

    // Disable UI controls when local password is required but local access isn't allowed
    $: uiDisabled = $keysStore.localRequired && !$keysStore.localAllowed;

    // Helper function to navigate to home page while preserving redirect parameter
    function gotoHomePageWithRedirect() {
        let targetUrl = $homePageStore.page;
        if ($navigationStore.redirect && targetUrl === '/') {
            targetUrl = '/?-Redirect';
        }
        goto(targetUrl);
    }


    async function gotoHome() {
        if (uiDisabled) return; // Prevent navigation when UI is disabled
        
        $navigationStore.lastPressedButton = 'home';
        checkDirty(async () => {
            error = false;
            wait = true;
            await fetch(getHttpUrl('/iot/logout'), {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
            });
            $navigationStore = { ...$navigationStore, level: 0, name: '', data: '', invalidate: false };
            $keysStore.accessLevel = 0;
            
            // Preserve redirect parameter when navigating to home page
            gotoHomePageWithRedirect();
            $navigationStore.dropDownPage = getDropDownPage($homePageStore.page);
            wait = false;
        });
    }

    function refresh() {
        if (uiDisabled) return; // Prevent refresh when UI is disabled
        
        $navigationStore.lastPressedButton = 'refresh';
        checkDirty(() => location.reload())
    }

    function isInUserLogSequence() {
        const path = $page.url.pathname;
        return path.startsWith('/history');
    }

    function hasPreviousLogState() {
        return $historyStore.showMain || $historyStore.showData || $historyStore.showDownload || $historyStore.showRange;
    }

    function hasNextLogState() {
        return $historyStore.showLog || $historyStore.showRange || $historyStore.showData;
    }

    async function getPreviousLogState() {
        if ($historyStore.showMain) {
            return { showMain: false, showRange: true, inSequence: true };
        } else if ($historyStore.showData) {
            return { showData: false, showRange: true, inSequence: true };
        } else if ($historyStore.showDownload) {
            return { showDownload: false, showRange: true, inSequence: true, downloadFromBackup: false };
        } else if ($historyStore.showRange) {
            return { showRange: false, showLog: true };
        } else {
            return null; // At beginning of sequence
        }
    }

    async function getNextLogState() {
        if ($historyStore.showLog) {
            return { showLog: false, showRange: true };
        } else if ($historyStore.showRange) {
            // process the reqular UI Sequence request
            if ($historyStore.inSequence) {
                if ($historyStore.logType === 'File' || $historyStore.logType === 'Backup') {
                    return { showRange: false, showDownload: true, display: await getIP() };
                } else {
                    return { showRange: false, showData: true };
                }
            } else {
                // request to update the range from (Graph|Table) display
                await getInitialData();
                return {showRange: false, showMain: true };
            }
        } else if ($historyStore.showData) {
            await getInitialData();
            return {
                showData: false,
                showDownload: $historyStore.logType === 'File' || $historyStore.logType === 'Backup',
                showMain: $historyStore.logType === 'Graph' || $historyStore.logType === 'Table',
                inSequence: false, // Mark sequence as complete after first pass
            };
        } else {
            return null; // At end of sequence
        }
    }

    async function getInitialData() {
        wait = true;
        $cachedDataStore = {};
        let url: URL | undefined = undefined;
        if ($historyStore.type === 'Activity') {
            url = new URL(getHttpUrl(`/iot/activity/dates`));
            url.searchParams.append('start', $historyStore.start);
            url.searchParams.append('end', $historyStore.end);
        } else if ($historyStore.type === 'User') {
            url = new URL(getHttpUrl(`/iot/user/dates`));
            url.searchParams.append('start', format($historyStore.startDate, "MM/dd/yyyy HH:mm"));
            url.searchParams.append('end', format($historyStore.endDate, "MM/dd/yyyy HH:mm"));
        }
        if (url !== undefined) {
            try {
                $datesStore = await safeJsonParse(await fetch(url));
            } finally {
                await getData();
            }
        }
        wait = false;
    };

    async function moveToPage(direction: DirectionEnum, name: string, level: number) {
        if (isInUserLogSequence()) {
            if (direction === DirectionEnum.PREV) {
                const previousState = await getPreviousLogState();
                if (previousState) {
                    // Update historyStore with previous state
                    $historyStore = { ...$historyStore, ...previousState };
                    $navigationStore.dropDownPage = 'history';
                } else {
                    // At beginning of sequence, go back to history
                    goto('/history');
                    $navigationStore.name = 'history';
                    $navigationStore.dropDownPage = 'history';
                }
            }
            else {
                const nextState = await getNextLogState();
                if (nextState) {
                    $historyStore = { ...$historyStore, ...nextState };
                    $navigationStore.dropDownPage = 'history';
                }
            }

        } else if ($navigationStore.name === 'history') {
            wait = true;
            await fetch(getHttpUrl('/iot/logout'), {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
            });
            $navigationStore = {...$navigationStore, level: 0, name: '', data: '', invalidate: false };
            $keysStore.accessLevel = 0;
            gotoHomePageWithRedirect();
            $navigationStore.dropDownPage = '';
            wait = false;
        } else if (direction === DirectionEnum.NEXT) {
            const navigated = await FooterNavigationAdapter.navigateToNext(level, name);
            if (navigated) {
                // dropDownPage is set inside adapter; keep it aligned with target page
                $navigationStore.dropDownPage = $navigationStore.name;
            }
        } else if (direction === DirectionEnum.PREV) {
            if ($navigationStore.name === 'history'){
                goto('/history');
                $navigationStore.dropDownPage = 'history';
                return;
            }

            const navigated = await FooterNavigationAdapter.navigateToPrevious(level, name);
            if (navigated) {
                const isSystemMonitor = $navigationStore.name === '';
                $navigationStore.dropDownPage = isSystemMonitor ? '' : $navigationStore.name;
            }
        } else {
            // assume we want to go home set the store and navigate there
            $navigationStore = { ...$navigationStore, level: 0, name: '', data: '', invalidate: false };
            gotoHomePageWithRedirect();
            $navigationStore.dropDownPage = getDropDownPage($homePageStore.page);
        }
    }

    function navigate(direction: DirectionEnum) {
        if (uiDisabled) return; // Prevent navigation when UI is disabled
        
        $navigationStore.lastPressedButton = direction === DirectionEnum.PREV ? 'back' : 'next';
        error = false;
        const { name, level } = $navigationStore;
        checkDirty(() => moveToPage(direction, name, level));
    }

    async function navigateLevel(level: number) {
        if (level === 1) {
            if ($navigationStore.name === '' || $navigationStore.level === 2) {
                $navigationStore.level = 1;
                $navigationStore.name = 'plentemp';
                goto('/level1/plentemp');
                $navigationStore.dropDownPage = 'plentemp';
            } else if ($navigationStore.name === 'history') {
                $navigationStore.level = 1;
                goto(`/${$navigationStore.name}`);
            } else {
                $navigationStore.level = 1;
                goto(`/level1/${$navigationStore.name}`);
                $navigationStore.dropDownPage = $navigationStore.name;
            }
        } else if (level === 2) {
            $navigationStore.level = 2;
            $navigationStore.name = 'basic';
            goto('/level2/basic');
            $navigationStore.dropDownPage = 'basic';
        } else if ($navigationStore.level !== 0) {
            $navigationStore.level = 0;
            $navigationStore.name = '';
            gotoHomePageWithRedirect();
            $navigationStore.dropDownPage = '';
        }
    }
    
    async function programButton() {
        if (uiDisabled) return; // Prevent program button when UI is disabled
        
        $navigationStore.lastPressedButton = 'program';
        checkDirty(async () => {
            if ($navigationStore.level === 2) {
                $keyboardStore.hidden = true;
                await checkPassword('', 'leveldown', (value) => wait = value, (value) => error = value, async (_) => {});
                navigateLevel(1);
            } else {
                if ($keysStore.hasLevel1Password || $navigationStore.level === 1) {
                    $keyboardStore.keyboardType = KeyboardTypes.Alpha;
                    $keyboardStore.label = 'Password';
                    $keyboardStore.start = '';
                    $keyboardStore.resultReady = async (data: string) => { let user = data.split(':'); await checkPassword(user.length > 1 ? user[0] : '', user.length > 1 ? user[1] : user[0], (value) => wait = value, (value) => error = value, (level) => navigateLevel(level)) };
                    $keyboardStore.inputType = 'loginPassword';
                    $keyboardStore.hidden = false;
                    $keyboardStore = $keyboardStore;
                } else {
                    await checkPassword('DEFAULT', '', (value) => wait = value, (value) => error = value, (level) => navigateLevel(level));
                }
            }
        });
    }

    function gotoHistory() {
        if (uiDisabled) return; // Prevent navigation when UI is disabled
        
        $navigationStore.lastPressedButton = 'history';
        checkDirty(() => {
            error = false;
            goto('/history');
            $navigationStore.name = 'history';
            $navigationStore.dropDownPage = 'history';
        });
    }

    function fullscreen() {
        if (uiDisabled) return; // Prevent fullscreen when UI is disabled
        
        $navigationStore.lastPressedButton = 'fullscreen';
        $navigationStore.hasUserPressedFullScreen = true;
        
        if (typeof window !== 'undefined' && screenfull.isEnabled) {
            screenfull.toggle();
        }
    }
</script>

<div class="bg-gray-300 bg-opacity-50 py-2 px-4 rounded-b-3xl w-full flex gap-2 relative" bind:this={div}>
    <Button id="home" class="flex-1 !px-2 md:!px-4 {$navigationStore.lastPressedButton === 'home' ? '!ring-2 !ring-white' : ''}" size="xl" disabled={uiDisabled} on:click={() => gotoHome()}>{homeText}</Button>
    <Button id="program" class="flex-1 !px-2 md:!px-4 {error ? '!variant-ghost-error' : ''} {$navigationStore.lastPressedButton === 'program' ? '!ring-2 !ring-white' : ''}" size="xl" disabled={uiDisabled} on:click={programButton}>{programText}</Button>
    <Button class="flex-1 !px-2 md:!px-4 {$navigationStore.lastPressedButton === 'back' ? '!ring-2 !ring-white' : ''}" size="xl" on:click={() => navigate(DirectionEnum.PREV)} disabled={showPrevious || uiDisabled}>{$t('global.back')}</Button>
    <Button class="flex-1 !px-2 md:!px-4 {$navigationStore.lastPressedButton === 'next' ? '!ring-2 !ring-white' : ''}" size="xl" on:click={() => navigate(DirectionEnum.NEXT)} disabled={showNext || uiDisabled}>{$t('global.next')}</Button>
    <Button class="flex-1 !px-2 md:!px-4 {$navigationStore.lastPressedButton === 'history' ? '!ring-2 !ring-white' : ''}" size="xl" disabled={uiDisabled} on:click={gotoHistory}>{$t('global.history')}</Button>
    {#if !$navigationStore.redirect}
        <Button class="flex-1 !px-2 md:!px-4 {$navigationStore.lastPressedButton === 'refresh' ? '!ring-2 !ring-white' : ''}" size="xl" disabled={uiDisabled} on:click={refresh}>{$t('global.refresh')}</Button>
    {/if}
    {#if $navigationStore.showFullscreen}
        <Button id="fullscreen" class="flex-1 !px-2 md:!px-4 {$navigationStore.lastPressedButton === 'fullscreen' ? '!ring-2 !ring-white' : ''}" size="xl" disabled={uiDisabled} on:click={fullscreen}>{$t('global.full-screen')}</Button>
    {/if}
</div>

{#if wait}
    <Wait show={wait} />
{/if}
