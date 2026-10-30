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

void FileState::close(const VmObject *owner) { files_.erase(owner); }

std::optional<int32_t> FileState::handle(const VmObject *owner) const {
  const auto file = files_.find(owner);
  if (file == files_.end())
    return std::nullopt;
  return file->second.handle;
}

namespace {
char searchFold(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
bool searchMatch(const std::string &pattern, const std::string &name) {
  if (pattern == "*.*") return true;
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
  if (leaf.find('?') != std::string::npos ||
      (leaf.find('*') != std::string::npos &&
       (leaf != "*.*" && (leaf[0] != '*' || leaf.find('*', 1) != std::string::npos ||
        (leaf.size() > 1 && leaf[1] != '.'))))) {
    error = "unsupported FindFile wildcard"; return false;
  }
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
