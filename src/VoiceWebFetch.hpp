#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace Voice {
// Local, non-browser fallback. Only public HTTP(S) destinations are permitted;
// each actual socket address is checked, including every manual redirect hop.
nlohmann::json FetchWebPage(const std::string& url, const std::function<bool()>& cancelled = {});
// Offline UTF-8 HTML/plain-text extraction, bounded to 2 MiB input, 8000 content
// bytes and <12000 serialized JSON bytes (default json::dump()). No script runs.
nlohmann::json ParseWebPage(const std::string& body, const std::string& contentType, const std::string& url);

namespace WebFetchDetail {
// Pure classification seams for offline security regression tests. URL checking
// does not resolve DNS; the socket callback remains the network security gate.
bool IsPublicAddress(const std::string& address);
bool IsAllowedUrl(const std::string& url);
}  // namespace WebFetchDetail
}  // namespace Voice
