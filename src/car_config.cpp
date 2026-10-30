#include "car_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

// ---------------------------------------------------------------------------------------------
// Stock scanner, 00553890.
// ---------------------------------------------------------------------------------------------

namespace {

bool isSpace(unsigned char c) { return (c > 8 && c < 14) || c == 0x20; }  // 00553890 tests '\b' < c < '\x0e' or 0x20
bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
bool isHex(unsigned char c) {
  return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
unsigned char lower(unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + 32) : c; }
bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}

// 00553ce0: "0x" then at least one hex digit; anything else fails the conversion.
bool scanHex(const char*& p, int32_t& out) {
  if (!(p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))) return false;
  const char* q = p + 2;
  if (!isHex(static_cast<unsigned char>(*q))) return false;
  uint32_t v = 0;
  while (isHex(static_cast<unsigned char>(*q))) {
    const unsigned char c = static_cast<unsigned char>(*q);
    uint32_t d = isDigit(c) ? c - '0' : (c >= 'a' ? c - 'a' + 10 : c - 'A' + 10);
    v = v * 16u + d;
    ++q;
  }
  out = static_cast<int32_t>(v);
  p = q;
  return true;
}

}  // namespace

StockScanResult stockScan(std::string_view inputView, std::string_view formatView) {
  // The stock routine works on NUL-terminated C strings.
  const std::string inputStr(inputView.substr(0, inputView.find('\0')));
  const std::string formatStr(formatView.substr(0, formatView.find('\0')));
  const char* const start = inputStr.c_str();
  const char* in = start;
  const char* fmt = formatStr.c_str();
  StockScanResult result;
  int count = 0;
  bool hash = false;
  auto fail = [&]() {
    result.returned = count;
    result.consumed = static_cast<size_t>(in - start);
    return result;
  };
  while (*fmt) {
    if (isSpace(static_cast<unsigned char>(*fmt))) {
      while (isSpace(static_cast<unsigned char>(*in))) ++in;
      while (isSpace(static_cast<unsigned char>(*fmt))) ++fmt;
    }
    if (*fmt != '%') {
      if (*fmt != *in) return fail();
      ++fmt;
      ++in;
      continue;
    }
    if (fmt[1] == '#') {  // "%#" only asks for the byte count to be returned at the end
      fmt += 2;
      hash = true;
      continue;
    }
    const char* spec = fmt + 1;
    bool suppress = false;
    if (*spec == '*') {
      suppress = true;
      ++spec;
    }
    int width = 0;
    while (isDigit(static_cast<unsigned char>(*spec))) width = width * 10 + (*spec++ - '0');
    if (width == 0) width = 0x400;
    int precision = 0x400;
    if (*spec == '.') {
      ++spec;
      int p = 0;
      while (isDigit(static_cast<unsigned char>(*spec))) p = p * 10 + (*spec++ - '0');
      precision = p == 0 ? 0x400 : p;
    }
    if (*spec == 'h' || *spec == 'l' || *spec == 'L') ++spec;
    const char conv = static_cast<char>(*spec | 0x20);
    fmt = spec + 1;
    switch (conv) {
      case 'c': {
        if (!suppress) {
          StockScanValue v;
          v.kind = 'c';
          v.i = static_cast<unsigned char>(*in);
          v.s.assign(1, *in);
          result.values.push_back(v);
        }
        ++in;
        ++count;
        break;
      }
      case 'f': {
        while (isSpace(static_cast<unsigned char>(*in))) ++in;
        const bool negative = *in == '-';
        if (negative) ++in;
        if (!isDigit(static_cast<unsigned char>(*in))) return fail();
        uint32_t whole = 0;
        while (isDigit(static_cast<unsigned char>(*in))) whole = whole * 10u + static_cast<uint32_t>(*in++ - '0');
        float acc = static_cast<float>(whole);
        if (*in == '.') {
          ++in;
          float scale = 0.1f;  // 005f08e8
          const float tenth = static_cast<float>(0.1);  // 005f0d20 is the double 0.1
          while (isDigit(static_cast<unsigned char>(*in)) && precision > 0) {
            const float term = static_cast<float>(*in - '0') * scale;
            acc = term + acc;
            scale = scale * tenth;
            ++in;
            --precision;
          }
          while (isDigit(static_cast<unsigned char>(*in))) ++in;
        }
        if (negative) acc = -acc;
        if (!suppress) {
          StockScanValue v;
          v.kind = 'f';
          v.f = acc;
          result.values.push_back(v);
        }
        ++count;
        break;
      }
      case 's': {
        while (isSpace(static_cast<unsigned char>(*in))) ++in;
        std::string text;
        int remaining = width;
        // Stops at whitespace, NUL, the width, or the character that follows %s in the format.
        while (*in && !isSpace(static_cast<unsigned char>(*in)) && remaining > 0 && *in != *fmt) {
          text.push_back(*in);
          ++in;
          --remaining;
        }
        if (!suppress) {
          StockScanValue v;
          v.kind = 's';
          v.s = std::move(text);
          result.values.push_back(std::move(v));
        }
        ++count;
        break;
      }
      case 'u':
      case 'd':
      case 'x': {
        while (isSpace(static_cast<unsigned char>(*in))) ++in;
        int32_t value = 0;
        const bool unsignedConv = conv == 'u';
        if (conv == 'x' || (in[0] == '0' && (in[1] == 'x' || in[1] == 'X'))) {
          if (!scanHex(in, value)) return fail();
        } else {
          const bool negative = *in == '-';
          if (negative) ++in;
          if (!isDigit(static_cast<unsigned char>(*in))) return fail();
          uint32_t v = 0;
          while (isDigit(static_cast<unsigned char>(*in))) v = v * 10u + static_cast<uint32_t>(*in++ - '0');
          value = static_cast<int32_t>(negative && !unsignedConv ? 0u - v : v);
        }
        if (!suppress) {
          StockScanValue v;
          v.kind = 'd';
          v.i = value;
          result.values.push_back(v);
        }
        ++count;
        break;
      }
      default:
        break;  // stock ignores any other conversion letter
    }
  }
  result.consumed = static_cast<size_t>(in - start);
  result.returned = hash ? static_cast<int>(result.consumed) : count;
  return result;
}

// ---------------------------------------------------------------------------------------------
// Line reader.
// ---------------------------------------------------------------------------------------------

bool tokenizeCarConfig(std::string_view text, std::vector<CarConfigLine>& lines, std::string& error) {
  lines.clear();
  if (text.find('\0') != std::string_view::npos) {
    error = "car configuration contains a zero byte";
    return false;
  }
  size_t offset = 0;
  int number = 0;
  while (offset < text.size()) {
    const size_t end = text.find('\n', offset);
    const size_t length = (end == std::string_view::npos ? text.size() : end) - offset;
    ++number;
    if (length >= 256) {
      error = "line " + std::to_string(number) + ": longer than the 256-byte buffer of stock 004b6520";
      return false;
    }
    std::string line(text.substr(offset, length));
    offset = end == std::string_view::npos ? text.size() : end + 1;
    // 004b6340 cuts the copy at the first ';', then '#', then '/'.
    const auto cut = line.find_first_of(";#/");
    if (cut != std::string::npos) line.resize(cut);
    const auto first = line.find_first_not_of(" \r\n\t");
    if (first == std::string::npos) continue;  // strtok finds no token
    auto last = line.find_first_of(" \r\n\t", first);
    const size_t keyEnd = last == std::string::npos ? line.size() : last;
    CarConfigLine out;
    out.number = number;
    out.rawKey = line.substr(0, keyEnd);
    out.key = line.substr(first, keyEnd - first);
    for (char& c : out.key) c = static_cast<char>(lower(static_cast<unsigned char>(c)));
    // strtok puts a NUL at keyEnd; handlers scan from param_1 + strlen(param_1) + 1.
    if (keyEnd + 1 < line.size()) out.args = line.substr(keyEnd + 1);
    if (out.key == "eof") break;  // 004b6340 returns 1 and the file loop stops
    lines.push_back(std::move(out));
  }
  error.clear();
  return true;
}

