#include "VoiceSynthesis.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Check(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
}

std::vector<float> Tone(double seconds, float amplitude) {
  std::vector<float> samples(static_cast<size_t>(seconds * 24000));
  for (size_t i = 0; i < samples.size(); ++i) samples[i] = amplitude * float(std::sin(2 * 3.14159265 * 220 * i / 24000));
  return samples;
}

size_t Samples(const std::string& pcm) { return pcm.size() / 2; }

void Trimmer() {
  const auto silence = Tone(0.8, 0.005f), speech = Tone(0.5, 0.3f), pause = Tone(0.3, 0.0f);
  Voice::SpeechTrimmer trimmer;
  Check(trimmer.Push(silence.data(), silence.size()).empty(), "leading silence is held back");
  const auto first = trimmer.Push(speech.data(), speech.size());
  Check(Samples(first) >= 12000 + 2400 - 240 && Samples(first) <= 12000 + 2400,
        "speech starts with about 100 ms of the silence before it");
  Check(trimmer.Push(pause.data(), pause.size()).empty(), "a pause waits to see whether speech follows");
  const auto second = trimmer.Push(speech.data(), speech.size());
  Check(Samples(second) >= 7200 + 12000 - 240, "a pause inside the sentence is kept");
  Check(trimmer.Push(silence.data(), silence.size()).empty(), "trailing silence is held back");
  Check(Samples(trimmer.Finish()) == 4800, "200 ms of trailing silence is kept");

  // Chunks of any size, as the model streams them.
  Voice::SpeechTrimmer chunked;
  size_t total = 0;
  for (const auto* part : {&silence, &speech, &silence})
    for (size_t at = 0; at < part->size(); at += 1000)
      total += Samples(chunked.Push(part->data() + at, std::min<size_t>(1000, part->size() - at)));
  total += Samples(chunked.Finish());
  Check(total >= 12000 + 2400 + 4800 - 240 && total <= 12000 + 2400 + 4800, "chunked output trims the same way");
  Check(Samples(chunked.Finish()) == 0, "finish resets the trimmer");

  Voice::SpeechTrimmer quiet;
  quiet.Push(silence.data(), silence.size());
  Check(quiet.Finish().empty(), "a sentence that never becomes audible plays nothing");
}

void Frames() {
  Check(Voice::MaxSynthesisFrames("好呀。") == 40, "short sentences still get a few seconds");
  Check(Voice::MaxSynthesisFrames("今天天气不错，要不要出去走走？") == 100, "about 0.4 s per character plus two seconds");
  Check(Voice::MaxSynthesisFrames(std::string(3000, 'a')) == 1024, "the budget is capped");
}

void Reference(const std::string& bundled) {
  Voice::VoiceReference voice{{0.5f, -1.25f, 2.0f}, {1, 2, 3, 4, 5, 6}, 2, 3, "你觉得黑色好还是白色好"};
  const auto path = (std::filesystem::temp_directory_path() / "jpet-voice-test.json").string();
  std::ofstream(path, std::ios::binary) << Voice::VoiceReferenceJson(voice, "test");
  Voice::VoiceReference loaded;
  std::string error;
  Check(Voice::LoadVoiceReference(path, loaded, error) && error.empty() && loaded.speaker == voice.speaker &&
        loaded.codes == voice.codes && loaded.codebooks == 2 && loaded.frames == 3 && loaded.text == voice.text,
        "a voice reference round-trips through JSON");
  std::ofstream(path, std::ios::binary) << R"({"format":"jpet-voice/1","text":"x","speaker_embedding":"AAAAAA==","codebooks":2,"frames":3,"codes":"AAAA"})";
  Check(!Voice::LoadVoiceReference(path, loaded, error) && !error.empty(), "codes must match codebooks x frames");
  Check(!Voice::LoadVoiceReference(path + ".missing", loaded, error), "a missing file is reported");
  std::filesystem::remove(path);

  Check(Voice::LoadVoiceReference(bundled, loaded, error), "the bundled voice loads");
  Check(loaded.speaker.size() == 1024 && loaded.codebooks == 16 && loaded.frames > 50 && !loaded.text.empty(),
        "the bundled voice matches Qwen3-TTS 0.6B (1024-wide speaker embedding, 16 codebooks)");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("usage: voice_synthesis_test <resources/models/joi-voice.json>");
    Trimmer();
    Frames();
    Reference(argv[1]);
    std::cout << "PASS voice synthesis helpers\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
