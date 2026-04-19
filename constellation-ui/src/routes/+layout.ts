import { browser } from "$app/environment";
import "$lib/i18n";
import { locale, waitLocale } from "svelte-i18n";
import type { LayoutLoad } from "./$types";

// Disable SSR — this is a kiosk / equipment-control SPA; server-side rendering
// only causes errors (CryptoJS, WebSocket, browser-only APIs) with zero benefit.
export const ssr = false;

export const load: LayoutLoad = async () => {
  if (browser) {
    locale.set(window.navigator.language);
  }
  await waitLocale();
};