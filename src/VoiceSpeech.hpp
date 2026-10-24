#pragma once

#include <memory>
#include <string>

namespace Voice {
// Local speech recognition with SenseVoice-Small through sherpa-onnx's C API.
// Like ONNX Runtime, the library is loaded at run time so a missing or broken
// install only disables local recognition.
class LocalRecognizer {
 public:
  // `directory` holds model.int8.onnx and tokens.txt. `hotwordsFile` is the
  // bundled UTF-8 token file. Uses JPet's patched CTC decoder, beam 8, score 12.
  static std::unique_ptr<LocalRecognizer> Load(const std::string& directory, const std::string& hotwordsFile,
                                               std::string& error);
  ~LocalRecognizer();
  // Transcribes one utterance of 16 kHz mono s16le PCM. Not reentrant: call
  // from one thread at a time.
  std::string Transcribe(const std::string& pcm);

 private:
  struct Impl;
  explicit LocalRecognizer(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// sherpa-onnx library location: JPET_SHERPA_ONNX_PATH, else next to ONNX Runtime.
std::string SherpaOnnxLibrary();
}  // namespace Voice
