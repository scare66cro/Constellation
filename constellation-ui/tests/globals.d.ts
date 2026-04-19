// Shared test-only global type declarations.
// Consolidates repeating `declare global` blocks from individual test files.

interface PageInfo {
  text: string;
  value: string;
  display: boolean;
  navigation: boolean;
}

interface MockPageTranslations {
  level1Pages: PageInfo[];
}

declare global {
  interface Window {
    mockPageTranslations: MockPageTranslations;
    getPreviousPageName: (level: number, currentName: string) => string | null;
  // Swipe/navigation test helpers
  SwipeGestureHandler: any;
  isChartPage: () => boolean;
  initNormalPage: () => void;
  initChartPage: () => void;
  showNormalPage: () => void;
  showChartPage: () => void;
  getDragPrevented: () => boolean;
  getSwipeResult: () => { detected: boolean; direction: string };
  navigationStore: { isDirty: () => boolean };
  }
}

export {};
