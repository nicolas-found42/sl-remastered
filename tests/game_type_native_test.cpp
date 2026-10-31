#include "camera_device_body.hpp"
#include "camera_device_factory.hpp"
#include "camera_traversal.hpp"
#include "car_runtime.hpp"
#include "car_wheel_runtime.hpp"
#include "class_environment.hpp"
#include "game_spatial_payload.hpp"
#include "ground_runtime.hpp"
#include "hotkeys.hpp"
#include "natives/game_type.hpp"
#include "natives/registry.hpp"
#include "pack_registry.hpp"
#include "physics_compound.hpp"
#include "render2d.hpp"
#include "resource_world.hpp"
#include "timers.hpp"
#include "vm.hpp"
#include "vm_test_harness.hpp"
#include "wheel_tire_step.hpp"
#include "world_model.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <set>

namespace {
ClassEnvironment *environment = nullptr;
std::set<std::string> graphDone;
const VmObject *eventText = nullptr;
std::vector<VmValue> cityLoadArgs;
ResourceNode *city2LoadTarget = nullptr;
std::vector<VmValue> groundEventArgs;
VmScriptCall groundEventCallback;
const VmObject *groundEventReceiver = nullptr;

NativeResult
probeCityNotification(const std::vector<VmValue> &, const VmObject *receiver,
                      VmArena *arena,
                      const std::vector<const TufaClass *> *classes) {
  return gameTypeAddNotification(cityLoadArgs, receiver, arena, classes);
}

NativeResult probeCity2Ground(const std::vector<VmValue> &, const VmObject *,
                              VmArena *arena,
                              const std::vector<const TufaClass *> *) {
  if (!arena || !city2LoadTarget)
    return std::string{"city2 probe target missing"};
  if (const auto error = loadGroundGameResource(*arena, *city2LoadTarget))
    return *error;
  return std::monostate{};
}

NativeResult probeGroundEvent(const std::vector<VmValue> &, const VmObject *,
                              VmArena *arena,
                              const std::vector<const TufaClass *> *classes) {
  auto previous = std::move(arena->callScript);
  arena->callScript = groundEventCallback;
  auto result =
      gameRefQueueEvent(groundEventArgs, groundEventReceiver, arena, classes);
  arena->callScript = std::move(previous);
  return result;
}

NativeResult
triggerPlayerCommand(const std::vector<VmValue> &args, const VmObject *receiver,
                     VmArena *arena,
                     const std::vector<const TufaClass *> *classes) {
  if (!args.empty())
    return std::string{"invalid trigger"};
  const VmObject *null = nullptr;
  return gameRefQueueEvent(
      {VmValue{null}, VmValue{int32_t{0x10}}, VmValue{eventText}}, receiver,
      arena, classes);
}

const TufaClass *loadGraph(const std::string &name) {
  std::string error;
  const TufaClass *cls = environment->findOrLoad(name, error);
  if (!cls || !cls->hasClassHeader)
    return nullptr;
  if (graphDone.count(name))
    return cls;
  graphDone.insert(name);
  const auto reference = [&](uint32_t index) {
    if (index >= cls->constants.size() || cls->constants[index].kind != 4)
      return false;
    return loadGraph(cls->constants[cls->constants[index].a].text) != nullptr;
  };
  if (cls->classHeader[3] != UINT32_MAX && !reference(cls->classHeader[3]))
    return nullptr;
  for (uint32_t index : cls->interfaces)
    if (!reference(index))
      return nullptr;
  return cls;
}

const TufaMethod *findConstructor(const TufaClass &cls,
                                  const std::string &parameters) {
  for (const auto &list : cls.methods)
    for (const auto &method : list) {
      const auto &w = method.words;
      if (cls.constants[w[1]].text == "<init>" &&
          cls.constants[w[2]].text == parameters)
        return &method;
    }
  return nullptr;
}
template <typename Dispatch>
bool finishDispatch(Dispatch &dispatch, VmScheduler &scheduler,
                    std::string &error) {
  // Two things can hold a dispatch:
  //  * an awaited subscriber (animation callbacks) which only resumes when
  //    the scheduler has finished its host task, so resume() must be retried
  //    after every tick, and
  //  * fire-and-forget cursor subscribers, which outlive the coroutine, so
  //    the loop also runs until the scheduler is idle.
  for (unsigned i = 0; i < 1000; ++i) {
    if (!dispatch.resume(error))
      return false;
    if (dispatch.finished() && scheduler.threadCount() == 0)
      return true;
    const auto tick = scheduler.tick(10000);
    if (!tick.ok) {
      error = tick.error;
      return false;
    }
  }
  error = "dispatch did not finish";
  return false;
}

bool runCursorTest(VmArena &arena, WorldNode &source,
                   const std::string &payload,
                   const std::vector<const TufaClass *> &classes,
                   const std::vector<VmStaticFields> &states,
                   std::string &error) {
  VmScheduler scheduler(arena, &nativeRegistry(), &classes, &states,
                        [] { return 0.0; });
  CursorDispatch dispatch(arena, scheduler, source, payload, classes, states);
  return finishDispatch(dispatch, scheduler, error);
}

bool runAnimationTest(VmArena &arena, float delta,
                      const std::vector<const TufaClass *> &classes,
                      const std::vector<VmStaticFields> &states,
                      std::string &error) {
  VmScheduler scheduler(arena, &nativeRegistry(), &classes, &states,
                        [] { return 0.0; });
  AnimationDispatch dispatch(arena, scheduler, delta, classes, states);
  return finishDispatch(dispatch, scheduler, error);
}
} // namespace

