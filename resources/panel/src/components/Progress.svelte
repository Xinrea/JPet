<script>
  import { afterUpdate, onMount } from "svelte";
  import { reportFrontend } from "../logger.js";

  export let value = 0;
  export let max = 100;
  const circumference = 2 * Math.PI * 45;
  $: progress = Math.min(max, Math.max(0, Number(value) || 0));
  $: progressRatio = max > 0 ? progress / max : 0;
  $: progressOffset = circumference * (1 - progressRatio);

  let root;
  let renderCount = 0;
  let renderReportCount = 0;
  let lastRenderReport = 0;

  function renderSnapshot() {
    const circle = root?.querySelector(".progress-value");
    const style = circle && typeof getComputedStyle === "function"
      ? getComputedStyle(circle)
      : null;
    return {
      value,
      max,
      progress,
      progressRatio,
      progressOffset,
      renderCount,
      visibilityState: document.visibilityState,
      hidden: document.hidden,
      circleFound: Boolean(circle),
      dashArray: circle?.getAttribute("stroke-dasharray"),
      dashOffset: circle?.getAttribute("stroke-dashoffset"),
      inlineStyle: circle?.getAttribute("style"),
      computedDashOffset: style?.strokeDashoffset,
    };
  }

  function reportRender(event, force = false) {
    const now =
      typeof performance !== "undefined" && typeof performance.now === "function"
        ? performance.now()
        : Date.now();
    if (!force && now - lastRenderReport < 5000) {
      return;
    }
    lastRenderReport = now;
    renderReportCount += 1;
    reportFrontend(
      renderReportCount === 1 ? "info" : "debug",
      `[Progress] ${event}`,
      renderSnapshot(),
    );
  }

  onMount(() => {
    reportFrontend("info", "[Progress] mounted", renderSnapshot());
    return () => {
      reportFrontend("info", "[Progress] destroyed", {
        renderCount,
        value,
        max,
      });
    };
  });

  afterUpdate(() => {
    renderCount += 1;
    reportRender("render update");
  });
</script>

<div bind:this={root} data-progress-value={progress} data-progress-offset={progressOffset}>
  <svg viewBox="0 0 100 100">
    <circle class="progress-track" cx="50" cy="50" r="45" />
    <circle
      fill="none"
      class="progress-value"
      cx="50"
      cy="50"
      r="45"
      stroke-dasharray={circumference}
      style={`stroke-dashoffset: ${progressOffset};`}
    />
  </svg>
  <div>
    <slot>
      <span>{value}</span>
    </slot>
  </div>
</div>

<style>
  svg {
    fill: var(--progress-fill, transparent);
    height: 100%;
    position: absolute;
    stroke-linecap: var(--progress-linecap, round);
    width: 100%;
  }
  .progress-track {
    fill: none;
    stroke: var(--progress-trackcolor, grey);
    stroke-width: var(--progress-trackwidth, 9px);
  }
  .progress-value {
    stroke: var(--progress-color, #79ca2e);
    stroke-width: var(--progress-width, 10px);
    transform: rotate(-90deg);
    transform-origin: 50% 50%;
    transition: stroke-dashoffset 60ms linear;
  }
  div {
    height: 100%;
    position: relative;
    width: 100%;
  }
  span {
    left: 50%;
    position: absolute;
    top: 50%;
    transform: translate(-50%, -50%);
  }
</style>
