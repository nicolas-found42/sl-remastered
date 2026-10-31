#include "scene_render_light.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <string>

#include "pack_registry.hpp"

namespace {

void rgbIntToFloats(uint32_t rgb, std::array<float, 3> &out) {
  constexpr float kScale = 0.00390625f;
  out[0] = static_cast<float>((rgb >> 16) & 0xff) * kScale;
  out[1] = static_cast<float>((rgb >> 8) & 0xff) * kScale;
  out[2] = static_cast<float>(rgb & 0xff) * kScale;
}

} // namespace

SceneRenderLight SceneRenderLight::stockDefaults() {
  SceneRenderLight light;
  rgbIntToFloats(0xffffff, light.at10);
  rgbIntToFloats(0x404040, light.at30);
  rgbIntToFloats(0xffffff, light.at20);
  return light;
}

void SceneRenderLight::applySetLight(uint32_t first, uint32_t second,
                                     uint32_t third) {
  rgbIntToFloats(first, at10);
  rgbIntToFloats(second, at30);
  rgbIntToFloats(third, at20);
}

namespace {
// Light payloads format integer fields as floats ("2.000") and reference
// resources in decimal or hex. Stock reads numbers with atof-grade
// conversion; mirror that with token-based strtod/strtoll.
bool parseLightNumber(std::istringstream &fields, double &out) {
  std::string text;
  if (!(fields >> text))
    return false;
  char *end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (!end || end == text.c_str() || *end != '\0' || !std::isfinite(value))
    return false;
  out = value;
  return true;
}

bool parseLightInt(std::istringstream &fields, int32_t &out) {
  double value = 0;
  if (!parseLightNumber(fields, value) ||
      value < static_cast<double>(INT32_MIN) ||
      value > static_cast<double>(INT32_MAX))
    return false;
  out = static_cast<int32_t>(value);
  return true;
}

bool parseLightResourceId(std::istringstream &fields, uint32_t &out) {
  std::string text;
  if (!(fields >> text))
    return false;
  char *end = nullptr;
  const long long value = std::strtoll(text.c_str(), &end, 0);
  if (!end || end == text.c_str() || (*end != '\0' && *end != '.') ||
      value < INT32_MIN || value > 0xffffffffll)
    return false;
  out = static_cast<uint32_t>(value);
  return true;
}
} // namespace

