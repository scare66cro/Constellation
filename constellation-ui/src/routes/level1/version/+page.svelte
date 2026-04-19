<script lang="ts">
	import { onMount, onDestroy } from "svelte";
  import { writable } from "svelte/store";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Button from "$lib/ui/Button.svelte";
  import uiversion from "$lib/business/uiversion";
  import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import { keysStore, navigationStore, upgradeStore, statusStore, rebootStore, createDefaultRebootState } from "$lib/store";
  import type { UpgradeStatus } from "$lib/business/wsClient";
  import { getHttpUrl, startRebootPause, fetchWithTimeout } from "$lib/business/util";
	import List from "$lib/ui/List.svelte";
  import { goto } from "$app/navigation";
  import { t } from "svelte-i18n";
  import { safeJsonParse } from "$lib/business/util";
  import WsClient from "$lib/business/wsClient";
  import type { PageType } from "$lib/business/PageType";

  // Provide a safe default; backend may temporarily fail returning partial data (avoids SSR 500)
  export let data: { controller?: string[], webserver?: string, ui?: string, displays?: string[], status?: number } = { controller: [], webserver: '', ui: '', displays: [], status: 0 };

  // Local upgrade status store (distinct from global statusStore which carries controller HTTP heartbeat)
  const upgradeStatusStore = writable<UpgradeStatus>({ UpgradeStatus: '', UpgradingSoftware: false, isEmpty: true });

  let client: WsClient | undefined;

  let title = $t('level1.version.system-software-versions');
  // Track reboot/completion phases to avoid regressions on transient 503s
  let upgradePhase: 'idle' | 'upgrading' | 'rebooting' = 'idle';
  let seenControllerReady = false;          // Have we observed a 200 heartbeat post-upgrade?
  let completionTriggered = false;          // Guard to ensure completion runs once
  let rebooting: NodeJS.Timeout | undefined = undefined;
  let messageInterval: NodeJS.Timeout | undefined = undefined;
  let started = false;    // tells when we have started receiving upgrade information from server
  let controllerSelected = '';
  let displaySelected = '';
  let version: { controller: string[], webserver: string, ui: string, displays: string[] } = { controller: [], webserver: '', ui: '', displays: [] };

  let boards: { name: string, version: string }[] = [];
  let displays: { ip: string, name: string, mac: string, version: string }[] = [];
  let controllerOptions: PageType[] = [];
  let displayOptions: PageType[] = [];
  let killTimeout: NodeJS.Timeout | undefined;
  const EMPTY_UPGRADE_STATUS: UpgradeStatus = { UpgradeStatus: '', UpgradingSoftware: false, isEmpty: true };
  const REBOOT_COMPLETION_TIMEOUT = 240000; // 4 minutes safety net
  const POST_REBOOT_SPINNER_DELAY = 5000; // Keep spinner visible while PI finishes booting
  let upgradeTimeoutError = false;  // Track timeout error state
  let rebootTime = 0;
  let isUpgradeInfoLoading = false; // Guard to prevent concurrent upgradeInfo calls
  let isCleaningUp = false; // Guard to prevent reactive triggers during cleanup
  let hasInitializedUpgradeInfo = false; // Track if we've called upgradeInfo at least once
  
  // Upload file state
  let uploadFileInput: HTMLInputElement;
  let uploadingFile = false;
  let uploadError = '';

  $: controllerList = [] as { ip: string, port: string, name: string, version: string }[];
  $: controllerOptions = controllerList.map((c) => ({
    value: c.ip,
    text: `${(!c.name || c.name === '') ? 'Agristar Panel' : c.name} (${c.ip}) - ${c.version}`
  }));
  $: displayList = [] as { ip: string, software: string, name: string, mac: string, version: string }[];
  $: displayOptions = displayList
    .filter((d) => d.name !== undefined && d.software !== undefined && d.version !== undefined)
    .map((d) => {
      // For USB/uploaded files (where name is a filename pattern), use the filename as value
      const isUsbFile = d.name.match(/^AS2.*\.rpi$/i);
      return {
        value: isUsbFile ? d.name : d.ip,  // Use filename for USB/uploaded files, IP for network displays
        text: isUsbFile
          ? `${d.name} (v${d.software})` 
          : `${(!d.name || d.name === '') ? 'Agristar Panel' : d.name} - ${d.software} - ${d.version}`
      };
    });
  
  // Centralized state management for wait and ready
  let ready = false;
  let wait = false;
  let waitMessage = '';
  
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  
  // Single reactive statement to call upgradeInfo when entering edit mode
  $: if (edit && !isCleaningUp && !hasInitializedUpgradeInfo) {
    hasInitializedUpgradeInfo = true;
    upgradeInfo();
  }
  
  // Handle invalidation separately
  $: if ($navigationStore.invalidate && edit && $navigationStore.name === 'version' && !isCleaningUp && hasInitializedUpgradeInfo) {
    upgradeInfo();
  }
  
  $: getVersion(data);

  function coerceBoolean(value: unknown): boolean {
    if (typeof value === 'boolean') return value;
    if (typeof value === 'string') {
      const normalized = value.trim().toLowerCase();
      return normalized === 'true' || normalized === '1' || normalized === 'yes';
    }
    if (typeof value === 'number') return value !== 0;
    return false;
  }

  function asUpgradeStatus(payload: unknown): UpgradeStatus {
    if (!payload || typeof payload !== 'object') {
      return { ...EMPTY_UPGRADE_STATUS };
    }
    const raw = payload as Record<string, unknown>;

    // Handle synthetic browser error object from WsClient polling fallback
    // This occurs during reboot when server returns 503 or other non-JSON responses
    if (raw.browserError === true) {
      return { UpgradeStatus: 'Rebooting...', UpgradingSoftware: true, isEmpty: false };
    }
    
    const isEmpty = Object.keys(raw).length === 0;
    const status: UpgradeStatus = {
      UpgradeStatus: typeof raw.UpgradeStatus === 'string' ? raw.UpgradeStatus : EMPTY_UPGRADE_STATUS.UpgradeStatus,
      UpgradingSoftware: coerceBoolean(raw.UpgradingSoftware),
      isEmpty,
    };
    return status;
  }

  function parseStatusCode(value: unknown): number | undefined {
    if (typeof value === 'number') {
      return Number.isNaN(value) ? undefined : value;
    }
    if (typeof value === 'string') {
      const parsed = parseInt(value, 10);
      return Number.isNaN(parsed) ? undefined : parsed;
    }
    return undefined;
  }

  async function getUpgrade(response: Response) {
    try {
      // safeJsonParse manages its own wait state internally, but we ensure wait is false on completion
      const result = await safeJsonParse(response, (isWaiting) => {
        wait = isWaiting;
      });

      // Parse ControllerList and DisplayList (now arrays of objects)
      controllerList = Array.isArray(result.ControllerList) ? result.ControllerList as { ip: string, port: string, name: string, version: string }[] : [];
      displayList = Array.isArray(result.DisplayList) ? result.DisplayList as { ip: string, software: string, name: string, mac: string, version: string }[] : [];
      
      // Auto-select first items if nothing is selected
      if (!controllerSelected && controllerList.length > 0) {
        controllerSelected = controllerList[0].ip;
      }
      if (!displaySelected && displayList.length > 0) {
        // For USB/uploaded files (filename pattern), use the name; for regular displays, use the IP
        const firstDisplay = displayList[0];
        displaySelected = firstDisplay.name.match(/^AS2.*\.rpi$/i) ? firstDisplay.name : firstDisplay.ip;
      }

      // Ensure wait is cleared after successful processing
      wait = false;
    } catch (error) {
      console.error('Error processing upgrade data:', error);
      wait = false; // Always clear wait on error
    }
  }

  async function upgradeInfo() {
    // Prevent concurrent calls
    if (isUpgradeInfoLoading || isCleaningUp) {
      return;
    }
    
    isUpgradeInfoLoading = true;
    wait = true;
    
    try {
      const result = await fetch(getHttpUrl('/iot/upgrade'));
      
      // Check if fetch was successful
      if (!result.ok) {
        console.error(`HTTP error: ${result.status} ${result.statusText}`);
        throw new Error(`HTTP error: ${result.status}`);
      }
      
      await getUpgrade(result);
    } catch (error) {
      console.error('Error fetching upgrade info:', error);
      
      // Only retry once after 10 seconds if not cleaning up
      if (!isCleaningUp) {
        setTimeout(() => {
          if (!isCleaningUp) {
            fetch(getHttpUrl(`/iot/upgrade`))
              .then((result) => {
                if (!result.ok) {
                  console.error(`Retry HTTP error: ${result.status} ${result.statusText}`);
                  throw new Error(`HTTP error: ${result.status}`);
                }
                return getUpgrade(result);
              })
              .catch((retryError) => {
                console.error('Error in retry:', retryError);
                // Ensure wait is cleared even on retry failure
                wait = false;
              });
          } else {
            // If cleaning up during timeout, ensure wait is cleared
            wait = false;
          }
        }, 10000);
      } else {
        // If cleaning up, clear wait immediately
        wait = false;
      }
    } finally {
      // Always clear the loading flag
      isUpgradeInfoLoading = false;
      // Ensure wait is cleared if no retry is scheduled
      if (isCleaningUp) {
        wait = false;
      }
    }
  }

  async function getVersion(raw: { controller?: string[], webserver?: string, ui?: string, displays?: string[] }) {
    // Reset arrays to avoid duplication on reactive re-runs
    boards = [];
    displays = [];
    const safeController = Array.isArray(raw?.controller) ? raw.controller : [];
    const safeDisplays = Array.isArray(raw?.displays) ? raw.displays : [];
    version = {
      controller: safeController.slice(),
      webserver: raw?.webserver || '',
      ui: uiversion,
      displays: safeDisplays.slice(),
    };
    // Controller board pairs: [count?, name, version, name, version, ...] starting at index 1 per original logic
    for (let i = 1; i + 1 < version.controller.length; i += 2) {
      boards.push({ name: version.controller[i], version: version.controller[i + 1] });
    }
    // Display entries in groups of 5: ip, software?, name, mac, version
    for (let i = 0; i + 4 < version.displays.length; i += 5) {
      displays.push({
        ip: version.displays[i] || '',
        name: version.displays[i + 2] || '',
        mac: version.displays[i + 3] || '',
        version: version.displays[i + 4] || '',
      });
    }
  }

  onMount(async () => {
    $navigationStore.data = getHttpUrl(`/iot/version`);
    ready = true;
  });

  onDestroy(() => {
    endUpgrade();
  });

  function endUpgrade(reboot = 30000, done: () => void | Promise<void> = () => {}) {
    isCleaningUp = true; // Set cleanup flag immediately
    
    // Clear reboot overlay and allow polling again
    rebootStore.set(createDefaultRebootState());

    setTimeout(async () => {
      if (client) {
        client.close();
        client = undefined;
      }
      $navigationStore.inLoad = false;
      $navigationStore.setOptions = true;
      if (killTimeout) {
        clearTimeout(killTimeout);
        killTimeout = undefined;
      }
      if (rebooting) {
        clearInterval(rebooting);
        rebooting = undefined;
      }
      if (messageInterval) {
        clearInterval(messageInterval);
        messageInterval = undefined;
      }
      // Reset all state flags to prevent infinite loops
      await done();
      $upgradeStore = false; // Allow alarms again
      started = false;
      wait = false; // Always clear wait state
      waitMessage = '';
      ready = true;
      upgradePhase = 'idle';
      seenControllerReady = false;
      completionTriggered = false;
      rebootTime = 0;
      isUpgradeInfoLoading = false;
      upgradeTimeoutError = false;
      hasInitializedUpgradeInfo = false; // Reset initialization flag
      // Reset cleanup flag after a brief delay to allow navigation to complete
      setTimeout(() => {
        isCleaningUp = false;
      }, 1000);
    }, reboot);
  }

  function save() {
    try {
      $upgradeStore = true;
      upgradeTimeoutError = false;  // Reset timeout error state
      wait = true; // Enable wait spinner for entire upgrade process
      upgradePhase = 'idle'; // Set phase to idle at the start of the upgrade
      started = false; // Reset started flag
      $upgradeStatusStore.UpgradeStatus = '';
      $upgradeStatusStore.UpgradingSoftware = false;
      
      // Check if displaySelected is an uploaded file (starts with AS2 and ends with .rpi)
      const isUploadedFile = displaySelected && displaySelected.match(/^AS2.*\.rpi$/i);
      
      fetch(
        getHttpUrl('/iot/upgrade'), {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json'
          },
          body: JSON.stringify({ 
            selDisplay: displaySelected, 
            selController: controllerSelected,
            uploadedFile: isUploadedFile ? displaySelected : undefined
          }),
        },
      )
        .then(async (response) => {
          // Don't throw error - let the upgrade process continue via WebSocket
          // The backend may return non-200 status but still initiate the upgrade
          if (!response.ok) {
            console.warn(`Upgrade POST returned ${response.status}, but continuing to monitor via WebSocket`);
          }
        })
        .catch((error) => {
          // Only abort on network errors, not HTTP status codes
          console.error('Network error during upgrade POST:', error);
          wait = false; // Clear wait on POST failure
          endUpgrade();
        });
    } catch (error) {
      console.error('Failed to initiate upgrade: ', error);
      wait = false; // Clear wait on exception
      endUpgrade();
      return;
    }
    
    killTimeout = setTimeout(() => {
      if (!started && $upgradeStore) {
        killTimeout = undefined;
        upgradeTimeoutError = true;  // Set timeout error state
        wait = false; // Clear wait on timeout
        console.error('Upgrade failed after 60 seconds');
        endUpgrade(30000, () => {
          // Reset upgrade state and navigate back to level 1 version page
          $navigationStore = { ...$navigationStore, level: 1, name: 'version', invalidate: true };
          goto('/level1/version');
        });
      }
    }, 60000); // 60 seconds

    client = new WsClient(getHttpUrl('/iot/ws'), 'upgrade-data', (data: any) => {
      // Skip processing if we're cleaning up
      if (isCleaningUp) {
        return;
      }
      
      // Handle browserError (503/network errors during reboot)
      if ($upgradeStore && data && typeof data === 'object' && data.browserError) {
        // If already rebooting, just update status and keep waiting
        if (rebooting !== undefined) {
          $upgradeStatusStore.UpgradeStatus = 'Rebooting...';
          return;
        }
        // If we haven't started yet, this is a legitimate error
        if (!started) {
          $upgradeStatusStore.UpgradeStatus = 'ARM:Failed';
          wait = false; // Only clear wait on legitimate failure
          endUpgrade(30000, () => {
            $navigationStore = { ...$navigationStore, level: 1, name: 'version', invalidate: true };
            goto('/level1/version');
          });
          return;
        }
        // Started upgrade and hit browser error - transition to reboot phase
        upgradePhase = 'rebooting';
        setupRebootingInterval();
        return;
      }

      const normalizedStatus = asUpgradeStatus(data);

      if (normalizedStatus.isEmpty && !$upgradeStore) {
        return;
      }

      if (normalizedStatus.UpgradeStatus === 'ARM:Failed' ||
        (normalizedStatus.isEmpty && upgradePhase === 'upgrading')
      ) {
        upgradeStatusStore.set({ ...normalizedStatus });
        wait = false; // Clear wait on legitimate failure
        endUpgrade(30000,() => {
          $navigationStore = { ...$navigationStore, level: 1, name: 'version', invalidate: true };
          goto('/level1/version');
        });
        console.error('Upgrade failed');
        return;
      }

      // Detect active upgrading state
      if (normalizedStatus.UpgradingSoftware) {
        started = true;
        if (upgradePhase === 'idle') upgradePhase = 'upgrading';
        if (killTimeout) {
          clearTimeout(killTimeout);
          killTimeout = undefined;
        }
      }

      // Add safety: if we've been upgrading and receive complete status, trigger reboot
      if (started && upgradePhase === 'upgrading' && !normalizedStatus.UpgradingSoftware) {
        upgradePhase = 'rebooting';
        setupRebootingInterval();
      }

      upgradeStatusStore.set({ ...normalizedStatus });
    });
    client.connect();
  }

  function setupRebootingInterval() {
    $upgradeStatusStore.UpgradeStatus = 'Rebooting...';
    rebootTime = 0;
    wait = true; // Ensure wait spinner stays on during reboot
    waitMessage = `Rebooting... ${rebootTime / 1000}s`;
    
    // Stop global pollers and show reboot overlay to prevent connection refused errors
    startRebootPause(REBOOT_COMPLETION_TIMEOUT);

    // Update message every second
    messageInterval = setInterval(() => {
      rebootTime += 1000;
      waitMessage = `Rebooting... ${Math.floor(rebootTime / 1000)}s`;
    }, 1000);

    rebooting = setInterval(waitForReboot, 15000);
  }

  async function waitForReboot() {
    if (completionTriggered || isCleaningUp) {
      return; // Don't clear wait - let completion handle it
    }
    
    // rebootTime is updated by messageInterval
    // Don't use statusStore as pollers are paused. Check manually.
    const rebootDeadlineReached = rebootTime > REBOOT_COMPLETION_TIMEOUT;
    
    try {
      // Use a short timeout to check if controller is back
      const response = await fetchWithTimeout(getHttpUrl('/iot/version'), { method: 'HEAD' }, 5000);
      if (response.ok) {
        seenControllerReady = true;
      }
    } catch (e) {
      // Still offline or connection refused - expected during reboot
    }

    // If we've crossed the safety window but still see 503/connection failures, keep waiting instead of reloading.
    // This prevents the kiosk from landing on a browser 503 page. We simply keep polling until the server is back.
    if (rebootDeadlineReached && !seenControllerReady) {
      waitMessage = 'Still waiting for server to finish rebooting...';
      return;
    }

    if (seenControllerReady) {
      completionTriggered = true; // Guard re-entry
      
      // Clear the intervals immediately to prevent further calls
      if (rebooting) {
        clearInterval(rebooting);
        rebooting = undefined;
      }
      if (messageInterval) {
        clearInterval(messageInterval);
        messageInterval = undefined;
      }
      
      // Success case - upgrade completed successfully
      waitMessage = 'Finalizing reboot...';
      finalizeAfterReboot();
    }
  }

  async function finalizeAfterReboot() {
    // Wait for the server to be stable (multiple consecutive OKs) to avoid reloading into a 503 if the Pi reboots again.
    const serverStable = await waitUntilServerStable(180000, 4000, 3);
    if (!serverStable) {
      // Keep waiting silently; avoid navigating into a 503 page in kiosk mode.
      waitMessage = 'Still waiting for server to finish rebooting...';
      setTimeout(() => {
        if (!isCleaningUp) {
          finalizeAfterReboot();
        }
      }, 5000);
      return;
    }

    endUpgrade(POST_REBOOT_SPINNER_DELAY, async () => {
      try {
        const response = await fetch(getHttpUrl('/iot/logout'), {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' }
        });
        if (!response.ok) {
          console.warn('Logout request returned non-OK status:', response.status, response.statusText);
        }
      } catch (error) {
        console.warn('Logout during upgrade completion failed', error);
      } finally {
        $navigationStore = { ...$navigationStore, level: 0, name: 'version', data: '', invalidate: true };
        $keysStore.accessLevel = 0;
        // Only reload after the server has been stable (multiple OKs)
        window.location.href = '/level1/version';
      }
    });
  }

  async function waitUntilServerStable(maxWaitMs = 60000, intervalMs = 3000, requiredConsecutive = 2): Promise<boolean> {
    const start = Date.now();
    let consecutiveOk = 0;
    while (Date.now() - start < maxWaitMs) {
      try {
        const resp = await fetchWithTimeout(getHttpUrl('/iot/version'), { method: 'HEAD' }, 5000);
        if (resp.ok) {
          consecutiveOk += 1;
          if (consecutiveOk >= requiredConsecutive) {
            return true;
          }
          // Short pause before the next confirmation check
          await new Promise((resolve) => setTimeout(resolve, intervalMs));
          continue;
        }
      } catch (e) {
        // Expected while services restart; reset the stability counter
      }
      consecutiveOk = 0;
      await new Promise((resolve) => setTimeout(resolve, intervalMs));
    }
    return false;
  }

  // Add a retry function that can be passed to GellertPage
  function retryLoadVersion() {
    // Don't retry if we're cleaning up
    if (isCleaningUp) {
      return;
    }
    
    // Reset error states
    wait = false;
    ready = false;
    upgradeTimeoutError = false;
    
    // Try to re-fetch the data
    if (edit) {
      upgradeInfo();
    } else {
      // Re-initialize for non-edit mode
      $navigationStore.data = getHttpUrl(`/iot/version`);
      ready = true;
    }
  }
  
  // Handle file upload
  async function handleFileUpload() {
    if (!uploadFileInput?.files || uploadFileInput.files.length === 0) {
      uploadError = 'No file selected';
      return;
    }

    const file = uploadFileInput.files[0];
    
    // Validate filename pattern
    if (!file.name.match(/^AS2.*\.rpi$/i)) {
      uploadError = 'Invalid file format. File must be AS2_*.rpi';
      return;
    }

    uploadingFile = true;
    wait = true;  // Enable wait spinner
    uploadError = '';

    try {
      const formData = new FormData();
      formData.append('file', file);

      const response = await fetch(getHttpUrl('/iot/upgrade/upload'), {
        method: 'POST',
        body: formData,
      });

      if (!response.ok) {
        const errorData = await response.json();
        throw new Error(errorData.error || 'Upload failed');
      }

      const result = await response.json();
      
      // Clear the file input
      if (uploadFileInput) {
        uploadFileInput.value = '';
      }

      // Refresh upgrade info to show new file
      setTimeout(() => {
        upgradeInfo();
      }, 1000);
    } catch (error) {
      uploadError = error instanceof Error ? error.message : 'Upload failed';
      console.error('File upload error:', error);
    } finally {
      if (uploadFileInput) {
        uploadFileInput.value = '';
      }
      uploadingFile = false;
      wait = false;  // Disable wait spinner
    }
  }

  function triggerFileUpload() {
    uploadError = '';
    if (uploadFileInput) {
      uploadFileInput.value = '';
    }
    uploadFileInput?.click();
  }