// 0060e0f8: 107 entries (name pointer, handler pointer) read from the executable.
const std::array<CarDirectiveInfo, 107> kCarDirectiveTable = {{
    {"click", 0x0046b950}, {"body", 0x0046bb80}, {"bone", 0x0046c280}, {"noclick", 0x0046c2d0},
    {"nocollision", 0x0046c310}, {"buoy", 0x0046c360}, {"render", 0x00443de0}, {"light", 0x00443de0},
    {"deformable", 0x0046bfa0}, {"shadow", 0x0046c1a0}, {"flags", 0x0046d0c0}, {"lod", 0x0046d180},
    {"lods", 0x0046d290}, {"colors", 0x0046d330}, {"category", 0x0046ced0}, {"slot", 0x0046c3a0},
    {"bolt", 0x0046c4e0}, {"attach", 0x0046c6a0}, {"compatible", 0x0046c770}, {"slotdmgmode", 0x0046c840},
    {"slotdeform", 0x0046c8a0}, {"slottype", 0x0046cb30}, {"slotflags", 0x0046cb30}, {"slotmove", 0x0046ccd0},
    {"deform", 0x0046cf10}, {"mesh", 0x0046ce60}, {"texture", 0x0046cdf0}, {"logo", 0x0046d400},
    {"flap", 0x0046c8f0}, {"flapsfx", 0x0046ca70}, {"camera", 0x00443b00}, {"type", 0x00442f90},
    {"friction", 0x004432b0}, {"spring", 0x004431c0}, {"wheel", 0x004433e0}, {"wheelbones", 0x004438d0},
    {"bonepos", 0x00443a00}, {"wheelpars", 0x00443310}, {"skid", 0x00443390}, {"wheeldeform", 0x004439b0},
    {"arm", 0x004435b0}, {"hub", 0x00443690}, {"instant_center", 0x004437a0}, {"steering", 0x00443ea0},
    {"sw_radius", 0x00443ef0}, {"steeringradius", 0x00443ef0}, {"maxsteer", 0x00444260}, {"rpm_idle", 0x004442b0},
    {"drag", 0x00444310}, {"wing", 0x004443c0}, {"center", 0x00444570}, {"rollbar", 0x00443a60},
    {"linked", 0x0046d8a0}, {"sfx_engine_up", 0x00445760}, {"sfx_engine_down", 0x00445890},
    {"sfx_exhaust", 0x004459c0}, {"sfx_gearbox_fwd", 0x00445af0}, {"sfx_gearbox_rev", 0x00445b80},
    {"sfx_wreck", 0x00445c10}, {"sfx_horn", 0x00445c80}, {"sfx_gearchange", 0x00445d50}, {"mass", 0x00443f50},
    {"enginepower", 0x00443f90}, {"ratios", 0x00444080}, {"torque", 0x004440e0}, {"rearendratio", 0x00444150},
    {"brakepower", 0x00444190}, {"clutch", 0x00444200}, {"driver", 0x004445f0}, {"controller", 0x004445f0},
    {"ext_camera", 0x00443d40}, {"nodriver", 0x00444710}, {"name", 0x00444730}, {"seat", 0x00444740},
    {"pedals", 0x00444810}, {"shifter", 0x00444990}, {"key", 0x00444a60}, {"handbrake", 0x00444ae0},
    {"cockpit_rpm", 0x00444b60}, {"cockpit_speed", 0x00444cb0}, {"osd_gauge", 0x00444e00},
    {"osd_rpmpin", 0x00444e80}, {"osd_spdpin", 0x00444f20}, {"osd_nospin", 0x00444fc0},
    {"osd_damage", 0x00445060}, {"osd_gear", 0x00445120}, {"osd_speed", 0x004451b0}, {"osd_rpm", 0x00445260},
    {"osd_nos", 0x00445310}, {"osd_turbo", 0x004453c0}, {"osd_mileage", 0x00445470}, {"osd_abs", 0x00445520},
    {"osd_asr", 0x004455b0}, {"osd_hbrake", 0x00445640}, {"osd_emergency", 0x004456d0},
    {"osd_damage_slot", 0x004450e0}, {"stockpart", 0x0046d970}, {"material", 0x00445e00},
    {"damage", 0x0046cf50}, {"damagedef", 0x00445ef0}, {"velfactor", 0x00445fc0}, {"turn", 0x00445f00},
    {"roll", 0x00446000}, {"pitch", 0x00446050}, {"steerhelp", 0x004460a0}, {"cheat_control", 0x00446100},
    {"notify", 0x00446160},
}};

