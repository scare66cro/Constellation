import type { Handle } from "@sveltejs/kit";
import { locale } from "svelte-i18n";

export const handle: Handle = async ({ event, resolve }) => {
  const lang = event.request.headers.get('accept-language')?.split(',')[0];
  if (lang) {
    locale.set(lang);
  }
  
  // Resolve the event with options that include filter for response headers
  return resolve(event, {
    filterSerializedResponseHeaders: (name, value) => {
      // Allow content-type and other common headers
      return name === 'content-type' || 
             name === 'cache-control' || 
             name === 'content-length' || 
             name === 'etag' ||
             name.startsWith('x-');
    }
  });
}