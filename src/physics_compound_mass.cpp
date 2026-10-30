#include "physics_compound_mass.hpp"

namespace physics {
namespace {
void shiftInertia(Mat34& inertia, const Vec3& d, float mass) {
  const float yy = d[1] * d[1];
  const float xx = d[0] * d[0];
  inertia[0] = (yy + d[2] * d[2]) * mass + inertia[0];
  inertia[5] = (xx + d[2] * d[2]) * mass + inertia[5];
  inertia[10] = (xx + yy) * mass + inertia[10];
  inertia[1] = inertia[4] - d[0] * d[1] * mass;
  inertia[6] = inertia[9] - d[1] * d[2] * mass;
  inertia[2] = inertia[2] - d[0] * d[2] * mass;
  inertia[4] = inertia[1];
  inertia[8] = inertia[2];
  inertia[9] = inertia[6];
}
}

CompoundMass aggregateCompoundMass(const std::vector<CompoundMassChild>& children) {
  CompoundMass result;
  float total = 0;
  Mat34 inertia{};
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    const auto& child = *it;
    if (child.prototype.invMass == 0) continue;
    const float mass = 1.0f / child.prototype.invMass;
    const float invTotal = 1.0f / (mass + total);
    Vec3 next{};
    for (size_t i = 0; i < 3; ++i)
      next[i] = (mass * child.position[i] + result.center[i] * total) * invTotal;
    Vec3 oldOffset{}, childOffset{};
    for (size_t i = 0; i < 3; ++i) {
      oldOffset[i] = next[i] - result.center[i];
      childOffset[i] = next[i] - child.position[i];
    }
    shiftInertia(inertia, oldOffset, total);
    Mat34 rotated = matrixMultiply(child.rotation,
        matrixMultiply(child.prototype.inertia, matrixTranspose(child.rotation)));
    shiftInertia(rotated, childOffset, mass);
    for (size_t i : {size_t{0}, size_t{1}, size_t{2}, size_t{4}, size_t{5},
                     size_t{6}, size_t{8}, size_t{9}, size_t{10}})
      inertia[i] = rotated[i] + inertia[i];
    result.center = next;
    total = mass + total;
  }
  if (total <= 0) {
    result.prototype.flags |= kProtoStatic;
  } else {
    result.prototype.invMass = 1.0f / total;
    result.prototype.inertia = inertia;
    result.prototype.invInertia = matrixInverse(inertia);
    result.prototype.linearDrag = total * k::linearDragPerMass;
    result.prototype.angularDrag = total * k::angularDragPerMass;
  }
  return result;
}
}  // namespace physics