</script>

<GellertPage {ready} {title} {level} name="version" {wait} {waitMessage} retryCallback={retryLoadVersion}>
  <Card class="container-wide mt-2 mb-0 flex flex-col">
    {#if !edit}
      <Table class="mb-0 text-size-xl">
        <Row>
          <Column class="w-1/3 border-r border-gray-400"><div class="py-1">{ $t('level1.version.controller') }</div></Column>
          <Column class="w-2/3"><b>v{version.controller?.[0] ?? ''}</b></Column>
        </Row>
        <Row>
          <Column class="w-1/3 border-r border-gray-400"><div class="py-1">{ $t('level1.version.web-server') }</div></Column>
          <Column class="w-2/3"><b>v{version.webserver}</b></Column>
        </Row>
        <Row>
          <Column class="w-1/3 border-r border-gray-400"><div class="py-1">{ $t('level1.version.user-interface') }</div></Column>
          <Column class="w-2/3"><b>v{version.ui}</b></Column>
        </Row>
        <Row>
          <Column class="w-1/3 border-r border-gray-400"><div class="py-2">{ $t('global.analog-boards') }</div></Column>
          <Column class="w-2/3 max-h-36">
            <div class="h-36 overflow-y-auto"
              data-touch-interactive
              style="touch-action: pan-y; -ms-touch-action: pan-y; -webkit-overflow-scrolling: touch; overscroll-behavior: contain;"
            >
              {#each boards as board}
                <div>
                  {board.name} - v{board.version}
                </div>
              {/each}
            </div>
          </Column>
        </Row>
        <Row>
          <Column class="w-1/3 border-r border-gray-400"><div class="py-2">{ $t('level1.version.displays') }</div></Column>
          <Column class="w-2/3 max-h-36">
            <div class="h-36 overflow-y-auto"
              data-touch-interactive
              style="touch-action: pan-y; -ms-touch-action: pan-y; -webkit-overflow-scrolling: touch; overscroll-behavior: contain;"
            >
              {#each displays as display}
                <div>
                  {display.name} - v{display.version}
                </div>
                <div>
                  IP: {display.ip}
                </div>
                <div class="mb-2">
                  MAC: {display.mac}
                </div>
              {/each}
            </div>
          </Column>
        </Row>
      </Table>
    {:else}
      {#if !$upgradeStore}
        <!-- File Upload Section -->
        <Table class="h-1/2 mb-1 text-size-xl">
          <Row>
            <Column class="w-1/3"></Column>
            <Column class="w-1/3 font-bold">{ $t('level1.version.available-software-updates') }</Column>
            <Column class="w-1/3">
              <input type="file" bind:this={uploadFileInput} on:change={handleFileUpload} accept=".rpi" class="hidden" />
              <Button class="ml-auto mr-2" size="sm" on:click={triggerFileUpload} disabled={uploadingFile}>
                {uploadingFile ? $t('level1.version.uploading') : $t('level1.version.upload-upgrade-file')}
              </Button>
            </Column>
          </Row>
          {#if displayList.length > 0}
              <Row>
                <Column colspan={3}>
                  <List class="w-full text-size-large" bind:value={displaySelected} edit={true} size={2} options={displayOptions} />
                </Column>
              </Row>
          {:else}
            <Row><Column colspan={3}>{ $t('level1.version.none-available') }</Column></Row>
          {/if}
          {#if uploadError}
            <Row><Column class="text-error-500 text-center text-size" colspan={3}>{uploadError}</Column></Row>
          {/if}
        </Table>
        <Table class="h-1/2 mb-1 text-size-xl">
          <Row><Column class="font-bold">{ $t('level1.version.system-to-upgrade') }</Column></Row>
            <Row>
              <Column>
                <List class="w-full text-size-large" bind:value={controllerSelected} edit={true} size={2} options={controllerOptions} />
              </Column>
            </Row>
        </Table>
        <Button class="mx-auto !my-1 {($upgradeStatusStore.UpgradeStatus === 'ARM:Failed' || upgradeTimeoutError) ? '!variant-ghost-error' : ''}" size="xl" on:click={save}>{($upgradeStatusStore.UpgradeStatus === 'ARM:Failed' || upgradeTimeoutError) ? $t('global.retry') : $t('level1.version.upgrade') }</Button>
      {:else}
        <div class="flex flex-col items-center text-size-xl">
          <div class="font-bold">{ $t('level1.version.upgrading-software') }</div>
          <div class="font-bold">{$upgradeStatusStore.UpgradeStatus}</div>
          <div class="font-bold">{ $t('level1.version.please-wait') }</div>
        </div>
      {/if}
    {/if}
  </Card>
</GellertPage>
