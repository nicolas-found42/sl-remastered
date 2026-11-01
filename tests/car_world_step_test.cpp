// CarWorldStepper fixture: a real Emer Nonus car registered in a GameWorld
// with a synthetic flat ground sector, stepped end to end. Covers the
// stepper's drive path, its failure/retry transaction (a failed step must not
// corrupt the collector, so a later step still succeeds), and erase/recreate
// of the car with the stepper still running.
#include "car_physics.hpp"
#include "car_runtime.hpp"
#include "car_world_step.hpp"
#include "game_creation.hpp"
#include "ground_runtime.hpp"
#include "pack_registry.hpp"
#include "physics_body.hpp"
#include "resource_world.hpp"
#include "world_model.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

int main() {
  // Same pack layout as car_runtime_test: the installed 2005 tree, whose
  // slots the resource ids are expressed against (first-seen order via
  // resolveLiteral, not the hardcoded production slots).
  PackRegistry packs(
      ".scratch/2005-extract/Program Files/Street Legal Racing - Redline");
  GameWorld world;
  assert(world.addRoot());
  world.setClassResolver([](const std::string &) { return true; });
  ResourceWorld resources(world, packs);
  std::string error;
  auto resolveId = [&](const char *pack, uint32_t entry) {
    uint32_t id = 0;
    assert(packs.resolveLiteral(pack, entry, id, error));
    return id;
  };

  // --- Ground sector -------------------------------------------------------
  const uint32_t cityId = resolveId("maps\\city.rpk", 1);
  const auto city = resources.lookup(cityId, 1);
  assert(city.status == LookupStatus::Found && city.node.packNode);
  ResourceNode *sectorNode = city.node.packNode;
  if (sectorNode->parent && !sectorNode->parent->loaded())
    assert(resources.loadEmptyGameParent(*sectorNode->parent, error));

  // The city root carries factory actions the passive loader refuses, so the
  // loaded passive-spatial state is established directly (mirroring
  // loadPassiveSpatialGame's side effects). The plane is tiled with 1 m
  // triangles: city ground faces are small, and the parent-face filter rejects
  // a body deep inside one large face.
  std::shared_ptr<GroundRuntime> ground;
  WorldNode *groundBodyNode = nullptr;
  auto establishGround = [&]() {
    sectorNode->stateWord = (sectorNode->stateWord & ~uint32_t{0x50}) | 0x21;
    sectorNode->bounds = ResourceBounds{};
    sectorNode->bounds->mode = 1;
    sectorNode->bounds->values = {0, 0, 0, 5, 5, 5};
    setLinkOwner(sectorNode->sectorLink, sectorNode);
    {
      auto payload = std::make_shared<PreparedSpatialGamePayload>();
      GamePhysicsGeometry geometry;
      constexpr float lo = -4.0f, hi = 4.0f, step = 1.0f;
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
          // Winding gives cross(v1-v0, v2-v0) = +Y, matching the normal.
          tri.vertices = {base, base + 2, base + 1};
          geometry.triangles.push_back(tri);
          tri.vertices = {base, base + 3, base + 2};
          geometry.triangles.push_back(tri);
        }
      // A raised box straddling the car's centre: the wheels rest in the gap
      // beside it while the chassis bottom sits on the box top, so a leaf body
      // (not a wheel) retains a ground-face pair -- the collector path a plain
      // flat landing never reaches (the suspension holds the chassis up).
      auto boxTop = [&](float x0, float x1, float z0, float z1, float y) {
        const uint32_t b = static_cast<uint32_t>(geometry.vertices.size());
        geometry.vertices.push_back({x0, y, z0});
        geometry.vertices.push_back({x1, y, z0});
        geometry.vertices.push_back({x1, y, z1});
        geometry.vertices.push_back({x0, y, z1});
        GamePhysicsTriangle tri;
        tri.normal = {0, 1, 0};
        tri.materialWord = 0;
        tri.vertices = {b, b + 2, b + 1};
        geometry.triangles.push_back(tri);
        tri.vertices = {b, b + 3, b + 2};
        geometry.triangles.push_back(tri);
      };
      boxTop(-0.5f, 0.5f, -1.6f, 1.6f, 0.72f);
      payload->physics = std::move(geometry);
      sectorNode->spatialPayload = std::move(payload);
    }
    auto factory = std::make_shared<GroundFactoryResult>();
    auto preparation = std::make_shared<GroundPreparation>();
    preparation->gravity = -physics::k::gravityY;
    preparation->files.materials = GroundConfig{};
    {
      GroundMaterial material;
      material.name = "asphalt";
      material.friction = 1.0f;
      material.rollingResistance = 0.01f;
      material.collisionCoefficient = 1.0f;
      preparation->files.materials->materials.push_back(material);
    }
    if (!ground) {
      auto created = createGroundRuntime({0, 0, 0}, 1.0f, 0);
      assert(created);
      ground = std::shared_ptr<GroundRuntime>(std::move(created));
    }
    preparation->physics = ground;
    // Mirror the production ground creation (game_type.cpp "ground" command):
    // the body node lives under the sector's external tree, the factory's
    // city link names the sector, and the body node names the GroundRuntime.
    if (!groundBodyNode) {
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
    setLinkOwner(preparation->bodyResource, groundBodyNode);
    setLinkOwner(factory->city, sectorNode);
    factory->preparation = preparation;
    sectorNode->groundFactory = std::move(factory);
  };
  establishGround();

  // --- Car -----------------------------------------------------------------
  const uint32_t carScriptId = resolveId("cars\\racers\\Nonus.rpk", 6);
  auto *script =
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
  auto *carNode = world.addTypedChild(*world.defaultParent(), 1, "car", 0);
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

  // Drop the car hard so the landing bottoms the chassis onto the plane:
  // wheel witnesses alone leave the pair collector empty, but a leaf body
  // (chassis) touching the ground builds retained body/ground-face pairs and
  // runs the corrector that mutates their accumulators in place.
  auto &root = *carNode->carInstance->carPhysics->root;
  root.state[0].pos = {0, 2.5f, 0};
  root.state[1] = root.state[0];

  CarWorldStepper stepper(world, resources);
  auto& runtime = *carNode->carInstance;
  runtime.smoothedSpeed = 3; // inject previous +0x1f74 without perturbing the landing.
  const float stepSeconds = 1.0f / 60.0f;
  assert(stepper.step(stepSeconds, error));
  assert(std::fabs(runtime.smoothedSpeed - 2.4f) < 0.00001f);
  assert(std::fabs(runtime.mileage - stepSeconds * 2.4f) < 0.00001f);
  runtime.damageEnabled = false;
  const float mileage = runtime.mileage;
  assert(stepper.step(stepSeconds, error));
  assert(runtime.mileage == mileage);
  runtime.damageEnabled = true;
  for (int i = 0; i < 400; ++i)
    assert(stepper.step(1.0f / 60.0f, error) && error.empty());
  const float settledY = root.state[root.current].pos[1];
  assert(settledY > 0.0f && settledY < 2.0f);
  assert(std::fabs(root.state[root.current].vel[1]) < 0.5f);
  // The hard landing must have driven the chassis into the plane, leaving
  // retained pairs: proof the collector path (not just wheel witnesses) ran.
  assert(stepper.debugPairCount() > 0);

  // --- Failure/retry: a failed step must not corrupt the collector --------
  {
    const size_t pairsBefore = stepper.debugPairCount();
    // Drop the ground mid-run: the gather reports traversal incomplete, the
    // step fails, and the collector must roll its accumulator snapshot back
    // (no erase happens on a failed step).
    sectorNode->groundFactory.reset();
    sectorNode->spatialPayload.reset();
    sectorNode->stateWord = (sectorNode->stateWord & ~uint32_t{0x31}) | 0x40;
    std::string stepError;
    const bool ok = stepper.step(1.0f / 60.0f, stepError);
    assert(!ok && !stepError.empty());
    // The failed step must not have erased the retained pairs.
    assert(stepper.debugPairCount() == pairsBefore);
    // Restore the ground and step again: the stepper recovers and the car is
    // still supported by the (restored) pairs.
    establishGround();
    for (int i = 0; i < 120; ++i)
      assert(stepper.step(1.0f / 60.0f, error) && error.empty());
    assert(root.state[root.current].pos[1] > 0.0f &&
           root.state[root.current].pos[1] < 2.0f);
  }

  // --- Erase/recreate: destroy the car, rebuild it, keep stepping ---------
  {
    world.destroy(*carNode);
    // Rebuild an identical car over the same ground.
    auto rebuilt = buildDetachedCarPhysics(*script->car, "0,0,0,0,0,0,0", 0,
                                           {0, physics::k::gravityY, 0}, error);
    assert(rebuilt && error.empty());
    auto *newCarNode =
        world.addTypedChild(*world.defaultParent(), 1, "car2", 0);
    assert(newCarNode);
    newCarNode->nativeDevice = "car";
    newCarNode->carInstance = std::make_unique<CarRuntimeInstance>();
    newCarNode->carInstance->isCar = true;
    newCarNode->carInstance->carPhysics =
        std::shared_ptr<CarPhysics>(std::move(rebuilt));
    setLinkOwner(newCarNode->carInstance->prototypeSource, script);
    world.noteInstanceCreated(*newCarNode);
    world.markLoaded(*newCarNode);
    assert(world.registerCarPhysics(
        *newCarNode, newCarNode->carInstance->carPhysics, error));
    auto &newRoot = *newCarNode->carInstance->carPhysics->root;
    newRoot.state[0].pos = {0, 2.0f, 0};
    newRoot.state[1] = newRoot.state[0];
    for (int i = 0; i < 300; ++i)
      assert(stepper.step(1.0f / 60.0f, error) && error.empty());
    // The rebuilt car settled onto the plane again.
    assert(newRoot.state[newRoot.current].pos[1] > 0.0f &&
           newRoot.state[newRoot.current].pos[1] < 2.0f);
  }

  // --- Mixed sleep/awake: a suspended car is skipped while its awake
  // neighbour is stepped (004a3f90/004a3bc0 skip sleeping trees in every
  // pass; the multi-root step only ever sees the awake trees).
  {
    auto parked = buildDetachedCarPhysics(*script->car, "0,0,0,0,0,0,0", 0,
                                          {0, physics::k::gravityY, 0}, error);
    assert(parked && error.empty());
    auto* parkedNode =
        world.addTypedChild(*world.defaultParent(), 1, "parked", 0);
    assert(parkedNode);
    parkedNode->nativeDevice = "car";
    parkedNode->carInstance = std::make_unique<CarRuntimeInstance>();
    parkedNode->carInstance->isCar = true;
    parkedNode->carInstance->carPhysics =
        std::shared_ptr<CarPhysics>(std::move(parked));
    setLinkOwner(parkedNode->carInstance->prototypeSource, script);
    world.noteInstanceCreated(*parkedNode);
    world.markLoaded(*parkedNode);
    assert(world.registerCarPhysics(*parkedNode,
                                    parkedNode->carInstance->carPhysics, error));
    auto& parkedRoot = *parkedNode->carInstance->carPhysics->root;
    // On the ground plane but beside the raised box, held suspended: no
    // contact can wake it.
    parkedRoot.state[0].pos = {2.5f, 0.8f, 0};
    parkedRoot.state[1] = parkedRoot.state[0];
    suspendCar(*parkedNode->carInstance->carPhysics);
    assert(parkedRoot.flags & physics::kBodyAsleep);

    // The settled car from the erase/recreate block is woken and lifted, so
    // it genuinely moves this frame.
    auto settledNodes = world.children(*world.defaultParent());
    WorldNode* settledNode = nullptr;
    assert(settledNodes);
    for (auto* candidate : *settledNodes)
      if (candidate->name == "car2") settledNode = candidate;
    assert(settledNode && settledNode->carInstance &&
           settledNode->carInstance->carPhysics);
    auto& settledRoot = *settledNode->carInstance->carPhysics->root;
    wakeCar(*settledNode->carInstance->carPhysics);
    settledRoot.state[0].pos = {0, 2.0f, 0};
    settledRoot.state[1] = settledRoot.state[0];

    const auto parkedState0 = parkedRoot.state[0];
    const auto parkedState1 = parkedRoot.state[1];
    const auto parkedCurrent = parkedRoot.current;
    const auto parkedFlags = parkedRoot.flags;
    const float timeBefore = world.physicsTime();

    for (int i = 0; i < 60; ++i)
      assert(stepper.step(1.0f / 60.0f, error) && error.empty());

    // The awake car was integrated: it fell from the lifted pose.
    assert(settledRoot.state[settledRoot.current].pos[1] < 2.0f);
    // The sleeping car was skipped by every pass: neither state buffer, the
    // current index, nor the asleep flag changed.
    assert(world.physicsTime() > timeBefore);
    assert(parkedRoot.current == parkedCurrent);
    assert(parkedRoot.flags == parkedFlags);
    assert(parkedRoot.flags & physics::kBodyAsleep);
    assert(parkedRoot.state[0].pos == parkedState0.pos &&
           parkedRoot.state[0].vel == parkedState0.vel &&
           parkedRoot.state[0].time == parkedState0.time);
    assert(parkedRoot.state[1].pos == parkedState1.pos &&
           parkedRoot.state[1].vel == parkedState1.vel);
  }

  return 0;
}
