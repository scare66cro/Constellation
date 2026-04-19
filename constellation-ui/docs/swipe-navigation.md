# Swipe Navigation for GellertPage

This implementation adds swipe gesture navigation to the GellertPage component, allowing users to navigate between pages by swiping left or right on both touch devices and with mouse drag gestures.

## Features

- **Touch Gesture Support**: Detects horizontal swipe gestures on touch devices (disabled on chart pages)
- **Mouse Drag Support**: Detects mouse drag gestures for desktop navigation (disabled on chart pages)
- **Smart Page Detection**: Automatically disables both touch and mouse swipe on chart/graph pages to avoid conflicts
- **Browser History Override**: Prevents browser's native back/forward navigation during swipes
- **Visual Feedback**: Subtle arrow indicators show swipe availability
- **Page Sequence Navigation**: Navigates through filterable pages in the correct order
- **Accessibility**: Works alongside existing navigation methods

## How it Works

### Swipe Gestures

- **Swipe Left** (Touch/Mouse): Navigate to the next page in the sequence
- **Swipe Right** (Touch/Mouse): Navigate to the previous page in the sequence
- **Threshold**: 50px minimum swipe distance required
- **Restraint**: 100px maximum vertical movement allowed
- **Timing**: 300ms maximum swipe duration

### Mouse Drag Gestures

Mouse swipe navigation works similarly to touch:

- **Left Click + Drag**: Hold left mouse button and drag horizontally
- **Automatic Detection**: Distinguishes between clicks and drags
- **Text Selection Prevention**: Prevents text selection during drag gestures
- **Chart Page Exclusion**: Automatically disabled on pages containing charts or graphs

### Chart Page Detection

Both touch and mouse swipe navigation are automatically disabled on pages that contain charts to prevent conflicts with chart interactions:

- Detects routes containing `/graph` or `/userlog`
- Checks page names for 'graph' or 'userlog'
- Completely disables swipe navigation on these pages

### Page Navigation Logic

The navigation uses the same logic as the footer navigation through the FooterNavigationAdapter:

1. Gets filtered and navigable pages for the current level using `canNavigateToPage`
2. Finds the current page index in the sequence
3. Navigates to the next/previous page if available
4. Updates the navigation store (including dropDownPage) and browser URL
5. Ensures consistent navigation behavior across swipe and button navigation

### Browser History Override

The implementation prevents the browser's native swipe-to-navigate feature by:

- Using `preventDefault()` on touch events when swipe navigation is possible
- Setting `touch-action: pan-x` CSS property to control touch behavior
- Only preventing default when at pages that support swipe navigation

## Files Structure

```
src/lib/
├── components/
│   └── GellertPage.svelte          # Main component with swipe integration
├── utils/
│   ├── swipeGesture.ts            # Reusable swipe gesture handler
│   └── pageNavigator.ts           # Page navigation logic
```

## Components

### SwipeGestureHandler (`swipeGesture.ts`)

A reusable class that handles touch gesture detection:

```typescript
new SwipeGestureHandler(element, {
  threshold: 50,         // Minimum swipe distance
  restraint: 100,        // Maximum perpendicular movement
  allowedTime: 300,      // Maximum swipe duration
  enableTouch: true,     // Enable touch swipe gestures
  enableMouse: true,     // Enable mouse drag gestures
  onSwipeLeft: () => {},   // Left swipe callback
  onSwipeRight: () => {}  // Right swipe callback
});
```

### FooterNavigationAdapter (`footerNavigationAdapter.ts`)

Handles the navigation logic using the same implementation as the footer navigation buttons:

- `getNextPageName(level, currentName)`: Gets the next navigable page name
- `getPreviousPageName(level, currentName)`: Gets the previous navigable page name
- `getNextPage(level, currentName)`: Gets next page navigation info
- `getPreviousPage(level, currentName)`: Gets previous page navigation info
- `navigateToNext(level, currentName)`: Performs next page navigation
- `navigateToPrevious(level, currentName)`: Performs previous page navigation
- `canNavigateNext(level, currentName)`: Checks if next navigation is possible
- `canNavigatePrevious(level, currentName)`: Checks if previous navigation is possible

This ensures consistency between swipe navigation and footer button navigation.

## Usage

