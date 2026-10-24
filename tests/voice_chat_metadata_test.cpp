#include "VoiceLlm.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
}
}

int main() {
  using nlohmann::json;
  try {
    for (const auto& invalid : json::array({nullptr, false, true, "32768", 32768.0, 4095, -1,
                                           10000001, json::object(), json::array(),
                                           std::numeric_limits<uint64_t>::max()}))
      Check(!Voice::ParseChatContextWindow(invalid), "invalid capacity is rejected before conversion");
    Check(Voice::ParseChatContextWindow(4096) == 4096 &&
          Voice::ParseChatContextWindow(10000000) == 10000000, "inclusive capacity boundaries");
    Check(Voice::kDefaultChatContextWindow == 32768, "conservative fallback for missing metadata");
    for (const auto* field : {"context_length", "context_window", "max_model_len"}) {
      const auto models = Voice::ParseChatModels("thirdparty", {{"data", {{{"id", "local"}, {field, 65536}}}}});
      Check(models.size() == 1 && models[0].contextWindow == 65536, "compatible metadata aliases");
    }
    const auto models = Voice::ParseChatModels("thirdparty", json::parse(R"({"data":[
      {"id":"a","context_length":8192,"context_window":16384,"max_model_len":32768},
      {"id":"b","context_length":"bad","context_window":16384},
      {"id":"c","context_length":2,"max_tokens":65536},
      {"id":"d"}]})"));
    Check(models.size() == 4 && models[0].contextWindow == 8192 && models[1].contextWindow == 16384 &&
          !models[2].contextWindow && !models[3].contextWindow,
          "metadata precedence, invalid alias fallback and absent capacities");
    const auto deepseek = Voice::ParseChatModels("deepseek", json::parse(R"({"data":[
      {"id":"deepseek-chat","context_length":65536},{"id":"deepseek-reasoner"},{"id":"unknown"}]})"));
    Check(deepseek.size() == 3 && deepseek[0].contextWindow == 65536 &&
          !deepseek[1].contextWindow && !deepseek[2].contextWindow,
          "provider model names never substitute for capacity metadata");
    const auto openrouter = Voice::ParseChatModels("openrouter", json::parse(R"({"data":[
      {"id":"model","context_length":200000,"supported_parameters":["tools"]}]})"));
    Check(openrouter.size() == 1 && openrouter[0].contextWindow == 200000,
          "OpenRouter capacity survives model filtering");
    std::cout << "PASS chat context metadata and validation\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
