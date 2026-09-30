import { writable } from 'svelte/store';
import { reportFrontend, reportFrontendError } from './logger.js';

function createSSE(url) {
  const { subscribe, update } = writable(null);

  const eventSource = new EventSource(url);

  eventSource.onopen = () => {
    reportFrontend("info", "[SSE] connection opened", {
      url,
      readyState: eventSource.readyState,
    });
  };

  eventSource.onmessage = event => {
    update(() => ({data: event.data, ts: Date.now()}));
  };

  eventSource.onerror = error => {
    reportFrontendError("[SSE] connection error", {
      url,
      readyState: eventSource.readyState,
      error,
    });
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
