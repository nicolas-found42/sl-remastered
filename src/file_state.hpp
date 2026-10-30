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
  void close(const VmObject *owner);
  std::optional<int32_t> handle(const VmObject *owner) const;

  bool findFirst(const std::string &pattern, int32_t flags, int32_t &handle,
                 std::optional<std::string> &result, std::string &error);
  std::optional<std::string> findNext(int32_t handle, int32_t flags);
  void findClose(int32_t handle);

private:
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
  int64_t nextHandle_ = 1;
};
