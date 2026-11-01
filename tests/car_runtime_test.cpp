#include "car_runtime.hpp"
#include "game_creation.hpp"
#include "natives/game_type.hpp"
#include "resource_world.hpp"
#include "world_model.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace {
// One world per scenario: install state is per node and every scenario needs a
// fresh car.
struct InstallFixture {
  // Same pack layout as main(): the installed 2005 tree, whose slots the
  // car resource ids (0x150104, 0x15b157, 0x20201) are expressed against.
  InstallFixture()
      : packs(".scratch/2005-extract/Program Files/Street Legal Racing - "
              "Redline"),
        resources(world, packs) {
    assert(world.addRoot());
    world.setClassResolver([](const std::string&) { return true; });
    state.packs = &packs;
    state.world = &world;
    state.resources = &resources;
    arena.gameState = &state;
  }
  // Pack slots are assigned in first-seen order, so ids must come from
  // resolveLiteral against this fixture's own registry.
  uint32_t id(const char* packLiteral, uint32_t entry) {
    uint32_t resolved = 0;
    std::string resolveError;
    assert(packs.resolveLiteral(packLiteral, entry, resolved, resolveError));
    return resolved;
  }
  WorldNode* add(uint32_t scriptId, const char* scriptClass, const char* config,
                 const char* device) {
    const auto created = createNativeInstance(
        world,
        GameCreationRequest{scriptId, scriptClass, std::nullopt,
                            std::string{"0,0,0,0,0,0,0"}, "instance", device,
                            std::string{config}},
        &binding, &resources);
    if (created.status != CreationStatus::Created || !created.node) {
      std::fprintf(stderr, "add %s failed: status=%d issue=%d detail=%s\n",
                   scriptClass, static_cast<int>(created.status),
                   static_cast<int>(created.issue), created.detail.c_str());
      std::abort();
    }
    return created.node;
  }
  static const RuntimeSlotState* slot(const WorldNode* node, int32_t id) {
    if (!node || !node->carInstance)
      return nullptr;
    for (const auto& entry : node->carInstance->slots)
      if (entry && entry->id == id)
        return entry.get();
    return nullptr;
  }
  const CarPrototype* prototype(const WorldNode* node) {
    if (!node || !node->carInstance)
      return nullptr;
    const uint32_t id = node->carInstance->prototypeSource.cachedValue
                            ? node->carInstance->prototypeSource.cachedValue
                            : node->scriptLink.cachedValue;
    auto* script = world.findScript(id);
    return script ? script->car.get() : nullptr;
  }

  PackRegistry packs;
  GameWorld world;
  ResourceWorld resources;
  VmGameState state;
  VmArena arena;
  ReceiverBinding binding;
};

constexpr const char* kCarClass = "java.game.cars.Baiern_DevilSport";
constexpr const char* kCarConfig =
    "cars\\racers\\Baiern_data\\scripts\\_main_DevilSport.cfg";
constexpr const char* kDoorClass = "java.game.cars.Baiern_FL_door";
constexpr const char* kDoorConfig =
    "cars\\racers\\Baiern_data\\scripts\\FL_door.cfg";
constexpr const char* kMirrorClass = "java.game.cars.Baiern_L_mirror";
constexpr const char* kMirrorConfig =
    "cars\\racers\\Baiern_data\\scripts\\L_mirror.cfg";
constexpr const char* kHoodClass = "java.game.cars.Baiern_hood";
constexpr const char* kHoodConfig =
    "cars\\racers\\Baiern_data\\scripts\\hood.cfg";
constexpr const char* kRimClass =
    "java.game.parts.wheels.rims.rim_Baiern_DTM_11_0_19_ET__25";
constexpr const char* kRimConfig =
    "parts\\wheels\\rims\\scripts\\rim_Baiern_DTM_11_0_19_ET__25_LOD_"
    "CATALOG_GARAGE.cfg";
constexpr const char* kTyreClass =
    "java.game.parts.wheels.tyres.tyre_235_45_19_9_0";
constexpr const char* kTyreConfig =
    "parts\\wheels\\tyres\\scripts\\tyre_235_45_19_9_0_LOD_CATALOG_"
    "GARAGE.cfg";
} // namespace

