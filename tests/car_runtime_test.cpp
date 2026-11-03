#include "car_runtime.hpp"
#include "dyno_model.hpp"
#include "camera_box_pick.hpp"
#include "game_creation.hpp"
#include "natives/game_type.hpp"
#include "natives/car_part.hpp"
#include "resource_world.hpp"
#include "world_model.hpp"
#include "vm_test_harness.hpp"

#include <algorithm>
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
    if (created.node->carInstance && created.node->carInstance->partPhysics) {
      auto* root = world.nodeFor(created.node->carInstance->physicsRoot.owner);
      assert(root && root->type == 2 && root->loaded);
      auto body = world.dynamicBody(*root).lock();
      assert(body && body.get() == created.node->carInstance->partPhysics->root.get());
      assert(body->flags & physics::kBodyAsleep);
      for (const auto& child : created.node->carInstance->partPhysics->children)
        assert(!(child.body->flags & physics::kBodyAsleep));
      bool hasClickResource = false;
      const auto children = world.children(*created.node);
      assert(children);
      for (auto* child : *children)
        if (child->name == "cbone" || child->name == "click")
          hasClickResource = bool(world.dynamicBody(*child).lock());
      if (created.node->carInstance->partPhysics->clickBody ||
          created.node->carInstance->partPhysics->rootBodyIndex < 0)
        assert(hasClickResource);
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
  const VmObject* reference(WorldNode& node) {
    const auto handle = static_cast<int32_t>(arena.resourceStates.size() + 1);
    auto object = std::make_unique<VmObject>();
    object->fields.push_back({"ptr", handle, "I"});
    auto* result = object.get();
    arena.objects.push_back(std::move(object));
    arena.resourceStates.push_back({result, {}});
    state.bindings[handle] = std::make_unique<ReceiverBinding>();
    setLinkOwner(state.bindings[handle]->link, &node);
    return result;
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
    // 00449680 marks refresh dirty; 004518c0 consumes it on the Car frame.
    // An install command must not refresh slots midway through Part.load.
    InstallFixture f;
    auto* car = f.add(f.id("cars\\racers\\Baiern.rpk", 0x157), kCarClass, kCarConfig, "car");
    vm_test::World scripts;
    auto& chassis = scripts.cls("test.RefreshChassis");
    chassis.instanceField("marker", "I").instanceField("suspend_update", "I")
        .method("updatevariables", "()V", 1,
                {vm_test::literal(99), vm_test::node(0x01, true, 0),
                 vm_test::node(0x1c, true, chassis.t("marker")),
                 vm_test::node(0x08, true, 35), vm_test::retVoid});
    auto ownedReceiver = std::make_unique<VmObject>();
    auto& receiver = *ownedReceiver;
    receiver.runtimeClass = &chassis.c;
    receiver.fields = {{"marker", int32_t{0}, "I"}, {"suspend_update", int32_t{1}, "I"}};
    // Shipped Chassis fields, with a distinct installed starter/inertia pair.
    // The resulting crank acceleration must use this Car's private settings.
    VmObject ratios, dragCenter;
    for (float value : {0.0f, 3.5f, 2.7f, 1.9f, 1.3f, 0.0f, 0.0f, -4.0f})
      ratios.arrayElements.emplace_back(value);
    dragCenter.fields = {{"x", 0.0f, "F"}, {"y", 0.0f, "F"}, {"z", 0.0f, "F"}};
    const std::pair<const char*, float> tuning[] = {
        {"engine_inertia", 20}, {"starter_torque", 120},
        {"engine_friction_fwd", .0001f}, {"engine_friction_rev", .005f},
        {"engine_rpm_idle", 800}, {"engine_mass", 80},
        {"rearend_ratio", 3.3f}, {"maxRPM", 7000}, {"RPM_limit", 20000},
        {"ClutchF", 500}, {"rpm_trans_fwd", 1400}, {"rpm_trans_rev", 1400},
        {"sfx_starter_rpm", 200}, {"tank_nitro", 0}, {"consumption_nitro", 0},
        {"C_drag", .32f}};
    for (const auto& [name, value] : tuning)
      receiver.fields.push_back({name, value, "F"});
    receiver.fields.push_back({"gears", int32_t{4}, "I"});
    for (const char* name : {"SFX_trans_fwd", "SFX_trans_rev", "SFX_ignition"})
      receiver.fields.push_back({name, int32_t{0}, "I"});
    receiver.fields.push_back({"ratio", &ratios, "[F"});
    receiver.fields.push_back({"drag_center", &dragCenter, "Ljava.lang.Vector3;"});
    f.arena.objects.push_back(std::move(ownedReceiver));
    car->scriptReceiver = &receiver;
    const std::vector<const TufaClass*> classes{&chassis.c};
    int immediateCalls = 0;
    f.arena.callScript = [&](const VmObject*, const std::string&, std::vector<VmValue>) {
      ++immediateCalls;
      return VmResult{true, int32_t{0}, {}, false};
    };
    VmObject command;
    f.arena.strings.emplace_back("updatevariables", &command);
    const auto* reference = f.reference(*car);
    const auto queued = gameRefQueueEvent({static_cast<const VmObject*>(nullptr), int32_t{0x10}, &command},
                                         reference, &f.arena, &classes);
    assert(std::holds_alternative<std::monostate>(queued));
    assert(immediateCalls == 0);
    f.arena.callScript = {};
    std::string error;
    const std::vector<VmStaticFields> states;
    car->carInstance->wheelExtras[0].tire.frictionScale = 0.75f;
    assert(dispatchCarVariableUpdates(f.arena, classes, states, error));
    assert(car->carInstance->wheelExtras[0].tire.frictionScale == 0.75f);
    assert(std::get<int32_t>(receiver.fields[0].value) == 0);
    receiver.fields[1].value = int32_t{0};
    // A suspended update is consumed, not postponed until suspension ends.
    assert(dispatchCarVariableUpdates(f.arena, classes, states, error));
    assert(std::get<int32_t>(receiver.fields[0].value) == 0);
    for (int i = 0; i < 2; ++i)
      assert(std::holds_alternative<std::monostate>(
          gameRefQueueEvent({static_cast<const VmObject*>(nullptr), int32_t{0x10}, &command},
                            reference, &f.arena, &classes)));
    const bool refreshed = dispatchCarVariableUpdates(f.arena, classes, states, error);
    if (!refreshed)
      std::fprintf(stderr, "car variable refresh: %s\n", error.c_str());
    assert(refreshed);
    assert(car->carInstance->wheelExtras[0].tire.frictionScale == 0.0f);
    assert(std::get<int32_t>(receiver.fields[0].value) == 99);
    auto& runtime = *car->carInstance;
    const auto& prototype = f.prototype(car)->config;
    const float originalInertia = prototype.enginePower[1];
    const auto& effective = runtime.effectiveConfig(prototype);
    runtime.engineRunning = true;
    runtime.damageEnabled = false;
    runtime.throttleState = 1;
    runtime.currentGear = 0;
    assert(runtime.advanceDrivetrain(effective, runtime.drivetrainContext(effective, .01f), error));
    assert(runtime.engineAngularSpeed == 6.0f);
    runtime.currentGear = 1;
    assert(runtime.advanceDrivetrain(effective, runtime.drivetrainContext(effective, .01f), error));
    assert(runtime.currentGearRatio == 3.5f);
    assert(prototype.enginePower[1] == originalInertia);
    // The same prototype remains available to another unmodified Car.
    CarRuntimeInstance untouched;
    assert(&untouched.effectiveConfig(prototype) == &prototype);

    // A real engine-block resource enters slot401. The native registry must
    // bind its calculated Dyno so a subsequent refresh produces real torque.
    auto* engine = f.add(f.id("parts\\engines\\Baiern_Emer.rpk", 0x52),
                         "java.game.parts.engines.Baiern_Emer.Baiern_Devils_6SFi_3_6_block",
                         "parts\\engines\\Baiern_Emer\\scripts\\Baiern_Devils_6SFi_3_6_block.cfg", "part");
    VmObject engineScript;
    auto ownedDyno = std::make_unique<VmObject>();
    auto& dyno = *ownedDyno;
    dyno.fields = {{"ptr", int32_t{0}, "I"}};
    f.arena.objects.push_back(std::move(ownedDyno));
    assert(std::holds_alternative<std::monostate>(dynoDataNewNative({}, &dyno, &f.arena, nullptr)));
    auto* nativeDyno = nativeResourceState(&dyno, &f.arena);
    assert(nativeDyno && nativeDyno->dyno);
    const auto calculated = nativeDyno->dyno->calculate(DynoParameters{}, 16);
    assert(std::holds_alternative<DynoResult>(calculated));
    engineScript.fields = {{"dynodata", static_cast<const VmObject*>(&dyno), "Ljava.game.parts.DynoData;"}};
    engine->scriptReceiver = &engineScript;
    // Installation here exercises the native ownership path without an active
    // VM script frame; the real callback ordering is separately source-bound.
    car->scriptReceiver = nullptr;
    const bool installedEngine = carPartInstall(*engine, car, 401, f.arena, error);
    car->scriptReceiver = &receiver;
    if (!installedEngine)
      std::fprintf(stderr, "engine install: %s\n", error.c_str());
    assert(installedEngine);
    f.arena.callScript = [&](const VmObject* target, const std::string& name, std::vector<VmValue> arguments) {
      return vmHostCall(f.arena, target, name, arguments, &nativeRegistry(), &classes, &states);
    };
    const auto force = chassisForceUpdate({}, &receiver, &f.arena, nullptr);
    if (const auto* failure = std::get_if<std::string>(&force))
      std::fprintf(stderr, "installed engine refresh: %s\n", failure->c_str());
    assert(std::holds_alternative<std::monostate>(force));
    runtime.currentGear = 0;
    runtime.engineRunning = true;
    runtime.engineAngularSpeed = 3000.0f / 9.549296379089355f;
    runtime.previousEngineRpm = 3000;
    runtime.throttleState = 1;
    const float beforeEngineSpeed = runtime.engineAngularSpeed;
    assert(runtime.advanceDrivetrain(runtime.effectiveConfig(prototype),
                                    runtime.drivetrainContext(runtime.effectiveConfig(prototype), .01f), error));
    assert(runtime.driveTorque > 100 && runtime.engineAngularSpeed > beforeEngineSpeed);
    engine->scriptReceiver = nullptr;
    assert(std::holds_alternative<std::monostate>(dynoDataDeleteNative({}, &dyno, &f.arena, nullptr)));

    receiver.fields[0].value = int32_t{0};
    assert(dispatchCarVariableUpdates(f.arena, classes, states, error));
    assert(std::get<int32_t>(receiver.fields[0].value) == 0);
    car->scriptReceiver = nullptr;
  }
  {
    // Part.save uses GameType.GII_TYPE (6), including for a Car chassis.
    // Stock getInfo vtable +0x3c returns the prototype resource identity;
    // returning an instance id or zero makes Part.createFromFile lose it.
    InstallFixture f;
    const auto carId = f.id("cars\\racers\\Baiern.rpk", 0x157);
    const auto doorId = f.id("cars\\racers\\Baiern.rpk", 0x115);
    auto* car = f.add(carId, kCarClass, kCarConfig, "car");
    auto* door = f.add(doorId, kDoorClass, kDoorConfig, "part");
    const auto checkType = [&](WorldNode& node, uint32_t expected) {
      const auto result = gameRefGetInfoIntInt(
          {int32_t{6}, int32_t{0}}, f.reference(node), &f.arena, nullptr);
      const auto* value = std::get_if<VmValue>(&result);
      assert(value && std::get<int32_t>(*value) == static_cast<int32_t>(expected));
    };
    checkType(*car, carId);
    checkType(*door, doorId);
  }
  {
    // Recursive Part.load assembles the exhaust before mounting it on a Car.
    // Both these shipped parts have click geometry but no body directive.
    InstallFixture f;
    auto* pipe = f.add(f.id("cars\\racers\\Duhen.rpk", 0x13b),
                       "java.game.cars.Duhen_stock_exhaust_pipe",
                       "cars\\racers\\Duhen_data\\scripts\\stock_exhaust_pipe.cfg", "part");
    auto* muffler = f.add(f.id("parts\\mufflers.rpk", 0x1b),
                          "java.game.parts.mufflers.muffler_08",
                          "parts\\mufflers\\scripts\\muffler_08.cfg", "part");
    std::string error;
    // Aim through a shipped click face. The same pipe remains selectable
    // after assembling its muffler, before the exhaust reaches a chassis.
    const auto* click = pipe->carInstance->partPhysics->root.get();
    assert(click && click->proto && !click->proto->shape.triangles.empty());
    const auto& shape = click->proto->shape;
    const auto& face = shape.triangles.front();
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0,
                                         0, 0, 1, 0, 0, 0, 0, 1};
    auto camera = identity;
    physics::Vec3 center{};
    for (size_t axis = 0; axis < 3; ++axis) {
      for (const auto index : face.indices)
        center[axis] += shape.vertices.at(index)[axis] / 3.0f;
    }
    const auto& pose = click->state[click->current];
    const auto worldCenter = physics::transformPoint(pose.pos, pose.rot, center);
    const auto normal = physics::matrixTransformRows(pose.rot, face.normal);
    for (size_t axis = 0; axis < 3; ++axis) {
      camera[8 + axis] = -20.0f * normal[axis];
      camera[12 + axis] = 10.0f * (worldCenter[axis] + normal[axis]);
    }
    ViewportInstance viewport{};
    viewport.width = viewport.height = 1;
    const auto before = pickCameraShapes({0, 0}, viewport, camera, identity,
                                         {{nullptr, click}}, error);
    assert(error.empty() && before);
    const bool installed = carPartInstall(*muffler, pipe, 2, f.arena, error);
    if (!installed)
      std::fprintf(stderr, "detached exhaust installation: %s (root shape=%u flags=0x%x)\n",
                   error.c_str(), static_cast<unsigned>(pipe->carInstance->partPhysics->rootPrototype.shape.type),
                   pipe->carInstance->partPhysics->rootPrototype.flags);
    assert(installed && error.empty());
    assert(InstallFixture::slot(pipe, 2)->attachedNode.owner == muffler);
    assert(InstallFixture::slot(muffler, 1)->attachedNode.owner == pipe);
    assert(muffler->carInstance->partPhysics->installedParent == pipe->carInstance->partPhysics.get());
    const auto after = pickCameraShapes({0, 0}, viewport, camera, identity,
                                        {{nullptr, click}}, error);
    if (!error.empty())
      std::fprintf(stderr, "detached exhaust cursor: %s\n", error.c_str());
    assert(error.empty() && after);
    for (size_t axis = 0; axis < 3; ++axis)
      assert(std::fabs(before->worldContact[axis] - after->worldContact[axis]) < 1e-5f);
  }
  {
    // 00470ff0 propagates ownership through a preassembled subtree. A Car's
    // public repair command must reach the mirror as well as its door.
    InstallFixture f;
    auto* car = f.add(f.id("cars\\racers\\Baiern.rpk", 0x157), kCarClass, kCarConfig, "car");
    auto* door = f.add(f.id("cars\\racers\\Baiern.rpk", 0x115), kDoorClass, kDoorConfig, "part");
    auto* mirror = f.add(f.id("cars\\racers\\Baiern.rpk", 0x104), kMirrorClass, kMirrorConfig, "part");
    const auto* mirrorRef = f.reference(*mirror);
    const auto* carRef = f.reference(*car);
    TufaClass mirrorClass;
    VmObject mirrorReceiver;
    mirrorReceiver.runtimeClass = &mirrorClass;
    mirror->scriptReceiver = &mirrorReceiver;
    std::vector<std::string> callbacks;
    f.arena.callScript = [&](const VmObject* receiver, const std::string& name,
                            std::vector<VmValue>) {
      assert(receiver == &mirrorReceiver);
      callbacks.push_back(name);
      return VmResult{true, int32_t{0}, {}, false};
    };
    std::string error;
    assert(carPartInstall(*mirror, door, 24, f.arena, error) && error.empty());
    assert(callbacks == std::vector<std::string>{"install"});
    assert(carPartRemove(*mirror, 0, f.arena, error) && error.empty());
    callbacks.clear();
    assert(carPartInstall(*mirror, door, 24, f.arena, error) && error.empty());
    assert(callbacks == std::vector<std::string>{"install"});
    assert(door->carInstance->attachedParts.size() == 1);
    assert(door->carInstance->attachedParts.front()->owner == mirror);
    assert(carPartInstall(*door, car, 5, f.arena, error) && error.empty());
    assert(door->carInstance->attachedParts.empty());
    const auto owner = gameRefGetInfoIntInt({int32_t{43}, int32_t{0}}, mirrorRef, &f.arena, nullptr);
    assert(std::holds_alternative<VmValue>(owner));
    assert(std::get<int32_t>(std::get<VmValue>(owner)) == static_cast<int32_t>(car->instanceId));
    partSetWear({0.3f}, mirrorRef, &f.arena, nullptr);
    VmObject repair;
    f.arena.strings.emplace_back("repair", &repair);
    const auto repaired = gameRefQueueEvent({static_cast<const VmObject*>(nullptr), int32_t{0x10}, &repair},
                                           carRef, &f.arena, nullptr);
    assert(std::holds_alternative<std::monostate>(repaired));
    const auto wear = partGetWear({}, mirrorRef, &f.arena, nullptr);
    assert(std::get<float>(std::get<VmValue>(wear)) == 1.0f);
    mirror->scriptReceiver = nullptr;
  }
  {
    // Newer 00501d00 detaches only the mounted root, retaining its children.
    // The detached root's registry must then own the same mounted subtree.
    InstallFixture f;
    auto* car = f.add(f.id("cars\\racers\\Baiern.rpk", 0x157), kCarClass, kCarConfig, "car");
    auto* door = f.add(f.id("cars\\racers\\Baiern.rpk", 0x115), kDoorClass, kDoorConfig, "part");
    auto* mirror = f.add(f.id("cars\\racers\\Baiern.rpk", 0x104), kMirrorClass, kMirrorConfig, "part");
    const auto* mirrorRef = f.reference(*mirror);
    std::string error;
    assert(carPartInstall(*mirror, door, 24, f.arena, error));
    assert(carPartInstall(*door, car, 5, f.arena, error));
    auto& doorPhysics = *door->carInstance->partPhysics;
    auto& mirrorPhysics = *mirror->carInstance->partPhysics;
    const auto pose = mirrorPhysics.root->state[mirrorPhysics.root->current];
    const auto relative = mirrorPhysics.root->attachment->relative;
    auto* physicalParent = mirrorPhysics.root->attachment->parent;
    const auto subtreeMass = doorPhysics.rootPrototype.invMass;
    const auto subtreeInertia = doorPhysics.rootPrototype.inertia;
    vm_test::Cb referenceClass("java.util.resource.GameRef");
    referenceClass.instanceField("ptr", "I")
        .method("queueEvent", "(Ljava.util.resource.ResourceRef;ILjava.lang.String;)V", 0x40, {});
    vm_test::Cb stringClass("java.lang.String");
    vm_test::Cb observerClass("test.SubtreeOwner");
    observerClass.instanceField("calls", "I")
        .instanceField("last", "Ljava.util.resource.GameRef;");
    const std::vector<TufaNode> observe{
        vm_test::node(0x01, true, 0),
        vm_test::node(0x1c, true, observerClass.t("calls")),
        vm_test::node(0x08, true, 23),
        vm_test::node(0x01, true, 1), vm_test::node(0x01, true, 0),
        vm_test::node(0x1c, true, observerClass.t("last")),
        vm_test::node(0x08, true, 35), vm_test::retVoid};
    observerClass.method("rempart", "(Ljava.util.resource.GameRef;)V", 1, observe)
        .method("addpart", "(Ljava.util.resource.GameRef;)V", 1, observe)
        .method("remove", "()V", 1, {vm_test::retVoid});
    auto observer = [&]() {
      auto object = std::make_unique<VmObject>();
      object->runtimeClass = &observerClass.c;
      object->fields = {{"calls", int32_t{0}, "I"},
                        {"last", static_cast<const VmObject*>(nullptr), "Ljava.util.resource.GameRef;"}};
      auto* result = object.get();
      f.arena.objects.push_back(std::move(object));
      return result;
    };
    car->scriptReceiver = observer();
    door->scriptReceiver = observer();
    const auto* doorRef = f.reference(*door);
    const_cast<VmObject*>(doorRef)->runtimeClass = &referenceClass.c;
    auto text = std::make_unique<VmObject>();
    text->runtimeClass = &stringClass.c;
    const auto* command = text.get();
    f.arena.objects.push_back(std::move(text));
    f.arena.strings.emplace_back("remove 0", command);
    vm_test::Cb caller("test.RemoveSubtree");
    const auto method = caller.ref(referenceClass.c.name, "queueEvent",
                                   "(Ljava.util.resource.ResourceRef;ILjava.lang.String;)V");
    const std::vector<TufaNode> remove{
        vm_test::node(0x01, true, 2), vm_test::literal(0x10),
        vm_test::node(0x01, true, 1), vm_test::literal(3), vm_test::node(0x24),
        vm_test::node(0x11, true, 2), vm_test::node(0x01, true, 0),
        vm_test::node(0x1a, true, method), vm_test::retVoid};
    const std::vector<const TufaClass*> classes{&referenceClass.c, &stringClass.c, &observerClass.c};
    const std::vector<VmStaticFields> states{{&referenceClass.c, {}},
                                             {&stringClass.c, {}},
                                             {&observerClass.c, {}}};
    const size_t firstCallback = f.arena.resourceStates.size();
    const auto removed = vmExecute(caller.c, remove,
                                   {doorRef, command, static_cast<const VmObject*>(nullptr)},
                                   &nativeRegistry(), &classes, &states, &f.arena);
    if (!removed.ok)
      std::fprintf(stderr, "subtree remove callback: %s\n", removed.error.c_str());
    assert(removed.ok);
    assert(f.arena.resourceStates.size() == firstCallback + 3);
    assert(f.arena.resourceStates[firstCallback].words[2] == door->instanceId);
    assert(f.arena.resourceStates[firstCallback + 1].words[2] == mirror->instanceId);
    assert(f.arena.resourceStates[firstCallback + 2].words[2] == mirror->instanceId);
    assert(std::get<int32_t>(car->scriptReceiver->fields[0].value) == 2);
    assert(std::get<int32_t>(door->scriptReceiver->fields[0].value) == 1);
    const auto* last = std::get<const VmObject*>(door->scriptReceiver->fields[1].value);
    assert(nativeResourceState(last, &f.arena)->words[2] == mirror->instanceId);
    car->scriptReceiver = nullptr;
    door->scriptReceiver = nullptr;
    assert(!doorPhysics.installedParent && !doorPhysics.root->attachment);
    assert(doorPhysics.installedChildren.size() == 1);
    assert(mirrorPhysics.installedParent == &doorPhysics);
    assert(mirrorPhysics.root->attachment->parent == physicalParent);
    assert(mirrorPhysics.root->attachment->relative.position == relative.position);
    assert(mirrorPhysics.root->attachment->relative.rotation() == relative.rotation());
    assert(mirrorPhysics.root->state[mirrorPhysics.root->current].pos == pose.pos);
    assert(mirrorPhysics.root->state[mirrorPhysics.root->current].rot == pose.rot);
    assert(!doorPhysics.indexedCar && !mirrorPhysics.indexedCar);
    assert(doorPhysics.rootPrototype.invMass == subtreeMass);
    assert(doorPhysics.rootPrototype.inertia == subtreeInertia);
    assert(InstallFixture::slot(door, 24)->attachedNode.owner == mirror);
    assert(car->carInstance->attachedParts.empty());
    assert(door->carInstance->attachedParts.size() == 1);
    assert(door->carInstance->attachedParts.front()->owner == mirror);
    assert(door->carInstance->carLink.owner == door);
    assert(mirror->carInstance->carLink.owner == door);
    assert(door->carInstance->wheelId == 0 && mirror->carInstance->wheelId == 0);
    assert(carPartInstall(*door, car, 5, f.arena, error) && error.empty());
    assert(door->carInstance->attachedParts.empty());
    assert(car->carInstance->attachedParts.size() == 2);
    assert(mirror->carInstance->carLink.owner == car);
    assert(mirrorPhysics.installedParent == &doorPhysics);
    assert(mirrorPhysics.indexedCar == car->carInstance->carPhysics.get());
    partSetWear({0.25f}, mirrorRef, &f.arena, nullptr);
    VmObject repair;
    f.arena.strings.emplace_back("repair", &repair);
    assert(std::holds_alternative<std::monostate>(gameRefQueueEvent(
        {static_cast<const VmObject*>(nullptr), int32_t{0x10}, &repair},
        f.reference(*car), &f.arena, nullptr)));
    assert(std::get<float>(std::get<VmValue>(partGetWear({}, mirrorRef, &f.arena, nullptr))) == 1.0f);
  }
  for (const bool invalidIndex : {false, true}) {
    // A failed physical detach must reach the public caller, rather than
    // succeeding after discarding the compound index or finalization error.
    InstallFixture f;
    auto* car = f.add(f.id("cars\\racers\\Baiern.rpk", 0x157), kCarClass, kCarConfig, "car");
    auto* door = f.add(f.id("cars\\racers\\Baiern.rpk", 0x115), kDoorClass, kDoorConfig, "part");
    std::string error;
    assert(carPartInstall(*door, car, 5, f.arena, error));
    auto& carPhysics = *car->carInstance->carPhysics;
    auto* parent = door->carInstance->partPhysics->root->attachment->parent;
    auto& prototype = const_cast<physics::BodyProto&>(*parent->proto);
    const auto shape = prototype.shape.type;
    std::unique_ptr<CarWheelRuntime> wheel;
    if (invalidIndex) {
      assert(!carPhysics.wheels.empty());
      wheel = std::move(carPhysics.wheels.back());
    } else {
      prototype.shape.type = physics::ShapeType::Empty;
    }
    assert(!carPartRemove(*door, 0, f.arena, error));
    assert(error.find(invalidIndex ? "missing wheel runtime" : "unsupported compound root") != std::string::npos);
    if (invalidIndex)
      carPhysics.wheels.back() = std::move(wheel);
    else
      prototype.shape.type = shape;
    assert(indexCarBodies(carPhysics, error));
  }
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
    // Select the changed analog mapping before requiring its missing device
    // class to refuse; an unmoved mapping must not steal a digital axis.
    key = 1;
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
  // Public GameType constants are literal selectors, not the command parser's
  // 0xe8-based operation numbers (native getInfo vtable +0x3c).
  {
    InstallFixture f;
    auto* car = f.add(f.id("cars\\racers\\Baiern.rpk", 0x157), kCarClass, kCarConfig, "car");
    auto* part = f.add(f.id("cars\\racers\\Baiern.rpk", 0x10d), kHoodClass, kHoodConfig, "part");
    const auto* carReference = f.reference(*car);
    const auto* partReference = f.reference(*part);
    const auto query = [&](const VmObject* reference, int32_t selector) {
      const auto result = gameRefGetInfoIntInt({selector, int32_t{0}}, reference, &f.arena, nullptr);
      const auto* value = std::get_if<VmValue>(&result);
      assert(value && std::holds_alternative<int32_t>(*value));
      return std::get<int32_t>(*value);
    };
    assert(query(carReference, 7) == 5);
    assert(query(partReference, 7) == 9);
    // Both shipped configs explicitly declare category2, distinct from the
    // native object's broad category5/9 above.
    assert(query(carReference, 0x37) == 2);
    assert(query(partReference, 0x37) == 2);
    for (const int32_t selector : {8, 9, 0x1c}) {
      assert(query(carReference, selector) == 1);
      assert(query(partReference, selector) == 0);
    }
    std::string error;
    assert(carPartInstall(*part, car, 0, f.arena, error));
    assert(query(partReference, 0x2b) == static_cast<int32_t>(car->instanceId));
    assert(query(carReference, 0x2b) == static_cast<int32_t>(car->instanceId));
    assert(carPartRemove(*part, 0, f.arena, error));
    assert(query(partReference, 0x2b) == static_cast<int32_t>(part->instanceId));
    for (const auto [reference, selector] : {
             std::pair{carReference, int32_t{0x34}},
             std::pair{carReference, int32_t{0x4c}},
             std::pair{carReference, int32_t{0x4d}},
             std::pair{partReference, int32_t{0x47}},
             std::pair{partReference, int32_t{0x48}},
             std::pair{partReference, int32_t{0x4d}}}) {
      const auto unsupported = gameRefGetInfoIntInt({selector, int32_t{0}}, reference, &f.arena, nullptr);
      assert(std::holds_alternative<std::string>(unsupported));
    }

    VmObject receiver;
    car->scriptReceiver = part->scriptReceiver = &receiver;
    int callbacks = 0;
    f.arena.callScript = [&](const VmObject*, const std::string& name, std::vector<VmValue>) {
      assert(name == "getInfo");
      ++callbacks;
      return VmResult{true, int32_t{999}, {}, false};
    };
    for (const auto* reference : {carReference, partReference}) {
      assert(query(reference, 0xdead) == 0);
      assert(query(reference, 0xf1) == 0);
    }
    assert(callbacks == 0);
    auto* scriptOnly = f.world.addTypedChild(*f.world.defaultParent(), 1, "script-query", 0);
    assert(scriptOnly);
    scriptOnly->scriptReceiver = &receiver;
    assert(query(f.reference(*scriptOnly), 0xdead) == 999 && callbacks == 1);
    car->scriptReceiver = part->scriptReceiver = scriptOnly->scriptReceiver = nullptr;
  }

  // Public GameRef placement targets the Part's selected main body, even
  // when its centre differs from the compound's centre (00470480/004c6370).
  {
    InstallFixture f;
    CarConfig config;
    config.rootBody = 0;
    config.bodies.resize(2);
    std::vector<CarGeometryPrototype> geometry(2);
    std::string error;
    for (size_t i = 0; i < geometry.size(); ++i) {
      geometry[i].bodyIndex = i;
      geometry[i].transform.position = {i ? -1.0f : 1.0f, 0, 0};
      auto box = physics::createBodyProto(1, 5, 0.2f, 0.2f, 0.2f, nullptr, error);
      assert(box);
      *geometry[i].body = *box;
    }
    PartSlotDirective geometryDirective{};
    geometryDirective.orientation[2] = 1.5707963f;
    PartSlotState geometryPose;
    assert(makePartSlotState(geometryDirective, geometryPose));
    geometry[0].transform.rotation = geometryPose.rotation;
    auto* node = f.world.addTypedChild(*f.world.defaultParent(), 1, "pose_part", 0);
    assert(node);
    node->nativeDevice = "part";
    node->carInstance = std::make_unique<CarRuntimeInstance>();
    auto& instance = *node->carInstance;
    instance.partPhysics = buildPartPhysics(config, geometry, "0,0,0,0,0,0,0", error);
    assert(instance.partPhysics);
    auto* rootResource = f.world.registerPartPhysics(*node, instance.partPhysics, error);
    assert(rootResource);
    setLinkOwner(instance.physicsRoot, rootResource);
    auto& root = *instance.partPhysics->root;
    auto* mainBody = instance.partPhysics->rootGeometryBody();
    assert(mainBody && mainBody->attachment && mainBody->attachment->relative.position[0] == 1);
    root.state[root.current].vel = {3, 4, 5};
    root.state[root.current].omega = {1, 2, 3};
    const auto* reference = f.reference(*node);
    VmObject position;
    position.fields = {{"x", 7.0f, "F"}, {"y", 8.0f, "F"}, {"z", 9.0f, "F"}};
    VmObject orientation;
    orientation.fields = {{"y", 1.5707963f, "F"}, {"p", 0.0f, "F"}, {"r", 0.0f, "F"}};
    const auto placed = gameRefSetMatrix({&position, &orientation}, reference, &f.arena, nullptr);
    assert(std::holds_alternative<std::monostate>(placed));
    const physics::Vec3 requested{7, 8, 9};
    for (size_t axis = 0; axis < 3; ++axis)
      assert(std::fabs(mainBody->state[mainBody->current].pos[axis] - requested[axis]) < 0.0001f);
    // Newer 004e9730 multiplies the requested +90Y by forward +90Z;
    // 005030b0 then publishes the child's +90Z again. This observed static
    // rule yields Y90*Z180, rather than the intuitive requested orientation.
    const std::array<float, 9> expectedRotation{0, 0, 1, 0, -1, 0, 1, 0, 0};
    for (size_t row = 0; row < 3; ++row)
      for (size_t column = 0; column < 3; ++column)
        assert(std::fabs(mainBody->state[mainBody->current].rot[row * 4 + column] -
                         expectedRotation[row * 3 + column]) < 0.0001f);
    assert(root.state[root.current].pos != requested);
    assert(root.flags & physics::kBodyAsleep);
    assert(root.state[root.current].vel == physics::Vec3{});
    assert(root.state[root.current].omega == physics::Vec3{});
    for (const auto& child : instance.partPhysics->children)
      assert(child.body->state[0].pos == child.body->state[1].pos);

    // A malformed attachment must refuse before moving bodies or the cache.
    const auto beforeRoot = root.state[root.current];
    const auto beforeCache = instance.position;
    const auto originalAttachment = mainBody->attachment;
    physics::Body outside;
    mainBody->attachment->parent = &outside;
    position.fields[0].value = 100.0f;
    const auto refused = gameRefSetMatrix({&position, &orientation}, reference, &f.arena, nullptr);
    assert(std::holds_alternative<std::string>(refused));
    assert(root.state[root.current].pos == beforeRoot.pos && instance.position == beforeCache);
    mainBody->attachment = originalAttachment;

    // Already-sleeping roots skip recursive sleep (00506d90). Moving one
    // must publish child poses without replacing unrelated child state.
    mainBody->state[mainBody->current].vel = {9, 8, 7};
    position.fields = {{"x", 10.0f, "F"}, {"y", 11.0f, "F"}, {"z", 12.0f, "F"}};
    assert(std::holds_alternative<std::monostate>(gameRefSetPos({&position}, reference, &f.arena, nullptr)));
    const physics::Vec3 second{10, 11, 12};
    for (size_t axis = 0; axis < 3; ++axis)
      assert(std::fabs(mainBody->state[mainBody->current].pos[axis] - second[axis]) < 0.0001f);
    assert((mainBody->state[mainBody->current].vel == physics::Vec3{9, 8, 7}));

    // Noncommuting initial parent X90 and child Y90 distinguish quaternion
    // operand order independently of the single-axis doubling example.
    const physics::Mat34 parentX90{1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0};
    const physics::Mat34 childY90{0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0};
    root.state[root.current].rot = parentX90;
    root.state[root.current].quat = physics::quatFromMatrix(parentX90);
    mainBody->state[mainBody->current].rot = childY90;
    mainBody->state[mainBody->current].quat = physics::quatFromMatrix(childY90);
    assert(physics::attachBody(*mainBody, root));
    orientation.fields = {{"y", 0.0f, "F"}, {"p", 0.0f, "F"}, {"r", 0.0f, "F"}};
    assert(std::holds_alternative<std::monostate>(
        gameRefSetMatrix({&position, &orientation}, reference, &f.arena, nullptr)));
    const std::array<float, 9> expectedRoot{0, 0, 1, -1, 0, 0, 0, -1, 0};
    const std::array<float, 9> expectedChild{0, -1, 0, 0, 0, -1, 1, 0, 0};
    for (size_t row = 0; row < 3; ++row)
      for (size_t column = 0; column < 3; ++column) {
        assert(std::fabs(root.state[root.current].rot[row * 4 + column] -
                         expectedRoot[row * 3 + column]) < 0.0001f);
        assert(std::fabs(mainBody->state[mainBody->current].rot[row * 4 + column] -
                         expectedChild[row * 3 + column]) < 0.0001f);
      }
  }

  // An installed Part's pose request moves its actual Car root through the
  // full relative chain; a second registered Car must remain untouched.
  {
    InstallFixture f;
    auto* car = f.add(f.id("cars\\racers\\Baiern.rpk", 0x157), kCarClass, kCarConfig, "car");
    auto* other = f.add(f.id("cars\\racers\\Baiern.rpk", 0x157), kCarClass, kCarConfig, "car");
    auto* hood = f.add(f.id("cars\\racers\\Baiern.rpk", 0x10d), kHoodClass, kHoodConfig, "part");
    std::string error;
    assert(carPartInstall(*hood, car, 0, f.arena, error));
    auto& carRoot = *car->carInstance->carPhysics->root;
    const auto previous = carRoot.state[carRoot.current].pos;
    const auto otherBefore = other->carInstance->carPhysics->root->state[0].pos;
    const auto* reference = f.reference(*hood);
    VmObject position;
    position.fields = {{"x", 5.0f, "F"}, {"y", 6.0f, "F"}, {"z", 7.0f, "F"}};
    VmObject orientation;
    orientation.fields = {{"y", 0.7f, "F"}, {"p", 0.2f, "F"}, {"r", -0.1f, "F"}};
    assert(std::holds_alternative<std::monostate>(gameRefSetMatrix({&position, &orientation}, reference, &f.arena, nullptr)));
    const auto* mainBody = hood->carInstance->partPhysics->rootGeometryBody();
    assert(mainBody);
    const physics::Vec3 requested{5, 6, 7};
    for (size_t axis = 0; axis < 3; ++axis)
      assert(std::fabs(mainBody->state[mainBody->current].pos[axis] - requested[axis]) < 0.0002f);
    assert(carRoot.state[carRoot.current].pos != previous);
    assert(other->carInstance->carPhysics->root->state[0].pos == otherBefore);
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
  {
    // A script setter must change the force produced by the real car's
    // suspension, not merely retain an extra field that no solver reads.
    auto* chassis = world.addTypedChild(*created.node, 1, "chassis", 0);
    assert(chassis);
    chassis->carInstance = std::make_unique<CarRuntimeInstance>();
    setLinkOwner(chassis->carInstance->carLink, created.node);
    auto wheelObject = std::make_unique<VmObject>();
    wheelObject->fields.push_back({"ptr", int32_t{2}, "I"});
    auto* wheelRef = wheelObject.get();
    arena.objects.push_back(std::move(wheelObject));
    arena.resourceStates.push_back({wheelRef, {chassis->instanceId, 0, 0, 0}});
    auto& live = *created.node->carInstance->carPhysics->wheels[0];
    const auto original = live.suspension;
    // FUN_00491100 applies force only with an awake dynamic side. This fixture
    // measures active suspension forces directly against a static ground.
    wakeCar(*created.node->carInstance->carPhysics);
    const float rest = live.suspension.settings.restLength;
    auto groundProto = std::make_shared<physics::BodyProto>(
        *physics::createBodyProto(0, 5, 1, 1, 1, nullptr, error));
    auto ground = std::make_shared<physics::Body>(physics::createBody(*groundProto, {0, 0, 0}));
    const auto forceAtHalfMetre = [&] {
      auto& u = live.suspension;
      u.contactBody = ground.get();
      u.contactOwner = ground;
      u.contactPrototype = groundProto;
      u.normal = {0, 1, 0};
      u.contactPoint = {0, 0, 0};
      u.distanceSquared = 0.25f;
      u.length = 0.5f;
      u.accumulatedForce = 0;
      assert(physics::updateSuspension(u) == physics::SuspensionUpdateResult::Active);
      return u.force;
    };
    assert(std::holds_alternative<std::monostate>(wheelRefSetForce({10000.0f}, wheelRef, &arena, nullptr)));
    assert(std::fabs(forceAtHalfMetre() - (rest - 0.5f) * 10000.0f) < 0.1f);
    assert(std::holds_alternative<std::monostate>(wheelRefSetForce({20000.0f}, wheelRef, &arena, nullptr)));
    assert(std::fabs(forceAtHalfMetre() - (rest - 0.5f) * 20000.0f) < 0.1f);
    assert(std::holds_alternative<std::monostate>(wheelRefSetRestLen({0.25f}, wheelRef, &arena, nullptr)));
    assert(std::fabs(forceAtHalfMetre() - (0.25f + live.radius - 0.5f) * 20000.0f) < 0.1f);
    assert(std::holds_alternative<std::monostate>(wheelRefSetRadius({0.4f}, wheelRef, &arena, nullptr)));
    assert(std::fabs(forceAtHalfMetre() - (0.65f - 0.5f) * 20000.0f) < 0.1f);
    const float springAtHalfMetre = (0.65f - 0.5f) * 20000.0f;
    auto& rootState = created.node->carInstance->carPhysics->root->state[created.node->carInstance->carPhysics->root->current];
    const auto originalVelocity = rootState.vel;
    assert(std::holds_alternative<std::monostate>(wheelRefSetDamping2({100.0f, 200.0f}, wheelRef, &arena, nullptr)));
    rootState.vel = {0, 0.1f, 0};
    assert(std::fabs(forceAtHalfMetre() - (springAtHalfMetre - 40.0f)) < 0.1f);
    rootState.vel = {0, -0.1f, 0};
    assert(std::fabs(forceAtHalfMetre() - (springAtHalfMetre + 20.0f)) < 0.1f);
    assert(std::holds_alternative<std::monostate>(wheelRefSetDamping1({100.0f}, wheelRef, &arena, nullptr)));
    rootState.vel = {0, 0.1f, 0};
    assert(std::fabs(forceAtHalfMetre() - (springAtHalfMetre - 14.0f)) < 0.1f);
    rootState.vel = originalVelocity;
    live.suspension = original;
  }
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
    // FUN_0044d330 expires timers after the automatic controller, so this
    // first call clears the stale manual-shift timer before auto can run.
    assert(car.advanceControls(input, config, step, error));
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
    // FUN_0044dc30 does not gate forward shifts on throttle demand, the
    // automatic launch speed threshold, or an expired clutch timer.
    car.currentGear = 1;
    car.currentGearRatio = config.gearRatios[0];
    car.shiftState = {};
    controls.continuous[1] = 0;
    step.time = 42;
    assert(car.advanceControls(controls, config, step, error));
    assert(car.currentGear == 2);
    // Rolling backwards in a forward gear selects first in semiautomatic;
    // automatic selects neutral, while manual retains its selected gear.
    car.currentGear = 3;
    car.currentGearRatio = config.gearRatios[2];
    car.shiftState = {};
    step.drivenWheelSpeed = -0.5f;
    assert(car.advanceControls(controls, config, step, error));
    assert(car.currentGear == 1);
    car.currentGear = 3;
    step.semiautomatic = false;
    assert(car.advanceControls(controls, config, step, error));
    assert(car.currentGear == 3);
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
  {
    // FUN_0044dd70: automatic clutch protects idle, respects the pedal,
    // and releases through its own history before the outer pedal smoothing.
    CarRuntimeInstance car;
    CarConfig config;
    CarControlSnapshot input;
    CarControlStepContext step;
    step.time = 10;
    step.step = 0.01f;
    step.engineRunning = step.automaticClutch = true;
    step.displayedEngineSpeed = config.rpmIdle * 0.10471975803375244f;
    car.currentGear = 1;
    car.currentGearRatio = config.gearRatios[0];
    car.previousClutchEngineSpeed = step.displayedEngineSpeed;
    std::string error;
    assert(car.advanceControls(input, config, step, error));
    assert(car.previousAutomaticClutch == 1 && car.clutchState > 0.33f);
    const float disconnected = car.clutchState;
    // At sufficient engine speed it engages gradually, rather than opening
    // on every frame or instantaneously erasing the clutch history.
    step.displayedEngineSpeed = 600;
    assert(car.advanceControls(input, config, step, error));
    assert(car.previousAutomaticClutch > 0.66f && car.previousAutomaticClutch < 0.67f);
    assert(car.clutchState > disconnected);
    for (int i = 0; i < 100; ++i)
      assert(car.advanceControls(input, config, step, error));
    assert(car.clutchState < 0.001f);
    input.digital[0] = 127;
    assert(car.advanceControls(input, config, step, error));
    assert(car.previousAutomaticClutch == 1 && car.clutchState > 0.33f);
    // Manual clutch disables the automatic intervention, including at idle.
    step.automaticClutch = false;
    input.digital[0] = 0;
    step.displayedEngineSpeed = 0;
    for (int i = 0; i < 100; ++i)
      assert(car.advanceControls(input, config, step, error));
    assert(car.clutchState < 0.001f);
    // FUN_0044dd70 opens the clutch when the driven speed would overrun
    // the lesser of the engine and limiter bounds, even with no pedal.
    step.automaticClutch = true;
    step.displayedEngineSpeed = 600;
    step.drivenWheelSpeed = 100;
    assert(car.advanceControls(input, config, step, error));
    assert(car.previousAutomaticClutch == 1 && car.clutchState > 0.33f);
    CarConfig invalid = config;
    invalid.gearRatios[0] = 0;
    const float beforeFailure = car.clutchState;
    assert(!car.advanceControls(input, invalid, step, error));
    assert(car.clutchState == beforeFailure && car.currentGear == 1);
    // Neutral (cached zero ratio) and stopped engines leave history alone.
    step.automaticClutch = true;
    car.currentGear = 0;
    car.currentGearRatio = 0;
    const float previous = car.previousClutchEngineSpeed;
    assert(car.advanceControls(input, config, step, error));
    assert(car.previousClutchEngineSpeed == previous);
    car.currentGear = 1;
    car.currentGearRatio = config.gearRatios[0];
    step.engineRunning = false;
    assert(car.advanceControls(input, config, step, error));
    assert(car.previousClutchEngineSpeed == previous);
  }
  {
    // FUN_0044e690 uses one latch for both shift directions. A held button
    // at a gear boundary must still be released before another shift.
    CarRuntimeInstance car;
    CarConfig config;
    CarControlSnapshot input;
    CarControlStepContext step;
    step.step = 0.1f;
    std::string error;
    const auto shift = [&](int request) {
      step.time += 1;
      input.digital[2] = request;
      assert(car.advanceControls(input, config, step, error));
    };
    car.currentGear = config.gearCount;
    shift(127);
    assert(car.currentGear == config.gearCount);
    shift(-127);
    assert(car.currentGear == config.gearCount);
    for (int gear = config.gearCount - 1; gear >= 0; --gear) {
      shift(0);
      shift(-127);
      assert(car.currentGear == gear);
      shift(-127);
      assert(car.currentGear == gear);
    }
    shift(0);
    shift(-127);
    assert(car.currentGear == 7);
    shift(0);
    shift(-127);
    assert(car.currentGear == 7);
    shift(0);
    shift(127);
    assert(car.currentGear == 0);
    shift(0);
    shift(127);
    assert(car.currentGear == 1);
  }
  {
    // Public named and indexed setters address one physical tyre block.
    GameWorld world;
    auto* root = world.addRoot();
    auto* node = world.addTypedChild(*root, 1, "wheel setter car", 0);
    assert(node);
    node->carInstance = std::make_unique<CarRuntimeInstance>();
    auto& extra = node->carInstance->wheelExtras[0];
    VmGameState game;
    game.world = &world;
    VmArena arena;
    arena.gameState = &game;
    auto ownedWheelReference = std::make_unique<VmObject>();
    auto& wheelReference = *ownedWheelReference;
    wheelReference.fields.push_back({"ptr", int32_t{1}, "I"});
    arena.objects.push_back(std::move(ownedWheelReference));
    arena.resourceStates.push_back({&wheelReference, {node->instanceId, 0, 0, 0}});
    assert(extra.tire.pacejka.size() == 17);
    for (int32_t index = 0; index < 17; ++index)
      assert(std::holds_alternative<std::monostate>(wheelRefSetPacejka(
          {index, float(index + 1)}, &wheelReference, &arena, nullptr)));
    assert(extra.tire.pacejka[16] == 17);
    assert(std::holds_alternative<std::monostate>(wheelRefSetStiffness(
        {3.0f}, &wheelReference, &arena, nullptr)));
    assert(extra.tire.pacejka[4] == 3);
    wheelRefSetPacejka({int32_t{4}, 7.0f}, &wheelReference, &arena, nullptr);
    assert(extra.tire.pacejka[4] == 7);
    // Indexed coefficient16 changes the loaded tyre force calculation. The
    // actual public setters feed the same block used by the world tyre step.
    for (int32_t index = 0; index < 17; ++index)
      wheelRefSetPacejka({index, 0.0f}, &wheelReference, &arena, nullptr);
    for (const int32_t index : {0, 2, 4, 12})
      wheelRefSetPacejka({index, 1.0f}, &wheelReference, &arena, nullptr);
    const auto force = [&]() {
      TireSlipForceInput input{};
      std::copy_n(extra.tire.pacejka.begin(), 11, input.curve.begin());
      input.slip = {0, 1, 0, .01f, 0, 0};
      input.slipParameters = {1, 1, .3f, .5f, 1, 0, 0, 0};
      input.effectiveLoad = 2;
      input.surfaceFriction = input.wheelFriction = 1;
      input.longitudinalScale = extra.tire.pacejka[12];
      input.radius = input.inverseWheelInertia = 1;
      input.angularSpeed = -2;
      input.stiffnessNumerator = extra.tire.pacejka[15];
      input.stiffnessDenominator = extra.tire.pacejka[16];
      return calculateTireSlipForce(input);
    };
    const auto unconstrained = force();
    wheelRefSetPacejka({int32_t{16}, 2.0f}, &wheelReference, &arena, nullptr);
    const auto loadScaled = force();
    assert(unconstrained && loadScaled &&
           unconstrained->longitudinalForce > loadScaled->longitudinalForce);
    wheelRefSetFriction({4.0f}, &wheelReference, &arena, nullptr);
    const float derivedFriction = extra.tire.friction6c;
    wheelRefSetPacejka({int32_t{2}, 9.0f}, &wheelReference, &arena, nullptr);
    assert(extra.tire.pacejka[2] == 9 && extra.tire.friction64 == 4 &&
           extra.tire.friction6c == derivedFriction);
    // Pre-script reset restores every prototype coefficient and clears the
    // actual friction/brake wear state, without resetting unrelated live data.
    node->carInstance->carPhysics = std::make_unique<CarPhysics>();
    node->carInstance->carPhysics->wheels.push_back(std::make_unique<CarWheelRuntime>());
    CarConfig config;
    config.wheelPars = {2, 3, 4, 5, 6, 7, 8, 9, 10};
    extra.tire.frictionScale = .5f;
    extra.frictionWearRate = .1f;
    extra.brakeThermal.wear = .6f;
    extra.brakeThermal.wearRate = .2f;
    extra.variableDamage16c = .7f;
    extra.variableDamageRate170 = .3f;
    extra.brakeThermal.temperature = 123;
    extra.drive = .8f;
    extra.radius = .45f;
    std::string resetError;
    assert(resetCarWheelVariables(*node->carInstance, config, resetError));
    assert(resetError.empty());
    assert(extra.tire.pacejka == (std::array<float, 17>{
        2, 0, 3, 0, 4, 0, -0.0f, 0, -1, 0, 0, 5, 6, 7, 8, 9, 10}));
    assert(std::signbit(extra.tire.pacejka[6]));
    assert(extra.tire.frictionScale == 0 && extra.frictionWearRate == 0 &&
           extra.brakeThermal.wear == 0 && extra.brakeThermal.wearRate == 0 &&
           extra.variableDamage16c == 0 && extra.variableDamageRate170 == 0);
    assert(extra.tire.friction64 == 4 && extra.tire.friction6c == derivedFriction);
    assert(extra.brakeThermal.temperature == 123 && extra.drive == .8f &&
           extra.radius == .45f);
    // Script setters run after the reset and must replace those defaults.
    wheelRefSetMaxLoad({42.0f}, &wheelReference, &arena, nullptr);
    assert(extra.tire.pacejka[11] == 42);
  }
  std::printf("car runtime ok\n");
}
