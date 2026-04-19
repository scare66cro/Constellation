const SENSITIVE_HEADERS = new Set(['authorization', 'cookie', 'set-cookie', 'x-api-key']);

let debugFetchEnabled = false;
let originalFetch: typeof fetch | undefined;
let requestCounter = 0;

type FetchArgs = Parameters<typeof fetch>;

type SanitizedHeaders = Record<string, string>;

/**
 * Check if a URL is a loopback address (127.0.0.1 or localhost)
 */
function isLoopbackUrl(url: string): boolean {
	try {
		const u = new URL(url, window.location.origin);
		return u.hostname === '127.0.0.1' || u.hostname === 'localhost';
	} catch {
		return false;
	}
}

function sanitizeHeaders(headersInit?: HeadersInit | null): SanitizedHeaders | undefined {
	if (!headersInit) {
		return undefined;
	}

	let headers: Headers;
	if (headersInit instanceof Headers) {
		headers = headersInit;
	} else {
		headers = new Headers(headersInit);
	}

	const sanitized: SanitizedHeaders = {};
	headers.forEach((value, key) => {
		const lowerKey = key.toLowerCase();
		if (SENSITIVE_HEADERS.has(lowerKey)) {
			sanitized[key] = '[redacted]';
		} else {
			sanitized[key] = value;
		}
	});

	return Object.keys(sanitized).length > 0 ? sanitized : undefined;
}

function describeRequest(input: FetchArgs[0], init: FetchArgs[1] = {}): {
	url: string;
	method: string;
	headers?: SanitizedHeaders;
	hasBody: boolean;
} {
	let url: string;
	let method = init?.method;
	let headersSource: HeadersInit | undefined;

	if (typeof input === 'string') {
		url = input;
	} else if (input instanceof URL) {
		url = input.toString();
	} else {
		url = input.url;
		if (!method) {
			method = input.method;
		}
		if (!headersSource) {
			headersSource = input.headers as HeadersInit;
		}
	}

	if (!method) {
		method = 'GET';
	}

	if (!headersSource) {
		headersSource = init?.headers ?? undefined;
	}

	const headers = sanitizeHeaders(headersSource);
	const normalizedMethod = method.toUpperCase();
	const hasBody = Boolean(init?.body ?? (normalizedMethod !== 'GET' && normalizedMethod !== 'HEAD'));

	return { url, method: normalizedMethod, headers, hasBody };
}

function now(): number {
	if (typeof performance !== 'undefined' && typeof performance.now === 'function') {
		return performance.now();
	}
	return Date.now();
}

export function enableFetchDebugLogging(): void {
	if (debugFetchEnabled) {
		return;
	}
	if (typeof window === 'undefined' || typeof window.fetch !== 'function') {
		return;
	}

	const target = window;
	const baseFetch = target.fetch;

	if (typeof baseFetch !== 'function') {
		return;
	}

	debugFetchEnabled = true;
	originalFetch = baseFetch.bind(target);

	target.fetch = (async (...args: FetchArgs) => {
		const requestId = ++requestCounter;
		const { url, method, headers, hasBody } = describeRequest(args[0], args[1]);
		const isLoopback = isLoopbackUrl(url);
		// Check if we're accessing from loopback (kiosk mode)
		const inKioskMode = typeof window !== 'undefined' && 
			(window.location.hostname === '127.0.0.1' || window.location.hostname === 'localhost');

		const start = now();

		try {
			const response = await originalFetch!(...args);

			return response;
		} catch (error) {
			throw error;
		}
	}) as typeof fetch;
}

export function restoreOriginalFetch(): void {
	if (!debugFetchEnabled || !originalFetch) {
		return;
	}
	window.fetch = originalFetch;
	debugFetchEnabled = false;
	originalFetch = undefined;
}
