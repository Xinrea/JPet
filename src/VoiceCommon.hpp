#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

// Types and helpers shared by the voice conversation, its tools and settings.
namespace Voice {
constexpr size_t InputBytesPerSecond = 16000 * 2;  // 16 kHz mono s16le microphone audio.

std::string Trim(const std::string& value);
bool ValidWorkspace(const std::string& value);
bool ValidApiKey(const std::string& value);
// Saved microphone or speaker id: empty for the system default, otherwise a
// Windows endpoint id or Core Audio device UID without control characters.
bool ValidAudioDeviceId(const std::string& value);
std::string EncodeBase64(const std::string& bytes);
std::string DecodeBase64(const std::string& encoded);
// Readable message for a Bailian error code or HTTP status.
std::string FriendlyError(const std::string& code);
// System prompt for the chat model.
std::string Instructions();

struct ToolCall {
  std::string id, name, arguments;
};

struct ConversationEvent {
  enum class Type { Started, UserTranscript, AssistantTranscript, InputFailed, ToolStarted, ToolCompleted, Completed, Interrupted, Failed };
  Type type;
  uint64_t turn;
  std::string text;
  std::string id;
  nlohmann::json data = nlohmann::json::object();
};
}  // namespace Voice
