#include "pack_registry.hpp"

#include "resource_id.hpp"
#include "run_log.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace {
bool safeName(const std::string &name) {
  if (name.empty() || name.front() == '/' || name.front() == '\\' ||
      name.find(':') != std::string::npos ||
      name.find('\0') != std::string::npos)
    return false;
  std::string normalized = name;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  const std::filesystem::path path(normalized);
  for (const auto &part : path)
    if (part == "." || part == ".." || part.empty())
      return false;
  return true;
}

bool equalName(const std::string &a, const std::string &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    auto lower = [](char c) {
      return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
    };
    if (lower(a[i]) != lower(b[i]))
      return false;
  }
  return true;
}
} // namespace

PackRegistry::PackRegistry(std::filesystem::path root)
    : root_(std::move(root)) {}

bool PackRegistry::registerName(const std::string &name, uint16_t &index,
                                std::string &error) {
  if (!safeName(name)) {
    error = "unsafe pack name";
    return false;
  }
  for (size_t i = 0; i < slots_.size(); ++i) {
    if (equalName(slots_[i]->name, name)) {
      index = static_cast<uint16_t>(i);
      return true;
    }
  }
  if (slots_.size() >= 0xffff) {
    error = "pack registry full";
    return false;
  }
  index = static_cast<uint16_t>(slots_.size());
  auto slot = std::make_unique<Slot>();
  slot->name = name;
  slots_.push_back(std::move(slot));
  runlog::event(runlog::EventKind::Note, "pack slot 0x%04x = %s", index,
                name.c_str());
  return true;
}

PackRegistry::LoadStatus PackRegistry::loadSlot(uint16_t index,
                                                std::string &error) {
  Slot &slot = *slots_[index];
  if (slot.loaded)
    return LoadStatus::Ok;
  std::error_code ec;
  const auto root = std::filesystem::canonical(root_, ec);
  if (ec) {
    error = "invalid resource root for pack " + slot.name;
    return LoadStatus::Failed;
  }
  std::string name = slot.name;
  std::replace(name.begin(), name.end(), '\\', '/');
  const auto path = std::filesystem::canonical(root / name, ec);
  if (ec) {
    error = "pack file not found: " + slot.name;
    return LoadStatus::Missing;
  }
  auto base = root.begin(), part = path.begin();
  for (; base != root.end() && part != path.end(); ++base, ++part)
    if (*base != *part) {
      error = "pack escapes resource root: " + slot.name;
      return LoadStatus::Failed;
    }
  if (base != root.end() || !std::filesystem::is_regular_file(path, ec) || ec) {
    error = "invalid pack file: " + slot.name;
    return LoadStatus::Failed;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "cannot open pack file: " + slot.name;
    return LoadStatus::Failed;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
  if (input.bad()) {
    error = "cannot read pack file: " + slot.name;
    return LoadStatus::Failed;
  }
  auto parsed = rpkParse(bytes.data(), bytes.size());
  if (!parsed.ok) {
    error = "invalid pack " + slot.name + ": " + parsed.error;
    return LoadStatus::Failed;
  }
  std::vector<uint16_t> dependencies;
  for (const auto &dep : parsed.pack.dependencies) {
    uint16_t mapped;
    if (!registerName(dep.path, mapped, error))
      return LoadStatus::Failed;
    dependencies.push_back(mapped);
  }
  slot.bytes = std::move(bytes);
  slot.pack = std::move(parsed.pack);
  slot.dependencies = std::move(dependencies);
  slot.loaded = true;
  return LoadStatus::Ok;
}

bool PackRegistry::registerPackName(const std::string &packName, uint16_t &slot,
                                    std::string &error) {
  error.clear();
  return registerName(packName, slot, error);
}

PackViewStatus PackRegistry::view(uint16_t index, PackView &result,
                                  std::string &error) {
  result = {};
  error.clear();
  if (index >= slots_.size() || index == 0xffff) {
    error = "invalid pack slot";
    return PackViewStatus::Invalid;
  }
  const auto status = loadSlot(index, error);
  if (status != LoadStatus::Ok)
    return status == LoadStatus::Missing ? PackViewStatus::FileMissing
                                         : PackViewStatus::Invalid;
  const Slot &slot = *slots_[index];
  result.slot = index;
  result.name = slot.name;
  result.pack = &slot.pack;
  result.bytes = &slot.bytes;
  result.dependencies = slot.dependencies;
  return PackViewStatus::Ok;
}

bool PackRegistry::resolveLiteral(const std::string &packName,
                                  uint32_t serializedId, uint32_t &result,
                                  std::string &error) {
  result = 0;
  error.clear();
  uint16_t index;
  if (!registerName(packName, index, error) || !load(index, error))
    return false;
  const Slot &slot = *slots_[index];
  const ResourceIdMapping mapping{index, static_cast<uint32_t>(slots_.size()),
                                  slot.dependencies};
  result = remapResourceId(serializedId, mapping);
  // 00544590 returns zero for a zero local ID or an invalid dependency;
  // 00538440 passes that value to the script rather than reporting an error.
  return true;
}

bool PackRegistry::lookup(uint32_t runtimeId, PackResource &result,
                          std::string &error) {
  result = {};
  error.clear();
  const uint32_t index = runtimeId >> 16;
  const uint32_t local = runtimeId & 0xffff;
  if (!local || index >= slots_.size() || index == 0xffff) {
    error = "invalid runtime resource ID";
    return false;
  }
  if (!load(static_cast<uint16_t>(index), error))
    return false;
  const Slot &slot = *slots_[index];
  const RpkEntry *entry = nullptr;
  for (const auto &candidate : slot.pack.entries) {
    if ((candidate.typeId & 0xffff) == local && (candidate.typeId >> 16) == 0) {
      if (entry) {
        error = "duplicate resource ID";
        return false;
      }
      entry = &candidate;
    }
  }
  if (!entry) {
    error = "resource not found in pack";
    return false;
  }
  const uint64_t end =
      static_cast<uint64_t>(entry->payloadOffset) + entry->payloadSize;
  const size_t header = 16 + slot.pack.dependencies.size() * 64;
  const uint32_t tableSize =
      static_cast<uint32_t>(slot.bytes[header]) |
      (static_cast<uint32_t>(slot.bytes[header + 1]) << 8) |
      (static_cast<uint32_t>(slot.bytes[header + 2]) << 16) |
      (static_cast<uint32_t>(slot.bytes[header + 3]) << 24);
  const uint64_t tableEnd = static_cast<uint64_t>(header) + 16 + tableSize;
  if (end > slot.bytes.size() ||
      (entry->payloadSize && entry->payloadOffset < tableEnd)) {
    error = "invalid resource payload";
    return false;
  }
  result = {runtimeId, *entry, &slot.bytes};
  return true;
}
