// Location Autocomplete JavaScript for Django Admin

(function() { // IIFE to avoid polluting global scope
    'use strict';

    function initializeAllAutocompleteWidgets(jQuery) {
        if (!jQuery) {
            console.error("Location Autocomplete: django.jQuery is not available.");
            return;
        }
        // Initialize autocomplete for location fields
        jQuery('.location-autocomplete').each(function(index) { // Added index for unique logging
            var $input = jQuery(this);
            var $container = jQuery('<div class="location-autocomplete-container"></div>');
            var $dropdown = jQuery('<ul class="location-autocomplete-dropdown"></ul>');
            var $toggleButton = jQuery('<button type="button" class="location-autocomplete-toggle">▼</button>');
            
            // Prevent re-initialization if already done
            if ($input.parent().hasClass('location-autocomplete-container')) {
                console.log('Widget ' + index + ': Autocomplete already initialized for input:', $input[0]);
                return; // Skip
            }
            
            $input.wrap($container);
            // $container is now $input.parent()
            $input.after($dropdown);
            $input.after($toggleButton); // DOM order: input, toggleButton, dropdown
              var currentSelection = -1;
            var lastQuery = '';
            var searchTimeout;
            var allLocations = [];
            var isDropdownOpen = false;
            var isUserTyping = false; // Track if user is actively typing
            var widgetId = 'widget-' + index; // For unique console logs per widget

            console.log(widgetId + ': Initializing autocomplete for input:', $input[0]);
            
            // Hide dropdown initially
            $dropdown.hide();
            
            // Load all locations on initialization
            loadAllLocations();
              // Handle dropdown toggle button click
            $toggleButton.on('click', function(e) {
                e.preventDefault();
                e.stopPropagation();
                console.log(widgetId + ': Toggle button clicked. isDropdownOpen:', isDropdownOpen);
                if (isDropdownOpen) {
                    $dropdown.hide();
                    isDropdownOpen = false;
                    console.log(widgetId + ': Dropdown hidden by toggle. isDropdownOpen set to false.');
                } else {
                    // Always show all locations when toggle button is clicked
                    console.log(widgetId + ': Toggle button clicked, showing all locations.');
                    displayResults(allLocations, $dropdown, $input);
                    if ($dropdown.is(':visible')) { // Check if displayResults actually showed it
                        isDropdownOpen = true;
                        console.log(widgetId + ': Dropdown shown by toggle. isDropdownOpen set to true.');
                    }
                }
            });
              // Handle input focus
            $input.on('focus', function() {
                console.log(widgetId + ': Input focused. isDropdownOpen:', isDropdownOpen);
                if (isDropdownOpen) {
                    console.log(widgetId + ': Dropdown already considered open, focus handler doing nothing more.');
                    return;
                }

                if (allLocations.length > 0) {
                    console.log(widgetId + ': Locations available on focus, displaying all results.');
                    // Always show all locations on focus, regardless of current value
                    displayResults(allLocations, $dropdown, $input);
                } else {
                    console.log(widgetId + ': Locations not loaded or empty on focus, showing loading message.');
                    showLoadingMessage($dropdown); // Show "Loading..."
                }
                // Set isDropdownOpen only if the dropdown was actually made visible
                if ($dropdown.is(':visible')) {
                    isDropdownOpen = true;
                    console.log(widgetId + ': Set isDropdownOpen = true in focus handler because dropdown is visible.');
                }
            });            // Handle input events for filtering
            $input.on('input', function() {
                var query = jQuery(this).val().trim();
                console.log(widgetId + ': Input event. Query:"' + query + '"');
                
                // Mark that user is actively typing
                isUserTyping = true;
                
                // Clear previous selection
                currentSelection = -1;
                
                if (searchTimeout) {
                    clearTimeout(searchTimeout);
                }
                
                if (query.length === 0) {
                    console.log(widgetId + ': Input query empty, displaying all available locations.');
                    displayResults(allLocations, $dropdown, $input);
                } else {
                    console.log(widgetId + ': Input query present, filtering locations locally.');
                    var filtered = filterLocationsLocally(query, allLocations);
                    displayResults(filtered, $dropdown, $input);
                }
                
                // Set isDropdownOpen only if the dropdown was actually made visible
                if ($dropdown.is(':visible')) {
                    isDropdownOpen = true;
                    console.log(widgetId + ': Set isDropdownOpen = true in input handler because dropdown is visible.');
                }
            });
              // Handle keyboard navigation and field clearing
            $input.on('keydown', function(e) {
                // If user is not currently typing and starts typing a character, clear the field first
                if (!isUserTyping && !e.ctrlKey && !e.altKey && !e.metaKey) {
                    // Check if it's a printable character (letters, numbers, space, etc.)
                    var isPrintable = (e.keyCode >= 32 && e.keyCode <= 126) || 
                                     (e.keyCode >= 128 && e.keyCode <= 255) ||
                                     e.keyCode === 8 || // Backspace
                                     e.keyCode === 46;  // Delete
                    
                    if (isPrintable && e.keyCode !== 9 && e.keyCode !== 13 && e.keyCode !== 27) { // Not Tab, Enter, or Escape
                        console.log(widgetId + ': User starting to type, clearing field before character entry.');
                        $input.val(''); // Clear the field before the character is typed
                        isUserTyping = true; // Mark as actively typing
                    }
                }
                
                var $items = $dropdown.find('li:not(.empty-message, .loading-message)'); // Exclude non-selectable items
                if (!$items.length) return;

                switch(e.keyCode) {
                    case 38: // Up arrow
                        e.preventDefault();
                        if (currentSelection > 0) {
                            currentSelection--;
                        } else {
                            currentSelection = $items.length - 1; // Wrap around
                        }
                        updateSelection($items, currentSelection);
                        break;
                    case 40: // Down arrow
                        e.preventDefault();
                        if (currentSelection < $items.length - 1) {
                            currentSelection++;
                        } else {
                            currentSelection = 0; // Wrap around
                        }
                        updateSelection($items, currentSelection);
                        break;
                    case 13: // Enter
                        e.preventDefault();
                        if (currentSelection >= 0 && currentSelection < $items.length) {
                            $items.eq(currentSelection).click();
                        } else if ($items.length === 1) { // Auto-select if only one item
                             $items.eq(0).click();
                        }
                        break;                        
                    case 27: // Escape
                        console.log(widgetId + ': Escape key pressed.');
                        $dropdown.hide();
                        isDropdownOpen = false;
                        currentSelection = -1;
                        console.log(widgetId + ': Set isDropdownOpen = false due to Escape key.');
                        break;
                }
            });
            
            // Hide dropdown when clicking outside
            jQuery(document).on('click', function(e) {
                if (!jQuery(e.target).closest($input.parent()).length) { // Check against the container
                    if ($dropdown.is(':visible')) {
                        console.log(widgetId + ': Clicked outside, hiding dropdown.');
                        $dropdown.hide();
                        isDropdownOpen = false;
                        currentSelection = -1;
                        console.log(widgetId + ': Set isDropdownOpen = false due to click outside.');
                    }
                }
            });
            
            // Validate input on blur
            $input.on('blur', function() {
                var currentValue = jQuery(this).val().trim();
                console.log(widgetId + ': Input blurred. Value:', currentValue);
                // Delay validation slightly to allow click on dropdown item to process
                setTimeout(function() {
                    // If dropdown is visible (e.g. user is about to click an item), don't validate yet.
                    // The item click will handle selection and validation.
                    if ($dropdown.is(':visible')) {
                        console.log(widgetId + ': Blur event, but dropdown is visible. Deferring validation.');
                        return;
                    }

                    var freshCurrentValue = $input.val().trim(); // Get the up-to-date value
                    if (freshCurrentValue.length > 0) {
                        var isValid = validateLocationInput(freshCurrentValue, allLocations);
                        if (!isValid) {
                            console.log(widgetId + ': Input value "' + freshCurrentValue + '" is invalid on blur.');
                            $input.addClass('location-autocomplete-error');
                            showValidationError($input, 'Please select a valid location from the dropdown.');
                        } else {
                            console.log(widgetId + ': Input value "' + freshCurrentValue + '" is valid on blur.');
                            $input.removeClass('location-autocomplete-error');
                            hideValidationError($input);
                        }
                    } else {
                        // If input is empty, ensure no error state
                        $input.removeClass('location-autocomplete-error');
                        hideValidationError($input);
                    }
                }, 150); // 150ms delay
            });
            
            // Load all locations function
            function loadAllLocations() {
                console.log(widgetId + ': loadAllLocations called.');
                // Show loading message in dropdown if input is focused
                if ($input.is(':focus') && !isDropdownOpen) {
                     showLoadingMessage($dropdown);
                     isDropdownOpen = true; // Tentatively set, will be confirmed by displayResults
                }

                jQuery.ajax({
                    url: '/api/locations/', // Ensure this URL is correct and accessible
                    method: 'GET',
                    success: function(response) {
                        console.log(widgetId + ': loadAllLocations success. Response:', response);
                        if (response && response.results && Array.isArray(response.results)) {
                            allLocations = response.results;
                        } else if (response && response.data && Array.isArray(response.data)) {
                            allLocations = response.data;
                        } else if (Array.isArray(response)) { // Handle if API returns a direct array
                            allLocations = response;
                        } else {
                            console.warn(widgetId + ': Unexpected response structure for locations. Setting to empty. Response:', response);
                            allLocations = [];
                        }
                        console.log(widgetId + ': allLocations populated. Count:', allLocations.length, 'Sample:', allLocations.slice(0,2));

                        // If input is focused and dropdown was showing "Loading...", refresh it.
                        if ($input.is(':focus') && $dropdown.find('.loading-message').length > 0) {
                            console.log(widgetId + ': Input focused and was loading, refreshing dropdown with loaded locations.');
                            var currentValue = $input.val().trim();
                            if (currentValue.length === 0) {
                                displayResults(allLocations, $dropdown, $input);
                            } else {
                                var filtered = filterLocationsLocally(currentValue, allLocations);
                                displayResults(filtered, $dropdown, $input);
                            }
                        } else if ($dropdown.find('.loading-message').length > 0 && !$input.is(':focus')) {
                            // If it was loading but input lost focus, hide the loading message.
                            $dropdown.hide();
                            isDropdownOpen = false;
                        }
                    },
                    error: function(xhr, status, error) {
                        console.error(widgetId + ': Failed to load locations. Status:', status, 'Error:', error, 'XHR:', xhr);
                        allLocations = [];
                        // If input is focused and dropdown was showing "Loading...", show error in dropdown.
                        if ($input.is(':focus') && $dropdown.find('.loading-message').length > 0) {
                            console.log(widgetId + ': Input focused and was loading, showing error message in dropdown due to load failure.');
                            showErrorMessage($dropdown, 'Error loading locations.');
                        } else if ($input.is(':focus')) { // If focused but not showing loading (e.g. focus after fail)
                            showErrorMessage($dropdown, 'Error loading locations.');
                            if ($dropdown.is(':visible')) isDropdownOpen = true;
                        }
                    }
                });
            }
              // Filter locations locally
            function filterLocationsLocally(query, locations) {
                if (!Array.isArray(locations)) {
                    console.error(widgetId + ': filterLocationsLocally called with non-array locations:', locations);
                    return [];
                }
                var lowerQuery = query.toLowerCase();
                return locations.filter(function(location) {
                    // Check for both location.name and location.location_name (database field name)
                    var locationName = location.name || location.location_name;
                    return location && typeof locationName === 'string' && locationName.toLowerCase().includes(lowerQuery);
                }).slice(0, 50); // Increased limit from 10 to 50 results
            }            // Validate that input matches an existing location
            function validateLocationInput(input, locations) {
                if (!input || input.length === 0) return true; // Empty is valid for blur, form submission handles required
                if (!Array.isArray(locations)) {
                    console.warn(widgetId + ': Cannot validate - locations array not available');
                    return false; // Cannot validate if locations aren't loaded
                }

                console.log(widgetId + ': Validating input:', input);
                for (var i = 0; i < locations.length; i++) {
                    var location = locations[i];
                    var locationName = location && (location.name || location.location_name);
                    if (locationName && locationName === input) {
                        console.log(widgetId + ': Input validation passed. Found matching location:', location);
                        return true;
                    }
                }
                console.warn(widgetId + ': Input validation failed. No matching location found for:', input);
                console.log(widgetId + ': Available location names:', locations.map(function(loc) { return loc.name || loc.location_name; }));
                return false;
            }
            
            // Show validation error
            function showValidationError($input, message) {
                var $container = $input.parent(); // The .location-autocomplete-container
                var $error = $container.find('.validation-error');
                
                if ($error.length === 0) {
                    $error = jQuery('<div class="validation-error" style="color: red; font-size: 0.9em;"></div>'); // Added basic style for visibility
                    $container.append($error); // Append to container
                }
                $error.text(message).show();
                console.log(widgetId + ': Validation error shown: ' + message);
            }
            
            // Hide validation error
            function hideValidationError($input) {
                var $container = $input.parent();
                $container.find('.validation-error').hide().empty();
                console.log(widgetId + ': Validation error hidden.');
            }

            // New function to show loading message
            function showLoadingMessage($dropdown) {
                console.log(widgetId + ': showLoadingMessage called.');
                $dropdown.empty();
                var $message = jQuery('<li class="loading-message"></li>').text('Loading locations...');
                $dropdown.append($message);
                console.log(widgetId + ': Calling $dropdown.show() from showLoadingMessage.');
                $dropdown.show();
            }

            // This function seems to be unused by the current logic (local filtering is used)
            // function searchLocations(query, $dropdown, $input) { ... }
            
            function displayResults(locations, $dropdown, $input) {
                console.log(widgetId + ': displayResults called with', (locations ? locations.length : 'null/undefined'), 'locations.');
                $dropdown.empty();
                currentSelection = -1; // Reset selection index
                
                if (!Array.isArray(locations) || locations.length === 0) {
                    var message = 'No matching locations found.';
                    if ($input.val().trim().length === 0 && allLocations.length === 0) {
                        message = 'No locations available.';
                    }
                    // Check if it was a loading state that resolved to no locations
                    if ($dropdown.find('.loading-message').length > 0 && allLocations.length === 0) {
                        message = 'No locations available.';
                    }
                    showErrorMessage($dropdown, message);
                    return;
                }
                locations.forEach(function(location) {
                    var locationName = location.name || location.location_name;
                    if (!location || typeof locationName === 'undefined') {
                        console.warn(widgetId + ': Invalid location object in displayResults:', location);
                        return; // Skip invalid location objects
                    }
                    var $item = jQuery('<li></li>')
                        .text(locationName)
                        .data('location', location) // Store full location object
                        .on('click', function() {
                            var selectedLocation = jQuery(this).data('location');
                            var selectedName = selectedLocation.name || selectedLocation.location_name;
                            console.log(widgetId + ': Dropdown item clicked. Location object:', selectedLocation);
                            console.log(widgetId + ': Selected name:', selectedName);
                            console.log(widgetId + ': Selected name length:', selectedName ? selectedName.length : 'null');
                            console.log(widgetId + ': Selected name characters:', selectedName ? selectedName.split('').map(c => c.charCodeAt(0)) : 'null');
                            
                            // Ensure we're setting the exact name that was displayed
                            var displayName = selectedLocation.name || selectedLocation.location_name;
                            $input.val(displayName).trigger('change'); // Set value and trigger change
                            
                            console.log(widgetId + ': Input value after setting:', $input.val());
                            console.log(widgetId + ': Input value length:', $input.val().length);
                            
                            $dropdown.hide();
                            isDropdownOpen = false;
                            currentSelection = -1;
                            isUserTyping = false; // User has made a selection, no longer typing
                            $input.removeClass('location-autocomplete-error');
                            hideValidationError($input);
                            console.log(widgetId + ': Set isDropdownOpen = false after item click.');
                            $input.focus(); // Optionally refocus input after selection
                        })
                        .on('mouseenter', function() { jQuery(this).addClass('highlighted'); })
                        .on('mouseleave', function() { jQuery(this).removeClass('highlighted'); });
                    
                    $dropdown.append($item);
                });
                
                console.log(widgetId + ': Calling $dropdown.show() from displayResults after populating items.');
                $dropdown.show();
            }
            
            function showErrorMessage($dropdown, message) {
                console.log(widgetId + ': showErrorMessage called with message:', message);
                $dropdown.empty();
                currentSelection = -1; // Reset selection index
                var $messageItem = jQuery('<li class="empty-message"></li>').text(message);
                $dropdown.append($messageItem);
                console.log(widgetId + ': Calling $dropdown.show() from showErrorMessage.');
                $dropdown.show();
            }
            
            function updateSelection($items, index) {
                $items.removeClass('selected highlighted'); // Clear both
                if (index >= 0 && index < $items.length) {
                    var $selectedItem = $items.eq(index);
                    $selectedItem.addClass('selected');
                    // Scroll into view if necessary
                    var dropdownElement = $dropdown[0];
                    var itemElement = $selectedItem[0];
                    if (itemElement.offsetTop < dropdownElement.scrollTop) {
                        dropdownElement.scrollTop = itemElement.offsetTop;
                    } else if (itemElement.offsetTop + itemElement.offsetHeight > dropdownElement.scrollTop + dropdownElement.offsetHeight) {
                        dropdownElement.scrollTop = itemElement.offsetTop + itemElement.offsetHeight - dropdownElement.offsetHeight;
                    }
                }
            }
        });
    }

    // Function to attempt initialization, with retries for django.jQuery
    function attemptInitialization(retriesLeft) {
        if (typeof django !== 'undefined' && typeof django.jQuery !== 'undefined') {
            console.log("Location Autocomplete: django.jQuery found. Initializing widgets.");
            initializeAllAutocompleteWidgets(django.jQuery);
        } else if (retriesLeft > 0) {
            console.log("Location Autocomplete: django.jQuery not found. Retrying in 100ms. Retries left: " + retriesLeft);
            setTimeout(function() {
                attemptInitialization(retriesLeft - 1);
            }, 100);
        } else {
            console.error("Location Autocomplete: django.jQuery not found after multiple retries. Autocomplete will not function.");
        }
    }

    // Wait for the DOM to be fully loaded before trying to initialize
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', function() {
            attemptInitialization(10); // Start with 10 retries (1 second total)
        });
    } else {
        // DOMContentLoaded has already fired
        attemptInitialization(10);
    }

})(); // End of IIFE
