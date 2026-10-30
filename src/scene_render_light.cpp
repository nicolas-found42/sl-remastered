#include "scene_render_light.hpp"

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
