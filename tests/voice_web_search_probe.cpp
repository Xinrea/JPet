// Opt-in public web tool probe. No JPet profile, credentials or browser needed.
// Deliberately tests only the free fallback adapters, never a paid API.
#include "VoiceTools.hpp"
#include "VoiceToolsNetwork.hpp"
#include "VoiceWebFetch.hpp"
#include <iostream>

int main(int argc, char** argv) {
  const bool fetch = argc == 3 && std::string(argv[1]) == "--fetch";
  if (argc != 2 && !fetch) {
    std::cerr << "Usage: voice_web_search_probe <query> | --fetch <url>\n";
    return 2;
  }
  Voice::ToolDependencies dependencies;
  dependencies.webSearch = Voice::QueryWebSearch;
  dependencies.webFetch = [](const nlohmann::json& request, const std::function<bool()>& cancelled) {
    return Voice::FetchWebPage(request.at("url").get<std::string>(), cancelled);
  };
  const auto args = fetch ? nlohmann::json{{"url", argv[2]}} : nlohmann::json{{"query", argv[1]}, {"limit", 3}};
  const auto result = Voice::ExecuteTool({"probe", fetch ? "web_fetch" : "web_search", args.dump()}, dependencies);
  std::cout << result.dump(2) << '\n';
  return result.value("ok", false) ? 0 : 1;
}
