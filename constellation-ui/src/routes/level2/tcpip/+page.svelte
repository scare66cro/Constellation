<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Card from "$lib/ui/Card.svelte";
  import IP from "$lib/components/IP.svelte";
  import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { navigationStore, keyboardStore } from "$lib/store";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import Select from "$lib/ui/Select.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { getModalStore, type ModalSettings } from "@skeletonlabs/skeleton";
  import Button from "$lib/ui/Button.svelte";
  import { getHttpUrl, updateIotConnection, startRebootPause, isLoopbackAccess } from "$lib/business/util";

  const modalStore = getModalStore();

  type TCPIPConfig = {
    HttpPort: string[];
    LocalIpAdd: string[];
    LocalIpMask: string[];
    LocalIpGateway: string[];
    LocalIpMode: string[];
    LocalDns: string[];
  };

  // Acknowledge potential extra props from SvelteKit load function
  export let data: TCPIPConfig & { props?: any };

  let level = 2;
  let title = `TCP/IP ${$t('level2.tcpip.setup')}`;
  let ipMode = '0';
  $: saveData = {
    HttpPort: [],
    LocalIpAdd: [],
    LocalIpMask: [],
    LocalIpGateway: [],
    LocalIpMode: [],
    LocalDns: [],
  } as TCPIPConfig;
  $: ready = false;
  $: wait = false;
  $: localip = Array(4).fill('');
  $: mask = Array(4).fill('');
  $: gateway = Array(4).fill('');
  $: publicIP = Array(4).fill('0');
  $: port = '';
  $: dns1 = Array(4).fill('');
  $: dns2 = Array(4).fill('');
  // Snapshot of the original values to compute dirty state against
  let originalData: TCPIPConfig | null = null;

  // Keyboard visibility handling - disable save button when keyboard is visible
  let isDisabledDueToKeyboard = false;
  let keyboardHideTimeout: NodeJS.Timeout;

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

  // Reactive statement to update saveData whenever any input changes
  $: {
    const ip = localip.join('.');
    const maskStr = mask.join('.');
    const gatewayStr = gateway.join('.');
    const publicIPStr = publicIP.join('.');
    const dns1Str = dns1.join('.');
    const dns2Str = dns2.join('.');
    // Only include DNS servers that are not empty (for static mode)
    const dnsServers = [];
    if (ipMode === '0') { // Static mode
      if (dns1Str && dns1Str !== '...') dnsServers.push(dns1Str);
      if (dns2Str && dns2Str !== '...') dnsServers.push(dns2Str);
    }
    saveData = {
      HttpPort: [port, publicIPStr],
      LocalIpAdd: [ip],
      LocalIpMask: [maskStr],
      LocalIpGateway: [gatewayStr],
      LocalIpMode: [ipMode],
      LocalDns: dnsServers,
    };
  }

  // Expose a stable isDirty function that compares current inputs to the original snapshot
  $: $navigationStore.isDirty = () => (originalData ? !isEqual(saveData, originalData) : false);

  // Custom save handler that shows warning for IP/port changes
  function handleSaveClick() {
    // Show confirmation modal before saving
    const newIp = localip.join('.');
    const newPort = port || '80'; // Default to 80 if port is empty
    // Use a temporary URL construction for display in modal
    const displayUrl = `http://${newIp}${newPort !== '80' ? ':' + newPort : ''}`;
    
    const modal: ModalSettings = {
      type: 'confirm',
      title: $t('level2.tcpip.restart-required'),
      body: $t('level2.tcpip.system-will-restart-and-redirect', { values: { url: displayUrl } }),
      buttonTextCancel: $t('global.cancel'),
      buttonTextConfirm: $t('global.save'),
      response: (confirmed: boolean) => {
        if (confirmed) {
          performSave();
        }
      }
    };
    modalStore.trigger(modal);
  }

  async function performSave() {
    wait = true;
    
    let redirectUrl = '';
    const newIp = localip.join('.');
    const newPort = port || '80'; // Default to 80 if port is empty
    // Construct the new URL with proper port handling
    const protocol = window.location.protocol; // Use current protocol (http/https)
    redirectUrl = `${protocol}//${newIp}${newPort !== '80' && newPort !== '443' ? ':' + newPort : ''}`;
    const inKioskMode = isLoopbackAccess();
    // Kiosk sessions must stay on loopback, so suppress redirect when kiosk is detected
    const shouldRedirect = !inKioskMode && redirectUrl !== '';
    
    try {
      // For network changes, we expect the connection to fail after the save
      // So we use a shorter timeout and don't expect a proper response
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 60000);
      
      const result = await fetch(getHttpUrl('/iot/tcpip'), {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(saveData),
        signal: controller.signal
      });

      clearTimeout(timeoutId);
      const json = await result.json();
      
      if (json.status === 200 || json.status === undefined) {
        // Save successful - update original data
        data = {...saveData};
        // Reset original snapshot to the saved values
        originalData = JSON.parse(JSON.stringify(saveData)) as TCPIPConfig;
        
        // Clear the dirty state since save was successful
        $navigationStore.isDirty = () => false;
        
        // Update IoT connection details in store if they changed
        const newIp = localip.join('.');
        const newPort = port || '80';
        const protocol = window.location.protocol;
        updateIotConnection(newIp, newPort, protocol);

        // Start global reboot pause (30-60s) to suppress backend polling
        startRebootPause();
        // Clear data endpoint so GellertPage won't attempt refetches during pause
        $navigationStore.data = '';

        if (shouldRedirect) {
          // Wait longer (8s) before redirect to allow network stack to come up
          setTimeout(() => {
            const currentPath = window.location.pathname + window.location.search;
            window.location.href = `${redirectUrl}${currentPath}`;
          }, 8000);
        }
      } else {
        console.error('Save failed:', json);
      }
    } catch (error) {
      // If this is a network change and the request fails/times out, that's expected
      if (
        (error as Error)?.name === 'AbortError' || 
        (error as Error)?.message?.includes('fetch') ||
        (error as Error)?.message?.includes('Failed to fetch')
      ) {
        // Update data and clear dirty state
        data = {...saveData};
        $navigationStore.isDirty = () => false;
        // Reset original snapshot to the expected new values
        originalData = JSON.parse(JSON.stringify(saveData)) as TCPIPConfig;

        // Update IoT connection details in store for the expected new settings
        const newIp = localip.join('.');
        const newPort = port || '80';
        const protocol = window.location.protocol;
        updateIotConnection(newIp, newPort, protocol);

        startRebootPause();
        $navigationStore.data = '';
        if (shouldRedirect) {
          setTimeout(() => {
            const currentPath = window.location.pathname + window.location.search;
            window.location.href = `${redirectUrl}${currentPath}`;
          }, 8000);
        }
      } else {
        console.error('Error saving:', error);
      }
    } finally {
      wait = false;
    }
  }

  onMount(async () => {
    $navigationStore.data = getHttpUrl('/iot/tcpip');

    // Initialize component state using the potentially larger 'data' object as before
    ipMode = data.LocalIpMode[0];
    const tcpip = data.LocalIpAdd[0].split(':')
    localip = tcpip[0].split('.');
    port = data.HttpPort[0];
    mask = data.LocalIpMask[0].split('.');
    gateway = data.LocalIpGateway[0].split('.');
    if (gateway === undefined || gateway.length < 4) {
      gateway = Array(4).fill('');
    }
    // Normalize public IP to 4 octets; backend may send '0' instead of '0.0.0.0'
    const publicIpRaw = data?.HttpPort?.[1];
    const parts = (publicIpRaw ?? '').split('.');
    publicIP = [0, 1, 2, 3].map((i) => (parts[i] !== undefined && parts[i] !== '' ? parts[i] : '0'));

    // Initialize DNS servers
    const dnsServers = data.LocalDns || [];
    if (dnsServers.length > 0) {
      const dns1Parts = dnsServers[0].split('.');
      dns1 = [0, 1, 2, 3].map((i) => (dns1Parts[i] !== undefined && dns1Parts[i] !== '' ? dns1Parts[i] : ''));
    }
    if (dnsServers.length > 1) {
      const dns2Parts = dnsServers[1].split('.');
      dns2 = [0, 1, 2, 3].map((i) => (dns2Parts[i] !== undefined && dns2Parts[i] !== '' ? dns2Parts[i] : ''));
    }

    // Capture original snapshot using the normalized current inputs so dirty check is consistent
    originalData = {
      HttpPort: [port, publicIP.join('.')],
      LocalIpAdd: [localip.join('.')],
      LocalIpMask: [mask.join('.')],
      LocalIpGateway: [gateway.join('.')],
      LocalIpMode: [ipMode],
      LocalDns: dnsServers,
    };
    ready = true;
  });

  onDestroy(() => {
    // Clean up keyboard timeout
    if (keyboardHideTimeout) {
      clearTimeout(keyboardHideTimeout);
    }
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="tcpip">
  <Card class="text-xl mx-auto flex flex-col w-3/4 mt-2">
    <Table class="mb-1 text-lg">
      <Row>
        <Column class="w-1/3 xl:py-1 border-r border-gray-400">{ $t('level2.tcpip.addressing-mode') }</Column>
        <Column class="w-2/3 xl:py-1">
          <Select bind:value={ipMode} class="w-64" size="lg" edit={true}>
            <option value="1">DHCP</option>
            <option value="0">Static</option>
          </Select>
        </Column>
      </Row>
      <Row>
        <Column class="w-1/3 xl:py-1 border-r border-gray-400">IP { $t('level2.tcpip.address') }</Column>
        <Column class="w-2/3 xl:py-1"><IP bind:ip={localip} width="w-32" edit={true}/></Column>
      </Row>
      <Row>
        <Column class="w-1/3 xl:py-1 border-r border-gray-400">{ $t('level2.tcpip.network-mask') }</Column>
        <Column class="w-2/3 xl:py-1"><IP bind:ip={mask} width="w-32" edit={true}/></Column>
      </Row>
      <Row>
        <Column class="w-1/3 xl:py-1 border-r border-gray-400">{ $t('level2.tcpip.default-gateway') }</Column>
        <Column class="w-2/3 xl:py-1"><IP bind:ip={gateway} width="w-32" edit={true}/></Column>
      </Row>
      {#if ipMode === '0'}
      <Row>
        <Column class="w-1/3 xl:py-1 border-r border-gray-400">DNS 1</Column>
        <Column class="w-2/3 xl:py-1"><IP bind:ip={dns1} width="w-32" edit={true}/></Column>
      </Row>
      <Row>
        <Column class="w-1/3 xl:py-1 border-r border-gray-400">DNS 2</Column>
        <Column class="w-2/3 xl:py-1"><IP bind:ip={dns2} width="w-32" edit={true}/></Column>
      </Row>
      {/if}
      <Row>
        <Column class="w-1/3 xl:py-1 border-r border-gray-400">{ $t('level2.tcpip.public-address') }</Column>
        <Column class="w-2/3 xl:py-1"><IP bind:ip={publicIP} width="w-32" edit={true}/></Column>
      </Row>
    </Table>
    <Table class="text-lg">
      <Row>
        <Column class="w-1/3 xl:py-1 border-r border-gray-400">{ $t('global.port') }</Column>
        <Column class="w-2/3 xl:py-1"><TextField bind:value={port} size="lg" edit={true} keyboardType={KeyboardTypes.Numeric}/></Column>
      </Row>
    </Table>
    
    <Button class="mx-auto mt-2" size="lg" on:click={handleSaveClick} disabled={wait || !$navigationStore.isDirty?.() || isDisabledDueToKeyboard}>
      {$t('global.save')}
    </Button>
  </Card>
</GellertPage>