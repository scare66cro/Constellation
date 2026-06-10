<script lang="ts">
	import { safeJsonParse, getHttpUrl } from "$lib/business/util";
  import Button from "$lib/ui/Button.svelte";
	import { cloneDeep, isEqual } from "lodash-es";
	import { createEventDispatcher, onDestroy } from "svelte";
  import { t } from "svelte-i18n";
  import { keyboardStore } from "$lib/store";
  import { onMount } from "svelte";
  import { beforeNavigate } from "$app/navigation";

  export let edit = false;
  export let wait = true;
  export let data: any;
  /**
   * Legacy `/iot/${route}` POST target. Optional — pages that supply an
   * `onSave` (the proto-direct path) don't need it. Kept for the few
   * remaining pages whose save still goes through `legacyShim` / a
   * registered `apiRoutes.ts` POST handler.
   */
  export let route: string = '';
  export let original: any;
  export let validation: Record<string, string> = {};
  export let reset = false;
  export let autoSave = false;
  /**
   * Phase 0.4+ hook — when provided, SaveButton calls `onSave(data)` in
   * place of POSTing to `/iot/${route}`. Use with `writeProto(TAG, msg)`
   * to cut a page's write path over to the typed-proto transport while
   * preserving SaveButton's autoSave / dirty-tracking / wait UX.
   *
   * Resolve to indicate success; throw to indicate error. The function
   * is responsible for any UI-space → proto-message shape conversion.
   * Field-level validation errors (the `Validation` response shape from
   * legacyShim) are NOT surfaced through this path — throw a plain Error
   * with a user-readable message instead.
   */
  export let onSave: ((data: any) => Promise<void>) | undefined = undefined;

  const dispatch = createEventDispatcher();

  let error = false;
  let isDisabledDueToKeyboard = false;
  let keyboardHideTimeout: NodeJS.Timeout;
  let savedTimeout: ReturnType<typeof setTimeout> | undefined;
  let autoSaveStatus: 'idle' | 'saving' | 'saved' | 'error' = 'idle';

  // Auto-save policy (rev May 2026): instead of debouncing a save
  // after every keystroke (which felt "annoying" — the spinner kept
  // flashing while the operator was still mid-edit), we now defer the
  // save until the page is navigated away from. The dirty data sits
  // in `data` while the operator works; the moment the route changes
  // (back arrow, home button, swipe-nav, sidebar link, …) SvelteKit
  // fires `beforeNavigate` and we flush. If the operator wants to
  // save explicitly without leaving the page they can still tap the
  // visible Save button (rendered when dirty + edit mode + no
  // autoSave OR when the proto-write callback exists).
  //
  // Edge cases handled:
  //   - Reverted edit (`data` matches `original`): no save fired.
  //   - Page closed via tab close / browser quit: SvelteKit's
  //     beforeNavigate also fires for `type==='leave'`; we kick the
  //     fetch synchronously. If the browser kills it before flush we
  //     accept the loss — there is no synchronous alternative for
  //     fetch with a JSON body.
  //   - Re-render thrash: `data` is rebuilt by the parent on every WS
  //     push, but `isEqual(data, original)` keeps us idle when
  //     nothing real changed.
  if (autoSave) {
    beforeNavigate(({ cancel }) => {
      if (!edit) return;
      if (!data || !original) return;
      if (isEqual(data, original)) return;
      // Fire the save and let the navigation proceed. We do NOT await
      // — blocking navigation would feel even more annoying than the
      // pre-rev autoSave. The fetch is queued at the network layer
      // before the route teardown begins; in practice it lands.
      void save();
    });
  }

  onDestroy(() => {
    if (savedTimeout) clearTimeout(savedTimeout);
  });

  // Watch for keyboard visibility changes
  $: {
    if (!$keyboardStore.hidden) {
      // Keyboard is visible - disable immediately
      isDisabledDueToKeyboard = true;
      // Clear any existing timeout
      if (keyboardHideTimeout) {
        clearTimeout(keyboardHideTimeout);
      }
    } else if (isDisabledDueToKeyboard) {
      // Keyboard just became hidden - keep disabled for a short period
      keyboardHideTimeout = setTimeout(() => {
        isDisabledDueToKeyboard = false;
      }, 300); // 300ms delay to prevent click propagation
    }
  }

  // Clean up timeout on component destroy
  onMount(() => {
    return () => {
      if (keyboardHideTimeout) {
        clearTimeout(keyboardHideTimeout);
      }
    };
  });

  $: if (reset) {
    reset = false;
    error = false;
    Object.keys(validation).forEach((key) => {
      validation[key] = '';
    });
  };

  // Exported so a parent can flush this save imperatively (e.g. a modal
  // that saves on close-without-cancel, where there's no route navigation
  // for the autoSave `beforeNavigate` hook to fire on). Reuses the full
  // validation / wait / original-update path — no logic duplicated.
  export async function save() {
    wait = true;
    error = false;
    if (autoSave) autoSaveStatus = 'saving';
    Object.keys(validation).forEach((key) => validation[key] = '');
    let wasSuccess = false;
    
    // For TCP/IP route, check if IP or port is changing
    let needsRedirect = false;
    let newUrl = '';
    if (route === 'tcpip' && data && original) {
      // Extract current IP (remove port if present)
      const currentIpWithPort = original.LocalIpAdd?.[0] || location.hostname;
      const currentIp = currentIpWithPort.includes(':') ? currentIpWithPort.split(':')[0] : currentIpWithPort;
      const currentPort = original.HttpPort?.[0] || location.port || '80';
      
      // Extract new IP and port
      const newIp = data.LocalIpAdd?.[0] || currentIp;
      const newPort = data.HttpPort?.[0] || currentPort;
      
      if (newIp !== currentIp || newPort !== currentPort) {
        needsRedirect = true;
        newUrl = `http://${newIp}${newPort !== '80' ? ':' + newPort : ''}`;
      }
    }
    
    // ── Phase 0.4+ proto write path ─────────────────────────────
    // When an onSave callback is supplied (e.g. a writeProto(…) wrapper),
    // skip the legacy /iot/${route} POST entirely. All proto pages land
    // here as they migrate off the CSV translation layer.
    if (onSave) {
      try {
        await onSave(data);
        original = cloneDeep(data);
        wasSuccess = true;
      } catch (e: any) {
        console.error(`[SaveButton:${route}] proto save error:`, e?.message ?? e);
        error = true;
      } finally {
        wait = false;
        if (autoSave) {
          autoSaveStatus = wasSuccess ? 'saved' : 'error';
          if (wasSuccess) {
            if (savedTimeout) clearTimeout(savedTimeout);
            savedTimeout = setTimeout(() => { autoSaveStatus = 'idle'; }, 2000);
          }
        }
        dispatch('complete', { success: wasSuccess });
      }
      return;
    }

    const result = await fetch(getHttpUrl(`/iot/${route}`), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      credentials: 'include',
      body: JSON.stringify(data),
    });

    try {
      const json = await safeJsonParse(result);
      // Handle 401 via global handler (returns true if it performed a logout/navigation)
      if (typeof window !== 'undefined' && window.handleUnauthorized) {
        if (await window.handleUnauthorized(json)) {
          error = true;
          wasSuccess = false;
          return; // wait cleared in finally
        }
      }

      if (json.status === 500 || json.status === 403 || json.data?.Type === 'Validation') {
        // 403 should also surface as error and stop spinner
        error = true;
        if (json.data?.Type === 'Validation') {
          const errs = json.data?.errors || {} as Record<string, string[]>;

          // Special handling for IO Config aggregated errors
          if (route === 'ioconfig') {
            const duplicates = errs['Duplicates'];
            if (Array.isArray(duplicates)) {
              duplicates.forEach((cell) => {
                if (typeof cell === 'string' && (cell.startsWith('o') || cell.startsWith('i'))) {
                  validation[cell] = 'Duplicate selection';
                }
              });
            }
            const badOutputs = errs['BadOutputs'];
            if (Array.isArray(badOutputs)) {
              badOutputs.forEach((cell) => {
                if (typeof cell === 'string' && cell.startsWith('o')) {
                  // Mark the specific output cell invalid
                  validation[cell] = 'Invalid selection';
                }
              });
            }
            const badInputs = errs['BadInputs'];
            if (Array.isArray(badInputs)) {
              badInputs.forEach((cell) => {
                if (typeof cell === 'string' && cell.startsWith('i')) {
                  // Mark the specific input cell invalid
                  validation[cell] = 'Invalid selection';
                }
              });
            }
          }

          // Generic field errors (exclude aggregated keys already handled for ioconfig)
          Object.keys(errs).forEach((key) => {
            if (route === 'ioconfig' && (key === 'BadOutputs' || key === 'BadInputs' || key === 'Duplicates')) {
              return;
            }
            const values = errs[key];
            if (!Array.isArray(values) || values.length === 0) return;
            const first = values[0];
            if (typeof first === 'string') {
              validation[key] = first.indexOf(':') > -1 ? first.split(':').slice(1).join(' ') : first;
            }
          });
        }
      } else if (json.status === 401) {
        error = true; // (Should already be handled above but keep for safety)
      } else {
        original = cloneDeep(data);
        wasSuccess = true;
        
        // Handle TCP/IP redirect after successful save
        if (needsRedirect && newUrl) {
          // Give the backend a moment to restart before redirecting
          setTimeout(() => {
            window.location.href = `${newUrl}?-Redirect`;
          }, 3000);
        }
      }
    } catch (e) {
      console.error('Error parsing response:', e);
      error = true;
    } finally {
      // Always release wait unless a global navigation already occurred
      wait = false;
      if (autoSave) {
        autoSaveStatus = wasSuccess ? 'saved' : 'error';
        if (wasSuccess) {
          if (savedTimeout) clearTimeout(savedTimeout);
          savedTimeout = setTimeout(() => { autoSaveStatus = 'idle'; }, 2000);
        }
      }
      dispatch('complete', { success: wasSuccess });
    }
  }
