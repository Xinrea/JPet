<script>
  import { achievementIconPaths } from "../assets/achievement-icons.js";

  export let name = "trophy";
  export let size = 24;
  $: paths = achievementIconPaths[name] ?? achievementIconPaths.trophy;
</script>

<svg
  xmlns="http://www.w3.org/2000/svg"
  width={size}
  height={size}
  viewBox="0 0 24 24"
  fill="none"
  stroke="currentColor"
  stroke-width="1.65"
  stroke-linecap="round"
  stroke-linejoin="round"
  aria-hidden="true"
  focusable="false"
>
  {#each paths as path}<path d={path} />{/each}
</svg>
