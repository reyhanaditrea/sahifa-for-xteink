#pragma once

// Host backend for StarDictIndex::Reader: the same probing code the device runs
// against HalFile, reading an ordinary file instead.

#include <StarDictIndex.h>

#include <cstdio>
#include <string>

class StdioReader : public StarDictIndex::Reader {
 public:
  explicit StdioReader(const std::string& path) {
    if (path.empty()) return;
    file = std::fopen(path.c_str(), "rb");
    if (!file) return;
    std::fseek(file, 0, SEEK_END);
    bytesTotal = static_cast<size_t>(std::ftell(file));
    std::fseek(file, 0, SEEK_SET);
  }
  ~StdioReader() override {
    if (file) std::fclose(file);
  }
  StdioReader(const StdioReader&) = delete;
  StdioReader& operator=(const StdioReader&) = delete;

  bool ok() const { return file != nullptr; }

  bool seekSet(size_t pos) override { return file && std::fseek(file, static_cast<long>(pos), SEEK_SET) == 0; }
  int read(void* buf, size_t bytes) override {
    if (!file) return -1;
    return static_cast<int>(std::fread(buf, 1, bytes, file));
  }
  int read() override {
    if (!file) return -1;
    const int c = std::fgetc(file);
    return c == EOF ? -1 : c;
  }
  size_t position() const override { return file ? static_cast<size_t>(std::ftell(file)) : 0; }
  size_t fileSize() override { return bytesTotal; }

 private:
  FILE* file = nullptr;
  size_t bytesTotal = 0;
};
