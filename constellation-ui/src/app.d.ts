// See https://kit.svelte.dev/docs/types#app
// for information about these interfaces
// and what to do when importing types
declare namespace App {
	interface Locals {
		user: {
			level: string,
		};
	}
	// interface PageData {}
	// interface Error {}
	// interface Platform {}
}

// In app.d.ts
declare global {
	interface Window {
			handleUnauthorized: (response: Response) => Promise<boolean>;
	}
}

export {};
