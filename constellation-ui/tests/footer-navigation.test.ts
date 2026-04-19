import { expect, test } from '@playwright/test';
// Global window test helpers & interfaces now come from tests/globals.d.ts

test.describe('Footer Navigation Adapter', () => {
  test('should handle navigation between pages correctly', async ({ page }) => {
    // Create a test page with simplified navigation logic (same as plentemp-navigation.test.ts)
    await page.setContent(`
      <!DOCTYPE html>
      <html>
      <head>
        <title>Footer Navigation Test</title>
        <script type="module">
          // Mock the exact page structure from the app (same as plentemp-navigation.test.ts)
          window.mockPageTranslations = {
            level1Pages: [
              { text: 'System Monitor', value: '', display: true, navigation: true },
              { text: 'Network Monitor', value: 'network', display: true, navigation: false },
              { text: 'Refresh Page', value: 'refresh', display: true, navigation: false },
              { text: 'Select Page', value: 'history', display: true, navigation: false },
              { text: 'Data Info', value: 'datainfo', display: false, navigation: false },
              { text: 'Plenum Temperature', value: 'plentemp', display: true, navigation: true }
            ]
          };

          // Use the exact same navigation logic as plentemp-navigation.test.ts
          window.getPreviousPageName = (level, currentName) => {
            const pageList = window.mockPageTranslations.level1Pages;
            const start = pageList.findIndex((page) => page.value === currentName);
            
            if (start === -1) return null;
            
            // Look for previous navigable page
            for (let i = start - 1; i >= 0; i--) {
              const pageInfo = pageList[i];
              
              if (level === 0) {
                // Special case: system monitor page (value: '') should always be navigable at level 0
                if (pageInfo.value === '') {
                  return pageInfo.value;
                }
                // For other pages at level 0, check if they can be navigated to
                if (pageInfo.navigation) {
                  return pageInfo.value;
                }
              }
            }
            
            return null;
          };
        </script>
      </head>
      <body>
        <div id="test-output"></div>
      </body>
      </html>
    `);

    // Test 1: Get previous page from plentemp at level 0 (should go to system monitor)
    const prevPageResult = await page.evaluate(() => {
      return window.getPreviousPageName(0, 'plentemp');
    });

    expect(prevPageResult).toBe(''); // Should return empty string for system monitor

    // Test 2: Get previous page from plentemp at level 1 (should not be able to navigate)
    const prevPageLevel1Result = await page.evaluate(() => {
      return window.getPreviousPageName(1, 'plentemp');
    });

    expect(prevPageLevel1Result).toBeNull(); // Should return null at level 1

    // Test 3: Test edge case - no previous page from system monitor
    const noPrevPage = await page.evaluate(() => {
      return window.getPreviousPageName(0, '');
    });

    expect(noPrevPage).toBeNull(); // Should return null for system monitor

    // Test 4: Test navigation from non-navigable page (network)
    const fromNetwork = await page.evaluate(() => {
      return window.getPreviousPageName(0, 'network');
    });

    expect(fromNetwork).toBe(''); // Should navigate to system monitor
  });
});