SceneLightParseResult sceneParseLight(const PackResource &resource) {
  SceneLightParseResult result;
  if (resource.entry.resType != 13 || !resource.packBytes) {
    result.error = "expected type-13 resource payload";
    return result;
  }
  const auto &bytes = *resource.packBytes;
  const size_t offset = resource.entry.payloadOffset;
  const size_t size = resource.entry.payloadSize;
  if (offset > bytes.size() || size > bytes.size() - offset) {
    result.error = "invalid light payload bounds";
    return result;
  }
  std::string payload(bytes.begin() + offset, bytes.begin() + offset + size);
  if (!payload.empty() && payload.back() == '\0')
    payload.pop_back();
  if (payload.find('\0') != std::string::npos) {
    result.error = "embedded NUL in light payload";
    return result;
  }
  auto &light = result.light;
  // FUN_005480e0 initializes all three colors to white before parsing.
  light.at10 = light.at20 = light.at30 = {1, 1, 1};
  std::istringstream lines(payload);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string key, extra;
    if (!(fields >> key))
      continue;
    bool parsed = true;
    if (key == "type") {
      std::string kind;
      parsed = bool(fields >> kind);
      if (kind == "point")
        light.sourceKind = 1;
      else if (kind == "spot")
        light.sourceKind = 2;
      else if (kind == "directional")
        light.sourceKind = 3;
      else if (kind == "fakepoint")
        light.sourceKind = 4;
      else if (kind == "fakespot")
        light.sourceKind = 5;
      else
        parsed = false;
    } else if (key == "diffuse" || key == "specular" || key == "ambient") {
      auto &rgb = key == "diffuse"    ? light.at10
                  : key == "specular" ? light.at20
                                      : light.at30;
      float &alpha = key == "diffuse"    ? light.diffuseAlpha
                     : key == "specular" ? light.specularAlpha
                                         : light.ambientAlpha;
      parsed = bool(fields >> alpha >> rgb[0] >> rgb[1] >> rgb[2]);
    } else if (key == "position" || key == "direction" ||
               key == "attenuation") {
      auto &v = key == "position"    ? light.position
                : key == "direction" ? light.direction
                                     : light.attenuation;
      parsed = bool(fields >> v[0] >> v[1] >> v[2]);
    } else if (key == "plane") {
      parsed = bool(fields >> light.plane[0] >> light.plane[1] >>
                    light.plane[2] >> light.plane[3]);
    } else if (key == "range")
      parsed = bool(fields >> light.range);
    else if (key == "falloff")
      parsed = bool(fields >> light.falloff);
    else if (key == "theta")
      parsed = bool(fields >> light.theta);
    else if (key == "phi")
      parsed = bool(fields >> light.phi);
    else if (key == "lit_dynamic") {
      int32_t value = 1;
      parsed = bool(fields >> value);
      light.litDynamic = value != 0;
    } else if (key == "iscolorable")
      light.isColorable = true;
    else if (key == "attH_texture" || key == "attV_texture") {
      uint32_t id = 0;
      parsed = parseLightResourceId(fields, id);
      (key == "attH_texture" ? light.attHTextureId : light.attVTextureId) =
          static_cast<int32_t>(id);
    } else if (key == "lit_sobj")
      parsed = parseLightInt(fields, light.litSobj);
    else if (key == "lit_dobj")
      parsed = parseLightInt(fields, light.litDobj);
    else if (key == "shd_sc_sr")
      parsed = parseLightInt(fields, light.shdScSr);
    else if (key == "shd_sc_dr")
      parsed = parseLightInt(fields, light.shdScDr);
    else if (key == "shd_dc_sr")
      parsed = parseLightInt(fields, light.shdDcSr);
    else if (key == "shd_dc_dr")
      parsed = parseLightInt(fields, light.shdDcDr);
    else if (key.compare(0, 3, "lf_") == 0) {
      if (!light.flare) {
        light.flare.emplace();
        light.flare->atD8 = 10;
        light.flare->wordE0 = static_cast<int32_t>(0xffff00ffu);
      }
      auto &flare = *light.flare;
      if (key == "lf_texture") {
        uint32_t id = 0;
        parsed = parseLightResourceId(fields, id);
        flare.textureId = static_cast<int32_t>(id);
      } else if (key == "lf_glow_offset")
        parsed = bool(fields >> flare.glowOffset);
      else if (key == "lf_texturemode")
        parsed = parseLightInt(fields, flare.textureMode);
      else if (key == "lf_minNdotV")
        parsed = bool(fields >> flare.minNdotV);
      else if (key == "lf_useNdotH")
        parsed = parseLightInt(fields, flare.useNdotH);
      else if (key == "lf_glow_color") {
        std::array<float, 4> color{};
        parsed = bool(fields >> color[0] >> color[1] >> color[2] >> color[3]);
        uint32_t packed = 0;
        for (float component : color) {
          const double scaled = static_cast<double>(component) * 255.0;
          if (!std::isfinite(scaled) ||
              scaled < std::numeric_limits<int32_t>::min() ||
              scaled > std::numeric_limits<int32_t>::max()) {
            parsed = false;
            break;
          }
          packed = (packed << 8) |
                   static_cast<uint32_t>(static_cast<int32_t>(scaled));
        }
        flare.wordE0 = static_cast<int32_t>(packed);
      } else if (key == "lf_glow_minsize")
        parsed = bool(fields >> flare.atD4);
      else if (key == "lf_glow_maxsize" || key == "lf_glow_size")
        parsed = bool(fields >> flare.atD8);
      else if (key == "lf_glow_colortype")
        parsed = parseLightInt(fields, flare.colorType);
      else if (key == "lf_ray_count")
        parsed = parseLightInt(fields, flare.atE4);
      else if (key == "lf_ray_minwidth")
        parsed = bool(fields >> flare.rayMinWidth);
      else if (key == "lf_ray_maxwidth")
        parsed = bool(fields >> flare.rayMaxWidth);
      else if (key == "lf_ray_minlenght")
        parsed = bool(fields >> flare.rayMinLength);
      else if (key == "lf_ray_maxlenght")
        parsed = bool(fields >> flare.rayMaxLength);
      else if (key == "lf_ray_firstangle")
        parsed = bool(fields >> flare.rayFirstAngle);
      else if (key == "lf_ray_rotating")
        parsed = bool(fields >> flare.rayRotating);
      else if (key == "lf_flare_count")
        parsed = parseLightInt(fields, flare.at100);
      else if (key == "lf_flare_minsize")
        parsed = bool(fields >> flare.flareMinSize);
      else if (key == "lf_flare_maxsize")
        parsed = bool(fields >> flare.flareMaxSize);
      else if (key == "lf_flare_distance")
        parsed = bool(fields >> flare.flareDistance);
      else {
        result.error = "unsupported light directive: " + key;
        return result;
      }
    } else {
      result.error = "unsupported light directive: " + key;
      return result;
    }
    if (!parsed || (fields >> extra)) {
      result.error = "invalid light directive: " + key;
      return result;
    }
  }
  result.ok = true;
  return result;
}
