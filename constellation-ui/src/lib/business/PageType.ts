export type Page = { text: string, value: string, display: boolean, navigation: boolean };
export type PageList = { level1Pages: Page[], level2Pages: Page[] };

export type PageType = {
  text: string;
  value: string;
  display?: boolean;
  navigation?: boolean;
};
