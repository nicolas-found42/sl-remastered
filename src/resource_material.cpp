#include "resource_material.hpp"

#include <charconv>
#include <cmath>
#include <sstream>
#include <string_view>

namespace {
bool parseId(std::string_view value, uint32_t& result) {
  const bool hex = value.size() >= 2 && value[0] == '0' &&
                   (value[1] == 'x' || value[1] == 'X');
  const char* first = value.data() + (hex ? 2 : 0);
  const char* last = value.data() + value.size();
  const auto parsed = std::from_chars(first, last, result, hex ? 16 : 10);
  return parsed.ec == std::errc{} && parsed.ptr == last;
}
bool parseFloat(std::string_view value, float& result) {
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  return parsed.ec == std::errc{} &&
         parsed.ptr == value.data() + value.size() && std::isfinite(result);
}
bool parseFlags(std::string_view value, uint32_t& result) {
  // Stock scans flags as an integer prefix, even when the payload spells
  // the value with a decimal suffix (for example, "4096.000").
  const bool negative = !value.empty() && value.front() == '-';
  const bool hex = !negative && value.size() >= 2 && value[0] == '0' &&
                   (value[1] == 'x' || value[1] == 'X');
  const char* first = value.data() + (negative ? 1 : hex ? 2
                                                         : 0);
  const char* last = value.data() + value.size();
  uint32_t magnitude = 0;
  const auto parsed = std::from_chars(first, last, magnitude, hex ? 16 : 10);
  if (parsed.ec != std::errc{} || parsed.ptr == first)
    return false;
  result = negative ? uint32_t{0} - magnitude : magnitude;
  return true;
}
} // namespace

ResourceMaterialResult resourceMaterialParse(const PackResource& resource) {
  ResourceMaterialResult out;
  if (resource.entry.resType != 14 || !resource.packBytes) {
    out.error = "expected type-14 resource payload";
    return out;
  }
  const auto& bytes = *resource.packBytes;
  const size_t offset = resource.entry.payloadOffset,
               size = resource.entry.payloadSize;
  if (offset > bytes.size() || size > bytes.size() - offset || size > 65536) {
    out.error = "invalid material payload bounds";
    return out;
  }
  std::string payload(bytes.begin() + offset, bytes.begin() + offset + size);
  if (!payload.empty() && payload.back() == '\0')
    payload.pop_back();
  if (payload.find('\0') != std::string::npos) {
    out.error = "embedded NUL in material payload";
    return out;
  }
  std::istringstream lines(payload);
  std::string line;
  bool mesh = false, flags = false, particle = false, skidmark = false;
  bool lodBias = false, lodAmplitude = false;
  bool bones = false, skeleton = false;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string key, value, extra;
    if (!(fields >> key))
      continue;
    if (key.rfind("shd_", 0) == 0) {
      // 00549xxx material scan: shd_texture %d, shd_vbase/vup/center/diru/dirv
      // %f %f %f, shd_radius %f, shd_uv %f %f %f %f.
      size_t count = 0;
      if (key == "shd_texture" || key == "shd_radius")
        count = 1;
      else if (key == "shd_vbase" || key == "shd_vup" || key == "shd_center" ||
               key == "shd_diru" || key == "shd_dirv")
        count = 3;
      else if (key == "shd_uv")
        count = 4;
      else {
        out.error = "unsupported or invalid material directive";
        return out;
      }
      std::vector<std::string> tokens;
      std::string token;
      while (fields >> token)
        tokens.push_back(token);
      if (tokens.size() != count) {
        out.error = "invalid material line: \"" + line + "\"";
        return out;
      }
      if (key == "shd_texture") {
        if (!parseId(tokens[0], out.shadowTexture)) {
          out.error = "invalid material texture";
          return out;
        }
        out.hasShadowTexture = true;
        continue;
      }
      std::vector<float> values(count);
      for (size_t i = 0; i < count; ++i)
        if (!parseFloat(tokens[i], values[i])) {
          out.error = "invalid material line: \"" + line + "\"";
          return out;
        }
      out.shadow.emplace_back(key, std::move(values));
      continue;
    }
    if (!(fields >> value) || (fields >> extra)) {
      out.error = "invalid material line: \"" + line + "\"";
      return out;
    }
    if (key == "mesh" && !mesh && parseId(value, out.mesh) && out.mesh)
      mesh = true;
    else if (key == "texture") {
      uint32_t texture = 0;
      if (!parseId(value, texture)) {
        out.error = "invalid material texture";
        return out;
      }
      out.textures.push_back(texture);
    } else if (key == "flags" && !flags && parseFlags(value, out.flags))
      flags = true;
    else if (key == "lod_bias" && !lodBias && parseFloat(value, out.lodBias))
      lodBias = true;
    else if (key == "lod_amp" && !lodAmplitude &&
             parseFloat(value, out.lodAmplitude))
      lodAmplitude = true;
    else if (key == "particle" && !particle) {
      out.particle = value;
      particle = true;
    } else if (key == "skidmark" && !skidmark) {
      out.skidmark = value;
      skidmark = true;
    } else if (key == "numbones" && !bones && parseId(value, out.bones)) {
      bones = true;
    } else if (key == "skeleton" && !skeleton) {
      out.skeleton = value;
      skeleton = true;
    } else {
      out.error = "unsupported or invalid material directive";
      return out;
    }
  }
  // Stock materials may omit "texture" entirely (e.g. 06x5_shop_uveg):
  // untextured meshes keep vertex or material colors.
  if (!mesh) {
    out.error = "incomplete material payload";
    return out;
  }
  out.ok = true;
  return out;
}
