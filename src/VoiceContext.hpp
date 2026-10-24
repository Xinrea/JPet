#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace Voice {
// A conservative local estimate, not a provider tokenizer: ASCII averages three
// bytes/token; non-ASCII code points allow two tokens each (including Chinese).
// Count serialized roles, tool schemas, arguments and results as well as prose.
inline size_t EstimateContextTokens(const nlohmann::json& value) {
  const auto text = value.dump();
  size_t ascii = 0, tokens = 0;
  for (unsigned char c : text) {
    if (c < 0x80) ++ascii;
    else if ((c & 0xc0) != 0x80) tokens += 2;
  }
  return tokens + (ascii + 2) / 3;
}
}  // namespace Voice
