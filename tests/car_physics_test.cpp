#include "car_physics.hpp"
#include "game_physics_geometry.hpp"
#include "physics_polyhedron.hpp"
#include "world_model.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

static bool near(float a, float b) { return std::abs(a - b) < 0.0001f; }
int main() {
  std::string error;
  CarConfig config;
  config.rootBody = 0;
  config.bodies.resize(1);
  std::vector<CarGeometryPrototype> geometry(1);
  geometry[0].bodyIndex = 0;
  geometry[0].transform.position = {2, 0, 0};
  auto proto = physics::createBodyProto(4, 1, 0.5f, 0, 0, nullptr, error);
  assert(proto);
  *geometry[0].body = *proto;
  auto part = buildPartPhysics(config, geometry, "10,3,4,0,0,0,0", error,
                               nullptr, 2.5f);
  assert(part);
  auto *oldRoot = part->root.get();
  auto *oldChild = part->children[0].body.get();
  const auto oldPose = oldRoot->state[oldRoot->current];
  auto car = buildCarPhysics(std::move(part), 0, error);
  assert(car && error.empty());
  assert(car->part->root.get() == oldRoot &&
         car->part->children[0].body.get() == oldChild);
  assert(car->root->proto == &car->rootPrototype &&
         car->rootPrototype.shape.type == physics::ShapeType::Box);
  assert(near(car->rootPrototype.invMass, 1) &&
         near(car->rootPrototype.shape.dim[0], 1));
  assert(near(car->mass, 4));
  assert(car->part->root->attachment->parent == car->root.get());
  assert(car->part->children[0].body->attachment->parent == oldRoot);
  assert(car->bodyChildren.at(car->root.get()).size() == 1);
  assert(car->bodyChildren.at(car->root.get())[0] == oldRoot);
  assert(car->bodyChildren.at(oldRoot).size() == 1);
  assert(car->bodyChildren.at(oldRoot)[0] == oldChild);
  assert(car->bodyChildren.at(oldChild).empty());
  const auto originalAttachment = oldRoot->attachment;
  oldRoot->attachment->parent = oldRoot;
  assert(!indexCarBodies(*car, error));
  assert(car->bodyChildren.at(car->root.get())[0] == oldRoot);
  oldRoot->attachment = originalAttachment;
  error.clear();
  assert(near(car->root->state[0].pos[0], oldPose.pos[0]) &&
         near(car->root->state[0].time, 2.5f));
  assert(near(car->part->root->attachment->relative.position[0], 0));
  assert(finalizeCarPhysics(*car, error));
  assert(near(car->rootPrototype.invMass, 0.25f));
  assert(car->rootPrototype.shape.type == physics::ShapeType::Mesh);
  assert(car->rootPrototype.shape.core &&
         car->rootPrototype.shape.core->vertices.size() == 8);
  assert(car->teardownOrder.size() == 3);
  assert(car->teardownOrder[0] == oldChild &&
         car->teardownOrder[1] == oldRoot &&
         car->teardownOrder[2] == car->root.get());
  auto installed = buildPartPhysics(config, geometry, "0,0,0,0,0,0,0", error);
  PartSlotDirective targetDirective{{1, 0, 0}, {0, 0, 0}, 1};
  PartSlotDirective partDirective{{0, 2, 0}, {0, 0, 0}, 2};
  PartSlotState targetSlot{}, partSlot{};
  assert(makePartSlotState(targetDirective, targetSlot));
  assert(makePartSlotState(partDirective, partSlot));
  assert(attachPartPhysics(*installed, partSlot, *car->part, targetSlot));
  assert(installed->root->attachment->parent == car->part->root.get());
  assert(car->bodyChildren.at(car->part->root.get()).size() == 2);
  auto installedObservation = car->bodyObservations.at(installed->root.get());
  assert(installedObservation->body == installed->root.get());
  assert(near(car->rootPrototype.invMass, 0.125f));
  const auto *targetBody = car->part->rootGeometryBody();
  const auto &targetPose = targetBody->state[targetBody->current];
  assert(near(installed->root->state[0].pos[0], targetPose.pos[0] + 1));
  assert(near(installed->root->state[0].pos[1], targetPose.pos[1] - 2));
  physics::State expectedChild;
  physics::predictChildPose(installed->root->state[0],
                            installed->children[0].body->attachment->relative,
                            expectedChild);
  for (size_t i = 0; i < 3; ++i)
    assert(near(installed->children[0].body->state[0].pos[i],
                expectedChild.pos[i]));
  assert(!attachPartPhysics(*car->part, targetSlot, *installed, partSlot));
  detachPartPhysics(*installed);
  assert(!installedObservation->body &&
         !car->bodyChildren.count(installed->root.get()));
  assert(near(car->rootPrototype.invMass, 0.25f));
  targetDirective.orientation[0] = 0.7f;
  partDirective.orientation[1] = -0.4f;
  assert(makePartSlotState(targetDirective, targetSlot));
  assert(makePartSlotState(partDirective, partSlot));
  assert(attachPartPhysics(*installed, partSlot, *car->part, targetSlot));
  const auto slotWorld = [](const physics::Body &body,
                            const PartSlotState &slot) {
    return physics::transformPoint(body.state[body.current].pos,
                                   body.state[body.current].rot, slot.position);
  };
  auto a = slotWorld(*installed->root, partSlot);
  auto b = slotWorld(*targetBody, targetSlot);
  for (size_t i = 0; i < 3; ++i)
    assert(near(a[i], b[i]));
  detachPartPhysics(*installed);
  assert(!installed->root->attachment && car->part->installedChildren.empty());
  assert(attachPartPhysics(*installed, partSlot, *car->part, targetSlot));
  auto nested = buildPartPhysics(config, geometry, "0,0,0,0,0,0,0", error);
  assert(attachPartPhysics(*nested, partSlot, *installed, targetSlot));
  assert(nested->root->attachment->parent == installed->root.get());
  auto nestedObservation = car->bodyObservations.at(nested->root.get());
  detachPartPhysics(*installed);
  assert(!nested->root->attachment && !nested->installedParent &&
         !nestedObservation->body);
  assert(attachPartPhysics(*installed, partSlot, *car->part, targetSlot));
  CarConfig clickOnly;
  CarGeometryPrototype click;
  auto clickProto = physics::createBodyProto(0, 1, 0.5f, 0, 0, nullptr, error);
  assert(clickProto);
  *click.body = *clickProto;
  click.transform.rotation = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  auto battery =
      buildPartPhysics(clickOnly, {}, "0,0,0,0,0,0,0", error, &click);
  assert(battery &&
         attachPartPhysics(*battery, partSlot, *car->part, targetSlot));
  assert(near(car->rootPrototype.invMass, 0.125f));
  detachPartPhysics(*battery);
  assert(near(car->rootPrototype.invMass, 0.125f));
  auto temporary = buildPartPhysics(config, geometry, "0,0,0,0,0,0,0", error);
  assert(attachPartPhysics(*temporary, partSlot, *car->part, targetSlot));
  auto temporaryObservation = car->bodyObservations.at(temporary->root.get());
  const auto *temporaryRoot = temporary->root.get();
  temporary.reset();
  assert(!temporaryObservation->body &&
         !car->bodyChildren.count(temporaryRoot));
  auto rootObservation = car->bodyObservations.at(car->root.get());
  auto partObservation = car->bodyObservations.at(oldRoot);
  auto childObservation = car->bodyObservations.at(oldChild);
  WorldNode resource;
  resource.value = 42;
  setLinkOwner(childObservation->resource, &resource);
  assert(resource.head == &childObservation->resource &&
         childObservation->body == oldChild);
  car.reset();
  assert(!installed->root->attachment && !installed->installedParent &&
         !installed->indexedCar);
  assert(!rootObservation->body && !partObservation->body &&
         !childObservation->body);
  assert(!resource.head && !childObservation->resource.owner &&
         childObservation->resource.cachedValue == 0);
  auto second = buildPartPhysics(config, geometry, "0,0,0,0,0,0,0", error);
  auto specified = buildCarPhysics(std::move(second), 1200, error);
  assert(specified && near(specified->mass, 1200));
  assert(!buildCarPhysics(nullptr, 1, error) && !error.empty());
  {
    // 004a0aa0 uses the swept root sphere and a swept oriented mesh
    // bounding box before passing surviving faces to the children.
    GamePhysicsGeometry track;
    track.vertices = {{-10, 0, -10}, {0, 0, 10}, {10, 0, -10}};
    track.triangles.push_back({0, {0, 1, 2}, {0, 1, 0}});
    ResourceBounds bounds;
    bounds.mode = 1;
    bounds.values = {0, 0, 0, 20, 20, 20};
    ResourceBounds neighbor = bounds;
    neighbor.values[0] = 40;
    assert(updateSectorNeighbor(false, true, bounds, neighbor) ==
           SectorNeighborTransition::Add); // touching X/Z bounds overlap
    assert(updateSectorNeighbor(false, false, bounds, neighbor) ==
           SectorNeighborTransition::Keep);
    assert(updateSectorNeighbor(true, false, bounds, neighbor) ==
           SectorNeighborTransition::Keep);
    neighbor.values[2] = 41;
    assert(updateSectorNeighbor(true, true, bounds, neighbor) ==
           SectorNeighborTransition::Remove);
    assert(updateSectorNeighbor(false, true, bounds, neighbor) ==
           SectorNeighborTransition::Keep);
    neighbor.mode = 0;
    assert(updateSectorNeighbor(false, true, bounds, neighbor) ==
           SectorNeighborTransition::Unsupported);
    physics::BodyProto bodyProto;
    bodyProto.shape.type = physics::ShapeType::Mesh;
    bodyProto.shape.boundRadius = 2;
    bodyProto.shape.core = std::make_shared<physics::CollisionCore>(
        physics::boundsVertices({-1, -1, -1}, {1, 1, 1}));
    physics::Body body;
    body.proto = &bodyProto;
    body.flags = physics::kBodyActive | physics::kBodyHasChildren;
    body.state[0].pos = {0, 2, 0};
    body.state[1].pos = {0, 0.5f, 0};
    assert(queryParentFace(track, 0, body, bounds, false) ==
           ParentFaceMask::Accepted);
    assert(queryParentFace(track, 0, body, bounds, true) ==
           ParentFaceMask::Rejected);
    body.state[1].rot = {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0};
    assert(queryParentFace(track, 0, body, bounds, false) ==
           ParentFaceMask::Accepted);
    physics::Body ancestor;
    body.attachment =
        physics::BodyAttachment{&ancestor, physics::RelativeBodyPose{}};
    body.flags |= physics::kBodyChild;
    assert(queryParentFace(track, 0, body, bounds, false) ==
           ParentFaceMask::Accepted);
    body.flags |= 0x20000;
    assert(queryParentFace(track, 0, body, bounds, false) ==
           ParentFaceMask::Unsupported);
    body.flags &= ~uint32_t{0x20000};
    body.state[0].pos = body.state[1].pos = {0, 10, 0};
    assert(queryParentFace(track, 0, body, bounds, false) ==
           ParentFaceMask::Rejected);
    body.state[0].pos = body.state[1].pos = {0, 0.5f, 0};
    bodyProto.shape.core.reset();
    assert(queryParentFace(track, 0, body, bounds, false) ==
           ParentFaceMask::Unsupported);
  }
  {
    GameWorld world;
    auto *root = world.addRoot();
    auto *leaf = world.addChild(*root, "collision leaf", 0);
    assert(leaf && world.queueCollisionUpdate(*leaf));
    assert(world.queueCollisionUpdate(*leaf) &&
           world.collisionUpdateCount() == 1);
    assert(world.stepEmptyPhysics(0.00005f, error));
    assert(world.collisionUpdateCount() == 1 && (leaf->flags & 0x10000000u));
    assert(world.stepEmptyPhysics(0.001f, error));
    assert(world.collisionUpdateCount() == 0 && !(leaf->flags & 0x10000000u));
    auto *child = world.addChild(*leaf, "nested", 0);
    assert(child && world.queueCollisionUpdate(*leaf));
    const float time = world.physicsTime();
    assert(!world.stepEmptyPhysics(0.01f, error));
    assert(world.physicsTime() == time && world.collisionUpdateCount() == 1 &&
           (leaf->flags & 0x10000000u));
    assert(world.destroy(*child));
    assert(world.stepEmptyPhysics(0.01f, error));
    assert(world.collisionUpdateCount() == 0 && !(leaf->flags & 0x10000000u));
    auto *box = world.addTypedChild(*leaf, 2, "static box", 0);
    assert(box && world.registerStaticBox(*box, {1, 1, 1}));
    world.markLoaded(*box);
    const float smallRadius = box->physicsBox->radius;
    box->bounds.mode = 0;
    box->bounds.values = {4, 0, 0, smallRadius, smallRadius, smallRadius};
    assert(world.queueCollisionUpdate(*leaf));
    const float beforeProxy = world.physicsTime();
    assert(!world.stepEmptyPhysics(0.01f, error));
    assert(error.find("proxy discovery") != std::string::npos &&
           world.physicsTime() == beforeProxy &&
           world.collisionUpdateCount() == 1 && leaf->bounds.mode == 2);
    box->flags |= 0x200000u;
    assert(world.stepEmptyPhysics(0.01f, error));
    assert(world.collisionUpdateCount() == 0 && leaf->bounds.mode == 1);
    assert(world.destroy(*box));
    box = world.addTypedChild(*leaf, 2, "large static box", 0);
    assert(box && world.registerStaticBox(*box, {101, 0, 0}));
    world.markLoaded(*box);
    const float radius = box->physicsBox->radius;
    box->bounds.mode = 0;
    box->bounds.values = {4, 0, 0, radius, radius, radius};
    assert(world.queueCollisionUpdate(*leaf));
    assert(world.stepEmptyPhysics(0.01f, error));
    assert(world.collisionUpdateCount() == 0 && leaf->bounds.mode == 1 &&
           std::fabs(leaf->bounds.values[0] - 4) < 0.0001f &&
           std::fabs(leaf->bounds.values[3] - radius) < 0.0001f);
    assert(world.destroy(*box));
    assert(world.queueCollisionUpdate(*leaf) && world.destroy(*leaf));
    assert(world.stepEmptyPhysics(0.01f, error) &&
           world.collisionUpdateCount() == 0);
  }
  {
    GameWorld world;
    auto *worldRoot = world.addRoot();
    auto *node = world.addChild(*worldRoot, "car", 0);
    auto detached = buildCarPhysics(
        buildPartPhysics(config, geometry, "0,0,0,0,0,0,0", error), 0, error);
    assert(detached && finalizeCarPhysics(*detached, error));
    std::shared_ptr<CarPhysics> registered(std::move(detached));
    auto observation = registered->bodyObservations.at(registered->root.get());
    auto *rootNode = world.registerCarPhysics(*node, registered, error);
    assert(rootNode && !world.registerCarPhysics(*node, registered, error));
    const auto nodes = world.children(*node);
    assert(nodes && nodes->size() == 3);
    assert((*nodes)[0]->name == "part_comp" && (*nodes)[1]->name == "bbone" &&
           (*nodes)[2] == rootNode && rootNode->name == "car_root");
    assert(world.dynamicBody(*(*nodes)[0]).lock().get() ==
           registered->part->root.get());
    assert(world.dynamicBody(*(*nodes)[1]).lock().get() ==
           registered->part->rootGeometryBody());
    assert(world.dynamicBody(*rootNode).lock().get() == registered->root.get());
    assert(observation->resource.owner == rootNode);
    assert(!world.stepArmedCameraFollow(0.01f, error));
    assert(world.physicsTime() == 0);
    assert(!world.stepEmptyPhysics(0.01f, error));
    assert(world.physicsTime() == 0);
    std::weak_ptr<CarPhysics> weak = registered;
    registered.reset();
    assert(!weak.expired());
    assert(world.destroy(*node));
    assert(weak.expired() && !observation->body);
    assert(world.dynamicBodyCount() == 0);
  }
  {
    // getInfo(0x2f) wheel override (all.c:68278-68310): a slot whose slottype
    // is in (100, 0x191) is posed from the wheel record, not from the target's
    // root geometry body. The runtime slot supplies the slottype to the attach.
    auto wheelCarPart =
        buildPartPhysics(config, geometry, "0,0,0,0,0,0,0", error);
    auto wheelCar = buildCarPhysics(std::move(wheelCarPart), 0, error);
    assert(wheelCar && error.empty());
    std::vector<CarWheelPrototype> wheels(1);
    wheels[0].position = {3, 1, -2};
    auto wheelProto =
        physics::createBodyProto(1, 1, 0.3f, 0, 0, nullptr, error);
    assert(wheelProto);
    wheels[0].wheelBody = *wheelProto;
    wheels[0].spring = {0.5f, 0.3f, 0.05f};
    wheels[0].friction = {1.5f, 1.5f};
    wheels[0].hasWheel = true;
    const auto *wheelCarBody = wheelCar->part->rootGeometryBody();
    assert(wheelCarBody);
    wheelCar->wheels =
        buildCarWheelRuntime(wheels, *wheelCar->root, *wheelCarBody, {0, 0, 0},
                             {0, 0, 0}, wheelCar->mass, {0, -9.81f, 0}, error);
    assert(wheelCar->wheels.size() == 1 && wheelCar->wheels[0]->hubBody);
    assert(finalizeCarPhysics(*wheelCar, error) &&
           indexCarBodies(*wheelCar, error));

    PartSlotState wheelSlot{};
    PartSlotDirective wheelSlotDirective{{0, 0, 0}, {0, 0, 0}, 101};
    assert(makePartSlotState(wheelSlotDirective, wheelSlot));
    physics::State expected{};
    assert(wheelReferencePose(*wheelCar->part, 101, expected));
    // Stock sizes the record array to the real wheel count (all.c:58566,
    // count * 0x2b4) while getInfo 0x2f indexes (slottype-1) % 100 unbounded,
    // so 400 is an out-of-bounds read in stock; the port fails closed.
    assert(!wheelReferencePose(*wheelCar->part, 400, expected));
    assert(!wheelReferencePose(*wheelCar->part, 100, expected));
    assert(!wheelReferencePose(*wheelCar->part, 401, expected));
    // expected still holds the slot 101 pose for the comparison below: the
    // record pose is the hub position (the wheel part target).
    const auto *hub = wheelCar->wheels[0]->hubBody.get();
    assert(near(expected.pos[0], hub->state[hub->current].pos[0]) &&
           near(expected.pos[1], hub->state[hub->current].pos[1]) &&
           near(expected.pos[2], hub->state[hub->current].pos[2]));

    auto wheelMounted =
        buildPartPhysics(config, geometry, "0,0,0,0,0,0,0", error);
    assert(wheelMounted);
    assert(attachPartPhysics(*wheelMounted, wheelSlot, *wheelCar->part,
                             wheelSlot, 201));
    for (size_t i = 0; i < 3; ++i)
      assert(near(wheelMounted->root->state[0].pos[i],
                  hub->state[hub->current].pos[i]));
    detachPartPhysics(*wheelMounted);

    // A non-wheel slot still aligns to the root geometry body.
    assert(attachPartPhysics(*wheelMounted, wheelSlot, *wheelCar->part,
                             wheelSlot, 5));
    const auto &rootPose = wheelCarBody->state[wheelCarBody->current];
    for (size_t i = 0; i < 3; ++i)
      assert(near(wheelMounted->root->state[0].pos[i], rootPose.pos[i]));
    assert(!near(rootPose.pos[0], hub->state[hub->current].pos[0]));

    // slotReferencePosition applies the slot offset in the reference frame, so
    // the nearest-match scoring in 00473790 sees the wheel slot where the wheel
    // is, not where the chassis body is.
    PartSlotState offsetSlot{};
    PartSlotDirective offsetDirective{{0, 0, 1}, {0, 0, 0}, 101};
    assert(makePartSlotState(offsetDirective, offsetSlot));
    // The reference pose is the hub position: the car config's root geometry
    // body sits at {2,0,0} in part space, so the hub is at {5,1,-2}.
    const auto wheelSlotPos =
        slotReferencePosition(*wheelCar->part, 201, offsetSlot);
    assert(near(wheelSlotPos[0], 5) && near(wheelSlotPos[1], 1) &&
           near(wheelSlotPos[2], -1));
    detachPartPhysics(*wheelMounted);
    GameWorld world;
    auto *worldRoot = world.addRoot();
    auto *carNode = world.addChild(*worldRoot, "car", 0);
    std::shared_ptr<CarPhysics> registered(std::move(wheelCar));
    registered->wheels[0]->hubBody->current = 2;
    assert(!world.registerCarPhysics(*carNode, registered, error));
    assert(world.children(*carNode)->empty() && world.dynamicBodyCount() == 0);
    for (const auto &entry : registered->bodyObservations)
      assert(!entry.second->resource.owner);
    registered->wheels[0]->hubBody->current = 0;
    auto *rootNode = world.registerCarPhysics(*carNode, registered, error);
    assert(rootNode);
    auto nodes = world.children(*carNode);
    assert(nodes && nodes->size() == 5);
    assert((*nodes)[0]->name == "part_comp" && (*nodes)[1]->name == "bbone" &&
           (*nodes)[2] == rootNode && (*nodes)[3]->name == "hub" &&
           (*nodes)[4]->name == "a_arm");
    assert(world.dynamicBody(*(*nodes)[3]).lock().get() ==
           registered->wheels[0]->hubBody.get());
    assert(world.dynamicBody(*(*nodes)[4]).lock().get() ==
           registered->wheels[0]->armBody.get());
    assert(!registered->bodyObservations
                .at(registered->wheels[0]->suspensionBody.get())
                ->resource.owner);
  }
  {
    // 0044c460 ride-height placement. Static compression is
    // (|g| * mass / wheels) / springForce = 0.1 m, so each wheel centre sits at
    // anchor.y - (initialLength - 0.1) = anchor.y - 0.5.
    CarPhysics ride;
    ride.mass = 1000;
    const float xs[4] = {-0.8f, 0.8f, -0.8f, 0.8f};
    const float zs[4] = {-1.3f, -1.3f, 1.3f, 1.3f};
    for (int i = 0; i < 4; ++i) {
      auto wheel = std::make_unique<CarWheelRuntime>();
      wheel->anchor = {xs[i], 0.2f, zs[i]};
      wheel->initialLength = 0.6f;
      wheel->springForce = 9.81f * 1000 / 4 / 0.1f;
      ride.wheels.push_back(std::move(wheel));
    }
    std::array<float, 3> pos{}, ypr{};
    // The `center` clamp wins: root rises by -(R*center).y = 0.5.
    ride.center = {0, 0.5f, 0};
    assert(carRideHeightPlacement(ride, {10, 20, 30}, {0, 0, 0}, pos, ypr));
    assert(near(pos[0], 10) && near(pos[1], 20.5f) && near(pos[2], 30));
    assert(near(ypr[0], 0) && near(ypr[1], 0) && near(ypr[2], 0));
    // The mean wheel centre (y = -0.3) wins: root rises by 0.3.
    ride.center = {0, 0.1f, 0};
    assert(carRideHeightPlacement(ride, {10, 20, 30}, {0, 0, 0}, pos, ypr));
    assert(near(pos[1], 20.3f));
    // Rear wheels 0.26 m higher than front: pitch += atan(0.26 / 2.6).
    ride.wheels[2]->anchor[1] += 0.26f;
    ride.wheels[3]->anchor[1] += 0.26f;
    assert(carRideHeightPlacement(ride, {0, 0, 0}, {0.5f, 0, 0}, pos, ypr));
    assert(near(ypr[0], 0.5f) && near(ypr[1], std::atan(0.1f)) &&
           near(ypr[2], 0));
    // Fewer than four wheels: position += R * center.
    ride.wheels.resize(3);
    assert(carRideHeightPlacement(ride, {10, 20, 30}, {0, 0, 0}, pos, ypr));
    assert(near(pos[1], 20.1f) && near(ypr[1], 0));
  }
  {
    // 00458c00 `reset` (resetCarToRest): zero the root's linear/angular
    // velocity and accumulated force/torque, return each wheel's suspension
    // to its static preload length = initialLength - (|g| * mass / wheels) /
    // springForce, zero the live wheel slip state, then put the whole body
    // tree to sleep (004a7a80).
    auto restPart =
        buildPartPhysics(config, geometry, "0,0,0,0,0,0,0", error);
    auto restCar = buildCarPhysics(std::move(restPart), 0, error);
    assert(restCar && error.empty());
    std::vector<CarWheelPrototype> restWheels(1);
    restWheels[0].position = {3, 1, -2};
    auto restWheelProto =
        physics::createBodyProto(1, 1, 0.3f, 0, 0, nullptr, error);
    assert(restWheelProto);
    restWheels[0].wheelBody = *restWheelProto;
    restWheels[0].spring = {0.5f, 0.3f, 0.05f};
    restWheels[0].friction = {1.5f, 1.5f};
    restWheels[0].hasWheel = true;
    const auto* restMainBody = restCar->part->rootGeometryBody();
    assert(restMainBody);
    const auto& restCenter = restCar->part->rootPrototype.shape.centerOffset;
    restCar->wheels = buildCarWheelRuntime(restWheels, *restCar->root,
                                           *restMainBody, {restCenter[0], restCenter[1], restCenter[2]},
                                           restMainBody->attachment->relative.position, restCar->mass,
                                           {0, physics::k::gravityY, 0}, error);
    assert(restCar->wheels.size() == 1 && error.empty());
    assert(finalizeCarPhysics(*restCar, error) &&
           indexCarBodies(*restCar, error));

    // Every body the car owns: root, part bodies, and wheel bodies.
    std::vector<physics::Body*> treeBodies{restCar->root.get(),
                                           restCar->part->root.get()};
    for (const auto& childEntry : restCar->part->children)
      treeBodies.push_back(childEntry.body.get());
    for (const auto& wheel : restCar->wheels) {
      treeBodies.push_back(wheel->suspensionBody.get());
      if (wheel->boneBody) treeBodies.push_back(wheel->boneBody.get());
      treeBodies.push_back(wheel->hubBody.get());
      treeBodies.push_back(wheel->armBody.get());
    }

    // wakeCar (00458c00 `wakeup`) wakes the whole tree via
    // wakeDetachedCompound.
    wakeCar(*restCar);
    for (auto* body : treeBodies)
      assert(!(body->flags & physics::kBodyAsleep));

    // Disturb the car: root motion state, wheel suspension and slip state.
    auto& restState = restCar->root->state[restCar->root->current];
    restState.vel = {3, -2, 1};
    restState.omega = {0.5f, 1, -0.25f};
    restState.angMom = {1, 2, 3};
    restState.force = {4, -5, 6};
    restState.torque = {-1, 0.5f, 2};
    auto& restWheel = *restCar->wheels[0];
    restWheel.suspension.length = restWheel.initialLength + 0.2f;
    restWheel.suspension.force = 123;
    restWheel.angularSpeed = 40;
    restWheel.longitudinalVelocity = 12;
    restWheel.longitudinalSlip = 0.7f;
    restWheel.slipAngle = 0.3f;

    resetCarToRest(*restCar);
    assert(near(restState.vel[0], 0) && near(restState.vel[1], 0) &&
           near(restState.vel[2], 0));
    assert(near(restState.omega[0], 0) && near(restState.omega[1], 0) &&
           near(restState.omega[2], 0));
    assert(near(restState.angMom[0], 0) && near(restState.angMom[1], 0) &&
           near(restState.angMom[2], 0));
    assert(near(restState.force[0], 0) && near(restState.force[1], 0) &&
           near(restState.force[2], 0));
    assert(near(restState.torque[0], 0) && near(restState.torque[1], 0) &&
           near(restState.torque[2], 0));
    // Static preload: length = initialLength - (|g| * mass / wheels) /
    // springForce, and the live wheel state is zeroed.
    const float restWeightPerWheel =
        std::fabs(physics::k::gravityY) * restCar->mass /
        static_cast<float>(restCar->wheels.size());
    assert(near(restWheel.suspension.length,
                restWheel.initialLength -
                    restWeightPerWheel / restWheel.springForce));
    assert(near(restWheel.suspension.force, 0));
    assert(near(restWheel.angularSpeed, 0));
    assert(near(restWheel.longitudinalVelocity, 0));
    assert(near(restWheel.longitudinalSlip, 0));
    assert(near(restWheel.slipAngle, 0));
    for (auto* body : treeBodies)
      assert(body->flags & physics::kBodyAsleep);

    // suspendCar (00458c00 `suspend`) sleeps the whole tree; a second call
    // is a no-op on an already-sleeping tree.
    wakeCar(*restCar);
    for (auto* body : treeBodies)
      assert(!(body->flags & physics::kBodyAsleep));
    suspendCar(*restCar);
    for (auto* body : treeBodies)
      assert(body->flags & physics::kBodyAsleep);
    suspendCar(*restCar);
    for (auto* body : treeBodies)
      assert(body->flags & physics::kBodyAsleep);
  }
  std::cout << "car physics: ok\n";
}
