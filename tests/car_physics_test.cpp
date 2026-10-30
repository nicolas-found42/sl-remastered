#include "car_physics.hpp"
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
  }
  std::cout << "car physics: ok\n";
}
