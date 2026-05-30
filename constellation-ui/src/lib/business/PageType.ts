export type Page = { text: string, value: string, display: boolean, navigation: boolean };
export type PageList = { level1Pages: Page[], level2Pages: Page[] };

export type PageType = {
  text: string;
  /**
   * Option value. Strings remain the default for navigation/legacy
   * pages; numbers are accepted so proto-direct pages can bind a
   * Select directly to a typed numeric proto field without a
   * string round-trip (see useDraft.ts).
   */
  value: string | number;
  display?: boolean;
  navigation?: boolean;
};
