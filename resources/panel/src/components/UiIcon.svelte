<script>
  export let name = "home";
  export let size = 20;
  const paths = {
    home: "M3 10 12 3l9 7v10a1 1 0 0 1-1 1h-5v-7H9v7H4a1 1 0 0 1-1-1Z",
    task: "M5 5h14v16H5ZM9 3h6v4H9ZM8 11l1.5 1.5L12 10m2 2h2m-8 5h8",
    trophy: "M8 3h8v7a4 4 0 0 1-8 0ZM8 5H4v3a4 4 0 0 0 4 4m8-7h4v3a4 4 0 0 1-4 4m-4 2v5m-4 2h8m-6-2h4",
    dress: "m8 3-5 3 2 5 3-1v11h8V10l3 1 2-5-5-3a4 4 0 0 1-8 0Z",
    settings: "M10 3h4l1 3 3-1 2 3-2 3 2 3-2 3-3-1-1 3h-4l-1-3-3 1-2-3 2-3-2-3 2-3 3 1ZM12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8",
    rank: "M3 13h6v8H3Zm6-9h6v17H9Zm6 5h6v12h-6",
    book: "M12 5c-3-2-6-2-9-1v15c3-1 6-1 9 1m0-15c3-2 6-2 9-1v15c-3-1-6-1-9 1ZM7 8h2m-2 4h2m6-4h2m-2 4h2",
    star: "m12 3 2.8 5.7 6.2.9-4.5 4.4 1.1 6.2-5.6-3-5.6 3 1.1-6.2L3 9.6l6.2-.9Z",
    cloud: "M7 18h11a4 4 0 0 0 1-7.9A7 7 0 0 0 5.3 9 4.6 4.6 0 0 0 1 13.5 4.5 4.5 0 0 0 5.5 18m3.5-5 3-3 3 3m-3-3v8",
    user: "M12 3a4 4 0 1 0 0 8 4 4 0 0 0 0-8ZM4 21v-3a6 6 0 0 1 6-5h4a6 6 0 0 1 6 5v3",
    chat: "M4 4h16v13H9l-5 4ZM8 8h8m-8 5h5",
    sound: "m11 4-6 5H2v6h3l6 5Zm4 4a6 6 0 0 1 0 8m3-11a10 10 0 0 1 0 14",
    mic: "M9 5a3 3 0 0 1 6 0v7a3 3 0 0 1-6 0Zm-3 5v2a6 6 0 0 0 12 0v-2m-6 8v4m-3 0h6",
    display: "M3 3h18v13H3Zm9 13v5m-4 0h8",
    bell: "M6 9a6 6 0 0 1 12 0c0 6 3 6 3 8H3c0-2 3-2 3-8m3 11a3 3 0 0 0 6 0",
    wheel: "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20Zm0 0v6m10 4h-6m-4 10v-6M2 12h6m4-4a4 4 0 1 0 0 8 4 4 0 0 0 0-8",
    cursor: "m4 3 15 9-7 2-3 7Zm9 12 4 6",
    data: "M4 6c0-5 16-5 16 0s-16 5-16 0Zm0 0v12c0 5 16 5 16 0V6M4 12c0 5 16 5 16 0",
    check: "m5 12 4 4L19 6",
    arrow: "M4 12h16m-6-6 6 6-6 6",
    folder: "M3 19V5h6l2 3h10v3M3 19l3-8h15l-3 8Z",
    spark: "m12 2 2.5 7.5L22 12l-7.5 2.5L12 22l-2.5-7.5L2 12l7.5-2.5Z",
  };
</script>

<svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d={paths[name] || paths.home} /></svg>
