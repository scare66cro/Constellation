import { browser } from "$app/environment";
import { init, register } from "svelte-i18n";

const defaultLocale = 'en';

// Load locale files with proper SSR handling
register('en', async () => {
  if (browser) {
    // Client-side: use fetch with the copied files in static/locales
    const response = await fetch('/locales/en.json');
    return await response.json();
  } else {
    // Server-side: import directly from the source files
    try {
      const fs = await import('fs');
      const path = await import('path');
      const filePath = path.join(process.cwd(), 'src/lib/locales/en.json');
      const content = fs.readFileSync(filePath, 'utf-8');
      return JSON.parse(content);
    } catch (error) {
      console.warn('Failed to load en locale:', error);
      return {};
    }
  }
});

register('zh', async () => {
  if (browser) {
    // Client-side: use fetch with the copied files in static/locales
    const response = await fetch('/locales/zh.json');
    return await response.json();
  } else {
    // Server-side: import directly from the source files
    try {
      const fs = await import('fs');
      const path = await import('path');
      const filePath = path.join(process.cwd(), 'src/lib/locales/zh.json');
      const content = fs.readFileSync(filePath, 'utf-8');
      return JSON.parse(content);
    } catch (error) {
      console.warn('Failed to load zh locale:', error);
      return {};
    }
  }
});

init({
  fallbackLocale: defaultLocale,
  initialLocale: browser ? window.navigator.language : defaultLocale,
});