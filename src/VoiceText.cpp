#include "VoiceText.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>

namespace Voice {
namespace {
size_t SequenceLength(unsigned char lead) {
  return lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
}

std::vector<std::string> Split(const std::string& text) {
  std::vector<std::string> characters;
  for (size_t i = 0; i < text.size();) {
    const size_t length = std::min(SequenceLength(static_cast<unsigned char>(text[i])), text.size() - i);
    characters.push_back(text.substr(i, length));
    i += length;
  }
  return characters;
}

bool Is(const std::string& character, std::initializer_list<const char*> set) {
  return std::any_of(set.begin(), set.end(), [&](const char* item) { return character == item; });
}

bool SentenceEnd(const std::string& c) { return Is(c, {"。", "！", "？", "!", "?", "；", ";", "…", "\n"}); }
bool Pause(const std::string& c) { return Is(c, {"，", ",", "、", "：", ":"}); }
bool Punctuation(const std::string& c) {
  return SentenceEnd(c) || Pause(c) || Is(c, {"．", ".", "“", "”", "\"", "‘", "’", "'", "（", "）", "(", ")",
      "《", "》", "—", "-", "~", "～", " ", "\t", "\r"});
}
}  // namespace

size_t CodePoints(const std::string& text) { return Split(text).size(); }

std::vector<std::string> SentenceSplitter::Push(const std::string& delta) {
  buffer_ += delta;
  std::vector<std::string> sentences;
  const auto characters = Split(buffer_);
  size_t start = 0, bytes = 0, length = 0;
  bool link = false;  // Punctuation inside a URL ("?", ".") does not end a sentence.
  for (size_t i = 0; i < characters.size(); ++i) {
    const auto& c = characters[i];
    if (!link && (buffer_.compare(bytes, 7, "http://") == 0 || buffer_.compare(bytes, 8, "https://") == 0)) link = true;
    else if (link && (c.size() > 1 || std::isspace(static_cast<unsigned char>(c[0])))) link = false;
    bytes += c.size();
    ++length;
    if (link) continue;
    // "1.5" and "e.g." keep their periods; a period followed by a space ends a sentence.
    const bool period = (c == "." || c == "．") && i + 1 < characters.size() && characters[i + 1] == " ";
    const bool cut = SentenceEnd(c) || period || (Pause(c) && length >= (first_ ? 6u : 30u));
    if (!cut) continue;
    auto sentence = Speakable(buffer_.substr(start, bytes - start));
    start = bytes;
    length = 0;
    if (sentence.empty()) continue;
    sentences.push_back(std::move(sentence));
    first_ = false;
  }
  buffer_.erase(0, start);
  return sentences;
}

std::string SentenceSplitter::Flush() {
  auto rest = Speakable(buffer_);
  buffer_.clear();
  return rest;
}

void SentenceSplitter::Reset() {
  buffer_.clear();
  first_ = true;
}

std::string Speakable(const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size();) {
    if (text.compare(i, 7, "http://") == 0 || text.compare(i, 8, "https://") == 0) {
      while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) && static_cast<unsigned char>(text[i]) < 0x80) ++i;
      out += "链接";
      continue;
    }
    const char c = text[i];
    if (c == '*' || c == '#' || c == '`' || c == '_' || c == '>' || c == '|') { ++i; continue; }
    out += c;
    ++i;
  }
  const auto first = out.find_first_not_of(" \t\r\n-");
  if (first == std::string::npos) return {};
  out.erase(0, first);
  out.erase(out.find_last_not_of(" \t\r\n") + 1);
  // A fragment of punctuation only has nothing to say.
  const auto characters = Split(out);
  if (std::all_of(characters.begin(), characters.end(), Punctuation)) return {};
  return out;
}

std::string PrefixFraction(const std::string& text, double fraction) {
  const auto characters = Split(text);
  const size_t keep = size_t(std::floor(std::clamp(fraction, 0.0, 1.0) * characters.size()));
  std::string out;
  for (size_t i = 0; i < keep; ++i) out += characters[i];
  return out;
}

bool LooksLikeEcho(const std::string& heard, const std::string& spoken) {
  const auto normalize = [](const std::string& text) {
    std::vector<std::string> out;
    for (auto& c : Split(text)) {
      if (Punctuation(c)) continue;
      if (c.size() == 1) c[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(c[0])));
      out.push_back(c);
    }
    return out;
  };
  const auto a = normalize(heard), b = normalize(spoken);
  if (a.empty() || b.empty()) return false;
  // Longest common subsequence: recognition errors drop or change a few characters.
  std::vector<size_t> previous(b.size() + 1, 0), current(b.size() + 1, 0);
  for (const auto& c : a) {
    for (size_t j = 0; j < b.size(); ++j)
      current[j + 1] = c == b[j] ? previous[j] + 1 : std::max(previous[j + 1], current[j]);
    std::swap(previous, current);
  }
  return previous[b.size()] >= 0.6 * a.size();
}
}  // namespace Voice
