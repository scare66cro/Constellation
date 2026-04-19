<svelte:options accessors />

<script context="module" lang="ts">
	/**
	 * the third argument for event bundler
	 * @see https://github.com/WICG/EventListenerOptions/blob/gh-pages/explainer.md
	 */
	const thirdEventArg = (() => {
		let result: boolean | { passive: boolean } = false;

		try {
			const arg = Object.defineProperty({}, 'passive', {
				get(): boolean {
					result = { passive: true };
					return true;
				}
			});

			window.addEventListener('testpassive', () => {}, arg);
			window.removeEventListener('testpassive', () => {}, arg);
		} catch (e: unknown) {
			console.debug('Passive events not supported', e);
		}

		return result;
	})();
</script>

<script lang="ts">
	import { onMount, onDestroy, createEventDispatcher } from 'svelte';
	import SizeAndPositionManager, { type ItemSize } from './SizeAndPositionManager';
	import { SCROLL_CHANGE_REASON } from './constants';
	import Table from '$lib/ui/Table.svelte';

	export let height: number;

	export let itemCount: number;
	export let itemSize: ItemSize;
	export let estimatedItemSize: number | undefined = undefined;
	export let getKey: ((index: number) => any) | undefined = undefined;

	export let scrollOffset: number | undefined = undefined;
	export let scrollToIndex: number | undefined = undefined;
	export let scrollToAlignment: number | undefined = undefined;
	export let scrollToBehaviour: ScrollBehavior = 'instant';

	export let overscanCount = 3;

	const dispatchEvent = createEventDispatcher();

	const sizeAndPositionManager = new SizeAndPositionManager({
		itemCount,
		itemSize,
		estimatedItemSize: getEstimatedItemSize()
	});

	let mounted = false;
	let wrapper: HTMLDivElement;
	let items:{ index: number, style: any }[] = [];

	let state: {offset: number, scrollChangeReason: any} = {
		offset:
			scrollOffset ||
			(scrollToIndex != null && items.length && getOffsetForIndex(scrollToIndex)) ||
			0,
		scrollChangeReason: SCROLL_CHANGE_REASON.REQUESTED
	};

	let prevState = state;
	let prevProps = {
		scrollToIndex,
		scrollToAlignment,
		scrollOffset,
		itemCount,
		itemSize,
		estimatedItemSize
	};

	let styleCache = {};
	let wrapperStyle = '';
	let innerStyle = '';

	$: {
		// listen to updates:
		scrollToIndex, scrollToAlignment, scrollOffset, itemCount, itemSize, estimatedItemSize;
		// on update:
		propsUpdated();
	}

	$: {
		// listen to updates:
		state;
		// on update:
		stateUpdated();
	}

	$: {
		// listen to updates:
		height;
		// on update:
		if (mounted) recomputeSizes(0); // call scroll.reset
	}

	refresh(); // Initial Load

	onMount(() => {
		mounted = true;

		wrapper.addEventListener('scroll', handleScroll, thirdEventArg);

		if (scrollOffset != null) {
			scrollTo(scrollOffset);
		} else if (scrollToIndex != null) {
			scrollTo(getOffsetForIndex(scrollToIndex));
		}
	});

	onDestroy(() => {
		if (mounted) wrapper.removeEventListener('scroll', handleScroll);
	});

	function propsUpdated() {
		if (!mounted) return;

		const scrollPropsHaveChanged =
			prevProps.scrollToIndex !== scrollToIndex ||
			prevProps.scrollToAlignment !== scrollToAlignment;
		const itemPropsHaveChanged =
			prevProps.itemCount !== itemCount ||
			prevProps.itemSize !== itemSize ||
			prevProps.estimatedItemSize !== estimatedItemSize;

		if (itemPropsHaveChanged) {
			sizeAndPositionManager.updateConfig({
				itemSize,
				itemCount,
				estimatedItemSize: getEstimatedItemSize()
			});

			recomputeSizes();
		}

		if (prevProps.scrollOffset !== scrollOffset) {
			state = {
				offset: scrollOffset || 0,
				scrollChangeReason: SCROLL_CHANGE_REASON.REQUESTED
			};
		} else if (
			typeof scrollToIndex === 'number' &&
			(scrollPropsHaveChanged || itemPropsHaveChanged)
		) {
			state = {
				offset: getOffsetForIndex(scrollToIndex, scrollToAlignment, itemCount),

				scrollChangeReason: SCROLL_CHANGE_REASON.REQUESTED
			};
		}

		prevProps = {
			scrollToIndex,
			scrollToAlignment,
			scrollOffset,
			itemCount,
			itemSize,
			estimatedItemSize
		};
	}

	function stateUpdated() {
		if (!mounted) return;

		const { offset, scrollChangeReason } = state;

		if (prevState.offset !== offset || prevState.scrollChangeReason !== scrollChangeReason) {
			refresh();
		}

		if (prevState.offset !== offset && scrollChangeReason === SCROLL_CHANGE_REASON.REQUESTED) {
			scrollTo(offset);
		}

		prevState = state;
	}

	function refresh() {
		const { offset } = state;
		const { start, stop } = sizeAndPositionManager.getVisibleRange({
			containerSize: height,
			offset,
			overscanCount
		});

		let updatedItems: {index: number, style: any}[] = [];

		const totalSize = sizeAndPositionManager.getTotalSize();
		const heightUnit = typeof height === 'number' ? 'px' : '';
		wrapperStyle = `height:${height}${heightUnit};`;
		innerStyle = `flex-direction:column;height:${totalSize}px;`;

		if (start !== undefined && stop !== undefined) {
			for (let index = start; index <= stop; index++) {
				updatedItems.push({
					index,
					style: getStyle(index)
				});
			}

			dispatchEvent('itemsUpdated', {
				start,
				end: stop
			});
		}

		items = updatedItems;
	}

	function scrollTo(value: number) {
		if ('scroll' in wrapper) {
			wrapper.scroll({
				['top']: value,
				behavior: scrollToBehaviour
			});
		} else {
			(wrapper as any)['scrollTop'] = value;
		}
	}

	export function recomputeSizes(startIndex = 0) {
		styleCache = {};
		sizeAndPositionManager.resetItem(startIndex);
		refresh();
	}

	function getOffsetForIndex(index: number, align = scrollToAlignment, _itemCount = itemCount) {
		if (index < 0 || index >= _itemCount) {
			index = 0;
		}

		return sizeAndPositionManager.getUpdatedOffsetForIndex({
			align,
			containerSize: height,
			currentOffset: state.offset || 0,
			targetIndex: index
		});
	}

	function handleScroll(event: Event) {
		const offset = getWrapperOffset();
		const horizontalOffset = wrapper.scrollLeft;

		if (offset < 0 || (state.offset === offset && event.target !== wrapper)) return;

		state = {
			offset,
			scrollChangeReason: SCROLL_CHANGE_REASON.OBSERVED
		};

		dispatchEvent('afterScroll', {
			offset,
			event,
			scrollLeft: horizontalOffset
		});

		// Dispatch specific horizontal scroll event
		if (wrapper.scrollLeft !== undefined) {
			dispatchEvent('horizontalScroll', {
				scrollLeft: horizontalOffset
			});
		}
	}

	function getWrapperOffset() {
		return (wrapper as any)['scrollTop'];
	}

	function getEstimatedItemSize() {
		return estimatedItemSize || (typeof itemSize === 'number' && itemSize) || 50;
	}

	function getStyle(index: number | undefined) {
		if (index && (styleCache as any)[index]) return (styleCache as any)[index];

		const { size, offset } = sizeAndPositionManager.getSizeAndPositionForIndex(index ?? 0);

		let style;

		style = `height:${size}px;`; // Removed width:100% to allow natural width

		style += `position:absolute;top:${offset}px;`;
		return ((styleCache as any)[index ?? 0] = style);
	}
</script>

<slot name="header" />

<div bind:this={wrapper} class="virtual-list-wrapper" style={wrapperStyle}>
	<div class="virtual-list-inner" style={innerStyle}>
		{#each items as item (getKey ? getKey(item.index) : item.index)}
			<slot name="item" style={item.style} index={item.index} />
		{/each}
	</div>
</div>

<slot name="footer" />

<style>
	.virtual-list-wrapper {
		overflow: auto;
		will-change: transform;
		-webkit-overflow-scrolling: touch;
	}

	.virtual-list-inner {
		width: fit-content; /* Allows content to expand horizontally */
		min-width: 100%;
		position: relative; /* Ensures proper positioning of children */
	}
</style>