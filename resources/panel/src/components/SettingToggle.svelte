<script>
  export let checked = false;
  export let label;
  export let description = "";
  export let disabled = false;
</script>

<label class="setting-toggle" class:toggle-disabled={disabled}>
  <span class="setting-toggle-copy"><strong>{label}</strong>{#if description}<span>{description}</span>{/if}</span>
  <span class="game-switch">
    <input type="checkbox" bind:checked {disabled} on:change />
    <span class="game-switch-track" aria-hidden="true"><span>{checked ? "ON" : "OFF"}</span></span>
  </span>
</label>
