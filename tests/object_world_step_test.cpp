// Stepped dynamic scene objects: an awake object instance (00465bd0 compound
// of a body slot under an aggregate root) placed above the synthetic ground
// sector must fall, settle on the ground and retain body/ground-face pairs
// under the shared multi-root solver -- 004a5190 steps every
// registered root through 004a3f90/004a3bc0 and 004a4ed0. Stock sleep/wake
// force support must satisfy the stock rest gate and collision wake. A
// massless (static) instance must stay frozen instead. A failed step (ground
// dropped) must leave the object collector transactionally intact. Fails on
// the old code, which left awake objects at their placed pose forever.
#include "car_physics.hpp"
#include "car_runtime.hpp"
#include "car_world_step.hpp"
#include "game_creation.hpp"
#include "ground_runtime.hpp"
#include "object_config.hpp"
#include "object_physics.hpp"
#include "pack_registry.hpp"
#include "physics_body.hpp"
#include "resource_world.hpp"
#include "world_model.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

int main() {
  // Same pack layout and synthetic ground sector as car_world_step_test: the
  // installed 2005 tree, one city sector tiled with 1 m ground triangles.
  PackRegistry packs(
      ".scratch/2005-extract/Program Files/Street Legal Racing - Redline");
  GameWorld world;
  assert(world.addRoot());
  world.setClassResolver([](const std::string&) { return true; });
  ResourceWorld resources(world, packs);
  std::string error;
  auto resolveId = [&](const char* pack, uint32_t entry) {
    uint32_t id = 0;
    assert(packs.resolveLiteral(pack, entry, id, error));
    return id;
  };

  const uint32_t cityId = resolveId("maps\\city.rpk", 1);
  const auto city = resources.lookup(cityId, 1);
  assert(city.status == LookupStatus::Found && city.node.packNode);
  ResourceNode* sectorNode = city.node.packNode;
  if (sectorNode->parent && !sectorNode->parent->loaded())
    assert(resources.loadEmptyGameParent(*sectorNode->parent, error));

  const auto establishGround = [&]() {
    sectorNode->stateWord = (sectorNode->stateWord & ~uint32_t{0x50}) | 0x21;
    sectorNode->bounds = ResourceBounds{};
    sectorNode->bounds->mode = 1;
    sectorNode->bounds->values = {0, 0, 0, 24, 24, 24};
    setLinkOwner(sectorNode->sectorLink, sectorNode);
    auto payload = std::make_shared<PreparedSpatialGamePayload>();
    GamePhysicsGeometry geometry;
    constexpr float lo = -16.0f, hi = 16.0f, step = 1.0f;
    for (float z = lo; z < hi; z += step)
      for (float x = lo; x < hi; x += step) {
        const uint32_t base = static_cast<uint32_t>(geometry.vertices.size());
        geometry.vertices.push_back({x, 0, z});
        geometry.vertices.push_back({x + step, 0, z});
        geometry.vertices.push_back({x + step, 0, z + step});
        geometry.vertices.push_back({x, 0, z + step});
        GamePhysicsTriangle tri;
        tri.normal = {0, 1, 0};
        tri.materialWord = 0;
        tri.vertices = {base, base + 2, base + 1};
        geometry.triangles.push_back(tri);
        tri.vertices = {base, base + 3, base + 2};
        geometry.triangles.push_back(tri);
      }
    payload->physics = std::move(geometry);
    sectorNode->spatialPayload = std::move(payload);
    auto factory = std::make_shared<GroundFactoryResult>();
    auto preparation = std::make_shared<GroundPreparation>();
    preparation->gravity = -physics::k::gravityY;
    preparation->files.materials = GroundConfig{};
    GroundMaterial material;
    material.name = "asphalt";
    material.friction = 1.0f;
    material.rollingResistance = 0.01f;
    material.collisionCoefficient = 1.0f;
    preparation->files.materials->materials.push_back(material);
    static std::shared_ptr<GroundRuntime> ground;
    static WorldNode* groundBodyNode = nullptr;
    if (!ground) {
      auto created = createGroundRuntime({0, 0, 0}, 1.0f, 0);
      assert(created);
      ground = std::shared_ptr<GroundRuntime>(std::move(created));
      groundBodyNode = world.addTypedChildUnderExternal(
          sectorNode->handle, *sectorNode, 1, sectorNode->sectorLink.owner, 2,
          "groundbody", 0x201);
      assert(groundBodyNode);
      groundBodyNode->groundBody = ground;
      groundBodyNode->flags |= 0x200000u;
      world.noteInstanceCreated(*groundBodyNode);
      world.markLoaded(*groundBodyNode);
      groundBodyNode->loadPriority = 1.0f;
    }
    // Retained pairs name this runtime's bodies; it is created once and
    // never replaced, only its sector factory is rebuilt on retry.
    preparation->physics = ground;
    setLinkOwner(preparation->bodyResource, groundBodyNode);
    setLinkOwner(factory->city, sectorNode);
    factory->preparation = preparation;
    sectorNode->groundFactory = std::move(factory);
  };
  establishGround();

  // A registered car so the step takes the stepRegisteredCars path.
  const uint32_t carScriptId = resolveId("cars\\racers\\Nonus.rpk", 6);
  auto* script =
      world.addScript(carScriptId, "java.game.cars.Emer_Nonus_Street_GT");
  assert(script);
  assert(loadCarScriptPrototype(resources, *script,
                                "cars\\racers\\Nonus_data\\scripts\\"
                                "_main_Nonus_Street_GT.cfg",
                                error));
  assert(script->car);
  auto car = buildDetachedCarPhysics(*script->car, "0,0,0,0,0,0,0", 0,
                                     {0, physics::k::gravityY, 0}, error);
  assert(car && error.empty());
  auto* carNode = world.addTypedChild(*world.defaultParent(), 1, "car", 0);
  assert(carNode);
  carNode->nativeDevice = "car";
  carNode->carInstance = std::make_unique<CarRuntimeInstance>();
  carNode->carInstance->isCar = true;
  carNode->carInstance->carPhysics =
      std::shared_ptr<CarPhysics>(std::move(car));
  setLinkOwner(carNode->carInstance->prototypeSource, script);
  world.noteInstanceCreated(*carNode);
  world.markLoaded(*carNode);
  assert(world.registerCarPhysics(*carNode, carNode->carInstance->carPhysics,
                                  error));
  // Keep the complete sleeping car tree outside the drop fixture. Moving
  // only its root would leave sleeping child collision poses stale.
  for (auto* body : carNode->carInstance->carPhysics->teardownOrder)
    for (auto& state : body->state) { state.pos[0] -= 2.5f; state.pos[1] += 2.5f; }
  resetCarToRest(*carNode->carInstance->carPhysics);

  // The object under test: one dynamic box slot under an aggregate root
  // (00465bd0), spawned 2.5 m above the plane at (2.5, 2.5, 0). The body
  // pointer is kept so the test can force a contact wake later.
  auto objectPhysics = object::ObjectPhysics::create();
  object_config::ObjectSlot slot;
  slot.kind = object_config::SlotKind::Body;
  slot.shape.kind = object_config::ShapeKind::Box;
  slot.shape.mass = 20;
  slot.shape.params = {0.25f, 0.25f, 0.25f};
  assert(objectPhysics->addBody(slot, 0, nullptr, error) == 0 && error.empty());
  auto instance =
      objectPhysics->instantiate({2.5f, 2.5f, 0.0f}, {0, 0, 0}, error);
  assert(instance.has_value() && error.empty());
  physics::Body* boxRoot = instance->root.get();
  const auto boxBodies = instance->tree; // world ownership keeps these stable after sleep
  const float spawnY = boxRoot->state[boxRoot->current].pos[1];
  assert(std::fabs(spawnY - 2.5f) < 1e-4f);
  auto* objectNode = world.addTypedChild(*world.defaultParent(), 1, "cone", 0);
  assert(objectNode);
  assert(world.registerObjectPhysics(
      *objectNode, object::adoptInstance(std::move(*instance))));

  // A second, massless instance: static proto (kProtoStatic), must stay put.
  auto staticPhysics = object::ObjectPhysics::create();
  object_config::ObjectSlot staticSlot;
  staticSlot.kind = object_config::SlotKind::Body;
  staticSlot.shape.kind = object_config::ShapeKind::Box;
  staticSlot.shape.mass = 0;
  staticSlot.shape.params = {0.25f, 0.25f, 0.25f};
  assert(staticPhysics->addBody(staticSlot, 0, nullptr, error) == 0);
  auto staticInstance =
      staticPhysics->instantiate({-2.5f, 0.25f, 0.0f}, {0, 0, 0}, error);
  assert(staticInstance.has_value());
  physics::Body* rockRoot = staticInstance->root.get();
  const auto rockSpawn = rockRoot->state[rockRoot->current].pos;
  auto* rockNode = world.addTypedChild(*world.defaultParent(), 1, "rock", 0);
  assert(rockNode);
  assert(world.registerObjectPhysics(
      *rockNode, object::adoptInstance(std::move(*staticInstance))));

  CarWorldStepper stepper(world, resources);
  float lowestY = spawnY;
  size_t maxObjectPairs = 0;
  for (int i = 0; i < 900; ++i) {
    if (!stepper.step(1.0f / 60.0f, error)) {
      const auto& failed = boxRoot->state[boxRoot->current];
      std::printf("step %d failed: %s pos=(%g,%g,%g) vel=(%g,%g,%g) force=(%g,%g,%g)\n", i, error.c_str(), failed.pos[0],failed.pos[1],failed.pos[2],failed.vel[0],failed.vel[1],failed.vel[2],failed.force[0],failed.force[1],failed.force[2]);
      return 1;
    }
    error.clear();
    const auto& stepped = world.steppedObjectPhysics();
    if (stepped.size() == 1) {
      assert(stepped.front()->root() == boxRoot);
      const auto& st = boxRoot->state[boxRoot->current];
      lowestY = std::min(lowestY, st.pos[1]);
      maxObjectPairs =
          std::max(maxObjectPairs, stepper.debugObjectPairCount());
    } else {
      // 004a3f90/004a3bc0 skip the tree only once it fell asleep.
      assert(stepped.empty());
      assert((boxRoot->flags & physics::kBodyAsleep) != 0);
    }
  }
  // The box fell from its placed pose and rests on the plane: the old frozen
  // behaviour never moved it.
  const auto& finalState = boxRoot->state[boxRoot->current];
  const float speed = std::sqrt(finalState.vel[0] * finalState.vel[0] +
                                finalState.vel[1] * finalState.vel[1] +
                                finalState.vel[2] * finalState.vel[2]);
  std::printf("box: lowestY=%.3f finalY=%.3f vel=%.3f pairs=%zu\n", lowestY,
              finalState.pos[1], speed, maxObjectPairs);
  const float invMass = boxRoot->proto->invMass;
  const auto squaredLength = [](const physics::Vec3& value) {
    return value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
  };
  std::printf("box rest gate: omegaSq=%g scaledTorqueSq=%g scaledActivitySq=%g flags=0x%x\n",
              squaredLength(finalState.omega), invMass * invMass * squaredLength(finalState.torque),
              invMass * invMass * squaredLength(boxRoot->activity), boxRoot->flags);
  const auto& previousState = boxRoot->state[boxRoot->current ^ 1];
  std::printf("box support: lastForce=(%g,%g,%g) omega=(%g,%g,%g) activity=(%g,%g,%g)\n",
              previousState.force[0], previousState.force[1], previousState.force[2],
              finalState.omega[0], finalState.omega[1], finalState.omega[2],
              boxRoot->activity[0], boxRoot->activity[1], boxRoot->activity[2]);
  assert(lowestY < spawnY - 1.0f);
  assert(finalState.pos[1] > 0.0f && finalState.pos[1] < 1.0f);
  assert(speed < 0.3f);
  // FUN_004a4ed0: true sleep, not merely low visual velocity.
  assert((boxRoot->flags & physics::kBodyAsleep) != 0);
  for (const auto* body : boxBodies)
    assert((body->flags & physics::kBodyAsleep) != 0);
  // The collector retained body/ground-face pairs for the box while it was
  // awake and touching (the old code never built any).
  assert(maxObjectPairs > 0);

  // The static instance never moved: registered but unstepped in every pass.
  const auto rockNow = rockRoot->state[rockRoot->current].pos;
  for (size_t axis = 0; axis < 3; ++axis)
    assert(std::fabs(rockNow[axis] - rockSpawn[axis]) < 1e-5f);

  // An awake box approaches the sleeping box from outside its collision core: FUN_00496050 corrects the
  // actual contact then FUN_004a7990 wakes the complete sleeping tree.
  auto incoming = objectPhysics->instantiate({finalState.pos[0]-0.58f, finalState.pos[1], finalState.pos[2]}, {0, 0, 0}, error);
  assert(incoming && error.empty());
  auto* incomingRoot = incoming->root.get();
  incomingRoot->state[0].vel = {2, 0, 0};
  incomingRoot->state[1] = incomingRoot->state[0];
  auto* incomingNode = world.addTypedChild(*world.defaultParent(), 1, "incoming", 0);
  assert(incomingNode && world.registerObjectPhysics(*incomingNode,
      object::adoptInstance(std::move(*incoming))));
  bool contactWoke = false;
  for (int i = 0; i < 30 && !contactWoke; ++i) {
    assert(stepper.step(1.0f / 60.0f, error) && error.empty());
    contactWoke = !(boxRoot->flags & physics::kBodyAsleep);
  }
  assert(contactWoke);
  assert(boxRoot->activity[1] > 1000); // stock wake seeds activity.y=100000

  // --- Failure/retry: a failed step must not corrupt the object collector --
  {
    // Keep the tree awake (stock wakes an object when something hits it) so
    // it joins the step while the car stays asleep: the awake-object path
    // must run the full multi-root step, not the camera-only early one.
    boxRoot->flags &= ~physics::kBodyAsleep;
    const size_t pairsBefore = stepper.debugObjectPairCount();
    // Drop the sector's ground factory: the shared sector lookup then finds
    // no sector for the stepped trees, and the step must fail cleanly.
    sectorNode->groundFactory.reset();
    std::string stepError;
    const bool ok = stepper.step(1.0f / 60.0f, stepError);
    assert(!ok && !stepError.empty());
    // The failed step must not have erased the retained object pairs.
    assert(stepper.debugObjectPairCount() == pairsBefore);
    // Restore the ground and step again: the stepper recovers.
    establishGround();
    for (int i = 0; i < 240; ++i)
      assert(stepper.step(1.0f / 60.0f, error) && error.empty());
    const auto& restState = boxRoot->state[boxRoot->current];
    assert(restState.pos[1] > 0.0f && restState.pos[1] < 1.0f);
  }

  // Car still fine after the object settled beside it.
  auto* carRoot = carNode->carInstance->carPhysics->root.get();
  assert(carRoot->state[carRoot->current].pos[1] > 0.0f);
  return 0;
}
