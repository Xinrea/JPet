<script>
  export let local_version = "";
  export let latest_version = "";
  function openDataFolder() {
    fetch("/api/config/folder", { method: "POST" });
  }
  function openlink(link) {
    fetch("/api/openlink", { method: "POST", body: JSON.stringify({link: link})});
  }
</script>

<div>
  <p class="font-medium text-lg">版本信息</p>
  <p class="font-medium text-sm">当前版本：{local_version}</p>
  <p class="font-medium text-sm">最新版本：{latest_version}</p>
  <p class="font-medium text-sm"><a class="underline decoration-green-500 decoration-2 font-bold" href={"#"} on:click={()=>openlink("https://pet.vjoi.cn")}>查看最新版本信息</a></p>
  <p class="font-medium text-lg mt-4">关于<b>经验值</b></p>
  <p class="font-medium text-sm">经验获取：1 经验 = 挂机 1 分钟。</p>
  <p class="font-medium text-sm">随着智力的提升，每分钟获取的经验值会逐渐增加，最多 500 点。</p>
  <p class="font-medium text-lg mt-4">关于<b>速度</b></p>
  <p class="font-medium text-sm">速度会缩短任务完成所需的时间，进行中的任务不受速度变化的影响。</p>
  <p class="font-medium text-lg mt-4">关于<b>任务队列</b></p>
  <p class="font-medium text-sm">任务结束后自动结算并发放奖励，随后执行队列中的下一项。每个排队任务只执行一次，队列为空时停止。</p>
  <p class="font-medium text-sm">初始可排 2 个待执行任务，每颗星增加 1 个位置，运行中的任务不占队列容量。队列可调整顺序、移除，并会在退出后保留。</p>
  <p class="font-medium text-sm">成功率为 0 的队首任务会等待属性提升，也可以将其他任务移到它前面。</p>
  <p class="font-medium text-lg mt-4">关于<b>毅力</b></p>
  <p class="font-medium text-sm">毅力会影响任务的成功率，当毅力为 0 的情况下，任务成功率最高为 70%；天有不测风云，任务成功率不会超过 95%。</p>
  <p class="font-medium text-lg mt-4">配置文件与游戏数据</p>
  <p class="font-medium text-sm">本程序配置文件与游戏数据均在本地存储，位于用户文档目录下，点击 <a class="underline decoration-green-500 decoration-2 font-bold" href={"#"} on:click={()=>openDataFolder()}>此处</a> 打开目录。</p>
  <p class="font-medium text-lg mt-4">动态与直播提醒</p>
  <p class="font-medium text-sm">由于 B 站风控机制的限制，添加太多监控目标会导致状态更新不及时，请尽量登录使用。</p>
  <p class="font-medium text-lg mt-4">轴芯等级</p>
  <p class="font-medium text-sm">轴伊直播间粉丝勋章等级。</p>
</div>
