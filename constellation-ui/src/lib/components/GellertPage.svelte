<script lang="ts">
  import { homePageStore, keysStore, navigationStore, upgradeStore, pageTranslationsStore } from "$lib/store";
  import { ProgressRadial } from "@skeletonlabs/skeleton";
	import { onDestroy, onMount, type ComponentType } from "svelte";
	import { goto, invalidate } from "$app/navigation";
  import { page } from "$app/stores";
	import Wait from "$lib/ui/Wait.svelte";
  import { isLoading } from "svelte-i18n";  import { getFilteredPageList } from "$lib/business/paging";
  import { t } from "svelte-i18n";
  import { swipe } from "$lib/utils/swipeGesture";
  import { FooterNavigationAdapter } from "$lib/utils/footerNavigationAdapter";
  import { getHttpUrl } from "$lib/business/util";

  export let ready = true;
  export let title:string;
  export let level: number;
  export let name: string; // navigation name
  export let wait = false;
  export let waitMessage: string | undefined = undefined;
  export let action: any = undefined;
  export let left = "";
  export let retryCallback: (() => void) | undefined = undefined;
  let errorState = false;
  let retryCount = 0;
  const MAX_RETRIES = 3;
  let containerElement: HTMLElement;
  let timeoutHandle: ReturnType<typeof setTimeout> | undefined;
  // Page indicator variables
  $: currentPage = 1;
  $: totalPages = 1;
  $: showPageContent = ready && !$isLoading && !errorState;
  
  // Function to detect if current page contains charts (exclude mouse swipe on these pages)
  function isChartPage(): boolean {
    const route = $page.route.id;
    const pathname = $page.url.pathname;
    
    // Check if current route/page contains charts
    return (
      route?.includes('/graph') || 
      route?.includes('/userlog') ||
      pathname.includes('/graph') ||
      pathname.includes('/userlog') ||
      name === 'graph' ||
      name === 'userlog'
    );
  }
  
  import { isRebooting } from '$lib/business/util';
  $: if ($navigationStore.invalidate) {
    // Suppress data invalidation while reboot pause active
    if (!isRebooting()) {
      $navigationStore.invalidate = false;
      if ($navigationStore.data) invalidate($navigationStore.data);
    }
  }// Calculate page numbers for level0, level1 and level2 routes
  $: if ((level === 0 || level === 1 || level === 2) && $pageTranslationsStore) {
    const filteredPages = getFilteredPageList(level, $pageTranslationsStore);
    let navigablePages = filteredPages.filter(page => page.navigation);

    // For level 1, exclude system monitor page as it's not shown in navigation
    if (level === 1) {
      // Filter out system monitor related pages for level 1
      navigablePages = navigablePages.filter(page => 
        page.text !== $t('system-monitor.system-monitor') && 
        page.value !== ''
      );
    }
    totalPages = navigablePages.length;

    // Find current page index - for level 0 system monitor, treat empty name or 'version' as first page
    let searchName = name;
    if (level === 0 && name === '') {
      // System monitor page is typically the first page
      currentPage = 1;
    } else {
      const currentPageIndex = navigablePages.findIndex(page => page.value === searchName);
      currentPage = currentPageIndex >= 0 ? currentPageIndex + 1 : 1;
    }
  }

  function handleRetry() {
    if (retryCount < MAX_RETRIES && retryCallback) {
      retryCount++;
      errorState = false;
      wait = true;
      retryCallback();
    } else {
      // If we've exceeded max retries or have no retry callback, we'll just go back to home
      goToHome();
    }
  }
  async function goToHome() {
    wait = true;
    try {
      await fetch(getHttpUrl('/iot/logout'), {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
      });
    } catch (e) {
      console.error("Error during logout:", e);
    } finally {
      $navigationStore = { ...$navigationStore, level: 0, name: 'version', data: '', invalidate: false };
      $keysStore.accessLevel = 0;
      $navigationStore.isDirty = () => false;
      $navigationStore.dropDownPage = '';
      
      // Preserve redirect parameter when navigating to home page
      let targetUrl = $homePageStore.page;
      if ($navigationStore.redirect && targetUrl === '/') {
        targetUrl = '/?-Redirect';
      }
      goto(targetUrl);
      wait = false;
    }
  }  // Swipe navigation functions
  async function handleSwipeLeft() {
    // Swipe left = go to next page
    if (FooterNavigationAdapter.canNavigateNext(level, name)) {
      try {
        await FooterNavigationAdapter.navigateToNext(level, name);
      } catch (error) {
        console.error('Navigation error:', error);
      }
    }
  }

  async function handleSwipeRight() {
    // Swipe right = go to previous page
    if (FooterNavigationAdapter.canNavigatePrevious(level, name)) {
      try {
        await FooterNavigationAdapter.navigateToPrevious(level, name);
      } catch (error) {
        console.error('Navigation error:', error);
      }
    }
  }  // Browser history override for swipe navigation
  function preventDefaultNavigation(event: TouchEvent) {
    // Don't prevent default on chart pages since swipe is disabled there
    if (isChartPage()) {
      return;
    }
    
    // Check if the touch target is a form control or interactive component - don't prevent default for these
    const target = event.target as HTMLElement;
    const isFormControl = target && (
      target.tagName === 'SELECT' ||
      target.tagName === 'INPUT' ||
      target.tagName === 'TEXTAREA' ||
      target.tagName === 'BUTTON' ||
      target.closest('select') ||
      target.closest('input') ||
      target.closest('textarea') ||
      target.closest('button') ||
      target.closest('.date-time-field') ||
      target.closest('.date-time-picker') ||
      target.classList.contains('select') || // Svelte select components
      target.closest('.select') || // Svelte select containers
      // Skeleton Labs SlideToggle components
      target.classList.contains('slide-toggle') ||
      target.closest('.slide-toggle') ||
      target.closest('[data-testid*="slide-toggle"]') ||
      // Any element with role="switch" (common for toggle switches)
      target.getAttribute('role') === 'switch' ||
      target.closest('[role="switch"]') ||
      // Generic slider/range controls
      target.classList.contains('slider') ||
      target.closest('.slider') ||
      target.classList.contains('range') ||
      target.closest('.range') ||
      // Check for any parent with touch interaction classes
      target.closest('.touch-interactive') ||
      target.closest('[data-touch-interactive]')
    );
    
    if (isFormControl) {
      return;
    }
    
    // Only prevent default if we're at the edge of a swipeable page
    if (event.touches.length === 1) {
      const canSwipeNext = FooterNavigationAdapter.canNavigateNext(level, name);
      const canSwipePrev = FooterNavigationAdapter.canNavigatePrevious(level, name);
      
      if (canSwipeNext || canSwipePrev) {
        // Prevent browser back/forward gesture
        event.preventDefault();
      }
    }
  }

  onMount(async () => {
    if (($keysStore.accessLevel < level && name !== 'history')
     || ($keysStore.localRequired && !$keysStore.localAllowed && name !== '')) {
      wait = true;
      await fetch(getHttpUrl('/iot/logout'), {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
      });
      $navigationStore = { ...$navigationStore, level: 0, name: 'version', data: '', invalidate: false };
      $keysStore.accessLevel = 0;
      
      // Navigate to root while preserving redirect parameter
      let targetUrl = '/';
      if ($navigationStore.redirect) {
        targetUrl = '/?-Redirect';
      }
      goto(targetUrl);
      wait = false;
      return; // Don't set navigation store after redirect
    }
    if (name !== '') {
      timeoutHandle = setTimeout(async () => {
        // do not logout if we are upgrading
        if ($upgradeStore) return;
        goToHome();
        $navigationStore.lastPressedButton = null;
      }, 300000);
    }
    $navigationStore.name = name;
    $navigationStore.level = level;
  });

  onDestroy(() => {
    if (timeoutHandle) {
      clearTimeout(timeoutHandle);
      timeoutHandle = undefined;
    }
  });
