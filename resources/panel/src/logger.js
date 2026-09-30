const MAX_MESSAGE_LENGTH = 3500;
const MAX_PENDING_LOGS = 8;

let nextLogId = 1;
let pendingLogs = 0;

function stringifyDetails(details) {
  if (details === undefined) {
    return "";
  }

  try {
    return ` ${JSON.stringify(details, (_key, value) => {
      if (value instanceof Error) {
        return {
          name: value.name,
          message: value.message,
          stack: value.stack,
        };
      }
      if (typeof value === "bigint") {
        return `${value}n`;
      }
      return value;
    })}`;
  } catch (error) {
    return ` [details unavailable: ${error?.message || "serialization failed"}]`;
  }
}

function logToConsole(level, message, details) {
  try {
    const method = console[level] || console.log;
    method.call(console, message, details);
  } catch (_error) {
    // A broken or replaced console must not interfere with the panel.
  }
}

/**
 * Send a bounded frontend diagnostic to the native panel server.
 *
 * The server is local to the embedded page, so this deliberately uses the
 * same-origin endpoint instead of depending on DevTools being attached.
 */
export function reportFrontend(level, message, details) {
  const normalizedLevel = ["debug", "info", "warn", "error"].includes(level)
    ? level
    : "error";
  const text = String(message || "").trim();
  if (!text) {
    return;
  }

  const formattedMessage = `[${nextLogId++}] ${text}${stringifyDetails(details)}`
    .slice(0, MAX_MESSAGE_LENGTH);
  logToConsole(normalizedLevel, formattedMessage, details);

  if (typeof fetch !== "function") {
    return;
  }
  // Do not let a stalled local server create an unbounded queue while the
  // browser is starting or recovering its WebView process.
  if (pendingLogs >= MAX_PENDING_LOGS && normalizedLevel === "debug") {
    return;
  }

  pendingLogs += 1;
  fetch("/api/log", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      level: normalizedLevel,
      message: formattedMessage,
    }),
  }).then(
    () => {
      pendingLogs = Math.max(0, pendingLogs - 1);
    },
    () => {
      pendingLogs = Math.max(0, pendingLogs - 1);
    },
  );
}

export function reportFrontendError(message, details) {
  reportFrontend("error", message, details);
}
