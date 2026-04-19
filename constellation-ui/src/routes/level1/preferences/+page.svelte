<script lang="ts">
  import { onMount } from 'svelte';
  import GellertPage from '$lib/components/GellertPage.svelte';
  import Card from '$lib/ui/Card.svelte';
  import Table from '$lib/ui/Table.svelte';
  import Row from '$lib/ui/Row.svelte';
  import Column from '$lib/ui/Column.svelte';
  import Select from '$lib/ui/Select.svelte';
  import Button from '$lib/ui/Button.svelte';
  import ImageManager from '$lib/components/ImageManager.svelte';
  import { navigationStore, backgroundStore, localeStore, pageTranslationsStore } from '$lib/store';
  import { getHttpUrl } from '$lib/business/util';
  import { cloneDeep } from 'lodash-es';
  import { t, locale } from 'svelte-i18n';

  // Local state and deriveds
  $: title = $t('page-list.preferences');
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;

  $: lang = $locale;
  let ready = false;
  let wait = false;
  let showImageManager = false;

  const localeOptions = [
    { text: 'English', value: 'en' },
    { text: 'Chinese', value: 'zh' },
  ];

  async function changeLocale() {
    wait = true;
    try {
      const resp = await fetch(getHttpUrl('/iot/locale'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ locale: lang }),
      });
      if (resp.status === 200) {
        // Update client locale and refresh page translations
        $locale = lang;
        $localeStore = $locale;
        $pageTranslationsStore = cloneDeep($pageTranslationsStore);
      }
      // Allow time for backend processes to complete
      setTimeout(() => { wait = false; }, 30000);
    } catch (err) {
      console.error('Error changing locale:', err);
      wait = false;
    }
  }

  onMount(async () => {
    try {
      // Ensure background images/options are loaded so the dropdown reflects persisted/custom options
      await $backgroundStore.loadPictures?.();
    } catch (e) {
      console.error(e);
    }
    ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="preferences">
  <Card class="mx-auto mt-2 flex flex-col container-wide">
    <Table class="mb-2">
      <Row>
        <Column class="text-size-xl">
          {$t('level1.miscellaneous.background-image')}: <Select class="ml-2 w-128" size="xl" bind:value={$backgroundStore.backgroundImage} options={$backgroundStore.images} {edit} />
          {#if edit}
            <Button size="xl" on:click={() => (showImageManager = true)}>
              {$t('level1.miscellaneous.manage-images')}
            </Button>
          {/if}
        </Column>
      </Row>
      <Row>
        <Column class="text-size-xl">
          {$t('level1.miscellaneous.language')}: <Select class="ml-2 xl:w-96" size="xl" bind:value={lang} options={localeOptions} {edit} on:change={changeLocale} />
        </Column>
      </Row>
    </Table>
  </Card>
</GellertPage>

<!-- Image Manager Modal -->
<ImageManager bind:visible={showImageManager} on:close={() => (showImageManager = false)} />

<!-- relies on global utility classes for width/typography (same as Miscellaneous page) -->
