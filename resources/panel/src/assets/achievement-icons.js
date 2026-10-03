// A shared 24 × 24 drawing grid and rounded strokes keep the collection quiet
// and consistent. Related milestones share a motif with different details.
const sprout = ["M12 21v-9", "M12 15C5 15 3 11 3 6c6 0 9 3 9 9Z", "M12 12c0-5 3-8 9-8 0 5-3 8-9 8Z"];
const leaf = ["M5 19c-4-7 0-14 14-14 0 14-7 18-14 14Z", "m5 19 9-9m-5 5v-5m0 5h5"];
const calendar = ["M5 5h14a1 1 0 0 1 1 1v14H4V6a1 1 0 0 1 1-1Z", "M8 3v4m8-4v4M4 10h16"];
const shoe = ["m4 5 5 1 1 6 3 2 5 1c2 0 3 2 3 4H3v-5l1-9Z", "m10 12-3 1m6 1-3 2M3 16h4"];
const dumbbell = ["M3 8h4v8H3Zm14 0h4v8h-4ZM7 12h10M1 10v4m22-4v4"];
const flame = ["M12 3c1 5 6 6 6 11a6 6 0 0 1-12 0c0-3 2-5 3-7 0 3 1 4 3 4V3Z", "M12 14c-3 3-2 6 0 7 3-1 3-4 0-7Z"];
const heart = ["M12 20 4 12C-2 6 7 0 12 7c5-7 14-1 8 5l-8 8Z"];
const star = ["m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2-5.6-2.9-5.6 2.9 1.1-6.2L3 9.6l6.2-.9L12 3Z"];
const coins = ["M16 8c0 2-3 3-6 3s-6-1-6-3 3-3 6-3 6 1 6 3Z", "M4 8v8c0 2 3 3 6 3s6-1 6-3V8M4 12c2 3 10 3 12 0"];
const trophy = ["M7 3h10v6a5 5 0 0 1-10 0V3Z", "M7 5H3v3c0 3 2 4 5 4m9-7h4v3c0 3-2 4-5 4M12 14v6m-4 1h8"];
const medal = ["m7 3 2 7m8-7-2 7M5 3h14", "M18 15a6 6 0 1 1-12 0 6 6 0 0 1 12 0Z", "m12 12 1 2 2 .3-1.5 1.4.4 2.1-1.9-1-1.9 1 .4-2.1L9 14.3l2-.3 1-2Z"];
const monitor = ["M3 4h18v13H3ZM8 21h8m-4-4v4", "m10 8 5 3-5 3V8Z"];
const controller = ["M8 7h8c3 0 4 3 5 8s-2 6-5 2l-1-1H9l-1 1c-3 4-6 3-5-2s2-8 5-8Z", "M6 11v4m-2-2h4M16 12h.1m2 2h.1"];
const crown = ["m3 6 5 4 4-7 4 7 5-4-2 12H5L3 6Z", "M5 21h14m-9-7h4"];

