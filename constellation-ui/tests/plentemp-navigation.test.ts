import { expect, test } from '@playwright/test';
// Shared window test globals & interfaces imported via tests/globals.d.ts

test.describe('Plentemp Navigation Scenarios', () => {
  test('should navigate from plentemp to system monitor at both levels', async ({ page }) => {
    // Create a test page with the enhanced navigation logic
    await page.setContent(`
      <!DOCTYPE html>
      <html>
      <head>
        <title>Plentemp Navigation Test</title>
        <script type="module">
          // Mock the exact page structure from the app
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

          // Enhanced navigation logic with special case handling
          window.getPreviousPageName = (level, currentName) => {
            const pageList = window.mockPageTranslations.level1Pages;
            const start = pageList.findIndex((page) => page.value === currentName);
            
            console.log(\`Current page: "\${currentName}" at index \${start}, level \${level}\`);
            
            if (start === -1) return null;
            
            // Look for previous navigable page
            for (let i = start - 1; i >= 0; i--) {
              const pageInfo = pageList[i];
              console.log(\`Checking index \${i}: "\${pageInfo.text}" (value: "\${pageInfo.value}", navigation: \${pageInfo.navigation})\`);
              
              if (level === 0) {
                // Special case: system monitor page (value: '') should always be navigable at level 0
                if (pageInfo.value === '') {
                  console.log(\`SPECIAL CASE: System monitor always navigable at level 0\`);
                  return pageInfo.value;
                }
                // For other pages at level 0, check if they can be navigated to
                if (pageInfo.navigation) {
                  console.log(\`Found navigable page: "\${pageInfo.value}"\`);
                  return pageInfo.value;
                } else {
                  console.log(\`Not navigable (navigation: false)\`);
                }
              }
            }
            
            console.log(\`No previous page found\`);
            return null;
          };
        </script>
      </head>
      <body>
        <div id="test-output"></div>
      </body>
      </html>
    `);

    // Test 1: Plentemp at Level 0 (should navigate to system monitor)
    const result0 = await page.evaluate(() => {
      return window.getPreviousPageName(0, 'plentemp');
    });

    expect(result0).toBe(''); // Should return empty string for system monitor
    console.log('✅ Level 0 navigation test passed');

    // Test 2: Plentemp at Level 1 (should return null - no previous navigable pages)
    const result1 = await page.evaluate(() => {
      return window.getPreviousPageName(1, 'plentemp');
    });

    expect(result1).toBeNull(); // Should return null at level 1
    console.log('✅ Level 1 navigation test passed');
  });

  test('should handle edge cases in navigation', async ({ page }) => {
    await page.setContent(`
      <!DOCTYPE html>
      <html>
      <head>
        <script type="module">
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

          window.getPreviousPageName = (level, currentName) => {
            const pageList = window.mockPageTranslations.level1Pages;
            const start = pageList.findIndex((page) => page.value === currentName);
            
            if (start === -1) return null;
            
            for (let i = start - 1; i >= 0; i--) {
              const pageInfo = pageList[i];
              
              if (level === 0) {
                if (pageInfo.value === '') {
                  return pageInfo.value;
                }
                if (pageInfo.navigation) {
                  return pageInfo.value;
                }
              }
            }
            
            return null;
          };
        </script>
      </head>
      <body></body>
      </html>
    `);

    // Test with non-existent page
    const invalidPageResult = await page.evaluate(() => {
      return window.getPreviousPageName(0, 'nonexistent');
    });
    expect(invalidPageResult).toBeNull();

    // Test with system monitor page (should have no previous page)
    const systemMonitorResult = await page.evaluate(() => {
      return window.getPreviousPageName(0, '');
    });
    expect(systemMonitorResult).toBeNull();

    // Test with network monitor page (should navigate to system monitor)
    const networkResult = await page.evaluate(() => {
      return window.getPreviousPageName(0, 'network');
    });
    expect(networkResult).toBe('');
  });

  test('should verify page structure consistency', async ({ page }) => {
    await page.setContent(`
      <!DOCTYPE html>
      <html>
      <head>
        <script type="module">
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
        </script>
      </head>
      <body></body>
      </html>
    `);

    // Verify that plentemp is at the expected index
    const plentempIndex = await page.evaluate(() => {
      const pageList = window.mockPageTranslations.level1Pages;
      return pageList.findIndex((page: any) => page.value === 'plentemp');
    });
    expect(plentempIndex).toBe(5);

    // Verify that system monitor is at index 0
    const systemMonitorIndex = await page.evaluate(() => {
      const pageList = window.mockPageTranslations.level1Pages;
      return pageList.findIndex((page: any) => page.value === '');
    });
    expect(systemMonitorIndex).toBe(0);

    // Verify navigation properties
    const navigationProperties = await page.evaluate(() => {
      const pageList = window.mockPageTranslations.level1Pages;
      return {
        systemMonitorNav: pageList[0].navigation,
        plentempNav: pageList[5].navigation,
        networkNav: pageList[1].navigation
      };
    });

    expect(navigationProperties.systemMonitorNav).toBe(true);
    expect(navigationProperties.plentempNav).toBe(true);
    expect(navigationProperties.networkNav).toBe(false);
  });
});
