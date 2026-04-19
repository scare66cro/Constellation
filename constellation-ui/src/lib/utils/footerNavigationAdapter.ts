import { goto } from '$app/navigation';
import { get } from 'svelte/store';
import { navigationStore, pageTranslationsStore, frontMatterStore } from '$lib/store';
import { canNavigateToPage } from '$lib/business/paging';
import type { Page } from '$lib/business/PageType';

export interface NavigationResult {
  canNavigate: boolean;
  url?: string;
  pageName?: string;
  targetLevel?: number;
}

/**
 * Refactored navigation utility that uses the same logic as GellertFooter
 * This ensures consistent navigation behavior across the application
 */
export class FooterNavigationAdapter {
  /**
   * Build an ordered list of navigable pages for a level, clamping ends and
   * excluding system monitor for level 1 navigation (we handle fallback separately).
   */
  private static buildNavigablePages(level: number): Page[] {
    const pageTranslations = get(pageTranslationsStore);
    const frontMatter = get(frontMatterStore);

    if (!pageTranslations) return [];

    const pageList = level <= 1 ? pageTranslations.level1Pages : pageTranslations.level2Pages;
    if (!pageList) return [];

    const navigable = pageList.filter((_, idx) => canNavigateToPage(level, idx, pageTranslations, frontMatter));

    // For level 1 navigation we do not include system monitor in the regular sequence.
    return level === 1 ? navigable.filter(page => page.value !== '') : navigable;
  }

  /**
   * Get next page navigation result
   */
  static getNextPage(level: number, currentName: string): NavigationResult {
    const navigablePages = this.buildNavigablePages(level);
    const currentIndex = navigablePages.findIndex(page => page.value === currentName);

    if (currentIndex === -1 || currentIndex >= navigablePages.length - 1) {
      return { canNavigate: false };
    }

    const nextPage = navigablePages[currentIndex + 1];
    const levelName = level <= 1 ? 'level1' : 'level2';
    const url = `/${levelName}/${nextPage.value}`;

    return {
      canNavigate: true,
      url,
      pageName: nextPage.value,
      targetLevel: level,
    };
  }
  /**
   * Get previous page navigation result
   */
  static getPreviousPage(level: number, currentName: string): NavigationResult {
    const navigablePages = this.buildNavigablePages(level);
    const currentIndex = navigablePages.findIndex(page => page.value === currentName);

    if (currentIndex === -1) {
      return { canNavigate: false };
    }

    // Standard backward step within the same level.
    if (currentIndex > 0) {
      const prevPage = navigablePages[currentIndex - 1];
      const levelName = level <= 1 ? 'level1' : 'level2';
      const url = prevPage.value === '' ? '/' : `/${levelName}/${prevPage.value}`;

      return {
        canNavigate: true,
        url,
        pageName: prevPage.value,
        targetLevel: level,
      };
    }

    // Level 1 special case: downgrade to level 0 equivalent or system monitor.
    if (level === 1) {
      if (['plentemp'].includes(currentName)) {
        return { canNavigate: false };
      }

      const level0Pages = this.buildNavigablePages(0);
      const samePageAtLevel0 = level0Pages.find(page => page.value === currentName);

      if (samePageAtLevel0) {
        return {
          canNavigate: true,
          url: `/level1/${currentName}`,
          pageName: currentName,
          targetLevel: 0,
        };
      }

      const systemMonitor = level0Pages.find(page => page.value === '');
      if (systemMonitor) {
        return {
          canNavigate: true,
          url: '/',
          pageName: '',
          targetLevel: 0,
        };
      }
    }

    return { canNavigate: false };
  }
  /**
   * Navigate to the next page
   */
  static async navigateToNext(level: number, currentName: string): Promise<boolean> {
    const result = this.getNextPage(level, currentName);
    if (result.canNavigate && result.url) {
      // Update navigation store
      const navStore = get(navigationStore);
      
      // Determine the target level based on the destination page
      const targetLevel = result.targetLevel ?? (result.pageName === '' ? 0 : level);
      
      navigationStore.set({
        ...navStore,
        name: result.pageName || '',
        level: targetLevel,
        dropDownPage: result.pageName || ''
      });
      
      await goto(result.url);
      return true;
    }
    
    return false;
  }
  /**
   * Navigate to the previous page
   */
  static async navigateToPrevious(level: number, currentName: string): Promise<boolean> {
    const result = this.getPreviousPage(level, currentName);
    if (result.canNavigate && result.url) {
      // Update navigation store
      const navStore = get(navigationStore);
      
      // Determine the target level based on the destination page
      const targetLevel = result.targetLevel ?? (result.pageName === '' ? 0 : level);
      
      navigationStore.set({
        ...navStore,
        name: result.pageName || '',
        level: targetLevel,
        dropDownPage: result.pageName || ''
      });
      
      await goto(result.url);
      return true;
    }
    
    return false;
  }
  
  /**
   * Check if can navigate to next page
   */
  static canNavigateNext(level: number, currentName: string): boolean {
    return this.getNextPage(level, currentName).canNavigate;
  }
  
  /**
   * Check if can navigate to previous page
   */
  static canNavigatePrevious(level: number, currentName: string): boolean {
    return this.getPreviousPage(level, currentName).canNavigate;
  }
}
