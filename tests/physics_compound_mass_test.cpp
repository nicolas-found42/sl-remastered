#include "physics_compound_mass.hpp"

#include <cassert>
#include <cmath>

using namespace physics;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

int main() {
  BodyProto a;
  a.invMass = 0.5f;
  a.inertia = matrixIdentity();
  BodyProto b = a;
  b.invMass = 1.0f;
  b.inertia[0] = 2;
  b.inertia[5] = 3;
  b.inertia[10] = 4;
  Mat34 quarterTurn = matrixIdentity();
  quarterTurn[0] = 0; quarterTurn[1] = -1;
  quarterTurn[4] = 1; quarterTurn[5] = 0;
  std::vector<CompoundMassChild> children{{a, {0, 0, 0}, matrixIdentity()},
                                           {b, {3, 0, 0}, quarterTurn}};
  const auto first = aggregateCompoundMass(children);
  auto second = aggregateCompoundMass(children);
  assert(near(first.center[0], 1));
  assert(near(first.prototype.invMass, 1.0f / 3));
  assert(near(first.prototype.inertia[0], 4));
  assert(near(first.prototype.inertia[5], 9));
  assert(near(first.prototype.inertia[10], 11));
  assert(near(first.prototype.invInertia[0], 0.25f));
  second.prototype.inertia[0] = 99;
  assert(near(first.prototype.inertia[0], 4));
  assert(near(children[0].prototype.inertia[0], 1));
  const auto empty = aggregateCompoundMass({});
  assert(empty.prototype.invMass == 0 && (empty.prototype.flags & kProtoStatic));
}
