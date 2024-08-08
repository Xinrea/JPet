#pragma once

#include <string>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <filesystem>

#include "LAppPal.hpp"

enum class EntryType {
  TypeString,
  TypeInt,
  TypeBool,
  TypeFloat,
};

class Entry {
 public:
  EntryType type;
  std::string key;
  std::vector<char> value;

  Entry(EntryType type, const std::string& key, int v)
      : type(EntryType::TypeInt), key(key) {
    value = EncodeVarint(v);
  }

  Entry(EntryType type, const std::string& key, bool v)
      : type(EntryType::TypeBool), key(key) {
    value.push_back(v ? 1 : 0);
  }

  Entry(EntryType type, const std::string& key, float v)
      : type(EntryType::TypeFloat), key(key) {
    value.resize(sizeof(float));
    memcpy(value.data(), &v, sizeof(float));
  }

  Entry(EntryType type, const std::string& key, const std::string& v)
      : type(EntryType::TypeString), key(key) {
    value.insert(value.end(), v.begin(), v.end());
  }

  Entry(const std::vector<char>& data, size_t& offset) {
    type = static_cast<EntryType>(data[offset++]);
    // read key
    int keySize = DecodeVarint(data, offset);
    key.resize(keySize);
    std::copy(data.begin() + offset, data.begin() + offset + keySize,
              key.begin());
    offset += keySize;
    // read value
    int valueSize = DecodeVarint(data, offset);
    value.resize(valueSize);
    std::copy(data.begin() + offset, data.begin() + offset + valueSize,
              value.begin());
    offset += valueSize;
  }

  // Get value
  int getInt() {
    if (type != EntryType::TypeInt) {
      throw std::runtime_error("Type mismatch");
    }
    size_t offset = 0;
    return DecodeVarint(value, offset);
  }

  bool getBool() {
    if (type != EntryType::TypeBool) {
      throw std::runtime_error("Type mismatch");
    }
    return value[0] == 1;
  }

  float getFloat() {
    if (type != EntryType::TypeFloat) {
      throw std::runtime_error("Type mismatch");
    }
    float v;
    memcpy(&v, value.data(), sizeof(float));
    return v;
  }

  std::string getString() {
    if (type != EntryType::TypeString) {
      throw std::runtime_error("Type mismatch");
    }
    return std::string(value.begin(), value.end());
  }

  std::vector<char> serialize() {
    // serialize data depends on type
    std::vector<char> data;
    // write type
    data.push_back(static_cast<char>(type));
    // write key length with varint
    auto keySizeEncoded = EncodeVarint(key.size());
    data.insert(data.end(), keySizeEncoded.begin(), keySizeEncoded.end());
    // write key
    data.insert(data.end(), key.begin(), key.end());
    // write value length with varint
    auto valueSizeEncoded = EncodeVarint(value.size());
    data.insert(data.end(), valueSizeEncoded.begin(), valueSizeEncoded.end());
    // write value
    data.insert(data.end(), value.begin(), value.end());
    return data;
  }

  std::vector<char> EncodeVarint(int value) {
    std::vector<char> result;
    while (value >= 0x80) {
      result.push_back((value & 0x7F) | 0x80);
      value >>= 7;
    }
    result.push_back(value);
    return result;
  }

  int DecodeVarint(const std::vector<char>& data, size_t& offset) {
    int result = 0;
    int shift = 0;
    while (offset < data.size()) {
      char byte = data[offset++];
      result |= (byte & 0x7F) << shift;
      if ((byte & 0x80) == 0) {
        return result;
      }
      shift += 7;
    }
    throw std::runtime_error("Varint decode failed");
  }
};

/**
 * @brief  GameData manager
 * basicly a simple kv store for game data
 */
class GameData {
 private:
  std::wstring _path;
  std::unordered_map<std::string, Entry> _m;
  std::mutex _mutex;

  void parse(const std::vector<char>& data) {
    std::lock_guard<std::mutex> lock(_mutex);
    size_t offset = 0;
    while (offset < data.size()) {
      Entry entry(data, offset);
      _m.insert({entry.key, entry});
    }
    LAppPal::PrintLog(LogLevel::Debug, "[GameData]Parse done");
  }

 public:
  GameData(const std::wstring& path) : _path(path) {
    // load data from file, if file not exist, create one
    if (!std::filesystem::exists(std::filesystem::path(_path))) {
      std::ofstream file(_path);
      if (!file.is_open()) {
        LAppPal::PrintLog(LogLevel::Debug, "Failed to create file");
        throw std::runtime_error("Failed to create file");
      }
      file.close();
    }
    std::ifstream file(_path, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
      std::streamsize size = file.tellg();
      file.seekg(0, std::ios::beg);
      // print file size
      std::vector<char> buffer(size);
      if (!file.read(buffer.data(), size)) {
        throw std::runtime_error("Failed to read file");
      }
      file.close();
      parse(buffer);
    } else {
      throw std::runtime_error("Failed to open file");
    }
  }

  void Dump() {
    // write data to file
    std::ofstream file(_path, std::ios::binary);
    if (file.is_open()) {
      for (auto& kv : _m) {
        auto data = kv.second.serialize();
        file.write(data.data(), data.size());
      }
      file.close();
    } else {
      throw std::runtime_error("Failed to open file");
    }
  }

  void Update(const std::string& key, int value) {
    std::lock_guard<std::mutex> lock(_mutex);
    _m.insert_or_assign(key, Entry(EntryType::TypeInt, key, value));
    Dump();
  }

  void Update(const std::string& key, bool value) {
    std::lock_guard<std::mutex> lock(_mutex);
    _m.insert_or_assign(key, Entry(EntryType::TypeBool, key, value));
    Dump();
  }

  void Update(const std::string& key, float value) {
    std::lock_guard<std::mutex> lock(_mutex);
    _m.insert_or_assign(key, Entry(EntryType::TypeFloat, key, value));
    Dump();
  }

  void Update(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(_mutex);
    _m.insert_or_assign(key, Entry(EntryType::TypeString, key, value));
    Dump();
  }

  bool Get(const std::string& key, int& value) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_m.find(key) != _m.end()) {
      value = _m.at(key).getInt();
      return true;
    }
    return false;
  }

  bool Get(const std::string& key, bool& value) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_m.find(key) != _m.end()) {
      value = _m.at(key).getBool();
      return true;
    }
    return false;
  }

  bool Get(const std::string& key, float& value) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_m.find(key) != _m.end()) {
      value = _m.at(key).getFloat();
      return true;
    }
    return false;
  }

  bool Get(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_m.find(key) != _m.end()) {
      value = _m.at(key).getString();
      return true;
    }
    return false;
  }
};
