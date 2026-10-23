#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Voice {
// Splits a streaming reply into speakable sentences for per-sentence synthesis,
// like Pipecat's SimpleTextAggregator. The first segment may end at a comma so
// the first audio starts sooner; later segments end at a comma only when long.
class SentenceSplitter {
 public:
  std::vector<std::string> Push(const std::string& delta);
  // Returns the remaining text at the end of the reply.
  std::string Flush();
  void Reset();

 private:
  std::string buffer_;
  bool first_ = true;
};

// Removes Markdown markup and links, which a voice should not read aloud.
std::string Speakable(const std::string& text);
// UTF-8 safe prefix holding `fraction` of the text's code points.
std::string PrefixFraction(const std::string& text, double fraction);
size_t CodePoints(const std::string& text);
// True when recognised speech is mostly the reply being played, i.e. the
// microphone heard the speaker rather than the user.
bool LooksLikeEcho(const std::string& heard, const std::string& spoken);
}  // namespace Voice
