#include "physics_compound.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdint>

using namespace physics;

static bool near(float a, float b) { return std::fabs(a - b) < 0.00001f; }

int main() {
  State root;
  root.pos = {10, 20, 30};
  root.time = 4;
  const float h = std::sqrt(0.5f);
  root.quat = {0, 0, h, h};
  root.rot = matrixFromQuat(root.quat);
  RelativeBodyPose relative{{2, 0, 3}, {0, h, 0, h}};
  State child;
  child.vel = {7, 8, 9};
  predictChildPose(root, relative, child);
  assert(near(child.pos[0], 10) && near(child.pos[1], 22) && near(child.pos[2], 33));
  assert(near(child.quat[0], -0.5f) && near(child.quat[1], 0.5f));
  assert(near(child.quat[2], 0.5f) && near(child.quat[3], 0.5f));
  assert(child.time == 4 && child.vel == Vec3({7, 8, 9}));
  State grandchild;
  predictChildPose(child, RelativeBodyPose{{0, 0, 1}, {0, 0, 0, 1}}, grandchild);
  assert(near(grandchild.pos[0], 10) && near(grandchild.pos[1], 23) && near(grandchild.pos[2], 33));
  Body parentBody, attachedBody;
  parentBody.state[0].pos = {2,3,4};
  attachedBody.state[0].pos = {4,5,6};
  parentBody.state[0].quat = {0.1f,0.2f,0.3f,0.4f};
  attachedBody.state[0].quat = {0,0,0.70710677f,0.70710677f};
  parentBody.state[0].rot = matrixFromQuat(parentBody.state[0].quat);
  attachedBody.state[0].rot = matrixFromQuat(attachedBody.state[0].quat);
  assert(attachBody(attachedBody, parentBody));
  const uint32_t expectedPosition[]{0x3feb851f,0x3fccccce,0x40147ae1};
  const uint32_t expectedQuaternion[]{0xbf3504f3,0xbe715be8,0x3e715be8,0x3fd33071};
  const auto& pose = attachedBody.attachment->relative;
  for (size_t i = 0; i < 3; ++i) {
    uint32_t word; std::memcpy(&word, &pose.position[i], 4);
    assert(word == expectedPosition[i]);
  }
  for (size_t i = 0; i < 4; ++i) {
    uint32_t word; std::memcpy(&word, &pose.orientation()[i], 4);
    assert(word == expectedQuaternion[i]);
  }
}
