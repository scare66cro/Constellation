import adapter from '@sveltejs/adapter-node';
// vitePreprocess moved out of @sveltejs/kit/vite in SvelteKit 2.5+; it now
// lives in @sveltejs/vite-plugin-svelte. Importing from the old path
// silently fails at config-load time and Vite never starts listening on :81.
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';
import dotenv from 'dotenv';

dotenv.config();

/** @type {import('@sveltejs/kit').Config} */
const config = {
	// Consult https://kit.svelte.dev/docs/integrations#preprocessors
	// for more information about preprocessors
	preprocess: [
		// `script: true` is required so TypeScript inside <script context="module">
		// (e.g. the `export enum KeyboardTypes` in src/lib/ui/Keyboard.svelte) is
		// transpiled before the Svelte compiler sees it. Without it, Vite throws
		// `typescript_invalid_feature` and the dynamic import 500s.
		vitePreprocess({ script: true })
	],

	kit: {
		// adapter-auto only supports some environments, see https://kit.svelte.dev/docs/adapter-auto for a list.
		// If your environment is not supported or you settled on a specific environment, switch out the adapter.
		// See https://kit.svelte.dev/docs/adapters for more information about adapters.
		adapter: adapter({
			pages: 'build',
			fallback: 'index.html'
		}),
		alias: {
			// Generated proto types live outside src/ in the repo's shared
			// generated/ts tree. Aliasing keeps imports stable across UI and
			// bridge while letting TS + Vite resolve them with a normal path.
			// See docs/proto-direct-redesign-plan.md.
			$proto: '../generated/ts'
		},
		serviceWorker: {
			register: false // We register our own custom service worker
		}
	}
};
export default config;
