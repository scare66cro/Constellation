<script lang="ts">
  import { createEventDispatcher, onMount, onDestroy } from 'svelte';
  import {
    format,
    startOfMonth,
    endOfMonth,
    eachDayOfInterval,
    addMonths,
    subMonths,
    addDays,
    subDays,
    getDay,
    isSameDay,
    isSameMonth,
    isToday,
    startOfDay,
    isBefore,
    isAfter,
    isValid
  } from 'date-fns';
  import { computePosition, flip, offset, shift } from '@floating-ui/dom';

  // Props
  export let value: Date = new Date();
  export let min: Date | undefined = undefined;
  export let max: Date | undefined = undefined;
  export let closeOnSelection: boolean = true;

  const dispatch = createEventDispatcher<{ select: Date }>();

  // State
  let isOpen = false;
  let viewingMonth: Date = startOfMonth(value || new Date());
  let focusedDate: Date = value || new Date();
  let lastValueTime = value ? value.getTime() : undefined;
  let lastMinTime = min ? startOfDay(min).getTime() : undefined;
  let lastMaxTime = max ? startOfDay(max).getTime() : undefined;
  
  // DOM references
  let inputElement: HTMLInputElement;
  let dropdownElement: HTMLDivElement;
  let containerElement: HTMLDivElement;

  // Day names starting with Sunday
  const dayNames = ['Su', 'Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa'];

  // Format the displayed value
  $: displayValue = value ? format(value, 'MM/dd/yyyy') : '';

  // Derived state for valid min/max
  $: effectiveMin = min && isValid(min) ? min : undefined;
  $: effectiveMax = max && isValid(max) ? max : undefined;

  // Keep internal state aligned whenever constraints or bound value change
  $: {
    const currentValueTime = value ? value.getTime() : undefined;
    const currentMinTime = effectiveMin ? startOfDay(effectiveMin).getTime() : undefined;
    const currentMaxTime = effectiveMax ? startOfDay(effectiveMax).getTime() : undefined;

    const valueChanged = currentValueTime !== lastValueTime;
    const minChanged = currentMinTime !== lastMinTime;
    const maxChanged = currentMaxTime !== lastMaxTime;

    if (valueChanged || minChanged || maxChanged) {
      const baseDate = value ?? effectiveMin ?? new Date();
      const clamped = clampDate(baseDate);
      const clampedTime = clamped.getTime();

      if (!value || clampedTime !== currentValueTime) {
        value = clamped;
      }

      if (!isSameDay(focusedDate, clamped)) {
        focusedDate = clamped;
      }

      if (!isSameMonth(viewingMonth, clamped)) {
        viewingMonth = startOfMonth(clamped);
      }

      lastValueTime = clampedTime;
      lastMinTime = currentMinTime;
      lastMaxTime = currentMaxTime;
    }
  }

  // When min/max change, ensure viewingMonth remains within valid range
  $: {
    if (effectiveMin || effectiveMax) {
      const viewStart = startOfMonth(viewingMonth);
      const viewEnd = endOfMonth(viewingMonth);
      
      if (effectiveMin && isBefore(viewEnd, startOfDay(effectiveMin))) {
        viewingMonth = startOfMonth(effectiveMin);
      }
      if (effectiveMax && isAfter(viewStart, startOfDay(effectiveMax))) {
        viewingMonth = startOfMonth(effectiveMax);
      }
    }
  }

  // Generate calendar days for the viewing month
  // Include min/max in the dependency to force re-render when they change
  $: calendarDays = generateCalendarDays(viewingMonth, effectiveMin, effectiveMax);

  // Check if we can navigate to previous/next month
  $: canGoPrevious = canNavigateToPreviousMonth(viewingMonth, effectiveMin);
  $: canGoNext = canNavigateToNextMonth(viewingMonth, effectiveMax);

  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  function generateCalendarDays(month: Date, _min?: Date, _max?: Date): (Date | null)[] {
    const start = startOfMonth(month);
    const end = endOfMonth(month);
    const days = eachDayOfInterval({ start, end });
    
    // Get the day of week for the first day (0 = Sunday)
    const startDayOfWeek = getDay(start);
    
    // Add null placeholders for days before the month starts
    const leadingNulls: (Date | null)[] = Array(startDayOfWeek).fill(null);
    
    return [...leadingNulls, ...days];
  }

  function canNavigateToPreviousMonth(viewing: Date, minDate: Date | undefined): boolean {
    if (!minDate) return true;
    const prevMonth = subMonths(viewing, 1);
    const endOfPrevMonth = endOfMonth(prevMonth);
    return !isBefore(endOfPrevMonth, startOfDay(minDate));
  }

  function canNavigateToNextMonth(viewing: Date, maxDate: Date | undefined): boolean {
    if (!maxDate) return true;
    const nextMonth = addMonths(viewing, 1);
    const startOfNextMonth = startOfMonth(nextMonth);
    return !isAfter(startOfNextMonth, startOfDay(maxDate));
  }

  function isDateDisabled(date: Date): boolean {
    if (!date) return true;
    const normalizedDate = startOfDay(date);
    const beforeMin = effectiveMin && isBefore(normalizedDate, startOfDay(effectiveMin));
    const afterMax = effectiveMax && isAfter(normalizedDate, startOfDay(effectiveMax));
    return beforeMin || afterMax || false;
  }

  function isDateSelected(date: Date): boolean {
    if (!date || !value) return false;
    return isSameDay(date, value);
  }

  function isDateFocused(date: Date): boolean {
    if (!date || !focusedDate) return false;
    return isSameDay(date, focusedDate);
  }

  function selectDate(date: Date): void {
    const disabled = isDateDisabled(date);
    if (disabled) return;
    
    value = startOfDay(date);
    dispatch('select', value);
    
    if (closeOnSelection) {
      closeDropdown();
    }
  }

  function goToPreviousMonth(): void {
    if (!canGoPrevious) return;
    viewingMonth = subMonths(viewingMonth, 1);
  }

  function goToNextMonth(): void {
    if (!canGoNext) return;
    viewingMonth = addMonths(viewingMonth, 1);
  }

  function openDropdown(): void {
    isOpen = true;
    const current = clampDate(value || new Date());
    viewingMonth = startOfMonth(current);
    focusedDate = current;
    updateDropdownPosition();
  }

  function closeDropdown(): void {
    isOpen = false;
  }

  function toggleDropdown(): void {
    if (isOpen) {
      closeDropdown();
    } else {
      openDropdown();
    }
  }

  function clampDate(date: Date): Date {
    let result = startOfDay(date);
    if (effectiveMin && isBefore(result, startOfDay(effectiveMin))) {
      result = startOfDay(effectiveMin);
    }
    if (effectiveMax && isAfter(result, startOfDay(effectiveMax))) {
      result = startOfDay(effectiveMax);
    }
    return result;
  }

  function moveFocus(days: number): void {
    let newDate = days > 0 ? addDays(focusedDate, days) : subDays(focusedDate, Math.abs(days));
    newDate = clampDate(newDate);
    
    // Update viewing month if focused date moves to different month
    if (!isSameMonth(newDate, viewingMonth)) {
      viewingMonth = startOfMonth(newDate);
    }
    
    focusedDate = newDate;
  }

  function handleKeydown(event: KeyboardEvent): void {
    if (!isOpen) {
      if (event.key === 'Enter' || event.key === ' ' || event.key === 'ArrowDown') {
        event.preventDefault();
        openDropdown();
      }
      return;
    }

    switch (event.key) {
      case 'ArrowLeft':
        event.preventDefault();
        moveFocus(-1);
        break;
      case 'ArrowRight':
        event.preventDefault();
        moveFocus(1);
        break;
      case 'ArrowUp':
        event.preventDefault();
        moveFocus(-7);
        break;
      case 'ArrowDown':
        event.preventDefault();
        moveFocus(7);
        break;
      case 'Enter':
      case ' ':
        event.preventDefault();
        selectDate(focusedDate);
        break;
      case 'Escape':
        event.preventDefault();
        closeDropdown();
        break;
    }
  }

  function handleClickOutside(event: MouseEvent): void {
    if (containerElement && !containerElement.contains(event.target as Node)) {
      closeDropdown();
    }
  }

  async function updateDropdownPosition(): Promise<void> {
    if (!inputElement || !dropdownElement) return;
    
    const { x, y } = await computePosition(inputElement, dropdownElement, {
      placement: 'bottom-start',
      middleware: [
        offset(4),
        flip({ fallbackPlacements: ['top-start', 'bottom-end', 'top-end'] }),
        shift({ padding: 8 })
      ]
    });

    Object.assign(dropdownElement.style, {
      left: `${x}px`,
      top: `${y}px`
    });
  }

  function handleResize(): void {
    if (isOpen) {
      updateDropdownPosition();
    }
  }

  onMount(() => {
    document.addEventListener('click', handleClickOutside);
    window.addEventListener('resize', handleResize);
    window.addEventListener('scroll', handleResize, true);
  });

  onDestroy(() => {
    document.removeEventListener('click', handleClickOutside);
    window.removeEventListener('resize', handleResize);
    window.removeEventListener('scroll', handleResize, true);
  });

  // Update dropdown position when it opens
  $: if (isOpen && dropdownElement) {
    updateDropdownPosition();
  }
