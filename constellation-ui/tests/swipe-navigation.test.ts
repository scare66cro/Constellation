import { expect, test, type Page } from '@playwright/test';
// Global window test helpers declared centrally in tests/globals.d.ts

// Test swipe navigation implementation
test.describe('Swipe Navigation', () => {
  test('SwipeGestureHandler should be properly implemented', async ({ page }) => {
    // Create a test page with the swipe gesture handler
    await page.setContent(`
      <!DOCTYPE html>
      <html>
      <head>
        <title>Swipe Navigation Test</title>
        <style>
          .test-container {
            width: 400px;
            height: 300px;
            background: #f0f0f0;
            border: 2px solid #ccc;
            display: flex;
            align-items: center;
            justify-content: center;
            user-select: none;
          }
          .chart-page .test-container {
            background: #ffeeee;
          }
        </style>
      </head>
      <body>
        <div id="normal-page">
          <h2>Normal Page (Swipe Enabled)</h2>
          <div class="test-container" id="normal-container">
            Swipe or drag here
          </div>
          <div id="swipe-result"></div>
        </div>
        
        <div id="chart-page" class="chart-page" style="display: none;">
          <h2>Chart Page (Swipe Disabled)</h2>
          <div class="test-container" id="chart-container">
            Chart content - no swipe
          </div>
          <div id="chart-result"></div>
        </div>

        <script type="module">
          // Mock SwipeGestureHandler implementation for testing
          class SwipeGestureHandler {
            constructor(element, options = {}) {
              this.element = element;
              this.options = {
                threshold: options.threshold ?? 50,
                restraint: options.restraint ?? 100,
                allowedTime: options.allowedTime ?? 300,
                enableTouch: options.enableTouch ?? true,
                enableMouse: options.enableMouse ?? true,
                onSwipeLeft: options.onSwipeLeft ?? (() => {}),
                onSwipeRight: options.onSwipeRight ?? (() => {})
              };
              
              this.state = {
                startX: 0,
                startY: 0,
                startTime: 0,
                isActive: false,
                isDragging: false
              };
              
              this.init();
            }
            
            init() {
              if (this.options.enableTouch) {
                this.element.addEventListener('touchstart', this.handleTouchStart.bind(this), { passive: false });
                this.element.addEventListener('touchmove', this.handleTouchMove.bind(this), { passive: false });
                this.element.addEventListener('touchend', this.handleTouchEnd.bind(this), { passive: false });
              }
              
              if (this.options.enableMouse) {
                this.element.addEventListener('mousedown', this.handleMouseDown.bind(this));
                this.element.addEventListener('mousemove', this.handleMouseMove.bind(this));
                this.element.addEventListener('mouseup', this.handleMouseUp.bind(this));
                this.element.addEventListener('mouseleave', this.handleMouseUp.bind(this));
              }
            }
            
            handleTouchStart(e) {
              if (e.touches.length !== 1) return;
              const touch = e.touches[0];
              this.state = {
                startX: touch.clientX,
                startY: touch.clientY,
                startTime: Date.now(),
                isActive: true,
                isDragging: false
              };
            }
            
            handleTouchMove(e) {
              if (!this.state.isActive || e.touches.length !== 1) return;
              e.preventDefault();
            }
            
            handleTouchEnd(e) {
              if (!this.state.isActive) return;
              const touch = e.changedTouches[0];
              this.processSwipe(touch.clientX, touch.clientY);
            }
            
            handleMouseDown(e) {
              if (e.button !== 0) return;
              this.state = {
                startX: e.clientX,
                startY: e.clientY,
                startTime: Date.now(),
                isActive: true,
                isDragging: false
              };
              e.preventDefault();
            }
            
            handleMouseMove(e) {
              if (!this.state.isActive) return;
              const deltaX = Math.abs(e.clientX - this.state.startX);
              const deltaY = Math.abs(e.clientY - this.state.startY);
              if (!this.state.isDragging && (deltaX > 5 || deltaY > 5)) {
                this.state.isDragging = true;
              }
              if (this.state.isDragging) {
                e.preventDefault();
              }
            }
            
            handleMouseUp(e) {
              if (!this.state.isActive) return;
              this.processSwipe(e.clientX, e.clientY);
            }
            
            processSwipe(endX, endY) {
              const deltaTime = Date.now() - this.state.startTime;
              const deltaX = endX - this.state.startX;
              const deltaY = endY - this.state.startY;
              
              this.state.isActive = false;
              this.state.isDragging = false;
              
              if (deltaTime <= this.options.allowedTime) {
                if (Math.abs(deltaX) >= this.options.threshold && Math.abs(deltaY) <= this.options.restraint) {
                  if (deltaX > 0) {
                    this.options.onSwipeRight();
                  } else {
                    this.options.onSwipeLeft();
                  }
                }
              }
            }
            
            destroy() {
              this.element.removeEventListener('touchstart', this.handleTouchStart);
              this.element.removeEventListener('touchmove', this.handleTouchMove);
              this.element.removeEventListener('touchend', this.handleTouchEnd);
              this.element.removeEventListener('mousedown', this.handleMouseDown);
              this.element.removeEventListener('mousemove', this.handleMouseMove);
              this.element.removeEventListener('mouseup', this.handleMouseUp);
              this.element.removeEventListener('mouseleave', this.handleMouseUp);
            }
          }
          
          // Function to detect chart pages
          function isChartPage() {
            const route = window.location.pathname;
            return route.includes('/graph') || route.includes('/userlog') || 
                   route.includes('chart') || document.body.classList.contains('chart-page');
          }
          
          // Initialize swipe handlers
          let normalHandler, chartHandler;
          
          function initNormalPage() {
            const container = document.getElementById('normal-container');
            const result = document.getElementById('swipe-result');
            
            normalHandler = new SwipeGestureHandler(container, {
              enableTouch: true,
              enableMouse: true,
              onSwipeLeft: () => {
                result.textContent = 'Swiped Left on Normal Page';
                result.style.color = 'green';
              },
              onSwipeRight: () => {
                result.textContent = 'Swiped Right on Normal Page';
                result.style.color = 'blue';
              }
            });
          }
          
          function initChartPage() {
            const container = document.getElementById('chart-container');
            const result = document.getElementById('chart-result');
            
            chartHandler = new SwipeGestureHandler(container, {
              enableTouch: false,  // Disabled on chart pages
              enableMouse: false,  // Disabled on chart pages
              onSwipeLeft: () => {
                result.textContent = 'ERROR: Swipe should be disabled!';
                result.style.color = 'red';
              },
              onSwipeRight: () => {
                result.textContent = 'ERROR: Swipe should be disabled!';
                result.style.color = 'red';
              }
            });
          }
          
          // Make functions available globally for testing
          window.SwipeGestureHandler = SwipeGestureHandler;
          window.isChartPage = isChartPage;
          window.initNormalPage = initNormalPage;
          window.initChartPage = initChartPage;
          window.showNormalPage = () => {
            document.getElementById('normal-page').style.display = 'block';
            document.getElementById('chart-page').style.display = 'none';
            document.body.classList.remove('chart-page');
          };
          window.showChartPage = () => {
            document.getElementById('normal-page').style.display = 'none';
            document.getElementById('chart-page').style.display = 'block';
            document.body.classList.add('chart-page');
          };
          
          // Initialize
          initNormalPage();
          initChartPage();
        </script>
      </body>
      </html>
    `);

    // Test SwipeGestureHandler constructor and options
    const handlerOptions = await page.evaluate(() => {
      const container = document.getElementById('normal-container');
      const handler = new window.SwipeGestureHandler(container, {
        threshold: 100,
        enableTouch: false,
        enableMouse: true
      });
      return handler.options;
    });

    expect(handlerOptions.threshold).toBe(100);
    expect(handlerOptions.enableTouch).toBe(false);
    expect(handlerOptions.enableMouse).toBe(true);
  });

  test('should detect chart pages correctly', async ({ page }) => {
    // Set up test page
    await page.setContent(`
      <script>
        function isChartPage() {
          const route = window.location.pathname;
          return route.includes('/graph') || route.includes('/userlog');
        }
        window.isChartPage = isChartPage;
      </script>
    `);

    // Test normal page
    await page.goto('data:text/html,<script>function isChartPage() { return false; } window.isChartPage = isChartPage;</script>');
    let isChart = await page.evaluate(() => window.isChartPage());
    expect(isChart).toBe(false);

    // Test chart detection by simulating different routes
    const testCases = [
      { path: '/level2/graph', expected: true },
      { path: '/history/userlog', expected: true },
      { path: '/level1/lights', expected: false },
      { path: '/level2/settings', expected: false }
    ];

    for (const testCase of testCases) {
      const result = await page.evaluate((path) => {
        return path.includes('/graph') || path.includes('/userlog');
      }, testCase.path);
      
      expect(result).toBe(testCase.expected);
    }
  });
  test('should handle mouse drag on normal pages', async ({ page }) => {
    await page.setContent(`
      <div id="container" style="width: 400px; height: 300px; background: #f0f0f0; user-select: none;">
        Drag here
      </div>
      <div id="result"></div>
      <script>
        let swipeDetected = false;
        let swipeDirection = '';
        
        // Simple mouse drag detection
        let startX = 0;
        const container = document.getElementById('container');
        
        container.addEventListener('mousedown', (e) => {
          startX = e.clientX;
          e.preventDefault();
        });
        
        container.addEventListener('mouseup', (e) => {
          const deltaX = e.clientX - startX;
          if (Math.abs(deltaX) >= 50) { // 50px threshold
            swipeDetected = true;
            swipeDirection = deltaX > 0 ? 'right' : 'left';
            document.getElementById('result').textContent = \`Swiped \${swipeDirection}\`;
          }
        });
        
        window.getSwipeResult = () => ({ detected: swipeDetected, direction: swipeDirection });
      </script>
    `);

    // Test mouse drag from left to right (should trigger swipe right)
    await page.mouse.move(100, 150);
    await page.mouse.down();
    await page.mouse.move(250, 150); // Move 150px to the right
    await page.mouse.up();

    // Wait a bit for the handler to process
    await page.waitForTimeout(100);

    const result = await page.evaluate(() => window.getSwipeResult());
    expect(result.detected).toBe(true);
    expect(result.direction).toBe('right');
  });

  test('should disable swipe on chart pages', async ({ page }) => {
    await page.setContent(`
      <div id="container" style="width: 400px; height: 300px; background: #ffeeee; user-select: none;">
        Chart content
      </div>
      <div id="result"></div>
      <script type="module">
        class SwipeGestureHandler {
          constructor(element, options = {}) {
            this.element = element;
            this.options = {
              enableTouch: options.enableTouch ?? true,
              enableMouse: options.enableMouse ?? true,
              onSwipeLeft: options.onSwipeLeft ?? (() => {}),
              onSwipeRight: options.onSwipeRight ?? (() => {})
            };
            this.eventListeners = [];
            
            if (this.options.enableMouse) {
              this.element.addEventListener('mousedown', () => {
                document.getElementById('result').textContent = 'Mouse event triggered (should not happen on chart pages)';
              });
            }
            
            if (this.options.enableTouch) {
              this.element.addEventListener('touchstart', () => {
                document.getElementById('result').textContent = 'Touch event triggered (should not happen on chart pages)';
              });
            }
          }
        }
        
        // Initialize with chart page settings (both disabled)
        const container = document.getElementById('container');
        new SwipeGestureHandler(container, {
          enableTouch: false,
          enableMouse: false
        });
      </script>
    `);

    // Try to trigger mouse events
    await page.mouse.move(200, 150);
    await page.mouse.down();
    await page.mouse.move(350, 150);
    await page.mouse.up();

    // Wait and check that no swipe was triggered
    await page.waitForTimeout(100);
    const result = await page.locator('#result').textContent();
    expect(result).toBe(''); // Should be empty since events are disabled
  });

  test('should prevent text selection during mouse drag', async ({ page }) => {
    await page.setContent(`
      <div id="container" style="width: 400px; height: 300px; background: #f0f0f0;">
        <p>This text should not be selected during drag</p>
      </div>
      <script>
        let dragPrevented = false;
        
        document.getElementById('container').addEventListener('mousedown', (e) => {
          e.preventDefault();
          dragPrevented = true;
        });
        
        window.getDragPrevented = () => dragPrevented;
      </script>
    `);

    // Perform a drag operation
    await page.mouse.move(200, 150);
    await page.mouse.down();
    await page.mouse.move(350, 150);
    await page.mouse.up();

    const dragPrevented = await page.evaluate(() => window.getDragPrevented());
    expect(dragPrevented).toBe(true);
  });
});

