import './app.css'
import App from './App.svelte'
import { reportFrontend, reportFrontendError } from './logger.js'

function runtimeNow() {
  return typeof performance !== "undefined" && typeof performance.now === "function"
    ? performance.now()
    : Date.now();
}

function runtimeSnapshot() {
  const cssSupports = typeof CSS !== "undefined" && typeof CSS.supports === "function";
  return {
    href: `${window.location.origin}${window.location.pathname}`,
    userAgent: navigator.userAgent,
    platform: navigator.platform,
    language: navigator.language,
    visibilityState: document.visibilityState,
    hidden: document.hidden,
    hasFetch: typeof fetch === "function",
    hasRequestAnimationFrame: typeof requestAnimationFrame === "function",
    hasSVG: typeof SVGElement !== "undefined",
    supportsStrokeDashoffset: cssSupports && CSS.supports("stroke-dashoffset", "1px"),
    devicePixelRatio: window.devicePixelRatio,
    innerSize: `${window.innerWidth}x${window.innerHeight}`,
    performanceNow:
      typeof performance !== "undefined" && typeof performance.now === "function",
  };
}

if (typeof window !== "undefined") {
  window.addEventListener('error', (event) => {
    reportFrontendError(
      "window error",
      {
        message: event.message,
        filename: event.filename,
        line: event.lineno,
        column: event.colno,
        stack: event.error?.stack,
      },
    )
  })

  window.addEventListener('unhandledrejection', (event) => {
    reportFrontendError("unhandled rejection", event.reason)
  })

  document.addEventListener("visibilitychange", () => {
    reportFrontend(
      "info",
      "document visibility changed",
      {
        visibilityState: document.visibilityState,
        hidden: document.hidden,
        at: runtimeNow(),
      },
    )
  })

  window.addEventListener("pageshow", (event) => {
    reportFrontend("info", "page shown", {
      persisted: event.persisted,
      visibilityState: document.visibilityState,
    })
  })

  window.addEventListener("pagehide", (event) => {
    reportFrontend("info", "page hidden", {
      persisted: event.persisted,
      visibilityState: document.visibilityState,
    })
  })

  reportFrontend("info", "frontend runtime initialized", runtimeSnapshot())
}

const app = new App({
  target: document.getElementById('app'),
})

export default app
