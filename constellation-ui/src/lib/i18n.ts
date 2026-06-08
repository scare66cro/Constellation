import { addMessages, init, getLocaleFromNavigator } from 'svelte-i18n';

// Import locale JSON from single canonical source
import en from './locales/en.json';
import zh from './locales/zh.json';
import es from './locales/es.json';
import fr from './locales/fr.json';

addMessages('en', en as any);
addMessages('zh', zh as any);
addMessages('es', es as any);
addMessages('fr', fr as any);

init({
  fallbackLocale: 'en',
  initialLocale: getLocaleFromNavigator() || 'en'
});
