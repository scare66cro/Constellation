<script lang="ts" context="module">
  import type { MetricConfigs } from '$lib/business/charting';
  import type { LineSeriesOption } from 'echarts/charts';

  export const maxColor = 8;
  export const colors = [
    '#c12e34',
    '#e6b600',
    '#0098d9',
    '#2b821d',
    '#005eaa',
    '#339ca8',
    '#cda819',
    '#32a487',
  ];

  export function createSeries(metric: string, data: number[]): LineSeriesOption {
    return {
      name: metric,
      type: 'line',
      data,
      showSymbol: false,
      clip: true,
      connectNulls: false,
      lineStyle: {
        color: 'black',
      },
      itemStyle: {
        color: 'black',
      },
    };
  }

  export function createChartData(
    metricConfigs: MetricConfigs,
    metricLabels: string[],
    zoom: string | undefined,
  ): ChartData {
    const data = new ChartData(metricConfigs, zoom);
    metricLabels.forEach((label) => {
      data.metricConfigs[label] = { display: false, axis: AxisType.Primary };
    });
    return data;
  }

  export class ChartData {
    public metricConfigs: MetricConfigs;
    constructor(metricConfigs: MetricConfigs, zoom: string | undefined) {
      this.metricConfigs = metricConfigs;
      this.dataZoom = zoom;
    }
    public series: LineSeriesOption[] = [];
    public seriesAxis: number[] = [];
    public secondary = false;
    public dataZoom;
  }

  export declare type OptionDataValue = number | string | Date;

</script>

