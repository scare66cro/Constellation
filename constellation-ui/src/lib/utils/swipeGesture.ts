import { browser } from '$app/environment';

export interface SwipeOptions {
  threshold?: number; // Minimum distance in pixels to trigger a swipe
  restraint?: number; // Maximum perpendicular distance allowed
  allowedTime?: number; // Maximum time allowed for swipe (ms) - used for touch gestures
  allowedTimeTouch?: number; // Maximum time allowed for touch swipes (ms)
  enableTouch?: boolean; // Enable touch swipe gestures
  // When true, allow swipe detection to start even if the touch begins on a form control
  // Useful for pages like Accounts where the screen is mostly inputs
  allowFromFormControls?: boolean;
  onSwipeLeft?: () => void;
  onSwipeRight?: () => void;
}

export interface SwipeState {
  startX: number;
  startY: number;
  startTime: number;
  isActive: boolean;
  isDragging: boolean; // For mouse events
  // Whether the touch originated in an interactive container like ScrollableArea
  isInInteractiveZone?: boolean;
  // Whether touch began in a horizontally scrollable area (tables, carousels, etc.)
  isInHorizontalScrollableZone?: boolean;
}

export class SwipeGestureHandler {
  private element: HTMLElement;
  private options: Required<SwipeOptions>;
  
  private state: SwipeState = {
    startX: 0,
    startY: 0,
    startTime: 0,
    isActive: false,
    isDragging: false
  };

  constructor(element: HTMLElement, options: SwipeOptions = {}) {
    this.element = element;
    this.options = {
      threshold: options.threshold ?? 50,
      restraint: options.restraint ?? 100,
      allowedTime: options.allowedTime ?? 300,
      allowedTimeTouch: options.allowedTimeTouch ?? options.allowedTime ?? 300,
      enableTouch: options.enableTouch ?? true,
  allowFromFormControls: options.allowFromFormControls ?? false,
      onSwipeLeft: options.onSwipeLeft ?? (() => {}),
      onSwipeRight: options.onSwipeRight ?? (() => {})
    };

    this.init();
  }  private init(): void {
    if (!browser) return;

    // Touch events (if enabled)
    if (this.options.enableTouch) {
      this.element.addEventListener('touchstart', this.handleTouchStart, { passive: true });
      this.element.addEventListener('touchmove', this.handleTouchMove, { passive: false });
      this.element.addEventListener('touchend', this.handleTouchEnd, { passive: true });
    }
  }  private handleTouchStart = (e: TouchEvent): void => {
    // Only handle single finger touches
    if (e.touches.length !== 1) return;

    // Check if the touch target is a form control or interactive component
    const target = e.target as HTMLElement;
    const isTrueFormControl = target && (
      target.tagName === 'SELECT' ||
      target.tagName === 'INPUT' ||
      target.tagName === 'TEXTAREA' ||
      target.tagName === 'BUTTON' ||
      target.closest('select') ||
      target.closest('input') ||
      target.closest('textarea') ||
      target.closest('button') ||
      target.classList.contains('select') || // Svelte select components
      target.closest('.select') || // Svelte select containers
      // Skeleton Labs SlideToggle components
      target.classList.contains('slide-toggle') ||
      target.closest('.slide-toggle') ||
      target.closest('[data-testid*="slide-toggle"]') ||
      // Any element with role="switch" (common for toggle switches)
      target.getAttribute('role') === 'switch' ||
      target.closest('[role="switch"]') ||
      // Generic slider/range controls
      target.classList.contains('slider') ||
      target.closest('.slider') ||
      target.classList.contains('range') ||
      target.closest('.range')
    );

    // Interactive containers (like ScrollableArea): allow swipe detection but don't block vertical scroll
    const isInteractiveContainer = target && (
      target.closest('.touch-interactive') ||
      target.closest('[data-touch-interactive]')
    );

    // Determine if the touch started in a horizontally scrollable region
    const isHorizontallyScrollable = (el: HTMLElement | null): boolean => {
      let node: HTMLElement | null = el;
      while (node && node !== document.documentElement) {
        const style = window.getComputedStyle(node);
        const overflowX = style.overflowX;
        const canScrollX = (overflowX === 'auto' || overflowX === 'scroll');
        if (canScrollX && node.scrollWidth > node.clientWidth + 1) {
          return true;
        }
        node = node.parentElement as HTMLElement | null;
      }
      return false;
    };

    const isRangeLikeControl = (() => {
      // Common horizontally draggable controls we should not intercept
      if (target instanceof HTMLInputElement && target.type === 'range') return true;
      if (target.getAttribute('role') === 'slider' || target.closest('[role="slider"]')) return true;
      // Custom slider classes already covered in isTrueFormControl
      return false;
    })();

    const inHorizontalScrollZone = isHorizontallyScrollable(target);

    // If it's horizontally scrollable or a slider-like control, do not start swipe handling
    if (inHorizontalScrollZone || isRangeLikeControl) {
      this.state.isActive = false;
      return;
    }

    // If it's a form control or interactive component, don't interfere unless explicitly allowed
    if (isTrueFormControl && !this.options.allowFromFormControls) {
      this.state.isActive = false;
      return;
    }

    const touch = e.touches[0];
    this.state = {
      startX: touch.clientX,
      startY: touch.clientY,
      startTime: Date.now(),
      isActive: true,
      isDragging: false,
      isInInteractiveZone: Boolean(isInteractiveContainer),
      isInHorizontalScrollableZone: inHorizontalScrollZone
    };
  };
  private handleTouchMove = (e: TouchEvent): void => {
    if (!this.state.isActive || e.touches.length !== 1) return;
    // If started within a horizontal scroll area, don't hijack touch
    if (this.state.isInHorizontalScrollableZone) return;
    
    const touch = e.touches[0];
    const deltaX = Math.abs(touch.clientX - this.state.startX);
    const deltaY = Math.abs(touch.clientY - this.state.startY);
    
    // Only prevent default if we're moving horizontally (potential swipe)
    // and the horizontal movement is greater than vertical movement
    if (deltaX > deltaY && deltaX > 10) {
      // Prevent browser's pull-to-refresh and navigation gestures on mobile
      e.preventDefault();
    }
  };

