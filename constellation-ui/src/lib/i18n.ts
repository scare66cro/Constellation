import { addMessages, init, getLocaleFromNavigator } from 'svelte-i18n';

// Import locale JSON from single canonical source
import en from './locales/en.json';
import zh from './locales/zh.json';

addMessages('en', en as any);
addMessages('zh', zh as any);

init({
  fallbackLocale: 'en',
  initialLocale: getLocaleFromNavigator() || 'en'
});
