#include "resource_extp_bounds.hpp"
#include <cmath>

namespace {
bool valid(const ResourceBounds &box) {
  if (box.mode != 1) return false;
  for (float v : box.values) if (!std::isfinite(v)) return false;
  for (size_t i=3;i<6;++i) if (box.values[i]<0) return false;
  return true;
}
}
std::optional<ResourceBounds> expandExtpBounds(const ResourceBounds &parent,
                                               const std::vector<ResourceBounds> &children) {
  if (!valid(parent)) return {};
  auto result=parent;
  for (const auto &child : children) {
    if (!valid(child)) return {};
    for (size_t axis=0;axis<3;++axis) {
      const float center=result.values[axis], childCenter=child.values[axis];
      const float radius=child.values[axis+3];
      float &extent=result.values[axis+3];
      if (childCenter-radius < center-extent) extent=-((childCenter-radius)-center);
      if (extent+center < radius+childCenter) extent=(radius+childCenter)-center;
      if (!std::isfinite(extent)) return {};
    }
  }
  result.mode=1;
  return result;
}