int main() {
  {
    GameWorld world;
    auto* root = world.addRoot();
    auto* player = world.addChild(*root, "control snapshot player", 0x21);
    assert(player);
    world.markLoaded(*player);
    player->nativeDevice = "player";
    player->player = std::make_unique<PlayerInstance>();
    player->playerInput = std::make_unique<PlayerInputState>();
    float key = 1;
    player->playerInput->sampleInput = [&](int32_t, int32_t) {
      return std::optional<float>{key};
    };
    player->playerInput->inputType = [](int32_t, int32_t) {
      return std::optional<int32_t>{0};
    };
    assert(player->playerInput->add(1, 0, 0xcb, 0, {0, 1, 0, -1}, 0) == 1);
    assert(player->playerInput->add(0x1c, 0, 0x11, 0, {0, 1, 0, 1}, 0) == 2);
    assert(player->playerInput->add(0x31, 0, 0x39, 0, {0, 1, 0, 1}, 0) == 3);
    player->playerInput->beginFrame(0.00025f);
    CarSeat seat;
    setLinkOwner(seat.player, player);
    auto result = sampleCarControls(world, seat);
    assert(result && std::fabs(result->continuous[0] + 0.25f) < 1e-6f &&
           std::fabs(result->continuous[1] - 0.25f) < 1e-6f &&
           result->continuous[2] == 0 && result->digital[0] == 31);
    key = 0;
    result = sampleCarControls(world, seat);
    assert(result && result->digital[0] == 31 &&
           std::fabs(result->continuous[0] + 0.25f) < 1e-6f);
    player->playerInput->beginFrame(0.00025f);
    result = sampleCarControls(world, seat);
    assert(result && result->continuous[0] == 0 && result->digital[0] == 0);
    player->player->scriptedControlOverride = true;
    assert(!sampleCarControls(world, seat));
    player->player->scriptedControlOverride = false;
    player->playerInput->inputType = [](int32_t, int32_t) {
      return std::optional<int32_t>{1};
    };
    player->playerInput->beginFrame(0.001f);
    assert(!sampleCarControls(world, seat));
    assert(world.destroy(*player) && !seat.player.owner &&
           !sampleCarControls(world, seat));
  }
  {
    LinkOwner player, camera, render, osd, other;
    player.value = 1;
    other.value = 2;
    CarRuntimeInstance car;
    car.seats.push_back(std::make_unique<CarSeat>());
    car.seats.push_back(std::make_unique<CarSeat>());
    auto* first = car.seats[0].get();
    auto* second = car.seats[1].get();
    assert(car.selectSeat(1) == first);
    setLinkOwner(first->player, &player);
    setLinkOwner(first->camera, &camera);
    setLinkOwner(first->renderInstance, &render);
    setLinkOwner(first->osd, &osd);
    assert(car.selectSeat(1) == first && car.selectSeat(2) == second);
    setLinkOwner(second->player, &other);
    assert(car.selectSeat(3) == nullptr);
    car.seats.clear();
    assert(!player.head && !camera.head && !render.head && !osd.head &&
           !other.head);
  }
  PackRegistry packs(
      ".scratch/2005-extract/Program Files/Street Legal Racing - Redline");
  GameWorld world;
  assert(world.addRoot());
  ResourceWorld resources(world, packs);
  std::string error, text;
  assert(!readNativeConfig(packs, "../outside.cfg", text, error));
  assert(!readNativeConfig(packs, "/absolute.cfg", text, error));
  assert(!readNativeConfig(packs, "C:\\outside.cfg", text, error));
  const std::string path =
      "cars\\racers\\Baiern_data\\scripts\\_main_DevilSport.cfg";
  if (!readNativeConfig(packs, path, text, error) || text.empty())
    std::fprintf(stderr, "readNativeConfig failed: error=%s\n", error.c_str());
  assert(!text.empty());
  uint32_t id = 0;
  assert(packs.resolveLiteral("cars\\racers\\Baiern.rpk", 0x157, id, error));
  const auto resolved = resources.lookup(id, 8);
  assert(resolved.node.script);
  auto& source = *resolved.node.script;
  PackView view;
  assert(packs.view(static_cast<uint16_t>(id >> 16), view, error) ==
         PackViewStatus::Ok);
  const ResourceIdMapping mapping{
      view.slot, static_cast<uint32_t>(packs.slotCount()), view.dependencies};
  const auto nodesBefore = world.nodeCount();
  std::unique_ptr<CarPrototype> prototype;
  assert(loadCarPrototype(resources, source, mapping, text, prototype, error));
  assert(error.empty() && !source.loaded && !source.car);
  assert(world.nodeCount() == nodesBefore);
  assert(prototype && prototype->source.owner == &source);
  assert(!prototype->resources.empty());
  for (const auto& resource : prototype->resources)
    assert(resource->id ? resource->owner.owner &&
                              resource->owner.cachedValue == resource->id
                        : !resource->owner.owner);
  prototype.reset();
  const CarCameraSettings initialCamera{0.4f, 1.25f};
  assert(
      loadCarScriptPrototype(resources, source, path, error, &initialCamera));
  assert(!source.loaded && source.car && world.nodeCount() == nodesBefore);
  assert(source.car->stage == CarPrototypeStage::PhysicsGeometryReady);
  auto* cached = source.car.get();
  const CarCameraSettings laterCamera{0.9f, 2.0f};
  assert(loadCarScriptPrototype(resources, source, path, error, &laterCamera));
  assert(source.car.get() == cached);
  assert(source.car->cameraSettings &&
         source.car->cameraSettings->distanceScale == 0.4f &&
         source.car->cameraSettings->fieldOfViewScale == 1.25f);
  world.setClassResolver([](const std::string&) { return true; });
  ReceiverBinding binding;
  bool initialized = false;
  binding.initializeReceiver = [&](WorldNode&) -> std::optional<std::string> {
    initialized = true;
    return std::nullopt;
  };
  GameCreationRequest request{
      id, "test.Car", {}, std::string{"1,2,3,0,0,0,0"}, "car", "car", path};
  const auto created =
      createNativeInstance(world, request, &binding, &resources);
  assert(created.status == CreationStatus::Created && created.node);
  assert(initialized && binding.link.owner == created.node && source.loaded);
  assert(created.node->carInstance && created.node->carInstance->isCar);
  assert(created.node->carInstance->carPhysics &&
         created.node->carInstance->carLink.owner == created.node);
  {
    auto& car = *created.node->carInstance;
    const auto& wheels = car.carPhysics->wheels;
    assert(wheels.size() >= 2);
    assert(!car.wheelRefs[0] && !car.wheelRefs[1]);
    car.wheelExtras[0].steer = car.wheelExtras[1].steer = 1;
    car.steeringState = 0;
    auto angles = calculateCarSteeringAngles(car, source.car->config);
    assert(angles && (*angles)[0] == 0 && (*angles)[1] == 0);
    car.steeringState = 0.0001f;
    angles = calculateCarSteeringAngles(car, source.car->config);
    assert(angles && (*angles)[0] == 0 && (*angles)[1] == 0);
    car.steeringState = 0.5f;
    const float angle = source.car->config.maxSteer[0] * car.steeringState;
    const float base = source.car->config.maxSteer[1];
    assert(base > 0);
    const float radius = base / std::tan(angle);
    angles = calculateCarSteeringAngles(car, source.car->config);
    assert(angles &&
           std::fabs((*angles)[0] -
                     std::atan2(base, radius + wheels[0]->anchor[0])) < 1e-6f &&
           std::fabs((*angles)[1] -
                     std::atan2(base, radius + wheels[1]->anchor[0])) < 1e-6f);
    car.steeringState = -0.5f;
    angles = calculateCarSteeringAngles(car, source.car->config);
    assert(angles &&
           std::fabs((*angles)[0] +
                     std::atan2(base, radius - wheels[0]->anchor[0])) < 1e-6f &&
           std::fabs((*angles)[1] +
                     std::atan2(base, radius - wheels[1]->anchor[0])) < 1e-6f);
    auto noAckermann = source.car->config;
    noAckermann.maxSteer[1] = 0;
    auto missing = std::move(car.carPhysics->wheels[1]);
    car.wheelSteeringAngles[1] = 0.33f;
    angles = calculateCarSteeringAngles(car, noAckermann);
    assert(angles && std::fabs((*angles)[0] + angle) < 1e-6f &&
           (*angles)[1] == 0.33f);
    car.carPhysics->wheels[1] = std::move(missing);
  }
  assert(created.node->carInstance->position[0] == 1.0f &&
         created.node->carInstance->position[1] == 2.0f);
  auto* physicsRoot =
      world.nodeFor(created.node->carInstance->physicsRoot.owner);
  assert(physicsRoot && physicsRoot->type == 2 &&
         physicsRoot->name == "car_root");
  assert(world.parent(*physicsRoot) == created.node &&
         world.dynamicBodyCount() == 1);
  auto body = world.dynamicBody(*physicsRoot);
  assert(body.lock().get() ==
         created.node->carInstance->carPhysics->root.get());
  VmGameState game;
  game.world = &world;
  game.resources = &resources;
  VmArena arena;
  arena.gameState = &game;
  auto object = std::make_unique<VmObject>();
  object->fields.push_back({"ptr", int32_t{1}, "I"});
  auto* carRef = object.get();
  arena.objects.push_back(std::move(object));
  arena.resourceStates.push_back({carRef, {}});
  game.bindings[1] = std::make_unique<ReceiverBinding>();
  setLinkOwner(game.bindings[1]->link, created.node);
  const auto info =
      gameRefGetInfoIntInt({int32_t{1}, int32_t{0}}, carRef, &arena, nullptr);
  const auto* value = std::get_if<VmValue>(&info);
  assert(value && std::get<int32_t>(*value) ==
                      static_cast<int32_t>(physicsRoot->instanceId));
  assert(world.destroy(*created.node));
  assert(body.expired() && world.dynamicBodyCount() == 0 &&
         !game.bindings[1]->link.owner);
  assert(world.nodeCount() == nodesBefore);
  binding.initializeReceiver = [](WorldNode&) -> std::optional<std::string> {
    return std::string{"constructor failure"};
  };
  const auto failed =
      createNativeInstance(world, request, &binding, &resources);
  assert(failed.status == CreationStatus::Unsupported && !failed.node);
  assert(world.nodeCount() == nodesBefore && world.dynamicBodyCount() == 0);
  binding.initializeReceiver = nullptr;
  const auto recreated =
      createNativeInstance(world, request, &binding, &resources);
  assert(recreated.status == CreationStatus::Created && recreated.node);
  auto* recreatedRoot =
      world.nodeFor(recreated.node->carInstance->physicsRoot.owner);
  assert(recreatedRoot && world.destroy(*recreatedRoot));
  assert(!recreated.node->carInstance->physicsRoot.owner &&
         world.dynamicBodyCount() == 0);
  assert(world.destroy(*recreated.node) && world.nodeCount() == nodesBefore);

  // 00470910: compatibility is attachments-vs-attachments, so the mirror's
  // `attach 0x115 24` pairs with the door's slot 24 and nothing else.
  {
    InstallFixture f;
    auto* door = f.add(f.id("cars\\racers\\Baiern.rpk", 0x115), kDoorClass,
                       kDoorConfig, "part");
    auto* mirror = f.add(f.id("cars\\racers\\Baiern.rpk", 0x104), kMirrorClass,
                         kMirrorConfig, "part");
    const auto* doorProto = f.prototype(door);
    const auto* mirrorProto = f.prototype(mirror);
    assert(doorProto && mirrorProto);
    assert(carPartSlotsCompatible(*mirrorProto, 24, *doorProto, 24));
    assert(carPartSlotsCompatible(*doorProto, 24, *mirrorProto, 24));
    assert(!carPartSlotsCompatible(*mirrorProto, 24, *doorProto, 5));
    assert(!carPartSlotsCompatible(*mirrorProto, 24, *doorProto, 6));
    assert(!carPartSlotsCompatible(*mirrorProto, 24, *mirrorProto, 24));
  }

  // 00474600/00474170: a successful install fills the car's attached-part
  // registry (part+0x74/+0x78) and pairs the slot ids on both records.
  {
    InstallFixture f;
    auto* car = f.add(f.id("cars\\racers\\Baiern.rpk", 0x157), kCarClass,
                      kCarConfig, "car");
    auto* hood = f.add(f.id("cars\\racers\\Baiern.rpk", 0x10d), kHoodClass,
                       kHoodConfig, "part");
    assert(car && hood && car->carInstance && hood->carInstance);
    assert(car->carInstance->attachedParts.empty());
    std::string installError;
    assert(carPartInstall(*hood, car, 0, f.arena, installError));
    assert(installError.empty());
    assert(car->carInstance->attachedParts.size() == 1);
    assert(car->carInstance->attachedParts[0]->owner == hood);
    assert(hood->carInstance->carLink.owner == car);
    assert(hood->carInstance->stateMode == 1);
    assert(car->carInstance->usedSlots == 1 &&
           hood->carInstance->usedSlots == 1);
    assert(!hood->carInstance->slots.empty());
    const auto* partSlot = hood->carInstance->slots[0].get();
    assert((partSlot->flags & 1u) != 0 && (partSlot->flags & 4u) == 0);
    assert(partSlot->attachedNode.owner == car);
    const auto* carSlot = InstallFixture::slot(car, partSlot->attachedSlotId);
    assert(carSlot && (carSlot->flags & 2u) != 0);
    assert(carSlot->attachedSlotId == partSlot->id);
    assert(carSlot->attachedNode.owner == hood);
    assert(!carPartInstall(*hood, car, 0, f.arena, installError));
    installError.clear();
    assert(carPartRemove(*hood, 0, f.arena, installError));
    assert(car->carInstance->attachedParts.empty());
    assert(partSlot->attachedNode.owner == nullptr &&
           (partSlot->flags & 1u) == 0);
    assert(carSlot->attachedNode.owner == nullptr &&
           (carSlot->flags & 2u) == 0);
    assert(car->carInstance->usedSlots == 0);
  }

  // 00473b10 with getInfo 0x31: the search descends into the parts installed on
  // the car, so a mirror finds the door's slot 24 even though no car slot takes
  // it.
  {
    InstallFixture f;
    auto* car = f.add(f.id("cars\\racers\\Baiern.rpk", 0x157), kCarClass,
                      kCarConfig, "car");
    auto* door = f.add(f.id("cars\\racers\\Baiern.rpk", 0x115), kDoorClass,
                       kDoorConfig, "part");
    auto* mirror = f.add(f.id("cars\\racers\\Baiern.rpk", 0x104), kMirrorClass,
                         kMirrorConfig, "part");
    assert(car && door && mirror && car->carInstance);
    std::string installError;
    assert(carPartInstall(*door, car, 0, f.arena, installError));
    assert(car->carInstance->attachedParts.size() == 1);
    WorldNode* target = car;
    int32_t targetSlotId = 0;
    assert(carPartFindCompatibleSlot(*mirror, target, targetSlotId, f.world) ==
           24);
    assert(targetSlotId == 24 && target == door);
    assert(carPartInstall(*mirror, car, 0, f.arena, installError));
    assert(installError.empty());
    assert(car->carInstance->attachedParts.size() == 2);
    assert(!mirror->carInstance->slots.empty());
    const auto* mirrorSlot = mirror->carInstance->slots[0].get();
    assert(mirrorSlot->attachedNode.owner == door);
    assert(InstallFixture::slot(door, mirrorSlot->attachedSlotId) != nullptr);
    assert(door->carInstance->usedSlots == 2);
    assert(mirror->carInstance->partPhysics && door->carInstance->partPhysics &&
           car->carInstance->carPhysics);
    assert(mirror->carInstance->partPhysics->installedParent ==
           door->carInstance->partPhysics.get());
    assert(door->carInstance->partPhysics->installedParent ==
           car->carInstance->carPhysics->part.get());
    // The registry is not the route: with the mirror removed and the registry
    // cleared, the slot records alone still offer the door's slot 24.
    assert(carPartRemove(*mirror, 0, f.arena, installError));
    for (auto& link : car->carInstance->attachedParts)
      setLinkOwner(*link, nullptr);
    car->carInstance->attachedParts.clear();
    target = car;
    targetSlotId = 0;
    auto* otherMirror = f.add(f.id("cars\\racers\\Baiern.rpk", 0x104),
                              kMirrorClass, kMirrorConfig, "part");
    assert(carPartFindCompatibleSlot(*otherMirror, target, targetSlotId,
                                     f.world) == 24);
    assert(target == door);
  }

  // `tyre_...cfg` slot 1 has `attach 0x20200 2`; the rim's slot 2 carries
  // `compatible 0x20200 2`, so only the rim takes the tyre.
  {
    InstallFixture f;
    auto* rim =
        f.add(f.id("parts\\wheels.rpk", 0x2de), kRimClass, kRimConfig, "part");
    auto* tyre = f.add(f.id("parts\\wheels.rpk", 0x3e5), kTyreClass,
                       kTyreConfig, "part");
    assert(rim && tyre);
    WorldNode* target = rim;
    int32_t targetSlotId = 0;
    assert(carPartFindCompatibleSlot(*tyre, target, targetSlotId, f.world) ==
           1);
    assert(targetSlotId == 2 && target == rim);
  }
  {
    CarRuntimeInstance car;
    CarConfig config;
    CarControlSnapshot input;
    CarControlStepContext step;
    std::string error;
    step.time = 10;
    step.step = 0.1f;
    input.continuous[1] = 1;
    input.digital[2] = 127;
    assert(car.advanceControls(input, config, step, error));
    assert(car.currentGear == 1 && car.clutchState == 1 &&
           car.throttleState == 0);
    step.time = 10.1f;
    assert(car.advanceControls(input, config, step, error));
    assert(car.currentGear == 1);
    input.digital[2] = 0;
    step.time = 10.4f;
    assert(car.advanceControls(input, config, step, error));
    assert(car.currentGear == 1 && car.clutchState == 0 &&
           car.throttleState == 1);
    input.digital[2] = -127;
    assert(car.advanceControls(input, config, step, error));
    assert(car.currentGear == 0);
    input.digital[2] = 0;
    assert(car.advanceControls(input, config, step, error));
    input.digital[2] = -127;
    assert(car.advanceControls(input, config, step, error));
    assert(car.currentGear == 7);
    step.automatic = true;
    step.engineRunning = true;
    step.drivenWheelSpeed = 2;
    step.time = 20;
    const auto gear = car.currentGear;
    const auto clutch = car.clutchState;
    assert(!car.advanceControls(input, config, step, error));
    assert(car.currentGear == gear && car.clutchState == clutch &&
           !error.empty());
    step.automatic = false;
    input.digital[2] = 0;
    car.throttleState = -std::numeric_limits<float>::max();
    input.continuous[1] = std::numeric_limits<float>::max();
    const auto unchangedGear = car.currentGear;
    assert(!car.advanceControls(input, config, step, error));
    assert(car.throttleState == -std::numeric_limits<float>::max() &&
           car.currentGear == unchangedGear);
    car.throttleState = 0;
    input.continuous[1] = 1;
    input.digital[2] = 127;
    car.forcedBrake = true;
    assert(car.advanceControls(input, config, step, error));
    assert(car.currentGear == 0 && car.brakeState == 1 &&
           car.handbrakeState == 1 && car.throttleState == 0);
  }
  {
    // Mode-4 ABS/ASR lamps follow actual stock pedal interventions.
    CarRuntimeInstance car;
    CarConfig config;
    car.carPhysics = std::make_shared<CarPhysics>();
    auto wheel = std::make_unique<CarWheelRuntime>();
    wheel->suspensionBody = std::make_unique<physics::Body>();
    wheel->suspension.force = 1;
    wheel->longitudinalSlip = 1;
    car.carPhysics->wheels.push_back(std::move(wheel));
    car.wheelExtras[0].brake = car.wheelExtras[0].drive = 1;
    car.abs = car.asr = 1;
    CarControlSnapshot input;
    input.continuous[1] = input.continuous[2] = 1;
    CarControlStepContext step;
    step.step = 0.1f;
    std::string error;
    assert(car.advanceControls(input, config, step, error));
    assert(car.absActive && car.asrActive);
    assert(car.brakeState == 0 && car.throttleState == 0 && car.clutchState == 1);
    car.carPhysics->wheels[0]->longitudinalSlip = 0;
    assert(car.advanceControls(input, config, step, error));
    assert(!car.absActive && !car.asrActive);
    assert(car.brakeState == 1 && car.throttleState == 1 && car.clutchState == 0);
    car.abs = car.asr = 0;
    car.carPhysics->wheels[0]->longitudinalSlip = 1;
    assert(car.advanceControls(input, config, step, error));
    assert(!car.absActive && !car.asrActive && car.brakeState == 1 && car.throttleState == 1);
    car.abs = car.asr = 1;
    car.carPhysics->wheels[0]->suspension.force = 0;
    assert(car.advanceControls(input, config, step, error));
    assert(!car.absActive && !car.asrActive);
  }
  {
    CarRuntimeInstance car;
    CarConfig config;
    car.carPhysics = std::make_shared<CarPhysics>();
    auto wheel = std::make_unique<CarWheelRuntime>();
    wheel->suspensionBody = std::make_unique<physics::Body>();
    wheel->radius = 0.3f;
    wheel->transferCoefficient = 1;
    car.carPhysics->wheels.push_back(std::move(wheel));
    car.wheelExtras[0].drive = 1;
    car.currentGear = 1;
    car.engineAngularSpeed = 1500.0f / 9.549296379089355f;
    car.previousEngineRpm = 1500;
    car.throttleState = 0.5f;
    CarDrivetrainStepContext step;
    step.step = 0.01f;
    step.engineRunning = true;
    step.damageEnabled = false;
    step.rpmLimit = 6000;
    step.maximumRpm = 7000;
    step.powerScale = 200;
    step.engineInertiaFactor = 1;
    step.brakeFactor = 1;
    std::string error;
    assert(car.advanceDrivetrain(config, step, error));
    assert(car.carPhysics->wheels[0]->angularSpeed < 0 && car.driveTorque > 0);
    car.clutchState = 1;
    const float speed = car.carPhysics->wheels[0]->angularSpeed;
    assert(car.advanceDrivetrain(config, step, error));
    assert(car.carPhysics->wheels[0]->angularSpeed == speed &&
           car.couplingForce == 0);
    step.damageEnabled = true;
    assert(car.advanceDrivetrain(config, step, error));
    assert(car.engineTemperature > 20 && car.engineWear > 0);
    const float engineSpeed = car.engineAngularSpeed;
    assert(car.carPhysics->wheels[0]->angularSpeed == speed);
    step.damageEnabled = false;
    car.currentGear = 0;
    car.clutchState = std::numeric_limits<float>::quiet_NaN();
    assert(!car.advanceDrivetrain(config, step, error));
    assert(car.engineAngularSpeed == engineSpeed &&
           car.carPhysics->wheels[0]->angularSpeed == speed);
    car.clutchState = 1;
    car.handbrakeState = 1;
    car.wheelExtras[0].handBrake = 1;
    car.wheelExtras[0].brakeThermal.baseHandbrake = 1;
    car.wheelExtras[0].brakeThermal.wearRate = 10;
    car.brakeFailureSoundEnabled = true;
    car.carPhysics->wheels[0]->angularSpeed = 10;
    step.engineRunning = false;
    step.damageEnabled = true;
    assert(car.advanceDrivetrain(config, step, error));
    assert(car.wheelExtras[0].brakeThermal.wear == 1 &&
           car.wheelExtras[0].bearing == 100 && car.failureSounds.size() == 1 &&
           car.failureSounds[0].kind == CarFailureSoundEvent::Kind::Brake);
    assert(car.engineAngularSpeed == 0 &&
           car.carPhysics->wheels[0]->angularSpeed < 10);
    const auto live = car.drivetrainContext(config, 0.01f);
    assert(live.maximumRpm == 7000 && live.rpmLimit == 10000 &&
           live.engineInertiaFactor == 4 && live.brakeFactor == 4 &&
           live.powerScale ==
               config.enginePower[0] *
                   (1.0f - car.engineWear * car.engineWear * 0.6f));
    car.currentGear = 1;
    car.currentGearRatio = config.gearRatios[0];
    car.shiftState = {};
    car.wheelExtras[0].drive = 1;
    car.carPhysics->wheels[0]->suspension.force = 1;
    car.carPhysics->wheels[0]->longitudinalSlip = 0;
    CarControlSnapshot controls;
    controls.continuous[1] = 1;
    CarControlStepContext controlStep;
    controlStep.time = 30;
    controlStep.step = 0.1f;
    controlStep.automatic = true;
    controlStep.engineRunning = true;
    controlStep.drivenWheelSpeed = 70;
    controlStep.displayedEngineSpeed = 840;
    assert(car.advanceControls(controls, config, controlStep, error));
    assert(car.currentGear == 2 && car.clutchState == 1 &&
           car.throttleState == 0);
  }
  {
    // Semiautomatic (stock flag 0x20, FUN_0044dc30): up/downshifts between
    // forward gears while moving, but never launches from neutral or engages
    // reverse by itself.
    CarRuntimeInstance car;
    CarConfig config;
    car.carPhysics = std::make_shared<CarPhysics>();
    auto wheel = std::make_unique<CarWheelRuntime>();
    wheel->suspensionBody = std::make_unique<physics::Body>();
    wheel->suspension.force = 1;
    wheel->config.wheelParameters[1] = 1;
    wheel->longitudinalSlip = 0;
    car.carPhysics->wheels.push_back(std::move(wheel));
    car.wheelExtras[0].drive = 1;
    car.currentGear = 1;
    car.currentGearRatio = config.gearRatios[0];
    CarControlSnapshot controls;
    controls.continuous[1] = 1;
    CarControlStepContext step;
    step.time = 40;
    step.step = 0.1f;
    step.semiautomatic = true;
    step.engineRunning = true;
    step.drivenWheelSpeed = 70;
    step.displayedEngineSpeed = 840;
    std::string error;
    assert(car.advanceControls(controls, config, step, error));
    assert(car.currentGear == 2 && car.clutchState == 1 &&
           car.throttleState == 0);
    // Stationary in neutral with throttle: semiautomatic must not launch.
    CarRuntimeInstance car2;
    car2.carPhysics = std::make_shared<CarPhysics>();
    car2.currentGear = 0;
    CarControlStepContext step2;
    step2.time = 50;
    step2.step = 0.1f;
    step2.semiautomatic = true;
    step2.engineRunning = true;
    step2.drivenWheelSpeed = 0;
    step2.displayedEngineSpeed = 840;
    CarControlSnapshot controls2;
    controls2.continuous[1] = 1;
    assert(car2.advanceControls(controls2, config, step2, error));
    assert(car2.currentGear == 0);
    // Manual shift input still works in semiautomatic mode.
    controls2.digital[2] = 127;
    step2.time = 50.1f;
    assert(car2.advanceControls(controls2, config, step2, error));
    assert(car2.currentGear == 1);
  }
  {
    CarRuntimeInstance car;
    car.carPhysics = std::make_shared<CarPhysics>();
    car.throttleState = 1;
    CarDrivetrainStepContext step;
    step.step = 0.01f;
    step.engineRunning = true;
    step.damageEnabled = false;
    step.maximumRpm = 7000;
    step.rpmLimit = 6000;
    step.powerScale = 200;
    step.engineInertiaFactor = 1;
    step.brakeFactor = 1;
    std::string error;
    assert(car.advanceDrivetrain(CarConfig{}, step, error));
    assert(car.engineAngularSpeed == 5 && car.driveTorque == 0);
  }
  {
    CarRuntimeInstance car;
    CarConfig config;
    car.carPhysics = std::make_shared<CarPhysics>();
    car.engineRunning = true;
    car.engineAngularSpeed = 7000.0f / 9.549296379089355f;
    car.previousEngineRpm = 7000;
    car.engineWear = 0.9999f;
    car.engineTemperature = 120;
    car.engineFailureSoundEnabled = true;
    const auto step = car.drivetrainContext(config, 0.01f);
    std::string error;
    assert(car.advanceDrivetrain(config, step, error));
    assert(car.engineWear == 1 && car.engineFrictionMultiplier == 50);
    assert(car.engineCoolingWear > 0 && car.engineTemperature < 120);
    assert(car.failureSounds.size() == 1 &&
           car.failureSounds[0].kind == CarFailureSoundEvent::Kind::Engine);
    assert(car.drivetrainContext(config, 0.01f).powerScale ==
           config.enginePower[0] * 0.05f);
  }
  std::printf("car runtime ok\n");
}
