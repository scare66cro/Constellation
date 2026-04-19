import { defineConfig, devices } from '@playwright/test';

// Playwright configuration extended to automatically launch the Vite dev server
// so tests that navigate to http://localhost:5173 succeed without a manually started server.
export default defineConfig({
	testDir: 'tests',
	testMatch: /(.+\.)?(test|spec)\.[jt]s/,
	timeout: 30_000,
	expect: { timeout: 5000 },
	retries: process.env.CI ? 2 : 0,
	use: {
		baseURL: 'http://localhost:81',
		trace: 'on-first-retry',
		video: 'retain-on-failure',
		screenshot: 'only-on-failure'
	},
	projects: [
		{
			name: 'chromium',
			use: { ...devices['Desktop Chrome'] }
		}
	],
	// webServer: {
	// 	// Invoke Vite directly via node to bypass shell script execution policies
	// 	command: 'npm run dev',
	// 	port: 5173,
	// 	reuseExistingServer: !process.env.CI,
	// 	timeout: 120_000,
	// 	stdout: 'pipe',
	// 	stderr: 'pipe'
	// }
});
