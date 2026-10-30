#include "part_config.hpp"
#include "world_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <limits>

bool parsePartConfig(std::string_view text, std::vector<PartConfigLine>& lines,
                     std::string& error) {
  lines.clear();
  if (text.find('\0') != std::string_view::npos) {
    error = "part configuration contains a zero byte";
    return false;
  }
  size_t offset = 0;
  while (offset < text.size()) {
    const size_t end = text.find('\n', offset);
    const size_t length = (end == std::string_view::npos ? text.size() : end) - offset;
    // 004b6520 copies each line into a 256-byte buffer before dispatch.
    std::string line(text.substr(offset, std::min<size_t>(length, 255)));
    offset = end == std::string_view::npos ? text.size() : end + 1;
    const auto comment = line.find_first_of(";#/");
    if (comment != std::string::npos) line.resize(comment);
    const auto first = line.find_first_not_of(" \r\n\t");
    if (first == std::string::npos) continue;
    const auto lastKey = line.find_first_of(" \r\n\t", first);
    std::string key = line.substr(first, lastKey - first);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
      return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    });
    if (key == "eof") break;
    std::string arguments;
    if (lastKey != std::string::npos) {
      const auto start = line.find_first_not_of(" \r\n\t", lastKey);
      if (start != std::string::npos) arguments = line.substr(start);
    }
    lines.push_back({std::move(key), std::move(arguments)});
  }
  error.clear();
  return true;
}

bool parsePartSlot(const PartConfigLine& line, PartSlotDirective& slot) {
  if (line.key != "slot") return false;
  PartSlotDirective parsed{};
  int consumed = 0;
  const int fields = std::sscanf(line.arguments.c_str(), "%f%f%f%f%f%f%n",
      &parsed.position[0], &parsed.position[1], &parsed.position[2],
      &parsed.orientation[0], &parsed.orientation[1], &parsed.orientation[2], &consumed);
  if (fields != 6) return false;
  const char* number = line.arguments.c_str() + consumed;
  while (*number == ' ' || *number == '\t' || *number == '\r' || *number == '\n') ++number;
  const int base = number[0] == '0' && (number[1] == 'x' || number[1] == 'X') ? 16 : 10;
  char* end = nullptr;
  errno = 0;
  const long value = std::strtol(number, &end, base);
  if (errno || end == number || value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max()) return false;
  while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
  if (*end) return false;
  parsed.id = static_cast<int>(value);
  for (float v : parsed.position) if (!std::isfinite(v)) return false;
  for (float v : parsed.orientation) if (!std::isfinite(v)) return false;
  slot = parsed;
  return true;
}

bool makePartSlotState(const PartSlotDirective& directive, PartSlotState& slot) {
  for (float value : directive.position) if (!std::isfinite(value)) return false;
  for (float value : directive.orientation) if (!std::isfinite(value)) return false;
  const double y = directive.orientation[0], p = directive.orientation[1], r = directive.orientation[2];
  const double sy = std::sin(y), sp = std::sin(p);
  const float cy = static_cast<float>(std::cos(y)), cp = static_cast<float>(std::cos(p));
  const float sr = static_cast<float>(std::sin(r)), cr = static_cast<float>(std::cos(r));
  const float rs = static_cast<float>(static_cast<double>(sr) * sp);
  const float rc = static_cast<float>(static_cast<double>(cr) * sp);
  PartSlotState result{};
  result.position = {directive.position[0], directive.position[1], directive.position[2]};
  result.rotation = {
      cr * cy + static_cast<float>(static_cast<double>(rs) * sy),
      static_cast<float>(static_cast<double>(rc) * sy) - sr * cy,
      static_cast<float>(static_cast<double>(cp) * sy),
      static_cast<float>(static_cast<double>(sr) * cp), static_cast<float>(static_cast<double>(cr) * cp),
      static_cast<float>(-sp),
      rs * cy - static_cast<float>(static_cast<double>(cr) * sy),
      static_cast<float>(static_cast<double>(sr) * sy) + rc * cy,
      static_cast<float>(static_cast<double>(cp) * cy)};
  result.id = directive.id;
  slot = result;
  return true;
}

bool parsePartAttach(const PartConfigLine& line, PartAttachDirective& attach) {
  if (line.key != "attach") return false;
  const char* input = line.arguments.c_str();
  while (*input == ' ' || *input == '\t' || *input == '\r' || *input == '\n') ++input;
  if (*input == '-') return false;
  const int base = input[0] == '0' && (input[1] == 'x' || input[1] == 'X') ? 16 : 10;
  errno = 0;
  char* end = nullptr;
  const unsigned long resource = std::strtoul(input, &end, base);
  if (errno || end == input || resource > std::numeric_limits<unsigned int>::max()) return false;
  input = end;
  while (*input == ' ' || *input == '\t' || *input == '\r' || *input == '\n') ++input;
  const int slotBase = input[0] == '0' && (input[1] == 'x' || input[1] == 'X') ? 16 : 10;
  errno = 0;
  const long slot = std::strtol(input, &end, slotBase);
  if (errno || end == input || slot < std::numeric_limits<int>::min() || slot > std::numeric_limits<int>::max()) return false;
  while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
  if (*end) return false;
  attach = {static_cast<unsigned int>(resource), static_cast<int>(slot)};
  return true;
}

