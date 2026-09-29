import './app.css'
import App from './App.svelte'

function reportFrontendError(level, value) {
  const message = String(value || '').slice(0, 3500)
  if (!message) return
  fetch('/api/log', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ level, message }),
  }).catch(() => {})
}

window.addEventListener('error', (event) => {
  reportFrontendError(
    'error',
    event.error?.stack || `${event.message} (${event.filename}:${event.lineno}:${event.colno})`,
  )
})

window.addEventListener('unhandledrejection', (event) => {
  reportFrontendError('error', event.reason?.stack || event.reason)
})

const app = new App({
  target: document.getElementById('app'),
})

export default app
