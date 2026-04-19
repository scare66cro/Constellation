<script lang="ts">
	import { safeJsonParse, getHttpUrl } from "$lib/business/util";
  import Button from "$lib/ui/Button.svelte";
	import { cloneDeep, isEqual } from "lodash-es";
	import { createEventDispatcher, onDestroy } from "svelte";
  import { t } from "svelte-i18n";
  import { keyboardStore } from "$lib/store";
  import { onMount } from "svelte";

  export let edit = false;
  export let wait = true;
  export let data: any;
  export let route: string;
  export let original: any;
  export let validation: Record<string, string> = {};
  export let reset = false;
  export let autoSave = false;

  const dispatch = createEventDispatcher();

  let error = false;
  let isDisabledDueToKeyboard = false;
  let keyboardHideTimeout: NodeJS.Timeout;
  let autoSaveTimer: ReturnType<typeof setTimeout> | undefined;
  let autoSaveStatus: 'idle' | 'saving' | 'saved' | 'error' = 'idle';
  let savedTimeout: ReturnType<typeof setTimeout> | undefined;

  // Auto-save: watch for data changes and trigger save after debounce
  // Only triggers when keyboard is hidden (user finished entering values)
  $: if (autoSave && edit && data && original && $keyboardStore.hidden) {
    scheduleAutoSave(data, original);
  }

  function scheduleAutoSave(current: any, orig: any) {
    if (isEqual(current, orig)) return;
    if (autoSaveTimer) clearTimeout(autoSaveTimer);
    autoSaveStatus = 'idle';
    autoSaveTimer = setTimeout(() => {
      autoSaveTimer = undefined;
      save();
    }, 1200);
  }

  onDestroy(() => {
    if (autoSaveTimer) clearTimeout(autoSaveTimer);
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

  async function save() {
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
  <div class="mx-auto text-center h-6 mt-1">
    {#if autoSaveStatus === 'saving'}
      <span class="text-sm text-gray-500 animate-pulse">{$t('global.save')}...</span>
    {:else if autoSaveStatus === 'saved'}
      <span class="text-sm text-green-600">✓</span>
    {:else if autoSaveStatus === 'error'}
      <button class="text-sm text-red-600 underline cursor-pointer" on:click={save}>{$t('global.retry')}</button>
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