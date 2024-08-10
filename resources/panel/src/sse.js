import { writable } from 'svelte/store';

function createSSE(url) {
  const { subscribe, set } = writable(null);

  const eventSource = new EventSource(url);

  eventSource.onmessage = event => {
    set(event.data);
  };

  eventSource.onerror = error => {
    console.error("SSE error:", error);
    eventSource.close();
  };

  return {
    subscribe,
    close: () => {
      eventSource.close();
      set(null);
    }
  };
}

export const sse = createSSE('/api/sse');
