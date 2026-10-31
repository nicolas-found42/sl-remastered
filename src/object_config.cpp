#include "object_config.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace object_config {
namespace {

// Stock 004b6340 cuts lines at these characters before dispatch.
bool isCommentChar(char c) { return c == ';' || c == '#' || c == '/'; }

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// sscanf("%f")-style conversion of one token: parses the longest valid prefix
// (so "1.5x" converts 1.5 and leaves "x"), returns false when no conversion.
bool scanFloat(const char *&cursor, float &out) {
  while (isSpace(*cursor))
    ++cursor;
  if (*cursor == '\0')
    return false;
  errno = 0;
  char *end = nullptr;
  const float value = std::strtof(cursor, &end);
  if (end == cursor)
    return false;
  if (errno == ERANGE && (value == HUGE_VALF || value == -HUGE_VALF))
    return false;
  out = value;
  cursor = end;
  return true;
}

// sscanf("%s")-style token.
bool scanWord(const char *&cursor, std::string &out) {
  while (isSpace(*cursor))
    ++cursor;
  if (*cursor == '\0')
    return false;
  const char *begin = cursor;
  while (*cursor != '\0' && !isSpace(*cursor))
    ++cursor;
  out.assign(begin, cursor);
  return true;
}

// sscanf("%d")-style decimal, longest valid prefix.
bool scanInt(const char *&cursor, long &out) {
  while (isSpace(*cursor))
    ++cursor;
  if (*cursor == '\0')
    return false;
  errno = 0;
  char *end = nullptr;
  const long value = std::strtol(cursor, &end, 10);
  if (end == cursor || errno == ERANGE)
    return false;
  out = value;
  cursor = end;
  return true;
}

// 00553890 "%#%d": decimal or 0x-prefixed hex, with 32-bit wrap.
// A failed conversion returns zero bytes, leaving both cursor and value intact.
bool scanStockInteger(const char *&cursor, uint32_t &out) {
  const char *p = cursor;
  while (isSpace(*p))
    ++p;
  const bool hex = p[0] == '0' && (p[1] == 'x' || p[1] == 'X');
  bool negative = false;
  if (hex)
    p += 2;
  else if (*p == '-') {
    negative = true;
    ++p;
  }
  auto digit = [hex](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (hex && c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (hex && c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  if (digit(*p) < 0)
    return false;
  uint32_t value = 0;
  for (int d = digit(*p); d >= 0; d = digit(*p)) {
    value = value * (hex ? 16u : 10u) + static_cast<uint32_t>(d);
    ++p;
  }
  out = negative ? 0u - value : value;
  cursor = p;
  return true;
}

// sscanf("%x")-style hex (optional 0x prefix like stock).
bool scanHex(const char *&cursor, unsigned long &out) {
  while (isSpace(*cursor))
    ++cursor;
  if (*cursor == '\0')
    return false;
  errno = 0;
  char *end = nullptr;
  unsigned long value = std::strtoul(cursor, &end, 16);
  if (end == cursor || errno == ERANGE) {
    // strtoul with base 16 rejects a bare "0x" differently than sscanf %x.
    if (!(cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')))
      return false;
    cursor += 2;
    value = std::strtoul(cursor, &end, 16);
    if (end == cursor)
      return false;
  }
  out = value;
  cursor = end;
  return true;
}

bool equalsIgnoreCase(const std::string &a, const char *b) {
  size_t i = 0;
  for (; i < a.size() && b[i] != '\0'; ++i) {
    unsigned char ca = static_cast<unsigned char>(a[i]);
    unsigned char cb = static_cast<unsigned char>(b[i]);
    if (ca >= 'A' && ca <= 'Z')
      ca = static_cast<unsigned char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z')
      cb = static_cast<unsigned char>(cb - 'A' + 'a');
    if (ca != cb)
      return false;
  }
  return i == a.size() && b[i] == '\0';
}

// Stock __ftol: converts through the FPU with round-to-nearest semantics.
int toStockInt(float value) { return static_cast<int>(std::lrintf(value)); }

// Stock 00464500/00464900 shape keyword dispatch. Numeric tokens take the
// resource-registry path; unknown strings build a mesh with {1, 1, 1} params
// (stock overrides the parsed trailing floats in that branch).
ShapeKind resolveShapeKind(const std::string &token, const char *,
                           ShapeDirective &shape) {
  uint32_t numeric = 0;
  const char *probe = token.c_str();
  if (scanStockInteger(probe, numeric)) {
    shape.kind = ShapeKind::ReuseRegistry;
    shape.reuseExisting = true;
    shape.meshId = numeric;
    return shape.kind;
  }
  if (equalsIgnoreCase(token, "ball") || equalsIgnoreCase(token, "sphere"))
    shape.kind = ShapeKind::Sphere;
  else if (equalsIgnoreCase(token, "box"))
    shape.kind = ShapeKind::Box;
  else if (equalsIgnoreCase(token, "rodx"))
    shape.kind = ShapeKind::CylinderX;
  else if (equalsIgnoreCase(token, "rody"))
    shape.kind = ShapeKind::CylinderY;
  else if (equalsIgnoreCase(token, "rodz"))
    shape.kind = ShapeKind::CylinderZ;
  else {
    shape.kind = ShapeKind::Mesh;
    shape.name = token; // Unnamed mesh path: stock passes the type string on.
    shape.params = {1.0f, 1.0f, 1.0f};
  }
  return shape.kind;
}

// Shared operand tail of body/trigger: "%f %f %f" position, "%f %f %f"
// orientation, shape type token, then up to three floats. Like the stock
// sscanf, a failed conversion stops the whole scan: remaining fields keep
// their defaults (the shape token defaults to "6" for body / "" for trigger,
// data 0063c848/0063c84c). Returns false when the first float did not convert.
bool parseShapeOperands(const std::string &arguments, bool isTrigger,
                        ShapeDirective &shape) {
  const char *cursor = arguments.c_str();
  shape = ShapeDirective{};
  if (isTrigger)
    shape.mass = 0.001f; // 0x3a83126f in 00464900.
  float *fields[6] = {&shape.position.x,    &shape.position.y,
                      &shape.position.z,    &shape.orientation.x,
                      &shape.orientation.y, &shape.orientation.z};
  int converted = 0;
  for (float *field : fields) {
    if (!scanFloat(cursor, *field))
      break;
    ++converted;
  }
  // Body carries a seventh float (mass) before the shape token; trigger does
  // not and fixes mass to 0.001f.
  if (!isTrigger && scanFloat(cursor, shape.mass))
    ++converted;
  const bool complete = converted == (isTrigger ? 6 : 7);
  // sscanf only reaches %s once every earlier field converted; the stock
  // stack default ("6" for body, "" for trigger) survives a failed %s.
  std::string token = isTrigger ? "" : "6";
  if (complete)
    scanWord(cursor, token);
  if (converted == 0 && token.empty())
    return false;
  resolveShapeKind(token, cursor, shape);
  if (shape.kind != ShapeKind::Mesh) {
    for (float &param : shape.params) {
      if (!scanFloat(cursor, param))
        break;
    }
    // The unnamed-string mesh branch overrides params with {1, 1, 1}, which
    // resolveShapeKind already applied; named primitives keep parsed values.
  }
  for (float v : {shape.position.x, shape.position.y, shape.position.z,
                  shape.orientation.x, shape.orientation.y, shape.orientation.z,
                  shape.mass})
    if (!std::isfinite(v))
      return false;
  return true;
}

// Applies a modifier directive to the newest slot. Mirrors the stock +0x120
// init gate shared by 00464ff0/00465030/00465070/00465520/00465590/004656d0/
// 004657a0.
bool applySlotModifier(const ObjectConfigLine &line,
                       uint32_t initializationCount,
                       const ResourceIdMapping &mapping, ObjectConfig &config,
                       std::string &error) {
  if (initializationCount != 0 || config.slots.empty())
    return true;
  ObjectSlot &slot = config.slots.back();
  const char *cursor = line.arguments.c_str();
  if (line.key == "nocollision") {
    slot.modifiers.nocollision = true;
    slot.shape.flags |= 0x8u;
    return true;
  }
  if (line.key == "buoy") {
    slot.modifiers.buoy = true;
    slot.shape.flags |= 0x800u;
    return true;
  }
  if (line.key == "friction") {
    float value = 0.0f;
    if (!scanFloat(cursor, value)) {
      error = "object directive 'friction' expects a float operand";
      return false;
    }
    slot.modifiers.hasFriction = true;
    slot.modifiers.friction = value;
    return true;
  }
  if (line.key == "flags") {
    long value = 0;
    if (!scanInt(cursor, value)) {
      error = "object directive 'flags' expects a decimal operand";
      return false;
    }
    slot.modifiers.hasFlags = true;
    slot.modifiers.flags |= static_cast<uint32_t>(value) & 0x1ffu;
    slot.shape.flags |= static_cast<uint32_t>(value) & 0x1ffu;
    return true;
  }
  if (line.key == "lod") {
    long nearValue = 0, farValue = 0;
    if (!scanInt(cursor, nearValue) || !scanInt(cursor, farValue)) {
      error = "object directive 'lod' expects two decimal operands";
      return false;
    }
    slot.modifiers.hasLod = true;
    slot.modifiers.lodNear = static_cast<int>(nearValue & 0xf);
    slot.modifiers.lodFar = static_cast<int>(farValue & 0xf);
    return true;
  }
  if (line.key == "color") {
    // Fixed four raw ARGB words; stock callback unidentified (see header).
    for (auto &value : slot.modifiers.stateColors) {
      uint32_t raw = 0;
      if (scanStockInteger(cursor, raw))
        value = raw;
    }
    slot.modifiers.hasStateColors = true;
    return true;
  }
  if (line.key == "colors") {
    uint32_t rawCount = 0;
    scanStockInteger(cursor, rawCount);
    const int32_t count = static_cast<int32_t>(rawCount);
    if (count < 0) {
      error = "object directive 'colors' has a negative count";
      return false;
    }
    slot.modifiers.hasColors = true;
    slot.modifiers.colors.clear();
    slot.modifiers.colors.reserve(static_cast<size_t>(count));
    // Each conversion writes a different, uninitialized 4-byte allocation
    // element. Unlike rendertypes, failed scans do not reuse a prior value.
    for (int32_t i = 0; i < count; ++i) {
      uint32_t value = 0;
      if (scanStockInteger(cursor, value))
        slot.modifiers.colors.emplace_back(value);
      else
        slot.modifiers.colors.emplace_back(std::nullopt);
    }
    return true;
  }
  if (line.key == "rendertypes") {
    uint32_t rawCount = 0;
    scanStockInteger(cursor, rawCount);
    const int32_t count = static_cast<int32_t>(rawCount);
    if (count < 0) {
      error = "object directive 'rendertypes' has a negative count";
      return false;
    }
    slot.modifiers.hasRendertypes = true;
    slot.modifiers.rendertypes.clear();
    slot.modifiers.rendertypes.reserve(static_cast<size_t>(count));
    // 004657a0 initializes its operand once, outside the loop. If scanf
    // fails (including at the 255-byte line boundary), it returns zero and
    // leaves the previous operand intact for this and subsequent links.
    uint32_t value = 0;
    for (int32_t i = 0; i < count; ++i) {
      scanStockInteger(cursor, value);
      slot.modifiers.rendertypes.push_back(remapResourceId(value, mapping));
    }
    return true;
  }
  error = "unsupported object slot modifier '" + line.key + "'";
  return false;
}

} // namespace

bool parseObjectConfig(std::string_view text,
                       std::vector<ObjectConfigLine> &lines,
                       std::string &error) {
  lines.clear();
  size_t offset = 0;
  while (offset < text.size()) {
    size_t end = text.find('\n', offset);
    if (end == std::string_view::npos)
      end = text.size();
    std::string_view raw = text.substr(offset, end - offset);
    offset = end + 1;
    for (size_t i = 0; i < raw.size(); ++i) {
      if (isCommentChar(raw[i])) {
        raw = raw.substr(0, i);
        break;
      }
    }
    // Stock copies each line through a 256-byte buffer (00551120), so longer
    // lines are truncated, not rejected.
    if (raw.size() > 255)
      raw = raw.substr(0, 255);
    size_t begin = 0;
    while (begin < raw.size() && isSpace(raw[begin]))
      ++begin;
    size_t stop = begin;
    while (stop < raw.size() && !isSpace(raw[stop]))
      ++stop;
    if (begin == stop)
      continue;
    ObjectConfigLine line;
    line.key.assign(raw.substr(begin, stop - begin));
    for (char &c : line.key)
      if (c >= 'A' && c <= 'Z')
        c = static_cast<char>(c - 'A' + 'a');
    while (stop < raw.size() && isSpace(raw[stop]))
      ++stop;
    line.arguments.assign(raw.substr(stop));
    if (line.key == "eof")
      break;
    lines.push_back(std::move(line));
  }
  error.clear();
  return true;
}

bool parseInstanceHeader(std::string_view text, InstanceHeader &header,
                         std::string &error) {
  // 00465bd0 header: "%f,%f,%f,%f,%f,%f,%d" (comma separated).
  std::string buffer(text);
  const char *cursor = buffer.c_str();
  float values[6];
  for (size_t i = 0; i < 6; ++i) {
    if (!scanFloat(cursor, values[i])) {
      error = "object instance header expects six comma-separated floats";
      return false;
    }
    if (i != 5) {
      if (*cursor != ',') {
        error = "object instance header expects comma separators";
        return false;
      }
      ++cursor;
    }
  }
  int32_t parsedId = -1;
  if (*cursor == ',') {
    ++cursor;
    long id = 0;
    if (!scanInt(cursor, id)) {
      error = "object instance header has an invalid decimal id";
      return false;
    }
    parsedId = static_cast<int32_t>(static_cast<uint32_t>(id));
  }
  for (float value : values)
    if (!std::isfinite(value)) {
      error = "object instance header has a non-finite float";
      return false;
    }
  if (parsedId < 0) {
    // 00465c58 sums z+x+y in x87 and __ftol truncates to int64.
    const long double sum =
        static_cast<long double>(values[2]) + values[0] + values[1];
    const long double limit = std::ldexp(1.0L, 63);
    const int64_t converted =
        sum >= -limit && sum < limit ? static_cast<int64_t>(sum) : INT64_MIN;
    parsedId =
        static_cast<int32_t>(static_cast<uint32_t>(converted) & 0x7fffffffu);
  }
  header.position = {values[0], values[1], values[2]};
  header.orientation = {values[3], values[4], values[5]};
  header.id = parsedId;
  error.clear();
  return true;
}

bool applyObjectConfigLine(const ObjectConfigLine &line,
                           uint32_t initializationCount,
                           const ResourceIdMapping &mapping,
                           uint32_t systemRpkId, ObjectConfig &config,
                           std::string &error) {
  const std::string &key = line.key;
  const char *cursor = line.arguments.c_str();

  auto addSlot = [&](SlotKind kind) -> ObjectSlot * {
    if (config.slots.size() >= 16) {
      error = "object config exceeds the stock limit of 16 slots";
      return nullptr;
    }
    if ((kind == SlotKind::Body || kind == SlotKind::Trigger) &&
        config.defaultSlotIndex < 0)
      config.defaultSlotIndex = static_cast<int>(config.slots.size());
    config.slots.emplace_back();
    config.slots.back().kind = kind;
    return &config.slots.back();
  };
  auto recountRenderSlots = [&]() {
    int count = 0;
    for (const ObjectSlot &slot : config.slots)
      if (slot.kind == SlotKind::Body || slot.kind == SlotKind::Trigger)
        ++count;
    config.multipleRenderSlots = count > 1;
  };

  if (key == "body" || key == "trigger") {
    const bool isTrigger = key == "trigger";
    ObjectSlot *slot = addSlot(isTrigger ? SlotKind::Trigger : SlotKind::Body);
    if (!slot)
      return false;
    if (!parseShapeOperands(line.arguments, isTrigger, slot->shape)) {
      error = "object directive '" + key +
              "' expects six floats, a shape type, and up to three parameters";
      config.slots.pop_back();
      if (config.defaultSlotIndex == static_cast<int>(config.slots.size()))
        config.defaultSlotIndex = -1;
      return false;
    }
    if (slot->shape.kind == ShapeKind::ReuseRegistry)
      slot->shape.meshId = remapResourceId(slot->shape.meshId, mapping);
    recountRenderSlots();
    return true;
  }

  if (key == "render" || key == "light") {
    ObjectSlot *slot = addSlot(SlotKind::Render);
    if (!slot)
      return false;
    unsigned long id = 0;
    if (!scanHex(cursor, id)) {
      error = "object directive '" + key + "' expects a hex resource id";
      config.slots.pop_back();
      if (config.defaultSlotIndex == static_cast<int>(config.slots.size()))
        config.defaultSlotIndex = -1;
      return false;
    }
    // Stock links the remapped id as both type 0xd and 0xe in 00464d20.
    slot->renderId =
        remapResourceId(static_cast<uint32_t>(id & 0xffffffffu), mapping);
    slot->lightKeyword = key == "light";
    for (float *field :
         {&slot->position.x, &slot->position.y, &slot->position.z,
          &slot->orientation.x, &slot->orientation.y, &slot->orientation.z})
      if (!scanFloat(cursor, *field))
        break;
    return true;
  }

  if (key == "part") {
    ObjectSlot *slot = addSlot(SlotKind::Part);
    if (!slot)
      return false;
    for (float *field :
         {&slot->position.x, &slot->position.y, &slot->position.z,
          &slot->orientation.x, &slot->orientation.y, &slot->orientation.z})
      if (!scanFloat(cursor, *field))
        break;
    unsigned long id = 0;
    if (!scanHex(cursor, id)) {
      error =
          "object directive 'part' expects six floats and a hex resource id";
      config.slots.pop_back();
      if (config.defaultSlotIndex == static_cast<int>(config.slots.size()))
        config.defaultSlotIndex = -1;
      return false;
    }
    slot->resourceId =
        remapResourceId(static_cast<uint32_t>(id & 0xffffffffu), mapping);
    return true;
  }

  if (key == "bounds") {
    // Not a stock callback; shipped configs may omit the decimal flag.
    float *fields[6] = {&config.bounds.min.x, &config.bounds.min.y,
                        &config.bounds.min.z, &config.bounds.max.x,
                        &config.bounds.max.y, &config.bounds.max.z};
    for (float *field : fields) {
      if (!scanFloat(cursor, *field)) {
        error = "object directive 'bounds' expects six floats";
        return false;
      }
    }
    long flag = 0;
    scanInt(cursor, flag);
    config.bounds.flag = static_cast<int32_t>(flag);
    for (float v :
         {config.bounds.min.x, config.bounds.min.y, config.bounds.min.z,
          config.bounds.max.x, config.bounds.max.y, config.bounds.max.z})
      if (!std::isfinite(v)) {
        error = "object directive 'bounds' has a non-finite float";
        return false;
      }
    config.hasBounds = true;
    return true;
  }

  if (key == "damage") {
    float capacity = 0.0f, root = 0.0f;
    if (!scanFloat(cursor, capacity)) {
      error = "object directive 'damage' expects one or two floats";
      return false;
    }
    if (!scanFloat(cursor, root))
      root = 0.0f;
    if (!std::isfinite(capacity) || !std::isfinite(root)) {
      error = "object directive 'damage' has a non-finite float";
      return false;
    }
    config.damage.capacity = capacity;
    config.damage.root = root == 0.0f ? capacity : root; // 004650d0.
    return true;
  }

  if (key == "material") {
    // 00465150 writes the record indexed by +0x8c4; this callback never
    // increments the index. Retain directive order so the loader performs
    // every lookup and link replacement before retaining the last record.
    uint32_t generic = systemRpkId, firstSound = 0, impactSound = 0;
    float threshold = 0.0f;
    // Like sscanf, a failed preceding conversion leaves the remaining
    // operands at their defaults instead of resuming at the bad token.
    if (scanStockInteger(cursor, generic) &&
        scanStockInteger(cursor, firstSound) &&
        scanStockInteger(cursor, impactSound))
      scanFloat(cursor, threshold);
    MaterialDirective material{
        remapResourceId(generic, mapping), remapResourceId(firstSound, mapping),
        remapResourceId(impactSound, mapping), threshold};
    config.materials.push_back(material);
    return true;
  }

  if (key == "powerup") {
    PowerupDirective powerup;
    if (!scanWord(cursor, powerup.name) || powerup.name.size() > 32) {
      error =
          "object directive 'powerup' expects a name of at most 32 characters";
      return false;
    }
    long a = 0, b = 0;
    if (!scanInt(cursor, a) || !scanInt(cursor, b)) {
      error =
          "object directive 'powerup' expects a name and two decimal operands";
      return false;
    }
    powerup.a = static_cast<int>(a);
    powerup.b = static_cast<int>(
        remapResourceId(static_cast<uint32_t>(b & 0xffffffffu), mapping));
    config.powerup = std::move(powerup);
    config.hasPowerup = true;
    return true;
  }

  if (key == "init") {
    long value = 0;
    if (!scanInt(cursor, value)) {
      error = "object directive 'init' expects a decimal operand";
      return false;
    }
    config.initValue = static_cast<int>(value);
    return true;
  }

  if (key == "basetexture") {
    long value = 0;
    if (!scanInt(cursor, value)) {
      error = "object directive 'basetexture' expects a decimal operand";
      return false;
    }
    config.baseTextureId =
        remapResourceId(static_cast<uint32_t>(value & 0xffffffffu), mapping);
    return true;
  }

  if (key == "sfx_loop") {
    unsigned long id = 0;
    if (!scanHex(cursor, id)) {
      error = "object directive 'sfx_loop' expects a hex resource id";
      return false;
    }
    SfxLoopDirective sfx;
    sfx.resourceId =
        remapResourceId(static_cast<uint32_t>(id & 0xffffffffu), mapping);
    float value = 0.0f;
    if (scanFloat(cursor, value))
      sfx.value = value;
    if (scanFloat(cursor, value))
      sfx.param2 = toStockInt(value * 3600.0f);
    if (scanFloat(cursor, value))
      sfx.param3 = toStockInt(value * 3600.0f);
    config.sfxLoop = sfx;
    config.hasSfxLoop = true;
    return true;
  }

  if (key == "sfx_random") {
    if (config.sfxRandom.size() >= 16) {
      error = "object config exceeds the stock limit of 16 sfx_random entries";
      return false;
    }
    unsigned long id = 0;
    if (!scanHex(cursor, id)) {
      error = "object directive 'sfx_random' expects a hex resource id";
      return false;
    }
    SfxRandomDirective sfx;
    sfx.resourceId =
        remapResourceId(static_cast<uint32_t>(id & 0xffffffffu), mapping);
    float value = 0.0f;
    if (scanFloat(cursor, value))
      sfx.volume = value;
    if (scanFloat(cursor, value))
      sfx.pitchMin = value; // Default 1.0f in stock.
    float pitchMax = 1.0f;  // Default 1.0f in stock.
    if (scanFloat(cursor, value))
      pitchMax = value;
    sfx.pitchSpan = pitchMax - sfx.pitchMin;
    if (scanFloat(cursor, value))
      sfx.param2 = toStockInt(value * 3600.0f);
    if (scanFloat(cursor, value))
      sfx.param3 = toStockInt(value * 3600.0f);
    config.sfxRandom.push_back(sfx);
    return true;
  }

  if (key == "lods") {
    if (initializationCount != 0)
      return true;
    long count = 0;
    if (!scanInt(cursor, count) || count < 0 || count > 38) {
      error =
          "object directive 'lods' expects a decimal count between 0 and 38";
      return false;
    }
    LodsDirective lods;
    for (long i = 0; i < count; ++i) {
      float value = 0.0f;
      if (!scanFloat(cursor, value)) {
        error = "object directive 'lods' has fewer operands than its count";
        return false;
      }
      lods.thresholds.push_back(value);
    }
    // The stored count excludes the sentinel, but the endpoint itself is
    // still read by 00465bd0 when configuring the LOD transition.
    if (lods.thresholds.empty() || lods.thresholds.back() < 1.0f)
      lods.thresholds.push_back(1.0f);
    config.lods = std::move(lods);
    config.hasLods = true;
    return true;
  }

  if (key == "nocollision" || key == "buoy" || key == "friction" ||
      key == "flags" || key == "lod" || key == "color" || key == "colors" ||
      key == "rendertypes")
    return applySlotModifier(line, initializationCount, mapping, config, error);

  error = "unsupported object directive '" + key + "'";
  return false;
}

} // namespace object_config
