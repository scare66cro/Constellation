/// <reference types="vitest" />
import { purgeCss } from 'vite-plugin-tailwind-purgecss';
import { sveltekit } from '@sveltejs/kit/vite';
import { defineConfig, type UserConfig } from 'vite';
// NOTE: defineConfig is imported from 'vite' (not 'vitest/config') because
// vitest 2.x bundles a nested vite 5 copy whose Plugin<any> type is
// nominally distinct from our root vite 6 — using vitest/config triggers
// a sveltekit() plugin overload mismatch. The `test` block is still
// honored by vitest at runtime; we widen UserConfig to include it.
export default defineConfig({
	plugins: [sveltekit(), purgeCss()],
	// JSON named exports cause a conflict because our locale file has a top-level key named "global".
	// Vite will by default generate `export const global = ...` for en.json which can clash with
	// other injected code in the SSR context leading to: "Identifier 'global' has already been declared".
	// Disabling namedExports ensures we only use the default export object.
	json: {
		namedExports: false
	},
	build: {
    sourcemap: true, // Enables source maps for production builds
    target: 'esnext', // Optional: ensures modern output
    minify: 'esbuild' // Optional: faster builds with esbuild
	},
	test: {
		include: ['src/**/*.{test,spec}.{js,ts}']
	},
	server: {
    host: true, // Allows external access (important for remote debugging)
    port: 81,
    strictPort: true,
    sourcemapIgnoreList: false, // Optional: ensures all source maps are included
		fs: {
			allow: ['..'],
		},
		proxy: {
			// Forward /iot/* requests to the bridge server during development
			'/iot/ws': {
				target: 'ws://localhost:9001',
				ws: true,
			},
			// Phase 0.3+ proto-direct binary stream (see docs/proto-direct-redesign-plan.md)
			'/proto/stream': {
				target: 'ws://localhost:9001',
				ws: true,
			},
			// Phase 0.4+ proto-direct typed write path (`POST /proto/write/<field>`).
			// Must be registered AFTER `/proto/stream` so the WS entry matches first.
			// Without this, UI POSTs return 404 from Vite and saves silently fail.
			'/proto': {
				target: 'http://localhost:9001',
				changeOrigin: true,
			},
			'/iot': {
				target: 'http://localhost:9001',
				changeOrigin: true,
			},
			// Forward /vfd/* to bridge (rewritten to /iot/*)
			'/vfd': {
				target: 'http://localhost:9001',
				changeOrigin: true,
				rewrite: (path: string) => path.replace(/^\/vfd/, '/iot'),
			},
		},
	},
	ssr: {
		noExternal: ['svelte-i18n'],
	},
	optimizeDeps: {
		include: ['svelte-i18n']
	}
} as UserConfig & { test?: { include?: string[] } });
