#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>

namespace Voice {
// Offline parser only: never requests a URL or follows a result link. Accepts
// at most 2 MiB of UTF-8 HTML and a 1024-byte UTF-8 query. Returns at most five
// results and less than 12,000 serialized JSON bytes (default json::dump()).
// Unknown, truncated and challenge pages are errors, not empty search results.
nlohmann::json ParseDuckDuckGoSearch(const std::string& html, const std::string& query, size_t limit);
}  // namespace Voice
