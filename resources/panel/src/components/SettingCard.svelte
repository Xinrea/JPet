<script>
  import UiIcon from "./UiIcon.svelte";
  export let title;
  export let description = "";
  export let icon = "settings";
</script>

<section class="setting-card game-card">
  <div class="setting-card-heading">
    <span class="setting-card-icon"><UiIcon name={icon} size={20} /></span>
    <div><h2>{title}</h2>{#if description}<p>{description}</p>{/if}</div>
    <span class="heading-stripes" aria-hidden="true"></span>
  </div>
  <div class="setting-card-body"><slot /></div>
</section>
