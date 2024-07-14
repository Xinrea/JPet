<script>
  const levelToExp = (/** @type {number} */ level) => {
    return Math.ceil(level * level + 30);
  };
</script>

<div>
  <p class="font-medium text-xs">经验获取：1 经验 = 挂机 1 分钟</p>
  <p class="font-medium text-xs">*轴伊直播时经验获取 ×10</p>
  <!-- table -->
  <table class="min-w-full divide-y divide-gray-200 dark:divide-gray-700">
    <thead>
      <tr>
        <th
          class="px-6 py-3 text-xs font-medium tracking-wider text-left text-gray-500 dark:text-gray-400 uppercase"
        >
          等级
        </th>
        <th
          class="px-6 py-3 text-xs font-medium tracking-wider text-left text-gray-500 dark:text-gray-400 uppercase"
        >
          升级所需经验
        </th>
        <th
          class="px-6 py-3 text-xs font-medium tracking-wider text-left text-gray-500 dark:text-gray-400 uppercase"
        >
          等级
        </th>
        <th
          class="px-6 py-3 text-xs font-medium tracking-wider text-left text-gray-500 dark:text-gray-400 uppercase"
        >
          升级所需经验
        </th>
      </tr>
    </thead>
    <tbody class="bg-white divide-y divide-gray-200 dark:divide-gray-700">
      {#each { length: 27 } as _, i}
        <tr>
          <td
            class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-200"
          >
            {i}
          </td>
          <td
            class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-200"
          >
            {levelToExp(i)}
          </td>
          <td
            class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-200"
          >
            {i + 27}
          </td>
          <td
            class="px-6 py-4 whitespace-nowrap text-sm text-gray-900 dark:text-gray-200"
          >
            {levelToExp(i + 27)}
          </td>
        </tr>
      {/each}
    </tbody>
  </table>
</div>
