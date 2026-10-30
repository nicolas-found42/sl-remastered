#include "ground_runtime.hpp"

#include <cassert>

int main() {
  auto ground = createGroundRuntime({12, -3, 7}, 0.75f, 4);
  assert(ground->body.proto == &ground->prototype);
  assert(ground->prototype.shape.type == physics::ShapeType::Empty);
  assert(!ground->prototype.shape.core);
  assert(ground->prototype.flags & physics::kProtoStatic);
  assert(ground->prototype.invMass == 0);
  assert((ground->body.flags & physics::kBodyActive) == 0);
  assert(ground->prototype.shape.friction == 0.75f);
  assert(ground->materialField54 == 0.01f);
  assert(ground->prototype.shape.bounce == 0.2f);
  for (const auto& state : ground->body.state) {
    assert((state.pos == physics::Vec3{12, -3, 7}));
    assert(state.rot == physics::matrixIdentity());
    assert(state.time == 4);
  }
  assert(createGroundRuntime({}, 0)->prototype.shape.friction == 1);
  assert(createGroundRuntime({}, -2)->prototype.shape.friction == 1);
  GroundPreparation missing;
  std::string error;
  assert(!groundContactMaterial(missing, 0, error) && !error.empty());
}
