#pragma once

#include "game_file.hpp"

#include <array>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

struct VmObject;

enum class FileOpenResult { opened, missing, error };

// Owners must remain alive until closed or this state is destroyed.
class FileState {
public:
  FileState(std::filesystem::path dataRoot,
            std::filesystem::path referenceRoot,
            std::filesystem::path gameRoot = {});
  bool exists(const std::string &name, bool &result, std::string &error) const;
  FileOpenResult openRead(const VmObject *owner, const std::string &name,
                          std::string &error);
  bool readInt(const VmObject *owner, int32_t &value, std::string &error);
  bool readFloat(const VmObject *owner, float &value, std::string &error);
  bool readString(const VmObject *owner, std::string &value, std::string &error);
  const std::vector<std::pair<uint32_t, std::string>> *
  readPacks(const VmObject *owner) const;
  // Write mode (File.open(1), FUN_0054caf0): creates missing directories,
  // truncates, then streams the 8-byte SDAT header and payload. close() appends
  // the type-2 trailer (FUN_00485870).
  FileOpenResult openWrite(const VmObject *owner, const std::string &name,
                           std::string &error);
  bool writeBytes(const VmObject *owner, const void *data, size_t size,
                  std::string &error);
  // Pack table entry for a File.write(ResourceRef); returns its 1-based ordinal
  // (FUN_00485c60 else branch). Entries are keyed by pack path.
  uint32_t packOrdinal(const VmObject *owner, const std::string &packPath);
  // File.delete/copy/move (FUN_0054c7c0/c550/c650): 0 on success, -1 on
  // failure; a missing source is a silent -1 like the stock Win32 path.
  bool deletePath(const std::string &name, int32_t &result, std::string &error);
  bool copyPath(const std::string &from, const std::string &to,
                int32_t &result, std::string &error);
  bool movePath(const std::string &from, const std::string &to,
                int32_t &result, std::string &error);
  bool isWriter(const VmObject *owner) const { return writers_.count(owner) != 0; }
  void close(const VmObject *owner);
  std::optional<int32_t> handle(const VmObject *owner) const;

  bool findFirst(const std::string &pattern, int32_t flags, int32_t &handle,
                 std::optional<std::string> &result, std::string &error);
  std::optional<std::string> findNext(int32_t handle, int32_t flags);
  void findClose(int32_t handle);

private:
  bool resolveSavePath(const std::string &name, std::filesystem::path &out,
                       std::string &error) const;
  struct SearchEntry { std::string name; bool directory; };
  struct Search { std::vector<SearchEntry> entries; size_t next = 0; };
  std::array<std::optional<Search>, 32> searches_;
  std::filesystem::path gameRoot_;
  std::filesystem::path dataRoot_;
  std::filesystem::path referenceRoot_;
  struct Entry {
    GameFile file;
    int32_t handle;
  };
  std::unordered_map<const VmObject *, Entry> files_;
  struct Writer {
    std::filesystem::path path;
    std::vector<uint8_t> payload;
    std::vector<std::string> packs;
    int32_t handle;
  };
  std::unordered_map<const VmObject *, Writer> writers_;
  int64_t nextHandle_ = 1;
};
