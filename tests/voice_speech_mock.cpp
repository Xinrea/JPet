// Builds as four independent shared libraries: compatible, stock (no capability),
// wrong capability version, and incomplete (last required symbol missing).
// No model or ONNX Runtime needed.
#include "voice_speech_mock.hpp"
#include <sherpa-onnx/c-api/c-api.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifdef _WIN32
#define MOCK_EXPORT __declspec(dllexport)
#else
#define MOCK_EXPORT __attribute__((visibility("default")))
#endif

namespace {
VoiceSpeechMockState state;
std::string directory, hotwords;
int mode = Normal;
int recognizerStorage, streamStorage;
const auto* recognizer = reinterpret_cast<const SherpaOnnxOfflineRecognizer*>(&recognizerStorage);
const auto* stream = reinterpret_cast<const SherpaOnnxOfflineStream*>(&streamStorage);
SherpaOnnxOfflineRecognizerResult result{};
void Check(bool condition, const char* message) {
  if (!condition && !state.failure[0]) std::snprintf(state.failure, sizeof(state.failure), "%s", message);
}
bool Equal(const char* actual, const std::string& expected) { return actual && actual == expected; }
}  // namespace

extern "C" {
MOCK_EXPORT void JpetSpeechMockConfigure(const char* modelDirectory, const char* hotwordsFile, int requestedMode) {
  directory = modelDirectory;
  hotwords = hotwordsFile;
  mode = requestedMode;
}
MOCK_EXPORT const VoiceSpeechMockState* JpetSpeechMockInspect() { return &state; }

#ifndef JPET_MOCK_STOCK
MOCK_EXPORT int SherpaOnnxJpetSenseVoiceHotwordsVersion() {
  ++state.capability;
#ifdef JPET_MOCK_WRONG_VERSION
  return 2;
#else
  return 1;
#endif
}
#endif

MOCK_EXPORT const SherpaOnnxOfflineRecognizer* SherpaOnnxCreateOfflineRecognizer(
    const SherpaOnnxOfflineRecognizerConfig* config) {
  ++state.created;
  Check(state.capability > 0, "capability must be checked before recognizer creation");
  Check(Equal(config->decoding_method, "jpet_ctc_beam_search"), "wrong decoding method");
  Check(config->max_active_paths == 8, "wrong beam size");
  Check(config->hotwords_score == 12.0f, "wrong production hotword score");
  Check(Equal(config->hotwords_file, hotwords), "wrong hotword resource path");
  std::ifstream file(std::filesystem::u8path(config->hotwords_file ? config->hotwords_file : ""), std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(file)), {});
  Check(bytes == "轴 伊\n" || bytes == "轴 伊\r\n", "hotword resource must contain tokenized name and newline");
  Check(Equal(config->model_config.sense_voice.model, directory + "/model.int8.onnx"), "wrong model path");
  Check(Equal(config->model_config.tokens, directory + "/tokens.txt"), "wrong token path");
  Check(Equal(config->model_config.sense_voice.language, "auto"), "language must stay auto");
  Check(config->model_config.sense_voice.use_itn == 1, "ITN must stay enabled");
  Check(config->model_config.num_threads == 2, "wrong thread count");
  Check(Equal(config->model_config.provider, "cpu"), "wrong provider");
  Check(config->feat_config.sample_rate == 16000 && config->feat_config.feature_dim == 80, "wrong feature config");
  return mode == RejectRecognizer ? nullptr : recognizer;
}
MOCK_EXPORT void SherpaOnnxDestroyOfflineRecognizer(const SherpaOnnxOfflineRecognizer* value) {
  Check(value == recognizer, "destroyed wrong recognizer");
  ++state.destroyed;
}
MOCK_EXPORT const SherpaOnnxOfflineStream* SherpaOnnxCreateOfflineStream(const SherpaOnnxOfflineRecognizer* value) {
  Check(value == recognizer, "stream created with wrong recognizer");
  if (mode == RejectStream) return nullptr;
  ++state.streams;
  return stream;
}
MOCK_EXPORT void SherpaOnnxDestroyOfflineStream(const SherpaOnnxOfflineStream* value) {
  Check(value == stream, "destroyed wrong stream");
  ++state.streamsDestroyed;
}
MOCK_EXPORT void SherpaOnnxAcceptWaveformOffline(const SherpaOnnxOfflineStream* value,
    int32_t sampleRate, const float* samples, int32_t count) {
  Check(value == stream && sampleRate == 16000, "incorrect waveform stream or sample rate");
  Check(count == 5, "incorrect PCM sample count");
  const float expected[] = {-1.0f, -0.5f, 0.0f, 0.5f, 32767.0f / 32768.0f};
  if (count == 5)
    for (int i = 0; i < count; ++i) Check(samples[i] == expected[i], "PCM conversion changed");
  ++state.accepted;
}
MOCK_EXPORT void SherpaOnnxDecodeOfflineStream(const SherpaOnnxOfflineRecognizer* r,
    const SherpaOnnxOfflineStream* s) {
  Check(r == recognizer && s == stream, "decoded wrong objects");
  Check(state.accepted == state.decoded + 1, "decode must follow waveform acceptance");
  ++state.decoded;
}
MOCK_EXPORT const SherpaOnnxOfflineRecognizerResult* SherpaOnnxGetOfflineStreamResult(
    const SherpaOnnxOfflineStream* value) {
  Check(value == stream && state.decoded == state.streams, "result requested before decode");
  if (mode == NullResult) return nullptr;
  ++state.results;
  result.text = mode == NullText ? nullptr : "轴伊你好呀";
  return &result;
}
#ifndef JPET_MOCK_INCOMPLETE
MOCK_EXPORT void SherpaOnnxDestroyOfflineRecognizerResult(const SherpaOnnxOfflineRecognizerResult* value) {
  Check(value == &result, "destroyed wrong result");
  Check(state.streamsDestroyed + 1 == state.streams, "result must be destroyed before its stream");
  ++state.resultsDestroyed;
}
#endif
}  // extern "C"
