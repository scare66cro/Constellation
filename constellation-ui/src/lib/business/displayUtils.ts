import { get } from 'svelte/store';
import { getHttpUrl, safeJsonParse } from './util';
import { format } from 'date-fns';

export interface DisplayOption {
  text: string;
  value: string;
}

export interface DisplayFetchResult {
  success: boolean;
  displays: DisplayOption[];
  filename: string;
  error?: string;
}

/**
 * Fetches available displays from the IoT endpoint and formats them for dropdown selection
 * @param logType - Type of log for filename generation (e.g., 'HistoryLog', 'ActivityLog')
 * @returns Promise with display options and generated filename
 */
export async function fetchDisplayOptions(logType: string = 'HistoryLog'): Promise<DisplayFetchResult> {
  const defaultFilename = `${logType}_${format(new Date(), 'MM-dd-yyyy_HH-mm')}`;
  
  try {
    const response = await fetch(getHttpUrl('/iot/displays'));
    const displays = await safeJsonParse(response);
    
    // Validate response structure
    if (!displays || !displays.data || !Array.isArray(displays.data.DisplayList)) {
      console.error('Invalid displays data structure:', displays);
      return {
        success: false,
        displays: [{ text: 'Default Display', value: 'default' }],
        filename: defaultFilename,
        error: 'Invalid displays data structure'
      };
    }
    
    const displayList = displays.data.DisplayList;
    
    // Handle empty display list
    if (displayList.length === 0) {
      console.warn('No displays found in DisplayList');
      return {
        success: false,
        displays: [{ text: 'No displays available', value: 'none' }],
        filename: defaultFilename,
        error: 'No displays found'
      };
    }
    
    // Process displays with proper bounds checking
    const displayOptions: DisplayOption[] = [];
    
    for (let i = 0; i < displayList.length; i += 5) {
      // Ensure we have enough elements for the expected structure
      if (i + 2 < displayList.length && displayList[i] && displayList[i + 2]) {
        displayOptions.push({
          text: `${displayList[i]} ${displayList[i + 2]}`,
          value: displayList[i]
        });
      } else {
        console.warn(`Insufficient display data at index ${i}:`, displayList.slice(i, i + 5));
        // Continue processing other displays even if one is malformed
      }
    }
    
    // Ensure we have at least one valid display option
    if (displayOptions.length === 0) {
      console.warn('No valid displays could be processed');
      return {
        success: false,
        displays: [{ text: 'No valid displays', value: 'none' }],
        filename: defaultFilename,
        error: 'No valid displays found'
      };
    }
    
    return {
      success: true,
      displays: displayOptions,
      filename: defaultFilename
    };
    
  } catch (error) {
    console.error('Error fetching displays:', error);
    return {
      success: false,
      displays: [{ text: 'Default Display', value: 'default' }],
      filename: defaultFilename,
      error: error instanceof Error ? error.message : 'Unknown error'
    };
  }
}

/**
 * Validates display data structure for backward compatibility
 * @param displays - Raw display data from server
 * @returns boolean indicating if structure is valid
 */
export function validateDisplayData(displays: any): boolean {
  return !!(
    displays &&
    displays.data &&
    Array.isArray(displays.data.DisplayList) &&
    displays.data.DisplayList.length > 0
  );
}

/**
 * Safely processes display list with bounds checking
 * @param displayList - Array of display data
 * @returns Array of processed display options
 */
export function processDisplayList(displayList: any[]): DisplayOption[] {
  const options: DisplayOption[] = [];
  
  for (let i = 0; i < displayList.length; i += 5) {
    // Check if we have the minimum required elements
    if (i < displayList.length && displayList[i]) {
      const name = displayList[i];
      const description = (i + 2 < displayList.length) ? displayList[i + 2] : '';
      
      options.push({
        text: description ? `${name} ${description}` : name,
        value: name
      });
    }
  }
  
  return options;
}
