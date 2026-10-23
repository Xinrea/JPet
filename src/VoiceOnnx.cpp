#include "VoiceOnnx.hpp"

#include <onnxruntime_c_api.h>

#include <cstdlib>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace Voice {
namespace {
struct Runtime {
  const OrtApi* api = nullptr;
  OrtEnv* env = nullptr;
  std::string error;
};

std::string Directory(const std::string& path) {
  const auto slash = path.find_last_of("/\\");
  return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}

#ifdef _WIN32
std::wstring Wide(const std::string& value) {
  if (value.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  std::wstring out(length, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), length);
  return out;
}
#endif

// Loads the library once for the process lifetime; models share one OrtEnv.
const Runtime& LoadRuntime() {
  static Runtime runtime;
  static std::once_flag once;
  std::call_once(once, [] {
    const auto path = OnnxRuntimeLibrary();
    using GetApiBase = const OrtApiBase* (ORT_API_CALL*)();
    GetApiBase getApiBase = nullptr;
#ifdef _WIN32
    // A full path avoids picking up an older onnxruntime.dll from System32.
    if (HMODULE library = LoadLibraryExW(Wide(path).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH))
      getApiBase = reinterpret_cast<GetApiBase>(GetProcAddress(library, "OrtGetApiBase"));
#else
    if (void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL))
      getApiBase = reinterpret_cast<GetApiBase>(dlsym(library, "OrtGetApiBase"));
#endif
    if (!getApiBase) {
      runtime.error = "无法加载本地语音组件 onnxruntime，请重新安装 JPet";
      return;
    }
    runtime.api = getApiBase()->GetApi(ORT_API_VERSION);
    if (!runtime.api) { runtime.error = "本地语音检测组件版本不兼容，请重新安装 JPet"; return; }
    if (OrtStatus* status = runtime.api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "jpet", &runtime.env)) {
      runtime.api->ReleaseStatus(status);
      runtime.api = nullptr;
      runtime.error = "无法初始化本地语音检测组件";
    }
  });
  return runtime;
}
}  // namespace

std::string OnnxRuntimeLibrary() {
  if (const char* path = std::getenv("JPET_ONNXRUNTIME_PATH"); path && *path) return path;
#ifdef _WIN32
  wchar_t buffer[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  std::string executable;
  if (length) {
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    executable.resize(bytes);
    WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), executable.data(), bytes, nullptr, nullptr);
  }
  return Directory(executable) + "\\onnxruntime.dll";
#elif defined(__APPLE__)
  char buffer[4096];
  uint32_t size = sizeof(buffer);
  if (_NSGetExecutablePath(buffer, &size) != 0) return "libonnxruntime.dylib";
  return Directory(buffer) + "/../Frameworks/libonnxruntime.dylib";
#else
  return "libonnxruntime.so";
#endif
}

bool LoadOnnxRuntime(std::string& error) {
  const auto& runtime = LoadRuntime();
  if (!runtime.api) error = runtime.error;
  return runtime.api != nullptr;
}

struct OnnxModel::Impl {
  const OrtApi* api = nullptr;
  OrtSession* session = nullptr;
  OrtMemoryInfo* memory = nullptr;
  std::vector<std::string> outputs;
  ~Impl() {
    if (session) api->ReleaseSession(session);
    if (memory) api->ReleaseMemoryInfo(memory);
  }
  // Consumes status; returns true when it reported an error.
  bool Failed(OrtStatus* status, std::string& error) {
    if (!status) return false;
    error = api->GetErrorMessage(status);
    api->ReleaseStatus(status);
    return true;
  }
};