  private handleTouchEnd = (e: TouchEvent): void => {
    if (!this.state.isActive) return;
    if (this.state.isInHorizontalScrollableZone) {
      this.state.isActive = false;
      return;
    }

    const touch = e.changedTouches[0];
    const deltaTime = Date.now() - this.state.startTime;
    const deltaX = touch.clientX - this.state.startX;
    const deltaY = touch.clientY - this.state.startY;

    this.state.isActive = false;

    // Check if the swipe meets our criteria
    if (deltaTime <= this.options.allowedTimeTouch) {
      // Check if horizontal movement is sufficient and vertical movement is restrained
      if (Math.abs(deltaX) >= this.options.threshold && Math.abs(deltaY) <= this.options.restraint) {
        if (deltaX > 0) {
          // Swipe right
          this.options.onSwipeRight();
        } else {
          // Swipe left
          this.options.onSwipeLeft();
        }
      }
    }
  };

  public destroy(): void {
    if (!browser) return;

    // Remove touch event listeners (if enabled)
    if (this.options.enableTouch) {
      this.element.removeEventListener('touchstart', this.handleTouchStart);
      this.element.removeEventListener('touchmove', this.handleTouchMove);
      this.element.removeEventListener('touchend', this.handleTouchEnd);
    }
  }

  public updateOptions(newOptions: Partial<SwipeOptions>): void {
    this.options = { ...this.options, ...newOptions };
  }
}

// Svelte action for easy use in components
export function swipe(element: HTMLElement, options: SwipeOptions = {}) {
  const handler = new SwipeGestureHandler(element, options);

  return {
    destroy() {
      handler.destroy();
    },
    update(newOptions: SwipeOptions) {
      handler.updateOptions(newOptions);
    }
  };
}