</script>

{#if showPageContent}
  <!-- External swipe indicators -->
  <div class="swipe-wrapper">
    <div 
      class="flex flex-1 flex-col h-full swipe-container"
      bind:this={containerElement}
      role="region"
      aria-label="page content"
      use:swipe={{
        threshold: 50,
        restraint: 100,
        allowedTimeTouch: 300,  // Keep touch swipes responsive
        enableTouch: !isChartPage(), // Disable touch swipe on chart pages
        // Globally allow swipes to start from form controls, but our swipe handler
        // skips horizontally scrollable areas and sliders automatically.
        allowFromFormControls: true,
        onSwipeLeft: handleSwipeLeft,
        onSwipeRight: handleSwipeRight
      }}
      on:touchstart={preventDefaultNavigation}
    >
      <div class="w-full flex flex-col bg-gradient-to-b from-gray-300/50">
        <div class="flex flex-row">
          <div class="w-1/4 text-size-xl -mt-8 mb-4 ml-2 text-left">{left}</div>
          <div class="w-1/2 text-size-xl font-bold -mt-8 mb-4 text-center">{title}</div>
          <div class="w-1/4 -mt-8 mb-4 text-center mr-2 flex justify-end items-center gap-4">
            {#if (level === 0 && totalPages >= 1) || ((level === 1 || level === 2) && totalPages > 1)}
              <div class="page-indicator text-size-large font-medium">
                Page {currentPage} of {totalPages}
              </div>
            {/if}
            {#if action}<svelte:component this={action} on:click />{/if}
          </div>
        </div>
      </div>
      <slot />
    </div>
  </div>
{:else if errorState}
  <div class="flex flex-col h-full justify-center items-center">
    <div class="text-xl mb-4">There was an issue loading the data</div>
    <button class="px-4 py-2 bg-blue-500 text-white rounded" on:click={handleRetry}>
      {retryCount < MAX_RETRIES ? 'Retry' : 'Return to Home'}
    </button>
  </div>
{:else}
  {#if !wait}
    <div class="flex flex-col h-full justify-center items-center">
      <ProgressRadial />
    </div>
  {/if}
{/if}

{#if wait}
  <Wait show={wait} message={waitMessage} />
{/if}

<style>
  .page-indicator {
    white-space: nowrap;
  }

  .swipe-wrapper {
    position: relative;
    display: flex;
    flex: 1;
    flex-direction: column;
    height: 100%;
  }

  .swipe-container {
    position: relative;
    flex: 1;
    display: flex;
    flex-direction: column;
    height: 100%;
    /* Allow page content to scroll vertically when it exceeds the
       slot height (e.g. plenum L1 ramp-rate card sat behind the nav
       footer on smaller touchscreens). `min-height: 0` lets the
       flex child shrink so `overflow-y` activates instead of
       pushing the footer out of bounds. `touch-action` keeps
       horizontal swipe-nav while permitting vertical scroll. */
    min-height: 0;
    overflow-y: auto;
    touch-action: pan-x pan-y;
  }
</style>
