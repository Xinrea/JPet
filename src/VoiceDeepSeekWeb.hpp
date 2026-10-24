#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace Voice {
// Offline parsers: at most 2 MiB of serialized input, at most five search
// results, and less than 12,000 serialized output bytes (default dump()).
// Fetch retains Markdown, capped at 8,000 UTF-8 bytes. Errors never echo remote
// bodies. Query argument validation throws std::invalid_argument, like ToolArgs.
nlohmann::json ParseDeepSeekSearch(const nlohmann::json& body, const std::string& query, size_t limit);
nlohmann::json ParseDeepSeekFetch(const nlohmann::json& body, const std::string& url);
nlohmann::json QueryDeepSeekSearch(const nlohmann::json& request, const std::string& key,
                                 const std::function<bool()>& cancelled = {});
nlohmann::json QueryDeepSeekFetch(const nlohmann::json& request, const std::string& key,
                                const std::function<bool()>& cancelled = {});

// Per-call transport seam, with no global override or CPR dependency in callers.
// Transports POST synchronously, stop when either callback returns false, and
// must not retain callbacks. Only headers contain the credential. Production
// always uses the fixed DeepSeek endpoints, verified TLS and no redirects.
struct DeepSeekWebRequest {
  std::string url;
  std::map<std::string, std::string> headers;
  std::string body;
  int timeoutMs = 15000;
  int connectTimeoutMs = 3000;
  bool followRedirects = false;
};
struct DeepSeekWebReply {
  int status = 0;
  bool networkOk = true;
};
using DeepSeekWebTransport = std::function<DeepSeekWebReply(
  const DeepSeekWebRequest&, const std::function<bool(const std::string&)>& write,
  const std::function<bool()>& progress)>;
nlohmann::json QueryDeepSeekSearch(const nlohmann::json& request, const std::string& key,
                                 const std::function<bool()>& cancelled, const DeepSeekWebTransport& transport);
nlohmann::json QueryDeepSeekFetch(const nlohmann::json& request, const std::string& key,
                                const std::function<bool()>& cancelled, const DeepSeekWebTransport& transport);
}  // namespace Voice
