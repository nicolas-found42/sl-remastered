#include "file_state.hpp"

#include <algorithm>
#include <climits>
#include <fstream>
#include <system_error>
#include <utility>

FileState::FileState(std::filesystem::path dataRoot,
                     std::filesystem::path referenceRoot,
                     std::filesystem::path gameRoot)
    : gameRoot_(std::move(gameRoot)), dataRoot_(std::move(dataRoot)), referenceRoot_(std::move(referenceRoot)) {
}

bool FileState::exists(const std::string &name, bool &result,
                       std::string &error) const {
  result = false;
  error.clear();
  if (name.empty() || name.size() >= 256 || name.front() == '/' ||
      name.front() == '\\' || name.find(':') != std::string::npos ||
      name.find('\0') != std::string::npos) {
    error = "unsupported File.exists path";
    return false;
  }
  std::string normalized = name;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  const std::filesystem::path relative(normalized);
  auto part = relative.begin();
  if (part == relative.end() || *part != "save" || ++part == relative.end()) {
    error = "File.exists path is not beneath save";
    return false;
  }
  for (const auto &component : relative)
    if (component == "." || component == ".." || component.empty()) {
      error = "unsupported File.exists path";
      return false;
    }
  auto within = [](const std::filesystem::path &root,
                   const std::filesystem::path &path) {
    auto a = root.begin(), b = path.begin();
    for (; a != root.end() && b != path.end(); ++a, ++b)
      if (*a != *b)
        return false;
    return a == root.end();
  };
  std::error_code ec;
  const auto ref = std::filesystem::canonical(referenceRoot_, ec);
  if (ec) {
    error = "invalid reference root";
    return false;
  }
  const auto root = std::filesystem::canonical(dataRoot_, ec);
  if (ec || within(ref, root)) {
    error = "invalid or reference data root";
    return false;
  }
  const auto save = std::filesystem::canonical(root / "save", ec);
  if (ec || !within(root, save) || within(ref, save)) {
    error = "invalid save directory";
    return false;
  }
  const auto path = std::filesystem::weakly_canonical(root / relative, ec);
  if (ec || !within(save, path) || within(ref, path)) {
    error = "File.exists path escapes data root";
    return false;
  }
  const bool present = std::filesystem::exists(path, ec);
  if (ec) {
    error = "File.exists filesystem error: " + ec.message();
    return false;
  }
  if (present) {
    if (!std::filesystem::is_regular_file(path, ec)) {
      if (ec) {
        error = "File.exists filesystem error: " + ec.message();
        return false;
      }
      return true;
    }
    std::ifstream input(path, std::ios::binary);
    result = input.is_open();
  }
  return true;
}

FileOpenResult FileState::openRead(const VmObject *owner,
                                   const std::string &name,
                                   std::string &error) {
  error.clear();
  if (!owner) {
    error = "missing file owner";
    return FileOpenResult::error;
  }
  files_.erase(owner);
  GameFile file;
  if (!file.open(dataRoot_, referenceRoot_, name, error))
    return error == "save file not found" ? FileOpenResult::missing
                                          : FileOpenResult::error;
  if (nextHandle_ > INT32_MAX) {
    error = "file handle table exhausted";
    return FileOpenResult::error;
  }
  files_.emplace(owner,
                 Entry{std::move(file), static_cast<int32_t>(nextHandle_++)});
  return FileOpenResult::opened;
}

bool FileState::readInt(const VmObject *owner, int32_t &value,
                        std::string &error) {
  error.clear();
  const auto file = files_.find(owner);
  if (file == files_.end()) {
    error = "file is not open";
    return false;
  }
  const auto result = file->second.file.readInt();
  if (!result) {
    error = "truncated file payload";
    return false;
  }
  value = *result;
  return true;
}

bool FileState::readFloat(const VmObject *owner, float &value,
                          std::string &error) {
  error.clear();
  const auto file = files_.find(owner);
  if (file == files_.end()) {
    error = "file is not open";
    return false;
  }
  const auto result = file->second.file.readFloat();
  if (!result) {
    error = "truncated file payload";
    return false;
  }
  value = *result;
  return true;
}