The swipe navigation is automatically enabled on any page using the `GellertPage` component. No additional setup is required.

### Customization

You can customize the swipe behavior by modifying the options in `GellertPage.svelte`:

```svelte
use:swipe={{
  threshold: 50,       // Adjust sensitivity
  restraint: 100,      // Adjust vertical tolerance
  allowedTime: 300,    // Adjust timing requirement
  enableTouch: !isChartPage(),  // Disable touch on chart pages
  enableMouse: !isChartPage(),  // Disable mouse on chart pages
  onSwipeLeft: handleSwipeLeft,
  onSwipeRight: handleSwipeRight
}}
```

## Chart Page Detection Logic

The `isChartPage()` function automatically detects pages that contain charts to disable mouse swipe navigation:

```typescript
function isChartPage(): boolean {
  const route = $page.route.id;
  const pathname = $page.url.pathname;
  
  return (
    route?.includes('/graph') || 
    route?.includes('/userlog') ||
    pathname.includes('/graph') ||
    pathname.includes('/userlog') ||
    name === 'graph' ||
    name === 'userlog'
  );
}
```

This prevents mouse drag conflicts with chart interactions while preserving touch swipe navigation.

## Visual Indicators

The implementation includes subtle visual indicators:

- Small arrow icons appear on hover (desktop) or always visible (touch devices)
- `touch-action: pan-x` CSS property provides system-level touch guidance
- Page indicator shows current position in the sequence

## Implementation Summary

### ✅ Completed Features

1. **Touch Swipe Navigation**: 
   - Horizontal swipe gestures on touch devices
   - Prevents browser back/forward navigation
   - Configurable thresholds and timing

2. **Mouse Drag Navigation**:
   - Mouse drag gestures for desktop users
   - Prevents text selection during drag
   - Smart detection between clicks and drags

3. **Chart Page Exclusion**:
   - Automatically detects chart/graph pages
   - Disables both touch and mouse swipe on chart pages to prevent conflicts
   - Maintains normal browser navigation on chart pages

4. **Visual Feedback**:
   - Subtle arrow indicators for swipe availability
   - Page position indicators
   - CSS touch-action optimization

5. **Browser Compatibility**:
   - iOS Safari with history override
   - Chrome, Firefox, Edge support
   - Touch and mouse event handling

### 🔧 Technical Details

- **Files Modified**: `GellertPage.svelte`, new utility files in `src/lib/utils/`
- **Build Status**: ✅ Successful build with no new errors
- **Type Safety**: ✅ TypeScript compatible
- **Testing**: ✅ Playwright test suite in `tests/swipe-navigation.test.ts`

### 🚀 Usage

The swipe navigation is automatically enabled on all pages using `GellertPage.svelte`. Mouse swipe is intelligently disabled on chart pages while touch swipe remains functional everywhere.

### 🧪 Testing

Comprehensive Playwright tests are available in `tests/swipe-navigation.test.ts`:

```bash
# Run swipe navigation tests
npx playwright test swipe-navigation.test.ts

# Run tests with headed browser (visual)
npx playwright test swipe-navigation.test.ts --headed
```

**Test Coverage:**
- SwipeGestureHandler constructor and options
- Chart page detection logic
- Mouse drag gesture handling
- Touch and mouse swipe disabling on chart pages
- Text selection prevention during drag
- Different swipe thresholds

## Browser Compatibility

- **iOS Safari**: Full support with browser navigation override
- **Chrome Mobile**: Full support with browser navigation override
- **Firefox Mobile**: Full support with browser navigation override
- **Desktop**: Hover indicators show swipe availability (though swiping requires touch)

## Accessibility Considerations

- Swipe navigation supplements existing navigation methods
- Keyboard navigation and clicking remain fully functional
- Visual indicators help users understand swipe availability
- No impact on screen readers or assistive technologies

## Future Enhancements

Potential improvements could include:

1. **Animation Feedback**: Visual transitions during swipe gestures
2. **Haptic Feedback**: Vibration on successful navigation (where supported)
3. **Swipe Preview**: Show preview of next/previous page during swipe
4. **Configurable Sensitivity**: Per-device swipe sensitivity adjustment
5. **Gesture Analytics**: Track swipe usage patterns
