/**
 * Time Utilities for Gellert UI-Svelte Frontend
 * 
 * IMPORTANT: Always use controller time instead of the browser's/Pi's system time.
 * The controller's RTC is the source of truth for all time-related operations.
 * 
 * The controller provides time via Polling frontmatter data in the format:
 * DateTime: ["MM/DD/YYYY", "HH:MM:SS", "AM/PM"]
 * Example: ["10/19/2021", "3:18:24", "PM"]
 */

import { get } from 'svelte/store';
import { frontMatterStore } from '../store';

/**
 * Parses controller DateTime data from frontmatter
 * @param dateTimeData Array containing [date, time, ampm] from controller
 * @returns Unix timestamp in milliseconds, or null if unavailable
 */
export function parseControllerTime(dateTimeData?: string[]): number | null {
  if (!dateTimeData || dateTimeData.length < 2) {
    return null;
  }

  try {
    const [datePart, timePart, ampm] = dateTimeData;
    
    // Parse date (MM/DD/YYYY format)
    const [month, day, year] = datePart.split('/').map(Number);
    
    // Parse time (HH:MM:SS format)
    let [hours, minutes, seconds] = timePart.split(':').map(Number);
    
    // Handle AM/PM if present
    if (ampm) {
      if (ampm.toUpperCase() === 'PM' && hours < 12) {
        hours += 12;
      } else if (ampm.toUpperCase() === 'AM' && hours === 12) {
        hours = 0;
      }
    }
    
    // Create Date object (months are 0-indexed in JavaScript)
    const controllerDate = new Date(year, month - 1, day, hours, minutes, seconds);
    
    // Validate the date
    if (isNaN(controllerDate.getTime())) {
      return null;
    }
    
    return controllerDate.getTime();
  } catch (error) {
    console.error(`Error parsing controller datetime: ${(error as Error).message}`);
    return null;
  }
}

/**
 * Gets the current controller timestamp from frontmatter store
 * @returns Unix timestamp in milliseconds from controller, or system time as fallback
 */
export function getControllerTimestamp(): number {
  try {
    const frontMatter = get(frontMatterStore);
    const dateTimeData = frontMatter?.DateTime;
    
    // Ensure dateTimeData is an array
    const dateTimeArray = Array.isArray(dateTimeData) ? dateTimeData : undefined;
    const controllerTime = parseControllerTime(dateTimeArray);
    
    if (controllerTime === null) {
      console.warn('Controller time unavailable in frontmatter, falling back to system time');
      return Date.now();
    }
    
    return controllerTime;
  } catch (error) {
    console.error(`Error accessing controller time: ${(error as Error).message}`);
    return Date.now();
  }
}

/**
 * Gets the current controller time as a Date object
 * @returns Date object using controller time, or system time as fallback
 */
export function getControllerDate(): Date {
  return new Date(getControllerTimestamp());
}

/**
 * Formats controller time according to controller's date format
 * @param timestamp Optional timestamp (defaults to current controller time)
 * @returns Formatted string in MM/DD/YYYY HH:MM:SS format
 */
export function formatControllerTime(timestamp?: number): string {
  const time = timestamp || getControllerTimestamp();
  const date = new Date(time);
  
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const year = date.getFullYear();
  const hours = String(date.getHours()).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');
  const seconds = String(date.getSeconds()).padStart(2, '0');
  
  return `${month}/${day}/${year} ${hours}:${minutes}:${seconds}`;
}

/**
 * Formats controller time for display with AM/PM
 * @param timestamp Optional timestamp (defaults to current controller time)
 * @returns Formatted string in MM/DD/YYYY HH:MM:SS AM/PM format
 */
export function formatControllerTimeWithAMPM(timestamp?: number): string {
  const time = timestamp || getControllerTimestamp();
  const date = new Date(time);
  
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const day = String(date.getDate()).padStart(2, '0');
  const year = date.getFullYear();
  
  let hours = date.getHours();
  const ampm = hours >= 12 ? 'PM' : 'AM';
  hours = hours % 12 || 12; // Convert to 12-hour format
  
  const hoursStr = String(hours).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');
  const seconds = String(date.getSeconds()).padStart(2, '0');
  
  return `${month}/${day}/${year} ${hoursStr}:${minutes}:${seconds} ${ampm}`;
}

/**
 * DEPRECATED: Use getControllerTimestamp() instead
 * This function is kept for backward compatibility but should be phased out
 */
export function now(): number {
  console.warn('now() is deprecated. Use getControllerTimestamp() instead.');
  return getControllerTimestamp();
}