<script lang="ts">
  import { onMount, onDestroy, createEventDispatcher } from 'svelte';
  import * as echarts from 'echarts/core';
  import { LineChart } from 'echarts/charts';
  import { DataZoomInsideComponent, DataZoomSliderComponent, DataZoomComponent, GridComponent, TooltipComponent, LegendComponent } from 'echarts/components';
  import type { DataZoomComponentOption, GridComponentOption, TooltipComponentOption } from 'echarts/components';
  import { CanvasRenderer } from 'echarts/renderers';
  import { format } from 'date-fns';
  import gellert_theme from '$lib/utils/gellert.theme';
  import type { XAXisComponentOption } from 'echarts';
  import { AxisType } from '$lib/business/charting';
  import { debounce } from 'lodash-es';
  import Button from '$lib/ui/Button.svelte';
  import Icon from '$lib/ui/Icon.svelte';
  import { mdiRefresh } from '@mdi/js';
	import Wait from '$lib/ui/Wait.svelte';
  
  // Font sizes tuned for 1920x1080 readability
  const FONT_SIZES = {
    axis: 24,
    legend: 24,
    tooltip: 24,
    richDate: 24,
    richTime: 24,
    dataZoom: 24,
  } as const;

  // Slider sizing for dataZoom (visibility on 1080p)
  const SLIDER_SIZES = {
    height: 48,
    handleSize: 32,
  } as const;

  // Position tweaks for the dataZoom slider
  const SLIDER_POS = {
    bottom: 16,
  } as const;

  const GRID_PADDING = {
    top: 12,
    bottom: 75,
    left: Math.ceil(FONT_SIZES.axis * 1.25),
    right: 220,
  } as const;
  
  export let labels: string[];

  export let chartData: ChartData;

  export let id: string;

  export let height = 390;

  let dispatch = createEventDispatcher();

  let width: number;

  let chartDiv: HTMLDivElement;

  let isDragging = false;
  let dragStartX = 0;
  let selectionDiv: HTMLDivElement | undefined;

  let isPinching = false;
  let initialPinchDistance = 0;
  let lastTouchX = 0;
  let isPanning = false;
  // Track tap detection
  let touchStartTime = 0;
  let touchStartPoint: Point = { x: 0, y: 0 };
  let currentTouchPoint: Point = { x: 0, y: 0 };
  let touchMoved = false;
  const TAP_DURATION_MS = 300;
  const TAP_MOVE_TOLERANCE = 10; // px

  // Track mouse click for tooltip (optional, helps desktop users)
  let mouseDownTime = 0;
  let mouseDownPoint: Point = { x: 0, y: 0 };

  // Slider (dataZoom) touch state
  type SliderMode = 'none' | 'handle-left' | 'handle-right' | 'move-window';
  let sliderTouchActive = false;
  let sliderMode: SliderMode = 'none';
  let sliderLastX = 0;
  let sliderTouchMoved = false;
  let lastSliderTapTime = 0;
  let lastSliderTapPoint: Point = { x: 0, y: 0 };

  $: zoomValues = {
    start: 0,
    end: 100,
  };

  $: wait = false;

  const MIN_ZOOM_RANGE = 5; // Minimum zoom range in percentage (5%)

  type ECOption = echarts.ComposeOption<
    | LineSeriesOption
    | TooltipComponentOption
    | GridComponentOption
    | DataZoomComponentOption
    | XAXisComponentOption
  >;

  let chart: echarts.ECharts | null = null;
  let startDate: number;
  let endDate: number;
  let chartReady = false;

  const dateFormatter = (value: string, index: number): string => {
    const date = new Date(value);
    if (index === 0) {
      return `{date|${format(date, 'MM/dd/yyyy')}}`;
    }
    if (date.getMinutes() === 0 && date.getHours() === 0) {
      if (date.getDate() === 1) {
        return `{date|${format(date, 'MM/dd')}} {time|${format(date, 'H:mm')}}`;
      }
      return `{date|${format(date, 'd')}} {time|${format(date, 'H:mm')}}`;
    }
    if (endDate - startDate < 3600000) {
      // hour
      return `{time|${format(date, 'H:mm')}}`;
    }
    if (endDate - startDate < 3600000 * 24 * 31) {
      // month
      return `{date|${format(date, 'd')}} {time|${format(date, 'H:mm')}}`;
    }
    return `{date|${format(date, 'MM/dd')}} {time|${format(date, 'H:mm')}}`;
  };

  const dataZoom: DataZoomComponentOption[] = [
    {
      type: 'inside',
      xAxisIndex: [0],
      filterMode: 'none',
      zoomOnMouseWheel: false,
      moveOnMouseMove: false,
    },
    {
      type: 'slider',
      xAxisIndex: [0],
      filterMode: 'none',
      textStyle: { fontSize: FONT_SIZES.dataZoom },
      height: SLIDER_SIZES.height,
      handleSize: SLIDER_SIZES.handleSize,
  bottom: SLIDER_POS.bottom,
    },
  ];

  function decorateDataZoom(dz: DataZoomComponentOption[] | undefined): DataZoomComponentOption[] {
    if (!dz) return dataZoom;
    return dz.map((z) => {
      if ((z as any).type === 'slider') {
        return {
          ...z,
          textStyle: { fontSize: FONT_SIZES.dataZoom, ...(z as any).textStyle },
          height: SLIDER_SIZES.height,
          handleSize: SLIDER_SIZES.handleSize,
          bottom: SLIDER_POS.bottom,
        } as DataZoomComponentOption;
      }
      return z;
    });
  }

  let xAxis: XAXisComponentOption[] = [{
    type: 'category',
    data: [],
    // minInterval: 300000,
    triggerEvent: true,
    axisLabel: {
      showMinLabel: true,
      fontSize: FONT_SIZES.axis,
      formatter: dateFormatter,
      // color wheel triad color.adobe.com
      // #9BAB15 #0080FF #DEF705 #F72B1E #AB160C
      rich: {
        date: {
          backgroundColor: '#004E7C',
          color: '#FFFFFF',
          borderRadius: 15,
          padding: 5,
          fontSize: FONT_SIZES.richDate,
        },
        time: {
          padding: 5,
          fontSize: FONT_SIZES.richTime,
        },
      },
    },
  }];

  let options: ECOption = {
    tooltip: [{
      trigger: 'axis',
      // Allow tooltip on both mouse move and clicks; we'll also drive it manually for touch taps
      triggerOn: 'mousemove|click',
      confine: true,
      axisPointer: {
        label: {
          fontSize: FONT_SIZES.tooltip,
          formatter: (params: any): string => {
            const date = new Date(params.value);
            return format(date, 'MM/dd/yyyy H:mm');
          },
        },
      },
      textStyle: { fontSize: FONT_SIZES.tooltip },
      valueFormatter: (value): string => {
        return typeof value === 'number' ? value.toFixed(1) : '';
      },
    }],
    legend: {
      orient: 'vertical',
      type: 'scroll',
      right: 10,
      top: 'center',
      selectedMode: false,
      textStyle: { fontSize: FONT_SIZES.legend },
    },
    series: [],
    xAxis,
    yAxis: [
      { show: true, axisLabel: { fontSize: FONT_SIZES.axis } },
      { show: false, axisLabel: { fontSize: FONT_SIZES.axis } },
    ],
    grid: {
      top: GRID_PADDING.top,
      bottom: GRID_PADDING.bottom,
      left: GRID_PADDING.left,
      right: GRID_PADDING.right,
      containLabel: true,
    },
    dataZoom: dataZoom as DataZoomComponentOption[],
  };

  function setAxisFormatting(): void {
    if (chart) {
      const opts = (chart.getOption() as ECOption);
      const { start, end } = (opts.dataZoom as Array<DataZoomComponentOption>)[0];
      const length = (opts.xAxis as any[])[0].data.length;
      const data = (opts.xAxis as any[])[0].data;
      if (start) {
        startDate = new Date(data[Math.floor((start * length) / 100)]).valueOf();
      }
      if (end) {
        endDate = new Date(data[Math.floor((end * length)  / 100)]).valueOf();
      }
    }
  }

  function setZoom(dz: DataZoomComponentOption[]): void {
    chartData.dataZoom = JSON.stringify(dz);
  }

  const debounceSetZoom = debounce(setZoom, 500);
  function reset(labels: string[]): void {
    if (chartReady) {
      destroyChart();
      echarts.use([
        TooltipComponent,
        GridComponent,
        LegendComponent,
        DataZoomComponent,
        DataZoomInsideComponent,
        DataZoomSliderComponent,
        CanvasRenderer,
        LineChart,
      ]);
      chart = echarts.init(chartDiv, gellert_theme, { height, width,});
  (options.xAxis as any[])[0].data = labels;
  const parsedDZ = (chartData.dataZoom !== undefined && chartData.dataZoom !== '') ? JSON.parse(chartData.dataZoom) : dataZoom;
  options.dataZoom = decorateDataZoom(parsedDZ as DataZoomComponentOption[]);
      chart.setOption(options, true);
      chart.on('datazoom', () => {
        setAxisFormatting();
        if (chart) {
          const opts = chart.getOption();
          const dataZoom = opts.dataZoom as DataZoomComponentOption[];
          if (dataZoom && dataZoom[0]) {
            zoomValues = {
              start: dataZoom[0].start || 0,
              end: dataZoom[0].end || 100,
            };
          }
          debounceSetZoom(dataZoom);
        }
      });
    }
  }

  function resize(width: number, height: number): void {
    if (chart) {
      chart.resize({ width, height });
    }
  }

  function update(series: LineSeriesOption[], secondary: boolean): void {
    if (!chart) return;
    const yAxis = [
      { show: true, axisLabel: { fontSize: FONT_SIZES.axis } },
      { show: secondary, axisLabel: { fontSize: FONT_SIZES.axis } },
    ];
    series.forEach((item, index) => {
      if (item.lineStyle) {
        item.lineStyle.color = colors[index % maxColor];
      }
      if (item.itemStyle) {
        item.itemStyle.color = colors[index % maxColor];
      }
      item.yAxisIndex = chartData.seriesAxis[index];
    });
    try {
      chart.setOption({
        xAxis: chart.getOption().xAxis,
        yAxis,
        series,
        dataZoom: chart.getOption().dataZoom,
      }, { replaceMerge: ['xAxis', 'yAxis', 'series', 'dataZoom']});
      setAxisFormatting();
      const dz = chart.getOption().dataZoom as DataZoomComponentOption;
      if (dz) {
        chartData.dataZoom = JSON.stringify(dz);
      }
    } catch (e) {
      console.log((e as Error).message);
    }
  }

  function configChart(): void {
    dispatch('configChart', { id, update: (series: LineSeriesOption[], secondary: boolean) => update(series, secondary) });
  }

  function destroyChart(): void {
    if (chart && !chart.isDisposed()) {
      chart.off('datazoom');
      chart.dispose();
    }
  }

  function getChartGridRect(): DOMRect {
    if (!chart) return chartDiv.getBoundingClientRect();
    
    const opts = chart.getOption();
    const grid = opts.grid as GridComponentOption;
    const rect = chartDiv.getBoundingClientRect();
    
    const left = rect.left + (typeof grid.left === 'number' ? grid.left : 35);
    const right = rect.right - (typeof grid.right === 'number' ? grid.right : 200);
    const top = rect.top + (typeof grid.top === 'number' ? grid.top : 7);
    const bottom = rect.bottom - (typeof grid.bottom === 'number' ? grid.bottom : 75);
    
    return {
      left,
      right,
      top,
      bottom,
      width: right - left,
      height: bottom - top,
      x: left,
      y: top,
      toJSON: () => ({})
    };
  }

  function getSliderRect(): DOMRect {
    const rect = chartDiv.getBoundingClientRect();
    const grid = getChartGridRect();
    const top = rect.bottom - SLIDER_POS.bottom - SLIDER_SIZES.height;
    const bottom = top + SLIDER_SIZES.height;
    const left = grid.left;
    const right = grid.right;
    return {
      left,
      right,
      top,
      bottom,
      width: right - left,
      height: bottom - top,
      x: left,
      y: top,
      toJSON: () => ({})
    };
  }

  interface Point {
    x: number;
    y: number;
  }

  type InteractionEvent = MouseEvent | TouchEvent;

  function getEventPoint(event: InteractionEvent): Point {
    if (event instanceof MouseEvent) {
      return { x: event.clientX, y: event.clientY };
    }
    if (event.touches.length > 0) {
      return { x: event.touches[0].clientX, y: event.touches[0].clientY };
    }
    return { x: 0, y: 0 };
  }

  function handleStart(event: InteractionEvent): void {
    const point = getEventPoint(event);
  mouseDownTime = Date.now();
  mouseDownPoint = { ...point };
    const gridRect = getChartGridRect();
    
    if (point.x >= gridRect.left && point.x <= gridRect.right && 
        point.y >= gridRect.top && point.y <= gridRect.bottom) {
      isDragging = true;
      dragStartX = point.x;
      
      selectionDiv = document.createElement('div');
      selectionDiv.style.position = 'absolute';
      selectionDiv.style.backgroundColor = 'rgba(0, 128, 255, 0.2)';
      selectionDiv.style.border = '1px solid rgba(0, 128, 255, 0.8)';
      selectionDiv.style.pointerEvents = 'none';
      selectionDiv.style.top = `${gridRect.top - chartDiv.getBoundingClientRect().top}px`;
      selectionDiv.style.height = `${gridRect.height}px`;
      selectionDiv.style.left = `${point.x - chartDiv.getBoundingClientRect().left}px`;
      selectionDiv.style.width = '0';
      
      chartDiv.appendChild(selectionDiv);
    }
  }

  function handleMove(event: InteractionEvent): void {
    if (isDragging && selectionDiv) {
      const point = getEventPoint(event);
      const gridRect = getChartGridRect();
      const chartRect = chartDiv.getBoundingClientRect();
      
      const currentX = Math.max(gridRect.left, Math.min(gridRect.right, point.x));
      const startX = Math.max(gridRect.left, Math.min(gridRect.right, dragStartX));
      
      const left = Math.min(startX, currentX) - chartRect.left;
      const width = Math.abs(currentX - startX);
      
      selectionDiv.style.left = `${left}px`;
      selectionDiv.style.width = `${width}px`;
    }
  }

  function handleEnd(event: InteractionEvent): void {
    if (!isDragging) return;
    isDragging = false;
    
    if (selectionDiv) {
      const point = getEventPoint(event);
      const gridRect = getChartGridRect();
      const startX = Math.max(gridRect.left, Math.min(gridRect.right, dragStartX));
      const endX = Math.max(gridRect.left, Math.min(gridRect.right, point.x));
      if (Math.abs(endX - startX) < 5) {
        selectionDiv.remove();
        selectionDiv = undefined;
        return;
      }
      const percentStartX = ((startX - gridRect.left) / gridRect.width) * 100;
      const percentEndX = ((endX - gridRect.left) / gridRect.width) * 100;
      const oldRange = zoomValues.end - zoomValues.start;
      zoomValues = { 
        start: oldRange * percentStartX / 100 + zoomValues.start,
        end: oldRange * percentEndX / 100 + zoomValues.start,
      }

      selectionDiv.remove();
      selectionDiv = undefined;

      chart?.dispatchAction({
        type: 'dataZoom',
        dataZoomIndex: 0,
        start: Math.min(zoomValues.start, zoomValues.end),
        end: Math.max(zoomValues.start, zoomValues.end),
      });
      chart?.dispatchAction({
        type: 'dataZoom',
        dataZoomIndex: 1,
        start: Math.min(zoomValues.start, zoomValues.end),
        end: Math.max(zoomValues.start, zoomValues.end),
      });
    }
  }

  function showTooltipAtClientPoint(point: Point): void {
    if (!chart) return;
    const gridRect = getChartGridRect();
    if (
      point.x < gridRect.left || point.x > gridRect.right ||
      point.y < gridRect.top || point.y > gridRect.bottom
    ) {
      return;
    }
    const rect = chartDiv.getBoundingClientRect();
    const relX = point.x - rect.left;
    const relY = point.y - rect.top;
    chart.dispatchAction({ type: 'showTip', x: relX, y: relY });
  }

  function getTouchDistance(touch1: Touch, touch2: Touch): number {
    const dx = touch1.clientX - touch2.clientX;
    const dy = touch1.clientY - touch2.clientY;
    return Math.sqrt(dx * dx + dy * dy);
  }

  function handleTouchStart(event: TouchEvent): void {
    const gridRect = getChartGridRect();
    const sliderRect = getSliderRect();
    
    if (event.touches.length === 2) {
      // Pinch zoom
      event.preventDefault();
      isPinching = true;
      initialPinchDistance = getTouchDistance(event.touches[0], event.touches[1]);
    } else if (event.touches.length === 1) {
      // Single finger pan or slider interaction
      const touch = event.touches[0];
      const inGrid = touch.clientX >= gridRect.left && touch.clientX <= gridRect.right && 
          touch.clientY >= gridRect.top && touch.clientY <= gridRect.bottom;
      const inSlider = touch.clientX >= sliderRect.left && touch.clientX <= sliderRect.right && 
          touch.clientY >= sliderRect.top && touch.clientY <= sliderRect.bottom;
      if (inSlider) {
        // Slider touch: decide mode
        event.preventDefault();
        sliderTouchActive = true;
        sliderTouchMoved = false;
        sliderLastX = touch.clientX;
        const grid = getChartGridRect();
        const width = grid.width;
        const startX = grid.left + (zoomValues.start / 100) * width;
        const endX = grid.left + (zoomValues.end / 100) * width;
        const handleHitPx = Math.max(16, SLIDER_SIZES.handleSize / 2);
        if (Math.abs(touch.clientX - startX) <= handleHitPx) {
          sliderMode = 'handle-left';
        } else if (Math.abs(touch.clientX - endX) <= handleHitPx) {
          sliderMode = 'handle-right';
        } else if (touch.clientX > Math.min(startX, endX) && touch.clientX < Math.max(startX, endX)) {
          sliderMode = 'move-window';
        } else {
          // Tap on empty slider track: center window around tap keeping range
          const range = zoomValues.end - zoomValues.start;
          const tapPercent = ((touch.clientX - grid.left) / width) * 100;
          let newStart = tapPercent - range / 2;
          let newEnd = tapPercent + range / 2;
          if (newStart < 0) { newEnd -= newStart; newStart = 0; }
          if (newEnd > 100) { newStart -= (newEnd - 100); newEnd = 100; }
          updateZoom(Math.max(0, newStart), Math.min(100, newEnd));
          sliderMode = 'move-window';
        }
        // Double-tap detection setup
        touchStartTime = Date.now();
        touchStartPoint = { x: touch.clientX, y: touch.clientY };
      } else if (inGrid) {
        event.preventDefault();
        isPanning = true;
        lastTouchX = touch.clientX;
  touchStartTime = Date.now();
  touchStartPoint = { x: touch.clientX, y: touch.clientY };
  currentTouchPoint = { ...touchStartPoint };
  touchMoved = false;
      }
    }
  }

  function handleTouchMove(event: TouchEvent): void {
  if (isPinching && event.touches.length === 2) {
      // Handle pinch zoom
      event.preventDefault();
      const currentDistance = getTouchDistance(event.touches[0], event.touches[1]);
      const scale = initialPinchDistance / currentDistance;
      
      const range = zoomValues.end - zoomValues.start;
      const center = (zoomValues.start + zoomValues.end) / 2;
      const newRange = range * scale;
      
      // Clamp the zoom range between 1 and 100
      const clampedRange = Math.min(Math.max(newRange, 1), 100);
      let newStart = center - (clampedRange / 2);
      let newEnd = center + (clampedRange / 2);
      
      // Clamp the values between 0 and 100
      if (newStart < 0) {
        newStart = 0;
        newEnd = clampedRange;
      }
      if (newEnd > 100) {
        newEnd = 100;
        newStart = 100 - clampedRange;
      }
      
      updateZoom(newStart, newEnd);
      initialPinchDistance = currentDistance;
    } else if (sliderTouchActive && event.touches.length === 1) {
      // Handle slider drag
      event.preventDefault();
      const touch = event.touches[0];
      const grid = getChartGridRect();
      const width = grid.width;
      const deltaX = touch.clientX - sliderLastX;
      sliderLastX = touch.clientX;
      const deltaPercent = (deltaX / width) * 100;
      sliderTouchMoved = sliderTouchMoved || Math.abs(touch.clientX - touchStartPoint.x) > TAP_MOVE_TOLERANCE || Math.abs(touch.clientY - touchStartPoint.y) > TAP_MOVE_TOLERANCE;

      if (sliderMode === 'handle-left') {
        let newStart = zoomValues.start + deltaPercent;
        newStart = Math.min(newStart, zoomValues.end - MIN_ZOOM_RANGE);
        newStart = Math.max(0, newStart);
        updateZoom(newStart, zoomValues.end);
      } else if (sliderMode === 'handle-right') {
        let newEnd = zoomValues.end + deltaPercent;
        newEnd = Math.max(newEnd, zoomValues.start + MIN_ZOOM_RANGE);
        newEnd = Math.min(100, newEnd);
        updateZoom(zoomValues.start, newEnd);
      } else if (sliderMode === 'move-window') {
        let newStart = zoomValues.start + deltaPercent;
        let newEnd = zoomValues.end + deltaPercent;
        const range = newEnd - newStart;
        if (newStart < 0) { newStart = 0; newEnd = range; }
        if (newEnd > 100) { newEnd = 100; newStart = 100 - range; }
        updateZoom(newStart, newEnd);
      }
    } else if (isPanning && event.touches.length === 1) {
      // Handle single finger pan
      event.preventDefault();
      const touch = event.touches[0];
      const deltaX = touch.clientX - lastTouchX;
      currentTouchPoint = { x: touch.clientX, y: touch.clientY };
      const dxFromStart = Math.abs(currentTouchPoint.x - touchStartPoint.x);
      const dyFromStart = Math.abs(currentTouchPoint.y - touchStartPoint.y);
      if (dxFromStart > TAP_MOVE_TOLERANCE || dyFromStart > TAP_MOVE_TOLERANCE) {
        touchMoved = true;
      }
      const gridRect = getChartGridRect();
      const movePercent = (deltaX / gridRect.width) * (zoomValues.end - zoomValues.start);
      
      let newStart = zoomValues.start - movePercent;
      let newEnd = zoomValues.end - movePercent;
      
      // Clamp pan values between 0 and 100
      if (newStart < 0) {
        newEnd = newEnd - newStart;
        newStart = 0;
      }
      if (newEnd > 100) {
        newStart = newStart - (newEnd - 100);
        newEnd = 100;
      }
      
      updateZoom(newStart, newEnd);
      lastTouchX = touch.clientX;
    }
  }

  function handleTouchEnd(event: TouchEvent): void {
    const now = Date.now();
    const duration = now - touchStartTime;

    if (sliderTouchActive) {
      // Handle double-tap on slider to reset zoom
      const isTap = duration <= TAP_DURATION_MS && !sliderTouchMoved;
      if (isTap) {
        const sinceLast = now - lastSliderTapTime;
        const dist = Math.hypot(
          (touchStartPoint.x - lastSliderTapPoint.x),
          (touchStartPoint.y - lastSliderTapPoint.y)
        );
        if (sinceLast <= 300 && dist <= 25) {
          resetZoom();
          lastSliderTapTime = 0; // reset
        } else {
          lastSliderTapTime = now;
          lastSliderTapPoint = { ...touchStartPoint };
        }
      }
      sliderTouchActive = false;
      sliderMode = 'none';
      sliderTouchMoved = false;
    } else {
      // Detect tap in grid for tooltip
      if (!isPinching && duration <= TAP_DURATION_MS && !touchMoved) {
        showTooltipAtClientPoint(currentTouchPoint.x ? currentTouchPoint : touchStartPoint);
      }
    }

    isPinching = false;
    isPanning = false;
    initialPinchDistance = 0;
  }

  function updateZoom(start: number, end: number): void {
    zoomValues = { start, end };
    
    chart?.dispatchAction({
      type: 'dataZoom',
      dataZoomIndex: 0,
      start,
      end,
    });
    chart?.dispatchAction({
      type: 'dataZoom',
      dataZoomIndex: 1,
      start,
      end,
    });
  }

  const handleMouseDown = (e: MouseEvent) => handleStart(e);
  const handleMouseMove = (e: MouseEvent) => handleMove(e);
  const handleMouseUp = (e: MouseEvent) => handleEnd(e);
  const handleClick = (e: MouseEvent) => {
    // Show tooltip on quick click without drag
    const duration = Date.now() - mouseDownTime;
    const dx = Math.abs(e.clientX - mouseDownPoint.x);
    const dy = Math.abs(e.clientY - mouseDownPoint.y);
    if (!selectionDiv && duration <= 300 && dx <= TAP_MOVE_TOLERANCE && dy <= TAP_MOVE_TOLERANCE) {
      showTooltipAtClientPoint({ x: e.clientX, y: e.clientY });
    }
  };
  const handleTouchStartWrapper = (e: TouchEvent) => handleTouchStart(e);
  const handleTouchMoveWrapper = (e: TouchEvent) => handleTouchMove(e);
  const handleTouchEndWrapper = (e: TouchEvent) => handleTouchEnd(e);

  function resetZoom() {
    zoomValues = { start: 0, end: 100 };
    chart?.dispatchAction({
      type: 'dataZoom',
      dataZoomIndex: 0,
      start: 0,
      end: 100,
    });
    chart?.dispatchAction({
      type: 'dataZoom',
      dataZoomIndex: 1,
      start: 0,
      end: 100,
    });
  }

  onMount(async () => {
    reset([]);
    chartReady = true;
    // Mouse events
    chartDiv.addEventListener('mousedown', handleMouseDown);
    chartDiv.addEventListener('mousemove', handleMouseMove);
    chartDiv.addEventListener('mouseup', handleMouseUp);
  chartDiv.addEventListener('click', handleClick);
    // Touch events with passive: false to allow preventDefault
    chartDiv.addEventListener('touchstart', handleTouchStartWrapper, { passive: false });
    chartDiv.addEventListener('touchmove', handleTouchMoveWrapper, { passive: false });
    chartDiv.addEventListener('touchend', handleTouchEndWrapper);
  });

  onDestroy(() => {
    if (selectionDiv) {
      selectionDiv.remove();
    }
    destroyChart();
    chartDiv.removeEventListener('mousedown', handleMouseDown);
    chartDiv.removeEventListener('mousemove', handleMouseMove);
    chartDiv.removeEventListener('mouseup', handleMouseUp);
  chartDiv.removeEventListener('click', handleClick);
    chartDiv.removeEventListener('touchstart', handleTouchStartWrapper);
    chartDiv.removeEventListener('touchmove', handleTouchMoveWrapper);
    chartDiv.removeEventListener('touchend', handleTouchEndWrapper);
  });

  $: resize(width, height);
  
  $: if (chartReady) {
    reset(labels);
    update(chartData.series, chartData.secondary);
  }

  function getDataUrl(): string | undefined {
    return chart?.getDataURL();
  }

  export { getDataUrl };

</script>

<div
  class={`shadow-lg rounded-lg overflow-hidden w-full relative ${$$restProps.class}`}
  style={`height: ${height}; touch-action: none;`}
  bind:clientWidth={width}
>
  <div id="chartLine" bind:this={chartDiv}></div>
  {#if $$slots.default}
    <slot />
  {/if}
  <div class="absolute bottom-2 right-2">
    <Button on:click={resetZoom}>
      <Icon class="fill-white stroke-white" src={mdiRefresh}/>
    </Button>
  </div>
</div>

{#if wait}
  <Wait />
{/if}
