import type { DataSelection } from '$lib/store';
import { get, type Writable } from 'svelte/store';
import { getHttpUrl } from './util';

/**
 * Load the last saved graph favorites from the server and apply them
 */
export async function loadGraphFavorites(dataSelectionStore: Writable<DataSelection>): Promise<boolean> {
  try {
    const response = await fetch(getHttpUrl('/iot/graph/favorites'));
    
    if (!response.ok) {
      throw new Error(`HTTP error! status: ${response.status}`);
    }
    
    // Check if response has content before parsing JSON
    const text = await response.text();
    if (!text || text.trim() === '') {
      return false;
    }
    
    const data = JSON.parse(text);
    
    // Expect data to be an array of selection values or comma-separated string
    let selections: string[] = [];
    
    if (typeof data === 'string') {
      selections = data.split(',').filter((s: string) => s.trim());
    } else if (Array.isArray(data)) {
      selections = data;
    } else if (data && data.favoriteSelections) {
      selections = typeof data.favoriteSelections === 'string' 
        ? data.favoriteSelections.split(',').filter((s: string) => s.trim())
        : data.favoriteSelections;
    }
    
    if (selections.length > 0) {
      applySelections(selections, dataSelectionStore);
      return true;
    }
    
    return false;
  } catch (error) {
    console.error('Error loading graph favorites:', error);
    return false;
  }
}

/**
 * Save the current graph selection as favorites
 */
export async function saveGraphFavorites(dataSelectionStore: Writable<DataSelection>): Promise<boolean> {
  try {
    const currentDataSelection = get(dataSelectionStore);
    
    // Get currently selected items
    const selectedItems: string[] = [];
    currentDataSelection.selected.forEach((isSelected, index) => {
      if (isSelected && currentDataSelection.selections[index]) {
        selectedItems.push(currentDataSelection.selections[index].value);
      }
    });
    
    if (selectedItems.length === 0) {
      console.warn('No items selected to save as favorites');
      return false;
    }

    const response = await fetch(getHttpUrl('/iot/graph/favorites'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({ GraphFavorites: selectedItems.join(',') }),
    });
    
    if (!response.ok) {
      throw new Error(`HTTP error! status: ${response.status}`);
    }
    
    // Check if server returned any response data
    const text = await response.text();
    if (text && text.trim() !== '') {
      try {
        const result = JSON.parse(text);
      } catch (e) {
        // Non-JSON response is okay for save operations
        console.log('Save response (non-JSON):', text);
      }
    }
    
    return true;
  } catch (error) {
    console.error('Error saving graph favorites:', error);
    return false;
  }
}

/**
 * Apply selections to the data selection store
 */
function applySelections(selections: string[], dataSelectionStore: Writable<DataSelection>): void {
  const currentDataSelection = get(dataSelectionStore);
  
  // Reset all selections
  const newSelected = new Array(currentDataSelection.selections.length).fill(false);
  
  // Set selections based on favorites
  selections.forEach(selection => {
    const index = currentDataSelection.selections.findIndex(item => item.value === selection);
    if (index !== -1) {
      newSelected[index] = true;
    }
  });
  
  // Update the store
  dataSelectionStore.update(store => ({
    ...store,
    selected: newSelected
  }));
}
