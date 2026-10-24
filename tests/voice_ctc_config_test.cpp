// Opt-in patched sherpa-onnx C-API validation test. No audio or production JPet.
// Link the patched shared libsherpa-onnx-c-api, passing its source root with -I.
// Usage: voice_ctc_config_test MODEL_DIRECTORY [WORK_DIRECTORY]
// MODEL_DIRECTORY must contain model.int8.onnx and tokens.txt. Temporary test
// files are written only beneath WORK_DIRECTORY (default build/sensevoice-hotwords).
#include "sherpa-onnx/c-api/c-api.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static void Check(bool ok, const std::string &what) {
  if (!ok) {
    std::cerr << "FAIL: " << what << '\n';
    std::exit(1);
  }
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: voice_ctc_config_test MODEL_DIRECTORY [WORK_DIRECTORY]\n";
    return 2;
  }
  const auto model = (fs::path(argv[1]) / "model.int8.onnx").string();
  const auto tokens = (fs::path(argv[1]) / "tokens.txt").string();
  Check(fs::is_regular_file(model) && fs::is_regular_file(tokens), "model files exist");
  fs::path work = argc == 3 ? fs::path(argv[2]) : fs::path("build/sensevoice-hotwords");
  work /= "config-test-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  fs::create_directories(work);
  const auto hotwords = (work / "hotwords.txt").string();

  SherpaOnnxOfflineRecognizerConfig config{};
  config.decoding_method = "jpet_ctc_beam_search";
  config.max_active_paths = 8;
  config.hotwords_score = 1.5f;
  config.model_config.sense_voice.model = model.c_str();
  config.model_config.sense_voice.language = "zh";
  config.model_config.tokens = tokens.c_str();
  config.model_config.provider = "cpu";
  config.model_config.num_threads = 1;
  int cases = 0;
  auto expect = [&](bool valid, const char *label) {
    const auto *recognizer = SherpaOnnxCreateOfflineRecognizer(&config);
    bool accepted = recognizer != nullptr;
    if (recognizer) SherpaOnnxDestroyOfflineRecognizer(recognizer);
    Check(accepted == valid, label);
    ++cases;
  };
  auto write_hotwords = [&](const char *line) {
    std::ofstream output(hotwords);
    output << line << '\n';
    output.close();
    Check(!output.fail(), "write temporary hotwords");
  };

  for (int beam : {0, -1, 129}) {
    config.max_active_paths = beam;
    expect(false, "invalid beam rejected");
  }
  config.max_active_paths = 8;
  for (float score : {-1.0f, 101.0f, std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN()}) {
    config.hotwords_score = score;
    expect(false, "invalid score rejected");
  }
  config.hotwords_score = 1.5f;
  config.model_config.sense_voice.model = nullptr;
  expect(false, "experimental decoding rejected without SenseVoice");
  config.model_config.sense_voice.model = model.c_str();
  config.model_config.funasr_nano.encoder_adaptor = model.c_str();
  expect(false, "experimental decoding rejected for FunASR-Nano");
  config.model_config.funasr_nano.encoder_adaptor = nullptr;
  config.model_config.provider = "rknn";
  expect(false, "unsupported provider rejected");
  config.model_config.provider = "cpu";

  config.hotwords_file = hotwords.c_str();
  for (const char *token : {"jpet_nonexistent_token_192837", "<unk>",
                            "<|zh|>", "<s>"}) {
    write_hotwords(token);
    expect(false, "unknown/reserved hotword rejected before model load");
  }
  // Reject the actual blank symbol even if a token table changes its spelling.
  std::ifstream vocabulary(tokens);
  std::string line, blank;
  while (std::getline(vocabulary, line)) {
    std::istringstream fields(line);
    std::string token; int id = -1;
    if ((fields >> token >> id) && id == 0) { blank = token; break; }
  }
  Check(!blank.empty(), "blank symbol exists");
  write_hotwords(blank.c_str());
  expect(false, "blank hotword rejected");

  write_hotwords("轴 伊");
  config.decoding_method = "greedy_search";
  expect(false, "greedy rejects hotwords instead of silently ignoring them");
  config.hotwords_file = nullptr;
  expect(true, "ordinary greedy accepted");
  config.decoding_method = "jpet_ctc_beam_search";
  expect(true, "beam without hotwords accepted");
  config.hotwords_file = hotwords.c_str();
  expect(true, "beam with whitespace-tokenized UTF-8 hotwords accepted");
  config.hotwords_score = 0;
  expect(true, "beam accepts score zero");

  // No C-API config getter exists: score=0 preservation is inspected in the
  // mapping patch and tested numerically in voice_ctc_decoder_test instead.
  fs::remove(hotwords);
  fs::remove(work);
  std::cout << "voice_ctc_config_test: PASS (" << cases << " config cases)\n";
}