export const achievementIconPaths = Object.freeze({
  trophy,
  check: ["m5 12 4 4L19 6"],
  close: ["m6 6 12 12M6 18 18 6"],
  laurel: ["M9 20C1 16 2 7 7 3m8 17c8-4 7-13 2-17", "m4 6 3 2M3 11l4 1m-2 4 3-1m12-9-3 2m4 3-4 1m2 4-3-1", "m9 12 2 2 4-5"],
  hello: sprout,
  hour: ["M4 9h12v7a4 4 0 0 1-4 4H8a4 4 0 0 1-4-4V9Z", "M16 10h2a3 3 0 0 1 0 6h-2M8 3v3m4-3v3M2 22h16"],
  ten_hours: ["m6 15 1 6h10l1-6H6Z", ...sprout.slice(1), "M12 15v-3"],
  fifty_hours: ["m3 11 9-8 9 8M5 9v12h14V9M10 21v-7h4v7"],
  days_3: [...calendar, "M8 14h2m4 0h2m-8 3h2"],
  days_7: [...calendar, "m8 15 3 3 5-5"],
  days_30: ["M20 15A8 8 0 1 1 9 4a7 7 0 0 0 11 11Z", "M17 3v4m-2-2h4"],
  touch_1: ["M8 12V6a1.5 1.5 0 0 1 3 0v6-8a1.5 1.5 0 0 1 3 0v8-6a1.5 1.5 0 0 1 3 0v7-3a1.5 1.5 0 0 1 3 0v6c0 4-3 6-7 6h-1c-2 0-4-2-5-4l-3-5a1.5 1.5 0 0 1 2.5-1.5L8 12Z"],
  touch_50: heart,
  touch_200: ["M11 20 4 13C0 9 6 4 10 9c4-5 10 0 6 4l-5 7Z", "M14 4c5-3 11 3 6 7l-2 2"],
  speed_10: shoe,
  speed_50: ["M3 8h12c5 0 5-6 1-6-2 0-3 1-3 2M3 12h16c4 0 4 5 0 5M3 16h6c5 0 5 6 1 6-2 0-3-1-3-2"],
  endurance_10: leaf,
  endurance_50: ["M3 7h16v10H3ZM19 10h2v4h-2", "m11 8-3 5h4l-1 3 5-5h-5l1-3Z"],
  strength_10: dumbbell,
  strength_50: ["M3 5h4v14H3Zm14 0h4v14h-4ZM7 12h10M1 8v8m22-8v8", "M10 3h4m-2-2v4"],
  will_10: flame,
  will_50: ["M12 21 4 14C-1 9 4 4 8 6", "M16 6c4-2 9 3 4 8l-8 7", "M12 2c0 4 4 4 4 7a4 4 0 0 1-8 0c0-2 2-3 2-4l2 3V2Z"],
  intellect_10: ["M9 17c0-4-4-4-4-8a7 7 0 0 1 14 0c0 4-4 4-4 8H9Z", "M9 20h6m-5 3h4m-2-6v-6m-2-1 2 2 2-2"],
  intellect_50: ["M12 6C9 3 5 3 2 4v15c4-1 7 0 10 2 3-2 6-3 10-2V4c-3-1-7-1-10 2v15", "M6 8h2m-2 4h2m8-4h2m-2 4h2"],
  balanced_10: ["m12 3 9 7-3 11H6L3 10l9-7Z", "M12 3v10m9-3-9 3m6 8-6-8m-6 8 6-8m-9-3 9 3"],
  balanced_50: star,
  balanced_100: ["m7 3-5 6 10 13L22 9l-5-6H7Z", "M2 9h20M7 3l5 19 5-19"],
  exp_10000: coins,
  exp_100000: [...coins, "M20 10v9c0 2-3 3-6 3m6-8c0 2-2 3-4 3"],
  success_1: ["M5 22V3h13l-3 4 3 4H5"],
  success_10: medal,
  success_50: trophy,
  queued_5: ["M9 3h6v4H9ZM9 5H5v17h14V5h-4", "M8 11h8m-8 4h8m-8 4h5"],
  failure_1: ["M18 4C4 1 1 10 6 17c7 5 16 2 12-13Z", "M4 21 14 9m-5 5v-4m0 4h4M19 19l2 2m0-2-2 2"],
  recovery: ["M4 12a8 8 0 1 0 2-5M3 3v5h5", "m9 13 3-4 3 4m-3-4v8"],
  streak_5: ["m14 2-9 12h6l-1 8 9-12h-6l1-8Z"],
  variety_7: ["M20 12a8 8 0 1 1-8-8M16 12a4 4 0 1 1-4-4", "m12 12 9-9m-4 0h4v4"],
  task_1: ["M9 3h6v3H9Zm0 3v3l-3 4v8h12v-8l-3-4V6M6 15h12"],
  task_2: ["M15 5a2 2 0 1 1-4 0 2 2 0 0 1 4 0Z", "m3 11 5-1 3-3 4 4 5 1m-9-5-2 7 5 2 1 5m-6-7-3 6H2"],
  task_3: monitor,
  task_4: ["M18 12a6 6 0 1 1-12 0 6 6 0 0 1 12 0Z", "M4 6 2 8v8l2 2m16-12 2 2v8l-2 2M9 2h6m-3 0v4"],
  task_5: ["M3 8h18v13H3ZM5 8l13-6", "M10 15a3 3 0 1 1-6 0 3 3 0 0 1 6 0Zm4-3h4m-4 4h4"],
  task_6: controller,
  task_7: ["M9 5a3 3 0 0 1 6 0v7a3 3 0 0 1-6 0V5Z", "M6 10v2a6 6 0 0 0 12 0v-2M12 18v4m-4 0h8"],
  task_9: ["m6 4 5 2-2 6-5 5c-3 3-2 5 1 5h3l5-7 2-9", "M14 6h5l-1 7c4 4 5 8 2 9h-3l-4-5"],
  task_10: ["M8 6V3h8v3M3 6h18v15H3ZM8 6v15m8-15v15"],
  task_11: ["m10 8 2-2a5 5 0 0 1 7 7l-3 3a5 5 0 0 1-7 0m5 0-2 2a5 5 0 0 1-7-7l3-3a5 5 0 0 1 7 0"],
  task_13: crown,
  dress: ["M9 3v4h6V3m-6 4 1 4-5 10h14l-5-10 1-4M10 11h4"],
  winter: ["m9 3 3 4 3-4 5 4-2 6-2-2v10H8V11l-2 2-2-6 5-4Z", "M12 7v14M9 3l-1 5 4-1 4 1-1-5M12 11h.1m-.1 4h.1"],
  wardrobe: ["M10 5a2 2 0 1 1 3 2l-1 1v3L2 18v2h20v-2l-10-7", "M7 20h10"],
  star_1: star,
  star_3: ["m12 5 2 4 4.5.7-3.2 3.1.7 4.5-4-2.1-4 2.1.7-4.5-3.2-3.1L10 9l2-4Z", "M3 3h.1M21 3h.1M12 22h.1"],
  star_5: ["m12 6 1.8 3.6 4 .6-2.9 2.8.7 4-3.6-1.9L8.4 17l.7-4-2.9-2.8 4-.6L12 6Z", "M8 2C-3 6 1 21 12 22c11-1 15-16 4-20M3 12H1m22 0h-2"],
});
