<script lang="ts">
	import { goto } from "$app/navigation";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Button from "$lib/ui/Button.svelte";
	import Card from "$lib/ui/Card.svelte";
  import Alarms from '$lib/components/Alarms.svelte';
  import { alarmsStore, historyStore, keyboardStore, keysStore } from "$lib/store";
	import { getModalStore, type ModalComponent, type ModalSettings } from "@skeletonlabs/skeleton";
	import { checkPassword } from "$lib/business/util";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { t } from "svelte-i18n";

  $: error = [false, false, false];
  let wait = false;

  const modalStore = getModalStore();

  const alarmComponent: ModalComponent = {
		ref: Alarms,
	};

  const modal: ModalSettings = {
		type: 'component',
		component: alarmComponent,
	};

  function showAlarms() {
    if (!$alarmsStore.isShowingAlarms) {
      if ($alarmsStore.handle) {
        clearTimeout($alarmsStore.handle);
        $alarmsStore.handle = undefined;
      }
      $alarmsStore.canShowAlarm = true;
      if ($alarmsStore.canShowAlarm && !$alarmsStore.isShowingAlarms) {
    		modalStore.trigger(modal);
		    $alarmsStore.isShowingAlarms = true;
      }
  	}
  }

  async function gotoHistoryPage(navigate: string, errorIndex: number) {
    if ($keysStore.hasLevel1Password && $keysStore.accessLevel < 1) {
      $keyboardStore.keyboardType = KeyboardTypes.Alpha;
      $keyboardStore.label = 'Password';
      $keyboardStore.start = '';
      $keyboardStore.resultReady = async (data: string) => { let user = data.split(':'); await checkPassword(user.length > 1 ? user[0] : '', user.length > 1 ? user[1] : user[0], (value) => wait = value, (value) => error[errorIndex] = value, (_) => goto(navigate)) };
      $keyboardStore.inputType = 'loginPassword';
      $keyboardStore.hidden = false;
      $keyboardStore = $keyboardStore;
    } else if (!$keysStore.hasLevel1Password) {
      await checkPassword('DEFAULT', '', (value) => wait = value, (value) => error[errorIndex] = value, (_) => goto(navigate));
    } else {
      goto(navigate);
    }
  }
</script>

<GellertPage title={$t('level1.history.history-data')} level={1} name="history" {wait}>
  <Card class="mx-auto w-1/2 flex flex-col">
    <Button size="xl" class="mx-auto w-64 {error[0] ? 'text-red-500' : ''}" on:click={() => { $historyStore.type = 'User'; gotoHistoryPage('/history/userlog', 0); }}>{$t('level1.history.history-log')}</Button>
    <Button size="xl" class="mx-auto w-64 {error[1] ? 'text-red-500' : ''}" on:click={() => { $historyStore.type = 'Activity'; gotoHistoryPage('/history/activitylog', 1); }}>{$t('level1.history.activity-log')}</Button>
    <Button size="xl" class="mx-auto w-64 {error[2] ? 'text-red-500' : ''}" on:click={() => gotoHistoryPage('/history/alarm', 2)}>{$t('level1.history.alarm-log')}</Button>
    <Button size="xl" class="mx-auto w-128" on:click={showAlarms}>{$t('level1.history.show-alarm-window')}</Button>
  </Card>
</GellertPage>