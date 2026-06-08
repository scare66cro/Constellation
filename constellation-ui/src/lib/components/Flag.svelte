<script lang="ts">
  // Inline-SVG country flags for the language switcher. Drawn as vector
  // shapes (NOT flag emoji) so they render identically on the Pi/Linux
  // Chromium kiosk, which has no flag-emoji font and would otherwise show
  // "US"/"CN" letterboxes. Aspect ratio is a clean 3:2 (viewBox 24×16).
  //   en → United States, es → Spain, fr → France, zh → China
  export let code: string = "en";
  export let w: number = 21;
  $: h = Math.round((w * 2) / 3);
  $: c = (code ?? "en").slice(0, 2).toLowerCase();

  // 5-point star, outer radius 1, pointing up — reused for the China flag.
  const STAR =
    "M0,-1L0.225,-0.309L0.951,-0.309L0.363,0.118L0.588,0.809L0,0.382L-0.588,0.809L-0.363,0.118L-0.951,-0.309L-0.225,-0.309Z";

  // US canton star-field (offset rows → star-field look, all within canton)
  const US_STARS = [
    [1.1, 1.2], [2.8, 1.2], [4.5, 1.2], [6.2, 1.2], [7.9, 1.2],
    [1.95, 3.1], [3.65, 3.1], [5.35, 3.1], [7.05, 3.1],
    [1.1, 5.0], [2.8, 5.0], [4.5, 5.0], [6.2, 5.0], [7.9, 5.0],
    [1.95, 6.9], [3.65, 6.9], [5.35, 6.9], [7.05, 6.9],
  ];
  // US red stripes (13 stripes over h=16 → 7 red); y = even index × 16/13
  const US_STRIPES = [0, 2.462, 4.923, 7.385, 9.846, 12.308, 14.769];
</script>

<span class="flag" style:width="{w}px" style:height="{h}px" title={code}>
  <svg viewBox="0 0 24 16" width="100%" height="100%" preserveAspectRatio="none" aria-hidden="true">
    {#if c === "fr"}
      <rect width="24" height="16" fill="#fff" />
      <rect width="8" height="16" fill="#0055A4" />
      <rect x="16" width="8" height="16" fill="#EF4135" />
    {:else if c === "es"}
      <rect width="24" height="16" fill="#AA151B" />
      <rect y="4" width="24" height="8" fill="#F1BF00" />
    {:else if c === "zh"}
      <rect width="24" height="16" fill="#DE2910" />
      <g fill="#FFDE00">
        <path transform="translate(5,4.2) scale(2.6)" d={STAR} />
        <path transform="translate(9.5,1.6) scale(0.9)" d={STAR} />
        <path transform="translate(11.3,3.4) scale(0.9)" d={STAR} />
        <path transform="translate(11.3,5.9) scale(0.9)" d={STAR} />
        <path transform="translate(9.5,7.6) scale(0.9)" d={STAR} />
      </g>
    {:else}
      <!-- United States (default / en) -->
      <rect width="24" height="16" fill="#fff" />
      <g fill="#B22234">
        {#each US_STRIPES as y}
          <rect {y} width="24" height="1.231" />
        {/each}
      </g>
      <rect width="9.6" height="8.615" fill="#3C3B6E" />
      <g fill="#fff">
        {#each US_STARS as [cx, cy]}
          <circle cx={cx} cy={cy} r="0.4" />
        {/each}
      </g>
    {/if}
  </svg>
</span>

<style>
  .flag {
    display: inline-block;
    border-radius: 3px;
    overflow: hidden;            /* clips the square SVG into the rounded chip */
    line-height: 0;
    box-shadow: 0 0 0 1px rgba(0, 0, 0, 0.35);  /* hairline edge on any bg */
    flex: none;
  }
  .flag svg { display: block; }
</style>
