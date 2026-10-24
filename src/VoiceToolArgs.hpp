#pragma once

// Argument validation and text cleanup shared by the voice tool implementations.
// Validation failures throw std::invalid_argument; ExecuteTool reports them to the
// model as `invalid_arguments` so it can correct the call.
#include "VoiceCommon.hpp"
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace Voice::ToolArgs {
using nlohmann::json;

inline json Fail(const std::string& error) { return {{"ok", false}, {"error", error}}; }

// Function-calling schema builders. Every tool closes its arguments object.
inline json String(const std::string& description, size_t maxLength) {
  return {{"type", "string"}, {"description", description}, {"maxLength", maxLength}};
}
inline json Enum(const std::vector<const char*>& values) { return {{"type", "string"}, {"enum", values}}; }
inline json Integer(int64_t min, int64_t max) { return {{"type", "integer"}, {"minimum", min}, {"maximum", max}}; }
inline json Definition(const char* name, const char* description, json properties, json required = json::array()) {
  return {{"type", "function"}, {"function", {{"name", name}, {"description", description},
    {"parameters", {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}}}}}};
}

inline void Fields(const json& args, const std::vector<const char*>& allowed) {
  if (!args.is_object()) throw std::invalid_argument("工具参数必须是对象");
  for (const auto& item : args.items())
    if (std::none_of(allowed.begin(), allowed.end(), [&](const char* key) { return item.key() == key; }))
      throw std::invalid_argument("工具包含不支持的字段；允许的字段为 " + json(allowed).dump());
}
inline void Fields(const json& args, std::initializer_list<const char*> allowed) {
  Fields(args, std::vector<const char*>(allowed));
}

inline std::string Text(const json& args, const char* key, size_t max, const std::string& fallback = "") {
  if (!args.contains(key)) return fallback;
  if (!args[key].is_string()) throw std::invalid_argument("工具文本参数类型错误");
  auto value = Trim(args[key].get<std::string>());
  if (value.empty() || value.size() > max || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32; }))
    throw std::invalid_argument("工具文本参数为空、过长或包含控制字符");
  return value;
}

inline std::string Choice(const json& args, const char* key, const std::vector<const char*>& values, const char* fallback = "") {
  auto value = Text(args, key, 64, fallback);
  if (std::none_of(values.begin(), values.end(), [&](const char* option) { return value == option; }))
    throw std::invalid_argument(std::string(key) + " 必须为 " + json(values).dump() + " 中的一项");
  return value;
}

inline int64_t Number(const json& args, const char* key, int64_t min, int64_t max, int64_t fallback = -1) {
  if (!args.contains(key)) {
    if (fallback >= min && fallback <= max) return fallback;
    throw std::invalid_argument(std::string("缺少数字参数 ") + key);
  }
  if (!args[key].is_number_integer()) throw std::invalid_argument(std::string(key) + " 必须是整数");
  if (args[key].is_number_unsigned() && args[key].get<uint64_t>() > static_cast<uint64_t>(INT64_MAX))
    throw std::invalid_argument("工具数字参数超出范围");
  auto value = args[key].get<int64_t>();
  if (value < min || value > max) throw std::invalid_argument(std::string(key) + " 必须在 " + std::to_string(min) + " 到 " + std::to_string(max) + " 之间");
  return value;
}

// Removes HTML tags and control characters, decodes common entities and
// truncates on a UTF-8 boundary.
inline std::string PlainText(const std::string& source, size_t max = 1000) {
  std::string value;
  bool tag = false;
  for (unsigned char c : source) {
    if (c == '<') tag = true;
    else if (c == '>') tag = false;
    else if (!tag && c >= 32) value += c;
  }
  for (const auto& pair : {std::pair{"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}}) {
    size_t at = 0;
    while ((at = value.find(pair.first, at)) != std::string::npos) {
      value.replace(at, std::char_traits<char>::length(pair.first), pair.second);
      at += std::char_traits<char>::length(pair.second);
    }
  }
  if (value.size() > max) {
    auto end = max;
    while (end && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) --end;
    value.resize(end);
  }
  return value;
}
inline std::string Plain(const json& item, const char* key, size_t max = 1000) {
  if (!item.is_object() || !item.contains(key) || !item[key].is_string()) return {};
  return PlainText(item[key].get_ref<const std::string&>(), max);
}

// A protocol-relative or absolute HTTP(S) link from external data, or empty.
inline std::string Link(std::string url) {
  if (url.compare(0, 2, "//") == 0) url = "https:" + url;
  if (url.size() > 2048 || (url.compare(0, 8, "https://") != 0 && url.compare(0, 7, "http://") != 0) ||
      std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32; })) return {};
  return url;
}
}  // namespace Voice::ToolArgs