bool FileState::resolveSavePath(const std::string &name,
                                std::filesystem::path &out,
                                std::string &error) const {
  error.clear();
  if (name.empty() || name.size() >= 256 || name.front() == '/' ||
      name.front() == '\\' || name.find(':') != std::string::npos ||
      name.find('\0') != std::string::npos) {
    error = "invalid save path";
    return false;
  }
  std::string normalized = name;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  const std::filesystem::path relative(normalized);
  auto part = relative.begin();
  if (part == relative.end() || *part != "save" || ++part == relative.end()) {
    error = "path is not beneath save: " + name;
    return false;
  }
  for (const auto &component : relative)
    if (component == "." || component == ".." || component.empty()) {
      error = "invalid save path";
      return false;
    }
  auto within = [](const std::filesystem::path &root,
                   const std::filesystem::path &path) {
    auto a = root.begin(), b = path.begin();
    for (; a != root.end() && b != path.end(); ++a, ++b)
      if (*a != *b)
        return false;
    return a == root.end();
  };
  std::error_code ec;
  const auto ref = std::filesystem::canonical(referenceRoot_, ec);
  if (ec) {
    error = "invalid reference root";
    return false;
  }
  const auto root = std::filesystem::canonical(dataRoot_, ec);
  if (ec || within(ref, root)) {
    error = "invalid or reference data root";
    return false;
  }
  const auto save = std::filesystem::weakly_canonical(root / "save", ec);
  if (ec || !within(root, save) || within(ref, save)) {
    error = "invalid save directory";
    return false;
  }
  out = std::filesystem::weakly_canonical(root / relative, ec);
  if (ec || !within(save, out) || within(ref, out)) {
    error = "path escapes data root";
    return false;
  }
  return true;
}

bool FileState::deletePath(const std::string &name, int32_t &result,
                           std::string &error) {
  std::filesystem::path path;
  if (!resolveSavePath(name, path, error))
    return false;
  std::error_code ec;
  // DeleteFileA, then RemoveDirectoryA (empty directories only).
  result = std::filesystem::remove(path, ec) && !ec ? 0 : -1;
  return true;
}

bool FileState::copyPath(const std::string &from, const std::string &to,
                         int32_t &result, std::string &error) {
  std::filesystem::path source, target;
  if (!resolveSavePath(from, source, error) ||
      !resolveSavePath(to, target, error))
    return false;
  std::error_code ec;
  std::filesystem::create_directories(target.parent_path(), ec);
  std::filesystem::copy_file(source, target,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  result = ec ? -1 : 0;
  return true;
}

bool FileState::movePath(const std::string &from, const std::string &to,
                         int32_t &result, std::string &error) {
  std::filesystem::path source, target;
  if (!resolveSavePath(from, source, error) ||
      !resolveSavePath(to, target, error))
    return false;
  std::error_code ec;
  std::filesystem::create_directories(target.parent_path(), ec);
  std::filesystem::rename(source, target, ec);
  result = ec ? -1 : 0;
  return true;
}

FileOpenResult FileState::openWrite(const VmObject *owner,
                                    const std::string &name,
                                    std::string &error) {
  error.clear();
  if (!owner) {
    error = "missing file owner";
    return FileOpenResult::error;
  }
  close(owner);
  if (name.empty() || name.size() >= 256 || name.front() == '/' ||
      name.front() == '\\' || name.find(':') != std::string::npos ||
      name.find('\0') != std::string::npos) {
    error = "invalid save path";
    return FileOpenResult::error;
  }
  std::string normalized = name;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  const std::filesystem::path relative(normalized);
  auto part = relative.begin();
  if (part == relative.end() || *part != "save" || ++part == relative.end()) {
    error = "File.open(write) path is not beneath save";
    return FileOpenResult::error;
  }
  for (const auto &component : relative)
    if (component == "." || component == ".." || component.empty()) {
      error = "invalid save path";
      return FileOpenResult::error;
    }
  auto within = [](const std::filesystem::path &root,
                   const std::filesystem::path &path) {
    auto a = root.begin(), b = path.begin();
    for (; a != root.end() && b != path.end(); ++a, ++b)
      if (*a != *b)
        return false;
    return a == root.end();
  };
  std::error_code ec;
  const auto ref = std::filesystem::canonical(referenceRoot_, ec);
  if (ec) {
    error = "invalid reference root";
    return FileOpenResult::error;
  }
  const auto root = std::filesystem::canonical(dataRoot_, ec);
  if (ec || within(ref, root)) {
    error = "invalid or reference data root";
    return FileOpenResult::error;
  }
  const auto save = std::filesystem::weakly_canonical(root / "save", ec);
  if (ec || !within(root, save) || within(ref, save)) {
    error = "invalid save directory";
    return FileOpenResult::error;
  }
  const auto path = std::filesystem::weakly_canonical(root / relative, ec);
  if (ec || !within(save, path) || within(ref, path)) {
    error = "File.open(write) path escapes data root";
    return FileOpenResult::error;
  }
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "cannot create save directory: " + ec.message();
    return FileOpenResult::error;
  }
  std::ofstream truncate(path, std::ios::binary | std::ios::trunc);
  if (!truncate) {
    // Stock logs a Save error and File.open reports failure (returns 0).
    return FileOpenResult::missing;
  }
  truncate.close();
  if (nextHandle_ > INT32_MAX) {
    error = "file handle table exhausted";
    return FileOpenResult::error;
  }
  Writer writer;
  writer.path = path;
  writer.handle = static_cast<int32_t>(nextHandle_++);
  // DAT_00612c70: "SDAT", version 0x0100, type 2, flags 0.
  static const uint8_t header[8] = {'S', 'D', 'A', 'T', 0x00, 0x01, 0x02, 0x00};
  writer.payload.assign(header, header + 8);
  writers_.emplace(owner, std::move(writer));
  return FileOpenResult::opened;
}

