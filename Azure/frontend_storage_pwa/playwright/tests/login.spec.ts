import { test, expect } from '@playwright/test';

test('Login page to fail if no login', async ({ page }) => {
  await page.goto('https://10.1.2.108:8000/storage-app');

  // create a locator
  const login = page.locator('text="Login"');

  const firstAttempt = await page.waitForResponse('https://10.1.2.108:8000/login/');
  expect(firstAttempt.status()).toBe(401);

  const [response] = await Promise.all([
    page.waitForResponse('https://10.1.2.108:8000/login/'),
    // Click login button
    login.click(),
  ]);

  expect(response.status()).toBe(500);
});

test('SAML login', async ({ page }) => {
  // Go to https://10.1.2.108:8000/storage-app/
  await page.goto('https://10.1.2.108:8000/storage-app/');
  // Click text=Login with Microsoft
  await page.locator('text=Login with Microsoft').click();
  // Fill [aria-label="Enter your email\, phone\, or Skype\."]
  await page.locator('[aria-label="Enter your email\\, phone\\, or Skype\\."]').fill('john@gellert.com');
  // Click text=Next
  await page.locator('text=Next').click();
  // Fill input[name="passwd"]
  await page.locator('input[name="passwd"]').fill('1010dopDNG');
  // Click text=Sign in
  await page.locator('text=Sign in').click();
  // Click text=Yes
  await page.locator('text=Yes').click();
  await expect(page).toHaveURL('https://10.1.2.108:8000/storage-app/sites');
});
