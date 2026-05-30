import type { Page, PageList } from "$lib/business/PageType";
import { frontMatterStore } from "$lib/store";
import { get } from "svelte/store";

import { EQUIP_NOT_DEFINED } from "./mode";

export enum DirectionEnum {
  NEXT,
  PREV,
  HOME,
}

function pageVisible(level: number, page: Page, hasRefrig: boolean, hasHumidifier: boolean, hasBaylights: boolean, hasAux: string, frontMatter?: Record<string, string | number | string[]>): boolean {
  // Triton orbits also unlock the Level-2 Refrigeration page (it now hosts
  // the Triton SCADA UI in addition to the legacy stage table).
  const hasTriton = frontMatter?.hasTriton === 'true';
  if (level <= 1) {
    return (page.value !== 'humidifier' || hasHumidifier) && (page.value !== 'pile' || frontMatter?.hasPileSensor === 'true')
      && (page.value !== 'lights' || hasBaylights)
      && ((frontMatter?.panel as string[])?.[8] !== '1' || (page.value !== 'climacell' && page.value !== 'co2'))
      && (level === 1 ? (page.value !== 'service' && page.value !== 'multiview') : true)
      && (level === 0 ? (page.value !== 'lights' && page.value !== 'date' && page.value !== 'alerts') : true);
  } else {
    return (page.value !== 'refrigeration' || hasRefrig || hasTriton) && (page.value !== 'auxiliary' || hasAux === 'true')
      && ((frontMatter?.panel as string[])?.[8] !== '1' || page.value !== 'climacell')
      && ((frontMatter?.panel as string[])?.[8] === '1' || page.value !== 'burner');
  }
}

export function getFilteredPageList(level: number, pageTranslations: PageList, hasAux = 'false'): Page[] {
  const frontMatter = get(frontMatterStore);
	const hasRefrig = (frontMatter?.refrigData as string[])?.filter((i) => i !== '-1').length > 0;

  const hasHumidifier = (frontMatter?.panel as string[])?.[14] !== EQUIP_NOT_DEFINED ||
			(frontMatter?.panel as string[])?.[18] !== EQUIP_NOT_DEFINED ||
			(frontMatter?.panel as string[])?.[22] !== EQUIP_NOT_DEFINED;

  const hasBaylights = (frontMatter?.panel as string[])?.[26] !== EQUIP_NOT_DEFINED || (frontMatter?.panel as string[])?.[27] !== EQUIP_NOT_DEFINED;

	return level <= 1
    ? pageTranslations.level1Pages.filter((i) => i.display && pageVisible(level, i, hasRefrig, hasHumidifier, hasBaylights, hasAux, frontMatter))
    : pageTranslations.level2Pages.filter((i) => i.display && pageVisible(level, i, hasRefrig, hasHumidifier, hasBaylights, hasAux, frontMatter));
}

export function canNavigateToPage(level: number, page: number, pageTranslations: PageList, frontMatter?: Record<string, string | number | string[]>): boolean {
	const hasRefrig = (frontMatter?.refrigData as string[])?.filter((i) => i !== '-1').length > 0;

  const hasHumidifier = (frontMatter?.panel as string[])?.[14] !== EQUIP_NOT_DEFINED ||
			(frontMatter?.panel as string[])?.[18] !== EQUIP_NOT_DEFINED ||
			(frontMatter?.panel as string[])?.[22] !== EQUIP_NOT_DEFINED;

  const hasBaylights = (frontMatter?.panel as string[])?.[26] !== EQUIP_NOT_DEFINED || (frontMatter?.panel as string[])?.[27] !== EQUIP_NOT_DEFINED;

  const pageInfo = level <= 1 ? pageTranslations.level1Pages[page] : pageTranslations.level2Pages[page];
  if (pageInfo.navigation && pageVisible(level, pageInfo, hasRefrig, hasHumidifier, hasBaylights, frontMatter?.hasAux as string, frontMatter)) {
    return true;
  }
  return false;
}

export function getHomePage(page?: string): string {
  switch (page) {
    case 'mnPileTemps.htm':
    case 'mnPileHumids.htm':
      return '/level1/pile';
    case 'mnRunTimes.htm':
      return '/level1/runclock';
    case 'mnFreqCtrl.htm':
      return '/level1/fanspeed';
    case 'mnRampRate.htm':
      // Ramp rate UI was merged into the plenum-setpoints page.
      return '/level1/plentemp';
    case 'mnHumidCtrl.htm':
      return '/level1/humidifier';
    case 'mnClimacellTimes.htm':
      return '/level1/climacell';
    case 'mnCo2Purge.htm':
      return '/level1/co2';
    case 'mnEquipStatus.htm':
      return '/level1/equipment';
    case 'mnPanelSwitches.htm':
      return '/level1/switches';
    case 'mnNetMonitor.htm':
      return '/level1/network';
    case 'mnMainData.htm':
    default:
      return '/';
  }
}

export function getDropDownPage(page: string): string {
  switch (page) {
    case '/level1/pile': return 'pile';
    case '/level1/runclock': return 'runclock';
    case '/level1/fanspeed': return 'fanspeed';
    case '/level1/ramp': return 'plentemp';
    case '/level1/humidifier': return 'humidifier';
    case '/level1/climacell': return 'climacell';
    case '/level1/co2': return 'co2';
    case 'level1/equipment': return 'equipment';
    case '/level1/switches': return 'switches';
    case '/level1/network': return 'network';
    case '/':
    default:
      return '';
  }
}
