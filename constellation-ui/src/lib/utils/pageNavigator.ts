import { goto } from '$app/navigation';
import { get } from 'svelte/store';
import { navigationStore, pageTranslationsStore } from '$lib/store';
import { getFilteredPageList } from '$lib/business/paging';
import type { Page } from '$lib/business/PageType';

export interface NavigationResult {
  canNavigate: boolean;
  url?: string;
  pageName?: string;
}

export class PageNavigator {
  static getNavigablePages(level: number): Page[] {
    const pageTranslations = get(pageTranslationsStore);
    if (!pageTranslations) return [];

    const filteredPages = getFilteredPageList(level, pageTranslations);
    let navigablePages = filteredPages.filter(page => page.navigation);    // For level 1, exclude system monitor page as it's not shown in navigation
    if (level === 1) {
      navigablePages = navigablePages.filter(page => 
        page.value !== '' // Filter out the system monitor page which has empty value
      );
    }

    return navigablePages;
  }

  static getCurrentPageIndex(level: number, currentName: string): number {
    const navigablePages = this.getNavigablePages(level);
    
    // For level 0 system monitor, treat empty name or 'version' as first page
    if (level === 0 && currentName === '') {
      return 0;
    }

    const currentPageIndex = navigablePages.findIndex(page => page.value === currentName);
    return currentPageIndex >= 0 ? currentPageIndex : 0;
  }
  
  static getNextPage(level: number, currentName: string): NavigationResult {
    const navigablePages = this.getNavigablePages(level);
    const currentIndex = this.getCurrentPageIndex(level, currentName);

    if (currentIndex < navigablePages.length - 1) {
      const nextPage = navigablePages[currentIndex + 1];
      const baseUrl = this.getBaseUrlForLevel(level);
      const url = level === 0 && nextPage.value === '' ? '/' : `${baseUrl}/${nextPage.value}`;
      return {
        canNavigate: true,
        url: url,
        pageName: nextPage.value
      };
    }

    return { canNavigate: false };
  }
  static getPreviousPage(level: number, currentName: string): NavigationResult {
    const navigablePages = this.getNavigablePages(level);
    const currentIndex = this.getCurrentPageIndex(level, currentName);

    if (currentIndex > 0) {
      const prevPage = navigablePages[currentIndex - 1];
      const baseUrl = this.getBaseUrlForLevel(level);
      const url = level === 0 && prevPage.value === '' ? '/' : `${baseUrl}/${prevPage.value}`;
      return {
        canNavigate: true,
        url: url,
        pageName: prevPage.value
      };
    }

    return { canNavigate: false };
  }
  private static getBaseUrlForLevel(level: number): string {
    let baseUrl: string;
    switch (level) {
      case 0:
        baseUrl = '';
        break;
      case 1:
        baseUrl = '/level1';
        break;
      case 2:
        baseUrl = '/level2';
        break;
      default:
        baseUrl = '';
        break;
    }
    return baseUrl;
  }
  static async navigateToNext(level: number, currentName: string): Promise<boolean> {
    const result = this.getNextPage(level, currentName);
    if (result.canNavigate && result.url) {
      // Update navigation store
      const navStore = get(navigationStore);
      navigationStore.set({
        ...navStore,
        name: result.pageName || '',
        level: level
      });

      await goto(result.url, { 
        replaceState: false,
        noScroll: false,
        keepFocus: false
      });
      return true;
    }
    return false;
  }
  static async navigateToPrevious(level: number, currentName: string): Promise<boolean> {
    const result = this.getPreviousPage(level, currentName);
    if (result.canNavigate && result.url) {
      // Update navigation store
      const navStore = get(navigationStore);
      navigationStore.set({
        ...navStore,
        name: result.pageName || '',
        level: level
      });

      await goto(result.url, { 
        replaceState: false,
        noScroll: false,
        keepFocus: false
      });
      return true;
    }
    return false;
  }

  static canNavigateNext(level: number, currentName: string): boolean {
    return this.getNextPage(level, currentName).canNavigate;
  }

  static canNavigatePrevious(level: number, currentName: string): boolean {
    return this.getPreviousPage(level, currentName).canNavigate;
  }
}
