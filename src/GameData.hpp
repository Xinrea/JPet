#pragma once

#include <string>
#include <fstream>
#include <vector>
#include <filesystem>
#include <rocksdb/db.h>

#include "LAppPal.hpp"
#include "LAppDefine.hpp"

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
  rocksdb::DB* db;
  rocksdb::WriteOptions writeOptions;

  void parse(const std::vector<char>& data) {
    try {
      size_t offset = 0;
      while (offset < data.size()) {
        Entry entry(data, offset);
        switch (entry.type)
        {
        case EntryType::TypeInt: {
          int32_t v = entry.getInt();
          db->Put(writeOptions, entry.key, std::string(reinterpret_cast<char*>(&v), sizeof(int32_t)));
          break;
        }
        case EntryType::TypeBool: {
          bool b = entry.getBool();
          db->Put(writeOptions, entry.key, std::string(reinterpret_cast<char*>(&b), sizeof(bool)));
          break;
        }
        case EntryType::TypeFloat: {
          float f = entry.getFloat();
          db->Put(writeOptions, entry.key, std::string(reinterpret_cast<char*>(&f), sizeof(float)));
          break;
        }
        case EntryType::TypeString: {
          db->Put(writeOptions, entry.key, entry.getString());
          break;
        }
        default: {
          LAppPal::PrintLog(LogLevel::Warn, "[GameData]Unknown entry type");
          break;
        }
        }
      }
    } catch (const std::exception& e) {
      LAppPal::PrintLog(LogLevel::Error, e.what());
    }
    LAppPal::PrintLog(LogLevel::Debug, "[GameData]Parse done");
  }

 public:
  GameData(const std::wstring& old_datapath) {
    writeOptions.sync = true;
    rocksdb::Options options;
    options.create_if_missing = true;
    std::wstring dbPath = LAppDefine::documentPath + L"/GameData";
    rocksdb::Status status =
        rocksdb::DB::Open(options, LAppPal::WStringToString(dbPath), &db);
    if (!status.ok()) {
      LAppPal::PrintLog(LogLevel::Error, "[GameData]Failed to open db: %d", status.code());
      return;
    }
    // load old data from file, if file not exist, skip reading
    if (std::filesystem::exists(std::filesystem::path(old_datapath))) {
      std::ifstream file(old_datapath, std::ios::binary | std::ios::ate);
      if (file.is_open()) {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        // print file size
        std::vector<char> buffer(size);
        if (file.read(buffer.data(), size)) {
          file.close();
          parse(buffer);
        }
      }

      // old data file is no longer needed
      std::filesystem::remove(old_datapath);
    }
  }

  ~GameData() {
    db->Close();
    delete db;
  }

  bool Initialized() {
    return db != nullptr;
  }

  void Drop() {
    rocksdb::Slice start("");
    rocksdb::Slice end("\xFF");
    auto status = db->DeleteRange(writeOptions, db->DefaultColumnFamily(), start, end);
    if (!status.ok()) {
      LAppPal::PrintLog(LogLevel::Error, "[GameData]Drop db failed: %d", status.code());
      return;
    }
    LAppPal::PrintLog(LogLevel::Info, "[GameData]Database dropped");
  }

  void Update(const std::string& key, int32_t value) {
    auto status = db->Put(writeOptions, key, std::string(reinterpret_cast<char*>(&value), sizeof(int32_t)));
    if (!status.ok()) {
      LAppPal::PrintLog(LogLevel::Error, "[GameData]Failed to update %s", key.c_str());
    }
  }

  void Update(const std::string& key, bool value) {
    auto status = db->Put(writeOptions, key, std::string(reinterpret_cast<char*>(&value), sizeof(bool)));
    if (!status.ok()) {
      LAppPal::PrintLog(LogLevel::Error, "[GameData]Failed to update %s", key.c_str());
    }
  }

  void Update(const std::string& key, float value) {
    auto status = db->Put(writeOptions, key, std::string(reinterpret_cast<char*>(&value), sizeof(float)));
    if (!status.ok()) {
      LAppPal::PrintLog(LogLevel::Error, "[GameData]Failed to update %s", key.c_str());
    }
  }

  void Update(const std::string& key, const std::string& value) {
    auto status = db->Put(writeOptions, key, value);
    if (!status.ok()) {
      LAppPal::PrintLog(LogLevel::Error, "[GameData]Failed to update %s", key.c_str());
    }
  }

  bool Get(const std::string& key, int32_t& value) {
    try {
      std::string v;
      auto status = db->Get(rocksdb::ReadOptions(), key, &v);
      if (status.ok()) {
        value = *reinterpret_cast<int32_t *>(v.data());
        return true;
      }
    } catch (const std::exception &e) {
      LAppPal::PrintLog(LogLevel::Debug, "[GameData]Get key failed %s: %s", key.c_str(), e.what());
    }
    return false;
  }

  bool Get(const std::string& key, bool& value) {
    try {
      std::string v;
      auto status = db->Get(rocksdb::ReadOptions(), key, &v);
      if (status.ok()) {
        value = *reinterpret_cast<bool *>(v.data());
        return true;
      }
    } catch (const std::exception &e) {
      LAppPal::PrintLog(LogLevel::Debug, "[GameData]Get key failed %s: %s", key.c_str(), e.what());
    }
    return false;
  }

  bool Get(const std::string& key, float& value) {
    try {
      std::string v;
      auto status = db->Get(rocksdb::ReadOptions(), key, &v);
      if (status.ok()) {
        value = *reinterpret_cast<float *>(v.data());
        return true;
      }
    } catch (const std::exception &e) {
      LAppPal::PrintLog(LogLevel::Debug, "[GameData]Get key failed %s: %s", key.c_str(), e.what());
    }
    return false;
  }

  bool Get(const std::string& key, std::string& value) {
    try {
      std::string v;
      auto status = db->Get(rocksdb::ReadOptions(), key, &v);
      if (status.ok()) {
        value = v;
        return true;
      }
    } catch (const std::exception &e) {
      LAppPal::PrintLog(LogLevel::Debug, "[GameData]Get key failed %s: %s", key.c_str(), e.what());
    }
    return false;
  }
};
