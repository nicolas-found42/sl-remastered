#pragma once

#include "game_file.hpp"

#include <array>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unistd.h>
#include <unordered_map>

struct VmObject;

enum class FileOpenResult { opened,
                            missing,
                            error };

// Owners must remain alive until closed or this state is destroyed.
// Not copyable or movable: open writers own raw descriptors, so a second
// instance holding the same values would double-close them.
class FileState {
public:
  FileState(std::filesystem::path dataRoot,
            std::filesystem::path referenceRoot,
            std::filesystem::path gameRoot = {});
  FileState(const FileState&) = delete;
  FileState& operator=(const FileState&) = delete;
  FileState(FileState&&) = delete;
  FileState& operator=(FileState&&) = delete;
  // Writers own their descriptors, so an unclosed writer is released here.
  ~FileState();
  bool exists(const std::string& name, bool& result, std::string& error) const;
  FileOpenResult openRead(const VmObject* owner, const std::string& name,
                          std::string& error);
  bool readInt(const VmObject* owner, int32_t& value, std::string& error);
  bool readFloat(const VmObject* owner, float& value, std::string& error);
  bool readString(const VmObject* owner, std::string& value, std::string& error);
  const std::vector<std::pair<uint32_t, std::string>>*
  readPacks(const VmObject* owner) const;
  // Write mode (File.open(1), FUN_0054caf0): creates missing directories,
  // truncates, then streams the 8-byte SDAT header and payload. close() appends
  // the type-2 trailer (FUN_00485870). Like stock, which holds the CreateFileA
  // handle from open to close, the port keeps a non-following descriptor open
  // from openWrite to close so a symlink swapped in after the open cannot
  // redirect the deferred write. close() returns the failure text, which the
  // File.close native turns into a run-log Error: stock logs
  // "Invalid datafile saved" and returns void, so the script keeps running.
  FileOpenResult openWrite(const VmObject* owner, const std::string& name,
                           std::string& error);
  bool writeBytes(const VmObject* owner, const void* data, size_t size,
                  std::string& error);
  // Pack table entry for a File.write(ResourceRef); returns its 1-based ordinal
  // (FUN_00485c60 else branch). Entries are keyed by pack path.
  uint32_t packOrdinal(const VmObject* owner, const std::string& packPath);
  // File.delete/copy/move (FUN_0054c7c0/c550/c650): 0 on success, -1 on
  // failure; a missing source is a silent -1 like the stock Win32 path.
  bool deletePath(const std::string& name, int32_t& result, std::string& error);
  bool copyPath(const std::string& from, const std::string& to,
                int32_t& result, std::string& error);
  bool movePath(const std::string& from, const std::string& to,
                int32_t& result, std::string& error);
  bool isWriter(const VmObject* owner) const { return writers_.count(owner) != 0; }
  // Flushes the buffered SDAT payload; returns an empty string on success.
  // Both callers turn a failure into a run-log Error as stock does: the
  // File.close native, and openWrite when it must retire an earlier writer.
  std::string close(const VmObject* owner);
  std::optional<int32_t> handle(const VmObject* owner) const;

  bool findFirst(const std::string& pattern, int32_t flags, int32_t& handle,
                 std::optional<std::string>& result, std::string& error);
  std::optional<std::string> findNext(int32_t handle, int32_t flags);
  void findClose(int32_t handle);

private:
  bool resolveSavePath(const std::string& name, std::filesystem::path& out,
                       std::string& error) const;
  struct SearchEntry {
    std::string name;
    bool directory;
  };
  struct Search {
    std::vector<SearchEntry> entries;
    size_t next = 0;
  };
  std::array<std::optional<Search>, 32> searches_;
  std::filesystem::path gameRoot_;
  std::filesystem::path dataRoot_;
  std::filesystem::path referenceRoot_;
  struct Entry {
    GameFile file;
    int32_t handle;
  };
  std::unordered_map<const VmObject*, Entry> files_;
  // Owns its descriptor: constructed by openWrite with the fd already set, so
  // no exception between ::open and insertion can strand it, and any unwind
  // inside close() still releases it.
  struct Writer {
    std::filesystem::path path;
    std::vector<uint8_t> payload;
    std::vector<std::string> packs;
    int32_t handle = 0;
    // Descriptor held from openWrite to close, mirroring the stock handle
    // lifetime (CreateFileA at FUN_0054caf0); opened O_NOFOLLOW there.
    int fd = -1;
    Writer() = default;
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    Writer(Writer&& other) noexcept : path(std::move(other.path)),
                                      payload(std::move(other.payload)),
                                      packs(std::move(other.packs)),
                                      handle(other.handle), fd(other.fd) {
      other.fd = -1;
    }
    Writer& operator=(Writer&& other) noexcept {
      if (this != &other) {
        if (fd >= 0)
          ::close(fd);
        path = std::move(other.path);
        payload = std::move(other.payload);
        packs = std::move(other.packs);
        handle = other.handle;
        fd = other.fd;
        other.fd = -1;
      }
      return *this;
    }
    ~Writer() {
      if (fd >= 0)
        ::close(fd);
    }
  };
  std::unordered_map<const VmObject*, Writer> writers_;
  int64_t nextHandle_ = 1;
};
