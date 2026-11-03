#include "game_creation.hpp"
#include "object_physics.hpp"
#include "resource_world.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

static int failures = 0;
// A node's referrer list also holds sector links (stock +0x48 is one list), so
// look for a specific link.
static bool listed(const LinkOwner &owner, const OwnerLink &link) {
  for (const OwnerLink *at = owner.head; at; at = at->next)
    if (at == &link)
      return true;
  return false;
}
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::printf("FAIL line %d: %s\n", __LINE__, #c);                         \
      ++failures;                                                              \
    }                                                                          \
  } while (0)
int main() {
  {
    GameWorld world, other;
    auto *root = world.addRoot();
    auto *first = world.addTypedChild(*root, 2, "first body", 0);
    auto *second = world.addTypedChild(*root, 2, "second body", 0);
    auto *foreign = other.addRoot();
    auto prototype = std::make_shared<physics::BodyProto>();
    auto body =
        std::make_shared<physics::Body>(physics::createBody(*prototype, {}));
    auto secondBody =
        std::make_shared<physics::Body>(physics::createBody(*prototype, {}));
    auto invalid = std::make_shared<physics::Body>();
    auto wrongPrototype = std::make_shared<physics::BodyProto>();
    CHECK(!world.registerDynamicBody(*foreign, body, prototype));
    CHECK(!world.registerDynamicBody(*root, body, prototype));
    CHECK(!world.registerDynamicBody(*first, invalid, prototype));
    CHECK(!world.registerDynamicBody(*first, body, wrongPrototype));
    CHECK(world.registerDynamicBody(*first, body, prototype));
    CHECK(!world.registerDynamicBody(*first, secondBody, prototype));
    CHECK(!world.registerDynamicBody(*second, body, prototype));
    CHECK(world.registerDynamicBody(*second, secondBody, prototype));
    std::weak_ptr<physics::Body> retained = body;
    body.reset();
    CHECK(!retained.expired());
    CHECK(world.dynamicBodyCount() == 2);
    std::string error;
    CHECK(!world.stepEmptyPhysics(0.01f, error));
    CHECK(world.physicsTime() == 0.0f);
    CHECK(world.unregisterDynamicBody(*second));
    CHECK(!world.unregisterDynamicBody(*second));
    CHECK(world.dynamicBodyCount() == 1);
    bool removedBeforeObserver = false;
    const auto observer = world.addNodeDestroyObserver([&](LinkOwner &owner) {
      if (&owner == first)
        removedBeforeObserver = world.dynamicBodyCount() == 0;
    });
    CHECK(world.destroy(*first));
    CHECK(removedBeforeObserver && world.dynamicBodyCount() == 0);
    world.removeNodeDestroyObserver(observer);
    CHECK(retained.expired());
    CHECK(world.stepEmptyPhysics(0.01f, error));
  }
  {
    GameWorld world;
    auto *root = world.addRoot();
    LinkOwner external, foreign;
    auto externalHandle = world.attachExternal(root->handle, external);
    CHECK(externalHandle);
    std::string error;
    auto physics = object::ObjectPhysics::create();
    object_config::ObjectSlot box;
    box.kind = object_config::SlotKind::Body;
    box.shape.kind = object_config::ShapeKind::Box;
    box.shape.mass = 1;
    box.shape.params = {1, 1, 1};
    CHECK(physics->addBody(box, 0, nullptr, error) == 0);
    auto makeHandle = [&]() {
      auto instance = physics->instantiate({0, 0, 0}, {0, 0, 0}, error);
      CHECK(instance.has_value());
      return instance ? object::adoptInstance(std::move(*instance))
                      : object::InstanceHandle{};
    };
    CHECK(!world.registerObjectPhysics(foreign, makeHandle()));
    CHECK(world.registerObjectPhysics(external, makeHandle()));
    CHECK(!world.registerObjectPhysics(external, makeHandle()));
    CHECK(!world.stepEmptyPhysics(0.01f, error));
    CHECK(world.physicsTime() == 0);
    CHECK(world.unregisterObjectPhysics(external));
    CHECK(!world.unregisterObjectPhysics(external));
    CHECK(world.stepEmptyPhysics(0.01f, error));
    CHECK(world.registerObjectPhysics(external, makeHandle()));
    std::weak_ptr<object::ObjectPhysics> retained = physics;
    physics.reset();
    CHECK(!retained.expired());
    CHECK(world.detachExternal(*externalHandle));
    CHECK(retained.expired());
    CHECK(!world.unregisterObjectPhysics(external));
    CHECK(world.stepEmptyPhysics(0.01f, error));
    auto staticPhysics = object::ObjectPhysics::create();
    box.shape.mass = 0;
    CHECK(staticPhysics->addBody(box, 0, nullptr, error) == 0);
    auto staticInstance =
        staticPhysics->instantiate({0, 0, 0}, {0, 0, 0}, error);
    CHECK(staticInstance.has_value());
    auto *local = world.addChild(*root, "object physics", 0);
    CHECK(local && staticInstance);
    if (local && staticInstance) {
      CHECK(world.registerObjectPhysics(
          *local, object::adoptInstance(std::move(*staticInstance))));
      CHECK(!world.stepEmptyPhysics(0.01f, error));
      CHECK(world.destroy(*local));
      CHECK(world.stepEmptyPhysics(0.01f, error));
    }
  }
  {
    GameWorld world;
    auto *root = world.addRoot();
    CHECK(root && !world.hasNodeType(18));
    auto *viewport =
        root ? world.addTypedChild(*root, 18, "pending viewport", 0) : nullptr;
    CHECK(viewport && !viewport->loaded && world.hasNodeType(18) &&
          world.countNodeType(18) == 1);
    if (viewport)
      CHECK(world.destroy(*viewport) && !world.hasNodeType(18) &&
            world.countNodeType(18) == 0);
  }

  // Player input defaults and mapping slot rules (FUN_0054d580, 0054d650,
  // 0054d700, 0054d750). These do not stand in for the missing device factory.
  {
    const auto part = parseNativePayloadDirective(
        "part cars\\racers\\Duhen_data\\scripts\\chassis.cfg");
    CHECK(part && part->device == "part" &&
          part->configPath == "cars\\racers\\Duhen_data\\scripts\\chassis.cfg");
    const auto cursor = parseNativePayloadDirective("cursor");
    CHECK(cursor && cursor->device == "cursor" && !cursor->configPath);
    CHECK(!parseNativePayloadDirective(" \t"));
    CHECK(!parseNativePayloadDirective(std::string("camera\0bad", 10)));
  }
  {
    PlayerInputState input;
    CHECK(input.mappingCount() == 0 && input.axes()[0].threshold == 0.1f &&
          input.axes()[75].factors[4] == 1.0f);
    const std::array<float, 4> range{{-1.0f, 1.0f, 0.0f, 0.5f}};
    CHECK(input.add(2, -1, 0, 0.25f, range, 3) == -1);
    CHECK(!input.add(76, 0, 0, 0.25f, range, 3));
    CHECK(input.add(2, 1, 4, 0.25f, range, 3) == 1);
    CHECK(input.add(2, 1, 4, 0.75f, range, 4) == 2);
    CHECK(input.mappingCount() == 2 && input.axes()[2].deviceType == 4 &&
          input.mappings()[0].sample == 0.25f &&
          input.mappings()[0].range == range);
    input.remove(2, 1, 4);
    CHECK(input.mappingCount() == 1 && input.mappings()[0].axis == 0 &&
          input.mappings()[1].axis == 2);
    CHECK(input.add(3, 2, 5, 0.0f, range, 1) == 1);
    input.reset();
    CHECK(input.mappingCount() == 0 && input.mappings()[0].axis == 0 &&
          input.mappings()[0].device == 2 && input.axes()[2].deviceType == 0);
    for (int i = 0; i < 152; ++i)
      CHECK(input.add(1, 0, i, 0.0f, range, 0) == i + 1);
    CHECK(input.add(1, 0, 152, 0.0f, range, 0) == 0);
    CHECK(input.mappingCount() == 152);
    // Stock uses virtual axis zero as the empty marker, even if an add stores
    // that axis; a second add therefore reuses the slot and increments again.
    input.reset();
    CHECK(input.add(0, 2, 3, 0.0f, range, 0) == 1);
    CHECK(input.add(1, 2, 3, 0.0f, range, 0) == 1);
    CHECK(input.mappingCount() == 2);
  }
  {
    PlayerInputState input;
    float key = 1.0f;
    input.sampleInput = [&](int32_t, int32_t) {
      return std::optional<float>{key};
    };
    input.inputType = [](int32_t, int32_t) {
      return std::optional<int32_t>{0};
    };
    CHECK(input.add(59, 0, 59, 0.0f, {0.0f, 1.0f, 0.0f, 1.0f}, 0) == 1);
    input.beginFrame(0.001f);
    CHECK(input.sampleAxis(59) == 1.0f);
    key = 0.0f;
    CHECK(input.sampleAxis(59) == 1.0f);
    input.beginFrame(0.001f);
    CHECK(input.sampleAxis(59) == 0.0f);
  }
  {
    // FUN_0054d830: a mouse analog axis bypasses digital ramp smoothing and
    // retains the same sample until the next input frame.
    PlayerInputState input;
    float motion = 0.75f;
    input.sampleInput = [&](int32_t, int32_t) { return std::optional<float>{motion}; };
    input.inputType = [](int32_t, int32_t) { return std::optional<int32_t>{1}; };
    input.deviceKind = [](int32_t) { return std::optional<int32_t>{2}; };
    CHECK(input.add(7, 1, 0, 0, {-1, 1, -1, 1}, 1) == 1);
    CHECK(input.setSpeed(7, 0.001f));
    input.beginFrame(0.001f);
    CHECK(input.sampleAxis(7) == 0.75f);
    motion = -0.4f;
    CHECK(input.sampleAxis(7) == 0.75f);
    input.beginFrame(0.001f);
    const auto reversed = input.sampleAxis(7);
    CHECK(reversed && std::fabs(*reversed + 0.4f) < 0.000001f);
  }
  {
    // Original constants at 005f1428/005f1070/005f0c50: non-mouse analog
    // jumps to within .02 of the target, then blends nearby motion by .3.
    PlayerInputState input;
    float stick = 0.6f;
    input.sampleInput = [&](int32_t, int32_t) { return std::optional<float>{stick}; };
    input.inputType = [](int32_t, int32_t) { return std::optional<int32_t>{1}; };
    input.deviceKind = [](int32_t) { return std::optional<int32_t>{3}; };
    CHECK(input.add(8, 2, 0, 0, {-1, 1, -1, 1}, 1) == 1);
    input.beginFrame(0.001f);
    auto value = input.sampleAxis(8);
    CHECK(value && std::fabs(*value - 0.58f) < 0.000001f);
    stick = 0.59f;
    input.beginFrame(0.001f);
    value = input.sampleAxis(8);
    CHECK(value && std::fabs(*value - 0.583f) < 0.000001f);
    stick = -0.6f;
    input.beginFrame(0.001f);
    value = input.sampleAxis(8);
    CHECK(value && std::fabs(*value + 0.58f) < 0.000001f);
    // A missing physical device class is unknown, rather than a fake zero or
    // a guessed mouse/joystick response. Refusal preserves the cached value.
    input.deviceKind = [](int32_t) { return std::optional<int32_t>{}; };
    input.beginFrame(0.001f);
    CHECK(!input.sampleAxis(8));
    CHECK(std::fabs(input.axes()[8].value + 0.58f) < 0.000001f);
  }
  {
    // 0054d8a1: a mapping from a different axis class takes over only after
    // movement strictly greater than one tenth of its source range.
    PlayerInputState input;
    float motion = 0.1f;
    input.sampleInput = [&](int32_t device, int32_t) {
      return std::optional<float>{device == 1 ? motion : 1.0f};
    };
    input.inputType = [](int32_t device, int32_t) {
      return std::optional<int32_t>{device == 1 ? 1 : 0};
    };
    input.deviceKind = [](int32_t device) {
      return std::optional<int32_t>{device == 1 ? 2 : 1};
    };
    CHECK(input.add(10, 1, 2, 0, {0, 1, -1, 1}, 1) == 1);
    CHECK(input.add(10, 0, 1, 1, {0, 1, 0, 1}, 0) == 2);
    input.beginFrame(0.001f);
    CHECK(input.sampleAxis(10) == 1.0f);
    CHECK(input.axes()[10].deviceType == 0);
    motion = 0.2f;
    input.beginFrame(0.001f);
    const auto selected = input.sampleAxis(10);
    CHECK(selected && std::fabs(*selected + 0.6f) < 0.000001f);
    CHECK(input.axes()[10].deviceType == 1);
  }
  {
    GameWorld world;
    auto *root = world.addRoot();
    auto *player =
        root ? world.addChild(*root, "control player", 0x21) : nullptr;
    CHECK(player);
    if (player) {
      world.markLoaded(*player);
      player->nativeDevice = "player";
      player->player = std::make_unique<PlayerInstance>();
      player->playerInput = std::make_unique<PlayerInputState>();
      float key = 1.0f;
      player->playerInput->sampleInput = [&](int32_t, int32_t) {
        return std::optional<float>{key};
      };
      player->playerInput->inputType = [](int32_t, int32_t) {
        return std::optional<int32_t>{0};
      };
      CHECK(player->playerInput->add(1, 0, 0xcb, 0.0f, {0, 1, 0, -1}, 0) == 1);
      player->playerInput->beginFrame(0.00025f);
      const auto pressed = world.playerControlValue(player, 1);
      CHECK(pressed && std::fabs(*pressed + 0.25f) < 1e-6f);
      key = 0;
      CHECK(world.playerControlValue(player, 1) == pressed);
      player->playerInput->beginFrame(0.00025f);
      CHECK(world.playerControlValue(player, 1) == 0.0f);
      CHECK(world.playerControlValue(player, 76) == 0.0f);
      CHECK(!world.playerControlValue(player, 0));
      player->playerInput->inputType = [](int32_t, int32_t) {
        return std::optional<int32_t>{1};
      };
      // Force the newly analog mapping to take ownership. Its physical
      // device class is still unavailable, so sampling must report a Gap.
      key = 1;
      player->playerInput->beginFrame(0.001f);
      CHECK(!world.playerControlValue(player, 1));
      player->player->scriptedControlOverride = true;
      CHECK(!world.playerControlValue(player, 1));
      OwnerLink observer;
      setLinkOwner(observer, player);
      CHECK(world.destroy(*player));
      CHECK(!observer.owner && !world.playerControlValue(observer.owner, 1));
    }
  }
  {
    GameWorld world;
    auto *root = world.addRoot();
    std::string error;
    CHECK(root && world.stepEmptyPhysics(0.00005f, error));
    CHECK(world.physicsTime() == 0.0f);
    CHECK(world.stepEmptyPhysics(0.00006f, error));
    CHECK(world.physicsTime() == 0.00005f + 0.00006f);
  }
  {
    GameWorld world;
    auto *root = world.addRoot();
    std::string error;
    CHECK(root && world.stepEmptyPhysics(0.01f, error));
    CHECK(world.physicsTime() == 0.01f);
    CHECK(!world.stepEmptyPhysics(0.0f, error));
    CHECK(world.physicsTime() == 0.01f);
    LinkOwner body;
    world.trackLoadState(body, 2, true);
    CHECK(!world.stepEmptyPhysics(0.01f, error));
    CHECK(world.physicsTime() == 0.01f);
    world.forgetLoadState(body);
    auto *box = world.addTypedChild(*root, 2, "box", 1);
    CHECK(box && world.registerStaticBox(*box, {1, 1, 1}));
    world.markLoaded(*box);
    CHECK(world.stepEmptyPhysics(0.01f, error));
    auto *child = world.addChild(*box, "child", 0);
    CHECK(child && !world.stepEmptyPhysics(0.01f, error));
    CHECK(world.destroy(*child));
    CHECK(world.stepEmptyPhysics(0.01f, error));
    CHECK(world.destroy(*box));
    CHECK(world.stepEmptyPhysics(0.01f, error));
    auto *node = world.addChild(*root, "hook", 0);
    CHECK(node && world.registerHook(GameHook::Kind9, *node));
    CHECK(!world.stepEmptyPhysics(0.01f, error));
    world.unregisterHook(GameHook::Kind9, node);
    CHECK(world.stepEmptyPhysics(0.01f, error));
  }
  {
    GameWorld world;
    WorldNode *root = world.addRoot();
    WorldNode *first = world.addChild(*root, "first", 0);
    WorldNode *second = world.addChild(*root, "second", 0);
    CHECK(first && second);
    CHECK(world.registerHook(GameHook::Kind8, *first));
    CHECK(world.registerHook(GameHook::Kind1c, *first));
    CHECK(world.registerHook(GameHook::Kind8, *second));
    CHECK(world.registerHook(GameHook::Kind8, *first));
    CHECK(
        world.registerHook(GameHook::Kind8, static_cast<LinkOwner *>(nullptr)));
    world.unregisterHook(GameHook::Kind8, first);
    CHECK((world.hooks(GameHook::Kind8) ==
           std::vector<const WorldNode *>{nullptr, second, first, nullptr}));
    CHECK(world.hooks(GameHook::Kind9).empty());
    GameWorld foreign;
    WorldNode *foreignRoot = foreign.addRoot();
    CHECK(foreignRoot && !world.registerHook(GameHook::Kind9, *foreignRoot));
    CHECK(world.destroy(*first));
    CHECK((world.hooks(GameHook::Kind8) ==
           std::vector<const WorldNode *>{nullptr, second, nullptr, nullptr}));
    CHECK((world.hooks(GameHook::Kind1c) ==
           std::vector<const WorldNode *>{nullptr}));
    world.pruneDetachedHooks(GameHook::Kind8);
    CHECK((world.hooks(GameHook::Kind8) ==
           std::vector<const WorldNode *>{second}));
    world.pruneDetachedHooks(GameHook::Kind1c);
    CHECK(world.hooks(GameHook::Kind1c).empty());
  }
  {
    GameWorld world;
    auto *root = world.addRoot();
    auto *node = world.addChild(*root, "cursor", 0);
    LinkOwner controller;
    CHECK(node);
    if (node) {
      node->cursor = std::make_unique<CursorInstance>();
      CHECK(!node->cursor->controller.owner);
      setLinkOwner(node->cursor->controller, &controller);
      CHECK(controller.head);
      CHECK(world.destroy(*node));
      CHECK(!controller.head);
    }
  }
  {
    GameWorld world;
    auto *root = world.addRoot();
    auto *first = world.addTypedChild(*root, 12, "first", 0);
    auto *second = world.addTypedChild(*root, 3, "second", 0);
    CHECK(first && second);
    if (first && second) {
      const uint32_t firstId = first->instanceId, secondId = second->instanceId;
      CHECK(world.queueDestroy(*first) && world.queueDestroy(*first) &&
            world.queueDestroy(*second));
      CHECK(world.deferredDestroyCount() == 3);
      CHECK(world.destroy(*first));
      std::string error;
      CHECK(world.drainDeferredDestroy(error));
      CHECK(!world.findInstance(firstId) && !world.findInstance(secondId));
      CHECK(world.deferredDestroyCount() == 0);
      auto *third = world.addTypedChild(*root, 12, "third", 0);
      auto *fourth = world.addTypedChild(*root, 3, "fourth", 0);
      CHECK(third && fourth);
      if (third && fourth) {
        std::vector<uint32_t> order;
        const uint32_t thirdId = third->instanceId,
                       fourthId = fourth->instanceId;
        const auto observer = world.addNodeDestroyObserver(
            [&](LinkOwner &owner) { order.push_back(owner.value); });
        CHECK(world.queueDestroy(*third) && world.queueDestroy(*fourth));
        CHECK(world.drainDeferredDestroy(error));
        CHECK((order == std::vector<uint32_t>{thirdId, fourthId}));
        world.removeNodeDestroyObserver(observer);
      }
    }
  }
  {
    LinkOwner sound, font, overlay, debug;
    sound.value = 9;
    font.value = 0x10023;
    {
      GameWorld world;
      world.addRoot();
      world.setClassResolver([](const std::string &) { return true; });
      ReceiverBinding receiver;
      int scriptGetInfoCalls = 0;
      receiver.getInfo = [&](int) -> std::optional<int32_t> {
        ++scriptGetInfoCalls;
        return 0;
      };
      GameCreationRequest request{50, "java.io.Controller", {}, std::string{"0 0 2"}, "controller", "player"};
      PlayerFactorySetup setup{{3, 0.5f, 12.0f, 4.0f}, &sound, &font};
      const auto made =
          createNativeInstance(world, request, &receiver, nullptr, &setup);
      CHECK(made.status == CreationStatus::Created && made.node);
      if (made.node) {
        CHECK(made.node->player && made.node->playerInput &&
              made.node->player->playerNumber == 2 &&
              made.node->player->name == "Player 2" &&
              made.node->player->config.display == 3 &&
              made.node->player->config.damageMultiplier == 0.5f &&
              made.node->player->font.owner == &font &&
              made.node->eventMask == 0x10 && scriptGetInfoCalls == 0);
        CHECK((world.hooks(GameHook::Kind8) ==
               std::vector<const WorldNode *>{made.node}));
        CHECK(world.hooks(GameHook::Kind9).empty());
        CHECK((world.hooks(GameHook::Kind1c) ==
               std::vector<const WorldNode *>{made.node}));
        CHECK(world.playerInputs().size() == 1 &&
              world.playerInputs()[0] == made.node);
        std::string physicsError;
        CHECK(world.stepEmptyPhysics(0.01f, physicsError));
        setLinkOwner(made.node->player->animationOverlay, &overlay);
        setLinkOwner(made.node->player->debugText[0], &debug);
        CHECK(world.destroy(*made.node));
        CHECK(!overlay.head && !debug.head);
        CHECK(world.playerInputs().empty() && !receiver.link.owner &&
              !font.head);
        CHECK((world.hooks(GameHook::Kind8) ==
               std::vector<const WorldNode *>{nullptr}));
      }
      CHECK(sound.head);
    }
    CHECK(!sound.head && !font.head);
  }
  PackRegistry registry("ref/raw");
  std::string error;
  uint32_t dummy = 0, wrong = 0;
  CHECK(registry.resolveLiteral("system.rpk", 0x1b, dummy, error));
  const auto prepared = prepareGameCreation(registry, dummy, {}, {}, "dummy");
  CHECK(prepared.request && prepared.request->scriptResourceId == dummy);
  if (prepared.request) {
    CHECK(prepared.request->scriptClass == "java.util.resource.Dummy");
    CHECK(prepared.request->nativeDevice.empty());
    CHECK(!prepared.request->parentResourceId &&
          !prepared.request->constructorArgument);
    CHECK(prepared.request->name == "dummy");
    const auto descriptor = prepareGameInstanceDescriptor(*prepared.request);
    CHECK(descriptor.type == 1 && descriptor.flags == 0x21 &&
          descriptor.name == "dummy");
  }
  for (size_t n : {size_t{0}, size_t{29}, size_t{30}, size_t{31}, size_t{32},
                   size_t{80}}) {
    GameCreationRequest input{};
    input.name = std::string(n, 'x');
    const auto descriptor = prepareGameInstanceDescriptor(input);
    CHECK(descriptor.type == 1 && descriptor.flags == 0x21);
    CHECK(descriptor.name == std::string(n < 30 ? n : 30, 'x'));
  }
  GameCreationRequest embedded{};
  embedded.name = std::string("a\0b", 3);
  CHECK(prepareGameInstanceDescriptor(embedded).name == "a");
  const auto supplied =
      prepareGameCreation(registry, dummy, 0x1234, "args", "label");
  CHECK(supplied.request && supplied.request->parentResourceId == 0x1234 &&
        supplied.request->constructorArgument == "args" &&
        supplied.request->name == "label");
  const auto emptyParent = prepareGameCreation(registry, dummy, 0, {}, "dummy");
  CHECK(emptyParent.request && !emptyParent.request->parentResourceId);
  CHECK(!prepareGameCreation(registry, 0, {}, {}, "dummy").request);
  CHECK(!prepareGameCreation(registry, dummy, {}, {}, {}).request);
  CHECK(!prepareGameCreation(registry, dummy, {}, {}, std::string("a\0b", 3))
             .request);
  CHECK(!prepareGameCreation(registry, 0xfffeffff, {}, {}, "dummy").request);
  CHECK(registry.resolveLiteral("frontend.rpk", 0x28, wrong, error));
  CHECK(!prepareGameCreation(registry, wrong, {}, {}, "dummy").request);

  uint32_t camera = 0;
  CHECK(registry.resolveLiteral("system.rpk", 0x33, camera, error));
  const auto cameraRequest =
      prepareGameCreation(registry, camera, {}, {}, "camera");
  CHECK(cameraRequest.request && cameraRequest.request->scriptClass.empty() &&
        cameraRequest.request->nativeDevice == "camera" &&
        !cameraRequest.request->nativeConfigPath);

  // The real io.Controller script resource (the startup path's first
  // createNativeInstance caller) uses the path form plus a native device
  // line: FUN_005461a0 maps "system\Scripts\io\Controller.class" through the
  // script-root table to the class the file header names.
  {
    PackResource probe;
    if (registry.lookup(50, probe, error)) {
      CHECK(probe.entry.resType == 8);
      const auto controllers =
          prepareGameCreation(registry, 50, {}, {}, "controller");
      CHECK(controllers.request &&
            controllers.request->scriptClass == "java.io.Controller");
      CHECK(controllers.request &&
            controllers.request->nativeDevice == "player");
    }
  }

  // createNativeInstance: the Dummy path against the real installed script
  // resource.
  {
    const auto request = prepareGameCreation(registry, dummy, {}, {}, "dummy");
    CHECK(request.request.has_value());
    if (!request.request)
      return 1;
    const auto resolver = [](const std::string &name) {
      return name == "java.util.resource.Dummy" &&
             std::filesystem::exists(
                 "ref/raw/system/Scripts/util/resource/Dummy.class");
    };
    std::vector<int> kinds;
    const auto answerZero = [&kinds](int kind) -> std::optional<int32_t> {
      kinds.push_back(kind);
      return 0;
    };

    GameWorld world;
    world.setClassResolver(resolver);
    WorldNode *root = world.addRoot();
    CHECK(root && world.defaultParent() == root);
    // FUN_00535e70: type 0, name nulltype, no instance id, flags 0, loaded, no
    // sector link, only one of them.
    CHECK(root && root->type == 0 && root->name == "nulltype" &&
          root->instanceId == 0 && root->flags == 0);
    CHECK(root && root->loaded && root->loadPriority == 1.0f &&
          !root->sectorLink.owner && !world.findInstance(0));
    CHECK(world.addRoot() == nullptr && world.nodeCount() == 1);
    ReceiverBinding receiver;
    receiver.getInfo = answerZero;

    auto deviceRequest = *request.request;
    deviceRequest.nativeDevice = "player";
    const auto deviceResult =
        createNativeInstance(world, deviceRequest, &receiver);
    CHECK(deviceResult.status == CreationStatus::Unsupported &&
          deviceResult.issue == CreationIssue::NativeDeviceUnsupported);
    CHECK(world.nodeCount() == 1 && !receiver.link.owner && kinds.empty());

    CHECK(createNativeInstance(world, *request.request, nullptr).issue ==
          CreationIssue::NoReceiverLink);
    ReceiverBinding noUpcall;
    const auto before = world.nodeCount();
    auto refused = createNativeInstance(world, *request.request, &noUpcall);
    CHECK(refused.status == CreationStatus::Unsupported &&
          refused.issue == CreationIssue::GetInfoUpcallMissing);
    CHECK(world.nodeCount() == before && world.children(*root)->empty() &&
          !noUpcall.link.owner);

    auto created = createNativeInstance(world, *request.request, &receiver);
    CHECK(created.status == CreationStatus::Created && created.node);
    if (!created.node)
      return 1;
    WorldNode &node = *created.node;
    CHECK((kinds == std::vector<int>{8, 9, 0x1c}));
    CHECK(node.name == "dummy" && node.type == 1 && node.flags == 0x221 &&
          node.loaded && node.loadPriority == 1.0f);
    // FUN_00544fe0 with the type-0 root as the parent: its hook entry is 0, so
    // the node is its own sector root.
    CHECK(node.sectorLink.owner == &node &&
          node.sectorLink.cachedValue == node.instanceId);
    CHECK(node.instanceId == created.instanceId &&
          (node.instanceId >> 16) == 0xffff);
    CHECK(world.parent(node) == root && world.children(*root)->size() == 1 &&
          (*world.children(*root))[0] == &node);
    CHECK(world.findInstance(node.instanceId) == &node && node.hasReceiver);
    ScriptNode *script = world.findScript(dummy);
    CHECK(script && script->loaded &&
          script->className == "java.util.resource.Dummy");
    CHECK(script && node.scriptLink.owner == script &&
          node.scriptLink.cachedValue == dummy);
    CHECK(receiver.link.owner == &node &&
          receiver.link.cachedValue == node.instanceId &&
          node.head == &receiver.link);
    CHECK(world.createdInstances() == 1);

    // Another creation appends after the first, reuses the script node and gets
    // a new id.
    ReceiverBinding second;
    second.getInfo = answerZero;
    auto again = createNativeInstance(world, *request.request, &second);
    CHECK(again.status == CreationStatus::Created && again.node &&
          again.node != &node);
    CHECK(again.node && again.instanceId != created.instanceId &&
          world.findScript(dummy) == script);
    CHECK(again.node && (*world.children(*root)).size() == 2 &&
          (*world.children(*root))[1] == again.node);

    // Re-running the handler on the same receiver moves its link to the new
    // node.
    auto moved = createNativeInstance(world, *request.request, &receiver);
    CHECK(moved.status == CreationStatus::Created && moved.node &&
          receiver.link.owner == moved.node);
    CHECK(!listed(node, receiver.link) && moved.node->head == &receiver.link);

    // An explicit parent id places the child under that node.
    auto nested =
        prepareGameCreation(registry, dummy, node.instanceId, {}, "child");
    CHECK(nested.request &&
          nested.request->parentResourceId == node.instanceId);
    ReceiverBinding third;
    third.getInfo = answerZero;
    auto child = createNativeInstance(world, *nested.request, &third);
    CHECK(child.status == CreationStatus::Created && child.node &&
          world.parent(*child.node) == &node);
    // A type-1 parent is hooked, so the child copies the parent's sector link
    // (here: the parent itself) and does not start its own.
    CHECK(child.node && child.node->sectorLink.owner == &node &&
          child.node->sectorLink.cachedValue == node.instanceId);
    CHECK(world.createdInstances() >= 1);
    auto missingParent =
        prepareGameCreation(registry, dummy, 0xffff7777, {}, "orphan");
    const auto count = world.nodeCount();
    auto orphan = createNativeInstance(world, *missingParent.request, &third);
    CHECK(orphan.status == CreationStatus::StockError &&
          orphan.issue == CreationIssue::ParentNotFound);
    CHECK(world.nodeCount() == count);

    // Destroying a node detaches the receiver link that points at it, with its
    // children.
    const uint32_t childId = child.instanceId, firstId = created.instanceId;
    const uint32_t countedBeforeDestroy = world.createdInstances();
    CHECK(countedBeforeDestroy >= 2);
    CHECK(world.destroy(node));
    CHECK(world.createdInstances() == countedBeforeDestroy - 2);
    CHECK(!world.findInstance(firstId) && !world.findInstance(childId));
    CHECK(!third.link.owner && third.link.cachedValue == 0 &&
          second.link.owner == again.node);

    // Unsupported results leave the tree alone.
    const size_t stable = world.nodeCount();
    ReceiverBinding initialized;
    std::vector<int> calls;
    initialized.initializeReceiver =
        [&](WorldNode &createdNode) -> std::optional<std::string> {
      CHECK(createdNode.scriptLink.owner == script && createdNode.loaded);
      calls.push_back(1);
      return std::nullopt;
    };
    initialized.getInfo = [&](int kind) -> std::optional<int32_t> {
      calls.push_back(kind);
      return 0;
    };
    auto initializedResult =
        createNativeInstance(world, *request.request, &initialized);
    CHECK(initializedResult.status == CreationStatus::Created);
    CHECK((calls == std::vector<int>{1, 8, 9, 0x1c}));
    if (initializedResult.node)
      CHECK(world.destroy(*initializedResult.node));
    ReceiverBinding badInitialization;
    badInitialization.initializeReceiver =
        [](WorldNode &) -> std::optional<std::string> {
      return "constructor failed";
    };
    badInitialization.getInfo = [&](int) -> std::optional<int32_t> {
      CHECK(false);
      return 0;
    };
    auto failedInitialization =
        createNativeInstance(world, *request.request, &badInitialization);
    CHECK(failedInitialization.status == CreationStatus::Unsupported &&
          failedInitialization.detail == "constructor failed");
    CHECK(world.nodeCount() == stable && !badInitialization.link.owner);
    ReceiverBinding destroyedInitialization;
    destroyedInitialization.initializeReceiver =
        [&](WorldNode &createdNode) -> std::optional<std::string> {
      CHECK(world.destroy(createdNode));
      return std::nullopt;
    };
    destroyedInitialization.getInfo = [&](int) -> std::optional<int32_t> {
      CHECK(false);
      return 0;
    };
    auto destroyedResult =
        createNativeInstance(world, *request.request, &destroyedInitialization);
    CHECK(destroyedResult.status == CreationStatus::Unsupported &&
          world.nodeCount() == stable);
    ReceiverBinding destroyedHook;
    WorldNode *hookNode = nullptr;
    destroyedHook.initializeReceiver =
        [&](WorldNode &createdNode) -> std::optional<std::string> {
      hookNode = &createdNode;
      return std::nullopt;
    };
    destroyedHook.getInfo = [&](int) -> std::optional<int32_t> {
      CHECK(world.destroy(*hookNode));
      return 0;
    };
    auto destroyedHookResult =
        createNativeInstance(world, *request.request, &destroyedHook);
    CHECK(destroyedHookResult.status == CreationStatus::Unsupported &&
          world.nodeCount() == stable);
    ReceiverBinding hook;
    hook.getInfo = [](int kind) -> std::optional<int32_t> {
      return kind == 9 ? 1 : 0;
    };
    auto hooked = createNativeInstance(world, *request.request, &hook);
    CHECK(hooked.status == CreationStatus::Unsupported &&
          hooked.issue == CreationIssue::GetInfoRegistration);
    CHECK(world.nodeCount() == stable && !hook.link.owner);
    ReceiverBinding failing;
    failing.getInfo = [](int) -> std::optional<int32_t> {
      return std::nullopt;
    };
    const auto failedCall =
        createNativeInstance(world, *request.request, &failing);
    CHECK(failedCall.status == CreationStatus::Unsupported &&
          failedCall.issue == CreationIssue::GetInfoFailed);
    CHECK(world.nodeCount() == stable);

    root->loaded = false;
    GameCreationRequest zero = *request.request;
    zero.scriptResourceId = 0;
    CHECK(createNativeInstance(world, zero, &receiver).issue ==
          CreationIssue::ZeroScriptId);
    CHECK(createNativeInstance(world, *request.request, &receiver).issue ==
          CreationIssue::ParentNotLoaded);
    root->loaded = true;
    CHECK(world.nodeCount() == stable);

    // Sector rule (FUN_00544fe0, FUN_00544f40), directly on the world model.
    // Flag 0x10 on creation or later makes the node its own sector root even
    // under a hooked parent. Destroying the sector root detaches the copies.
    WorldNode *sectorParent = world.addChild(*root, "sector_parent", 0x21);
    WorldNode *inherited =
        sectorParent ? world.addChild(*sectorParent, "inherited", 0x21)
                     : nullptr;
    WorldNode *ownRoot =
        sectorParent ? world.addChild(*sectorParent, "own", 0x31) : nullptr;
    CHECK(sectorParent && inherited && ownRoot);
    if (sectorParent && inherited && ownRoot) {
      CHECK(sectorParent->sectorLink.owner == sectorParent &&
            inherited->sectorLink.owner == sectorParent);
      CHECK(ownRoot->sectorLink.owner == ownRoot);
      world.orFlags(*inherited, 0x10);
      CHECK(inherited->sectorLink.owner == inherited &&
            inherited->sectorLink.cachedValue == inherited->instanceId);
      world.orFlags(*inherited, 0x10);
      CHECK(inherited->sectorLink.owner == inherited);
      WorldNode *grandchild = world.addChild(*inherited, "grandchild", 0x21);
      CHECK(grandchild && grandchild->sectorLink.owner == inherited);
      world.destroy(*inherited);
      WorldNode *orphanedCopy = world.addChild(*sectorParent, "copy", 0x21);
      CHECK(orphanedCopy && orphanedCopy->sectorLink.owner == sectorParent);
      world.destroy(*sectorParent);
    }
    CHECK(world.nodeCount() == stable);

    GameWorld bare;
    WorldNode *bareRoot = bare.addRoot();
    CHECK(bareRoot && bareRoot->type == 0);
    ReceiverBinding bareReceiver;
    bareReceiver.getInfo = answerZero;
    auto noResolver =
        createNativeInstance(bare, *request.request, &bareReceiver);
    CHECK(noResolver.status == CreationStatus::Unsupported &&
          noResolver.issue == CreationIssue::ClassResolverMissing);
    bare.setClassResolver([](const std::string &) { return false; });
    CHECK(createNativeInstance(bare, *request.request, &bareReceiver).issue ==
          CreationIssue::ScriptClassMissing);
    GameWorld empty;
    empty.setClassResolver(resolver);
    CHECK(createNativeInstance(empty, *request.request, &bareReceiver).issue ==
          CreationIssue::ParentNotFound);

    // Names keep stock's 30-character limit.
    GameCreationRequest longName = *request.request;
    longName.name = std::string(80, 'n');
    auto truncated = createNativeInstance(world, longName, &receiver);
    CHECK(truncated.node && truncated.node->name == std::string(30, 'n'));
  }
  {
    // Stock button factory FUN_00436f30: the `render` and `body` directives of
    // the native cfg parse into the script's ButtonPrototype with resource ids
    // remapped through the script's own pack; an existing prototype accepts
    // only its own config path and is reused without reparsing.
    PackRegistry packs(
        ".scratch/2005-extract/Program Files/Street Legal Racing - Redline");
    GameWorld world;
    CHECK(world.addRoot());
    ResourceWorld resources(world, packs);
    std::string error;
    uint32_t scriptId = 0;
    CHECK(packs.resolveLiteral("cars\\racers\\Baiern.rpk", 0x157, scriptId,
                               error));
    ScriptNode* source = world.addScript(scriptId, "");
    CHECK(source && source->resourceId == scriptId && !source->button);
    const std::string path = "misc\\garage\\scripts\\decals_button.cfg";
    CHECK(loadButtonScriptPrototype(resources, *source, path, error));
    CHECK(error.empty());
    CHECK(source->button && source->button->configPath == path);
    PackView view;
    CHECK(packs.view(static_cast<uint16_t>(scriptId >> 16), view, error) ==
          PackViewStatus::Ok);
    // render 0x00000083 and body 0x00000090 remap to the script pack's slot.
    CHECK(source->button->renderId ==
          ((static_cast<uint32_t>(view.slot) << 16) | 0x83));
    CHECK(source->button->hasBody);
    CHECK(source->button->bodyId ==
          ((static_cast<uint32_t>(view.slot) << 16) | 0x90));
    CHECK(source->button->bodyName.empty());
    // The same config path reuses the prototype and clears the error.
    ButtonPrototype* cached = source->button.get();
    error = "stale";
    CHECK(loadButtonScriptPrototype(resources, *source, path, error));
    CHECK(error.empty() && source->button.get() == cached);
    // A different config path is an error and keeps the cached prototype.
    CHECK(!loadButtonScriptPrototype(
        resources, *source, "misc\\garage\\scripts\\other.cfg", error));
    CHECK(!error.empty() && source->button.get() == cached);
    // Empty and "-" name no config at all.
    CHECK(!loadButtonScriptPrototype(resources, *source, "", error));
    CHECK(!loadButtonScriptPrototype(resources, *source, "-", error));
    // A zero local id names a primitive mesh body: bodyName is kept and the
    // numeric id stays zero.
    ScriptNode* primitive = world.addScript(scriptId + 1, "");
    CHECK(primitive);
    const std::string primitivePath = "frontend\\scripts\\paintcan.cfg";
    CHECK(loadButtonScriptPrototype(resources, *primitive, primitivePath,
                                    error));
    CHECK(primitive->button && primitive->button->hasBody &&
          primitive->button->bodyId == 0 &&
          primitive->button->bodyName == "0" &&
          primitive->button->renderId ==
              ((static_cast<uint32_t>(view.slot) << 16) | 0x21));
  }
  return failures ? 1 : 0;
}
