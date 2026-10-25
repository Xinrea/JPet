#pragma once

#include <CubismModelSettingJson.hpp>
#include <memory>
#include <stdexcept>
#include <string>

#include "LAppPal.hpp"

// Cubism's setting getters are unsafe after a parse failure. Keep validation
// here, before ownership can pass to a model or any getter can be called.
inline std::unique_ptr<Csm::CubismModelSettingJson> LoadModelSettings(
    const std::string& path) {
  Csm::csmSizeInt size = 0;
  std::unique_ptr<Csm::csmByte[]> buffer(LAppPal::LoadFileAsBytes(path, &size));
  if (!buffer || size <= 0) {
    throw std::runtime_error("Cannot read model settings: " + path);
  }
  auto setting = std::make_unique<Csm::CubismModelSettingJson>(buffer.get(), size);
  if (!setting->IsValid()) {
    throw std::runtime_error("Invalid model settings JSON: " + path);
  }
  if (!*setting->GetModelFileName()) {
    throw std::runtime_error("Model settings have no Moc file: " + path);
  }
  return setting;
}