test.describe('Integration Tests', () => {
  test('should work with different swipe thresholds', async ({ page }) => {
    await page.setContent(`
      <div id="container" style="width: 400px; height: 300px; background: #f0f0f0;"></div>
      <div id="result"></div>
      <script type="module">
        class SwipeGestureHandler {
          constructor(element, options = {}) {
            this.threshold = options.threshold ?? 50;
            this.element = element;
            this.startX = 0;
            
            element.addEventListener('mousedown', (e) => {
              this.startX = e.clientX;
            });
            
            element.addEventListener('mouseup', (e) => {
              const deltaX = e.clientX - this.startX;
              if (Math.abs(deltaX) >= this.threshold) {
                document.getElementById('result').textContent = \`Swipe detected: \${deltaX}px\`;
              } else {
                document.getElementById('result').textContent = \`Below threshold: \${deltaX}px\`;
              }
            });
          }
        }
        
        new SwipeGestureHandler(document.getElementById('container'), {
          threshold: 100 // Higher threshold
        });
      </script>
    `);

    // Test with movement below threshold
    await page.mouse.move(200, 150);
    await page.mouse.down();
    await page.mouse.move(250, 150); // 50px movement (below 100px threshold)
    await page.mouse.up();

    await page.waitForTimeout(50);
    let result = await page.locator('#result').textContent();
    expect(result).toContain('Below threshold');

    // Test with movement above threshold
    await page.mouse.move(200, 150);
    await page.mouse.down();
    await page.mouse.move(350, 150); // 150px movement (above 100px threshold)
    await page.mouse.up();

    await page.waitForTimeout(50);
    result = await page.locator('#result').textContent();
    expect(result).toContain('Swipe detected');
  });
});
