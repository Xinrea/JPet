#include "VoiceModels.hpp"

#include <httplib.h>
#include <openssl/evp.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void Check(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
}

std::string Sha256(const std::string& data) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned length = 0;
  EVP_Digest(data.data(), data.size(), digest, &length, EVP_sha256(), nullptr);
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (unsigned i = 0; i < length; ++i) { out += digits[digest[i] >> 4]; out += digits[digest[i] & 15]; }
  return out;
}

std::string Wait(Voice::SpeechModelStore& store) {
  for (int i = 0; i < 500; ++i) {
    const auto state = store.Status()["state"].get<std::string>();
    if (state != "downloading") return state;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return "timeout";
}
}  // namespace

int main() {
  try {
    const std::string model(300000, 'm'), tokens = "a 1\nb 2\n";
    const auto modelHash = Sha256(model), tokensHash = Sha256(tokens);
    httplib::Server server;
    bool corrupt = false, slow = false;
    server.Get("/broken/(.*)", [](const httplib::Request&, httplib::Response& res) { res.status = 404; });
    server.Get("/good/(.*)", [&](const httplib::Request& req, httplib::Response& res) {
      if (slow) std::this_thread::sleep_for(std::chrono::milliseconds(300));
      const auto name = req.matches[1].str();
      auto body = name == "model.int8.onnx" ? model : tokens;
      if (corrupt) body[0] = 'x';
      res.set_content(body, "application/octet-stream");
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    std::thread listener([&] { server.listen_after_bind(); });
    const std::string base = "http://127.0.0.1:" + std::to_string(port);
    const auto directory = (std::filesystem::temp_directory_path() / "jpet-models-test").string();
    std::filesystem::remove_all(directory);
    const std::vector<Voice::SpeechModelStore::File> files = {
      {"model.int8.onnx", model.size(), modelHash.c_str()}, {"tokens.txt", tokens.size(), tokensHash.c_str()}};

    {
      Voice::SpeechModelStore store(directory, {base + "/broken/", base + "/good/"}, files);
      Check(store.Installed().empty() && store.Status()["state"] == "missing", "nothing is installed at first");
      store.Download();
      Check(Wait(store) == "ready" && store.Installed() == directory, "a failing mirror falls back to the next");
      Check(store.Status()["received"] == model.size() + tokens.size(), "progress reports the full size");
    }
    {
      Voice::SpeechModelStore store(directory, {base + "/good/"}, files);
      Check(store.Status()["state"] == "ready", "a verified install is recognised after restart");
      std::string error;
      Check(store.Remove(error) && store.Installed().empty(), "the model can be removed");
    }
    {
      corrupt = true;
      Voice::SpeechModelStore store(directory, {base + "/good/"}, files);
      store.Download();
      Check(Wait(store) == "failed" && store.Installed().empty() &&
            !std::filesystem::exists(std::filesystem::path(directory) / "model.int8.onnx"),
            "a file with the wrong hash is rejected and not kept");
      corrupt = false;
    }
    {
      slow = true;
      Voice::SpeechModelStore store(directory, {base + "/good/"}, files);
      store.Download();
      store.Cancel();
      Check(store.Status()["state"] == "missing", "cancel stops the download");
      slow = false;
      store.Download();
      Check(Wait(store) == "ready", "a cancelled download can start again");
    }
    {
      using json = nlohmann::json;
      const json ready = {{"state", "ready"}, {"received", 10}, {"total", 10}, {"error", ""}};
      const json missing = {{"state", "missing"}, {"received", 0}, {"total", 30}, {"error", ""}};
      const json failed = {{"state", "failed"}, {"received", 5}, {"total", 30}, {"error", "下载失败"}};
      const json downloading = {{"state", "downloading"}, {"received", 7}, {"total", 30}, {"error", ""}};
      const auto Combine = Voice::SpeechModelStore::Combine;
      Check(Combine({ready, ready})["state"] == "ready", "both models ready");
      Check(Combine({ready, missing}) == json({{"state", "missing"}, {"received", 10}, {"total", 40}, {"error", ""}}),
            "a missing model makes the set missing and progress adds up");
      Check(Combine({failed, missing}) == json({{"state", "failed"}, {"received", 5}, {"total", 60}, {"error", "下载失败"}}),
            "a failure is reported with its message");
      Check(Combine({failed, downloading})["state"] == "downloading", "a download in progress wins");
    }
    server.stop();
    listener.join();
    std::filesystem::remove_all(directory);
    std::cout << "PASS voice model download\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
