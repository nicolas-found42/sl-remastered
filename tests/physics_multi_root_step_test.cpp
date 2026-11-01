#include "physics_multi_root_step.hpp"
#include "physics_suspension.hpp"
#include <cmath>
#include <cstdio>
#include <string>
using namespace physics;

int main() {
  BodyProto proto;
  proto.invMass = 1;
  proto.flags = kProtoSimple;
  Body root, child, camera;
  root.proto = child.proto = camera.proto = &proto;
  root.flags = kBodyActive | kBodyHasChildren | kBodyNoRotation;
  child.flags = kBodyActive | kBodyChild;
  camera.flags = kBodyActive | kBodyNoRotation;
  child.attachment = BodyAttachment{&root, {{2, 0, 0}, {0, 0, 0, 1}}};
  root.state[0].vel = {2, 0, 0};
  camera.state[0].vel = {0, 2, 0};
  std::vector<BodyTree> roots{{&root, &child}, {&camera}};
  std::string error;
  // Failure in a later root restores earlier predictions.
  camera.state[1].stepFlags = 1;
  if (predictRootSet(roots, 0.5f, error) || root.state[1].stepFlags ||
      root.current || child.current)
    return 1;
  camera.state[1].stepFlags = 0;
  if (!predictRootSet(roots, 0.5f, error) || root.current || child.current ||
      camera.current || child.state[1].pos != Vec3({3, 0, 0}) ||
      camera.state[1].pos != Vec3({0, 1, 0}))
    return 2;
  // Begin a fresh world step with no pre-existing predictions.
  root.state[1] = {};
  child.state[1] = {};
  camera.state[1] = {};
  World world;
  world.params.gravityEnabled = false;
  world.bodies = {&root, &camera};
  MultiRootWork work;
  work.collisionFreeProven = true;
  const auto oldRoot = root.state[0];
  const auto oldCamera = camera.state[0];
  for (int mode = 0; mode < 6; ++mode) {
    auto unsupported = work;
    if (mode == 0)
      unsupported.collisionFreeProven = false;
    if (mode == 1)
      unsupported.contacts = true;
    if (mode == 2)
      unsupported.joints = true;
    if (mode == 3)
      unsupported.wheelConstraints = true;
    if (mode == 4)
      unsupported.callbacks = true;
    if (mode == 5)
      unsupported.waterOrClipPlanes = true;
    if (stepMultiRootWorld(world, roots, 0.5f, unsupported, error) !=
            FreeBodyStepResult::Unsupported ||
        world.time != 0 || root.current || camera.current ||
        root.state[0].pos != oldRoot.pos ||
        camera.state[0].force != oldCamera.force)
      return 3;
  }
  if (stepMultiRootWorld(world, roots, 0.5f, work, error) !=
          FreeBodyStepResult::Advanced ||
      root.current != 1 || child.current != 1 || camera.current != 1 ||
      child.state[1].pos != Vec3({3, 0, 0}) ||
      camera.state[1].pos != Vec3({0, 1, 0}) ||
      std::fabs(world.time - 0.5f) > 1e-6f || world.stepsDone != 2)
    return 4;
  auto duplicate = roots;
  duplicate.push_back({&camera});
  const auto position = root.state[root.current].pos;
  if (stepMultiRootWorld(world, duplicate, 0.1f, work, error) !=
          FreeBodyStepResult::Unsupported ||
      world.time != 0.5f || root.state[root.current].pos != position)
    return 5;
  // The camera observes the compound root before prediction. A later callback
  // failure restores both the body and the first callback's smoothing state.
  Body car, wheel, follower;
  car.proto = wheel.proto = follower.proto = &proto;
  car.flags = kBodyActive | kBodyHasChildren | kBodyNoRotation;
  wheel.flags = kBodyActive | kBodyChild;
  follower.flags = kBodyActive | kBodyNoRotation;
  wheel.attachment = BodyAttachment{&car, {{2, 0, 0}, {0, 0, 0, 1}}};
  car.state[0].pos = {10, 0, 0};
  car.state[0].vel = {2, 0, 0};
  World followWorld;
  followWorld.params.gravityEnabled = false;
  followWorld.bodies = {&car, &follower};
  std::vector<BodyTree> followRoots{{&car, &wheel}, {&follower}};
  CameraFollowSmoothing smoothing;
  CameraFollowStep cb{};
  cb.camera = &follower;
  cb.look = cb.move = &car;
  cb.smoothing = &smoothing;
  cb.input.otherBranchesInactive = true;
  cb.input.command.force = 2;
  cb.input.command.moveDistance = 1;
  cb.input.maxForce = 3;
  MultiRootWork followWork;
  followWork.collisionFreeProven = true;
  auto failing = cb;
  failing.input.maxForce = 0;
  followWork.cameraCallbacks = {cb, failing};
  // FUN_004a3f90 clears this prediction flag during the force pass, so it
  // cannot be used to provoke a later prediction failure.
  follower.state[1].stepFlags = 1;
  if (stepMultiRootWorld(followWorld, followRoots, 0.5f, followWork, error) !=
          FreeBodyStepResult::Unsupported ||
      followWorld.time != 0 || !smoothing.initializeLook ||
      !smoothing.initializeMove || follower.state[0].force != Vec3{} ||
      car.state[1].stepFlags)
    return 6;
  followWork.cameraCallbacks = {cb};
  follower.state[1].stepFlags = 0;
  if (stepMultiRootWorld(followWorld, followRoots, 0.5f, followWork, error) !=
          FreeBodyStepResult::Advanced ||
      smoothing.look != Vec3({10, 0, 0}) ||
      smoothing.move != Vec3({10, 0, 0}) ||
      follower.state[follower.current].vel[0] <= 0 ||
      car.state[car.current].pos[0] <= 10)
    return 7;
  Body controlled;
  controlled.proto = &proto;
  controlled.flags = kBodyActive | kBodyNoRotation;
  controlled.state[0].vel = {1, 0, 0};
  World controlWorld;
  controlWorld.params.gravityEnabled = false;
  controlWorld.bodies = {&controlled};
  MultiRootWork controlWork;
  controlWork.collisionFreeProven = true;
  bool called = false;
  controlWork.beforePrediction = [&](std::string &) {
    called = true;
    addForce(controlled, {4, 0, 0}, nullptr);
    return false;
  };
  if (stepMultiRootWorld(controlWorld, {{&controlled}}, 0.5f, controlWork,
                         error) != FreeBodyStepResult::Unsupported ||
      !called || controlled.state[0].force != Vec3{} || controlWorld.time != 0)
    return 8;
  controlWork.beforePrediction = [&](std::string &) {
    addForce(controlled, {4, 0, 0}, nullptr);
    return true;
  };
  if (stepMultiRootWorld(controlWorld, {{&controlled}}, 0.5f, controlWork,
                         error) != FreeBodyStepResult::Advanced ||
      controlled.state[controlled.current].vel[0] <= 1)
    return 9;
  BodyProto sphere = proto;
  sphere.shape.type = ShapeType::Sphere;
  Body carRoot, cameraRoot, otherRoot;
  carRoot.proto = cameraRoot.proto = otherRoot.proto = &sphere;
  carRoot.flags = otherRoot.flags = kBodyActive;
  cameraRoot.flags = kBodyActive | 0x20u;
  World dynamicWorld;
  dynamicWorld.bodies = {&carRoot, &cameraRoot, &otherRoot};
  std::vector<BodyTree> dynamicRoots{{&carRoot}, {&cameraRoot}, {&otherRoot}};
  std::vector<DynamicRootPair> candidates;
  if (!discoverDynamicRootPairs(dynamicWorld, dynamicRoots, candidates,
                                error) ||
      candidates.size() != 1 || candidates[0].first != &carRoot ||
      candidates[0].second != &otherRoot)
    return 10;
  cameraRoot.flags &= ~0x20u;
  if (!discoverDynamicRootPairs(dynamicWorld, dynamicRoots, candidates,
                                error) ||
      candidates.size() != 3)
    return 11;
  cameraRoot.state[1].stepFlags = 2;
  if (!discoverDynamicRootPairs(dynamicWorld, dynamicRoots, candidates,
                                error) ||
      candidates.size() != 1)
    return 12;
  dynamicWorld.bodies.pop_back();
  if (discoverDynamicRootPairs(dynamicWorld, dynamicRoots, candidates, error) ||
      candidates.size() != 1)
    return 13;
  sphere.shape.boundRadius = 1;
  for (float &extent : sphere.shape.extent)
    extent = 1;
  Body part;
  part.proto = &sphere;
  part.flags = kBodyActive | kBodyChild;
  part.attachment = BodyAttachment{&carRoot, {}};
  carRoot.flags |= kBodyHasChildren;
  if (!discoverDynamicLeafPairs({&carRoot, &part}, {&otherRoot}, candidates,
                                error) ||
      candidates.size() != 1 || candidates[0].first != &part ||
      candidates[0].second != &otherRoot)
    return 14;
  otherRoot.state[1].pos = {5, 0, 0};
  if (!discoverDynamicLeafPairs({&carRoot, &part}, {&otherRoot}, candidates,
                                error) ||
      !candidates.empty())
    return 15;
  otherRoot.state[1].pos = {};
  part.flags |= 0x40u;
  if (discoverDynamicLeafPairs({&carRoot, &part}, {&otherRoot}, candidates,
                               error))
    return 16;
  // A suspension that never held a contact keeps its built length (stock
  // updates constraints only while the world is unsettled). Once it has held a
  // contact body, losing the contact still runs the update, which extends the
  // length to the maximum: contact -> airborne must not freeze a compressed
  // wheel.
  {
    BodyProto staticProto = proto;
    staticProto.flags = kProtoStatic;
    Body suspCar, suspWheel, ground;
    suspCar.proto = suspWheel.proto = &proto;
    ground.proto = &staticProto;
    suspCar.flags = kBodyActive | kBodyHasChildren | kBodyNoRotation;
    suspWheel.flags = kBodyActive | kBodyChild;
    suspWheel.attachment = BodyAttachment{&suspCar, {{0, -1, 0}, {0, 0, 0, 1}}};
    suspCar.state[0].vel = {1, 0, 0};
    World suspWorld;
    suspWorld.params.gravityEnabled = false;
    suspWorld.bodies = {&suspCar};
    std::vector<BodyTree> suspRoots{{&suspCar, &suspWheel}};
    SuspensionUpdate u{};
    u.settings = configureSuspension(1000, 200, 0.7f, 0.705f, 0, 100, 0.3f);
    u.wheelBody = &suspWheel;
    u.length = 0.605f;
    u.distanceSquared = 1.1f;
    MultiRootWork suspWork;
    suspWork.collisionFreeProven = true;
    suspWork.suspensions = {&u};
    if (stepMultiRootWorld(suspWorld, suspRoots, 0.1f, suspWork, error) !=
        FreeBodyStepResult::Advanced)
      return 17;
    if (std::fabs(u.length - 0.605f) > 1e-6f)
      return 18; // never-touched suspension must keep its built length
    suspCar.flags = kBodyActive | kBodyHasChildren | kBodyNoRotation;
    suspCar.state[suspCar.current].vel = {1, 0, 0};
    u.contactBody = &ground; // a held contact, now out of reach
    u.distanceSquared = 4.0f;
    u.length = 0.4f;
    if (stepMultiRootWorld(suspWorld, suspRoots, 0.1f, suspWork, error) !=
        FreeBodyStepResult::Advanced) {
      std::fprintf(stderr, "step 2: %s\n", error.c_str());
      return 19;
    }
    if (std::fabs(u.length - u.settings.maxLength) > 1e-6f || u.force != 0)
      return 20; // airborne after a contact extends to the maximum
  }
  // Stock 0049b830 keeps one pending contact per dynamic root. Three
  // simultaneous contacts of one body with the static ground (which never
  // holds a pending mark) correct only the smallest-separation one in a pass; without the
  // rule all three are corrected and their impulses stack.
  {
    BodyProto dynamicProto;
    dynamicProto.invMass = 1;
    dynamicProto.flags = kProtoSimple;
    BodyProto staticProto;
    staticProto.flags = kProtoStatic;
    for (const bool onePerRoot : {false, true}) {
      Body falling, ground;
      falling.proto = &dynamicProto;
      ground.proto = &staticProto;
      falling.flags = kBodyActive | kBodyNoRotation;
      falling.state[0].vel = {0, -5, 0};
      World contactWorld;
      contactWorld.params.gravityEnabled = false;
      contactWorld.bodies = {&falling};
      std::vector<BodyTree> contactRoots{{&falling}};
      MultiRootWork contactWork;
      contactWork.contacts = true;
      contactWork.onePendingContactPerRoot = onePerRoot;
      const float separation[3] = {-0.006f, -0.02f, -0.001f};
      unsigned passes = 0;
      std::vector<float> corrected;
      contactWork.gatherContacts = [&](std::vector<DirectContact> &out,
                                       std::string &) {
        if (passes++)
          return true;
        for (int i = 0; i < 3; ++i) {
          DirectContact c;
          c.a = &falling;
          c.b = &ground;
          c.normal = {0, 1, 0};
          c.pointA = {float(i) * 0.1f, 0, 0};
          c.pointB = {float(i) * 0.1f, 0, 0};
          c.separation = separation[i];
          out.push_back(c);
        }
        return true;
      };
      contactWork.finishContacts = [&](const std::vector<DirectContact> &done) {
        if (corrected.empty())
          for (const auto &c : done)
            if (c.normalImpulse != 0)
              corrected.push_back(c.separation);
      };
      if (stepMultiRootWorld(contactWorld, contactRoots, 0.01f, contactWork,
                             error) != FreeBodyStepResult::Advanced) {
        std::fprintf(stderr, "contact step: %s\n", error.c_str());
        return 21;
      }
      if (onePerRoot ? (corrected.size() != 1 || corrected[0] != -0.02f)
                     : corrected.size() != 3)
        return onePerRoot ? 22 : 23;
    }
  }
  // Sleeping dynamic roots remain registered contact targets. A real
  // correction wakes them in-pass; a later gather failure rolls the wake,
  // activity seed and modified velocities back with the complete tree.
  {
    Body sleeper, striker;
    sleeper.proto = striker.proto = &proto;
    sleeper.flags = kBodyActive | kBodyNoRotation | kBodyAsleep;
    striker.flags = kBodyActive | kBodyNoRotation;
    striker.state[0].vel = {-2, 0, 0};
    World wakeWorld;
    wakeWorld.params.gravityEnabled = false;
    wakeWorld.bodies = {&sleeper, &striker};
    MultiRootWork wakeWork;
    wakeWork.contacts = true;
    unsigned gather = 0;
    bool injectFailure = true;
    wakeWork.gatherContacts = [&](std::vector<DirectContact>& out, std::string& why) {
      if (gather++) {
        if (injectFailure) { why = "injected failure after contact wake"; return false; }
        return true;
      }
      DirectContact c;
      c.a = &sleeper;
      c.b = &striker;
      c.normal = {-1, 0, 0};
      c.separation = 0.025f;
      out.push_back(c);
      return true;
    };
    const std::vector<BodyTree> wakeRoots{{&sleeper}, {&striker}};
    if (stepMultiRootWorld(wakeWorld, wakeRoots, 0.01f, wakeWork, error) !=
            FreeBodyStepResult::Unsupported || !(sleeper.flags & kBodyAsleep) ||
        sleeper.activity != Vec3{} || sleeper.state[0].vel != Vec3{} || wakeWorld.time != 0)
      return 24;
    injectFailure = false;
    gather = 0;
    if (stepMultiRootWorld(wakeWorld, wakeRoots, 0.01f, wakeWork, error) !=
            FreeBodyStepResult::Advanced || (sleeper.flags & kBodyAsleep) ||
        sleeper.activity[1] < 1000 || sleeper.state[sleeper.current].vel[0] >= 0)
      return 25;
  }
  return 0;
}
