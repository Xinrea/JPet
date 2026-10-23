#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Voice {
// Joi's voice for Qwen3-TTS in-context cloning: the speaker embedding and the
// codec codes of a reference clip with its transcript. Extracted once with
// voice_synthesis_probe and bundled with JPet (resources/models/joi-voice.json).
struct VoiceReference {
  std::vector<float> speaker;   // Speaker embedding, one value per talker hidden unit.
  std::vector<int32_t> codes;   // [codebooks, frames], row-major.
  int codebooks = 0, frames = 0;
  std::string text;
};
bool LoadVoiceReference(const std::string& path, VoiceReference& voice, std::string& error);
std::string VoiceReferenceJson(const VoiceReference& voice, const std::string& source);

// Speaks reply sentences one at a time; audio arrives as 24 kHz mono s16le PCM.
class Synthesizer {
 public:
  struct Output {
    uint64_t id;
    std::string pcm;
    bool done = false;   // The sentence's last output.
    std::string error;   // Set on the last output when synthesis failed.
  };
  virtual ~Synthesizer() = default;
  virtual void Speak(uint64_t id, const std::string& text) = 0;
  // Stops the current sentence and drops queued ones; none of their output arrives.
  virtual void Cancel() = 0;
  virtual std::vector<Output> Poll() = 0;
};

struct SynthesisModel {
  std::string talker, codec;  // GGUF paths.
};

// Qwen3-TTS through qwentts.cpp, loaded at run time like sherpa-onnx. The model
// loads and speaks a warm-up sentence on the worker thread as soon as this is
// created, so the first reply does not wait for either.
std::unique_ptr<Synthesizer> MakeLocalSynthesizer(SynthesisModel model, VoiceReference voice,
                                                   std::function<void(const std::string&)> log);
// Checks that the library loads on this system (macOS 12 or later on a Mac).
bool LocalSynthesisAvailable(std::string& error);
// Extracts a voice reference from 24 kHz mono audio and its transcript.
bool ExtractVoiceReference(const SynthesisModel& model, const std::vector<float>& audio, const std::string& text,
                           VoiceReference& voice, std::string& error);
// qwentts library location: JPET_QWENTTS_PATH, else next to ONNX Runtime.
std::string QwenTtsLibrary();

// Generous frame budget for a sentence (12.5 frames per second of speech), so
// a sampling run-away stops after a few seconds instead of minutes.
int MaxSynthesisFrames(const std::string& text);

// Drops the silence the model leaves before and after a sentence while keeping
// pauses inside it. Input is float PCM, output s16le.
class SpeechTrimmer {
 public:
  std::string Push(const float* samples, size_t count);
  std::string Finish();

 private:
  std::vector<float> pending_;  // Quiet samples not yet known to precede speech.
  bool voiced_ = false;
};
}  // namespace Voice