int main() {
  PackRegistry packs("ref/raw");
  ClassEnvironment classes("ref/raw");
  environment = &classes;
  const TufaClass *stringClass = loadGraph("java.lang.String");
  const TufaClass *dummy = loadGraph("java.util.resource.Dummy");
  const TufaClass *gameRef = loadGraph("java.util.resource.GameRef");
  const TufaClass *vectorClass = loadGraph("java.util.Vector");
  CHECK(stringClass && dummy && gameRef && vectorClass);
  if (!stringClass || !dummy || !gameRef || !vectorClass)
    return 1;
  const auto loaded = classes.classes();

  {
    GameWorld inputWorld;
    VmGameState inputGame;
    inputGame.world = &inputWorld;
    VmArena inputArena;
    inputArena.gameState = &inputGame;
    CHECK(std::holds_alternative<std::string>(
        inputGetAxis({int32_t{0}, int32_t{7}}, nullptr, &inputArena, &loaded)));
    inputWorld.setInputDevices({"SysKeyboard", "SysMouse", "Joystick"});
    CHECK(std::holds_alternative<std::string>(
        inputGetAxis({int32_t{0}, int32_t{7}}, nullptr, &inputArena, &loaded)));
    HotkeyState inputHotkeys;
    inputArena.hotkeys = &inputHotkeys;
    inputHotkeys.physicalKey = [](int32_t key) -> std::optional<float> {
      return key == 7 ? std::optional<float>{1.0f} : std::nullopt;
    };
    CHECK(std::get<float>(std::get<VmValue>(inputGetAxis(
              {int32_t{0}, int32_t{7}}, nullptr, &inputArena, &loaded))) ==
          1.0f);
    CHECK(std::holds_alternative<std::string>(
        inputGetAxis({int32_t{0}, int32_t{8}}, nullptr, &inputArena, &loaded)));
    CHECK(std::get<float>(std::get<VmValue>(inputGetAxis(
              {int32_t{1}, int32_t{7}}, nullptr, &inputArena, &loaded))) ==
          0.0f);
    CHECK(std::holds_alternative<std::string>(
        inputGetAxis({int32_t{2}, int32_t{7}}, nullptr, &inputArena, &loaded)));
    CHECK(std::holds_alternative<std::string>(
        invokeNative("java.lang.System", "isLoading", "()I", {}, nullptr,
                     &inputArena, &loaded)));
    CHECK(std::holds_alternative<std::string>(
        invokeNative("java.lang.System", "isLoadingReset", "()V", {}, nullptr,
                     &inputArena, &loaded)));
  }
  {
    VmArena displayArena;
    VmGameState displayGame;
    displayArena.gameState = &displayGame;
    CHECK(std::holds_alternative<std::string>(
        gfxForceRendering({}, nullptr, &displayArena, &loaded)));
    int forceCalls = 0;
    displayGame.forceRendering = [&](std::string &error) {
      ++forceCalls;
      error.clear();
      return true;
    };
    CHECK(std::holds_alternative<std::monostate>(
              gfxForceRendering({}, nullptr, &displayArena, &loaded)) &&
          forceCalls == 1);
    displayGame.forceRendering = [](std::string &error) {
      error = "pending load";
      return false;
    };
    CHECK(std::get<std::string>(
              gfxForceRendering({}, nullptr, &displayArena, &loaded))
              .find("pending load") != std::string::npos);
    CHECK(std::holds_alternative<std::string>(
        gfxFlush({}, nullptr, &displayArena, &loaded)));
    int flushCalls = 0;
    displayGame.flush = [&](std::string &error) {
      ++flushCalls;
      error.clear();
      return true;
    };
    CHECK(std::holds_alternative<std::monostate>(
              gfxFlush({}, nullptr, &displayArena, &loaded)) &&
          flushCalls == 1);
    displayGame.flush = [](std::string &error) {
      error = "forced aging failed";
      return false;
    };
    CHECK(std::get<std::string>(gfxFlush({}, nullptr, &displayArena, &loaded))
              .find("forced aging failed") != std::string::npos);
    CHECK(std::holds_alternative<std::string>(
        gfxNumDisplayModes({}, nullptr, &displayArena, &loaded)));
    displayGame.displayModes =
        std::vector<PlatformDisplayMode>{{1920, 1080, 32}, {800, 600, 16}};
    CHECK(std::get<int32_t>(std::get<VmValue>(
              gfxNumDisplayModes({}, nullptr, &displayArena, &loaded))) == 2);
    CHECK(std::holds_alternative<std::string>(
        gfxCurrDisplayMode({}, nullptr, &displayArena, &loaded)));
    displayGame.currentDisplayMode = [] {
      return std::optional<PlatformDisplayMode>{{800, 600, 16}};
    };
    CHECK(std::get<int32_t>(std::get<VmValue>(
              gfxCurrDisplayMode({}, nullptr, &displayArena, &loaded))) == 1);
    displayGame.currentDisplayMode = [] {
      return std::optional<PlatformDisplayMode>{{640, 480, 32}};
    };
    CHECK(std::get<int32_t>(std::get<VmValue>(
              gfxCurrDisplayMode({}, nullptr, &displayArena, &loaded))) == 0);
    auto named =
        gfxDisplayModeName({int32_t{1}}, nullptr, &displayArena, &loaded);
    CHECK(std::holds_alternative<VmValue>(named));
    CHECK(!displayArena.strings.empty() &&
          displayArena.strings.back().first == "800 600 16");
    named = gfxDisplayModeName({int32_t{-1}}, nullptr, &displayArena, &loaded);
    CHECK(std::holds_alternative<VmValue>(named) &&
          displayArena.strings.back().first.empty());
  }
  std::vector<VmStaticFields> states;
  VmArena arena;
  std::string resolveError;
  VmResourceResolver resolve = [&](std::string_view name,
                                   uint32_t id) -> std::optional<uint32_t> {
    uint32_t result = 0;
    if (!packs.resolveLiteral(std::string(name), id, result, resolveError))
      return std::nullopt;
    return result;
  };
  CHECK(vmInitializeClass(*stringClass, loaded, states, &resolve, &arena).ok);
  CHECK(vmInitializeClass(*dummy, loaded, states, &resolve, &arena).ok);
  CHECK(vmInitializeClass(*vectorClass, loaded, states, &resolve, &arena).ok);

  GameWorld world;
  world.setClassResolver([&](const std::string &name) {
    std::string ignored;
    return classes.findOrLoad(name, ignored) != nullptr;
  });
  WorldNode *root = world.addRoot();
  CHECK(root);
  VmGameState game{&packs, &world, {}, {}};
  arena.gameState = &game;

  TufaClass expression;
  expression.constants.push_back({0, "Ljava.util.resource.Dummy;", 0, 0});
  const std::vector<TufaNode> allocate{
      {0x07, 0, true, 30}, {0x06, 0, true, 0}, {0x2b, 0, false, 0}};
  const auto allocateDummy = [&]() -> const VmObject * {
    const auto made =
        vmExecute(expression, allocate, {}, nullptr, &loaded, &states, &arena);
    const auto *object = std::get_if<const VmObject *>(&made.value);
    return made.ok && object ? *object : nullptr;
  };
  const TufaMethod *withFlags = findConstructor(*dummy, "(I)");
  CHECK(withFlags);
  if (!withFlags || !root)
    return 1;
  const auto construct = [&](const VmObject *object, int32_t flags) {
    return vmExecute(*dummy, dummy->trees[withFlags->words[3]], {object, flags},
                     &nativeRegistry(), &loaded, &states, &arena);
  };

  uint32_t dummyScriptId = 0;
  CHECK(packs.resolveLiteral("system.rpk", 0x1b, dummyScriptId, resolveError));

  // Dummy.<init>(I) through the real script chain: ResourceRef.newNative,
  // GameType.createNativeInstance with (null, RID_DUMMY, null, "dummy"), then
  // GameRef.setFlags(flags).
  const VmObject *plain = allocateDummy();
  CHECK(plain);
  // The stock Vector implementation uses postfix increment to append at
  // elementCount before incrementing. Exercise its real script methods.
  {
    vm_test::World builder;
    const auto descriptor = builder.main.t("Ljava.util.Vector;");
    const std::vector<TufaNode> vectorAllocation{
        vm_test::node(0x07, true, 30), vm_test::node(0x06, true, descriptor),
        vm_test::ret};
    const auto allocated =
        vmExecute(builder.main.c, vectorAllocation, {}, &nativeRegistry(),
                  &loaded, &states, &arena);
    const VmObject *vector = vm_test::asObject(allocated.value);
    const TufaMethod *ctor = findConstructor(*vectorClass, "()");
    const TufaMethod *add = nullptr, *at = nullptr;
    for (const auto &list : vectorClass->methods)
      for (const auto &method : list) {
        const auto &words = method.words;
        if (vectorClass->constants[words[1]].text == "addElement" &&
            vectorClass->constants[words[2]].text == "(Ljava.lang.Object;)V")
          add = &method;
        if (vectorClass->constants[words[1]].text == "elementAt" &&
            vectorClass->constants[words[2]].text == "(I)Ljava.lang.Object;")
          at = &method;
      }
    CHECK(allocated.ok && vector && ctor && add && at);
    if (vector && ctor && add && at) {
      const auto builtVector =
          vmExecute(*vectorClass, vectorClass->trees[ctor->words[3]], {vector},
                    &nativeRegistry(), &loaded, &states, &arena);
      const auto appended = vmExecute(
          *vectorClass, vectorClass->trees[add->words[3]], {vector, plain},
          &nativeRegistry(), &loaded, &states, &arena);
      const auto first = vmExecute(
          *vectorClass, vectorClass->trees[at->words[3]], {vector, int32_t{0}},
          &nativeRegistry(), &loaded, &states, &arena);
      CHECK(builtVector.ok && appended.ok && first.ok &&
            vm_test::asObject(first.value) == plain);
      if (!builtVector.ok || !appended.ok || !first.ok)
        std::printf("Vector regression: %s / %s / %s\n",
                    builtVector.error.c_str(), appended.error.c_str(),
                    first.error.c_str());
    }
  }
  const auto built = construct(plain, 0);
  CHECK(built.ok);
  if (!built.ok)
    std::printf("constructor error: %s\n", built.error.c_str());
  CHECK(world.createdInstances() == 1 && world.children(*root) &&
        world.children(*root)->size() == 1);
  WorldNode *node =
      world.children(*root)->empty() ? nullptr : world.children(*root)->front();
  CHECK(node && node->name == "dummy" && node->type == 1 &&
        node->flags == 0x221 && node->loaded);
  CHECK(node && node->sectorLink.owner == node && node->hasReceiver);
  ScriptNode *script = world.findScript(dummyScriptId);
  CHECK(script && script->className == "java.util.resource.Dummy" && node &&
        node->scriptLink.owner == script);
  const VmResourceState *plainState = nativeResourceState(plain, &arena);
  CHECK(plainState && node && plainState->words[2] == node->instanceId &&
        (node->instanceId >> 16) == 0xffff);
  // The receiver's own script `getInfo(I)I` ran for the three record kinds;
  // GameType.getInfo returns 0.
  CHECK((game.getInfoLog ==
         std::vector<std::pair<int32_t, int32_t>>{{8, 0}, {9, 0}, {0x1c, 0}}));
  CHECK(game.bindings.size() == 1 && node &&
        node->head == &game.bindings.begin()->second->link);

  // GameRef.WORLDTREEROOT (0x10) as the flags: the node is already its own
  // sector root (it was created under the type-0 root), so FUN_00544f40 changes
  // nothing, and the flag word still gains 0x10.
  const VmObject *treeRoot = allocateDummy();
  CHECK(treeRoot && construct(treeRoot, 0x10).ok);
  CHECK(world.children(*root)->size() == 2);
  WorldNode *rootNode = world.children(*root)->size() == 2
                            ? (*world.children(*root))[1]
                            : nullptr;
  CHECK(rootNode && rootNode->flags == 0x231 &&
        rootNode->sectorLink.owner == rootNode &&
        rootNode->sectorLink.cachedValue == rootNode->instanceId);
  CHECK(game.getInfoLog.size() == 6);

  // Flags accumulate, and setFlags without a node is a no-op.
  const auto errorOf = [](const NativeResult &result) {
    const auto *text = std::get_if<std::string>(&result);
    return text ? *text : std::string();
  };
  CHECK(std::holds_alternative<std::monostate>(
      invokeNative("java.util.resource.GameRef", "setFlags", "(I)V",
                   {VmValue{int32_t{0x4}}}, plain, &arena, &loaded)));
  CHECK(node && node->flags == 0x225);
  CHECK(std::holds_alternative<std::monostate>(
      invokeNative("java.util.resource.GameRef", "clearFlags", "(I)V",
                   {VmValue{int32_t{0x204}}}, plain, &arena, &loaded)));
  CHECK(node && node->flags == 0x21);
  CHECK(std::holds_alternative<std::monostate>(
      invokeNative("java.lang.GameType", "setEventMask", "(I)V",
                   {VmValue{int32_t{0x12}}}, plain, &arena, &loaded)));
  CHECK(node && node->eventMask == 0x12);
  CHECK(std::holds_alternative<std::monostate>(
      invokeNative("java.lang.GameType", "setEventMask", "(I)V",
                   {VmValue{int32_t{0x21}}}, plain, &arena, &loaded)));
  CHECK(node && node->eventMask == 0x33);
  CHECK(std::holds_alternative<std::monostate>(
      invokeNative("java.lang.GameType", "clearEventMask", "(I)V",
                   {VmValue{int32_t{0x11}}}, plain, &arena, &loaded)));
  CHECK(node && node->eventMask == 0x22);
  CHECK(std::holds_alternative<std::monostate>(
      invokeNative("java.lang.GameType", "setEventMask", "(I)V",
                   {VmValue{int32_t{0x11}}}, plain, &arena, &loaded)));
  const VmObject *unborn = allocateDummy();
  CHECK(unborn &&
        !errorOf(invokeNative("java.util.resource.GameRef", "setFlags", "(I)V",
                              {VmValue{int32_t{1}}}, unborn, &arena, &loaded))
             .empty());

  // Error cases at the handler.
  const auto string = [&](const std::string &text) {
    auto object = std::make_unique<VmObject>();
    object->runtimeClass = stringClass;
    arena.objects.push_back(std::move(object));
    arena.strings.emplace_back(text, arena.objects.back().get());
    return arena.objects.back().get();
  };
  const VmObject *null = nullptr;
  {
    PackRegistry packs("ref/raw");
    GameWorld world;
    auto *root = world.addRoot();
    ResourceWorld cameraResources(world, packs);
    VmGameState game{&packs, &world, {}, {}};
    VmArena arena;
    arena.gameState = &game;
    game.resources = &cameraResources;
    const auto allocateCameraRef = [&]() -> const VmObject * {
      const auto made = vmExecute(expression, allocate, {}, nullptr, &loaded,
                                  &states, &arena);
      const auto *object = std::get_if<const VmObject *>(&made.value);
      return made.ok && object ? *object : nullptr;
    };
    const auto cameraString = [&](const std::string &text) {
      auto object = std::make_unique<VmObject>();
      object->runtimeClass = stringClass;
      arena.objects.push_back(std::move(object));
      arena.strings.emplace_back(text, arena.objects.back().get());
      return arena.objects.back().get();
    };
    auto *cameraNode = world.addChild(*root, "camera command fixture", 0x21);
    const auto *cameraRef = allocateCameraRef();
    CHECK(cameraNode && cameraRef);
    if (cameraNode && cameraRef) {
      world.markLoaded(*cameraNode);
      cameraNode->nativeDevice = "camera";
      cameraNode->eventSubscriptionsKnownEmpty = true;
      cameraNode->eventMask = 0x14;
      std::string cameraError;
      CameraDeviceConfig cameraConfig{55,   0.02f, 2,  1,   -0.7f,
                                      0.2f, 200,   20, true};
      auto prototype = makeCameraDevicePrototype(cameraError);
      cameraNode->cameraDevice = prepareCameraDeviceResources(
          world, cameraResources, *cameraNode, cameraConfig, {}, prototype,
          cameraError);
      CHECK(cameraNode->cameraDevice);
      CHECK(std::holds_alternative<std::monostate>(
          invokeNative("java.util.resource.ResourceRef", "newNative", "()V", {},
                       cameraRef, &arena, &loaded)));
      const auto *refState = nativeResourceState(cameraRef, &arena);
      CHECK(refState);
      if (cameraNode->cameraDevice && refState) {
        const auto handle =
            static_cast<int32_t>(refState - arena.resourceStates.data()) + 1;
        auto &binding = game.bindings[handle];
        if (!binding)
          binding = std::make_unique<ReceiverBinding>();
        setLinkOwner(binding->link, cameraNode);
        const auto command = [&](int32_t event, const char *text) {
          return gameRefQueueEvent(
              {VmValue{null}, VmValue{event}, VmValue{cameraString(text)}},
              cameraRef, &arena, &loaded);
        };
        auto &state = cameraNode->cameraDevice->commands;
        CHECK(
            std::holds_alternative<std::monostate>(command(0x10, "dist 3 8")));
        CHECK(state.minDistance == 3 && state.maxDistance == 8);
        CHECK(std::holds_alternative<std::monostate>(command(0x10, "dist 4")));
        CHECK(state.minDistance == 4 && state.maxDistance == 8);
        CHECK(std::holds_alternative<std::monostate>(
            command(0x10, "smooth 0.2 0.3")));
        CHECK(std::fabs(state.smoothPosition - 0.2f) < 1e-6f);
        CHECK(std::holds_alternative<std::monostate>(
            command(0x10, "force 1.6 0.5 -0.7")));
        CHECK(std::fabs(state.force - 1.6f) < 1e-6f && state.damping == 0.5f);
        CHECK(std::holds_alternative<std::monostate>(
            command(0x10, "torque 0.1")));
        CHECK(std::holds_alternative<std::monostate>(command(0x10, "roll 0")));
        auto *followTarget =
            world.addTypedChild(*root, 2, "camera follow target", 0);
        CHECK(followTarget);
        if (followTarget) {
          const std::string id =
              std::to_string(static_cast<int32_t>(followTarget->instanceId));
          const auto look = "look " + id + " 1,2,3 0,0,-2 -1";
          const auto move = "move " + id + " 4,5,6 12 0,0,0";
          CHECK(std::holds_alternative<std::monostate>(
              command(0x10, look.c_str())));
          CHECK(cameraNode->cameraDevice->lookTarget.owner == followTarget);
          CHECK(state.lookOffset == (std::array<float, 3>{1, 2, 3}));
          CHECK(errorOf(command(0x10, move.c_str()))
                    .find("follow simulation unsupported") !=
                std::string::npos);
          CHECK(cameraNode->cameraDevice->moveTarget.owner == followTarget);
          CHECK(state.moveDistance == 12 && state.maxDistance == 12);
          CHECK(world.destroy(*followTarget));
          CHECK(!cameraNode->cameraDevice->lookTarget.owner &&
                !cameraNode->cameraDevice->moveTarget.owner);
        }
        auto *bodiedTarget =
            world.addTypedChild(*root, 2, "bodied camera follow", 0);
        std::string bodyError;
        auto builtTarget =
            physics::createBodyProto(1, 1, 1, 0, 0, nullptr, bodyError);
        auto bodyPrototype =
            builtTarget
                ? std::make_shared<physics::BodyProto>(std::move(*builtTarget))
                : nullptr;
        auto body = bodyPrototype
                        ? std::make_shared<physics::Body>(
                              physics::createBody(*bodyPrototype, {10, 0, 0}))
                        : nullptr;
        CHECK(bodiedTarget && body &&
              world.registerDynamicBody(*bodiedTarget, body, bodyPrototype));
        if (bodiedTarget && body) {
          const auto id =
              std::to_string(static_cast<int32_t>(bodiedTarget->instanceId));
          CHECK(std::holds_alternative<std::monostate>(
              command(0x10, ("look " + id + " 0,0,0 0,0,-1 -1").c_str())));
          CHECK(std::holds_alternative<std::monostate>(
              command(0x10, ("move " + id + " 0,1,0 2 0,0,0").c_str())));
          CHECK(cameraNode->cameraDevice->moveTarget.owner == bodiedTarget);
          CHECK(world.destroy(*bodiedTarget));
          CHECK(!cameraNode->cameraDevice->lookTarget.owner &&
                !cameraNode->cameraDevice->moveTarget.owner);
        }
        CHECK(!errorOf(command(0x10, "render 1")).empty());
        auto surface =
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
                SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32),
                SDL_DestroySurface);
        auto renderer = surface
                            ? Render2D::forSurface(surface.get(), cameraError)
                            : nullptr;
        CHECK(renderer);
        if (renderer) {
          game.renderer = renderer.get();
          CHECK(errorOf(command(0x10, "render 1 0 0 1 0"))
                    .find("fog state unavailable") != std::string::npos);
          CHECK(!cameraNode->cameraDevice->renderNode()->cameraViewport.owner);
          game.renderer = nullptr;
        }
        const float minimumBeforeIgnoredEvents = state.minDistance;
        CHECK(
            std::holds_alternative<std::monostate>(command(0x80, "dist 9 9")));
        CHECK(state.minDistance == minimumBeforeIgnoredEvents);
        cameraNode->eventMask &= ~uint32_t{0x10};
        CHECK(
            std::holds_alternative<std::monostate>(command(0x10, "dist 9 9")));
        CHECK(state.minDistance == minimumBeforeIgnoredEvents);
        cameraNode->eventMask |= 0x10;
        auto *seatCar = world.addTypedChild(*root, 2, "seat follow car", 0);
        CHECK(seatCar);
        if (seatCar) {
          CarDriver driver;
          driver.hasExtCamera = true;
          driver.extCamera = {120, 0, 1.8f, 0, 0, 1, -2, 0, 0, 0, 5};
          CHECK(cameraNode->cameraDevice->applySeatFollow(*seatCar, driver,
                                                          {0.4f, 1.25f}));
          CHECK(cameraNode->cameraDevice->lookTarget.owner == seatCar &&
                cameraNode->cameraDevice->moveTarget.owner == seatCar);
          CHECK(state.lookOffset == (std::array<float, 3>{0, 1.8f, 0}) &&
                state.moveDistance == 2 && state.zoomTarget == 120);
          CHECK(world.destroy(*seatCar));
          CHECK(!cameraNode->cameraDevice->lookTarget.owner &&
                !cameraNode->cameraDevice->moveTarget.owner);
        }
        auto *playerNode =
            world.addChild(*root, "player command fixture", 0x21);
        auto *viewportA = world.addTypedChild(*root, 18, "viewport A", 0);
        auto *viewportB = world.addTypedChild(*root, 18, "viewport B", 0);
        const auto *playerRef = allocateCameraRef();
        CHECK(playerNode && viewportA && viewportB && playerRef);
        if (playerNode && viewportA && viewportB && playerRef) {
          world.markLoaded(*playerNode);
          playerNode->nativeDevice = "player";
          playerNode->playerInput = std::make_unique<PlayerInputState>();
          playerNode->player = std::make_unique<PlayerInstance>();
          playerNode->scriptReceiver = playerRef;
          playerNode->eventMask = 0x10;
          playerNode->eventSubscriptionsKnownEmpty = true;
          CHECK(std::holds_alternative<std::monostate>(
              invokeNative("java.util.resource.ResourceRef", "newNative", "()V",
                           {}, playerRef, &arena, &loaded)));
          const auto *playerState = nativeResourceState(playerRef, &arena);
          CHECK(playerState);
          if (playerState) {
            const auto playerHandle =
                static_cast<int32_t>(playerState -
                                     arena.resourceStates.data()) +
                1;
            auto &playerBinding = game.bindings[playerHandle];
            if (!playerBinding)
              playerBinding = std::make_unique<ReceiverBinding>();
            setLinkOwner(playerBinding->link, playerNode);
            const auto sendPlayer = [&](const std::string &text) {
              return gameRefQueueEvent({VmValue{null}, VmValue{int32_t{0x10}},
                                        VmValue{cameraString(text)}},
                                       playerRef, &arena, &loaded);
            };
            const auto idText = [](uint32_t id) {
              int32_t signedId;
              std::memcpy(&signedId, &id, sizeof id);
              return std::to_string(signedId);
            };
            const auto playerOsd = [&]() {
              const auto result = gameRefGetInfoIntInt(
                  {int32_t{0x1a}, int32_t{0}}, playerRef, &arena, &loaded);
              const auto *value = std::get_if<VmValue>(&result);
              const auto *id = value ? std::get_if<int32_t>(value) : nullptr;
              return id ? *id : int32_t{-1};
            };
            float controlKey = 1.0f;
            playerNode->playerInput->sampleInput = [&](int32_t, int32_t) {
              return std::optional<float>{controlKey};
            };
            playerNode->playerInput->inputType = [](int32_t, int32_t) {
              return std::optional<int32_t>{0};
            };
            CHECK(playerNode->playerInput->add(1, 0, 0xcb, 0, {0, 1, 0, -1},
                                               0) == 1);
            playerNode->playerInput->beginFrame(0.00025f);
            const auto integerAxis = [&]() {
              const auto result = gameRefGetInfoIntInt(
                  {int32_t{0x19}, int32_t{1}}, playerRef, &arena, &loaded);
              const auto *value = std::get_if<VmValue>(&result);
              const auto *id = value ? std::get_if<int32_t>(value) : nullptr;
              return id ? *id : int32_t{999};
            };
            CHECK(integerAxis() == -31);
            controlKey = 0;
            CHECK(integerAxis() == -31);
            playerNode->playerInput->beginFrame(0.00025f);
            CHECK(integerAxis() == 0);
            CHECK(playerOsd() == 0);
            CHECK(std::holds_alternative<std::monostate>(
                sendPlayer("renderinstance " + idText(viewportA->instanceId))));
            CHECK(playerNode->player->renderInstance.owner == viewportA &&
                  playerOsd() == 0);
            CHECK(std::holds_alternative<std::monostate>(
                sendPlayer("renderinstance " + idText(viewportB->instanceId))));
            CHECK(playerNode->player->renderInstance.owner == viewportB);
            CHECK(errorOf(sendPlayer("osd " + idText(viewportA->instanceId)))
                      .find("controllable unavailable") != std::string::npos);
            CHECK(playerOsd() == 0 &&
                  playerNode->player->renderInstance.owner == viewportB);
            setLinkOwner(playerNode->player->osd, viewportA);
            CHECK(playerOsd() == static_cast<int32_t>(viewportA->instanceId));
            CHECK(std::holds_alternative<std::monostate>(
                sendPlayer("renderinstance 0")));
            CHECK(!playerNode->player->renderInstance.owner &&
                  playerOsd() == static_cast<int32_t>(viewportA->instanceId));
            CHECK(std::holds_alternative<std::monostate>(
                sendPlayer("renderinstance " + idText(viewportB->instanceId))));
            CHECK(std::holds_alternative<std::monostate>(
                sendPlayer("viewport " + idText(viewportA->instanceId))));
            CHECK(playerNode->player->viewport.owner == viewportA);
            CHECK(std::holds_alternative<std::monostate>(
                sendPlayer("viewport " + idText(viewportB->instanceId))));
            CHECK(playerNode->player->viewport.owner == viewportB);
            CHECK(std::holds_alternative<std::monostate>(
                sendPlayer("camera " + idText(cameraNode->instanceId))));
            CHECK(playerNode->player->camera.owner == cameraNode &&
                  cameraNode->cameraDevice->controller.owner == playerNode);
            CHECK(std::holds_alternative<std::monostate>(
                sendPlayer("camera " + idText(cameraNode->instanceId))));
            setLinkOwner(playerNode->player->controllable, cameraNode);
            CHECK(
                !errorOf(sendPlayer("camera " + idText(cameraNode->instanceId)))
                     .empty());
            setLinkOwner(playerNode->player->controllable, nullptr);
            auto *carNode =
                world.addChild(*root, "activation car fixture", 0x21);
            auto *carRoot =
                world.addTypedChild(*root, 2, "activation car root", 0);
            CHECK(carNode && carRoot);
            if (carNode && carRoot) {
              carNode->nativeDevice = "car";
              carNode->carInstance = std::make_unique<CarRuntimeInstance>();
              setLinkOwner(carNode->carInstance->physicsRoot, carRoot);
              carNode->carInstance->seats.push_back(
                  std::make_unique<CarSeat>());
              setLinkOwner(carNode->carInstance->seats[0]->player, playerNode);
              auto *carScript = world.addScript(0x151, "fixture.ActivationCar");
              CHECK(carScript);
              if (!carScript)
                return 1;
              carScript->car = std::make_unique<CarPrototype>();
              CarDriver driver;
              driver.hasExtCamera = true;
              driver.extCamera = {120, 0, 1.8f, 0, 0, 1, -2, 0, 0, 0, 5};
              carScript->car->config.drivers.push_back(driver);
              carScript->car->cameraSettings = CarCameraSettings{0.4f, 1.25f};
              setLinkOwner(carNode->carInstance->prototypeSource, carScript);
              auto targetProtoData =
                  physics::createBodyProto(1, 1, 1, 0, 0, nullptr, cameraError);
              auto targetProto = targetProtoData
                                     ? std::make_shared<physics::BodyProto>(
                                           std::move(*targetProtoData))
                                     : nullptr;
              auto targetBody =
                  targetProto
                      ? std::make_shared<physics::Body>(
                            physics::createBody(*targetProto, {10, 0, 0}))
                      : nullptr;
              CHECK(targetBody && world.registerDynamicBody(
                                      *carRoot, targetBody, targetProto));
              setLinkOwner(playerNode->player->controllable, carNode);
              CHECK(std::holds_alternative<std::monostate>(
                  sendPlayer("camera " + idText(cameraNode->instanceId))));
              CHECK(cameraNode->cameraDevice->lookTarget.owner == carRoot);
              CHECK(cameraNode->cameraDevice->moveTarget.owner == carRoot);
              CHECK(state.moveDistance == 2);
              CHECK(world.destroy(*carRoot));
              CHECK(!cameraNode->cameraDevice->lookTarget.owner &&
                    !cameraNode->cameraDevice->moveTarget.owner);
              setLinkOwner(playerNode->player->controllable, nullptr);
              CHECK(world.destroy(*carNode));
            }
            CHECK(std::holds_alternative<std::monostate>(
                sendPlayer("osd " + idText(viewportA->instanceId))));
            CHECK(playerNode->player->osd.owner == viewportA);
            auto *osdNode = world.addChild(*root, "player osd", 0x21);
            auto *osdCar = world.addChild(*root, "player osd car", 0x21);
            const auto *osdCarRef = allocateCameraRef();
            CHECK(osdNode && osdCar && osdCarRef);
            if (osdNode && osdCar && osdCarRef) {
              osdCar->nativeDevice = "car";
              osdCar->carInstance = std::make_unique<CarRuntimeInstance>();
              osdCar->carInstance->isCar = true;
              osdCar->eventMask = 0x10;
              osdCar->eventSubscriptionsKnownEmpty = true;
              world.markLoaded(*osdCar);
              CHECK(std::holds_alternative<std::monostate>(
                  invokeNative("java.util.resource.ResourceRef", "newNative",
                               "()V", {}, osdCarRef, &arena, &loaded)));
              const auto *carState = nativeResourceState(osdCarRef, &arena);
              CHECK(carState);
              if (carState) {
                const auto carHandle =
                    static_cast<int32_t>(carState -
                                         arena.resourceStates.data()) +
                    1;
                auto &carBinding = game.bindings[carHandle];
                if (!carBinding)
                  carBinding = std::make_unique<ReceiverBinding>();
                setLinkOwner(carBinding->link, osdCar);
                osdCar->scriptReceiver = osdCarRef;
                setLinkOwner(playerNode->player->controllable, osdCar);
                const auto osdError =
                    errorOf(sendPlayer("osd " + idText(osdNode->instanceId)));
                CHECK(playerOsd() == static_cast<int32_t>(osdNode->instanceId));
                CHECK(playerNode->player->osd.owner == osdNode);
                CHECK(osdError.find("car activate seat unavailable") !=
                      std::string::npos);
                CHECK(std::holds_alternative<std::monostate>(
                    sendPlayer("osd " + idText(osdNode->instanceId))));
                CHECK(playerNode->player->osd.owner == osdNode);
                setLinkOwner(playerNode->player->controllable, nullptr);
              }
              CHECK(world.destroy(*osdCar));
              CHECK(world.destroy(*osdNode));
            }
            setLinkOwner(playerNode->player->osd, viewportB);
            CHECK(world.destroy(*viewportB) &&
                  !playerNode->player->viewport.owner &&
                  !playerNode->player->renderInstance.owner);
            CHECK(playerOsd() == 0);
            CHECK(world.destroy(*playerNode) &&
                  !cameraNode->cameraDevice->controller.owner);
            CHECK(!playerBinding->link.owner);
          }
          CHECK(world.destroy(*viewportA));
        }
        CHECK(world.destroy(*cameraNode));
        CHECK(!binding->link.owner);
        CHECK(world.dynamicBodyCount() == 0);
      } else if (cameraNode)
        world.destroy(*cameraNode);
    }
  }
  CHECK(errorOf(
            inputGetDeviceName({VmValue{int32_t{0}}}, nullptr, &arena, &loaded))
            .find("platform device enumeration unavailable") !=
        std::string::npos);
  world.setInputDevices({"SysKeyboard", "SysMouse"});
  const auto device =
      inputGetDeviceName({VmValue{int32_t{0}}}, nullptr, &arena, &loaded);
  const auto *deviceValue = std::get_if<VmValue>(&device);
  CHECK(deviceValue && std::get<const VmObject *>(*deviceValue) &&
        arena.strings.back().first == "SysKeyboard");
  const auto missingDevice =
      inputGetDeviceName({VmValue{int32_t{2}}}, nullptr, &arena, &loaded);
  const auto *missingValue = std::get_if<VmValue>(&missingDevice);
  CHECK(missingValue && !std::get<const VmObject *>(*missingValue));
  const auto queue = [&](const VmObject *receiver, int32_t event) {
    return invokeNative(
        "java.util.resource.GameRef", "queueEvent",
        "(Ljava.util.resource.ResourceRef;ILjava.lang.String;)V",
        {VmValue{null}, VmValue{event}, VmValue{null}}, receiver, &arena,
        &loaded);
  };
  CHECK(errorOf(queue(unborn, 0x10)).find("no native link") !=
        std::string::npos);
  const VmObject *unlinked = allocateDummy();
  CHECK(unlinked);
  CHECK(std::holds_alternative<std::monostate>(
      invokeNative("java.util.resource.ResourceRef", "newNative", "()V", {},
                   unlinked, &arena, &loaded)));
  CHECK(std::holds_alternative<std::monostate>(queue(unlinked, 0x10)));
  CHECK(std::holds_alternative<std::monostate>(queue(plain, 0x40)));
  CHECK(errorOf(queue(plain, 0x10)).find("event delivery unsupported") !=
        std::string::npos);
  const auto reset = [&](const VmObject *object) {
    return invokeNative("java.io.Controller", "user_Reset", "()V", {}, object,
                        &arena, &loaded);
  };
  CHECK(std::holds_alternative<std::monostate>(reset(unlinked)));
  CHECK(errorOf(reset(plain)).find("player device unsupported") !=
        std::string::npos);
  node->playerInput = std::make_unique<PlayerInputState>();
  const std::vector<VmValue> addArgs{VmValue{int32_t{2}}, VmValue{int32_t{1}},
                                     VmValue{int32_t{3}}, VmValue{-1.f},
                                     VmValue{1.f},        VmValue{0.f},
                                     VmValue{1.f}};
  const auto add = [&]() {
    return invokeNative("java.io.Controller", "user_Add", "(IIIFFFF)I", addArgs,
                        plain, &arena, &loaded);
  };
  CHECK(errorOf(add()).find("physical input unavailable") != std::string::npos);
  auto invalidDevice = addArgs;
  invalidDevice[1] = int32_t{-1};
  const auto invalidAdd =
      invokeNative("java.io.Controller", "user_Add", "(IIIFFFF)I",
                   invalidDevice, plain, &arena, &loaded);
  const auto *invalidValue = std::get_if<VmValue>(&invalidAdd);
  CHECK(invalidValue && std::get<int32_t>(*invalidValue) == -1);
  node->playerInput->sampleInput = [](int32_t device,
                                      int32_t axis) -> std::optional<float> {
    return device == 1 && axis == 3 ? std::optional<float>{0.5f} : std::nullopt;
  };
  node->playerInput->inputType = [](int32_t device,
                                    int32_t axis) -> std::optional<int32_t> {
    return device == 1 && axis == 3 ? std::optional<int32_t>{7} : std::nullopt;
  };
  const auto added = add();
  const auto *addedValue = std::get_if<VmValue>(&added);
  CHECK(addedValue && std::get<int32_t>(*addedValue) == 1 &&
        node->playerInput->mappings()[0].sample == 0.5f &&
        node->playerInput->axes()[2].deviceType == 7);
  const auto smooth =
      invokeNative("java.io.Controller", "user_SetAxisSmooth", "(IFFFFF)I",
                   {VmValue{int32_t{2}}, VmValue{0.2f}, VmValue{0.3f},
                    VmValue{0.4f}, VmValue{0.5f}, VmValue{0.6f}},
                   plain, &arena, &loaded);
  const auto *smoothValue = std::get_if<VmValue>(&smooth);
  CHECK(smoothValue && std::get<int32_t>(*smoothValue) == 1);
  CHECK(node->playerInput->axes()[2].threshold == 0.2f &&
        node->playerInput->axes()[2].factors[0] == 0.3f &&
        node->playerInput->axes()[2].factors[1] == 0.4f &&
        node->playerInput->axes()[2].factors[2] == 0.5f &&
        node->playerInput->axes()[2].factors[3] == 1.f &&
        node->playerInput->axes()[2].factors[4] == 0.6f);
  const auto deleted = invokeNative(
      "java.io.Controller", "user_Del", "(III)I",
      {VmValue{int32_t{2}}, VmValue{int32_t{1}}, VmValue{int32_t{3}}}, plain,
      &arena, &loaded);
  const auto *deleteValue = std::get_if<VmValue>(&deleted);
  CHECK(deleteValue && std::get<int32_t>(*deleteValue) == 0 &&
        node->playerInput->mappingCount() == 0);
  CHECK(node->playerInput->add(2, 1, 3, 0.5f, {-1.f, 1.f, 0.f, 1.f}, 7) == 1);
  CHECK(std::holds_alternative<std::monostate>(reset(plain)));
  CHECK(node->playerInput->mappingCount() == 0 &&
        node->playerInput->axes()[2].deviceType == 0);
  node->playerInput.reset();
  node->nativeDevice = "player";
  CHECK(errorOf(queue(plain, 0x10)).find("native device unsupported: player") !=
        std::string::npos);
  node->playerInput = std::make_unique<PlayerInputState>();
  node->scriptReceiver = plain;
  node->eventSubscriptionsKnownEmpty = true;
  int calls = 0;
  arena.callScript = [&](const VmObject *target, const std::string &method,
                         std::vector<VmValue> args) -> VmResult {
    CHECK(target == plain && method == "setcontrol" && args.empty());
    ++calls;
    return {true, int32_t{0}, {}, true};
  };
  const auto command = [&](const std::string &text) {
    return invokeNative(
        "java.util.resource.GameRef", "queueEvent",
        "(Ljava.util.resource.ResourceRef;ILjava.lang.String;)V",
        {VmValue{null}, VmValue{int32_t{0x10}}, VmValue{string(text)}}, plain,
        &arena, &loaded);
  };
  CHECK(std::holds_alternative<std::monostate>(command("setcontrol")) &&
        calls == 1);
  CHECK(std::holds_alternative<std::monostate>(command("setcontrol extra")) &&
        calls == 2);
  arena.callScript = [](const VmObject *, const std::string &,
                        std::vector<VmValue>) -> VmResult {
    return {false, int32_t{0}, "control file unavailable"};
  };
  CHECK(errorOf(command("setcontrol")).find("control file unavailable") !=
        std::string::npos);
  // Exercise the callback through a running VM, not only a host stub. The
  // synthetic trigger is a native that sends the same command to the player;
  // the synthetic script callback writes an observable field.
  vm_test::World vmWorld;
  auto &playerClass = vmWorld.cls("test.Player");
  playerClass.instanceField("marker", "I")
      .method("trigger", "()V", 0x40, {})
      .method("setcontrol", "()V", 1,
              {vm_test::literal(99), vm_test::node(0x01, true, 0),
               vm_test::node(0x1c, true, playerClass.t("marker")),
               vm_test::node(0x08, true, 35), vm_test::retVoid});
  auto vmClasses = loaded;
  vmClasses.push_back(&playerClass.c);
  auto vmStates = states;
  vmStates.push_back({&playerClass.c, {}});
  auto *mutablePlain = const_cast<VmObject *>(plain);
  const TufaClass *savedClass = mutablePlain->runtimeClass;
  mutablePlain->runtimeClass = &playerClass.c;
  mutablePlain->fields.emplace_back("marker", int32_t{0}, "I");
  eventText = string("setcontrol");
  const std::vector<NativeEntry> triggerRegistry{
      {"test.Player", "trigger", "()V", "test", triggerPlayerCommand}};
  const auto triggerRef = vmWorld.main.ref("test.Player", "trigger", "()V");
  const std::vector<TufaNode> triggerTree{vm_test::literal(0),
                                          vm_test::node(0x24),
                                          vm_test::node(0x11, true, 2),
                                          vm_test::node(0x01, true, 0),
                                          vm_test::node(0x1a, true, triggerRef),
                                          vm_test::retVoid};
  arena.callScript = nullptr;
  const auto triggered =
      vmExecute(vmWorld.main.c, triggerTree, {plain}, &triggerRegistry,
                &vmClasses, &vmStates, &arena);
  if (!triggered.ok)
    std::printf("VM player event error: %s\n", triggered.error.c_str());
  CHECK(triggered.ok && vm_test::fieldOf(plain, "marker") &&
        std::get<int32_t>(vm_test::fieldOf(plain, "marker")->value) == 99);
  mutablePlain->fields.pop_back();
  mutablePlain->runtimeClass = savedClass;
  arena.callScript = [&](const VmObject *target, const std::string &method,
                         std::vector<VmValue> args) -> VmResult {
    CHECK(target == plain && method == "addDeviceMapping" && args.size() == 1 &&
          std::get<int32_t>(args[0]) == 3);
    ++calls;
    return {true, int32_t{0}, {}, true};
  };
  CHECK(std::holds_alternative<std::monostate>(command("device 3")) &&
        calls == 3);
  node->eventSubscriptionsKnownEmpty = false;
  CHECK(errorOf(command("setcontrol")).find("native device unsupported") !=
            std::string::npos &&
        calls == 3);
  arena.callScript = nullptr;
  node->playerInput.reset();
  node->eventSubscriptionsKnownEmpty = false;
  node->scriptReceiver = nullptr;
  node->nativeDevice.clear();
  const auto create = [&](const VmObject *receiver, int32_t id,
                          const VmObject *name,
                          const VmObject *parent = nullptr) {
    return invokeNative(
        "java.lang.GameType", "createNativeInstance",
        "(Ljava.util.resource.GameRef;ILjava.lang.String;Ljava.lang.String;)V",
        {VmValue{parent}, VmValue{id}, VmValue{null}, VmValue{name}}, receiver,
        &arena, &loaded);
  };
  const int32_t rawDummy = static_cast<int32_t>(dummyScriptId);
  const size_t nodesBefore = world.nodeCount();
  CHECK(errorOf(create(unborn, rawDummy, string("x")))
            .find("receiver has no native link") != std::string::npos);
  CHECK(errorOf(create(plain, rawDummy, string("x"), unborn))
            .find("unsupported parent without a native link") !=
        std::string::npos);
  CHECK(errorOf(create(plain, 0x7777, string("x"))).size() > 0);
  CHECK(world.nodeCount() == nodesBefore);
  VmArena bare;
  CHECK(errorOf(invokeNative(
            "java.lang.GameType", "createNativeInstance",
            "(Ljava.util.resource.GameRef;ILjava.lang.String;Ljava.lang.String;"
            ")V",
            {VmValue{null}, VmValue{rawDummy}, VmValue{null}, VmValue{null}},
            plain, &bare, &loaded)) ==
        "GameType.createNativeInstance: no game world state");

  // Running the one-GameRef constructor again on the same receiver reuses its
  // binding and moves the receiver link.
  const TufaMethod *withParent =
      findConstructor(*dummy, "(Ljava.util.resource.GameRef;)");
  CHECK(withParent);
  if (withParent && rootNode) {
    // A parent GameRef whose node is its own sector root (the usual case): the
    // child is created under that node and copies its sector link (FUN_00544fe0
    // with a hooked type-1 parent) instead of starting its own.
    const VmObject *kid = allocateDummy();
    const size_t before = world.nodeCount();
    CHECK(kid &&
          vmExecute(*dummy, dummy->trees[withParent->words[3]], {kid, treeRoot},
                    &nativeRegistry(), &loaded, &states, &arena)
              .ok);
    CHECK(world.nodeCount() == before + 1 && world.children(*rootNode) &&
          world.children(*rootNode)->size() == 1);
    WorldNode *childNode =
        world.children(*rootNode) && !world.children(*rootNode)->empty()
            ? world.children(*rootNode)->front()
            : nullptr;
    CHECK(childNode && childNode->name == "dummy" &&
          childNode->flags == 0x221 && childNode->sectorLink.owner == rootNode);
    CHECK(childNode &&
          childNode->sectorLink.cachedValue == rootNode->instanceId);
    const auto *kidState = nativeResourceState(kid, &arena);
    CHECK(kidState && childNode && kidState->words[2] == childNode->instanceId);
    if (childNode)
      world.destroy(*childNode);
    CHECK(world.nodeCount() == before);
  }
  if (withParent) {
    const auto again =
        vmExecute(*dummy, dummy->trees[withParent->words[3]], {plain, null},
                  &nativeRegistry(), &loaded, &states, &arena);
    CHECK(again.ok);
    // The first node's list still holds its own sector link, but no longer the
    // receiver's link.
    bool receiverStillListed = false;
    for (const OwnerLink *at = node ? node->head : nullptr; at; at = at->next)
      if (at == &game.bindings.begin()->second->link)
        receiverStillListed = true;
    CHECK(game.bindings.size() == 3 && world.children(*root)->size() == 3 &&
          node && !receiverStillListed);
    const auto *moved = nativeResourceState(plain, &arena);
    WorldNode *latest = world.children(*root)->back();
    CHECK(moved && latest && moved->words[2] == latest->instanceId &&
          latest->head == &game.bindings.begin()->second->link);
  }
  // The zero-id handler creates one loaded type-8 node per receiver class.
  arena.callScript = [](const VmObject *, const std::string &,
                        std::vector<VmValue>) {
    return VmResult{true, int32_t{0}, {}};
  };
  const size_t beforeFallback = world.nodeCount();
  CHECK(std::holds_alternative<std::monostate>(create(plain, 0, null)));
  CHECK(world.nodeCount() == beforeFallback + 2);
  ScriptNode *fallback = world.classScript(dummy->name);
  CHECK(fallback && fallback->loaded && fallback->className == dummy->name);
  WorldNode *fallbackNode =
      fallback ? world.findInstance(fallback->resourceId) : nullptr;
  CHECK(fallbackNode && fallbackNode->type == 8 && fallbackNode->flags == 2 &&
        fallbackNode->name == "Ljava.util.resource.Dummy;" &&
        fallbackNode->loaded);
  WorldNode *made = world.children(*root)->back();
  CHECK(made && made->type == 1 && made->name == "Ljava.util.resource.Dummy;" &&
        made->scriptLink.owner == fallback && made->hasReceiver);
  CHECK(std::holds_alternative<std::monostate>(
      create(plain, 0, string("second"))));
  CHECK(world.nodeCount() == beforeFallback + 3 &&
        world.classScript(dummy->name) == fallback);
  CHECK(world.children(*root)->back()->name == "second");
  CHECK(std::holds_alternative<std::monostate>(create(plain, rawDummy, null)));
  CHECK(world.children(*root)->back()->name == "Ljava.util.resource.Dummy;");
  arena.callScript = nullptr;
  const auto callbacksBefore = world.hooks(GameHook::Kind1c).size();
  CHECK(std::holds_alternative<std::monostate>(
      gameTypeRegisterCallback({int32_t{28}}, plain, &arena, &loaded)));
  CHECK(world.hooks(GameHook::Kind1c).size() == callbacksBefore + 1);
  CHECK(std::holds_alternative<std::monostate>(
      gameTypeUnregisterCallback({int32_t{28}}, plain, &arena, &loaded)));
  CHECK(world.hooks(GameHook::Kind1c).size() == callbacksBefore + 1 &&
        world.hooks(GameHook::Kind1c).back() == nullptr);
  CHECK(std::holds_alternative<std::monostate>(
      gameTypeUnregisterCallbacks({}, plain, &arena, &loaded)));
  world.pruneDetachedHooks(GameHook::Kind1c);
  HotkeyState hotkeys;
  arena.hotkeys = &hotkeys;
  auto hotkeyObject = std::make_unique<VmObject>();
  hotkeyObject->fields.push_back({"state", int32_t{0}, "I"});
  const auto *hotkey = hotkeyObject.get();
  arena.objects.push_back(std::move(hotkeyObject));
  auto handlerObject = std::make_unique<VmObject>();
  const auto *handler = handlerObject.get();
  arena.objects.push_back(std::move(handlerObject));
  const auto *nullHotkey = static_cast<const VmObject *>(nullptr);
  CHECK(std::holds_alternative<std::monostate>(inputCreateHotkey(
      {int32_t{7}, int32_t{2}, hotkey, handler, nullHotkey, int32_t{3}},
      nullptr, &arena, &loaded)));
  CHECK(hotkeys.entries[0].flags == 2 && hotkeys.entries[0].key == 7 &&
        hotkeys.entries[0].hotkey == hotkey &&
        hotkeys.entries[0].handler == handler &&
        hotkeys.entries[0].eventFilter == 3);
  hotkeys.entries[0].initialized = 1;
  hotkeys.entries[0].state = 1;
  CHECK(std::holds_alternative<std::monostate>(
      inputFlushHotkeys({}, nullptr, &arena, &loaded)));
  CHECK(hotkeys.entries[0].initialized == 0 && hotkeys.entries[0].state == 1);
  CHECK(std::holds_alternative<std::monostate>(
      inputDeleteHotkey({hotkey}, nullptr, &arena, &loaded)));
  CHECK(hotkeys.entries[0].flags == 0);
  CHECK(std::holds_alternative<std::monostate>(inputCreateHotkey(
      {int32_t{9}, int32_t{1}, hotkey, handler, nullHotkey, int32_t{2}},
      nullptr, &arena, &loaded)));
  CHECK(hotkeys.entries[0].flags == 1 && hotkeys.entries[0].key == 9);
  hotkeys.entries = {};
  for (size_t i = 0; i < hotkeys.entries.size(); ++i)
    CHECK(std::holds_alternative<std::monostate>(
        inputCreateHotkey({static_cast<int32_t>(i), int32_t{2}, hotkey, handler,
                           nullHotkey, int32_t{3}},
                          nullptr, &arena, &loaded)));
  CHECK(hotkeys.entries.back().key == 511);
  CHECK(std::holds_alternative<std::monostate>(inputCreateHotkey(
      {int32_t{999}, int32_t{2}, hotkey, handler, nullHotkey, int32_t{3}},
      nullptr, &arena, &loaded)));
  CHECK(hotkeys.entries.back().key == 511);
  hotkeys.entries = {};
  auto *eventNode = world.addChild(*root, "hotkey handler", 0x21);
  CHECK(eventNode != nullptr);
  if (eventNode) {
    world.markLoaded(*eventNode);
    eventNode->eventMask = 0x100000;
    eventNode->eventSubscriptionsKnownEmpty = true;
    eventNode->scriptReceiver = handler;
    const int32_t h = static_cast<int32_t>(arena.resourceStates.size() + 1);
    handler->fields.push_back({"ptr", h, "I"});
    arena.resourceStates.push_back({handler, {}});
    game.bindings[h] = std::make_unique<ReceiverBinding>();
    setLinkOwner(game.bindings[h]->link, eventNode);
    CHECK(std::holds_alternative<std::monostate>(gameTypeAddNotification(
        {handler, int32_t{0x10000}, int32_t{0}, nullHotkey}, plain, &arena,
        &loaded)));
    CHECK(eventNode->eventSubscriptions.size() == 1 &&
          eventNode->eventSubscriptions[0]->target.owner ==
              game.bindings[1]->link.owner &&
          eventNode->eventSubscriptions[0]->mask == 0x10000);
    CHECK(std::holds_alternative<std::monostate>(gameTypeAddNotification(
        {handler, int32_t{0x10000}, int32_t{42}, nullHotkey}, plain, &arena,
        &loaded)));
    CHECK(eventNode->eventSubscriptions.size() == 1 &&
          eventNode->eventSubscriptions[0]->replacementMask == 42);
    // Stock 0047e050 takes the "Mighty ERROR" soft path and continues,
    // unregistered, when the GameRef's native handle is absent.
    CHECK(std::holds_alternative<std::monostate>(gameTypeAddNotification(
        {nullHotkey, int32_t{0x10000}, int32_t{0}, nullHotkey}, plain, &arena,
        &loaded)));
    CHECK(eventNode->eventSubscriptions.size() == 1);
    eventNode->loaded = false;
    auto unloadedResult = gameTypeAddNotification(
        {handler, int32_t{0x20000}, int32_t{0}, nullHotkey}, plain, &arena,
        &loaded);
    CHECK(std::holds_alternative<std::string>(unloadedResult));
    CHECK(eventNode->eventSubscriptions.size() == 1);
    eventNode->loaded = true;
    ResourceNode packedTarget;
    packedTarget.type = 1;
    setLinkOwner(game.bindings[h]->link, &packedTarget);
    auto packedResult = gameTypeAddNotification(
        {handler, int32_t{0x20000}, int32_t{0}, nullHotkey}, plain, &arena,
        &loaded);
    CHECK(std::holds_alternative<std::string>(packedResult));
    CHECK(eventNode->eventSubscriptions.size() == 1);
    std::unique_ptr<SpatialGroundFace> retainedGroundFace;
    WorldNode *retainedGroundBody = nullptr;
    {
      ResourceWorld resources(world, packs);
      game.resources = &resources;
      uint16_t citySlot = 0;
      std::string error;
      CHECK(packs.registerPackName("maps\\city.rpk", citySlot, error));
      const auto city = resources.lookup((uint32_t(citySlot) << 16) | 1, 1);
      CHECK(city.node.packNode);
      if (city.node.packNode) {
        auto &node = *city.node.packNode;
        CHECK(node.parent && !node.parent->loaded());
        if (node.parent) {
          const bool parentLoaded =
              resources.loadEmptyGameParent(*node.parent, error);
          if (!parentLoaded)
            std::printf("city parent load error: %s\n", error.c_str());
          CHECK(parentLoaded && node.parent->loaded());
        }
        setLinkOwner(game.bindings[h]->link, &node);
        const size_t childrenBefore = node.children.size();
        node.flags |= 0x80000;
        auto failure = gameTypeAddNotification(
            {handler, int32_t{0x20000}, int32_t{0}, nullHotkey}, plain, &arena,
            &loaded);
        CHECK(std::holds_alternative<std::string>(failure));
        CHECK(std::get<std::string>(failure).find(
                  "type-1 instance construction unsupported") ==
              std::string::npos);
        CHECK(node.children.size() == childrenBefore && !node.loaded());
        node.flags &= ~uint32_t{0x80000};
        auto result = gameTypeAddNotification(
            {handler, int32_t{0x20000}, int32_t{0}, nullHotkey}, plain, &arena,
            &loaded);
        CHECK(std::holds_alternative<std::string>(result));
        CHECK(std::get<std::string>(result).find(
                  "type-1 instance construction unsupported: ground") !=
              std::string::npos);
        GameResourcePayload cityPayload;
        CHECK(resources.inspectGamePayload(node, cityPayload, error));
        CHECK(cityPayload.children.size() == 3);
        for (const auto &childInfo : cityPayload.children) {
          const uint32_t runtimeId = childInfo.runtimeId;
          CHECK(std::count_if(node.children.begin(), node.children.end(),
                              [&](const ResourceNode *child) {
                                return child->value == runtimeId &&
                                       child->parent == &node;
                              }) == 1);
        }
        CHECK(node.gameTypeLink.owner && !node.loaded());
        CHECK(node.eventSubscriptions.empty() && !node.groundFactory);
        CHECK(eventNode->eventSubscriptions.size() == 1);
        const size_t preparedCount = resources.nodeCount();
        const auto directFailure = loadGroundGameResource(arena, node);
        CHECK(directFailure &&
              directFailure->find("ground Config unavailable") !=
                  std::string::npos);
        CHECK(!node.loaded() && !node.groundFactory &&
              node.eventSubscriptions.empty() &&
              resources.nodeCount() == preparedCount);
        node.groundPreparation = std::make_shared<GroundPreparation>();
        const auto retry = gameTypeAddNotification(
            {handler, int32_t{0x20000}, int32_t{0}, nullHotkey}, plain, &arena,
            &loaded);
        CHECK(std::holds_alternative<std::string>(retry) &&
              std::get<std::string>(retry).find(
                  "ground reset required before retry") != std::string::npos);
        CHECK(node.groundPreparation &&
              resources.nodeCount() == preparedCount && !node.loaded());
        CHECK(node.eventSubscriptions.empty() && !node.groundFactory);
        node.groundPreparation.reset();
        WorldNode *configNode = world.addChild(*root, "config", 0x21);
        CHECK(configNode);
        if (configNode) {
          world.markLoaded(*configNode);
          configNode->scriptReceiver = plain;
          CHECK(world.attachConfig(*configNode));
          TufaClass configClass;
          configClass.name = "java.util.Config";
          configClass.hasClassHeader = true;
          vm_test::World probeWorld;
          auto &probeClass = probeWorld.cls("test.CityProbe");
          probeClass.method("load", "()V", 0x40, {});
          probeClass.method("send", "()V", 0x40, {});
          probeClass.method("city2", "()V", 0x40, {});
          auto probeClasses = loaded;
          probeClasses.push_back(&probeClass.c);
          probeClasses.push_back(&configClass);
          auto probeStates = states;
          probeStates.push_back({&probeClass.c, {}});
          probeStates.push_back({&configClass,
                                 {VmField{"gravity", 9.81f, "F"},
                                  VmField{"ground_friction", 1.0f, "F"}}});
          auto *mutablePlain = const_cast<VmObject *>(plain);
          const TufaClass *savedClass = mutablePlain->runtimeClass;
          mutablePlain->runtimeClass = &probeClass.c;
          const std::vector<NativeEntry> probeRegistry{
              {"test.CityProbe", "load", "()V", "test", probeCityNotification},
              {"test.CityProbe", "city2", "()V", "test", probeCity2Ground},
              {"test.CityProbe", "send", "()V", "test", probeGroundEvent}};
          cityLoadArgs = {handler, int32_t{0x20000}, int32_t{0}, nullHotkey};
          const auto loadRef =
              probeWorld.main.ref("test.CityProbe", "load", "()V");
          const std::vector<TufaNode> loadTree{
              vm_test::literal(0),
              vm_test::node(0x24),
              vm_test::node(0x11, true, 2),
              vm_test::node(0x01, true, 0),
              vm_test::node(0x1a, true, loadRef),
              vm_test::retVoid};
          const auto loadedCity =
              vmExecute(probeWorld.main.c, loadTree, {plain}, &probeRegistry,
                        &probeClasses, &probeStates, &arena);
          if (!loadedCity.ok)
            std::printf("city factory error: %s\n", loadedCity.error.c_str());
          const auto &factory = node.groundFactory;
          CHECK(loadedCity.ok && node.loaded() && factory &&
                factory->preparation && factory->roadWorld == &world &&
                factory->city.owner == &node &&
                factory->preparation->roadInitial &&
                !factory->preparation->roadInitial->pathMask.empty() &&
                factory->preparation->roadInitial->pathMask.front() == 0);
          if (loadedCity.ok && node.loaded()) {
            uint16_t city2Slot = 0;
            CHECK(packs.registerPackName("maps\\city2.rpk", city2Slot, error));
            auto city2 = resources.lookup((uint32_t(city2Slot) << 16) | 1, 1);
            CHECK(city2.node.packNode && city2.node.packNode->parent == &node);
            if (city2.node.packNode) {
              city2LoadTarget = city2.node.packNode;
              // Inspect the actual fa06a object payload without preparing
              // city2's own children or mutating its factory link.
              PackView city2View;
              CHECK(packs.view(city2Slot, city2View, error) ==
                    PackViewStatus::Ok);
              if (city2View.bytes) {
                const std::string marker =
                    "gametype 0x00020186\r\nparams "
                    "-172.736,10.208,755.170,-2.414,0.000,0.000\r\n";
                const auto found = std::search(city2View.bytes->begin(),
                                               city2View.bytes->end(),
                                               marker.begin(), marker.end());
                CHECK(found != city2View.bytes->end() &&
                      found - city2View.bytes->begin() >= 16);
                if (found != city2View.bytes->end() &&
                    found - city2View.bytes->begin() >= 16) {
                  const auto savedOffset = city2LoadTarget->payloadOffset;
                  const auto savedSize = city2LoadTarget->payloadSize;
                  city2LoadTarget->payloadOffset = static_cast<uint32_t>(
                      found - city2View.bytes->begin() - 16);
                  city2LoadTarget->payloadSize =
                      static_cast<uint32_t>(16 + marker.size());
                  const auto childrenBefore =
                      city2LoadTarget->registeredPayloadChildren;
                  const auto actionsBefore =
                      city2LoadTarget->preparedPayloadActions;
                  const auto nodeCountBefore = resources.nodeCount();
                  const auto stateBefore = city2LoadTarget->stateWord;
                  const auto objectError = loadGroundGameResource(
                      arena, *city2LoadTarget, &probeStates);
                  CHECK(objectError.has_value());
                  CHECK(!city2LoadTarget->loaded() &&
                        city2LoadTarget->stateWord == stateBefore &&
                        !city2LoadTarget->objectRuntime &&
                        resources.nodeCount() == nodeCountBefore &&
                        !city2LoadTarget->gameTypeLink.owner &&
                        city2LoadTarget->registeredPayloadChildren ==
                            childrenBefore &&
                        city2LoadTarget->preparedPayloadActions ==
                            actionsBefore);
                  city2LoadTarget->payloadOffset = savedOffset;
                  city2LoadTarget->payloadSize = savedSize;
                }
              }
              CameraTraversalParams traversal{{0, 0, 0}, 1, 0,     1,  1,
                                              0,         0, false, {}, {}};
              ResourceBounds cameraBound{};
              cameraBound.mode = 0;
              cameraBound.values = {0, 0, 0, 1.0e9f, 0, 0};
              traversal.cameraBound = cameraBound;
              CameraType1Records cameraRecords;
              SceneCameraCandidates cameraCandidates;
              CHECK(!traverseCameraCandidates(
                  world, *city2LoadTarget, cameraCandidates, traversal, error,
                  &cameraRecords, &resources));
              CHECK(error.find("factory requires VM context") !=
                        std::string::npos &&
                    !city2LoadTarget->loaded() &&
                    !city2LoadTarget->groundFactory);
              auto invalidStates = probeStates;
              for (auto &state : invalidStates)
                if (state.owner == &configClass)
                  state.fields[0].value =
                      std::numeric_limits<float>::quiet_NaN();
              const auto invalidConfig = loadGroundGameResource(
                  arena, *city2LoadTarget, &invalidStates);
              CHECK(invalidConfig &&
                    invalidConfig->find("ground Config unavailable") !=
                        std::string::npos &&
                    !city2LoadTarget->loaded() &&
                    !city2LoadTarget->groundFactory);
              // Preserve an unchanged inside root record so the test isolates
              // the factory path without loading its unrelated descendants.
              CHECK(!cameraRecords.beginVisit(*city2LoadTarget, true, false,
                                              true));
              cameraRecords.finishVisit(*city2LoadTarget, true);
              city2LoadTarget->flags &= ~uint32_t{0x40000};
              CHECK(traverseCameraCandidates(
                  world, *city2LoadTarget, cameraCandidates, traversal, error,
                  &cameraRecords, &resources, &arena, &probeStates));
              CHECK(city2LoadTarget->loaded() &&
                    city2LoadTarget->groundFactory);
              const auto city2Ref =
                  probeWorld.main.ref("test.CityProbe", "city2", "()V");
              const std::vector<TufaNode> city2Tree{
                  vm_test::literal(0),
                  vm_test::node(0x24),
                  vm_test::node(0x11, true, 2),
                  vm_test::node(0x01, true, 0),
                  vm_test::node(0x1a, true, city2Ref),
                  vm_test::retVoid};
              const auto result = vmExecute(
                  probeWorld.main.c, city2Tree, {plain}, &probeRegistry,
                  &probeClasses, &probeStates, &arena);
              if (!result.ok)
                std::printf("city2 factory error: %s\n", result.error.c_str());
              CHECK(result.ok && city2LoadTarget->loaded() &&
                    city2LoadTarget->groundFactory);
              if (result.ok) {
                ResourceNode *parent = city2LoadTarget;
                for (const uint32_t id :
                     {0xcba0u, 0xcba1u, 0xd692u, 0xdc2fu, 0xdc60u, 0xe249u,
                      0xe43du, 0xe43eu, 0xe530u, 0xe549u, 0xe55cu, 0xe55du,
                      0x98cdu, 0x98cfu, 0x98d8u}) {
                  const auto child =
                      resources.lookup((uint32_t(city2Slot) << 16) | id, 1);
                  CHECK(child.node.packNode &&
                        child.node.packNode->parent == parent);
                  if (!child.node.packNode ||
                      child.node.packNode->parent != parent)
                    break;
                  parent = child.node.packNode;
                  if (id == 0xcba0u) {
                    parent->flags |= 0x80000;
                    CHECK(!resources.loadPassiveSpatialGame(*parent, error));
                    CHECK(!parent->loaded() && !parent->spatialPayload);
                    parent->flags &= ~uint32_t{0x80000};
                    CHECK(
                        resources.registerGamePayloadChildren(*parent, error));
                    const auto next = resources.lookup(
                        (uint32_t(city2Slot) << 16) | 0xcba1u, 1);
                    CHECK(next.node.packNode);
                    if (next.node.packNode) {
                      CHECK(!resources.loadPassiveSpatialGame(
                          *next.node.packNode, error));
                      CHECK(!next.node.packNode->loaded() &&
                            !next.node.packNode->spatialPayload);
                    }
                  }
                  CHECK(resources.loadPassiveSpatialGame(*parent, error));
                  if (!parent->loaded())
                    break;
                  if (id == 0x98cdu) {
                    CHECK(parent->spatialPayload &&
                          parent->spatialPayload->polygons &&
                          parent->polygonCount && *parent->polygonCount > 0);
                    SceneCameraCandidates groundCandidates;
                    CameraType1Records groundRecords;
                    const auto before = resources.nodeCount();
                    CHECK(traverseCameraCandidates(
                        world, *parent, groundCandidates, traversal, error,
                        &groundRecords, &resources, &arena, &probeStates));
                    unsigned queued = 0;
                    groundCandidates.drain(0, true, [&](LinkOwner &owner) {
                      if (&owner == parent)
                        ++queued;
                      return 1u;
                    });
                    CHECK(queued == 1 && resources.nodeCount() == before);
                  }
                  if (id == 0x98d8u) {
                    CHECK(parent->spatialPayload &&
                          parent->spatialPayload->physics &&
                          parent->spatialPayload->physics->triangles.size() ==
                              7);
                    CHECK(resources.unloadPassiveSpatialGame(*parent, error));
                    CHECK(!parent->loaded() && !parent->spatialPayload);
                  }
                }
                const auto candidates = queryPassiveSpatialCandidates(
                    resources, *city2LoadTarget, {-278.518f, 9.8f, 1033.002f},
                    {-278.518f, 10.8f, 1033.002f});
                CHECK(!candidates.complete && !candidates.detail.empty());
                const auto leaf = std::find_if(
                    candidates.candidates.begin(), candidates.candidates.end(),
                    [city2Slot](const SpatialPhysicsCandidate &candidate) {
                      return candidate.resourceId ==
                             ((uint32_t(city2Slot) << 16) | 0x98d8u);
                    });
                CHECK(leaf != candidates.candidates.end());
                if (leaf != candidates.candidates.end()) {
                  if (leaf->node) {
                    const auto savedFlags = leaf->node->flags;
                    leaf->node->flags |= 0x200000u;
                    const auto excluded = queryPassiveSpatialCandidates(
                        resources, *city2LoadTarget,
                        {-278.518f, 9.8f, 1033.002f},
                        {-278.518f, 10.8f, 1033.002f});
                    CHECK(std::none_of(
                        excluded.candidates.begin(), excluded.candidates.end(),
                        [&](const SpatialPhysicsCandidate &candidate) {
                          return candidate.resourceId == leaf->resourceId;
                        }));
                    leaf->node->flags = savedFlags;
                  }
                  CHECK(leaf->payload && leaf->payload->physics &&
                        leaf->payload->physics->triangles.size() == 7);
                  if (leaf->payload && leaf->payload->physics)
                    CHECK(queryGamePhysicsFace(*leaf->payload->physics, 4,
                                               {-278.518f, 9.8f, 1033.002f},
                                               {-278.518f, 10.8f, 1033.002f})
                              .status == GamePhysicsHitStatus::Hit);
                  auto leafNode = resources.lookup(leaf->resourceId, 1);
                  std::unique_ptr<WheelGroundContact> wheelContact;
                  physics::SuspensionUpdate tireContact{};
                  TireStepState tireState{};
                  TireStepInput tireInput{};
                  CHECK(leafNode.node.packNode &&
                        leafNode.node.packNode->sectorLink.owner == &node &&
                        city2LoadTarget->sectorLink.owner == &node);
                  if (leaf->payload && leaf->payload->physics && factory &&
                      factory->preparation) {
                    // The leaf inherits the city sector. Stock carries that
                    // sector's ground body through city2, whose own factory has
                    // a different table.
                    const auto &ground = *factory->preparation;
                    const auto &face = leaf->payload->physics->triangles[4];
                    CHECK(face.materialWord == 6);
                    auto *groundBody = world.nodeFor(ground.bodyResource.owner);
                    CHECK(groundBody &&
                          groundBody->groundBody == ground.physics &&
                          ground.physics &&
                          ground.physics->body.proto ==
                              &ground.physics->prototype);
                    if (leafNode.node.packNode && groundBody) {
                      auto resolved = resolveSpatialGroundFace(
                          resources, *leafNode.node.packNode, 4, error);
                      CHECK(resolved && resolved->valid() &&
                            resolved->body() == ground.physics.get() &&
                            resolved->material() &&
                            resolved->material()->name == "GBegin1" &&
                            resolved->face() &&
                            resolved->face()->materialWord == 6);
                      const auto savedBody = groundBody->groundBody;
                      groundBody->groundBody.reset();
                      CHECK(!resolveSpatialGroundFace(
                          resources, *leafNode.node.packNode, 4, error));
                      CHECK(resolved && !resolved->valid());
                      groundBody->groundBody = savedBody;
                      CHECK(resolved && resolved->valid());
                      CHECK(!resolveSpatialGroundFace(
                          resources, *leafNode.node.packNode, 999, error));
                      retainedGroundFace = std::move(resolved);
                      retainedGroundBody = groundBody;
                      auto rootValue = physics::createBodyProto(100, 5, 1, 1, 1,
                                                                nullptr, error);
                      auto wheelValue = physics::createBodyProto(
                          0.01f, 3, 0.6f, 0.05f, 0, nullptr, error);
                      CHECK(rootValue && wheelValue);
                      if (rootValue && wheelValue) {
                        auto testCar = std::make_shared<CarPhysics>();
                        testCar->rootPrototype = std::move(*rootValue);
                        auto rootProto = std::shared_ptr<physics::BodyProto>(
                            testCar, &testCar->rootPrototype);
                        testCar->root =
                            std::make_unique<physics::Body>(physics::createBody(
                                *rootProto, {-278.518f, 10.3f, 1033.002f},
                                nullptr, 0));
                        auto root = std::shared_ptr<physics::Body>(
                            testCar, testCar->root.get());
                        testCar->wheels.push_back(
                            std::make_unique<CarWheelRuntime>());
                        auto *runtimeWheel = testCar->wheels.back().get();
                        runtimeWheel->config.wheelBody = std::move(*wheelValue);
                        auto wheelProto = std::shared_ptr<physics::BodyProto>(
                            testCar, &runtimeWheel->config.wheelBody);
                        runtimeWheel->suspensionBody =
                            std::make_unique<physics::Body>(physics::createBody(
                                *wheelProto, {-278.518f, 10.3f, 1033.002f},
                                nullptr, 0));
                        auto wheel = std::shared_ptr<physics::Body>(
                            testCar, runtimeWheel->suspensionBody.get());
                        CHECK(physics::attachBody(*wheel, *root));
                        wheel->flags |= 0x40;
                        testCar->bodyChildren[root.get()] = {wheel.get()};
                        testCar->bodyChildren[wheel.get()] = {};
                        for (auto *body : {root.get(), wheel.get()}) {
                          auto observation =
                              std::make_shared<CarBodyObservation>();
                          observation->body = body;
                          testCar->bodyObservations[body] =
                              std::move(observation);
                        }
                        CHECK(leafNode.node.packNode->bounds);
                        const auto parentMask = queryParentFace(
                            *leaf->payload->physics, 4, *root,
                            *leafNode.node.packNode->bounds, false);
                        CHECK(parentMask == ParentFaceMask::Accepted);
                        CHECK(queryParentFace(*leaf->payload->physics, 4, *root,
                                              *leafNode.node.packNode->bounds,
                                              true) ==
                              ParentFaceMask::Rejected);
                        root->state[root->current ^ 1].pos[1] += 1;
                        CHECK(queryParentFace(*leaf->payload->physics, 4, *root,
                                              *leafNode.node.packNode->bounds,
                                              false) !=
                              ParentFaceMask::Unsupported);
                        root->state[root->current ^ 1].pos[1] -= 1;
                        root->state[0].pos[1] += 100;
                        root->state[1].pos[1] += 100;
                        CHECK(queryParentFace(*leaf->payload->physics, 4, *root,
                                              *leafNode.node.packNode->bounds,
                                              false) ==
                              ParentFaceMask::Rejected);
                        root->state[0].pos[1] -= 100;
                        root->state[1].pos[1] -= 100;
                        const auto makeFace = [&] {
                          return resolveSpatialGroundFace(
                              resources, *leafNode.node.packNode, 4, error);
                        };
                        auto unavailable =
                            makeWheelGroundContact(makeFace(), root, rootProto,
                                                   wheel, wheelProto, false);
                        CHECK(unavailable.status ==
                              WheelGroundContactStatus::Unsupported);
                        auto wrongRoot =
                            std::make_shared<physics::Body>(physics::createBody(
                                *rootProto, {-278.518f, 10.3f, 1033.002f},
                                nullptr, 0));
                        CHECK(makeWheelGroundContact(makeFace(), wrongRoot,
                                                     rootProto, wheel,
                                                     wheelProto, true)
                                  .status ==
                              WheelGroundContactStatus::Unsupported);
                        wheel->flags |= 0x20;
                        CHECK(makeWheelGroundContact(makeFace(), root,
                                                     rootProto, wheel,
                                                     wheelProto, true)
                                  .status ==
                              WheelGroundContactStatus::Unsupported);
                        wheel->flags &= ~uint32_t{0x20};
                        auto collected = collectWheelGroundContacts(
                            resources, *city2LoadTarget, root, rootProto, wheel,
                            wheelProto);
                        CHECK(!collected.detail.empty());
                        CHECK(std::any_of(
                            collected.contacts.begin(),
                            collected.contacts.end(), [&](const auto &contact) {
                              const auto *candidate = contact->candidate();
                              return candidate && candidate->a == wheel.get() &&
                                     candidate->surface &&
                                     candidate->surface->materialWord == 6 &&
                                     std::fabs(candidate->pointB[1] -
                                               9.81177f) < 0.001f;
                            }));
                        auto compoundContacts = collectCarGroundContacts(
                            resources, *city2LoadTarget, testCar);
                        CHECK(!compoundContacts.complete &&
                              !compoundContacts.detail.empty());
                        CHECK(std::any_of(
                            compoundContacts.wheels.begin(),
                            compoundContacts.wheels.end(),
                            [&](const auto &hit) {
                              return hit.wheel == wheel.get() && hit.contact &&
                                     hit.contact->candidate() &&
                                     hit.contact->candidate()->surface &&
                                     hit.contact->candidate()
                                             ->surface->materialWord == 6;
                            }));
                        wheel->flags |= 0x20;
                        auto rejectedCollection = collectWheelGroundContacts(
                            resources, *city2LoadTarget, root, rootProto, wheel,
                            wheelProto);
                        CHECK(!rejectedCollection.passiveTraversalComplete &&
                              rejectedCollection.contacts.empty());
                        wheel->flags &= ~uint32_t{0x20};
                        wheel->state[wheel->current ^ 1].time =
                            wheel->state[wheel->current].time + 0.01f;
                        auto foundContact = makeWheelGroundContact(
                            makeFace(), root, rootProto, wheel, wheelProto,
                            parentMask == ParentFaceMask::Accepted);
                        CHECK(foundContact.status ==
                                  WheelGroundContactStatus::Hit &&
                              foundContact.contact);
                        if (foundContact.contact) {
                          const auto *candidate =
                              foundContact.contact->candidate();
                          CHECK(candidate && candidate->a == wheel.get() &&
                                candidate->b == &ground.physics->body &&
                                candidate->surface &&
                                candidate->surface->materialWord == 6 &&
                                std::fabs(candidate->pointB[1] - 9.81177f) <
                                    0.001f &&
                                std::fabs(candidate->pointA[1] + 0.48823f) <
                                    0.001f);
                          if (candidate && candidate->surface) {
                            tireContact.wheelBody = wheel.get();
                            tireContact.contactBody = candidate->b;
                            tireContact.contactOwner = candidate->otherOwner;
                            tireContact.contactPrototype =
                                candidate->otherPrototype;
                            tireContact.contactSurface = candidate->surface;
                            tireContact.contactFeature =
                                candidate->surface->featureIndex;
                            tireContact.contactPoint = candidate->pointB;
                            tireContact.normal = candidate->normal;
                            tireContact.wheelPoint = candidate->pointA;
                            tireContact.force = 2;
                            const auto resolvedMaterial = groundContactMaterial(
                                ground, face.materialWord, error);
                            CHECK(resolvedMaterial);
                            if (resolvedMaterial)
                              tireInput.surface = {
                                  candidate->surface,
                                  resolvedMaterial->friction,
                                  resolvedMaterial->rollingResistance};
                            tireInput.maxLoad = 2;
                            tireInput.geometryCorrections =
                                TireGeometryCorrections::None;
                            tireInput.tire.curve = {1, 0, 1, 0, 1, 0,
                                                    0, 0, 0, 0, 0};
                            tireInput.tire.slip = {0, 1, 0, 0.01f, 0, 0};
                            tireInput.tire.slipParameters = {1, 1, 0.3f, 0.5f,
                                                             1, 0, 0,    0};
                            tireInput.tire.wheelFriction = 1;
                            tireInput.tire.longitudinalScale = 1;
                            tireInput.tire.radius = wheelProto->shape.dim[0];
                            tireInput.tire.inverseWheelInertia = 1;
                            tireInput.tire.angularGain = 2.8f;
                            tireState.angularSpeed = -2 / tireInput.tire.radius;
                            CHECK(stepTireContact(*foundContact.contact,
                                                  tireContact, tireState,
                                                  tireInput));
                            CHECK(tireState.angularSpeed >
                                      -2 / tireInput.tire.radius &&
                                  root->state[root->current].force[2] < 0);
                            const auto stateBefore = tireState;
                            const auto forceBefore =
                                root->state[root->current].force;
                            const auto surfaceBefore =
                                tireInput.surface.identity;
                            tireInput.surface.identity = std::make_shared<
                                physics::SuspensionContactSurface>();
                            CHECK(!stepTireContact(*foundContact.contact,
                                                   tireContact, tireState,
                                                   tireInput));
                            CHECK(tireState.angularSpeed ==
                                      stateBefore.angularSpeed &&
                                  root->state[root->current].force ==
                                      forceBefore);
                            tireInput.surface.identity = surfaceBefore;
                            CHECK(physics::commit(*wheel) ==
                                  physics::CommitResult::Flipped);
                            CHECK(!foundContact.contact->candidate() &&
                                  foundContact.contact->tireCandidate());
                            CHECK(stepTireContact(*foundContact.contact,
                                                  tireContact, tireState,
                                                  tireInput));
                            runtimeWheel->suspension = tireContact;
                            runtimeWheel->suspension.settings.maxLength = 1;
                            runtimeWheel->radius = wheelProto->shape.dim[0];
                            runtimeWheel->effectiveInverseInertia = 1;
                            runtimeWheel->angularSpeed =
                                -2 / runtimeWheel->radius;
                            Wheel liveCoefficients;
                            liveCoefficients.pacejka[0] = 1;
                            liveCoefficients.pacejka[2] = 1;
                            liveCoefficients.pacejka[4] = 1;
                            liveCoefficients.pacejka[11] = 2;
                            liveCoefficients.pacejka[12] = 1;
                            liveCoefficients.friction6c = 1;
                            CHECK(stepLiveGroundTire(
                                *foundContact.contact, *runtimeWheel,
                                liveCoefficients, tireInput.tire.slipParameters,
                                2.8f, 0, 0.01f));
                            CHECK(runtimeWheel->angularSpeed >
                                  -2 / runtimeWheel->radius);
                            wheel->state[wheel->current ^ 1].time =
                                wheel->state[wheel->current].time + 0.01f;
                            CHECK(!foundContact.contact->tireCandidate());
                            const auto afterCommit = tireState;
                            CHECK(!stepTireContact(*foundContact.contact,
                                                   tireContact, tireState,
                                                   tireInput));
                            CHECK(tireState.angularSpeed ==
                                  afterCommit.angularSpeed);
                          }
                          wheelContact = std::move(foundContact.contact);
                        }
                      }
                    }
                    const auto material =
                        groundContactMaterial(ground, face.materialWord, error);
                    CHECK(material && material->name == "GBegin1" &&
                          material->friction == 1.0f &&
                          material->rollingResistance == 0.001f &&
                          material->collisionCoefficient == 0.2f);
                    CHECK(!groundContactMaterial(ground, 108, error) &&
                          !error.empty());
                    CHECK(!groundContactMaterial(ground, 0xffffffffu, error) &&
                          !error.empty());
                    if (city2LoadTarget->groundFactory &&
                        city2LoadTarget->groundFactory->preparation) {
                      const auto city2Material = groundContactMaterial(
                          *city2LoadTarget->groundFactory->preparation,
                          face.materialWord, error);
                      CHECK(city2Material && city2Material->name == "Cross");
                    }
                  }
                  CHECK(leafNode.node.packNode &&
                        resources.unloadPassiveSpatialGame(
                            *leafNode.node.packNode, error));
                  CHECK(!retainedGroundFace || !retainedGroundFace->valid());
                  CHECK(!wheelContact || !wheelContact->candidate());
                  if (wheelContact) {
                    const auto stateBefore = tireState;
                    CHECK(!stepTireContact(*wheelContact, tireContact,
                                           tireState, tireInput));
                    CHECK(tireState.angularSpeed == stateBefore.angularSpeed &&
                          tireState.longitudinalSlip ==
                              stateBefore.longitudinalSlip);
                  }
                  CHECK(leaf->payload && leaf->payload->physics &&
                        leaf->payload->physics->triangles.size() == 7);
                  if (leafNode.node.packNode &&
                      resources.loadPassiveSpatialGame(*leafNode.node.packNode,
                                                       error)) {
                    retainedGroundFace = resolveSpatialGroundFace(
                        resources, *leafNode.node.packNode, 4, error);
                    CHECK(retainedGroundFace && retainedGroundFace->valid());
                  } else
                    CHECK(false);
                }
                const auto outside = queryPassiveSpatialCandidates(
                    resources, *city2LoadTarget, {100000, 100000, 100000},
                    {100001, 100001, 100001});
                CHECK(outside.candidates.empty() && !outside.complete &&
                      !outside.detail.empty());
              }
              city2LoadTarget = nullptr;
            }
          }
          WorldNode *liveReceiver = nullptr;
          if (const auto *plainState = nativeResourceState(plain, &arena)) {
            const int32_t plainHandle =
                static_cast<int32_t>(plainState - arena.resourceStates.data()) +
                1;
            const auto bound = game.bindings.find(plainHandle);
            if (bound != game.bindings.end() && bound->second)
              liveReceiver = world.nodeFor(bound->second->link.owner);
          }
          CHECK(node.eventSubscriptions.size() == 1 && liveReceiver &&
                node.eventSubscriptions[0]->target.owner == liveReceiver &&
                node.eventSubscriptions[0]->mask == 0x20000);
          CHECK(!loadGroundGameResource(arena, node) &&
                node.groundFactory == factory &&
                node.eventSubscriptions.size() == 1);
          const auto again =
              vmExecute(probeWorld.main.c, loadTree, {plain}, &probeRegistry,
                        &probeClasses, &probeStates, &arena);
          CHECK(again.ok && node.groundFactory == factory &&
                node.eventSubscriptions.size() == 1 && node.loaded());
          if (factory && liveReceiver) {
            auto *observed = world.addChild(*root, "ground event observer", 0);
            CHECK(observed);
            if (observed) {
              liveReceiver->eventMask |= 0x10;
              liveReceiver->eventSubscriptionsKnownEmpty = true;
              node.eventSubscriptions.clear();
              for (int i = 0; i < 2; ++i) {
                auto subscription = std::make_unique<EventSubscription>();
                subscription->mask = 0x10;
                if (i == 1) {
                  subscription->replacementMask = 0x10000010;
                  subscription->replacementPayload = "replacement";
                }
                setLinkOwner(subscription->target, liveReceiver);
                node.eventSubscriptions.push_back(std::move(subscription));
              }
              const std::string payload =
                  "obs_add " +
                  std::to_string(static_cast<int32_t>(observed->instanceId));
              groundEventArgs = {nullHotkey, int32_t{0x10}, string(payload)};
              groundEventReceiver = handler;
              for (auto &subscription : node.eventSubscriptions)
                subscription->mask = 0x20;
              CHECK(std::holds_alternative<std::monostate>(gameRefQueueEvent(
                        groundEventArgs, handler, &arena, &probeClasses)) &&
                    factory->observers.size() == 1);
              groundEventArgs[2] = string(
                  "obs_rem " +
                  std::to_string(static_cast<int32_t>(observed->instanceId)));
              CHECK(std::holds_alternative<std::monostate>(gameRefQueueEvent(
                        groundEventArgs, handler, &arena, &probeClasses)) &&
                    factory->observers.empty());
              for (auto &subscription : node.eventSubscriptions)
                subscription->mask = 0x10;
              groundEventArgs[2] = string(payload);
              int calls = 0;
              groundEventCallback =
                  [&](const VmObject *recipient, const std::string &method,
                      std::vector<VmValue> values) -> VmResult {
                CHECK(recipient == plain && (method == "handleEvent" ||
                                             method == "handleEventAsync"));
                CHECK(factory->observers.size() == 1 &&
                      factory->observers[0]->owner == observed);
                CHECK(values.size() == 3 &&
                      std::get<int32_t>(values[1]) == 0x10);
                const auto *source = nativeResourceState(
                    std::get<const VmObject *>(values[0]), &arena);
                CHECK(source && source->words[2] == node.value);
                const auto *textObject = std::get<const VmObject *>(values[2]);
                bool correct = false;
                for (const auto &entry : arena.strings)
                  if (entry.second == textObject &&
                      entry.first == (calls ? "replacement" : payload))
                    correct = true;
                CHECK(correct);
                if (!calls)
                  node.eventSubscriptions.clear();
                ++calls;
                return {true, std::monostate{}, {}};
              };
              const auto sendRef =
                  probeWorld.main.ref("test.CityProbe", "send", "()V");
              const std::vector<TufaNode> sendTree{
                  vm_test::literal(0),
                  vm_test::node(0x24),
                  vm_test::node(0x11, true, 2),
                  vm_test::node(0x01, true, 0),
                  vm_test::node(0x1a, true, sendRef),
                  vm_test::retVoid};
              const auto sent = vmExecute(probeWorld.main.c, sendTree, {plain},
                                          &probeRegistry, &probeClasses,
                                          &probeStates, &arena);
              CHECK(sent.ok && calls == 2 && node.eventSubscriptions.empty());
              groundEventCallback = {};
              CHECK(world.destroy(*observed));
            }
          }
          mutablePlain->runtimeClass = savedClass;
          CHECK(world.destroy(*configNode));
        }
      }
      setLinkOwner(game.bindings[h]->link, eventNode);
      game.resources = nullptr;
      if (retainedGroundFace && retainedGroundBody) {
        CHECK(world.destroy(*retainedGroundBody));
        CHECK(!retainedGroundFace->valid() && !retainedGroundFace->body() &&
              !retainedGroundFace->face() && !retainedGroundFace->material());
      }
    }
    CHECK(!retainedGroundFace || !retainedGroundFace->valid());
    eventNode->eventSubscriptions.clear();
    eventNode->eventSubscriptionsKnownEmpty = true;
    float keyValue = 0.0f;
    hotkeys.physicalKey = [&](int32_t key) -> std::optional<float> {
      return key == 7 ? std::optional<float>{keyValue} : std::nullopt;
    };
    CHECK(std::holds_alternative<std::monostate>(inputCreateHotkey(
        {int32_t{7}, int32_t{2}, hotkey, handler, nullHotkey, int32_t{3}},
        nullptr, &arena, &loaded)));
    auto secondObject = std::make_unique<VmObject>();
    secondObject->fields.push_back({"state", int32_t{0}, "I"});
    const auto *secondHotkey = secondObject.get();
    arena.objects.push_back(std::move(secondObject));
    CHECK(std::holds_alternative<std::monostate>(inputCreateHotkey(
        {int32_t{7}, int32_t{2}, secondHotkey, handler, nullHotkey, int32_t{3}},
        nullptr, &arena, &loaded)));
    int delivered = 0;
    arena.callScript = [&](const VmObject *target, const std::string &method,
                           std::vector<VmValue> values) -> VmResult {
      CHECK(target == handler && method == "handleEvent" &&
            values.size() == 1 &&
            std::get<const VmObject *>(values[0]) == hotkey);
      ++delivered;
      return {true, int32_t{0}, {}};
    };
    CHECK(std::holds_alternative<std::monostate>(
        inputCheckHotkeys({nullHotkey, nullHotkey}, nullptr, &arena, &loaded)));
    CHECK(delivered == 0 && std::get<int32_t>(hotkey->fields[0].value) == 0);
    keyValue = 1.0f;
    CHECK(std::holds_alternative<std::monostate>(
        inputCheckHotkeys({nullHotkey, nullHotkey}, nullptr, &arena, &loaded)));
    CHECK(delivered == 1 && std::get<int32_t>(hotkey->fields[0].value) == 1 &&
          std::get<int32_t>(secondHotkey->fields[0].value) == 1);
    CHECK(std::holds_alternative<std::monostate>(inputCheckHotkeys(
              {nullHotkey, nullHotkey}, nullptr, &arena, &loaded)) &&
          delivered == 1);
    keyValue = 0.0f;
    CHECK(std::holds_alternative<std::monostate>(inputCheckHotkeys(
              {nullHotkey, nullHotkey}, nullptr, &arena, &loaded)) &&
          delivered == 2);
    CHECK(world.destroy(*eventNode));
  }
  arena.callScript = nullptr;
  uint32_t bufferedKey = 0x0041001e;
  hotkeys.lastKey = [&]() -> std::optional<uint32_t> { return bufferedKey; };
  auto last = inputLastKey({}, nullptr, &arena, &loaded);
  CHECK(std::holds_alternative<VmValue>(last) &&
        std::get<int32_t>(std::get<VmValue>(last)) ==
            static_cast<int32_t>(bufferedKey));
  CHECK(hotkeys.characterRing[0] == 'A' && hotkeys.characterWrite == 1);
  bufferedKey = 0;
  CHECK(std::holds_alternative<VmValue>(
            inputLastKey({}, nullptr, &arena, &loaded)) &&
        hotkeys.characterWrite == 1);
  hotkeys.lastKey = []() -> std::optional<uint32_t> { return std::nullopt; };
  CHECK(std::holds_alternative<std::string>(
      inputLastKey({}, nullptr, &arena, &loaded)));
  arena.hotkeys = nullptr;
  TimerState timers;
  arena.timers = &timers;
  CHECK(std::holds_alternative<std::monostate>(
            gameTypeAddTimer({0.5f, int32_t{7}}, plain, &arena, &loaded)) &&
        timers.size() == 1);
  CHECK(std::holds_alternative<std::monostate>(
      gameTypeRemoveAllTimers({}, plain, &arena, &loaded)));
  std::string timerError;
  CHECK(timers.advance(
            0,
            [](LinkOwner &, int32_t, uint32_t, int32_t, std::string &) {
              return true;
            },
            timerError) &&
        timers.size() == 0);
  arena.timers = nullptr;
  LinkOwner timerOwner;
  timerOwner.value = 42;
  CHECK(timers.add(&timerOwner, 2.0f, 17, timerError));
  CHECK(timers.add(&timerOwner, 1.0f, 18, timerError));
  std::vector<int32_t> fired;
  auto deliver = [&](LinkOwner &target, int32_t id, uint32_t, int32_t,
                     std::string &) {
    CHECK(&target == &timerOwner);
    fired.push_back(id);
    return true;
  };
  CHECK(timers.advance(0.5f, deliver, timerError) && fired.empty());
  CHECK(timers.advance(1.0f, deliver, timerError) &&
        fired == std::vector<int32_t>{18});
  CHECK(timers.advance(2.0f, deliver, timerError) &&
        (fired == std::vector<int32_t>{18, 17}) && timers.size() == 0);
  CHECK(timers.add(&timerOwner, 1.0f, 19, timerError));
  timers.removeAll(&timerOwner);
  CHECK(timers.advance(3.0f, deliver, timerError) && timers.size() == 0 &&
        fired.size() == 2);
  CHECK(timers.add(&timerOwner, 0.0f, 20, timerError));
  while (timerOwner.head)
    setLinkOwner(*timerOwner.head, nullptr);
  CHECK(timers.advance(3.0f, deliver, timerError) && timers.size() == 0);
  {
    vm_test::World vm;
    vm.cls("java.util.resource.ResourceRef").instanceField("ptr", "I");
    vm.cls("java.util.resource.GameRef", "java.util.resource.ResourceRef");
    auto &threadClass = vm.cls("java.lang.Thread");
    threadClass.c.methods[0].push_back(
        {{0x48, threadClass.t("sleep"), threadClass.t("(F)V"), UINT32_MAX, 0}});
    auto &handlerClass = vm.cls("test.TimerHandler");
    handlerClass.instanceField("asyncMode", "I")
        .instanceField("received", "I")
        .constructor();
    const auto received = handlerClass.t("received");
    const auto sleep = handlerClass.ref("java.lang.Thread", "sleep", "(F)V");
    const auto threadOwner = handlerClass.cref("java.lang.Thread");
    const float delay = 5.0f;
    uint32_t delayBits;
    std::memcpy(&delayBits, &delay, sizeof(delayBits));
    handlerClass.method("handleEvent", "(Ljava.util.resource.GameRef;II)V", 1,
                        {vm_test::literal(77), vm_test::node(0x01, true, 0),
                         vm_test::node(0x1c, true, received),
                         vm_test::node(0x08, true, 35), vm_test::retVoid});
    handlerClass.method(
        "animate", "()V", 1,
        {vm_test::node(0x07, true, 28), vm_test::node(0x0a, true, delayBits),
         vm_test::literal(1), vm_test::node(0x24), vm_test::node(0x11, true, 2),
         vm_test::node(0x19, true, threadOwner),
         vm_test::node(0x1a, true, sleep), vm_test::node(0x08, true, 27),
         vm_test::literal(66), vm_test::node(0x01, true, 0),
         vm_test::node(0x1c, true, received), vm_test::node(0x08, true, 35),
         vm_test::retVoid});
    vm.start();
    auto madeHandler = vm.alloc("test.TimerHandler");
    const auto *timerHandler =
        madeHandler.ok ? vm_test::asObject(madeHandler.value) : nullptr;
    CHECK(timerHandler != nullptr);
    GameWorld timerWorld;
    auto *timerRoot = timerWorld.addRoot();
    auto *timerNode = timerWorld.addChild(*timerRoot, "timer", 0x21);
    CHECK(timerNode != nullptr);
    VmGameState timerGame;
    timerGame.world = &timerWorld;
    vm.arena.gameState = &timerGame;
    if (timerHandler && timerNode) {
      timerWorld.markLoaded(*timerNode);
      timerNode->eventMask = 0x80;
      timerNode->eventSubscriptionsKnownEmpty = true;
      timerNode->scriptReceiver = timerHandler;
      CHECK(dispatchTimerEvent(vm.arena, *timerNode, 123, 0, 0,
                               vm.runtime->classes(),
                               vm.runtime->staticFields(), timerError));
      CHECK(std::get<int32_t>(
                vm_test::fieldOf(timerHandler, "received")->value) == 77);
      CHECK(!vm.arena.resourceStates.empty() &&
            vm.arena.resourceStates.back().words[2] == timerNode->instanceId);
      CHECK(timerWorld.registerHook(GameHook::Kind1c, *timerNode));
      {
        double now = 0;
        VmScheduler scheduler(vm.arena, &nativeRegistry(),
                              &vm.runtime->classes(),
                              &vm.runtime->staticFields(), [&] {
                                now += 0.01;
                                return now;
                              });
        AnimationDispatch dispatch(vm.arena, scheduler, 0.016f,
                                   vm.runtime->classes(),
                                   vm.runtime->staticFields());
        CHECK(dispatch.resume(timerError) && !dispatch.finished());
        CHECK(std::get<int32_t>(
                  vm_test::fieldOf(timerHandler, "received")->value) == 77);
        CHECK(dispatch.resume(timerError) && !dispatch.finished());
        const auto sleepTick = scheduler.tick(10000);
        if (!sleepTick.ok)
          std::fprintf(stderr, "animation sleep tick: %s\n",
                       sleepTick.error.c_str());
        CHECK(sleepTick.ok);
        CHECK(dispatch.resume(timerError) && !dispatch.finished());
        CHECK(std::get<int32_t>(
                  vm_test::fieldOf(timerHandler, "received")->value) == 77);
        now = 6.0;
        const auto wakeTick = scheduler.tick(10000);
        if (!wakeTick.ok)
          std::fprintf(stderr, "animation wake tick: %s\n",
                       wakeTick.error.c_str());
        CHECK(wakeTick.ok);
        CHECK(finishDispatch(dispatch, scheduler, timerError));
      }
      CHECK(std::get<int32_t>(
                vm_test::fieldOf(timerHandler, "received")->value) == 66);
      CHECK(timerWorld.destroy(*timerNode));
      CHECK(runAnimationTest(vm.arena, 0.016f, vm.runtime->classes(),
                             vm.runtime->staticFields(), timerError));
      CHECK(timerWorld.hooks(GameHook::Kind1c).empty());
    }
  }
  {
    vm_test::World vm;
    vm.cls("java.lang.String");
    vm.cls("java.util.resource.ResourceRef").instanceField("ptr", "I");
    vm.cls("java.util.resource.GameRef", "java.util.resource.ResourceRef");
    auto &handlerClass = vm.cls("test.CursorHandler");
    handlerClass.instanceField("asyncMode", "I")
        .instanceField("received", "I")
        .instanceField("event", "I")
        .instanceField("source", "Ljava.util.resource.GameRef;")
        .instanceField("payload", "Ljava.lang.String;")
        .constructor();
    const auto received = handlerClass.t("received");
    const auto payload = handlerClass.t("payload");
    const auto event = handlerClass.t("event");
    const auto sourceField = handlerClass.t("source");
    handlerClass.method(
        "handleEvent", "(Ljava.util.resource.GameRef;ILjava.lang.String;)V", 1,
        {vm_test::literal(91), vm_test::node(0x01, true, 0),
         vm_test::node(0x1c, true, received), vm_test::node(0x08, true, 35),
         vm_test::node(0x01, true, 1), vm_test::node(0x01, true, 0),
         vm_test::node(0x1c, true, payload), vm_test::node(0x08, true, 35),
         vm_test::node(0x01, true, 2), vm_test::node(0x01, true, 0),
         vm_test::node(0x1c, true, event), vm_test::node(0x08, true, 35),
         vm_test::node(0x01, true, 3), vm_test::node(0x01, true, 0),
         vm_test::node(0x1c, true, sourceField), vm_test::node(0x08, true, 35),
         vm_test::retVoid});
    vm.start();
    auto made = vm.alloc("test.CursorHandler");
    const auto *handler = made.ok ? vm_test::asObject(made.value) : nullptr;
    GameWorld eventWorld;
    auto *root = eventWorld.addRoot();
    auto *source = eventWorld.addChild(*root, "cursor", 0x21);
    auto *target = eventWorld.addChild(*root, "handler", 0x21);
    VmGameState eventGame;
    eventGame.world = &eventWorld;
    vm.arena.gameState = &eventGame;
    CHECK(handler && source && target);
    if (handler && source && target) {
      eventWorld.markLoaded(*source);
      eventWorld.markLoaded(*target);
      target->eventMask = 0x10000;
      target->eventSubscriptionsKnownEmpty = true;
      target->scriptReceiver = handler;
      auto entry = std::make_unique<EventSubscription>();
      entry->mask = 0x10000;
      setLinkOwner(entry->target, target);
      source->eventSubscriptions.push_back(std::move(entry));
      std::string error;
      auto second = std::make_unique<EventSubscription>();
      second->mask = 0x10000;
      setLinkOwner(second->target, target);
      source->eventSubscriptions.push_back(std::move(second));
      {
        VmScheduler scheduler(vm.arena, &nativeRegistry(),
                              &vm.runtime->classes(),
                              &vm.runtime->staticFields(), [] { return 0.0; });
        CursorDispatch dispatch(
            vm.arena, scheduler, *source, "1 2 3 4 0.000 0.000 0.000",
            vm.runtime->classes(), vm.runtime->staticFields());
        const auto before = vm.arena.resourceStates.size();
        // Fire-and-forget: the coroutine completes without waiting for the
        // subscriber, which the scheduler drains (stock does not join its
        // per-dispatch thread).
        CHECK(dispatch.resume(error) && dispatch.finished());
        CHECK(finishDispatch(dispatch, scheduler, error));
        CHECK(vm.arena.resourceStates.size() >= before + 1);
      }
      source->eventSubscriptions.pop_back();
      CHECK(std::get<int32_t>(vm_test::fieldOf(handler, "received")->value) ==
            91);
      bool foundPayload = false;
      for (const auto &value : vm.arena.strings)
        if (value.first == "1 2 3 4 0.000 0.000 0.000" &&
            std::get<const VmObject *>(
                vm_test::fieldOf(handler, "payload")->value) == value.second)
          foundPayload = true;
      CHECK(foundPayload && !vm.arena.resourceStates.empty() &&
            vm.arena.resourceStates.back().words[2] == source->instanceId);
      CHECK(std::get<int32_t>(vm_test::fieldOf(handler, "event")->value) ==
            0x10000);
      CHECK(std::get<const VmObject *>(
                vm_test::fieldOf(handler, "source")->value) ==
            vm.arena.resourceStates.back().owner);
      CHECK(runCursorTest(
          vm.arena, *source, "5 -65420 -65500 -0.049 -0.020 0.001",
          vm.runtime->classes(), vm.runtime->staticFields(), error));
      bool foundClick = false;
      for (const auto &value : vm.arena.strings)
        if (value.first == "5 -65420 -65500 -0.049 -0.020 0.001" &&
            std::get<const VmObject *>(
                vm_test::fieldOf(handler, "payload")->value) == value.second)
          foundClick = true;
      CHECK(foundClick);
      auto *doomed = eventWorld.addChild(*root, "doomed cursor", 0x21);
      CHECK(doomed != nullptr);
      if (doomed) {
        eventWorld.markLoaded(*doomed);
        auto doomedEntry = std::make_unique<EventSubscription>();
        doomedEntry->mask = 0x10000;
        setLinkOwner(doomedEntry->target, target);
        doomed->eventSubscriptions.push_back(std::move(doomedEntry));
        auto savedEnsure = vm.arena.ensureClass;
        vm.arena.ensureClass = [&](const std::string &name) {
          if (name == "java.lang.String" && doomed) {
            eventWorld.destroy(*doomed);
            doomed = nullptr;
          }
          return savedEnsure(name);
        };
        CHECK(!runCursorTest(vm.arena, *doomed, "1 2 3 4 0 0 0",
                             vm.runtime->classes(), vm.runtime->staticFields(),
                             error));
        CHECK(doomed == nullptr && error == "cursor event target destroyed");
        vm.arena.ensureClass = savedEnsure;
      }
      {
        VmScheduler scheduler(vm.arena, &nativeRegistry(),
                              &vm.runtime->classes(),
                              &vm.runtime->staticFields(), [] { return 0.0; });
        CursorDispatch dispatch(vm.arena, scheduler, *source, "2 2 3 4",
                                vm.runtime->classes(),
                                vm.runtime->staticFields());
        CHECK(dispatch.resume(error) && dispatch.finished());
        CHECK(eventWorld.destroy(*target));
        // The queued task's liveness now guards the destroyed target, so a
        // tick after destruction drops it instead of running into it.
        CHECK(scheduler.tick(10000).ok);
      }
      CHECK(!source->eventSubscriptions[0]->target.owner);
      CHECK(runCursorTest(vm.arena, *source, "2 2 3 4", vm.runtime->classes(),
                          vm.runtime->staticFields(), error));
    }
  }
  {
    GameWorld playerWorld;
    auto *playerRoot = playerWorld.addRoot();
    auto *playerNode =
        playerWorld.addChild(*playerRoot, "player animation", 0x21);
    VmGameState playerGame;
    playerGame.world = &playerWorld;
    VmArena playerArena;
    playerArena.gameState = &playerGame;
    CHECK(playerNode != nullptr);
    if (playerNode) {
      playerWorld.markLoaded(*playerNode);
      playerNode->nativeDevice = "player";
      playerNode->player = std::make_unique<PlayerInstance>();
      CHECK(playerWorld.registerHook(GameHook::Kind1c, *playerNode));
      CHECK(runAnimationTest(playerArena, 0.025f, loaded, states, timerError));
      CHECK(playerGame.playerAnimationDelta == 0.025f);
      LinkOwner overlay;
      overlay.value = 42;
      setLinkOwner(playerNode->player->animationOverlay, &overlay);
      CHECK(!runAnimationTest(playerArena, 0.03f, loaded, states, timerError));
      CHECK(playerGame.playerAnimationDelta == 0.03f);
      setLinkOwner(playerNode->player->animationOverlay, nullptr);
      CHECK(playerWorld.destroy(*playerNode));
    }
  }
  {
    // Regression: a script-backed animate subscriber that SUSPENDS inside
    // HostDispatch, so the dispatch only completes if finishDispatch resumes
    // the coroutine after the scheduler finishes the task. With a single
    // resume (the earlier bug) this times out instead of delivering.
    vm_test::World vm;
    vm.cls("java.util.resource.ResourceRef").instanceField("ptr", "I");
    vm.cls("java.util.resource.GameRef", "java.util.resource.ResourceRef");
    auto &threadClass = vm.cls("java.lang.Thread");
    threadClass.c.methods[0].push_back(
        {{0x48, threadClass.t("sleep"), threadClass.t("(F)V"), UINT32_MAX, 0}});
    auto &handlerClass = vm.cls("test.AnimateSuspender");
    handlerClass.instanceField("asyncMode", "I")
        .instanceField("received", "I")
        .constructor();
    const auto received = handlerClass.t("received");
    const auto sleep = handlerClass.ref("java.lang.Thread", "sleep", "(F)V");
    const auto threadOwner = handlerClass.cref("java.lang.Thread");
    const float delay = 0.01f;
    uint32_t delayBits;
    std::memcpy(&delayBits, &delay, sizeof(delayBits));
    // animate(): sleep(delay); received = 99; return
    handlerClass.method(
        "animate", "()V", 1,
        {vm_test::node(0x07, true, 28), vm_test::node(0x0a, true, delayBits),
         vm_test::literal(1), vm_test::node(0x24), vm_test::node(0x11, true, 2),
         vm_test::node(0x19, true, threadOwner),
         vm_test::node(0x1a, true, sleep), vm_test::node(0x08, true, 27),
         vm_test::literal(99), vm_test::node(0x01, true, 0),
         vm_test::node(0x1c, true, received), vm_test::node(0x08, true, 35),
         vm_test::retVoid});
    vm.start();
    auto madeSuspender = vm.alloc("test.AnimateSuspender");
    const auto *suspender =
        madeSuspender.ok ? vm_test::asObject(madeSuspender.value) : nullptr;
    CHECK(suspender != nullptr);
    if (suspender) {
      GameWorld suspendWorld;
      auto *suspendRoot = suspendWorld.addRoot();
      auto *suspendNode =
          suspendWorld.addChild(*suspendRoot, "suspending animate", 0x21);
      CHECK(suspendNode != nullptr);
      if (suspendNode) {
        VmGameState suspendGame;
        suspendGame.world = &suspendWorld;
        vm.arena.gameState = &suspendGame;
        suspendWorld.markLoaded(*suspendNode);
        suspendNode->scriptReceiver = suspender;
        CHECK(suspendWorld.registerHook(GameHook::Kind1c, *suspendNode));
        // The clock advances so the sleep can expire; the dispatch must be
        // resumed by the helper after each tick.
        double now = 0;
        VmScheduler scheduler(vm.arena, &nativeRegistry(),
                              &vm.runtime->classes(),
                              &vm.runtime->staticFields(), [&] {
                                now += 0.01;
                                return now;
                              });
        AnimationDispatch dispatch(vm.arena, scheduler, 0.016f,
                                   vm.runtime->classes(),
                                   vm.runtime->staticFields());
        std::string suspendError;
        // Advance the clock inside the drive loop by ticking the scheduler
        // with a moving clock.
        // Drive through the helper exactly once. The clock advances on every
        // scheduler read, so the 0.01 s sleep expires inside the loop; only a
        // helper that resumes the coroutine after each tick can finish it.
        CHECK(finishDispatch(dispatch, scheduler, suspendError));
        CHECK(std::get<int32_t>(
                  vm_test::fieldOf(suspender, "received")->value) == 99);
        CHECK(suspendWorld.destroy(*suspendNode));
      }
    }
  }
  {
    GameWorld cursorWorld;
    auto *root = cursorWorld.addRoot();
    auto *node = cursorWorld.addChild(*root, "cursor animation", 0x21);
    VmGameState game;
    game.world = &cursorWorld;
    game.mouseHelp = 0;
    VmArena cursorArena;
    cursorArena.gameState = &game;
    CHECK(node != nullptr);
    if (node) {
      cursorWorld.markLoaded(*node);
      node->nativeDevice = "cursor";
      node->cursor = std::make_unique<CursorInstance>();
      CHECK(cursorWorld.registerHook(GameHook::Kind1c, *node));
      CHECK(runAnimationTest(cursorArena, 0.02f, loaded, states, timerError));
      LinkOwner controller;
      controller.value = 42;
      setLinkOwner(node->cursor->controller, &controller);
      CHECK(!runAnimationTest(cursorArena, 0.02f, loaded, states, timerError));
      setLinkOwner(node->cursor->controller, nullptr);
      game.mouseHelp = 1;
      CHECK(!runAnimationTest(cursorArena, 0.02f, loaded, states, timerError));
      CHECK(cursorWorld.destroy(*node));
    }
  }
  TimerState simulation;
  std::vector<float> steps;
  int events = 0;
  std::string stepOrder;
  auto drain = [&](std::string &) {
    stepOrder += "d";
    return true;
  };
  CHECK(simulation.add(&timerOwner, 0.2f, 99, timerError));
  auto physics = [&](float delta, std::string &) {
    steps.push_back(delta);
    stepOrder += "p";
    return true;
  };
  auto event = [&](LinkOwner &, int32_t id, uint32_t, int32_t, std::string &) {
    CHECK(id == 99);
    ++events;
    stepOrder += "e";
    return true;
  };
  CHECK(!simulation.advanceFrame(0.25f, 1.0f, 1.0f, 0.1f, {}, event, drain,
                                 timerError));
  CHECK(simulation.now() == 0 && simulation.size() == 1);
  CHECK(simulation.advanceFrame(0.25f, 1.0f, 1.0f, 0.1f, physics, event, drain,
                                timerError));
  CHECK(steps.size() == 3 && std::abs(steps[0] - 0.1f) < 0.00001f &&
        std::abs(steps[1] - 0.075f) < 0.00001f &&
        std::abs(steps[2] - 0.075f) < 0.00001f);
  CHECK(events == 1 && std::abs(simulation.now() - 0.25f) < 0.00001f);
  CHECK(stepOrder == "pdpdped");
  stepOrder.clear();
  CHECK(simulation.advanceFrame(0, 1, 1, 0.1f, physics, event, drain,
                                timerError));
  CHECK(stepOrder == "d");
  {
    GameWorld emptyWorld;
    CHECK(emptyWorld.addRoot());
    TimerState clock;
    auto emptyPhysics = [&](float seconds, std::string &error) {
      return emptyWorld.stepEmptyPhysics(seconds, error);
    };
    auto noEvent = [](LinkOwner &, int32_t, uint32_t, int32_t, std::string &) {
      return true;
    };
    CHECK(clock.advanceFrame(0.05f, 1.0f, 1.0f, 0.1f, emptyPhysics, noEvent,
                             drain, timerError));
    CHECK(std::abs(clock.now() - 0.05f) < 0.00001f);
    LinkOwner body;
    emptyWorld.trackLoadState(body, 2, true);
    CHECK(!clock.advanceFrame(0.05f, 1.0f, 1.0f, 0.1f, emptyPhysics, noEvent,
                              drain, timerError));
    CHECK(std::abs(clock.now() - 0.05f) < 0.00001f);
    emptyWorld.forgetLoadState(body);
  }
  {
    ResourceWorld ownerResources(world, packs);
    game.resources = &ownerResources;
    const VmObject *playerRef = allocateDummy();
    const VmObject *secondRef = allocateDummy();
    const VmObject *thirdRef = allocateDummy();
    const VmObject *carRef = allocateDummy();
    CHECK(playerRef && secondRef && thirdRef && carRef &&
          construct(playerRef, 0).ok && construct(secondRef, 0).ok &&
          construct(thirdRef, 0).ok && construct(carRef, 0).ok);
    const auto linkedNode = [&](const VmObject *object) {
      const auto *state = nativeResourceState(object, &arena);
      return state ? world.findInstance(state->words[2]) : nullptr;
    };
    auto *first = linkedNode(playerRef);
    auto *second = linkedNode(secondRef);
    auto *third = linkedNode(thirdRef);
    auto *car = linkedNode(carRef);
    CHECK(first && second && third && car);
    if (first && second && third && car) {
      for (auto *player : {first, second, third}) {
        player->nativeDevice = "player";
        player->player = std::make_unique<PlayerInstance>();
        player->playerInput = std::make_unique<PlayerInputState>();
        player->eventMask |= 0x10;
        player->eventSubscriptionsKnownEmpty = true;
      }
      car->nativeDevice = "car";
      car->eventSubscriptionsKnownEmpty = true;
      car->eventMask |= 0x10;
      car->carInstance = std::make_unique<CarRuntimeInstance>();
      car->carInstance->isCar = true;
      car->carInstance->seats.push_back(std::make_unique<CarSeat>());
      car->carInstance->seats.push_back(std::make_unique<CarSeat>());
      const std::string text =
          "controllable " +
          std::to_string(static_cast<int32_t>(car->instanceId));
      const auto command = [&](const VmObject *player) {
        return gameRefQueueEvent(
            {VmValue{null}, VmValue{int32_t{0x10}}, VmValue{string(text)}},
            player, &arena, &loaded);
      };
      const auto owner = [&]() {
        const auto result = gameRefGetInfoIntInt({int32_t{0x18}, int32_t{0}},
                                                 carRef, &arena, &loaded);
        const auto *value = std::get_if<VmValue>(&result);
        const auto *id = value ? std::get_if<int32_t>(value) : nullptr;
        return id ? *id : int32_t{-1};
      };
      CHECK(owner() == 0);
      setLinkOwner(third->player->osd, second);
      CHECK(errorOf(command(thirdRef)).find("camera or osd unsupported") !=
            std::string::npos);
      CHECK(owner() == 0 && !third->player->controllable.owner &&
            car->eventSubscriptions.empty());
      setLinkOwner(third->player->osd, nullptr);
      first->player->config.damageMultiplier = 0.4567f;
      setLinkOwner(first->player->renderInstance, third);
      CHECK(std::holds_alternative<std::monostate>(command(playerRef)));
      CHECK(owner() == static_cast<int32_t>(first->instanceId));
      CHECK(first->player->controllable.owner == car &&
            first->player->driverPoseParityReported);
      CHECK(car->carInstance->seats[0]->renderInstance.owner == third);
      CHECK(!car->carInstance->seats[0]->camera.owner &&
            !car->carInstance->seats[0]->osd.owner);
      CHECK(std::fabs(car->carInstance->damageMultiplier - 0.457f) < 1e-6f);
      CHECK((first->eventMask & 1) && car->eventSubscriptions.size() == 1 &&
            car->eventSubscriptions[0]->target.owner == first &&
            car->eventSubscriptions[0]->mask == 1);
      CHECK(std::holds_alternative<std::monostate>(command(playerRef)) &&
            car->eventSubscriptions.size() == 1);
      CHECK(errorOf(command(secondRef)).find("subscriptions unsupported") !=
            std::string::npos);
      CHECK(!second->player->controllable.owner &&
            !car->carInstance->seats[1]->player.owner);
      setLinkOwner(car->carInstance->seats[0]->camera, first);
      setLinkOwner(car->carInstance->seats[0]->osd, first);
      CHECK(world.destroy(*first));
      CHECK(!car->carInstance->seats[0]->camera.owner &&
            !car->carInstance->seats[0]->osd.owner);
      CHECK(owner() == 0 && !car->eventSubscriptions[0]->target.owner);
      CHECK(std::holds_alternative<std::monostate>(command(secondRef)));
      CHECK(owner() == static_cast<int32_t>(second->instanceId));
      CHECK(second->player->controllable.owner == car);
      CHECK(world.destroy(*car));
      CHECK(!second->player->controllable.owner);
      CHECK(world.destroy(*second));
      CHECK(world.destroy(*third));
    }
    game.resources = nullptr;
  }
  {
    ResourceWorld carResources(world, packs);
    game.resources = &carResources;
    const VmObject *carRef = allocateDummy();
    CHECK(carRef && construct(carRef, 0).ok);
    const auto *state = nativeResourceState(carRef, &arena);
    auto *car = state ? world.findInstance(state->words[2]) : nullptr;
    CHECK(car != nullptr);
    if (car) {
      car->nativeDevice = "car";
      car->carInstance = std::make_unique<CarRuntimeInstance>();
      car->carInstance->isCar = true;
      car->eventMask |= 0x10;
      const auto send = [&](const char *text) {
        return gameRefQueueEvent(
            {VmValue{null}, VmValue{int32_t{0x10}}, VmValue{string(text)}},
            carRef, &arena, &loaded);
      };
      CHECK(car->carInstance->damageMultiplier == 1.0f);
      CHECK(std::holds_alternative<std::monostate>(
                send("damage_multiplier 0.375")) &&
            car->carInstance->damageMultiplier == 0.375f);
      CHECK(std::holds_alternative<std::monostate>(
                send("damage_multiplier -2")) &&
            car->carInstance->damageMultiplier == -2.0f);
      CHECK(std::holds_alternative<std::monostate>(
                send("damage_multiplier invalid")) &&
            car->carInstance->damageMultiplier == 1.0f);
      CHECK(std::holds_alternative<std::string>(send("activate 1 2 3")) &&
            car->carInstance->damageMultiplier == 1.0f);
      // FUN_00448d70 tail: the first occupied seat stores the player device's
      // message 0x15 reply (stock player +0x242c) on the car as +0x1edc; an
      // unlinked seat stores 0. Car getInfo(0x15) reads the stored word back
      // (stock car getInfo case 0x15, all.c:68209).
      const auto idText = [](uint32_t id) {
        int32_t signedId;
        std::memcpy(&signedId, &id, sizeof id);
        return std::to_string(signedId);
      };
      auto occupiedSeat = std::make_unique<CarSeat>();
      auto *occupiedSeatPtr = occupiedSeat.get();
      car->carInstance->seats.push_back(std::move(occupiedSeat));
      CHECK(std::holds_alternative<std::monostate>(send("activate 1 2 3")));
      CHECK(car->carInstance->activateInfo == 0);
      if (const auto *state = nativeResourceState(carRef, &arena)) {
        const auto handle =
            static_cast<int32_t>(state - arena.resourceStates.data()) + 1;
        auto &binding = game.bindings[handle];
        if (!binding)
          binding = std::make_unique<ReceiverBinding>();
        setLinkOwner(binding->link, car);
        const auto getInfo = [&](int32_t kind) {
          return invokeNative("java.util.resource.GameRef", "getInfo", "(II)I",
                              {VmValue{kind}, VmValue{int32_t{0}}}, carRef,
                              &arena, &loaded);
        };
        const auto emptyResult = getInfo(0x15);
        const auto *emptyValue = std::get_if<VmValue>(&emptyResult);
        CHECK(emptyValue && std::holds_alternative<int32_t>(*emptyValue) &&
              std::get<int32_t>(*emptyValue) == 0);
        auto *playerNode = world.addChild(*root, "activate seat player", 0);
        CHECK(playerNode);
        if (playerNode) {
          playerNode->nativeDevice = "player";
          playerNode->player = std::make_unique<PlayerInstance>();
          playerNode->player->debugAiValue = 0x1234;
          setLinkOwner(occupiedSeatPtr->player, playerNode);
          const auto activate = std::string{"activate "} +
                                idText(playerNode->instanceId) + " 0 0";
          CHECK(std::holds_alternative<std::monostate>(send(activate.c_str())));
          CHECK(car->carInstance->activateInfo == 0x1234);
          const auto storedResult = getInfo(0x15);
          const auto *stored = std::get_if<VmValue>(&storedResult);
          CHECK(stored && std::holds_alternative<int32_t>(*stored) &&
                std::get<int32_t>(*stored) == 0x1234);
          setLinkOwner(occupiedSeatPtr->player, nullptr);
          CHECK(std::holds_alternative<std::monostate>(send("activate 0 0 0")));
          CHECK(car->carInstance->activateInfo == 0);
          CHECK(world.destroy(*playerNode));
        }
      }
      CHECK(world.destroy(*car));
    }
    game.resources = nullptr;
  }
  {
    const VmObject *partRef = allocateDummy();
    CHECK(partRef && construct(partRef, 0).ok);
    const auto *partState = nativeResourceState(partRef, &arena);
    WorldNode *part =
        partState ? world.findInstance(partState->words[2]) : nullptr;
    WorldNode *target = world.addChild(*root, "install-target", 0);
    ScriptNode *partScript = world.addScript(0x12340001, "test.Part");
    ScriptNode *targetScript = world.addScript(0x12340002, "test.Target");
    CHECK(part && target && partScript && targetScript);
    if (part && target && partScript && targetScript) {
      partScript->car = std::make_unique<CarPrototype>();
      targetScript->car = std::make_unique<CarPrototype>();
      setLinkOwner(partScript->car->source, partScript);
      setLinkOwner(targetScript->car->source, targetScript);
      CarSlot partProtoSlot;
      partProtoSlot.state.id = 11;
      partProtoSlot.attachments.push_back({targetScript->resourceId, 22});
      partScript->car->config.slots.push_back(partProtoSlot);
      CarSlot targetProtoSlot;
      targetProtoSlot.state.id = 22;
      targetScript->car->config.slots.push_back(targetProtoSlot);
      part->nativeDevice = "part";
      part->eventMask |= 0x10;
      part->carInstance = std::make_unique<CarRuntimeInstance>();
      target->carInstance = std::make_unique<CarRuntimeInstance>();
      setLinkOwner(part->carInstance->prototypeSource, partScript);
      setLinkOwner(target->carInstance->prototypeSource, targetScript);
      auto partSlot = std::make_unique<RuntimeSlotState>();
      partSlot->id = 11;
      part->carInstance->slots.push_back(std::move(partSlot));
      auto targetSlot = std::make_unique<RuntimeSlotState>();
      targetSlot->id = 22;
      targetSlot->slotType = 201;
      target->carInstance->isCar = true;
      target->carInstance->slots.push_back(std::move(targetSlot));
      CarConfig physicsConfig;
      physicsConfig.rootBody = 0;
      physicsConfig.bodies.resize(1);
      std::vector<CarGeometryPrototype> geometry(1);
      geometry[0].bodyIndex = 0;
      std::string physicsError;
      auto bodyProto =
          physics::createBodyProto(4, 1, 0.5f, 0, 0, nullptr, physicsError);
      CHECK(bodyProto.has_value());
      if (bodyProto)
        *geometry[0].body = *bodyProto;
      part->carInstance->partPhysics = buildPartPhysics(
          physicsConfig, geometry, "0,0,0,0,0,0,0", physicsError);
      target->carInstance->partPhysics = buildPartPhysics(
          physicsConfig, geometry, "3,0,0,0,0,0,0", physicsError);
      CHECK(part->carInstance->partPhysics && target->carInstance->partPhysics);
      part->carInstance->slots[0]->transform.rotation = {1, 0, 0, 0, 1,
                                                         0, 0, 0, 1};
      target->carInstance->slots[0]->transform.rotation = {1, 0, 0, 0, 1,
                                                           0, 0, 0, 1};
      const auto *command = string(
          "install 0 -123 0 " +
          std::to_string(static_cast<int32_t>(target->instanceId)) + " 22");
      CHECK(std::holds_alternative<std::monostate>(gameRefQueueEvent(
          {VmValue{null}, VmValue{int32_t{0x10}}, VmValue{command}}, partRef,
          &arena, &loaded)));
      CHECK(part->carInstance->slots[0]->attachedNode.owner == target);
      CHECK(target->carInstance->slots[0]->attachedNode.owner == part);
      CHECK(part->carInstance->partPhysics->root->attachment->parent ==
            target->carInstance->partPhysics->root.get());
      CHECK(carPartRemove(*part, 11, arena, physicsError));
      CHECK(!part->carInstance->partPhysics->root->attachment);
      CHECK(!target->carInstance->slots[0]->attachedNode.owner);
    }
  }
  {
    const auto *cursorRef = allocateDummy();
    CHECK(cursorRef && construct(cursorRef, 0).ok);
    const auto *cursorState = nativeResourceState(cursorRef, &arena);
    auto *cursorNode =
        cursorState ? world.findInstance(cursorState->words[2]) : nullptr;
    CHECK(cursorNode);
    if (cursorNode) {
      cursorNode->nativeDevice = "cursor";
      cursorNode->eventMask |= 0x10;
      cursorNode->cursor = std::make_unique<CursorInstance>();
      world.setCursorConfiguration(1, 1);
      std::vector<bool> visible;
      world.setCursorSelector(
          [&](int32_t index, bool show, std::string &error) {
            CHECK(index == 0);
            visible.push_back(show);
            error.clear();
            return true;
          });
      const auto command = [&](const std::string &text) {
        return gameRefQueueEvent(
            {VmValue{null}, VmValue{int32_t{0x10}}, VmValue{string(text)}},
            cursorRef, &arena, &loaded);
      };
      CHECK(std::holds_alternative<std::monostate>(command("disable")));
      CHECK(cursorNode->cursor->flags == 2 && world.cursorDisplayCount() == -1);
      CHECK(visible == std::vector<bool>{false});
      CHECK(std::holds_alternative<std::monostate>(command("disable")));
      CHECK(visible.size() == 1);
      CHECK(std::holds_alternative<std::monostate>(command("enable")));
      CHECK(cursorNode->cursor->flags == 3 && world.cursorDisplayCount() == 0);
      CHECK(visible.size() == 2 && visible.back());
      CHECK(std::holds_alternative<std::monostate>(command("standby")));
      CHECK(cursorNode->cursor->flags == 6 && world.cursorDisplayCount() == -1);
      world.setCursorSelector([](int32_t, bool, std::string &error) {
        error = "injected cursor failure";
        return false;
      });
      CHECK(std::holds_alternative<std::string>(command("enable")));
      CHECK(cursorNode->cursor->flags == 6 && world.cursorDisplayCount() == -1);
      world.setCursorSelector({});
    }
  }
  return failures ? 1 : 0;
}
