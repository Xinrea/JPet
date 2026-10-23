#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Voice {
// ONNX Runtime (shipped with sherpa-onnx) is loaded at run time through its C
// API, so voice features that depend on it degrade with a readable error
// instead of failing to launch.
class OnnxModel {
 public:
  struct Input {
    const char* name;
    std::vector<int64_t> shape;
    const float* floats = nullptr;      // Exactly one of floats/ints is set.
    const int64_t* ints = nullptr;
  };
  // Returns null and sets error when the runtime or the model cannot be loaded.
  static std::unique_ptr<OnnxModel> Load(const std::string& path, std::string& error);
  ~OnnxModel();
  // Runs once; outputs are returned as float tensors in model output order.
  bool Run(const std::vector<Input>& inputs, std::vector<std::vector<float>>& outputs, std::string& error);

 private:
  struct Impl;
  explicit OnnxModel(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

// Runtime library location: JPET_ONNXRUNTIME_PATH, else next to the executable
// (Windows) or in the app bundle's Frameworks directory (macOS).
std::string OnnxRuntimeLibrary();
// Loads ONNX Runtime once for the process; libraries built on it (sherpa-onnx)
// must be loaded afterwards so they bind to this copy.
bool LoadOnnxRuntime(std::string& error);
}  // namespace Voice