bool FileState::writeBytes(const VmObject *owner, const void *data,
                           size_t size, std::string &error) {
  error.clear();
  const auto writer = writers_.find(owner);
  if (writer == writers_.end()) {
    error = "file is not open for writing";
    return false;
  }
  const auto *bytes = static_cast<const uint8_t *>(data);
  writer->second.payload.insert(writer->second.payload.end(), bytes,
                                bytes + size);
  return true;
}

uint32_t FileState::packOrdinal(const VmObject *owner,
                                const std::string &packPath) {
  auto &packs = writers_.at(owner).packs;
  for (size_t i = 0; i < packs.size(); ++i)
    if (packs[i] == packPath)
      return static_cast<uint32_t>(i + 1);
  packs.push_back(packPath);
  return static_cast<uint32_t>(packs.size());
}

bool FileState::readString(const VmObject *owner, std::string &value,
                           std::string &error) {
  error.clear();
  const auto file = files_.find(owner);
  if (file == files_.end()) {
    error = "file is not open";
    return false;
  }
  auto result = file->second.file.readString();
  if (!result) {
    error = "truncated file payload";
    return false;
  }
  value = std::move(*result);
  return true;
}

const std::vector<std::pair<uint32_t, std::string>> *
FileState::readPacks(const VmObject *owner) const {
  const auto file = files_.find(owner);
  return file == files_.end() ? nullptr : &file->second.file.packs();
}

void FileState::close(const VmObject *owner) {
  files_.erase(owner);
  const auto found = writers_.find(owner);
  if (found == writers_.end())
    return;
  // FUN_00485870 type 2: unsaved-resource count (0: unsaved resources are an
  // explicit gap and refused at write time), pack count, pack records, then
  // the trailer size excluding the size field itself.
  Writer writer = std::move(found->second);
  writers_.erase(found);
  auto put32 = [&](uint32_t value) {
    for (int i = 0; i < 4; ++i)
      writer.payload.push_back(static_cast<uint8_t>(value >> (8 * i)));
  };
  uint32_t trailer = 4 + 4;
  put32(0);
  put32(static_cast<uint32_t>(writer.packs.size()));
  for (size_t i = 0; i < writer.packs.size(); ++i) {
    const auto &path = writer.packs[i];
    const uint32_t length = static_cast<uint32_t>(path.size() + 1);
    put32(static_cast<uint32_t>(i + 1));
    put32(length);
    writer.payload.insert(writer.payload.end(), path.begin(), path.end());
    writer.payload.push_back(0);
    trailer += 8 + length;
  }
  put32(trailer);
  std::ofstream out(writer.path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(writer.payload.data()),
            static_cast<std::streamsize>(writer.payload.size()));
}

std::optional<int32_t> FileState::handle(const VmObject *owner) const {
  const auto writer = writers_.find(owner);
  if (writer != writers_.end())
    return writer->second.handle;
  const auto file = files_.find(owner);
  if (file == files_.end())
    return std::nullopt;
  return file->second.handle;
}