const CarDirectiveInfo* findCarDirective(std::string_view name) {
  for (const auto& entry : kCarDirectiveTable)
    if (iequals(name, entry.name)) return &entry;
  return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Directive handlers.
// ---------------------------------------------------------------------------------------------

namespace {

constexpr float kTenth = 0.1f;  // 005f08e8

struct Failure {
  std::string reason;
};

float floatOr(const StockScanResult& r, size_t index, float fallback) {
  size_t seen = 0;
  for (const auto& v : r.values) {
    if (seen++ == index) return v.kind == 'f' ? v.f : static_cast<float>(v.i);
  }
  return fallback;
}
int32_t intOr(const StockScanResult& r, size_t index, int32_t fallback) {
  if (index < r.values.size()) return r.values[index].i;
  return fallback;
}
bool has(const StockScanResult& r, size_t count) { return r.values.size() >= count; }

CarTransform makeTransform(const CarVec3& position, const CarVec3& orientation) {
  PartSlotDirective directive{};
  for (int i = 0; i < 3; ++i) {
    directive.position[i] = position[i];
    directive.orientation[i] = orientation[i];
  }
  PartSlotState state{};
  CarTransform out;
  out.position = position;
  out.orientation = orientation;
  if (makePartSlotState(directive, state)) out.rotation = state.rotation;
  return out;
}

void normalize(CarVec3& v) {
  const float length = std::sqrt(v[1] * v[1] + v[0] * v[0] + v[2] * v[2]);  // same order as 0046c8f0/0046ccd0
  if (length != 0.0f) {
    const float inverse = 1.0f / length;
    v[0] = inverse * v[0];
    v[1] = inverse * v[1];
    v[2] = inverse * v[2];
  }
}

bool floatBits(uint32_t bits, float& out) {
  static_assert(sizeof(float) == sizeof(uint32_t), "float width");
  std::memcpy(&out, &bits, sizeof(out));
  return true;
}
float bitsToFloat(uint32_t bits) {
  float f;
  floatBits(bits, f);
  return f;
}

// One OSD handler: format string, how many pointers stock passes (more conversions than this would
// write through stack garbage), and when the resource is requested.
struct OsdSpec {
  const char* name;
  const char* format;
  size_t pointers;       // pointer arguments stock supplies, including the resource id
  int loadWhen;          // 0 always, 1 when returned > 0, 2 when returned > 1
};
const OsdSpec kOsdSpecs[] = {
    {"osd_gauge", "%d %f %f %f", 4, 0},
    {"osd_rpmpin", "%d %f %f %f %f %f %f %f", 8, 0},
    {"osd_spdpin", "%d %f %f %f %f %f %f %f", 8, 0},
    {"osd_nospin", "%d %f %f %f %f %f %f %f", 8, 0},
    {"osd_damage", "%d %f %f %f", 4, 0},
    {"osd_gear", "%d %f %f %f %d %8s", 6, 2},
    {"osd_speed", "%d %f %f %f %d %8s", 10, 1},
    {"osd_rpm", "%d %f %f %f %d %8s", 10, 1},
    {"osd_nos", "%d %f %f %f %d %8s %f %f %f %f", 10, 1},
    {"osd_turbo", "%d %f %f %f %d %8s %f %f %f %f", 10, 1},
    {"osd_mileage", "%d %f %f %f %d %8s %f %f %f %f", 10, 1},
    {"osd_abs", "%d %f %f %f %d %8s %f %f %f %f", 5, 1},
    {"osd_asr", "%d %f %f %f %d %8s %f %f %f %f", 5, 1},
    {"osd_hbrake", "%d %f %f %f %d %8s %f %f %f %f", 5, 1},
    {"osd_emergency", "%d %f %f %f %d %8s %f %f %f %f", 5, 1},
};

class Parser {
 public:
  explicit Parser(CarConfig& config) : c_(config) {}

  // Returns false and fills `reason` when the line cannot be applied faithfully.
  bool apply(const CarConfigLine& line, std::string& reason) {
    line_ = &line;
    reason_.clear();
    const std::string& k = line.key;
    const CarDirectiveInfo* info = findCarDirective(k);
    if (!info) {
      c_.ignored.push_back({line.number, k, "not in the directive table at 0060e0f8; stock skips the line"});
      return true;
    }
    bool ok = dispatch(k, line);
    if (!ok) reason = reason_;
    return ok;
  }

 private:
  CarConfig& c_;
  const CarConfigLine* line_ = nullptr;
  std::string reason_;

  bool fail(std::string text) {
    reason_ = std::move(text);
    return false;
  }
  void skip(const std::string& why) { c_.ignored.push_back({line_->number, line_->key, why}); }
  void request(int loadType, uint32_t resource) {
    c_.resources.push_back({line_->key, resource, loadType, line_->number, false, {}, 0});
  }
  static StockScanResult scan(const CarConfigLine& l, const char* fmt) { return stockScan(l.args, fmt); }

  bool needBody(const char* what) {
    if (c_.bodies.empty()) {
      skip(std::string(what) + " does nothing before the first body or render (stock tests the body count)");
      return false;
    }
    return true;
  }
  bool haveSlot() const { return !c_.slots.empty(); }
  CarWheel* wheel() { return c_.wheels.empty() ? nullptr : &c_.wheels.back(); }
  bool needWheel() {
    if (c_.wheels.empty()) {
      skip("does nothing before the first `type` line (stock tests the wheel count)");
      return false;
    }
    return true;
  }

  // 0046bb80 -- shared by `body` and `steering`.
  bool body(bool isSteering) {
    const CarConfigLine& l = *line_;
    const size_t existing = c_.bodies.size();
    if (existing > 0x1f) {
      skip("more than 32 body records; stock returns without reading the line");
      if (isSteering) c_.steeringBody = static_cast<int>(existing) - 1;
      return true;
    }
    auto r = scan(l, "%f %f %f %f %f %f %f %s %f %f %f");
    if (!has(r, 6)) return fail("position and angles are missing; stock would read uninitialised stack");
    CarBodyRecord rec;
    rec.kind = CarBodyRecord::Body;
    rec.directive = l.key;
    rec.line = l.number;
    const CarVec3 pos{floatOr(r, 0, 0), floatOr(r, 1, 0), floatOr(r, 2, 0)};
    const CarVec3 rot{floatOr(r, 3, 0), floatOr(r, 4, 0), floatOr(r, 5, 0)};
    rec.transform = makeTransform(pos, rot);
    rec.shape.mass = floatOr(r, 6, 0);
    const std::string word = r.values.size() > 7 ? r.values[7].s : std::string();
    if (word.size() > 127) return fail("shape word longer than the 128-byte stock buffer");
    const float d0 = floatOr(r, 8, 0), d1 = floatOr(r, 9, 0), d2 = floatOr(r, 10, 0);
    auto needDims = [&](size_t conversions) {
      return r.values.size() >= conversions;
    };
    const auto number = stockScan(word, "%d");
    if (!number.values.empty()) {
      rec.shape.kind = CarShapeKind::MeshResource;
      rec.shape.meshResource = static_cast<uint32_t>(number.values[0].i);
      request(9, rec.shape.meshResource);
    } else if (iequals(word, "ball") || iequals(word, "sphere")) {
      if (!needDims(9)) return fail("sphere radius missing; stock would read uninitialised stack");
      rec.shape.kind = CarShapeKind::Sphere;
      rec.shape.dims = {d0, 0, 0};
    } else if (iequals(word, "box")) {
      if (!needDims(11)) return fail("box size missing; stock would read uninitialised stack");
      rec.shape.kind = CarShapeKind::Box;
      rec.shape.dims = {d0, d1, d2};
    } else if (iequals(word, "rodx") || iequals(word, "rody") || iequals(word, "rodz")) {
      if (!needDims(10)) return fail("rod size missing; stock would read uninitialised stack");
      rec.shape.kind = iequals(word, "rodx") ? CarShapeKind::RodX
                       : iequals(word, "rody") ? CarShapeKind::RodY : CarShapeKind::RodZ;
      rec.shape.dims = {d0, d1, 0};
    } else {
      if (word.empty()) return fail("shape word missing");
      rec.shape.kind = CarShapeKind::MeshFile;
      rec.shape.meshPath = word;
      rec.shape.dims = {1, 1, 1};
    }
    if (c_.rootBody < 0) c_.rootBody = static_cast<int>(existing);
    c_.bodies.push_back(std::move(rec));
    c_.lastKind = 0;
    c_.lastIndex = static_cast<int>(existing);
    if (isSteering) c_.steeringBody = static_cast<int>(c_.bodies.size()) - 1;
    return true;
  }

  // 0046bfa0 (+ 00443de0 for render/light).
  bool renderLike() {
    const CarConfigLine& l = *line_;
    const bool wrapper = l.key == "render" || l.key == "light";
    if (c_.bodies.size() >= 0x20 || c_.renderCount >= 0x20) {
      skip("more than 32 body/render records; stock returns without reading the line");
      return true;
    }
    auto r = scan(l, "%d %f %f %f %f %f %f");
    CarBodyRecord rec;
    rec.kind = CarBodyRecord::Render;
    rec.directive = l.key;
    rec.line = l.number;
    rec.renderResource = static_cast<uint32_t>(intOr(r, 0, 0));
    const bool light = iequals(l.rawKey, "light");  // strcmp on the first argument, which keeps leading blanks
    request(light ? 0xd : 0xe, rec.renderResource);
    if (!light) c_.resources.back().fallbackType = 0xd;
    rec.transform = makeTransform({floatOr(r, 1, 0), floatOr(r, 2, 0), floatOr(r, 3, 0)},
                                  {floatOr(r, 4, 0), floatOr(r, 5, 0), floatOr(r, 6, 0)});
    c_.bodies.push_back(std::move(rec));
    ++c_.renderCount;
    if (wrapper) {
      // 00443de0: tag the new record when it directly follows `steering` or `wheel`.
      auto& flags = c_.bodies.back().flags;
      if (c_.lastKind == 0) {
        if (c_.lastIndex == c_.steeringBody && c_.steeringBody >= 0)
          flags |= (static_cast<uint32_t>(c_.steeringBody) << 24) | kBodySteeringRender;
      } else if (c_.lastKind == 0x10 && c_.lastIndex == static_cast<int>(c_.wheels.size()) - 1) {
        flags |= (static_cast<uint32_t>(c_.wheels.size() - 1) << 24) | kBodyWheelRender;
      }
    }
    return true;
  }

  // 0046b950
  bool click() {
    auto r = scan(*line_, "%s %f %f %f %f %f %f");
    CarClick out;
    const std::string word = r.values.empty() ? std::string() : r.values[0].s;
    if (word.size() > 127) return fail("click word longer than the 128-byte stock buffer");
    out.position = {floatOr(r, 1, 0), floatOr(r, 2, 0), floatOr(r, 3, 0)};
    out.orientation = {floatOr(r, 4, 0), floatOr(r, 5, 0), floatOr(r, 6, 0)};
    const auto number = stockScan(word, "%d");
    if (!number.values.empty()) {
      out.resource = static_cast<uint32_t>(number.values[0].i);
      request(9, *out.resource);
    } else {
      if (word.empty()) return fail("click word missing");
      out.meshPath = word;
    }
    c_.click = std::move(out);
    return true;
  }

  // 0046c3a0
  bool slot() {
    auto r = scan(*line_, "%f %f %f %f %f %f %d");
    if (!has(r, 7)) return fail("slot needs 6 numbers and an id; stock leaves the id uninitialised otherwise");
    CarSlot s;
    s.line = line_->number;
    const CarVec3 pos{floatOr(r, 0, 0), floatOr(r, 1, 0), floatOr(r, 2, 0)};
    s.orientation = {floatOr(r, 3, 0), floatOr(r, 4, 0), floatOr(r, 5, 0)};
    PartSlotDirective d{};
    for (int i = 0; i < 3; ++i) { d.position[i] = pos[i]; d.orientation[i] = s.orientation[i]; }
    d.id = r.values[6].i;
    if (!makePartSlotState(d, s.state)) return fail("slot has a non-finite number");
    c_.slots.push_back(std::move(s));
    return true;
  }

  bool slotDirective() {
    const std::string& k = line_->key;
    if (k == "slot") return slot();
    if (!haveSlot()) {
      if (k == "slottype" || k == "slotflags")
        return fail("stock writes through a null slot pointer when no slot exists yet");
      skip("no slot declared yet; stock does nothing");
      return true;
    }
    CarSlot& s = c_.slots.back();
    if (k == "attach") {  // 0046c6a0
      auto r = scan(*line_, "%d %d");
      s.attachments.push_back({static_cast<unsigned>(intOr(r, 0, 0)), intOr(r, 1, 0)});
    } else if (k == "bolt") {  // 0046c4e0
      auto r = scan(*line_, "%f %f %f %f %f %f %d");
      if (!has(r, 6)) return fail("bolt position and angles are missing");
      CarSlotBolt b;
      b.transform = makeTransform({floatOr(r, 0, 0), floatOr(r, 1, 0), floatOr(r, 2, 0)},
                                  {floatOr(r, 3, 0), floatOr(r, 4, 0), floatOr(r, 5, 0)});
      b.resource = static_cast<uint32_t>(intOr(r, 6, 0));
      if (b.resource) request(0xe, b.resource);
      s.bolts.push_back(std::move(b));
    } else if (k == "compatible") {  // 0046c770
      auto r = scan(*line_, "%d %d");
      s.compatible.push_back({static_cast<uint32_t>(intOr(r, 0, 0)), intOr(r, 1, 0)});
    } else if (k == "slotdmgmode") {  // 0046c840: the target is only overwritten when a number parses
      auto r = scan(*line_, "%d");
      if (has(r, 1)) s.damageMode = r.values[0].i;
    } else if (k == "slotdeform") {  // 0046c8a0
      auto r = scan(*line_, "%f");
      if (has(r, 1)) s.deform = r.values[0].f;
    } else if (k == "slottype" || k == "slotflags") {  // 0046cb30: stores 0 when no number parses
      auto r = scan(*line_, "%d");
      s.slotType = intOr(r, 0, 0);
    } else if (k == "slotmove") {  // 0046ccd0
      auto r = scan(*line_, "%d %f %f %f %f");
      CarSlotMove m;
      m.mode = intOr(r, 0, 0);
      m.angle = floatOr(r, 1, bitsToFloat(0x40490fdb));
      CarVec3 axis{floatOr(r, 2, 1.0f), floatOr(r, 3, 0), floatOr(r, 4, 0)};
      const float length = std::sqrt(axis[0] * axis[0] + axis[2] * axis[2] + axis[1] * axis[1]);
      if (length != 0.0f) {
        const float inverse = 1.0f / length;
        m.axis = {inverse * axis[0], inverse * axis[1], axis[2] * inverse};
      }
      s.move = m;
      s.slotType = static_cast<int32_t>(static_cast<uint32_t>(s.slotType) | 0x400000u);
    } else if (k == "flap") {  // 0046c8f0
      auto r = scan(*line_, "%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f");
      CarFlap f;
      f.position = {floatOr(r, 0, 0), floatOr(r, 1, 0), floatOr(r, 2, 0)};
      f.axisA = {floatOr(r, 3, 0), floatOr(r, 4, 0), floatOr(r, 5, 0)};
      f.axisB = {floatOr(r, 6, 0), floatOr(r, 7, 0), floatOr(r, 8, 0)};
      f.values = {floatOr(r, 9, 0), floatOr(r, 10, 0), floatOr(r, 11, 0), floatOr(r, 12, 1.2f),
                  floatOr(r, 13, 0), floatOr(r, 14, 0.5f)};
      normalize(f.axisA);
      normalize(f.axisB);
      s.flap = f;
      s.flapSounds = {0, 0};
      s.hasFlapSounds = false;
    } else if (k == "flapsfx") {  // 0046ca70
      if (!s.flap) {
        skip("slot has no flap yet; stock does nothing");
        return true;
      }
      auto r = scan(*line_, "%d %d");
      s.flapSounds = {static_cast<uint32_t>(intOr(r, 0, 0)), static_cast<uint32_t>(intOr(r, 1, 0))};
      s.hasFlapSounds = true;
      request(6, s.flapSounds[0]);
      request(6, s.flapSounds[1]);
    }
    return true;
  }

  // Shape word for `type` (00442f90).
  bool wheelType() {
    if (c_.wheels.size() >= 16) {
      skip("more than 16 wheel records; stock returns without reading the line");
      return true;
    }
    auto r = scan(*line_, "%f %s %f %f %f");
    CarWheel w;
    w.line = line_->number;
    w.shape.mass = floatOr(r, 0, 0);
    const std::string word = r.values.size() > 1 ? r.values[1].s : std::string();
    if (word.size() > 127) return fail("shape word longer than the 128-byte stock buffer");
    if (iequals(word, "sphere")) {
      if (!has(r, 3)) return fail("sphere radius missing; stock would read uninitialised stack");
      w.shape.kind = CarShapeKind::Sphere;
      w.shape.dims = {floatOr(r, 2, 0), 0, 0};
      w.friction = {1.0f, 1.0f};
    } else if (iequals(word, "box")) {
      if (!has(r, 5)) return fail("box size missing; stock would read uninitialised stack");
      w.shape.kind = CarShapeKind::Box;
      w.shape.dims = {floatOr(r, 2, 0), floatOr(r, 3, 0), floatOr(r, 4, 0)};
    } else {
      if (word.empty()) return fail("shape word missing");
      w.shape.kind = CarShapeKind::MeshFile;
      w.shape.meshPath = word;
      w.shape.dims = {1, 1, 1};
    }
    c_.wheels.push_back(std::move(w));
    return true;
  }

  bool wheelDirective() {
    const std::string& k = line_->key;
    if (k == "type") return wheelType();
    if (k == "skid") {  // 00443390: not gated by the wheel count
      auto r = scan(*line_, "%f %f %f");
      c_.skid = {floatOr(r, 0, c_.skid[0]), floatOr(r, 1, c_.skid[1]), floatOr(r, 2, c_.skid[2])};
      return true;
    }
    if (!needWheel()) return true;
    CarWheel& w = *wheel();
    if (k == "friction") {  // 004432b0
      auto r = scan(*line_, "%f %f");
      w.friction = {floatOr(r, 0, w.friction[0]), floatOr(r, 1, w.friction[1])};
    } else if (k == "spring") {  // 004431c0
      auto r = scan(*line_, "%f %f %f");
      static const float fallback[3] = {0.1f, 0.2f, 0.05f};
      for (size_t i = 0; i < 3; ++i) {
        float v = floatOr(r, i, w.spring[i]);
        if (v <= 0.0f) {
          skip("Invalid spring parameter (parameter " + std::to_string(i) + "); stock logs it and uses a default");
          v = fallback[i];
        }
        w.spring[i] = v;
      }
    } else if (k == "wheel") {  // 004433e0
      auto r = scan(*line_, "%f %f %f %f %f %f %f %f %f %f");
      if (!has(r, 6)) return fail("wheel position and angles are missing; stock would read uninitialised stack");
      w.position = {floatOr(r, 0, 0), floatOr(r, 1, 0), floatOr(r, 2, 0)};
      w.orientation = {floatOr(r, 3, 0), floatOr(r, 4, 0), floatOr(r, 5, 0)};
      const float n7 = floatOr(r, 6, 0), n8 = floatOr(r, 7, 0);
      float n9 = floatOr(r, 8, -1.0f), n10 = floatOr(r, 9, -1.0f);
      if (n9 < 0.0f) n9 = 1.0f;
      if (n10 < 0.0f) n10 = n7 == 0.0f ? 1.0f : 0.0f;
      w.wheelA = n7;
      w.wheelB = n8;
      w.wheelC = std::fabs(n9);
      w.wheelD = std::fabs(n10);
      w.hasWheel = true;
      // Defaults the handler writes into the same record.
      w.bonePos = {0, 0.4f, 0};
      w.wheelDeform = 1.0f;
      w.armValue = -1.0f;
      w.hubValue = -1.0f;
      w.instantCenterA = {0, 0, 10000.0f};
      w.instantCenterB = {10000.0f, 0, 0};
      c_.lastKind = 0x10;
      c_.lastIndex = static_cast<int>(c_.wheels.size()) - 1;
    } else if (k == "wheelbones") {  // 004438d0
      w.wheelBones = true;
    } else if (k == "bonepos") {  // 00443a00
      auto r = scan(*line_, "%f %f %f");
      w.bonePos = {floatOr(r, 0, w.bonePos[0]), floatOr(r, 1, w.bonePos[1]), floatOr(r, 2, w.bonePos[2])};
    } else if (k == "wheeldeform") {  // 004439b0
      auto r = scan(*line_, "%f");
      w.wheelDeform = floatOr(r, 0, w.wheelDeform);
    } else if (k == "arm") {  // 004435b0
      auto r = scan(*line_, "%f %f %f %f %f %f %f");
      if (!has(r, 7)) return fail("arm needs 7 numbers; stock would read uninitialised stack");
      w.armValue = floatOr(r, 0, 0);
      w.armAnchor = {floatOr(r, 1, 0) + w.position[0], floatOr(r, 2, 0) + w.position[1], floatOr(r, 3, 0) + w.position[2]};
      w.armDirection = {floatOr(r, 4, 0), floatOr(r, 5, 0), floatOr(r, 6, 0)};
    } else if (k == "hub") {  // 00443690
      auto r = scan(*line_, "%f %f %f %f %f %f %f %f %f %f");
      if (!has(r, 10)) return fail("hub needs 10 numbers; stock would read uninitialised stack");
      w.hubValue = floatOr(r, 0, 0);
      for (size_t i = 0; i < 6; ++i) w.hubValues[i] = floatOr(r, 1 + i, 0);
      w.hubAnchor = {floatOr(r, 7, 0) + w.position[0], floatOr(r, 8, 0) + w.position[1], floatOr(r, 9, 0) + w.position[2]};
    } else if (k == "instant_center") {  // 004437a0: format has 10 %f but only 6 pointers
      auto r = scan(*line_, "%f %f %f %f %f %f %f %f %f %f");
      if (r.values.size() > 6) return fail("more than 6 numbers; stock would write through stack garbage");
      w.instantCenterA = {floatOr(r, 0, 0) + w.position[0], floatOr(r, 1, 0) + w.position[1], floatOr(r, 2, 0) + w.position[2]};
      w.instantCenterB = {floatOr(r, 3, 0) + w.position[0], floatOr(r, 4, 0) + w.position[1], floatOr(r, 5, 0) + w.position[2]};
    }
    return true;
  }

  // Resolve a `%d` shape word shared by sfx handlers.
  bool sfxEngine(std::vector<CarEngineSound>& list, size_t floats) {
    if (list.size() >= 16) {
      skip("more than 16 entries; stock skips the line");
      return true;
    }
    auto r = scan(*line_, floats == 4 ? "%d %f %f %f %f" : "%d %f %f %f %f %f");
    CarEngineSound s;
    s.resource = static_cast<uint32_t>(intOr(r, 0, 0));
    s.rpmScale = floatOr(r, 1, 1000.0f);
    s.values = {floatOr(r, 2, 1.0f), floatOr(r, 3, 1000000.0f), floatOr(r, 4, 1.0f), floatOr(r, 5, 1.0f)};
    request(6, s.resource);
    list.push_back(s);
    return true;
  }

  bool gauge(std::optional<CarGauge>& out, float second) {
    auto r = scan(*line_, "%f %f %f %f %f %f %d %f %f %f %f");
    CarGauge g;
    g.position = {floatOr(r, 0, 0), floatOr(r, 1, 0), floatOr(r, 2, 0)};
    g.orientation = {floatOr(r, 3, 0), floatOr(r, 4, 0), floatOr(r, 5, 0)};
    g.resource = static_cast<uint32_t>(intOr(r, 6, 0));
    g.values = {floatOr(r, 7, 2.2f), floatOr(r, 8, second), floatOr(r, 9, -2.2f), floatOr(r, 10, 2.2f)};
    request(0xe, g.resource);
    out = g;
    return true;
  }

  bool osd(const OsdSpec& spec) {
    auto r = scan(*line_, spec.format);
    if (r.values.size() > spec.pointers)
      return fail("more numbers than stock has destinations; it would write through stack garbage");
    CarOsdElement e;
    e.directive = line_->key;
    e.converted = r.returned;
    e.resource = static_cast<uint32_t>(intOr(r, 0, 0));
    for (size_t i = 1; i < r.values.size(); ++i) {
      const auto& v = r.values[i];
      if (v.kind == 'f') e.floats.push_back(v.f);
      else if (v.kind == 'd') e.color = v.i;
      else if (v.kind == 's') e.text = v.s;
    }
    const bool load = spec.loadWhen == 0 || (spec.loadWhen == 1 && r.returned > 0) || (spec.loadWhen == 2 && r.returned > 1);
    if (load) request(0xe, e.resource);
    c_.osd.push_back(std::move(e));
    return true;
  }

  bool dispatch(const std::string& k, const CarConfigLine& l) {
    // ---- body family (0046b950 .. 0046d400) ----
    if (k == "body") return body(false);
    if (k == "steering") {  // 00443ea0: body line, then remember it as the steering body
      const bool ok = body(true);
      return ok;
    }
    if (k == "render" || k == "light" || k == "deformable") return renderLike();
    if (k == "click") return click();
    if (k == "noclick") {  // 0046c2d0
      if (needBody("noclick")) c_.bodies.back().flags |= kBodyNoClick;
      return true;
    }
    if (k == "nocollision" || k == "buoy") {  // 0046c310 / 0046c360 touch the geometry of the last record
      if (!needBody(k.c_str())) return true;
      if (c_.bodies.back().kind != CarBodyRecord::Body)
        return fail("stock writes through the geometry pointer of the last record, which is a render record (null)");
      if (k == "nocollision") {
        c_.bodies.back().flags |= kBodyNoCollision;
        c_.bodies.back().geometryNoCollision = true;
      } else {
        c_.bodies.back().geometryBuoy = true;
      }
      return true;
    }
    if (k == "bone") {  // 0046c280
      if (needBody("bone")) {
        c_.bodies.back().flags |= kBodyBone;
        c_.rootBody = static_cast<int>(c_.bodies.size()) - 1;
      }
      return true;
    }
    if (k == "flags") {  // 0046d0c0
      if (needBody("flags")) {
        auto r = scan(l, "%d");
        c_.bodies.back().flags |= static_cast<uint32_t>(intOr(r, 0, 0)) & kBodyFlagMask;
      }
      return true;
    }
    if (k == "lod") {  // 0046d180
      if (needBody("lod")) {
        auto r = scan(l, "%d %d");
        auto& b = c_.bodies.back();
        b.lodNear = static_cast<uint32_t>(intOr(r, 0, 0)) & 0xf;
        b.lodFar = static_cast<uint32_t>(intOr(r, 1, 0)) & 0xf;
        b.flags = (b.flags & 0xff00ffffu) | (b.lodFar << 20) | (b.lodNear << 16);
      }
      return true;
    }
    if (k == "colors") {  // 0046d330: count, then that many numbers
      if (!needBody("colors")) return true;
      auto head = scan(l, "%#%d");
      int32_t count = head.values.empty() ? 0 : head.values[0].i;
      if (count < 0 || count > 4096) return fail("colors count out of range");
      std::string rest = l.args.substr(std::min<size_t>(static_cast<size_t>(head.returned), l.args.size()));
      std::vector<int32_t> out;
      for (int32_t i = 0; i < count; ++i) {
        auto one = stockScan(rest, "%#%d");
        if (one.values.empty()) return fail("colors lists fewer numbers than its count; stock keeps uninitialised memory");
        out.push_back(one.values[0].i);
        rest = rest.substr(std::min<size_t>(static_cast<size_t>(one.returned), rest.size()));
      }
      c_.bodies.back().colors = std::move(out);
      return true;
    }
    if (k == "linked") {  // 0046d8a0
      if (needBody("linked")) {
        auto r = scan(l, "%d %d %d");
        c_.bodies.back().links.push_back({intOr(r, 0, 0), intOr(r, 1, 0), intOr(r, 2, 0)});
        c_.bodies.back().flags |= kBodyLinked;
      }
      return true;
    }
    if (k == "lods") {  // 0046d290
      auto head = scan(l, "%#%d");
      const int32_t count = head.values.empty() ? 0 : head.values[0].i;
      if (count < 0 || count > 15) return fail("lods count would overrun the 16-float array (stock overwrites the next field)");
      std::string rest = l.args.substr(std::min<size_t>(static_cast<size_t>(head.returned), l.args.size()));
      std::vector<float> out;
      for (int32_t i = 0; i < count; ++i) {
        auto one = stockScan(rest, "%#%f");
        if (one.values.empty()) return fail("lods lists fewer numbers than its count");
        out.push_back(one.values[0].f);
        rest = rest.substr(std::min<size_t>(static_cast<size_t>(one.returned), rest.size()));
      }
      // Stock: if the last distance is below 1.0 a 1.0 end marker follows it, otherwise the last one is dropped.
      const float last = count == 0 ? 0.0f : out.back();
      if (last < 1.0f) {
        c_.lodEndMarker = true;
      } else {
        out.pop_back();
        c_.lodEndMarker = false;
      }
      c_.lodDistances = std::move(out);
      return true;
    }
    if (k == "category") {  // 0046ced0
      auto r = scan(l, "%d");
      if (has(r, 1)) c_.category = r.values[0].i;
      return true;
    }
    if (k == "deform") {  // 0046cf10
      auto r = scan(l, "%f");
      if (has(r, 1)) c_.deform = r.values[0].f;
      return true;
    }
    if (k == "damage") {  // 0046cf50: the field is overwritten with 0 when nothing parses
      auto r = scan(l, "%f %f");
      c_.damage = floatOr(r, 0, 0);
      return true;
    }
    if (k == "mesh" || k == "texture" || k == "logo") {  // 0046ce60 / 0046cdf0 / 0046d400
      auto r = scan(l, "%d");
      request(k == "mesh" ? 5 : 7, static_cast<uint32_t>(intOr(r, 0, 0)));
      return true;
    }
    if (k == "stockpart") {  // 0046d970
      auto r = scan(l, "%d");
      const int32_t id = intOr(r, 0, 0);
      if (id != 0) {
        c_.stockParts.push_back(static_cast<uint32_t>(id));
        request(8, static_cast<uint32_t>(id));
      }
      return true;
    }
    if (k == "shadow") {  // 0046c1a0
      auto r = scan(l, "%d %f %f %f %f %f %f");
      CarShadow s;
      s.resource = static_cast<uint32_t>(intOr(r, 0, 0));
      s.transform = makeTransform({floatOr(r, 1, 0), floatOr(r, 2, 0), floatOr(r, 3, 0)},
                                  {floatOr(r, 4, 0), floatOr(r, 5, 0), floatOr(r, 6, 0)});
      request(0xe, s.resource);
      c_.shadow = s;
      return true;
    }
    if (k == "slot" || k == "attach" || k == "bolt" || k == "compatible" || k == "slotdmgmode" ||
        k == "slotdeform" || k == "slottype" || k == "slotflags" || k == "slotmove" || k == "flap" ||
        k == "flapsfx")
      return slotDirective();

    // ---- wheel records (00442f90 .. 004439b0) ----
    if (k == "type" || k == "friction" || k == "spring" || k == "wheel" || k == "wheelbones" || k == "bonepos" ||
        k == "wheeldeform" || k == "arm" || k == "hub" || k == "instant_center" || k == "skid")
      return wheelDirective();
    if (k == "wheelpars") {  // 00443310
      auto r = scan(l, "%f %f %f %f %f %f %f %f %f");
      for (size_t i = 0; i < 9; ++i) c_.wheelPars[i] = floatOr(r, i, c_.wheelPars[i]);
      return true;
    }
    if (k == "rollbar") {  // 00443a60
      if (c_.rollbars.size() >= 8) {
        skip("more than 8 rollbars; stock skips the line");
        return true;
      }
      auto r = scan(l, "%d %d %f %f");
      if (!has(r, 2)) return fail("rollbar needs two integers; stock would read uninitialised stack");
      c_.rollbars.push_back({r.values[0].i, r.values[1].i, floatOr(r, 2, 10000.0f), floatOr(r, 3, 500.0f)});
      return true;
    }
    if (k == "camera") {  // 00443b00
      if (c_.cameras.size() >= 16) {
        skip("more than 16 cameras; stock skips the line");
        return true;
      }
      auto r = scan(l, "%f %f %f %f %f %f %f");
      if (!has(r, 6)) return fail("camera position and angles are missing; stock would read uninitialised stack");
      CarCamera cam;
      cam.position = {floatOr(r, 0, 0), floatOr(r, 1, 0), floatOr(r, 2, 0)};
      cam.orientation = {floatOr(r, 3, 0), floatOr(r, 4, 0), floatOr(r, 5, 0)};
      cam.fovRaw = floatOr(r, 6, 100.0f);
      c_.cameras.push_back(cam);
      if (c_.cameras.size() == 1) {
        // The first camera also sets default seat, pedal and shifter positions (00443b00 tail).
        const float x = cam.position[0], y = cam.position[1], z = cam.position[2];
        c_.seat = {x, y - 0.55f, z - 0.2f, 0, 0.3f, 0};  // last three: ECX of 00551c70 is not visible, taken as +0x928
        const float py = y - kTenth, pz = z - 0.6f;
        c_.pedals[0] = {x - kTenth, py, pz, 0, 0, 0};
        c_.pedals[1] = {x, py, pz, 0, 0, 0};
        c_.pedals[2] = {x + kTenth, py, pz, 0, 0, 0};
        c_.shifter = {0, -0.1f, -0.1f, 0, 0, 0};
      }
      return true;
    }
    if (k == "ext_camera") {  // 00443d40
      if (c_.drivers.empty())
        return fail("stock writes before the first driver record when no driver/controller line came first");
      auto r = scan(l, "%f %f,%f,%f %f,%f,%f %f,%f,%f %f");
      CarDriver& d = c_.drivers.back();
      for (size_t i = 0; i < 11; ++i) d.extCamera[i] = floatOr(r, i, d.extCamera[i]);
      d.hasExtCamera = true;
      return true;
    }
    if (k == "driver" || k == "controller") {  // 004445f0
      if (c_.drivers.size() >= 4) {
        skip("more than 4 drivers; stock skips the line");
        return true;
      }
      if (c_.drivers.empty()) c_.defaultDriver = 0;
      if (iequals(l.rawKey, "driver")) c_.defaultDriver = static_cast<int>(c_.drivers.size()) - 1;
      auto r = scan(l, "%d %d %d %d %d %d %d %d");
      CarDriver d;
      d.directive = l.key;
      d.converted = r.returned;
      for (size_t i = 0; i < 8; ++i) d.values[i] = intOr(r, i, 0);
      c_.drivers.push_back(d);
      return true;
    }
    if (k == "wing") {  // 004443c0
      if (c_.wings.size() > 16) {
        skip("more than 17 wings; stock skips the line");
        return true;
      }
      auto r = scan(l, "%d %f %f %f %f %f %f %f %f %f %f %f %f %f");
      CarWing w;
      w.line = l.number;
      w.type = intOr(r, 0, 0);
      w.position = {floatOr(r, 1, 0), floatOr(r, 2, 0), floatOr(r, 3, 0)};
      CarVec3 axis{floatOr(r, 4, 0), floatOr(r, 5, 0), floatOr(r, 6, 0)};
      const float length = std::sqrt(axis[0] * axis[0] + axis[2] * axis[2] + axis[1] * axis[1]);
      if (length != 0.0f) {
        const float inverse = 1.0f / length;
        w.axis = {axis[0] * inverse, inverse * axis[1], axis[2] * inverse};
      }
      w.a = -std::fabs(floatOr(r, 7, 0));
      w.b = std::fabs(floatOr(r, 8, 0));
      w.c = floatOr(r, 9, 1.0f);
      c_.wings.push_back(w);
      return true;
    }

    // ---- car-level numbers (00443ef0 .. 00446160) ----
    if (k == "sw_radius" || k == "steeringradius") {  // 00443ef0
      auto r = scan(l, "%f %f");
      c_.steeringRadius[0] = floatOr(r, 0, c_.steeringRadius[0]) * 0.01f;
      c_.steeringRadius[1] = floatOr(r, 1, c_.steeringRadius[1]) * 0.01f;
      return true;
    }
    if (k == "maxsteer") {  // 00444260
      auto r = scan(l, "%f %f");
      c_.maxSteer = {floatOr(r, 0, c_.maxSteer[0]), floatOr(r, 1, c_.maxSteer[1])};
      return true;
    }
    if (k == "rpm_idle") {  // 004442b0: the local defaults to 800
      auto r = scan(l, "%f");
      c_.rpmIdle = floatOr(r, 0, 800.0f);
      return true;
    }
    if (k == "drag") {  // 00444310
      auto r = scan(l, "%f %f %f %f");
      c_.drag = {floatOr(r, 0, 0.3f), floatOr(r, 1, 0), floatOr(r, 2, 0), floatOr(r, 3, 0)};
      return true;
    }
    if (k == "center") {  // 00444570
      auto r = scan(l, "%f %f %f");
      c_.center = {floatOr(r, 0, 0), floatOr(r, 1, 0), floatOr(r, 2, 0)};
      return true;
    }
    if (k == "mass") {  // 00443f50
      auto r = scan(l, "%f");
      c_.centerMass = floatOr(r, 0, c_.centerMass);
      return true;
    }
    if (k == "enginepower") {  // 00443f90
      auto r = scan(l, "%f %f %f %f %f %f");
      for (size_t i = 0; i < 6; ++i) c_.enginePower[i] = floatOr(r, i, c_.enginePower[i]);
      c_.enginePowerCopy = c_.enginePower[0];
      return true;
    }
    if (k == "ratios") {  // 00444080: gear count (+0x1c), then floats at +0x24..+0x3c
      auto r = scan(l, "%d %f %f %f %f %f %f %f");
      c_.gearCount = intOr(r, 0, c_.gearCount);
      for (size_t i = 0; i < 7; ++i) c_.gearRatios[i] = floatOr(r, 1 + i, c_.gearRatios[i]);
      return true;
    }
    if (k == "torque") {  // 004440e0: count, then count pairs "rpm:value" into a 5-entry buffer
      auto head = scan(l, "%#%d");
      const int32_t count = head.values.empty() ? 5 : head.values[0].i;
      if (count < 0 || count > 5) return fail("torque count above 5 would overrun the 5-entry buffer stock allocates");
      std::string rest = l.args.substr(std::min<size_t>(static_cast<size_t>(head.returned), l.args.size()));
      std::vector<std::array<float, 2>> out;
      for (int32_t i = 0; i < count; ++i) {
        auto one = stockScan(rest, "%#%f:%f");
        if (one.values.size() < 2) return fail("torque lists fewer pairs than its count");
        out.push_back({one.values[0].f, one.values[1].f});
        rest = rest.substr(std::min<size_t>(static_cast<size_t>(one.returned), rest.size()));
      }
      c_.torqueCurve = std::move(out);
      c_.torqueSet = true;
      return true;
    }
    if (k == "rearendratio") {  // 00444150
      auto r = scan(l, "%f");
      c_.rearEndRatio = floatOr(r, 0, c_.rearEndRatio);
      return true;
    }
    if (k == "brakepower") {  // 00444190
      auto r = scan(l, "%f %f");
      c_.brakePower = {floatOr(r, 0, c_.brakePower[0]), floatOr(r, 1, c_.brakePower[1])};
      return true;
    }
    if (k == "clutch") {  // 00444200
      auto r = scan(l, "%f");
      c_.clutch = floatOr(r, 0, c_.clutch);
      return true;
    }
    if (k == "seat") {  // 00444740
      auto r = scan(l, "%f %f %f %f %f %f");
      for (size_t i = 0; i < 6; ++i) c_.seat[i] = floatOr(r, i, 0);
      return true;
    }
    if (k == "pedals") {  // 00444810: three copies, outer two shifted by 0.1 in x
      auto r = scan(l, "%f %f %f %f %f %f");
      std::array<float, 6> v{};
      for (size_t i = 0; i < 6; ++i) v[i] = floatOr(r, i, 0);
      c_.pedals = {v, v, v};
      c_.pedals[0][0] = v[0] - kTenth;
      c_.pedals[2][0] = v[0] + kTenth;
      return true;
    }
    if (k == "shifter") {  // 00444990
      auto r = scan(l, "%f %f %f %f %f %f");
      for (size_t i = 0; i < 6; ++i) c_.shifter[i] = floatOr(r, i, 0);
      return true;
    }
    if (k == "key" || k == "handbrake") {  // 00444a60 / 00444ae0: scanned into locals and dropped
      auto r = scan(l, "%f %f %f %f %f %f");
      std::array<float, 6> v{};
      for (size_t i = 0; i < 6; ++i) v[i] = floatOr(r, i, 0);
      (k == "key" ? c_.key : c_.handbrake) = v;
      return true;
    }
    if (k == "cockpit_rpm") return gauge(c_.cockpitRpm, bitsToFloat(0xba03126f));    // 00444b60
    if (k == "cockpit_speed") return gauge(c_.cockpitSpeed, bitsToFloat(0xbca3d70a));  // 00444cb0
    if (k == "name") {  // 00444730 is an empty function
      c_.name = l.args;
      return true;
    }
    if (k == "nodriver" || k == "damagedef") return true;  // 00444710 / 00445ef0 do nothing
    if (k == "notify") {  // 00446160
      c_.notify = true;
      return true;
    }
    if (k == "material") {  // 00445e00
      if (c_.materials.size() >= 16) {
        skip("more than 16 materials; stock skips the line");
        return true;
      }
      auto r = scan(l, "%d %d %d %f");
      CarMaterial m;
      m.system = intOr(r, 0, 0);
      m.a = static_cast<uint32_t>(intOr(r, 1, 0));
      m.b = static_cast<uint32_t>(intOr(r, 2, 0));
      m.value = floatOr(r, 3, 0);
      request(0xe, static_cast<uint32_t>(m.system));
      if (r.values.empty()) {
        c_.resources.back().resource = 3;
        c_.resources.back().literalPack = "system.rpk";
      } else {
        c_.resources.back().runtimeId = true;
      }
      request(6, m.a);
      request(6, m.b);
      c_.materials.push_back(m);
      return true;
    }
    if (k == "velfactor") {  // 00445fc0
      auto r = scan(l, "%f %f");
      c_.velFactor = {floatOr(r, 0, c_.velFactor[0]), floatOr(r, 1, c_.velFactor[1])};
      return true;
    }
    if (k == "turn") {  // 00445f00: stored as -number; untouched values keep their value
      auto r = scan(l, "%f %f %f");
      for (size_t i = 0; i < 3; ++i) {
        const float current = -c_.turn[i];
        c_.turn[i] = -floatOr(r, i, current);
      }
      return true;
    }
    if (k == "roll") {  // 00446000
      auto r = scan(l, "%f %f %f");
      for (size_t i = 0; i < 3; ++i) c_.rollValues[i] = floatOr(r, i, c_.rollValues[i]);
      return true;
    }
    if (k == "pitch") {  // 00446050
      auto r = scan(l, "%f %f %f");
      for (size_t i = 0; i < 3; ++i) c_.pitchValues[i] = floatOr(r, i, c_.pitchValues[i]);
      return true;
    }
    if (k == "steerhelp") {  // 004460a0
      auto r = scan(l, "%f %f %f %f %f %f");
      for (size_t i = 0; i < 6; ++i) c_.steerHelp[i] = floatOr(r, i, c_.steerHelp[i]);
      return true;
    }
    if (k == "cheat_control") {  // 00446100
      auto r = scan(l, "%f %f %f %f %f");
      for (size_t i = 0; i < 5; ++i) c_.cheatControl[i] = floatOr(r, i, c_.cheatControl[i]);
      return true;
    }

    // ---- sounds (00445760 .. 00445d50) ----
    if (k == "sfx_engine_up") return sfxEngine(c_.engineUp, 4);
    if (k == "sfx_engine_down") return sfxEngine(c_.engineDown, 5);
    if (k == "sfx_exhaust") return sfxEngine(c_.exhaust, 5);
    if (k == "sfx_gearbox_fwd" || k == "sfx_gearbox_rev") {  // 00445af0 / 00445b80
      auto r = scan(l, "%d %f");
      const std::pair<uint32_t, float> v{static_cast<uint32_t>(intOr(r, 0, 0)), floatOr(r, 1, 1000.0f)};
      request(6, v.first);
      (k == "sfx_gearbox_fwd" ? c_.gearboxForward : c_.gearboxReverse) = v;
      return true;
    }
    if (k == "sfx_wreck") {  // 00445c10
      auto r = scan(l, "%d");
      c_.wreckSound = static_cast<uint32_t>(intOr(r, 0, 0));
      request(6, *c_.wreckSound);
      return true;
    }
    if (k == "sfx_horn") {  // 00445c80
      auto r = scan(l, "%d %f %d");
      const uint32_t id = static_cast<uint32_t>(intOr(r, 0, 0));
      const float volume = floatOr(r, 1, 1.0f);
      const int32_t index = intOr(r, 2, 1);
      request(6, id);
      if (index < 0) return fail("negative horn index; stock writes before its 4-entry table");
      c_.hornVolume[static_cast<size_t>(index & 3)] = volume;
      return true;
    }
    if (k == "sfx_gearchange") {  // 00445d50
      auto r = scan(l, "%d %d");
      c_.gearChangeSounds = std::make_pair(static_cast<uint32_t>(intOr(r, 0, 0)), static_cast<uint32_t>(intOr(r, 1, 0)));
      request(6, c_.gearChangeSounds->first);
      request(6, c_.gearChangeSounds->second);
      return true;
    }

    // ---- on-screen display (00444e00 .. 004456d0) ----
    for (const auto& spec : kOsdSpecs)
      if (k == spec.name) return osd(spec);
    if (k == "osd_damage_slot") {  // 004450e0: reads one %c into a local and returns
      auto r = scan(l, "%c");
      CarOsdElement e;
      e.directive = k;
      e.converted = r.returned;
      if (!r.values.empty()) e.text = r.values[0].s;
      c_.osd.push_back(std::move(e));
      return true;
    }

    return fail("directive is in the table but its handler is not read yet (handler " +
                [&] {
                  char buf[16];
                  std::snprintf(buf, sizeof buf, "%08x", findCarDirective(k)->handler);
                  return std::string(buf);
                }() + ")");
  }
};

}  // namespace

bool parseCarConfig(std::string_view text, CarConfig& config, std::string& error) {
  config = CarConfig{};
  std::vector<CarConfigLine> lines;
  if (!tokenizeCarConfig(text, lines, error)) return false;
  Parser parser(config);
  for (const auto& line : lines) {
    std::string reason;
    if (!parser.apply(line, reason)) {
      char buf[16] = "";
      if (const auto* info = findCarDirective(line.key)) std::snprintf(buf, sizeof buf, "%08x", info->handler);
      error = "line " + std::to_string(line.number) + ": directive '" + line.key + "' (handler " + buf + "): " + reason;
      return false;
    }
  }
  error.clear();
  return true;
}