</script>

<div class="relative inline-block {$$props.class ?? ''}" bind:this={containerElement}>
  <!-- Input Field -->
  <input
    type="text"
    readonly
    class="text-size-large w-full px-3 py-2 bg-white border border-gray-300 rounded cursor-pointer focus:outline-none focus:ring-2 focus:ring-primary-500 focus:border-primary-500"
    value={displayValue}
    on:click={toggleDropdown}
    on:keydown={handleKeydown}
    bind:this={inputElement}
  />

  <!-- Calendar Dropdown -->
  {#if isOpen}
    <div
      class="absolute z-50 bg-gray-200 border border-gray-400 rounded-lg shadow-lg p-3 min-w-[420px]"
      bind:this={dropdownElement}
    >
      <!-- Header with navigation -->
      <div class="flex items-center justify-between mb-3">
        <button
          type="button"
          class="p-1 rounded hover:bg-gray-300 disabled:opacity-30 disabled:cursor-not-allowed"
          on:click|stopPropagation={goToPreviousMonth}
          disabled={!canGoPrevious}
          aria-label="Previous month"
        >
          <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 19l-7-7 7-7" />
          </svg>
        </button>
        
        <span class="text-size-large font-semibold text-gray-800">
          {format(viewingMonth, 'MMMM yyyy')}
        </span>
        
        <button
          type="button"
          class="p-1 rounded hover:bg-gray-300 disabled:opacity-30 disabled:cursor-not-allowed"
          on:click|stopPropagation={goToNextMonth}
          disabled={!canGoNext}
          aria-label="Next month"
        >
          <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5l7 7-7 7" />
          </svg>
        </button>
      </div>

      <!-- Day names header -->
      <div class="grid grid-cols-7 gap-1 mb-1">
        {#each dayNames as dayName, i (i)}
          <div class="text-center text-xs font-medium text-gray-600 py-1">
            {dayName}
          </div>
        {/each}
      </div>

      <!-- Calendar grid -->
      <div class="grid grid-cols-7 gap-1">
        {#each calendarDays as day, i (i)}
          {#if day === null}
            <div class="w-9 h-9"></div>
          {:else if isDateDisabled(day)}
            <div class="w-9 h-9"></div>
          {:else}
            {@const selected = isDateSelected(day)}
            {@const focused = isDateFocused(day)}
            {@const today = isToday(day)}
            <button
              type="button"
              class="w-9 h-9 flex flex-col items-center justify-center rounded text-sm transition-colors relative
                hover:bg-gray-300 cursor-pointer
                {selected ? 'bg-primary-900 text-white hover:bg-primary-800' : ''}
                {focused && !selected ? 'ring-2 ring-primary-500' : ''}
                {today && !selected ? 'font-bold' : ''}"
              on:click|stopPropagation={() => selectDate(day)}
              tabindex="-1"
            >
              <span>{format(day, 'd')}</span>
              {#if today}
                <span class="absolute bottom-0.5 w-1 h-1 rounded-full {selected ? 'bg-white' : 'bg-primary-600'}"></span>
              {/if}
            </button>
          {/if}
        {/each}
      </div>
    </div>
  {/if}
</div>