OnnxModel::OnnxModel(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OnnxModel::~OnnxModel() = default;

std::unique_ptr<OnnxModel> OnnxModel::Load(const std::string& path, std::string& error) {
  const auto& runtime = LoadRuntime();
  if (!runtime.api) { error = runtime.error; return nullptr; }
  auto impl = std::make_unique<Impl>();
  impl->api = runtime.api;
  const OrtApi* api = runtime.api;
  OrtSessionOptions* options = nullptr;
  std::string detail;
  bool failed = impl->Failed(api->CreateSessionOptions(&options), detail);
  // Small models: one thread each keeps inference off the other cores.
  if (!failed) failed = impl->Failed(api->SetIntraOpNumThreads(options, 1), detail) ||
      impl->Failed(api->SetInterOpNumThreads(options, 1), detail) ||
      impl->Failed(api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL), detail);
#ifdef _WIN32
  if (!failed) failed = impl->Failed(api->CreateSession(runtime.env, Wide(path).c_str(), options, &impl->session), detail);
#else
  if (!failed) failed = impl->Failed(api->CreateSession(runtime.env, path.c_str(), options, &impl->session), detail);
#endif
  if (options) api->ReleaseSessionOptions(options);
  if (!failed) failed = impl->Failed(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &impl->memory), detail);
  size_t count = 0;
  if (!failed) failed = impl->Failed(api->SessionGetOutputCount(impl->session, &count), detail);
  OrtAllocator* allocator = nullptr;
  if (!failed) failed = impl->Failed(api->GetAllocatorWithDefaultOptions(&allocator), detail);
  for (size_t i = 0; !failed && i < count; ++i) {
    char* name = nullptr;
    failed = impl->Failed(api->SessionGetOutputName(impl->session, i, allocator, &name), detail);
    if (!failed) { impl->outputs.emplace_back(name); allocator->Free(allocator, name); }
  }
  if (failed) { error = "无法加载语音模型：" + detail; return nullptr; }
  return std::unique_ptr<OnnxModel>(new OnnxModel(std::move(impl)));
}

bool OnnxModel::Run(const std::vector<Input>& inputs, std::vector<std::vector<float>>& outputs, std::string& error) {
  const OrtApi* api = impl_->api;
  std::vector<const char*> inputNames, outputNames;
  std::vector<OrtValue*> inputValues, outputValues(impl_->outputs.size(), nullptr);
  bool failed = false;
  for (const auto& input : inputs) {
    size_t count = 1;
    for (const auto dimension : input.shape) count *= static_cast<size_t>(dimension);
    OrtValue* value = nullptr;
    void* data = input.floats ? static_cast<void*>(const_cast<float*>(input.floats))
                              : static_cast<void*>(const_cast<int64_t*>(input.ints));
    const size_t bytes = count * (input.floats ? sizeof(float) : sizeof(int64_t));
    failed = impl_->Failed(api->CreateTensorWithDataAsOrtValue(impl_->memory, data, bytes, input.shape.data(),
        input.shape.size(), input.floats ? ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT : ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
        &value), error);
    if (failed) break;
    inputNames.push_back(input.name);
    inputValues.push_back(value);
  }
  for (const auto& name : impl_->outputs) outputNames.push_back(name.c_str());
  if (!failed) failed = impl_->Failed(api->Run(impl_->session, nullptr, inputNames.data(), inputValues.data(),
      inputValues.size(), outputNames.data(), outputNames.size(), outputValues.data()), error);
  outputs.clear();
  for (auto* value : outputValues) {
    if (!value) continue;
    if (!failed) {
      OrtTensorTypeAndShapeInfo* info = nullptr;
      size_t count = 0;
      float* data = nullptr;
      failed = impl_->Failed(api->GetTensorTypeAndShape(value, &info), error) ||
          impl_->Failed(api->GetTensorShapeElementCount(info, &count), error) ||
          impl_->Failed(api->GetTensorMutableData(value, reinterpret_cast<void**>(&data)), error);
      if (info) api->ReleaseTensorTypeAndShapeInfo(info);
      if (!failed) outputs.emplace_back(data, data + count);
    }
    api->ReleaseValue(value);
  }
  for (auto* value : inputValues) api->ReleaseValue(value);
  return !failed;
}
}  // namespace Voice