</script>

{#if autoSave}
  <!-- AutoSave status indicator. Icon-only by user preference — a
       spinning blue circle while the save is pending/in-flight, a green
       check after success, a red retry pill on failure. The fixed
       `min-h-12` wrapper reserves the layout slot so the page doesn't
       shift when status appears/disappears. -->
  <div class="mx-auto text-center mt-1 min-h-12 flex items-center justify-center" aria-live="polite">
    {#if autoSaveStatus === 'saving'}
      <span class="inline-block w-8 h-8 rounded-full border-4 border-blue-200 border-t-blue-700 animate-spin" title={$t('global.save')} aria-label={$t('global.save')}></span>
    {:else if autoSaveStatus === 'saved'}
      <span class="text-3xl font-bold text-green-700" title={$t('global.saved')} aria-label={$t('global.saved')}>✓</span>
    {:else if autoSaveStatus === 'error'}
      <button class="text-size-large font-semibold text-red-700 underline cursor-pointer px-4 py-2 rounded bg-red-100" on:click={save}>
        {$t('global.retry')}
      </button>
    {/if}
  </div>
{:else if edit}
  <Button 
    id={`save-${route}`} 
    class="mx-auto !mb-0 disabled:pointer-events-none disabled:opacity-10 {$$restProps.class} {error ? '!variant-ghost-error' : ''}" 
    size="xl" 
    disabled={isDisabledDueToKeyboard}
    on:click={save}
  >
    {!error ? $t('global.save') : $t('global.retry')}
  </Button>
{/if}