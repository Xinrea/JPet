<script>
  import { onMount, onDestroy } from "svelte";
  import { Tooltip, Button, Modal } from "flowbite-svelte";
  import speedIcon from "../assets/at-sp.png";
  import enduranceIcon from "../assets/at-end.png";
  import strengthIcon from "../assets/at-str.png";
  import willIcon from "../assets/at-will.png";
  import intellectIcon from "../assets/at-int.png";
  import imgClothes1 from "../assets/c1.png";
  import imgClothes2 from "../assets/c2.png";
  import imgClothes3 from "../assets/c3.png";
  import starIcon from "../assets/star.png";
  import crownIcon from "../assets/crown.png";
  import starOutlineIcon from "../assets/star_outline.png";
  import UiIcon from "../components/UiIcon.svelte";
  import Progress from "../components/Progress.svelte";
  import BuffIcon from "../components/BuffIcon.svelte";

  const clothesImages = [imgClothes1, imgClothes2, imgClothes3];
  const attributeNames = ["速度", "耐力", "力量", "毅力", "智力"];
  const attributeIcons = [speedIcon, enduranceIcon, strengthIcon, willIcon, intellectIcon];
  const attributeDescriptions = ["影响任务完成所需的时间", "任务所需的基础属性", "任务所需的基础属性", "影响任务完成的成功率", "影响经验获取的效率"];
  const attributeArray = [
    "speed",
    "endurance",
    "strength",
    "will",
    "intellect",
  ];

  export let clothes = {
    current: 0,
    unlock: [true, false, false],
  };

  let clothesList = [
    {
      id: 0,
      name: "绿色",
    },
    {
      id: 1,
      name: "粉色",
    },
    {
      id: 2,
      name: "冬装",
    },
  ];

  export let attributes = {
    exp: 0,
    speed: 0,
    endurance: 0,
    strength: 0,
    will: 0,
    intellect: 0,
    buycnt: 0,
  };

  export let expdiff = 0;

  export let buffs = ["live", "dynamic", "guard"];

  export let starcnt = 0;
  export let online = false;
  export let expProgress = 0;
  export let buycost = 0;
  export let revertgain = 0;
  export let starAvailable = false;
  let error = "";
  let busy = false;

  $: currentExp = attributes.exp;
  let timeToNextPoint = 60;
  let receivedAt = 0;
  $: { expProgress; receivedAt = performance.now(); }
  function updateTimeToNextPoint() {
    const elapsed = online ? (performance.now() - receivedAt) / 1000 : 0;
    timeToNextPoint = Math.max(0, 60 - expProgress - elapsed);
  }
  onMount(() => {
    const timer = setInterval(updateTimeToNextPoint, 100);
    return () => clearInterval(timer);
  });
  async function gameAction(url, method = "POST") {
    if (!online || busy) return;
    busy = true; error = "";
    try {
      const response = await fetch(url, { method });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || "操作失败，请重试");
    } catch (failure) { error = failure.message; }
    finally { busy = false; }
  }

  // modal
  let addModal = false;
  let revertModal = false;
  let targetAttr = "speed";
  function attrHandle() {
    if (currentExp < buycost) {
      return;
    }
    if (attributes[targetAttr] >= 100 + 10 * starcnt) {
      return;
    }
    gameAction(`/api/attr/${targetAttr}`);
  }
  function revertHandle() {
    if (attributes[targetAttr] <= 0) {
      error = "属性值不足"; return;
    }
    gameAction(`/api/attr/${targetAttr}`, "DELETE");
  }

  function changeClothes(id) {
    if (!clothes.unlock[id] || id == clothes.current) {
      return;
    }
    gameAction(`/api/clothes/${id}`);
  }

  let starModal = false;

  function fetchStar() {
    gameAction("/api/star");
    starModal = false;
  }

  const tooltips = [
    "前期智力对经验值的加成非常可观",
    "一些任务是可重复完成的",
    "关闭或断网时，任务与经验进度会暂停",
    "我的头围是 53cm",
    "经验值可用于加点，但加点的消耗会越来越多",
    "装扮完可以试试给我拍张照",
    "没有毅力的话，任务的成功率最高为 70%",
    "点击发团可以快速打开面板",
    "登录账号后，轴芯等级可以提升经验获取量",
    "轴伊的一些活动可以触发经验加成",
    "鼠标中键点击加点可以跳过确认",
  ];
  let cur_tip = 0;
  const tipTimer = setInterval(() => {
    cur_tip = Math.floor(Math.random() * tooltips.length);
  }, 10000);
  onDestroy(() => clearInterval(tipTimer));
