import { writable } from 'svelte/store';

function createSSE(url) {
  const { subscribe, update } = writable(null);

  const eventSource = new EventSource(url);

  eventSource.onmessage = event => {
    update(() => ({data: event.data, ts: Date.now()}));
  };

  eventSource.onerror = error => {
    console.error("SSE error:", error);
    eventSource.close();
  };

  return {
    subscribe,
    close: () => {
      eventSource.close();
      update(() => null);
    }
  };
}

export const sse = createSSE('/api/sse');