namespace {
char searchFold(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
bool searchMatch(const std::string &pattern, const std::string &name) {
  if (pattern == "*.*") return true;
  // Win32 FindFirstFile: a trailing ".*" also matches names with no extension.
  if (pattern.size() > 2 && pattern.compare(pattern.size() - 2, 2, ".*") == 0 &&
      name.find('.') == std::string::npos &&
      searchMatch(pattern.substr(0, pattern.size() - 2), name))
    return true;
  size_t p = 0, n = 0, star = std::string::npos, retry = 0;
  while (n < name.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || searchFold(pattern[p]) == searchFold(name[n]))) { ++p; ++n; }
    else if (p < pattern.size() && pattern[p] == '*') { star = p++; retry = n; }
    else if (star != std::string::npos) { p = star + 1; n = ++retry; }
    else return false;
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}
bool searchWithin(const std::filesystem::path &root, const std::filesystem::path &path) {
  auto a = root.begin(), b = path.begin();
  for (; a != root.end() && b != path.end(); ++a, ++b) if (*a != *b) return false;
  return a == root.end();
}
}
bool FileState::findFirst(const std::string &pattern, int32_t flags, int32_t &handle,
                          std::optional<std::string> &result, std::string &error) {
  error.clear(); handle = -1; result.reset();
  if (pattern.empty() || pattern.size() >= 256 || pattern.front() == '/' || pattern.front() == '\\' ||
      pattern.find(':') != std::string::npos || pattern.find('\0') != std::string::npos) { error = "unsupported FindFile pattern"; return false; }
  std::string normalized = pattern; std::replace(normalized.begin(), normalized.end(), '\\', '/');
  const std::filesystem::path rel(normalized);
  for (const auto &part : rel) if (part == "." || part == ".." || part.empty()) { error = "unsupported FindFile pattern"; return false; }
  const std::string leaf = rel.filename().string();
  const auto base = *rel.begin() == "save" ? dataRoot_ : gameRoot_;
  if (base.empty()) { error = "FindFile game root unavailable: " + pattern; return false; }
  for (const auto &part : rel.parent_path()) if (part.string().find_first_of("*?") != std::string::npos) { error = "unsupported FindFile directory wildcard"; return false; }
  std::error_code ec; const auto root = std::filesystem::canonical(base, ec);
  if (ec) { error = "invalid FindFile root"; return false; }
  const auto dir = std::filesystem::weakly_canonical(root / rel.parent_path(), ec);
  if (ec || !searchWithin(root, dir)) { error = "FindFile path escapes root"; return false; }
  if (!std::filesystem::exists(dir, ec)) return !ec;
  Search search;
  std::filesystem::directory_iterator it(dir, ec), end;
  if (ec) { error = "FindFile directory error: " + ec.message(); return false; }
  for (; it != end; it.increment(ec)) {
    if (ec) { error = "FindFile iteration error: " + ec.message(); return false; }
    const auto name = it->path().filename().string();
    if (!searchMatch(leaf, name)) continue;
    if (std::any_of(name.begin(), name.end(), [](unsigned char c) { return c >= 128; })) {
      error = "unsupported FindFile non-ASCII name"; return false;
    }
    if (it->is_symlink(ec)) { error = "FindFile symlinks unsupported"; return false; }
    const bool directory = it->is_directory(ec);
    if (ec) { error = "FindFile entry error: " + ec.message(); return false; }
    search.entries.push_back({name, directory});
  }
  if (ec) { error = "FindFile iteration error: " + ec.message(); return false; }
  if (search.entries.empty()) return true;
  std::sort(search.entries.begin(), search.entries.end(), [](const SearchEntry &a, const SearchEntry &b) {
    return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(), [](char x, char y) { return searchFold(x) < searchFold(y); });
  });
  for (size_t i = 0; i < searches_.size(); ++i) if (!searches_[i]) {
    searches_[i] = std::move(search); handle = static_cast<int32_t>(i); result = findNext(handle, flags); return true;
  }
  return true;
}
std::optional<std::string> FileState::findNext(int32_t handle, int32_t flags) {
  if (handle < 0 || handle >= static_cast<int32_t>(searches_.size()) || !searches_[static_cast<size_t>(handle)]) return {};
  auto &search = *searches_[static_cast<size_t>(handle)];
  while (search.next < search.entries.size()) {
    const auto &entry = search.entries[search.next++];
    if (entry.directory ? flags != 1 : flags < 2) return entry.name;
  }
  return {};
}
void FileState::findClose(int32_t handle) {
  if (handle >= 0 && handle < static_cast<int32_t>(searches_.size())) searches_[static_cast<size_t>(handle)].reset();
}
