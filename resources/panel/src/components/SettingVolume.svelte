<script>
  import { createEventDispatcher } from "svelte";
  import UiIcon from "./UiIcon.svelte";
  export let label;
  export let description = "";
  export let volume = 100;
  export let muted = false;
  // Dims the row while another setting (the master mute) silences it anyway.
  export let silenced = false;
  const dispatch = createEventDispatcher();

  function toggleMute() {
    muted = !muted;
    dispatch("change");
  }
  // Like system volume controls, moving a muted slider unmutes it.
  function slide() {
    muted = false;
  }
</script>

<div class="setting-volume" class:volume-silenced={silenced}>
  <div class="setting-volume-copy"><strong>{label}</strong><span>{muted ? "已静音" : `${volume}%`}</span></div>
  {#if description}<p>{description}</p>{/if}
  <div class="volume-control">
    <button type="button" class="mute-button" class:muted aria-pressed={muted} aria-label={`${label}静音`} title={muted ? "取消静音" : "静音"} on:click={toggleMute}>
      <UiIcon name={muted ? "mute" : "sound"} size={18} />
    </button>
    <input type="range" min="0" max="100" step="1" aria-label={label} class:muted bind:value={volume} on:input={slide} on:change />
  </div>
</div>
