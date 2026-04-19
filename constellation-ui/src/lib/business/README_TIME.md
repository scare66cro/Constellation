# Time Utilities

**⚠️ IMPORTANT: Always use controller time, never browser's system time!**

## Quick Start

```typescript
import { getControllerTimestamp, getControllerDate, formatControllerTime } from '$lib/business/timeUtils';

// Get current timestamp (milliseconds)
const timestamp = getControllerTimestamp();

// Get Date object
const date = getControllerDate();

// Format for display (24-hour)
const formatted = formatControllerTime();
// Output: "10/19/2021 15:18:24"

// Format with AM/PM
import { formatControllerTimeWithAMPM } from '$lib/business/timeUtils';
const formattedAMPM = formatControllerTimeWithAMPM();
// Output: "10/19/2021 03:18:24 PM"
```

## In Svelte Components

```svelte
<script lang="ts">
  import { getControllerTimestamp } from '$lib/business/timeUtils';
  
  let displayTime = '';
  
  $: {
    // Gets controller time from frontMatterStore
    const timestamp = getControllerTimestamp();
    displayTime = new Date(timestamp).toLocaleTimeString();
  }
</script>

<div>{displayTime}</div>
```

## Why?

The TI Tiva controller's RTC is the authoritative time source. The browser's time can be:
- Incorrect due to user settings
- Out of sync with the controller
- Different across multiple client devices

## Functions

### `getControllerTimestamp(): number`
Returns current controller time from `frontMatterStore` as milliseconds since epoch. Falls back to system time if unavailable (with warning).

### `getControllerDate(): Date`
Returns a Date object representing the current controller time.

### `parseControllerTime(dateTimeData?: string[]): number | null`
Parses controller DateTime data in format `["MM/DD/YYYY", "HH:MM:SS", "AM/PM"]`. Returns `null` if parsing fails.

### `formatControllerTime(timestamp?: number): string`
Formats a timestamp (or current controller time) as "MM/DD/YYYY HH:MM:SS" (24-hour format).

### `formatControllerTimeWithAMPM(timestamp?: number): string`
Formats a timestamp (or current controller time) as "MM/DD/YYYY HH:MM:SS AM/PM" (12-hour format).

## ESLint Protection

ESLint is configured to prevent direct usage of:
- `Date.now()` → Use `getControllerTimestamp()` instead
- `new Date()` → Use `getControllerDate()` instead

## Full Documentation

See `/docs/CONTROLLER_TIME_USAGE.md` in the Gellert workspace for comprehensive guidelines.