bool applyPartSlotLine(const PartConfigLine& line, uint32_t initializationCount,
                       const ResourceIdMapping& mapping, std::vector<PartSlotRecord>& slots,
                       std::string& error) {
  if (line.key != "slot" && line.key != "attach") { error = "unsupported part directive"; return false; }
  if (initializationCount != 0) { error.clear(); return true; }
  if (line.key == "slot") {
    PartSlotDirective directive{};
    PartSlotRecord record{};
    if (!parsePartSlot(line, directive) || !makePartSlotState(directive, record.state)) {
      error = "unsupported part slot"; return false;
    }
    slots.push_back(std::move(record));
  } else if (!slots.empty()) {
    PartAttachDirective directive{};
    if (!parsePartAttach(line, directive)) { error = "unsupported part attachment"; return false; }
    slots.back().attachments.push_back({remapResourceId(directive.resourceId, mapping), directive.targetSlotId});
  }
  error.clear(); return true;
}

bool applyPartRenderLine(const PartConfigLine& line, uint32_t initializationCount,
                         const ResourceIdMapping& mapping,
                         std::vector<PartRenderRecord>& renders, std::string& error) {
  if (line.key != "render" && line.key != "flags") { error = "unsupported part directive"; return false; }
  if (line.key == "flags") {
    if (initializationCount || renders.empty()) { error.clear(); return true; }
    const char* input = line.arguments.c_str();
    while (*input == ' ' || *input == '\t') ++input;
    const int base = input[0] == '0' && (input[1] == 'x' || input[1] == 'X') ? 16 : 10;
    char* end = nullptr; errno = 0;
    const long flags = std::strtol(input, &end, base);
    if (errno || end == input || flags < 0 || flags > std::numeric_limits<int>::max()) {
      error = "unsupported part flags"; return false;
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end) { error = "unsupported part flags"; return false; }
    renders.back().flags |= static_cast<uint32_t>(flags) & 0x1ffu;
    error.clear(); return true;
  }
  if (renders.size() >= 32) { error = "part render limit reached"; return false; }
  const char* input = line.arguments.c_str();
  while (*input == ' ' || *input == '\t') ++input;
  if (*input == '-') { error = "unsupported part render resource"; return false; }
  const int base = input[0] == '0' && (input[1] == 'x' || input[1] == 'X') ? 16 : 10;
  char* end = nullptr; errno = 0;
  const unsigned long id = std::strtoul(input, &end, base);
  if (errno || end == input || id > UINT32_MAX) { error = "unsupported part render resource"; return false; }
  while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
  PartSlotDirective transform{};
  if (*end) {
    int consumed = 0;
    if (std::sscanf(end, "%f%f%f%f%f%f%n", &transform.position[0], &transform.position[1],
                    &transform.position[2], &transform.orientation[0], &transform.orientation[1],
                    &transform.orientation[2], &consumed) != 6) { error = "unsupported part render transform"; return false; }
    end += consumed;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end) { error = "unsupported part render transform"; return false; }
  }
  PartRenderRecord record{};
  if (!makePartSlotState(transform, record.transform)) { error = "invalid part render transform"; return false; }
  record.resourceId = remapResourceId(static_cast<uint32_t>(id), mapping);
  renders.push_back(std::move(record));
  error.clear(); return true;
}

bool initializePartPrototype(ScriptNode& source, const std::vector<PartConfigLine>& lines,
                             const ResourceIdMapping& mapping, LinkOwner* partResource,
                             LinkOwner* decalsResource, LinkOwner* soundResource,
                             std::string& error) {
  if (source.part || !partResource || !decalsResource || !soundResource ||
      mapping.currentPack != static_cast<uint16_t>(source.resourceId >> 16)) {
    error = "part prototype resources unavailable or already initialized"; return false;
  }
  std::vector<PartSlotRecord> slots;
  for (const auto& line : lines)
    if (!applyPartSlotLine(line, 0, mapping, slots, error)) return false;
  auto part = std::make_unique<PartPrototype>();
  part->slots = std::move(slots);
  setLinkOwner(part->source, &source);
  setLinkOwner(part->partResource, partResource);
  setLinkOwner(part->decalsResource, decalsResource);
  setLinkOwner(part->soundResource, soundResource);
  source.part = std::move(part);
  error.clear(); return true;
}
