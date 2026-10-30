#include "physics_compound.hpp"


namespace physics {

bool attachBody(Body& child, Body& parent) {
  for (auto* ancestor = &parent; ancestor;
       ancestor = ancestor->attachment ? ancestor->attachment->parent : nullptr)
    if (ancestor == &child) return false;
  const auto& root = parent.state[parent.current];
  const auto& state = child.state[child.current];
  const Vec3 delta{state.pos[0] - root.pos[0], state.pos[1] - root.pos[1], state.pos[2] - root.pos[2]};
  const Vec3 position{
    root.rot[4]*delta[1] + root.rot[8]*delta[2] + delta[0]*root.rot[0],
    root.rot[5]*delta[1] + root.rot[1]*delta[0] + root.rot[9]*delta[2],
    root.rot[6]*delta[1] + root.rot[2]*delta[0] + root.rot[10]*delta[2]};
  const Quat inverse = quatInverse(root.quat);
  child.attachment = BodyAttachment{&parent, {position, quatMultiply(inverse, state.quat)}};
  child.flags |= kBodyChild;
  parent.flags |= kBodyHasChildren;
  return true;
}

void predictChildPose(const State& parent, const RelativeBodyPose& relative, State& out) {
  const Vec3 offset = matrixTransformRows(parent.rot, relative.position);
  for (std::size_t i = 0; i < 3; ++i) out.pos[i] = parent.pos[i] + offset[i];
  out.quat = quatMultiply(parent.quat, relative.orientation());
  out.rot = matrixFromQuat(out.quat);
  out.time = parent.time;
}

}  // namespace physics

