#include "ground_files.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

namespace {
enum class FileStatus { Found, Missing, Error };

FileStatus readFile(const std::filesystem::path& root, std::string name,
                    std::vector<uint8_t>& bytes, std::string& error) {
  if (name.empty()) return FileStatus::Missing;
  if (name.front() == '/' || name.front() == '\\' || name.find(':') != std::string::npos ||
      name.find('\0') != std::string::npos) {
    error = "ground file: unsafe path";
    return FileStatus::Error;
  }
  std::replace(name.begin(), name.end(), '\\', '/');
  const std::filesystem::path relative(name);
  for (const auto& part : relative) {
    if (part == "." || part == ".." || part.empty()) {
      error = "ground file: unsafe path";
      return FileStatus::Error;
    }
  }
  std::error_code ec;
  const auto base = std::filesystem::canonical(root, ec);
  if (ec) { error = "ground file: invalid resource root"; return FileStatus::Error; }
  const auto path = std::filesystem::canonical(base / relative, ec);
  if (ec) {
    if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory)
      return FileStatus::Missing;
    error = "ground file: cannot resolve " + name;
    return FileStatus::Error;
  }
  auto b = base.begin(), p = path.begin();
  for (; b != base.end() && p != path.end(); ++b, ++p) {
    if (*b != *p) { error = "ground file: path escapes resource root"; return FileStatus::Error; }
  }
  if (b != base.end() || !std::filesystem::is_regular_file(path, ec) || ec) {
    error = "ground file: invalid file " + name;
    return FileStatus::Error;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) { error = "ground file: cannot open " + name; return FileStatus::Error; }
  bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  if (input.bad()) { error = "ground file: cannot read " + name; return FileStatus::Error; }
  return FileStatus::Found;
}
}

bool prepareGroundMaterialFile(const std::filesystem::path& root, const GroundParameters& parameters,
                               float globalFriction, std::optional<GroundConfig>& out,
                               std::string& error) {
  std::vector<uint8_t> bytes;
  const auto status = readFile(root, parameters.materialPath, bytes, error);
  if (status == FileStatus::Error) return false;
  std::optional<GroundConfig> result;
  if (status == FileStatus::Found) {
    GroundConfig materials;
    const std::string text(bytes.begin(), bytes.end());
    if (!parseGroundConfig(text, globalFriction, materials, error)) return false;
    result = std::move(materials);
  }
  out = std::move(result);
  error.clear();
  return true;
}

bool prepareGroundRoadFile(const std::filesystem::path& root, const GroundParameters& parameters,
                           std::optional<RoadNetwork>& out, std::string& error) {
  std::vector<uint8_t> bytes;
  const auto status = readFile(root, parameters.roadPath, bytes, error);
  if (status == FileStatus::Error) return false;
  std::optional<RoadNetwork> result;
  if (status == FileStatus::Found) {
    RoadNetwork roads;
    if (!readRoadNetwork(bytes, roads, error)) return false;
    result = std::move(roads);
  }
  out = std::move(result);
  error.clear();
  return true;
}

bool prepareGroundFiles(const std::filesystem::path& root, const GroundParameters& parameters,
                        float globalFriction, GroundFiles& out, std::string& error) {
  GroundFiles result;
  if (!prepareGroundMaterialFile(root, parameters, globalFriction, result.materials, error) ||
      !prepareGroundRoadFile(root, parameters, result.roads, error)) return false;
  out = std::move(result);
  error.clear();
  return true;
}