</script>

<div class="profile-page">
  {#if error}<p class="feedback error" role="alert">{error}</p>{/if}
  <section class="companion-card game-card">
    <div class="character-stage"><span class="character-spark spark-one">✦</span><span class="character-spark spark-two">✧</span><img src={clothesImages[clothes.current]} alt="轴伊的当前衣装" /><span class="character-name">轴伊 <small>Joi</small></span></div>
    <div class="growth-summary"><span class="growth-eyebrow">一起积攒的成长</span>
      <div class="exp-ring"><Progress max={60} value={60 - timeToNextPoint}><div class="exp-value"><strong>{currentExp.toLocaleString()}</strong><span>EXP</span></div></Progress></div>
      <p class="next-exp">{online ? `下次获得 +${expdiff} EXP` : "连接云端后继续成长"}</p>
      <div class="stars-row"><span>当前星级</span><button class="star-button" aria-label="提升星级" disabled={!online || busy} on:click={() => starModal = true}>
        {#if starcnt > 0}{#each { length: Math.floor(starcnt / 5) } as _}<img alt="五星" src={crownIcon} />{/each}{#each { length: starcnt % 5 } as _}<img alt="一星" src={starIcon} />{/each}{:else}<img alt="零星" src={starOutlineIcon} />{/if}<span>＋</span>
      </button><Tooltip>星星提升经验获取量和属性上限、降低任务成功率，也可消耗星星解锁任务队列容量</Tooltip></div>
      <div id="buff-ref" class="buff-row">{#each buffs as buff}<BuffIcon type={buff} />{:else}<span>期待下一次成长加成</span>{/each}</div>
    </div>
  </section>

  <section class="attribute-section"><div class="section-title"><UiIcon name="spark" size={18} /><h2>成长属性</h2><span>上限 {100 + 10 * starcnt}</span></div>
    <div class="attribute-grid">{#each attributeArray as attr, i}<div class="attribute-card game-card" style={`--attribute-color: ${["#6bb8c5", "#e58ca4", "#e8ad51", "#9e9bd0", "#80b554"][i]}`}>
      <div class="attribute-name"><img src={attributeIcons[i]} alt="" /><span>{attributeNames[i]}</span></div><Tooltip>{attributeDescriptions[i]}</Tooltip>
      <strong class="attribute-value">{attributes[attr]}</strong>
      <div class="attribute-actions"><button aria-label={`撤销${attributeNames[i]}属性`} disabled={busy || !online} on:mousedown={(event) => { if (event.button === 1) { targetAttr = attr; revertHandle(); } }} on:click={() => { revertModal = true; targetAttr = attr; }}>−</button><button aria-label={`增加${attributeNames[i]}属性`} disabled={busy || !online} on:mousedown={(event) => { if (event.button === 1) { targetAttr = attr; attrHandle(); } }} on:click={() => { addModal = true; targetAttr = attr; }}>＋</button></div>
    </div>{/each}</div>
  </section>

  <section class="outfit-section game-card"><div class="section-title"><UiIcon name="dress" size={18} /><h2>今日衣装</h2><span>选择你喜欢的样子</span></div>
    <div class="outfit-grid">{#each clothesList as outfit, i}<button class="outfit-choice" class:selected={clothes.current === i} disabled={busy || !online || !clothes.unlock[i]} aria-pressed={clothes.current === i} on:click={() => changeClothes(i)}><img src={clothesImages[i]} alt="" /><span><strong>{outfit.name}</strong><small>{!clothes.unlock[i] ? "尚未解锁" : clothes.current === i ? "正在穿着" : "点击换装"}</small></span>{#if clothes.current === i}<UiIcon name="check" size={15} />{/if}</button>{#if !clothes.unlock[i]}<Tooltip>目前还未解锁，请提升属性完成任务解锁</Tooltip>{/if}{/each}</div>
  </section>
  <div class="profile-tip"><UiIcon name="spark" size={18} /><p><strong>陪伴小贴士</strong>{tooltips[cur_tip]}</p></div>
</div>

<Modal title="属性加点" bind:open={addModal} size="xs" autoclose><p class="mb-5 text-sm leading-relaxed text-gray-500">此次操作需要消耗 {buycost} EXP，后续撤销仅会返还一半，确认加点吗？</p><div class="setting-actions"><Button disabled={busy || !online || currentExp < buycost || attributes[targetAttr] >= 100 + 10 * starcnt} on:click={attrHandle}>确认</Button><Button color="alternative">取消</Button></div></Modal>
<Modal title="属性撤销" bind:open={revertModal} size="xs" autoclose><p class="mb-5 text-sm leading-relaxed text-gray-500">此次操作会返还 {revertgain} EXP，确认撤销吗？</p><div class="setting-actions"><Button disabled={busy || !online} on:click={revertHandle}>确认</Button><Button color="alternative">取消</Button></div></Modal>
<Modal title="星级提升" bind:open={starModal} size="xs" autoclose><p class="mb-5 text-sm leading-relaxed text-gray-500">此次操作会消耗所有属性各 53 点换取一颗星，要换取吗？</p><div class="setting-actions"><Button disabled={!starAvailable || !online || busy} on:click={fetchStar}>确认</Button><Button color="alternative">取消</Button></div></Modal>

<style>
  .profile-page { display: flex; flex-direction: column; gap: 22px; }
  .companion-card { display: grid; grid-template-columns: 1fr 1fr; overflow: hidden; }
  .character-stage { position: relative; display: flex; align-items: center; justify-content: center; padding: 20px 20px 45px; min-height: 295px; background: radial-gradient(ellipse at 50% 50%, #fff 0 32%, transparent 32.3%), repeating-linear-gradient(135deg, #ffffff44 0 12px, transparent 12px 24px), linear-gradient(135deg, #e7f5d5, #f7f9e8); }
  .character-stage > img { height: 235px; width: auto; filter: drop-shadow(0 7px 2px #94b16222); z-index: 1; }
  .character-name { position: absolute; bottom: 16px; font-size: 16px; font-weight: 900; color: var(--ink); }
  .character-name small { font-size: 10px; margin-left: 8px; color: #9aab81; letter-spacing: 2px; }
  .character-spark { position: absolute; color: #b5d679; }
  .spark-one { top: 25px; left: 20%; font-size: 26px; }
  .spark-two { bottom: 70px; right: 14%; font-size: 30px; }
  .growth-summary { display: flex; flex-direction: column; align-items: center; justify-content: center; padding: 22px 18px; }
  .growth-eyebrow { font-size: 11px; font-weight: 800; color: #9da488; margin-bottom: 13px; }
  .exp-ring { width: 132px; height: 132px; --progress-trackcolor: #edf2e4; --progress-trackwidth: 7px; --progress-width: 7px; --progress-color: #94cf43; }
  .exp-value { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; }
  .exp-value strong { font-size: 24px; color: var(--ink); font-weight: 900; font-variant-numeric: tabular-nums; }
  .exp-value span { font-size: 11px; color: #bea370; font-weight: 900; letter-spacing: 2px; }
  .next-exp { margin-top: 12px; font-size: 10px; color: var(--muted); }
  .stars-row { display: flex; align-items: center; gap: 9px; margin-top: 18px; font-size: 10px; color: var(--muted); }
  .star-button { display: flex; align-items: center; gap: 2px; flex-wrap: wrap; padding: 4px 6px; border: 1px solid #f1e4c0; border-radius: 7px; background: #fffaf0; max-width: 150px; }
  .star-button img { width: 18px; height: 18px; object-fit: contain; }
  .star-button > span { font-size: 14px; color: #b99548; }
  .buff-row { display: flex; align-items: center; justify-content: center; gap: 6px; margin-top: 15px; min-height: 32px; }
  .buff-row > span { color: #b2b99e; font-size: 10px; }
  .section-title { display: flex; align-items: center; gap: 8px; color: #91b565; margin-bottom: 13px; }
  .section-title h2 { font-size: 14px; font-weight: 900; color: var(--ink); }
  .section-title > span { margin-left: auto; font-size: 10px; color: #a3a691; }
  .attribute-grid { display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); gap: 10px; }
  .attribute-card { text-align: center; overflow: hidden; }
  .attribute-name { display: flex; align-items: center; justify-content: center; gap: 5px; background: var(--attribute-color); padding: 9px 2px; color: white; font-size: 12px; font-weight: 800; }
  .attribute-name img { width: 15px; height: 15px; object-fit: contain; }
  .attribute-value { display: block; padding: 12px 0 8px; font-size: 24px; font-weight: 900; font-variant-numeric: tabular-nums; }
  .attribute-actions { display: flex; gap: 6px; justify-content: center; padding: 0 8px 10px; }
  .attribute-actions button { flex: 1; max-width: 35px; border: 1px solid #e5e7da; border-radius: 5px; background: linear-gradient(#fff, #f5f6ed); color: #849b65; font-weight: 800; font-size: 14px; box-shadow: 0 2px 0 #e6e8db; }
  .attribute-actions button:hover { background: #edf6df; }
  .outfit-section { padding: 17px; }
  .outfit-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 12px; }
  .outfit-choice { display: flex; align-items: center; position: relative; text-align: left; gap: 10px; border: 1px solid #e7e8db; border-radius: 10px; background: #fafbf5; padding: 9px; }
  .outfit-choice img { height: 58px; width: 34px; object-fit: cover; object-position: top; }
  .outfit-choice strong { display: block; font-size: 12px; font-weight: 800; }
  .outfit-choice small { display: block; font-size: 10px; color: #a4ac91; margin-top: 5px; white-space: nowrap; }
  .outfit-choice.selected { border-color: #abcf7b; background: #f2f9e7; box-shadow: inset 0 0 0 1px #daebc3; }
  .outfit-choice :global(svg) { position: absolute; right: 7px; top: 7px; color: #79a844; }
  .outfit-choice:disabled:not(.selected) img { filter: grayscale(.8); opacity: .5; }
  .profile-tip { display: flex; align-items: center; gap: 10px; padding: 14px 17px; border: 1px dashed #cddcba; border-radius: 10px; background: #f9fcf2; color: #9aaf77; }
  .profile-tip :global(svg) { flex-shrink: 0; }
  .profile-tip p { font-size: 11px; line-height: 1.9; color: var(--muted); }
  .profile-tip strong { margin-right: 9px; color: #839964; }
  @media (max-width: 460px) { .attribute-grid { gap: 5px; } .attribute-name { font-size: 11px; gap: 2px; } .attribute-name img { width: 12px; } .attribute-value { font-size: 20px; } .attribute-actions { padding: 0 5px 9px; gap: 4px; } .character-stage { padding: 15px 10px 40px; min-height: 265px; } .character-stage > img { height: 200px; } .growth-summary { padding: 18px 8px; } .exp-ring { width: 112px; height: 112px; } .exp-value strong { font-size: 20px; } .outfit-grid { gap: 6px; } .outfit-choice { flex-direction: column; text-align: center; gap: 5px; padding: 10px 4px; } .outfit-section { padding: 14px; } }
</style>
