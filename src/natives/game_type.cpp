#include "natives/game_type.hpp"
#include "camera_box_pick.hpp"
#include "camera_device_body.hpp"
#include "camera_device_factory.hpp"
#include "car_runtime.hpp"
#include "car_hud.hpp"
#include "ground_runtime.hpp"
#include "hotkeys.hpp"
#include "natives/lang_string.hpp"
#include "natives/resource_ref.hpp"
#include "object_runtime.hpp"
#include "object_sound.hpp"
#include "physics_compound.hpp"
#include "render2d.hpp"
#include "resource_id.hpp"
#include "resource_world.hpp"
#include "run_log.hpp"
#include "scene_2d.hpp"
#include "scx.hpp"
#include "skeleton.hpp"
#include "sector_collision.hpp"
#include "timers.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <coroutine>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string_view>

namespace {
std::optional<CarCameraSettings> carCameraSettings(const VmArena &arena) {
  const auto distance =
      vmActiveStaticField(arena, "java.util.Config", "camera_distance");
  const auto fov = vmActiveStaticField(arena, "java.util.Config", "camera_FOV");
  if (!distance || !fov || !std::holds_alternative<float>(*distance) ||
      !std::holds_alternative<float>(*fov))
    return std::nullopt;
  return CarCameraSettings{std::get<float>(*distance) * 0.2f,
                           std::get<float>(*fov)};
}
// FUN_00448f20: construct constraints only after the render skeleton exists.
void activateSeatedDriver(ResourceWorld &resources, GameWorld &world,
                          WorldNode &carNode, CarRuntimeInstance &car, CarSeat &seat, const CarConfig &config) {
  const auto renderNode = resources.resolve(seat.renderInstance.owner);
  auto render = renderNode ? resources.sceneRenderInstance(renderNode) : nullptr;
  if (!render) return;
  std::string error;
  if (!sceneEnsureSkeleton(resources, *render, error)) {
    runlog::event(runlog::EventKind::Note,
                  "FUN_00448f20 seated driver skeleton unavailable: %s", error.c_str());
    return;
  }
  const auto &pose = render->skeletonPose();
  if (!pose) return; // Non-skeletal material: stock FUN_00541400 returns zero.
  const auto missing = configureSeatedDriver(*pose, config);
  runlog::event(runlog::EventKind::Note,
      "FUN_00448f20 driver animation parity subgap: named head/foot/hand and sthbone targets, live wheel-frame updates and skin palettes active; FUN_00533450 exact quaternion/Euler equivalence selection and annealing schedule not matched; normalized steering-to-wheel angle and seat/root alignment remain [INFERENCE]");
  for (const auto &bone : missing)
    runlog::event(runlog::EventKind::Note,
                  "FUN_00448f20 seated driver constraint bone absent: %s", bone.c_str());
  SceneRenderInstance seatFrame(nullptr);
  seatFrame.setBoneMatrix(config.seat.data(), config.seat.data()+3);
  render->setAttachmentOffset(seatFrame.boneMatrix());
  render->setDriverCar(&carNode);
  auto *root = world.nodeFor(car.physicsRoot.owner);
  if (!root || !render->attachPhysicsBone(world, *root))
    runlog::event(runlog::EventKind::Note,
                  "FUN_00448f20 seated driver physics root unavailable");
}

class Type1LoadTrace {
public:
  explicit Type1LoadTrace(uint32_t resource) : resource_(resource) {
    if (runlog::active()) {
      id_ = next_.fetch_add(1, std::memory_order_relaxed) + 1;
      stage("payload");
    }
  }
  ~Type1LoadTrace() {
    if (id_)
      runlog::event(runlog::EventKind::Note,
                    "load1 "
                    "{\"v\":1,\"id\":%llu,\"resource\":%u,\"event\":\"end\","
                    "\"stage\":\"%s\"}",
                    static_cast<unsigned long long>(id_), resource_, stage_);
  }
  void stage(const char *name) {
    stage_ = name;
    if (id_)
      runlog::event(runlog::EventKind::Note,
                    "load1 "
                    "{\"v\":1,\"id\":%llu,\"resource\":%u,\"event\":\"enter\","
                    "\"stage\":\"%s\"}",
                    static_cast<unsigned long long>(id_), resource_, stage_);
  }

private:
  inline static std::atomic<uint64_t> next_{0};
  uint64_t id_ = 0;
  uint32_t resource_;
  const char *stage_ = "payload";
};

const std::string *stringBytes(const VmArena &arena, const VmObject *object) {
  const std::string *found = nullptr;
  for (const auto &entry : arena.strings)
    if (entry.second == object) {
      if (found)
        return nullptr;
      found = &entry.first;
    }
  return found;
}
} // namespace

NativeResult
gameTypeCreateNativeInstance(const std::vector<VmValue> &args,
                             const VmObject *receiver, VmArena *arena,
                             const std::vector<const TufaClass *> *) {
  if (!arena || !arena->gameState || !arena->gameState->packs ||
      !arena->gameState->world)
    return std::string{"GameType.createNativeInstance: no game world state"};
  VmGameState &state = *arena->gameState;
  if (args.size() != 4 || !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]) ||
      !std::holds_alternative<const VmObject *>(args[2]) ||
      !std::holds_alternative<const VmObject *>(args[3]))
    return std::string{"invalid GameType.createNativeInstance call"};

  VmResourceState *receiverState = nativeResourceState(receiver, arena);
  if (!receiverState)
    return std::string{"GameType.createNativeInstance: receiver has no native "
                       "link (ResourceRef.newNative has not run)"};
  const int32_t handle =
      static_cast<int32_t>(receiverState - arena->resourceStates.data()) + 1;

  std::optional<uint32_t> parent;
  if (const VmObject *parentObject = std::get<const VmObject *>(args[0])) {
    const VmResourceState *parentState =
        nativeResourceState(parentObject, arena);
    if (!parentState)
      return std::string{"GameType.createNativeInstance: unsupported parent "
                         "without a native link"};
    parent = parentState->words[2];
  }
  if (!parent || !*parent)
    parent = state.world->gameParentId();
  uint32_t scriptId;
  const int32_t rawId = std::get<int32_t>(args[1]);
  std::memcpy(&scriptId, &rawId, sizeof scriptId);
  std::optional<std::string> argument, name;
  if (const VmObject *object = std::get<const VmObject *>(args[2])) {
    const std::string *bytes = stringBytes(*arena, object);
    if (!bytes)
      return std::string{
          "GameType.createNativeInstance: unreadable argument string"};
    argument = *bytes;
  }
  if (const VmObject *object = std::get<const VmObject *>(args[3])) {
    const std::string *bytes = stringBytes(*arena, object);
    if (!bytes)
      return std::string{
          "GameType.createNativeInstance: unreadable name string"};
    name = *bytes;
  }

  if (!receiver || !receiver->runtimeClass ||
      receiver->runtimeClass->name.empty())
    return std::string{
        "GameType.createNativeInstance: receiver class unavailable"};
  const std::string className = receiver->runtimeClass->name;
  if (!name)
    name = "L" + className + ";";
  GameCreationResult prepared;
  if (!scriptId) {
    if ((argument && argument->find('\0') != std::string::npos) ||
        name->find('\0') != std::string::npos)
      return std::string{
          "GameType.createNativeInstance: embedded NUL is unsupported"};
    ScriptNode *script = state.world->classScript(className);
    if (!script)
      return std::string{"GameType.createNativeInstance: class script resource "
                         "creation failed"};
    prepared.request = GameCreationRequest{
        script->resourceId, className, parent, argument, *name, {}};
  } else {
    prepared = prepareGameCreation(*state.packs, scriptId, parent, argument,
                                   name, arena->loadScriptPath);
  }
  if (!prepared.request)
    return "GameType.createNativeInstance: " + prepared.error;

  std::optional<PlayerFactorySetup> player;
  if (prepared.request->nativeDevice == "player") {
    if (!state.resources || !state.world->configNode() ||
        !state.world->configNode()->scriptReceiver)
      return std::string{"GameType.createNativeInstance: unsupported: player "
                         "needs linked Config and resource world"};
    const auto display =
        vmActiveStaticField(*arena, "java.util.Config", "player_display");
    const auto damage = vmActiveStaticField(*arena, "java.util.Config",
                                            "player_damage_multiplier");
    const auto distance = vmActiveStaticField(*arena, "java.util.Config",
                                              "player_change_distance");
    const auto velocity = vmActiveStaticField(*arena, "java.util.Config",
                                              "player_change_velocity");
    if (!display || !damage || !distance || !velocity ||
        !std::holds_alternative<int32_t>(*display) ||
        !std::holds_alternative<float>(*damage) ||
        !std::holds_alternative<float>(*distance) ||
        !std::holds_alternative<float>(*velocity))
      return std::string{"GameType.createNativeInstance: unsupported: player "
                         "Config fields unavailable"};
    std::string error;
    uint32_t soundId = 0, fontId = 0;
    if (!state.packs->resolveLiteral("system.rpk", 9, soundId, error) ||
        !state.packs->resolveLiteral("frontend.rpk", 0x23, fontId, error))
      return "GameType.createNativeInstance: unsupported: player resource "
             "names: " +
             error;
    const auto sound = state.resources->lookup(soundId, 6);
    const auto font = state.resources->lookup(fontId, 14);
    if (!sound.node || !font.node)
      return std::string{"GameType.createNativeInstance: unsupported: player "
                         "resource links unavailable"};
    player = PlayerFactorySetup{
        {std::get<int32_t>(*display), std::get<float>(*damage),
         std::get<float>(*distance), std::get<float>(*velocity)},
        sound.node.owner,
        font.node.owner};
  }

  auto &slot = state.bindings[handle];
  if (!slot)
    slot = std::make_unique<ReceiverBinding>();
  ReceiverBinding &binding = *slot;
  binding.getInfo = [arena, receiver,
                     &state](int kind) -> std::optional<int32_t> {
    if (!arena->callScript)
      return std::nullopt;
    const auto answer = arena->callScript(
        receiver, "getInfo", {VmValue{static_cast<int32_t>(kind)}});
    if (!answer.ok || answer.voidReturn)
      return std::nullopt;
    const auto *value = std::get_if<int32_t>(&answer.value);
    if (!value)
      return std::nullopt;
    state.getInfoLog.emplace_back(kind, *value);
    return *value;
  };
  const auto cameraSettings = prepared.request->nativeDevice == "car"
                                  ? carCameraSettings(*arena)
                                  : std::nullopt;
  const auto created = createNativeInstance(
      *state.world, *prepared.request, &binding, state.resources,
      player ? &*player : nullptr, cameraSettings ? &*cameraSettings : nullptr);
  binding.getInfo = nullptr;
  if (created.status != CreationStatus::Created)
    return std::string(created.status == CreationStatus::StockError
                           ? "GameType.createNativeInstance: stock error: "
                           : "GameType.createNativeInstance: unsupported: ") +
           created.detail;
  // FUN_00481a70 stores the new node's instance id in the receiver's link
  // record, +8.
  receiverState->words[2] = binding.link.cachedValue;
  created.node->scriptReceiver = receiver;
  return std::monostate{};
}

NativeResult gameRefSetFlags(const std::vector<VmValue> &args,
                             const VmObject *receiver, VmArena *arena,
                             const std::vector<const TufaClass *> *) {
  if (!arena || !arena->gameState || !arena->gameState->world)
    return std::string{"GameRef.setFlags: no game world state"};
  if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
    return std::string{"invalid GameRef.setFlags call"};
  const VmResourceState *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"GameRef.setFlags: receiver has no native link "
                       "(ResourceRef.newNative has not run)"};
  VmGameState &game = *arena->gameState;
  const auto binding = game.bindings.find(
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1);
  if (binding == game.bindings.end() || !binding->second->link.owner)
    return std::monostate{};
  WorldNode *node = game.world->nodeFor(binding->second->link.owner);
  if (!node)
    return std::string{
        "GameRef.setFlags: receiver link does not point at a world node"};
  uint32_t bits;
  const int32_t raw = std::get<int32_t>(args[0]);
  std::memcpy(&bits, &raw, sizeof bits);
  game.world->orFlags(*node, bits);
  return std::monostate{};
}

NativeResult gameRefClearFlags(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!arena || !arena->gameState || !arena->gameState->world)
    return std::string{"GameRef.clearFlags: no game world state"};
  if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
    return std::string{"invalid GameRef.clearFlags call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"GameRef.clearFlags: stock crash: missing native link"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second->link.owner)
    return std::monostate{};
  auto *node = game.world->nodeFor(binding->second->link.owner);
  if (!node)
    return std::string{"GameRef.clearFlags: non-world node unsupported"};
  node->flags &= ~static_cast<uint32_t>(std::get<int32_t>(args[0]));
  return std::monostate{};
}

static NativeResult changeEventMask(const std::vector<VmValue> &args,
                                    const VmObject *receiver, VmArena *arena,
                                    const std::vector<const TufaClass *> *,
                                    bool set) {
  if (!arena || !arena->gameState || !arena->gameState->world)
    return std::string{"GameType.setEventMask: no game world state"};
  if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
    return std::string{"invalid GameType.setEventMask call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{
        "GameType.setEventMask: stock crash: missing native link"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second->link.owner)
    return std::monostate{};
  auto *node = game.world->nodeFor(binding->second->link.owner);
  if (!node && game.resources) {
    const auto resolved = game.resources->resolve(binding->second->link.owner);
    if (resolved.packNode && resolved.type == 1 &&
        resolved.packNode->groundFactory && resolved.packNode->loaded()) {
      const auto mask = static_cast<uint32_t>(std::get<int32_t>(args[0]));
      if (set)
        resolved.packNode->eventMask |= mask;
      else
        resolved.packNode->eventMask &= ~mask;
      return std::monostate{};
    }
  }
  if (!node || !node->hasReceiver)
    return std::string{"GameType.setEventMask: instance unsupported"};
  const auto mask = static_cast<uint32_t>(std::get<int32_t>(args[0]));
  if (set)
    node->eventMask |= mask;
  else
    node->eventMask &= ~mask;
  return std::monostate{};
}

NativeResult
gameTypeSetEventMask(const std::vector<VmValue> &args, const VmObject *receiver,
                     VmArena *arena,
                     const std::vector<const TufaClass *> *classes) {
  return changeEventMask(args, receiver, arena, classes, true);
}
NativeResult
gameTypeClearEventMask(const std::vector<VmValue> &args,
                       const VmObject *receiver, VmArena *arena,
                       const std::vector<const TufaClass *> *classes) {
  return changeEventMask(args, receiver, arena, classes, false);
}

std::optional<std::string>
loadGroundGameResource(VmArena &context, ResourceNode &node,
                       const std::vector<VmStaticFields> *staticFields) {
  auto *arena = &context;
  if (!arena->gameState || !arena->gameState->world ||
      !arena->gameState->resources)
    return std::string{"ground loader: resource world unavailable"};
  auto &game = *arena->gameState;
  const auto resolved = game.resources->resolve(&node);
  if (resolved.packNode != &node || resolved.type != 1)
    return std::string{"ground loader: invalid resource"};
  if (node.parent && !node.parent->loaded())
    return std::string{"ground loader: parent is not loaded"};
  if (node.loaded())
    return node.groundFactory ? std::nullopt
                              : std::optional<std::string>{
                                    "type-1 instance construction unsupported"};
  Type1LoadTrace trace(resolved.id);
  GameResourcePayload payload;
  std::string error;
  // Reject unsupported object factories before registering children or
  // attaching the game-type link.
  if (!game.resources->inspectGamePayload(node, payload, error))
    return error;
  if (payload.runtimeGameType && *payload.runtimeGameType) {
    PackResource type;
    if (game.resources->packs().lookup(*payload.runtimeGameType, type, error) &&
        type.entry.resType == 8 && type.packBytes &&
        type.entry.payloadOffset <= type.packBytes->size() &&
        type.entry.payloadSize <=
            type.packBytes->size() - type.entry.payloadOffset &&
        type.entry.payloadSize >= 8) {
      const auto *p = type.packBytes->data() + type.entry.payloadOffset;
      const size_t size = type.entry.payloadSize;
      const uint32_t chunkSize = uint32_t(p[4]) | uint32_t(p[5]) << 8 |
                                 uint32_t(p[6]) << 16 | uint32_t(p[7]) << 24;
      // ICFG payloads of every game-type class ("object", "trlight", ...)
      // share the directive set; unsupported directives are rejected by the
      // object loader itself. Non-ICFG type-8 payloads stay with the
      // native-directive factories below.
      if (std::memcmp(p, "ICFG", 4) == 0 && chunkSize <= size - 8) {
        const auto *begin = reinterpret_cast<const char *>(p + 8);
        const void *terminator = std::memchr(begin, 0, chunkSize);
        if (terminator && terminator != begin)
          return object::loadObjectGameResource(*arena, node, type, payload);
      }
    }
  }
  if (!game.resources->prepareGamePayload(*resolved.packNode, payload, error))
    return error;
  trace.stage("factory");
  if (payload.runtimeGameType && payload.parameters) {
    const auto linked =
        game.resources->resolve(resolved.packNode->gameTypeLink.owner);
    PackResource topLevel;
    if (linked.packNode && linked.type == 8 &&
        game.resources->packs().lookup(*payload.runtimeGameType, topLevel,
                                       error) &&
        linked.packNode->payloadSlot == (*payload.runtimeGameType >> 16) &&
        linked.packNode->payloadOffset == topLevel.entry.payloadOffset &&
        linked.packNode->payloadSize == topLevel.entry.payloadSize) {
      const auto factory = readNativeOnlyGameType(
          game.resources->packs(), *payload.runtimeGameType, error);
      if (factory && factory->device == "ground") {
        trace.stage("config");
        auto &ground = resolved.packNode->groundPreparation;
        if (resolved.packNode->groundFactory) {
          trace.stage("factory_completion");
        } else if (ground)
          return std::string{"type-1 instance construction unsupported: ground "
                             "reset required before retry"};
        else {
          {
            const auto *config = game.world->configNode();
            const auto configField =
                [&](const char *name) -> std::optional<VmValue> {
              if (arena->activeExecution)
                return vmActiveStaticField(*arena, "java.util.Config", name);
              if (!staticFields)
                return std::nullopt;
              const VmStaticFields *state = nullptr;
              for (const auto &candidate : *staticFields) {
                if (!candidate.owner ||
                    candidate.owner->name != "java.util.Config")
                  continue;
                if (state)
                  return std::nullopt;
                state = &candidate;
              }
              if (!state)
                return std::nullopt;
              const VmField *match = nullptr;
              for (const auto &field : state->fields) {
                if (field.name != name)
                  continue;
                if (match || field.descriptor != "F")
                  return std::nullopt;
                match = &field;
              }
              return match ? std::optional<VmValue>{match->value}
                           : std::nullopt;
            };
            const auto gravity = configField("gravity");
            const auto friction = configField("ground_friction");
            if (!config || !config->scriptReceiver || !gravity || !friction ||
                !std::holds_alternative<float>(*gravity) ||
                !std::holds_alternative<float>(*friction) ||
                !std::isfinite(std::get<float>(*gravity)) ||
                !std::isfinite(std::get<float>(*friction)))
              return std::string{"type-1 instance construction unsupported: "
                                 "ground Config unavailable"};
            trace.stage("ground_parameters");
            auto prepared = prepareGround(
                *payload.parameters, std::get<float>(*gravity),
                std::get<float>(*friction), game.world->physicsTime(), error);
            if (!prepared)
              return std::string{"type-1 instance construction unsupported: "
                                 "ground preparation: "} +
                     error + " (parameters \"" + *payload.parameters + "\")";
            if (!prepared->bindCityOwner(*resolved.packNode))
              return std::string{"ground city owner unavailable"};
            trace.stage("default_materials");
            if (!bindGroundDefaultMaterials(*game.resources, *prepared->physics,
                                            error))
              return std::string{"ground material resources: "} + error;
            trace.stage("material_file");
            if (!prepareGroundMaterialFile(
                    game.resources->packs().root(), prepared->parameters,
                    prepared->physics->prototype.shape.friction,
                    prepared->files.materials, error))
              return std::string{"ground material file: "} + error;
            if (!prepared->files.materials ||
                prepared->files.materials->materials.empty()) {
              trace.stage("fallback_material");
              if (!bindGroundFallbackMaterial(*game.resources, *prepared,
                                              error))
                return std::string{"ground fallback material: "} + error;
            }
            trace.stage("body_node");
            WorldNode *body = game.world->addTypedChildUnderExternal(
                resolved.packNode->handle, *resolved.packNode, 1,
                resolved.packNode->sectorLink.owner, 2, "groundbody", 0x201);
            if (!body)
              return std::string{"type-1 instance construction unsupported: "
                                 "ground body resource"};
            body->groundBody = prepared->physics;
            body->flags |= 0x200000;
            game.world->noteInstanceCreated(*body);
            game.world->markLoaded(*body);
            body->loadPriority = 1.0f;
            setLinkOwner(prepared->bodyResource, body);
            ground = std::shared_ptr<GroundPreparation>(std::move(prepared));
            trace.stage("render_type");
            PackView cityView;
            if (game.resources->packs().view(resolved.packNode->payloadSlot,
                                             cityView,
                                             error) != PackViewStatus::Ok)
              return std::string{"ground render resource pack unavailable"};
            const ResourceIdMapping mapping{
                cityView.slot,
                static_cast<uint32_t>(game.resources->packs().slotCount()),
                cityView.dependencies};
            const auto renderId =
                remapResourceId(ground->parameters.resourceId, mapping);
            const auto renderType = game.resources->lookup(renderId, 14);
            if (renderType.status != LookupStatus::Found ||
                !renderType.node.owner)
              return std::string{"ground render type unavailable"};
            trace.stage("render_node");
            auto *render = game.world->addTypedChildUnderExternal(
                resolved.packNode->handle, *resolved.packNode, 1,
                resolved.packNode->sectorLink.owner, 3, "r_ground", 0x201);
            if (!render)
              return std::string{"ground render node unavailable"};
            ground->pendingRender =
                std::make_shared<SceneRenderInstance>(renderType.node.owner);
            game.world->noteInstanceCreated(*render);
            setLinkOwner(ground->renderResource, render);
            trace.stage("render_material");
            auto material = sceneLoadMaterial(*game.resources, renderId);
            if (!material.material)
              return std::string{"ground material preparation: "} +
                     material.error;
            ground->pendingMaterial = std::shared_ptr<SceneMaterialResources>(
                std::move(material.material));
            trace.stage("mesh");
            auto mesh = std::make_shared<ScxModel>();
            if (!sceneLoadGroundMesh(
                    *game.resources, ground->pendingMaterial->meshId(),
                    game.resources->packs().root(), *mesh, error))
              return std::string{"ground mesh preparation: "} + error;
            ground->pendingMesh = std::move(mesh);
            trace.stage("bone");
            ground->pendingRender->prepareUnattachedGroundBone();
            trace.stage("road_file");
            if (!prepareGroundRoadFile(game.resources->packs().root(),
                                       ground->parameters, ground->files.roads,
                                       error))
              return std::string{"ground road file: "} + error;
            if (ground->files.roads) {
              trace.stage("road_initialization");
              if (!ground->bindRoadOwner(*resolved.packNode))
                return std::string{"ground road owner unavailable"};
              if (!ground->initializeRoad(game.world->physicsTime()))
                return std::string{"ground road initialization unsupported"};
            }
            trace.stage("factory_completion");
            auto result = finishGroundFactory(ground, *game.world, error);
            if (!result)
              return std::string{"ground factory result: "} + error;
            setLinkOwner(result->city, resolved.packNode);
            result->sectorCollision =
                std::make_shared<SectorCollisionManager>(*resolved.packNode);
            resolved.packNode->groundFactory = std::move(result);
            resolved.packNode->eventMask |= 0x14;
            resolved.packNode->stateWord =
                (resolved.packNode->stateWord & ~uint32_t{0x50}) | 0x21;
            resolved.packNode->loadPriority = 1.0f;
            game.world->trackLoadState(linkOwner(*resolved.packNode),
                                       resolved.packNode->type, true);
          }
        }
      }
    }
  }
  // Stock devices "walk" (FUN_0042ced0) and "road" (FUN_0042c1c0) construct
  // only a small listener object and defer every behavior to script
  // commands. The host records the loaded state without instances or
  // geometry; script-command delivery to these listeners is not implemented.
  if (!node.groundFactory && payload.runtimeGameType) {
    const auto listener = readNativeOnlyGameType(
        game.resources->packs(), *payload.runtimeGameType, error);
    if (listener &&
        (listener->device == "walk" || listener->device == "road")) {
      if (!game.resources->loadListenerGame(*resolved.packNode, error))
        return std::string{"listener type-1 load: "} + error;
      return std::nullopt;
    }
  }
  if (!node.groundFactory) {
    if (payload.runtimeGameType) {
      const auto factory = readNativeOnlyGameType(
          game.resources->packs(), *payload.runtimeGameType, error);
      if (factory)
        return std::string{
                   "type-1 instance construction unsupported: device "} +
               factory->device;
      return std::string{
                 "type-1 instance construction unsupported: script 0x"} +
             [](uint32_t v) {
               char b[9];
               std::snprintf(b, sizeof b, "%08x", v);
               return std::string{b};
             }(*payload.runtimeGameType) +
             ": " + error;
    }
    return std::string{"type-1 instance construction unsupported"};
  }
  return std::nullopt;
}

NativeResult gameTypeAddNotification(const std::vector<VmValue> &args,
                                     const VmObject *receiver, VmArena *arena,
                                     const std::vector<const TufaClass *> *) {
  if (!arena || !arena->gameState || !arena->gameState->world ||
      (args.size() != 4 && args.size() != 5) ||
      !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]) ||
      !std::holds_alternative<int32_t>(args[2]) ||
      !std::holds_alternative<const VmObject *>(args[3]) ||
      (args.size() == 5 && !std::holds_alternative<const VmObject *>(args[4])))
    return std::string{"GameType.addNotification: invalid arguments"};
  const auto *targetObject = std::get<const VmObject *>(args[0]);
  if (!targetObject)
    return std::monostate{};
  auto &game = *arena->gameState;
  auto resolve = [&](const VmObject *object) -> WorldNode * {
    const auto *state = nativeResourceState(object, arena);
    if (!state)
      return nullptr;
    const int32_t handle =
        static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
    const auto it = game.bindings.find(handle);
    return it == game.bindings.end() || !it->second
               ? nullptr
               : game.world->nodeFor(it->second->link.owner);
  };
  auto *source = resolve(targetObject);
  auto *target = resolve(receiver);
  // Stock 0048b500 loads the linked source before subscribing.
  if (!source) {
    const auto *state = nativeResourceState(targetObject, arena);
    if (state) {
      const int32_t handle =
          static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
      const auto binding = game.bindings.find(handle);
      if (binding != game.bindings.end() && binding->second &&
          binding->second->link.owner) {
        if (game.resources) {
          const auto resolved =
              game.resources->resolve(binding->second->link.owner);
          if (resolved.packNode && resolved.type == 1 &&
              (!resolved.packNode->loaded() ||
               resolved.packNode->groundFactory)) {
            if (!resolved.packNode->loaded()) {
              if (const auto error =
                      loadGroundGameResource(*arena, *resolved.packNode))
                return "GameType.addNotification: " + *error;
            }
            if (resolved.packNode->groundFactory) {
              if (!target)
                return std::string{
                    "GameType.addNotification: receiver unavailable"};
              auto text = [&](size_t index, std::string &out) -> bool {
                const auto *object = std::get<const VmObject *>(args[index]);
                if (!object)
                  return true;
                const auto *bytes = stringBytes(*arena, object);
                if (!bytes)
                  return false;
                out = bytes->substr(0, bytes->find('\0'));
                return true;
              };
              auto entry = std::make_unique<EventSubscription>();
              entry->mask = static_cast<uint32_t>(std::get<int32_t>(args[1]));
              entry->replacementMask =
                  static_cast<uint32_t>(std::get<int32_t>(args[2]));
              if (!text(3, entry->replacementPayload) ||
                  (args.size() == 5 && !text(4, entry->method)))
                return std::string{
                    "GameType.addNotification: string unavailable"};
              setLinkOwner(entry->target, target);
              auto &entries = resolved.packNode->eventSubscriptions;
              entries.erase(std::remove_if(entries.begin(), entries.end(),
                                           [&](const auto &old) {
                                             return old->target.owner ==
                                                        target &&
                                                    old->mask == entry->mask;
                                           }),
                            entries.end());
              entries.push_back(std::move(entry));
              return std::monostate{};
            }
            return std::string{"GameType.addNotification: type-1 instance "
                               "construction unsupported"};
          }
        }
        return std::string{"GameType.addNotification: target load unsupported"};
      }
      return std::monostate{};
    }
    return std::monostate{};
  }
  if (!source->loaded)
    return std::string{"GameType.addNotification: target load unsupported"};
  if (!target)
    return std::string{"GameType.addNotification: receiver unavailable"};
  auto text = [&](size_t index, std::string &out) -> bool {
    const auto *object = std::get<const VmObject *>(args[index]);
    if (!object)
      return true;
    const auto *bytes = stringBytes(*arena, object);
    if (!bytes)
      return false;
    out = bytes->substr(0, bytes->find('\0'));
    return true;
  };
  auto entry = std::make_unique<EventSubscription>();
  entry->mask = static_cast<uint32_t>(std::get<int32_t>(args[1]));
  entry->replacementMask = static_cast<uint32_t>(std::get<int32_t>(args[2]));
  if (!text(3, entry->replacementPayload) ||
      (args.size() == 5 && !text(4, entry->method)))
    return std::string{"GameType.addNotification: string unavailable"};
  setLinkOwner(entry->target, target);
  auto &entries = source->eventSubscriptions;
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&](const auto &old) {
                                 return old->target.owner == target &&
                                        old->mask == entry->mask;
                               }),
                entries.end());
  entries.push_back(std::move(entry));
  source->eventSubscriptionsKnownEmpty = false;
  return std::monostate{};
}

namespace {
const CarPrototype *nodePrototype(const WorldNode &node, GameWorld &world);

// FUN_00443d40 writes ext_camera as zoom, move offset, look offset, rotation,
// then distance. FUN_0048ce30 sends look from the look offset and move from the
// move offset.
SeatCameraPose seatCameraPoseFromDriver(const CarSeat &seat,
                                        const CarPrototype *prototype,
                                        uint32_t target) {
  SeatCameraPose pose;
  pose.target = target;
  const CarDriver *driver = nullptr;
  if (prototype && seat.cameraPreset >= 0 &&
      static_cast<size_t>(seat.cameraPreset) < prototype->config.drivers.size())
    driver = &prototype->config.drivers[static_cast<size_t>(seat.cameraPreset)];
  if (driver && driver->hasExtCamera) {
    const auto &v = driver->extCamera;
    const float distanceScale = prototype->cameraSettings
                                    ? prototype->cameraSettings->distanceScale
                                    : 0.0f;
    const float fovScale = prototype->cameraSettings
                               ? prototype->cameraSettings->fieldOfViewScale
                               : 0.0f;
    pose.moveOffset = {v[1], v[2], v[3]};
    pose.lookOffset = {v[4], v[5], v[6]};
    pose.moveRotation = {v[7], v[8], v[9]};
    pose.moveDistance = v[10] * distanceScale;
    pose.zoom = v[0] * fovScale;
    pose.hasZoom = pose.zoom > 0.0f;
  } else if (driver) {
    // FUN_004445f0 defaults. The car fields at +0x126c, +0x1270, +0x1274, and
    // +0x1278 stay 0.
    pose.moveRotation = {0.0f, -1.0f, -10.0f};
    pose.zoom = 100.0f;
    pose.hasZoom = true;
  }
  return pose;
}

std::optional<std::string> applySeatCameraPose(ResourceWorld &resources,
                                               WorldNode &cameraNode,
                                               const SeatCameraPose &pose) {
  if (!cameraNode.cameraDevice)
    return std::nullopt;
  const auto found = resources.lookup(pose.target, 0);
  if (found.status == LookupStatus::StockCrash ||
      found.status == LookupStatus::MountRefused)
    return "GameRef.queueEvent: car activate camera pose lookup: " +
           found.detail;
  LinkOwner *owner =
      found.status == LookupStatus::Found ? found.node.owner : nullptr;
  setLinkOwner(cameraNode.cameraDevice->lookTarget, owner);
  setLinkOwner(cameraNode.cameraDevice->moveTarget, owner);
  CameraFollowCommand look;
  look.look = true;
  look.target = pose.target;
  look.offset = pose.lookOffset;
  look.direction = {0.0f, 0.0f, -1.0f};
  look.value = pose.lookValue;
  applyCameraFollowCommand(cameraNode.cameraDevice->commands, look);
  CameraFollowCommand move;
  move.target = pose.target;
  move.offset = pose.moveOffset;
  move.value = pose.moveDistance;
  move.direction = pose.moveRotation;
  applyCameraFollowCommand(cameraNode.cameraDevice->commands, move);
  if (pose.hasZoom) {
    char text[64];
    std::snprintf(text, sizeof text, "zoom %f", static_cast<double>(pose.zoom));
    applyCameraCommand(cameraNode.cameraDevice->commands, text);
  }
  const auto& commands = cameraNode.cameraDevice->commands;
  runlog::event(runlog::EventKind::Note,
                "seat camera pose: camera=0x%x target=0x%x look=%.2f,%.2f,%.2f move=%.2f,%.2f,%.2f distance=%.2f",
                cameraNode.instanceId, pose.target,
                commands.lookOffset[0], commands.lookOffset[1], commands.lookOffset[2],
                commands.moveOffset[0], commands.moveOffset[1], commands.moveOffset[2],
                commands.moveDistance);
  runlog::event(runlog::EventKind::Note,
                "seat camera zoom: camera=0x%x current=%.2f target=%.2f speed=%.2f deviceFov=%.2f",
                cameraNode.instanceId, commands.zoomCurrent, commands.zoomTarget,
                commands.zoomSpeed, cameraNode.cameraDevice->fieldOfView);
  return std::nullopt;
}
} // namespace

namespace {
struct GroundEventPending {
  OwnerLink target;
  uint32_t event = 0;
  std::string payload, method;
  ~GroundEventPending() { setLinkOwner(target, nullptr); }
};
struct GroundEventLink {
  OwnerLink link;
  ~GroundEventLink() { setLinkOwner(link, nullptr); }
};
} // namespace

NativeResult gameRefQueueEvent(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *classes) {
  if (!arena || !arena->gameState || !arena->gameState->world)
    return std::string{"GameRef.queueEvent: no game world state"};
  if (args.size() != 3 || !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]) ||
      !std::holds_alternative<const VmObject *>(args[2]))
    return std::string{"invalid GameRef.queueEvent call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"GameRef.queueEvent: receiver has no native link"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second->link.owner)
    return std::monostate{};
  auto *node = game.world->nodeFor(binding->second->link.owner);
  if (!node && game.resources) {
    const auto target = game.resources->resolve(binding->second->link.owner);
    if (target.packNode && target.type == 1 && target.packNode->groundFactory) {
      auto &ground = *target.packNode;
      if (!ground.loaded())
        return std::string{"GameRef.queueEvent: target loading is unsupported"};
      const uint32_t event = static_cast<uint32_t>(std::get<int32_t>(args[1]));
      std::vector<std::unique_ptr<GroundEventPending>> pending;
      GroundEventLink source;
      auto *sourceOwner = target.owner;
      if (const auto *sender = std::get<const VmObject *>(args[0])) {
        const auto *senderState = nativeResourceState(sender, arena);
        if (!senderState)
          return std::string{"GameRef.queueEvent: invalid ground sender"};
        const int32_t senderHandle =
            static_cast<int32_t>(senderState - arena->resourceStates.data()) +
            1;
        const auto senderBinding = game.bindings.find(senderHandle);
        if (senderBinding == game.bindings.end() || !senderBinding->second ||
            !senderBinding->second->link.owner)
          return std::string{"GameRef.queueEvent: ground sender unavailable"};
        sourceOwner = senderBinding->second->link.owner;
      }
      setLinkOwner(source.link, sourceOwner);
      const auto *object = std::get<const VmObject *>(args[2]);
      const auto *bytes = object ? stringBytes(*arena, object) : nullptr;
      if (!bytes)
        return std::string{
            "GameRef.queueEvent: ground command string unavailable"};
      const std::string original = bytes->substr(0, bytes->find('\0'));
      for (const auto &entry : ground.eventSubscriptions) {
        if (!entry->target.owner || !(entry->mask & event))
          continue;
        if ((entry->mask & 0xc0000000u) ||
            ((entry->replacementMask & 0x0fffffffu) &&
             (entry->replacementMask & 0x0fffffffu) != 0x10))
          return std::string{
              "GameRef.queueEvent: ground notification flags unsupported"};
        auto item = std::make_unique<GroundEventPending>();
        setLinkOwner(item->target, entry->target.owner);
        item->event = (entry->replacementMask & 0x0fffffffu)
                          ? entry->replacementMask & 0x0fffffffu
                          : event;
        item->payload = (entry->replacementMask & 0x10000000u)
                            ? entry->replacementPayload
                            : original;
        item->method = entry->method;
        pending.push_back(std::move(item));
      }
      if (event != 0x10)
        return std::string{"GameRef.queueEvent: ground event unsupported"};
      const auto command = stockScan(*bytes, "%s");
      if ((ground.eventMask & event) &&
          (command.values.empty() || (command.values[0].s != "obs_add" &&
                                      command.values[0].s != "obs_rem")))
        return "GameRef.queueEvent: unsupported ground command: " + *bytes;
      auto &factory = *ground.groundFactory;
      const auto parsed = stockScan(*bytes, "%*s %d");
      const uint32_t id = parsed.values.empty()
                              ? 0u
                              : static_cast<uint32_t>(parsed.values[0].i);
      if ((ground.eventMask & event) && factory.roadWorld &&
          command.values[0].s == "obs_add" && id) {
        const auto found = game.resources->lookup(id, 1);
        if (found.status == LookupStatus::StockCrash ||
            found.status == LookupStatus::MountRefused)
          return "GameRef.queueEvent: ground observer lookup: " + found.detail;
        auto observer = std::make_unique<OwnerLink>();
        if (found.status == LookupStatus::Found)
          setLinkOwner(*observer, found.node.owner);
        factory.observers.push_back(std::move(observer));
      } else if ((ground.eventMask & event) && factory.roadWorld &&
                 command.values[0].s == "obs_rem") {
        const auto found = std::find_if(
            factory.observers.begin(), factory.observers.end(),
            [id](const auto &observer) { return observer->cachedValue == id; });
        if (found != factory.observers.end()) {
          setLinkOwner(**found, nullptr);
          factory.observers.erase(found);
        }
      }
      for (const auto &item : pending) {
        if (!source.link.owner)
          return std::string{"GameRef.queueEvent: ground source destroyed"};
        auto *subscriber = game.world->nodeFor(item->target.owner);
        if (!subscriber)
          continue;
        if (!subscriber->loaded || !subscriber->nativeDevice.empty() ||
            !subscriber->eventSubscriptionsKnownEmpty)
          return std::string{
              "GameRef.queueEvent: ground notification target unsupported"};
        if (!(subscriber->eventMask & item->event) ||
            !subscriber->scriptReceiver)
          continue;
        const VmField *mode = nullptr;
        for (const auto &field : subscriber->scriptReceiver->fields)
          if (field.name == "asyncMode" && field.descriptor == "I") {
            if (mode)
              return std::string{
                  "GameRef.queueEvent: ambiguous notification asyncMode"};
            mode = &field;
          }
        if (!mode || !std::holds_alternative<int32_t>(mode->value) ||
            !arena->callScript || !classes)
          return std::string{
              "GameRef.queueEvent: ground callback context unavailable"};
        auto reference = vmAllocateNative(*arena, "java.util.resource.GameRef");
        if (!reference.ok ||
            !std::holds_alternative<const VmObject *>(reference.value))
          return "GameRef.queueEvent: ground source allocation: " +
                 reference.error;
        const auto *ref = std::get<const VmObject *>(reference.value);
        if (!std::holds_alternative<std::monostate>(
                invokeNative("java.util.resource.ResourceRef", "newNative",
                             "()V", {}, ref, arena, classes)))
          return std::string{
              "GameRef.queueEvent: ground source link unavailable"};
        auto *resourceState = nativeResourceState(ref, arena);
        if (!resourceState || !source.link.owner || !item->target.owner)
          return std::string{
              "GameRef.queueEvent: ground notification target destroyed"};
        const int32_t h =
            static_cast<int32_t>(resourceState - arena->resourceStates.data()) +
            1;
        auto receiverBinding = std::make_unique<ReceiverBinding>();
        setLinkOwner(receiverBinding->link, source.link.owner);
        resourceState->words[2] = receiverBinding->link.cachedValue;
        game.bindings.emplace(h, std::move(receiverBinding));
        const auto text = internStringResult(item->payload, arena, classes);
        if (!std::holds_alternative<VmValue>(text))
          return std::string{
              "GameRef.queueEvent: ground notification string unavailable"};
        const auto *textObject =
            std::get_if<const VmObject *>(&std::get<VmValue>(text));
        if (!textObject)
          return std::string{
              "GameRef.queueEvent: invalid ground notification string"};
        const std::string method =
            !item->method.empty()
                ? item->method
                : (std::get<int32_t>(mode->value) ? "handleEventAsync"
                                                  : "handleEvent");
        auto called = arena->callScript(
            subscriber->scriptReceiver, method,
            {ref, static_cast<int32_t>(item->event), *textObject});
        if (!called.ok)
          return "GameRef.queueEvent: ground notification " + method + ": " +
                 called.error;
      }
      return std::monostate{};
    }
  }
  if (!node || !node->loaded)
    return std::string{"GameRef.queueEvent: target loading is unsupported"};
  if (node->nativeDevice == "camera") {
    if (!node->eventSubscriptionsKnownEmpty)
      return std::string{
          "GameRef.queueEvent: camera subscriptions unsupported"};
    if (!(node->eventMask & static_cast<uint32_t>(std::get<int32_t>(args[1]))))
      return std::monostate{};
    if (std::get<int32_t>(args[1]) != 0x10)
      return std::string{"GameRef.queueEvent: camera event unsupported"};
    if (!node->cameraDevice || !node->cameraDevice->physicsNode() ||
        game.world->dynamicBody(*node->cameraDevice->physicsNode()).expired())
      return std::string{"GameRef.queueEvent: camera physics unavailable"};
    const auto *object = std::get<const VmObject *>(args[2]);
    const auto *bytes = object ? stringBytes(*arena, object) : nullptr;
    if (!bytes)
      return std::string{
          "GameRef.queueEvent: camera command string unavailable"};
    const auto name = stockScan(*bytes, "%s");
    CameraFollowCommand follow;
    if (parseCameraFollowCommand(*bytes, node->cameraDevice->commands.torque,
                                 follow)) {
      if (!game.resources)
        return std::string{
            "GameRef.queueEvent: camera follow target lookup unavailable"};
      const auto target = game.resources->lookup(follow.target, 0);
      if (target.status == LookupStatus::StockCrash ||
          target.status == LookupStatus::MountRefused)
        return "GameRef.queueEvent: camera follow target lookup: " +
               target.detail;
      setLinkOwner(follow.look ? node->cameraDevice->lookTarget
                               : node->cameraDevice->moveTarget,
                   target.status == LookupStatus::Found ? target.node.owner
                                                        : nullptr);
      applyCameraFollowCommand(node->cameraDevice->commands, follow);
      const char *followBlock = nullptr;
      if (!game.world->directCameraFollowReady(*node->cameraDevice,
                                               &followBlock)) {
        const std::string block = followBlock ? followBlock : "";
        // One target set so far: the device is not armed yet and the
        // follow step skips it (integrateArmedCameraFollows); the script
        // sets the other target next. Log the partial-arm gap once.
        if (block == "look" || block == "move") {
          runlog::event(runlog::EventKind::Note,
                        "gap: camera follow with only one target set is not "
                        "simulated (missing %s)",
                        block.c_str());
          return std::monostate{};
        }
        return std::string{
                   "GameRef.queueEvent: camera follow simulation unsupported "
                   "(block "} +
               (followBlock ? followBlock : "?") + ")";
      }
      return std::monostate{};
    }
    if (!name.values.empty() && name.values[0].s == "control") {
      if (!game.resources)
        return std::string{
            "GameRef.queueEvent: camera controller lookup unavailable"};
      const auto parsed = stockScan(*bytes, "%*s %d");
      const uint32_t id = parsed.values.empty()
                              ? 0u
                              : static_cast<uint32_t>(parsed.values[0].i);
      const auto target = game.resources->lookup(id, 1);
      if (target.status == LookupStatus::StockCrash ||
          target.status == LookupStatus::MountRefused)
        return "GameRef.queueEvent: camera controller lookup: " + target.detail;
      setLinkOwner(node->cameraDevice->controller,
                   target.status == LookupStatus::Found ? target.node.owner
                                                        : nullptr);
      return std::monostate{};
    }
    if (!name.values.empty() && name.values[0].s == "render") {
      if (!game.resources || !game.renderer)
        return std::string{
            "GameRef.queueEvent: camera render host unavailable"};
      const auto parsed = stockScan(*bytes, "%*s %d %d %d %d %d");
      if (parsed.values.empty() || parsed.values[0].i == 0)
        return std::string{
            "GameRef.queueEvent: camera render viewport unavailable"};
      const auto sector = game.resources->resolve(node->sectorLink.owner);
      if (!sector || !sector.packNode || !sector.packNode->groundFactory)
        return std::string{
            "GameRef.queueEvent: camera sector fog state unavailable"};
      const auto range = vmActiveStaticField(*arena, "java.util.Config",
                                             "camera_ext_viewrange");
      if (!range || !std::holds_alternative<float>(*range) ||
          !std::isfinite(std::get<float>(*range)))
        return std::string{"GameRef.queueEvent: camera view range unavailable"};
      auto *camera = node->cameraDevice->cameraNode();
      if (!camera || !camera->camera)
        return std::string{"GameRef.queueEvent: camera resource unavailable"};
      applyCameraSectorEnvironment(*camera->camera,
                                   *sector.packNode->groundFactory,
                                   std::get<float>(*range));
      const auto viewport =
          game.resources->lookup(static_cast<uint32_t>(parsed.values[0].i), 18);
      if (viewport.status != LookupStatus::Found || !viewport.node.worldNode)
        return std::string{"GameRef.queueEvent: camera viewport unavailable"};
      uint32_t priority = parsed.values.size() > 3
                              ? static_cast<uint32_t>(parsed.values[3].i)
                              : 0;
      if (priority > 0xffff0000u)
        priority = 0;
      int32_t signedPriority;
      std::memcpy(&signedPriority, &priority, sizeof priority);
      std::string error;
      if (!node->cameraDevice->attachViewport(
              *game.resources, *game.renderer, *viewport.node.worldNode,
              signedPriority, std::get<float>(*range), error))
        return "GameRef.queueEvent: " + error;
      return std::monostate{};
    }
    if (!name.values.empty() && name.values[0].s == "hide") {
      const auto parsed = stockScan(*bytes, "%*s %d");
      if (parsed.values.empty() || parsed.values[0].i == 0)
        return std::monostate{};
      if (!game.resources)
        return std::string{"GameRef.queueEvent: camera hide host unavailable"};
      const auto viewport =
          game.resources->lookup(static_cast<uint32_t>(parsed.values[0].i), 18);
      if (viewport.status != LookupStatus::Found || !viewport.node.worldNode)
        return std::string{
            "GameRef.queueEvent: camera hide viewport unavailable"};
      std::string error;
      if (!node->cameraDevice->detachViewport(*viewport.node.worldNode, error))
        return "GameRef.queueEvent: " + error;
      return std::monostate{};
    }
    if (!name.values.empty() && name.values[0].s == "deactivate") {
      // Stock FUN_0043a4b0 clears the active flag (+0x10 bit 0) and unlinks
      // the camera from its owner's step list; the port has no active flag
      // or step list for camera devices, so this is a logged gap.
      static bool reported = false;
      if (!reported) {
        reported = true;
        runlog::event(runlog::EventKind::Note,
                      "gap: camera deactivate does not unlink the camera "
                      "from stepping (FUN_0043a4b0)");
      }
      return std::monostate{};
    }
    if (!applyCameraCommand(node->cameraDevice->commands, *bytes))
      return "GameRef.queueEvent: unsupported camera command: " + *bytes;
    if (!name.values.empty() && name.values[0].s == "zoom") {
      const auto& commands = node->cameraDevice->commands;
      runlog::event(runlog::EventKind::Note,
                    "camera zoom command: camera=0x%x current=%.2f target=%.2f speed=%.2f",
                    node->instanceId, commands.zoomCurrent, commands.zoomTarget,
                    commands.zoomSpeed);
    }
    return std::monostate{};
  }
  if (node->nativeDevice.empty() && node->eventSubscriptionsKnownEmpty &&
      !(node->eventMask & static_cast<uint32_t>(std::get<int32_t>(args[1]))))
    return std::monostate{};
  if (node->nativeDevice == "cursor" && node->cursor &&
      std::get<int32_t>(args[1]) == 0x10 && (node->eventMask & 0x10)) {
    for (const auto &entry : node->eventSubscriptions)
      if (entry->target.owner && (entry->mask & 0x10))
        return std::string{
            "GameRef.queueEvent: cursor command subscriptions unsupported"};
    const auto *object = std::get<const VmObject *>(args[2]);
    const auto *bytes = object ? stringBytes(*arena, object) : nullptr;
    if (!bytes)
      return std::string{
          "GameRef.queueEvent: cursor command string unavailable"};
    std::istringstream input(bytes->substr(0, bytes->find('\0')));
    std::string command;
    input >> command;
    if (command == "cursor") {
      if (!game.resources)
        return std::string{
            "GameRef.queueEvent: cursor resource world unavailable"};
      int32_t values[3]{};
      for (auto &value : values) {
        if (!(input >> value)) {
          if (input.eof())
            break;
          return std::string{
              "GameRef.queueEvent: unsupported cursor arguments"};
        }
      }
      OwnerLink *links[] = {&node->cursor->viewport, &node->cursor->camera,
                            &node->cursor->target};
      const uint32_t types[] = {18, 0, 1};
      for (size_t i = 0; i < 3; ++i) {
        const uint32_t id = static_cast<uint32_t>(values[i]);
        if (links[i]->cachedValue == id)
          continue;
        auto found = game.resources->lookup(id, types[i]);
        if (found.status == LookupStatus::StockCrash ||
            found.status == LookupStatus::MountRefused)
          return "GameRef.queueEvent: cursor lookup: " + found.detail;
        setLinkOwner(*links[i], found.status == LookupStatus::Found
                                    ? found.node.owner
                                    : nullptr);
      }
      return std::monostate{};
    }
    if (command == "move" || command == "relmove") {
      float x = 0, y = 0;
      char comma = 0;
      if (!(input >> x >> comma >> y) || comma != ',' || !std::isfinite(x) ||
          !std::isfinite(y))
        return "GameRef.queueEvent: invalid cursor motion: " + *bytes;
      if (node->cursor->pointer.owner)
        return std::string{
            "GameRef.queueEvent: cursor pointer bone update unsupported"};
      if (command == "relmove") {
        x += node->cursor->x;
        y += node->cursor->y;
      }
      node->cursor->x = std::clamp(x, -1.0f, 1.0f);
      node->cursor->y = std::clamp(y, -1.0f, 1.0f);
      return std::monostate{};
    }
    if (command == "activate") {
      if (!game.resources)
        return std::string{
            "GameRef.queueEvent: cursor resource world unavailable"};
      int32_t ids[2]{};
      for (auto &id : ids) {
        if (!(input >> id)) {
          if (input.eof())
            break;
          return std::string{"GameRef.queueEvent: invalid cursor activation"};
        }
      }
      OwnerLink *links[] = {&node->cursor->controller,
                            &node->cursor->activationTarget};
      for (size_t i = 0; i < 2; ++i) {
        const auto id = static_cast<uint32_t>(ids[i]);
        if (links[i]->cachedValue == id)
          continue;
        auto found = game.resources->lookup(id, 1);
        if (found.status == LookupStatus::StockCrash ||
            found.status == LookupStatus::MountRefused)
          return "GameRef.queueEvent: cursor activation lookup: " +
                 found.detail;
        setLinkOwner(*links[i], found.status == LookupStatus::Found
                                    ? found.node.owner
                                    : nullptr);
      }
      node->cursor->active = true;
      return std::monostate{};
    }
    if (command == "deactivate") {
      setLinkOwner(node->cursor->controller, nullptr);
      node->cursor->active = false;
      return std::monostate{};
    }
    if (command == "enable") {
      std::string error;
      if (game.world->systemCursor() && !(node->cursor->flags & 1) &&
          !game.world->changeCursorVisibility(true, error))
        return "GameRef.queueEvent: " + error;
      node->cursor->flags |= 1;
      return std::monostate{};
    }
    if (command == "disable" || command == "standby") {
      std::string error;
      if (game.world->systemCursor() && (node->cursor->flags & 1) &&
          !game.world->changeCursorVisibility(false, error))
        return "GameRef.queueEvent: " + error;
      node->cursor->flags &= ~uint32_t{5};
      if (command == "standby")
        node->cursor->flags |= 4;
      setLinkOwner(node->cursor->hoveredBox, nullptr);
      setLinkOwner(node->cursor->pressedBox, nullptr);
      return std::monostate{};
    }
    if (command == "mode") {
      int32_t mode = 0, styleId = 0;
      char glyph = 0;
      if (!(input >> mode >> glyph >> styleId))
        return std::string{"GameRef.queueEvent: unsupported cursor mode"};
      // Stock 00462...: the glyph character picks the host cursor handle
      // through FUN_004b7bd0; unlisted characters select cursor 0.
      int32_t hostCursor = 0;
      switch (glyph) {
      case '1': case 'G': hostCursor = 1; break;
      case '2': case 'M': hostCursor = 2; break;
      case '3': case 'W': hostCursor = 3; break;
      case '4': case 'A': hostCursor = 4; break;
      case '5': case 'D': hostCursor = 5; break;
      case '6': hostCursor = 6; break;
      case '7': hostCursor = 7; break;
      case '8': hostCursor = 8; break;
      case '9': hostCursor = 9; break;
      default: break;
      }
      if (!game.resources || !game.world->cursorSelector())
        return std::string{"GameRef.queueEvent: cursor host unavailable"};
      if (node->cursor->pointer.owner)
        return std::string{"GameRef.queueEvent: deferred cursor pointer "
                           "destruction unsupported"};
      const auto id = static_cast<uint32_t>(styleId);
      auto found = game.resources->lookup(id, 14);
      if (found.status == LookupStatus::StockCrash ||
          found.status == LookupStatus::MountRefused)
        return "GameRef.queueEvent: cursor style lookup: " + found.detail;
      std::string cursorError;
      if (!game.world->cursorSelector()(hostCursor,
                                        game.world->systemCursor() != 0,
                                        cursorError))
        return "GameRef.queueEvent: cursor host: " + cursorError;
      node->cursor->mode = mode;
      node->cursor->glyph = glyph;
      if (id != 0 && node->cursor->style.cachedValue != id)
        setLinkOwner(node->cursor->style, found.status == LookupStatus::Found
                                              ? found.node.owner
                                              : nullptr);
      return std::monostate{};
    }
    return "GameRef.queueEvent: unsupported cursor command: " + *bytes +
           " (SysCursor=" + std::to_string(game.world->systemCursor()) + ")";
  }
  // Stock creates this device and lets it set up its event mask. The port has
  // only its name, so the recorded mask cannot establish whether it receives
  // the event.
  if (node->nativeDevice == "player" && node->playerInput &&
      node->eventSubscriptionsKnownEmpty && node->scriptReceiver &&
      std::get<int32_t>(args[1]) == 0x10 && (node->eventMask & 0x10)) {
    const auto *object = std::get<const VmObject *>(args[2]);
    const auto *bytes = object ? stringBytes(*arena, object) : nullptr;
    if (!bytes)
      return std::string{
          "GameRef.queueEvent: player command string unavailable"};
    std::istringstream input(bytes->substr(0, bytes->find('\0')));
    std::string command;
    input >> command;
    if (command == "setcontrol" || command == "device") {
      if (!arena->callScript)
        return std::string{"GameRef.queueEvent: script callback unavailable"};
      std::vector<VmValue> arguments;
      const char *method = "setcontrol";
      if (command == "device") {
        int32_t device = 0;
        input >> device;
        if (input.fail() && !input.eof())
          return std::string{
              "GameRef.queueEvent: unsupported player device number"};
        method = "addDeviceMapping";
        arguments.emplace_back(device);
      }
      const uint32_t instanceId = node->instanceId;
      const auto result =
          arena->callScript(node->scriptReceiver, method, std::move(arguments));
      if (!result.ok)
        return "GameRef.queueEvent: " + std::string(method) + ": " +
               result.error;
      const auto *after = game.world->findInstance(instanceId);
      if (after != node || !after->eventSubscriptionsKnownEmpty)
        return std::string{
            "GameRef.queueEvent: subscriptions changed during callback"};
      return std::monostate{};
    }
    if (command == "controllable") {
      int32_t rawId = 0;
      input >> rawId;
      if (input.fail())
        return std::string{"GameRef.queueEvent: invalid controllable id"};
      auto *target = game.world->findInstance(static_cast<uint32_t>(rawId));
      if (!target || target->nativeDevice != "car" || !target->carInstance ||
          !target->carInstance->isCar || !node->player)
        return std::string{"GameRef.queueEvent: controllable car unavailable"};
      auto &player = *node->player;
      if (player.controllable.owner == target)
        return std::monostate{};
      if (player.controllable.owner && player.controllable.owner != target)
        return std::string{
            "GameRef.queueEvent: switching controllable unsupported"};
      if (player.camera.owner || player.osd.owner || player.renderTarget.owner)
        return std::string{"GameRef.queueEvent: controllable activation camera "
                           "or osd unsupported"};
      if (!(target->eventMask & 0x10))
        return std::string{
            "GameRef.queueEvent: controllable car event mask unsupported"};
      if ((!target->eventSubscriptionsKnownEmpty &&
           target->eventSubscriptions.empty()) ||
          std::any_of(target->eventSubscriptions.begin(),
                      target->eventSubscriptions.end(), [](const auto &entry) {
                        return entry && entry->target.owner;
                      }))
        return std::string{
            "GameRef.queueEvent: controllable car subscriptions unsupported"};
      auto *seat = target->carInstance->selectSeat(node->instanceId);
      if (!seat)
        return std::string{
            "GameRef.queueEvent: controllable car has no available seat"};
      auto collision = std::make_unique<EventSubscription>();
      collision->mask = 1;
      setLinkOwner(collision->target, node);
      target->eventSubscriptions.clear();
      setLinkOwner(seat->player, node);
      setLinkOwner(seat->camera, nullptr);
      setLinkOwner(seat->renderInstance, player.renderInstance.owner);
      setLinkOwner(seat->osd, nullptr);
      char damage[64];
      std::snprintf(damage, sizeof damage, "%.3f",
                    static_cast<double>(player.config.damageMultiplier));
      const auto parsedDamage = stockScan(damage, "%f");
      target->carInstance->damageMultiplier =
          parsedDamage.values.empty() ? 1.0f : parsedDamage.values[0].f;
      target->eventSubscriptions.push_back(std::move(collision));
      target->eventSubscriptionsKnownEmpty = false;
      node->eventMask |= 1;
      setLinkOwner(player.controllable, target);
      game.world->logViewportAttachments();
      if (seat->renderInstance.cachedValue) {
        auto *playerRender =
            game.world->findInstance(seat->renderInstance.cachedValue);
        runlog::event(
            runlog::EventKind::Note,
            "player render instance: id=0x%x node=%p type=%u loaded=%d",
            seat->renderInstance.cachedValue, (const void *)playerRender,
            playerRender ? static_cast<unsigned>(playerRender->type) : 0u,
            playerRender && playerRender->loaded ? 1 : 0);
      }
      if (seat->renderInstance.owner && game.resources) {
        const auto source = game.resources->resolve(target->carInstance->prototypeSource.owner);
        const auto *prototype = source.script ? source.script->car.get() : nullptr;
        if (prototype)
          activateSeatedDriver(*game.resources, *game.world, *target, *target->carInstance,
                               *seat, prototype->config);
      }
      return std::monostate{};
    }
    if (command == "viewport" || command == "camera") {
      if (!node->player || !game.resources)
        return std::string{
            "GameRef.queueEvent: player camera state unavailable"};
      const auto parsed = stockScan(*bytes, "%*s %d");
      const uint32_t id = parsed.values.empty()
                              ? 0u
                              : static_cast<uint32_t>(parsed.values[0].i);
      const auto target =
          game.resources->lookup(id, command == "viewport" ? 18 : 1);
      if (target.status == LookupStatus::StockCrash ||
          target.status == LookupStatus::MountRefused)
        return "GameRef.queueEvent: player target lookup: " + target.detail;
      LinkOwner *owner =
          target.status == LookupStatus::Found ? target.node.owner : nullptr;
      auto &player = *node->player;
      if (command == "viewport") {
        setLinkOwner(player.viewport, owner);
        if (player.renderTarget.owner && player.viewport.owner)
          return std::string{
              "GameRef.queueEvent: player render event unsupported"};
        return std::monostate{};
      }
      setLinkOwner(player.camera, owner);
      if (owner) {
        auto *camera = game.world->nodeFor(owner);
        if (!camera || camera->nativeDevice != "camera" ||
            !camera->cameraDevice || !camera->loaded)
          return std::string{
              "GameRef.queueEvent: player camera target unsupported"};
        if (!camera->eventSubscriptionsKnownEmpty)
          return std::string{
              "GameRef.queueEvent: camera subscriptions unsupported"};
        if (camera->eventMask & 0x10) {
          auto *physics = camera->cameraDevice->physicsNode();
          if (!physics || game.world->dynamicBody(*physics).expired())
            return std::string{
                "GameRef.queueEvent: camera physics unavailable"};
          setLinkOwner(camera->cameraDevice->controller, node);
        }
      }
      if (player.renderTarget.owner && player.renderTarget.owner != owner)
        return std::string{"GameRef.queueEvent: player hide event unsupported"};
      if (player.controllable.owner) {
        auto *car = game.world->nodeFor(player.controllable.owner);
        auto *camera = owner ? game.world->nodeFor(owner) : nullptr;
        if (!car || !car->carInstance || car->nativeDevice != "car" ||
            !camera || !camera->cameraDevice)
          return std::string{"GameRef.queueEvent: player camera activation "
                             "target unavailable"};
        auto *target = game.world->nodeFor(car->carInstance->physicsRoot.owner);
        if (!target || target->type != 2)
          return std::string{
              "GameRef.queueEvent: player camera car physics unavailable"};
        if (camera->eventMask & 0x10) {
          CameraFollowCommand look;
          look.look = true;
          look.target = target->instanceId;
          look.direction = {0, 0, -1};
          look.value = -1;
          setLinkOwner(camera->cameraDevice->lookTarget, target);
          applyCameraFollowCommand(camera->cameraDevice->commands, look);
          CameraFollowCommand move;
          move.target = target->instanceId;
          move.value = 3.0f; // stock player initial +0x21c0
          setLinkOwner(camera->cameraDevice->moveTarget, target);
          applyCameraFollowCommand(camera->cameraDevice->commands, move);
        }
        auto *seat = car->carInstance->findSeat(node->instanceId);
        if (!seat)
          return std::string{
              "GameRef.queueEvent: player camera seat unavailable"};
        setLinkOwner(seat->camera, owner);
        size_t occupied = 0, cameras = 0, seatIndex = 0;
        for (size_t i = 0; i < car->carInstance->seats.size(); ++i) {
          const auto &candidate = car->carInstance->seats[i];
          if (candidate.get() == seat)
            seatIndex = i;
          if (candidate && candidate->player.owner)
            ++occupied;
          if (candidate && candidate->camera.owner)
            ++cameras;
        }
        if (cameras && cameras != occupied)
          return std::string{
              "GameRef.queueEvent: mixed seat camera activation unsupported"};
        const auto source =
            game.resources->resolve(car->carInstance->prototypeSource.owner);
        const auto *prototype =
            source.script ? source.script->car.get() : nullptr;
        if (!prototype || !prototype->cameraSettings ||
            seatIndex >= prototype->config.drivers.size())
          return std::string{
              "GameRef.queueEvent: seat camera configuration unavailable"};
        if ((camera->eventMask & 0x10) &&
            !camera->cameraDevice->applySeatFollow(
                *target, prototype->config.drivers[seatIndex],
                *prototype->cameraSettings))
          return std::string{
              "GameRef.queueEvent: seat camera configuration unsupported"};
        const char *block = "none";
        if (!game.world->directCameraFollowReady(*camera->cameraDevice,
                                                 &block)) {
          runlog::event(
              runlog::EventKind::Note,
              "camfollow1 "
              "{\"v\":1,\"p\":%u,\"c\":%u,\"car\":%u,\"body\":%u,\"look\":%u,"
              "\"move\":%u,\"r\":%u,\"reason\":\"sim\",\"w\":\"%s\"}",
              node->instanceId, camera->instanceId, car->instanceId,
              target->instanceId, camera->cameraDevice->lookTarget.cachedValue,
              camera->cameraDevice->moveTarget.cachedValue,
              game.world->dynamicBody(*target).expired() ? 0u : 1u,
              block ? block : "none");
          return std::string{"GameRef.queueEvent: player camera follow "
                             "simulation unsupported"};
        }
        return std::monostate{};
      }
      if (player.renderTarget.owner && player.viewport.owner)
        return std::string{
            "GameRef.queueEvent: player render event unsupported"};
      return std::monostate{};
    }
    if (command == "renderinstance") {
      if (!node->player || !game.resources)
        return std::string{
            "GameRef.queueEvent: player renderinstance lookup unavailable"};
      const auto parsed = stockScan(*bytes, "%*s %d");
      const uint32_t id = parsed.values.empty()
                              ? 0u
                              : static_cast<uint32_t>(parsed.values[0].i);
      if (node->player->renderInstance.cachedValue == id)
        return std::monostate{};
      if (!id) {
        setLinkOwner(node->player->renderInstance, nullptr);
        return std::monostate{};
      }
      const auto target = game.resources->lookup(id, 0);
      if (target.status == LookupStatus::StockCrash ||
          target.status == LookupStatus::MountRefused)
        return "GameRef.queueEvent: player renderinstance lookup: " +
               target.detail;
      setLinkOwner(node->player->renderInstance,
                   target.status == LookupStatus::Found ? target.node.owner
                                                        : nullptr);
      return std::monostate{};
    }
    if (command == "osd") {
      // Stock FUN_0042df70: a different id is linked with FUN_00546070(id, 1,
      // 0), then `activate 0x%x 0x%x 0x%x` is queued on the current
      // controllable. The three words are the player instance (device +8), the
      // camera id
      // (+0x218c) and the render-instance id (+0x219c).
      if (!node->player || !game.resources)
        return std::string{"GameRef.queueEvent: player osd state unavailable"};
      const auto parsed = stockScan(*bytes, "%*s %d");
      const uint32_t id = parsed.values.empty()
                              ? 0u
                              : static_cast<uint32_t>(parsed.values[0].i);
      auto &player = *node->player;
      if (player.osd.cachedValue == id)
        return std::monostate{};
      const auto target = game.resources->lookup(id, 1);
      {
        static bool osdNodeLogged = false;
        if (!osdNodeLogged) {
          auto *osdByWorld = game.world->findInstance(id);
          osdNodeLogged = true;
          runlog::event(
              runlog::EventKind::Note,
              "osd node: id=0x%x lookupFound=%d lookupWorldNode=%p "
              "worldFind=%p type=%u loaded=%d class=%s",
              id, target.status == LookupStatus::Found ? 1 : 0,
              target.status == LookupStatus::Found
                  ? (const void *)target.node.worldNode
                  : nullptr,
              (const void *)osdByWorld,
              osdByWorld ? static_cast<unsigned>(osdByWorld->type) : 0u,
              osdByWorld && osdByWorld->loaded ? 1 : 0,
              osdByWorld && osdByWorld->scriptReceiver &&
                      osdByWorld->scriptReceiver->runtimeClass
                  ? osdByWorld->scriptReceiver->runtimeClass->name.c_str()
                  : "-");
        }
      }
      if (target.status == LookupStatus::StockCrash ||
          target.status == LookupStatus::MountRefused)
        return "GameRef.queueEvent: player osd lookup: " + target.detail;
      setLinkOwner(player.osd, target.status == LookupStatus::Found
                                   ? target.node.owner
                                   : nullptr);
      {
        static bool osdLogged = false;
        if (!osdLogged) {
          osdLogged = true;
          const char *osdName =
              target.status == LookupStatus::Found && target.node.packNode
                  ? target.node.packNode->name.c_str()
                  : "";
          runlog::event(runlog::EventKind::Note,
                        "player osd command: id=0x%x status=%s name=%s", id,
                        target.status == LookupStatus::Found
                            ? "found"
                            : target.detail.c_str(),
                        osdName);
        }
      }
      if (!player.controllable.owner)
        return std::string{
            "GameRef.queueEvent: player osd controllable unavailable"};
      auto *car = game.world->nodeFor(player.controllable.owner);
      if (!car || !car->scriptReceiver)
        return std::string{
            "GameRef.queueEvent: player osd controllable unavailable"};
      const auto *carState = nativeResourceState(car->scriptReceiver, arena);
      if (!carState)
        return std::string{
            "GameRef.queueEvent: player osd controllable receiver unavailable"};
      const auto carHandle =
          static_cast<int32_t>(carState - arena->resourceStates.data()) + 1;
      const auto carBinding = game.bindings.find(carHandle);
      if (carBinding == game.bindings.end() || !carBinding->second ||
          !carBinding->second->link.owner)
        return std::string{
            "GameRef.queueEvent: player osd controllable receiver unavailable"};
      char activate[96];
      std::snprintf(activate, sizeof activate, "activate 0x%x 0x%x 0x%x",
                    static_cast<unsigned>(node->instanceId),
                    static_cast<unsigned>(player.camera.cachedValue),
                    static_cast<unsigned>(player.renderInstance.cachedValue));
      const auto text = internStringResult(activate, arena, classes);
      if (!std::holds_alternative<VmValue>(text))
        return std::string{
            "GameRef.queueEvent: player osd activate string unavailable"};
      const auto *textObject =
          std::get_if<const VmObject *>(&std::get<VmValue>(text));
      if (!textObject || !*textObject)
        return std::string{
            "GameRef.queueEvent: player osd activate string unavailable"};
      const VmObject *sender = nullptr;
      const auto queued = gameRefQueueEvent(
          {VmValue{sender}, VmValue{int32_t{0x10}}, VmValue{*textObject}},
          car->scriptReceiver, arena, classes);
      for (const auto &textLink : player.debugText) {
        if (!textLink.cachedValue || !textLink.owner)
          continue;
        auto *textNode = game.world->nodeFor(textLink.owner);
        if (!textNode || !game.world->queueDestroy(*textNode))
          return std::string{
              "GameRef.queueEvent: player osd debug text release unsupported"};
      }
      if (const auto *err = std::get_if<std::string>(&queued))
        return *err;
      return std::monostate{};
    }
    if (command == "leave" || command == "control" || command == "release" ||
        command == "display") {
      return std::monostate{};
    }
  }
  if ((node->nativeDevice == "car" || node->nativeDevice == "part") &&
      node->carInstance && std::get<int32_t>(args[1]) == 0x10 &&
      (node->eventMask & 0x10)) {
    const auto *object = std::get<const VmObject *>(args[2]);
    const auto *bytes = object ? stringBytes(*arena, object) : nullptr;
    if (!bytes)
      return std::string{
          "GameRef.queueEvent: car/part command string unavailable"};
    std::istringstream input(bytes->substr(0, bytes->find('\0')));
    std::string command;
    input >> command;
    auto &inst = *node->carInstance;
    if (inst.isCar && command == "damage_multiplier") {
      const auto parsed = stockScan(*bytes, "%*s %f");
      inst.damageMultiplier = parsed.values.empty() ? 1.0f : parsed.values[0].f;
      return std::monostate{};
    }
    if (inst.isCar && (command == "abs" || command == "asr")) {
      // FUN_00458c00 stores reciprocal thresholds; nonpositive disables.
      const auto parsed = stockScan(*bytes, "%*s %f");
      const float value = parsed.values.empty() ? 0.0f : parsed.values[0].f;
      (command == "abs" ? inst.abs : inst.asr) = value > 0 ? 1.0f / value : 0.0f;
      return std::monostate{};
    }
    if (inst.isCar && command == "activate") {
      // FUN_00458c00 links the three ids, calls getInfo(0x1a), and stores the
      // seat camera mode. FUN_00449680 then calls FUN_0048ce30, records the
      // missing driver skeleton, and clears empty link slots. A seat osd id of
      // 0 skips cockpit text and gauges. A seated player then needs getInfo
      // 0x15, which this path does not implement.
      if (!game.resources || !game.world)
        return std::string{
            "GameRef.queueEvent: car activate resource world unavailable"};
      const auto parsed = stockScan(*bytes, "%*s %d %d %d");
      const auto word = [&](size_t index) {
        return parsed.values.size() > index
                   ? static_cast<uint32_t>(parsed.values[index].i)
                   : 0u;
      };
      const uint32_t playerId = word(0);
      const uint32_t cameraId = word(1);
      const uint32_t renderId = word(2);
      CarSeat *seat = inst.findSeat(playerId);
      const bool alreadySeated = seat != nullptr;
      if (!seat)
        seat = inst.findSeat(0);
      if (!seat)
        return std::string{"GameRef.queueEvent: car activate seat unavailable"};
      auto linkType1 = [&](OwnerLink &link,
                           uint32_t id) -> std::optional<std::string> {
        if (link.cachedValue == id)
          return std::nullopt;
        if (id == 0) {
          setLinkOwner(link, nullptr);
          return std::nullopt;
        }
        const auto found = game.resources->lookup(id, 1);
        if (found.status == LookupStatus::StockCrash ||
            found.status == LookupStatus::MountRefused)
          return "GameRef.queueEvent: car activate lookup: " + found.detail;
        if (found.status == LookupStatus::Found)
          setLinkOwner(link, found.node.owner);
        else if (link.owner)
          setLinkOwner(link, nullptr);
        return std::nullopt;
      };
      if (const auto err = linkType1(seat->player, playerId))
        return *err;
      if (const auto err = linkType1(seat->camera, cameraId))
        return *err;
      if (!alreadySeated && seat->renderInstance.cachedValue != 0)
        return std::string{"GameRef.queueEvent: car activate driver pose "
                           "teardown unsupported"};
      const auto render =
          resourceRefSet(*game.resources, seat->renderInstance, renderId);
      if (render.status == RefSetStatus::StockCrash ||
          render.status == RefSetStatus::MountRefused)
        return "GameRef.queueEvent: car activate render lookup: " +
               render.detail;
      auto *playerNode = game.world->nodeFor(seat->player.owner);
      if (playerNode && seat->player.cachedValue != 0 && playerNode->loaded) {
        if (playerNode->type == 8)
          return std::string{
              "GameRef.queueEvent: car activate driver pose unsupported"};
        if (playerNode->type == 1 && playerNode->scriptReceiver) {
          if (!arena->callScript)
            return std::string{
                "GameRef.queueEvent: car activate player getInfo unsupported"};
          const auto answer = arena->callScript(
              playerNode->scriptReceiver, "getInfo", {VmValue{int32_t{0x1a}}});
          if (!answer.ok)
            return "GameRef.queueEvent: car activate player getInfo: " +
                   answer.error;
        }
      }
      // FUN_00426470(seat+4, 0x1a, seat+0x54) reaches the native player's
      // getInfo case 0x1a (FUN_0047b220), which relinks the seat's osd record
      // to the player's osd link (player +0x21b0, id +0x21ac).
      if (playerNode && playerNode->player && playerNode->loaded)
        setLinkOwner(seat->osd, playerNode->player->osd.owner);
      // FUN_00448e60. Seat count is the car's seat list. Stock reads that count
      // from the event receiver at +0x177c and writes the mode on the car.
      int32_t playerIds = 0;
      int32_t cameraIds = 0;
      {
        static bool seatIdsLogged = false;
        if (!seatIdsLogged) {
          int32_t p0 = 0, c0 = 0;
          for (const auto &s : inst.seats) {
            if (s && s->player.cachedValue)
              ++p0;
            if (s && s->camera.cachedValue)
              ++c0;
          }
          seatIdsLogged = true;
          runlog::event(
              runlog::EventKind::Note,
              "car activate seat ids: seats=%zu players=%d cameras=%d",
              inst.seats.size(), p0, c0);
        }
      }
      const auto seatCount = static_cast<int32_t>(inst.seats.size());
      for (int32_t i = 0; i < seatCount; ++i) {
        const auto &candidate = inst.seats[static_cast<size_t>(i)];
        if (!candidate)
          continue;
        if (candidate->player.cachedValue != 0)
          ++playerIds;
        if (candidate->camera.cachedValue != 0)
          ++cameraIds;
      }
      if (cameraIds != 0 && cameraIds != playerIds) {
        inst.seatCameraMode = 1;
        inst.seatCameraSeat = 0;
        inst.seatCameraMarker = -1;
        for (int32_t i = 0; i < seatCount; ++i) {
          const auto &candidate = inst.seats[static_cast<size_t>(i)];
          if (candidate && candidate->player.cachedValue != 0 &&
              candidate->camera.cachedValue != 0)
            inst.seatCameraSeat = i;
        }
        inst.seatCameraMarker = -1;
      } else {
        inst.seatCameraMode = 0;
      }
      int32_t seatIndex = 0;
      for (int32_t i = 0; i < seatCount; ++i)
        if (inst.seats[static_cast<size_t>(i)].get() == seat)
          seatIndex = i;
      const auto *poseSeat =
          inst.seats[static_cast<size_t>(
                         inst.seatCameraMode ? inst.seatCameraSeat : seatIndex)]
              .get();
      // FUN_0048ce30 returns before sending commands when the seat camera id is
      // 0.
      const auto *prototype = nodePrototype(*node, *game.world);
      if (poseSeat && poseSeat->camera.cachedValue != 0) {
        if (inst.seatCameraMode) {
          if (!inst.carPhysics || !inst.carPhysics->root ||
              !inst.carPhysics->root->proto || !inst.physicsRoot.owner)
            return std::string{
                "GameRef.queueEvent: car activate chase body unavailable"};
          const float radius = inst.carPhysics->root->proto->shape.boundRadius;
          const float scale = prototype && prototype->cameraSettings
                                  ? prototype->cameraSettings->distanceScale
                                  : 0.0f;
          SeatCameraPose pose;
          pose.target = inst.physicsRoot.cachedValue;
          pose.lookOffset = {0.0f, 0.0f, -20.0f};
          pose.moveDistance = 0.01f;
          pose.moveRotation = {0.0f, -(radius * 0.1f + 1.0f),
                               -(radius + scale)};
          pose.hasZoom = true;
          pose.zoom = 100.0f;
          inst.seatCameraPose = pose;
        } else {
          inst.seatCameraPose = seatCameraPoseFromDriver(
              *poseSeat, prototype, inst.physicsRoot.cachedValue);
        }
        if (auto *cameraNode = game.world->nodeFor(poseSeat->camera.owner))
          if (const auto err = applySeatCameraPose(*game.resources, *cameraNode,
                                                   *inst.seatCameraPose))
            return *err;
      }
      if (prototype && seat->renderInstance.owner)
        activateSeatedDriver(*game.resources, *game.world, *node, inst, *seat, prototype->config);
      // FUN_00449680 tears down old seat-local cockpit instances before
      // rebuilding them, including when the next activation has no OSD.
      for (OwnerLink& link : seat->links) {
        if (auto* old = game.world->nodeFor(link.owner))
          game.world->destroy(*old);
        setLinkOwner(link, nullptr);
        link.cachedValue = 0;
      }
      seat->hudText.fill(nullptr);
      seat->hudModels.fill(nullptr);
      // Seat osd id is stock +0x5c. Zero skips cockpit text and gauges. The
      // other arm creates them only for the active driver, param_3 == +0x1780.
      const int32_t activeDriver =
          prototype ? prototype->config.defaultDriver : 0;
      if (seat->osd.cachedValue != 0 && seatIndex == activeDriver) {
        // FUN_00446180 defaults and FUN_00449680 seat-local creation.
        // FUN_0048c670 uses the same material-backed text path as Text.create.
        uint16_t carsSlot = 0;
        std::string hudError;
        if (!game.packs->registerPackName("cars.rpk", carsSlot, hudError))
          return "HUD font pack: " + hudError;
        const auto parent = game.resources->resolve(seat->osd.owner);
        if (!parent.worldNode)
          return std::string{"HUD parent unavailable"};
        const uint32_t pack = uint32_t(carsSlot) << 16;
        // FUN_00446180 default model transforms. The source atlas/SCX owns
        // the cluster arc, red zone, legends and right-hand silhouette panel.
        if (!carHudModel(*game.resources, *game.world, *seat, 0, pack | 0x55,
                         {3.24f, -2.4f, -3.59f}, 0, true, hudError) ||
            !carHudModel(*game.resources, *game.world, *seat, 1, pack | 0x56,
                         {1.43f, -4.36f, -3.58f},
                         carHudNeedle(inst.displayedEngineSpeed * 9.549296f), true, hudError))
          return hudError;
        std::array<CarHudElement, 13> elements{};
        elements[0] = {0x58, 2, 0.55f, 0.86f};
        elements[1] = carHudSpeed();
        elements[2] = carHudScore();
        // FUN_00449680 uses composite panel-font glyphs A/B/C/D/E/T/L/O.
        // Each glyph includes its own panel position; they share this origin.
        for (size_t i = 3; i < 11; ++i)
          elements[i] = {0x57, 2, 0.93f, 0.8f, 0xffffffff};
        elements[11] = {0x5c, 2, -0.17f, 0.8f, 0xff000000, "%01.0f", 0, 8, 0, 8};
        elements[12] = {0x5c, 2, -0.17f, 0.91f, 0xff000000, "%01.0f", 0, 8, 0, 8};
        constexpr std::array<size_t, 13> hudSlots{25, 29, 24, 1, 4, 5, 3, 6, 7, 21, 8, 22, 23};
        for (size_t i = 0; i < elements.size(); ++i) {
          auto& link = seat->links[hudSlots[i]];
          if (seat->hudText[i] && link.owner)
            continue;
          const uint32_t fontId = (uint32_t(carsSlot) << 16) | elements[i].material;
          const auto font = game.resources->lookup(fontId, 14);
          if (font.status != LookupStatus::Found)
            return std::string{"HUD font unavailable"};
          auto material = game.resources->sceneMaterial(font.node);
          if (!material) {
            auto loaded = sceneLoadMaterial(*game.resources, fontId);
            if (!loaded.material)
              return "HUD font: " + loaded.error;
            material = std::move(loaded.material);
            if (!game.resources->bindSceneMaterial(font.node, material))
              return std::string{"HUD font binding failed"};
          }
          SceneMaterialCache fontCache;
          if (!sceneRefreshMaterial(*game.resources, *material,
                                    game.packs->root(), fontCache, hudError, false, true))
            return "HUD font preparation: " + hudError;
          auto* text = game.world->addTypedChild(*parent.worldNode, 3,
                                                 "r_text", 0x200);
          if (!text)
            return std::string{"HUD text allocation failed"};
          text->bounds = ResourceBounds{};
          text->field6c = parent.worldNode->field6c;
          text->field70 = parent.worldNode->field70;
          game.world->markLoaded(*text);
          text->loadPriority = 1.0f;
          auto render = std::make_shared<SceneRenderInstance>(font.node.owner);
          render->setBoneTranslation(elements[i].x, elements[i].y, 0);
          const std::string initial = i == 0             ? carHudGear(inst.currentGear)
                                      : i < 3 || i >= 11 ? ""
                                                         : std::string(1, "ABCDETLO"[i - 3]);
          render->setTextState({initial, 2, elements[i].color, 1.0f});
          if (i >= 3 && i < 11)
            render->setInstanceColor(0xff00ff00); // FUN_0048c8c0 initial healthy color.
          if (!game.resources->bindSceneRenderInstance(
                  game.resources->resolve(text), render))
            return std::string{"HUD text binding failed"};
          seat->hudText[i] = std::move(render);
          setLinkOwner(link, text);
        }
        runlog::event(runlog::EventKind::Note,
                      "HUD right panel: per-part damage-color refresh and wheel diagnostic glyphs not implemented");
      }
      // FUN_00448d70 (all.c:57388): clear car +0x1edc, scan seats for the
      // first nonzero player id (+0x128, stride 0x4a8), relink the car's
      // active-player list at +0x1ee0, and store the player device's message
      // 0x15 reply. The reply is player +0x242c, a debug value written only
      // by console AI commands, so a fresh run stores 0. Stock never fails
      // here; an unlinked or non-player seat just stores 0. The +0x1ee0 list
      // head itself has no host consumer yet.
      inst.activateInfo = 0;
      for (int32_t i = 0; i < seatCount; ++i) {
        const auto &candidate = inst.seats[static_cast<size_t>(i)];
        if (!candidate || candidate->player.cachedValue == 0)
          continue;
        if (auto *occupiedPlayer = game.world->nodeFor(candidate->player.owner))
          if (occupiedPlayer->nativeDevice == "player" &&
              occupiedPlayer->player)
            inst.activateInfo = occupiedPlayer->player->debugAiValue;
        break;
      }
      return std::monostate{};
    }
    if (command == "install") {
      int32_t targetId = 0, slotId = 0;
      const auto parsed = stockScan(*bytes, "%*s %d %d %d %d %d %f %f %f");
      if (parsed.values.size() > 3)
        targetId = parsed.values[3].i;
      if (parsed.values.size() > 4)
        slotId = parsed.values[4].i;
      // FUN_00458c00 binds the second integer to part +0x60 before
      // FUN_00473b10 searches that parent and its children. Its position
      // argument is null even if the command contains trailing coordinates.
      if (!game.resources)
        return std::string{"GameRef.queueEvent: install resource world unavailable"};
      const uint32_t parentId = parsed.values.size() > 1
                                    ? static_cast<uint32_t>(parsed.values[1].i)
                                    : 0;
      auto &parentLink = node->carInstance->installParentLink;
      if (parentLink.cachedValue != parentId) {
        auto parent = game.resources->lookup(parentId, 1);
        if (parent.status == LookupStatus::StockCrash ||
            parent.status == LookupStatus::MountRefused)
          return "GameRef.queueEvent: install parent lookup: " + parent.detail;
        setLinkOwner(parentLink, parent.status == LookupStatus::Found
                                     ? parent.node.owner : nullptr);
      }
      WorldNode *targetNode =
          targetId ? game.world->findInstance(static_cast<uint32_t>(targetId))
                   : nullptr;
      std::string err;
      const bool installed =
          carPartInstall(*node, targetNode, slotId, *arena, err);
      runlog::event(
          runlog::EventKind::Note,
          "part install: part=0x%08x target=0x%08x slot=%d outcome=%s",
          node->instanceId, static_cast<uint32_t>(targetId), slotId,
          installed ? "ok" : (err.empty() ? "rejected" : "error"));
      if (!installed && !err.empty())
        return "GameRef.queueEvent: install: " + err;
      return std::monostate{};
    }
    if (command == "remove") {
      int32_t slotId = 0;
      input >> std::setbase(0) >> slotId;
      std::string err;
      const bool removed = carPartRemove(*node, slotId, *arena, err);
      runlog::event(runlog::EventKind::Note,
                    "part remove: part=0x%08x slot=%d outcome=%s",
                    node->instanceId, slotId,
                    removed ? "ok" : (err.empty() ? "rejected" : "error"));
      if (!removed && !err.empty())
        return "GameRef.queueEvent: remove: " + err;
      return std::monostate{};
    }
    if (command == "repair") {
      inst.wear = 1.0f;
      inst.tear = 1.0f;
      if (node->nativeDevice == "car") {
        for (const auto &partLink : inst.attachedParts) {
          if (auto *partNode = game.world->nodeFor(partLink->owner)) {
            if (partNode->carInstance) {
              partNode->carInstance->wear = 1.0f;
              partNode->carInstance->tear = 1.0f;
            }
            if (partNode->scriptReceiver && arena->callScript) {
              const auto *cls = partNode->scriptReceiver->runtimeClass;
              for (const auto *c = cls; c;) {
                bool hasRepair = false;
                for (const auto &list : c->methods)
                  for (const auto &m : list)
                    if (c->constants[m.words[1]].text == "repair" &&
                        c->constants[m.words[2]].text == "()V")
                      hasRepair = true;
                if (hasRepair) {
                  auto r =
                      arena->callScript(partNode->scriptReceiver, "repair", {});
                  if (!r.ok)
                    return "GameRef.queueEvent: repair: " + r.error;
                  break;
                }
                const auto p = c->classHeader[3];
                if (p == UINT32_MAX || p >= c->constants.size() || !classes)
                  break;
                const auto &pName = c->constants[c->constants[p].a].text;
                c = nullptr;
                for (const auto *cand : *classes)
                  if (cand && cand->name == pName)
                    c = cand;
              }
            }
          }
        }
      }
      return std::monostate{};
    }
    if (command == "texture") {
      int32_t texId = 0;
      input >> std::setbase(0) >> texId;
      inst.textureId = static_cast<uint32_t>(texId);
      if (node->nativeDevice == "car") {
        for (const auto &partLink : inst.attachedParts) {
          if (auto *partNode = game.world->nodeFor(partLink->owner)) {
            if (partNode->carInstance)
              partNode->carInstance->textureId = static_cast<uint32_t>(texId);
          }
        }
      }
      return std::monostate{};
    }
    if (command == "mesh") {
      int32_t meshId = 0;
      input >> std::setbase(0) >> meshId;
      inst.meshId = static_cast<uint32_t>(meshId);
      return std::monostate{};
    }
    if (command == "colorscheme") {
      int32_t schemeId = 0;
      input >> std::setbase(0) >> schemeId;
      inst.colorIndex = schemeId;
      // FUN_00475ec0: for every body record, when its colour list is
      // non-empty, FUN_0048c8c0 writes colors[scheme % count] onto that
      // body's render resource (+0xcc). The render resource is the body's
      // `renderResource`, which the instance already carries as renderType.
      inst.bodyTints.clear();
      if (game.resources && inst.prototypeSource.owner) {
        const auto proto =
            game.resources->resolve(inst.prototypeSource.owner);
        if (proto.script && proto.script->car) {
          // FUN_00475ec0 walks every indexed body; each record with colours
          // gets its own render resource written (+0xcc).
          const auto &config = proto.script->car->config;
          for (const auto &body : config.bodies) {
            if (body.kind != CarBodyRecord::Render || body.colors.empty())
              continue;
            const size_t index =
                static_cast<size_t>(schemeId < 0 ? 0 : schemeId) %
                body.colors.size();
            // The renderer looks tints up by the remapped resource id
            // (CarRuntimeInstance::renderType is remapped likewise).
            uint32_t key = body.renderResource;
            PackView view;
            std::string mapErr;
            if (game.resources->packs().view(
                    static_cast<uint16_t>(proto.script->resourceId >> 16),
                    view, mapErr) == PackViewStatus::Ok)
              key = remapResourceId(
                  key, ResourceIdMapping{
                           view.slot,
                           static_cast<uint32_t>(
                               game.resources->packs().slotCount()),
                           view.dependencies});
            inst.bodyTints[key] =
                static_cast<uint32_t>(body.colors[index]);
          }
        }
      }
      return std::monostate{};
    }
    if (command == "add_part" || command == "rem_part") {
      int32_t partId = 0;
      input >> std::setbase(0) >> partId;
      WorldNode *partNode =
          game.world->findInstance(static_cast<uint32_t>(partId));
      if (!partNode)
        return std::monostate{};
      if (command == "add_part") {
        bool exists = false;
        for (const auto &link : inst.attachedParts)
          if (link->owner == partNode)
            exists = true;
        if (!exists) {
          auto link = std::make_unique<OwnerLink>();
          setLinkOwner(*link, partNode);
          inst.attachedParts.push_back(std::move(link));
        }
      } else {
        // Detach each removed target before its destruction: the link still
        // sits on the part's LinkOwner list.
        for (auto it = inst.attachedParts.begin();
             it != inst.attachedParts.end();) {
          if (!(*it)->owner || (*it)->owner == partNode) {
            setLinkOwner(**it, nullptr);
            it = inst.attachedParts.erase(it);
          } else {
            ++it;
          }
        }
      }
      if (node->scriptReceiver && arena->callScript && classes) {
        auto refVal = wrapResourceReference(partNode, arena, classes);
        if (const auto *val = std::get_if<VmValue>(&refVal)) {
          const char *method = command == "add_part" ? "addpart" : "rempart";
          for (const auto *c = node->scriptReceiver->runtimeClass; c;) {
            bool found = false;
            for (const auto &list : c->methods)
              for (const auto &m : list)
                if (c->constants[m.words[1]].text == method)
                  found = true;
            if (found) {
              auto r = arena->callScript(node->scriptReceiver, method, {*val});
              if (!r.ok)
                return "GameRef.queueEvent: " + std::string(method) + ": " +
                       r.error;
              break;
            }
            const auto p = c->classHeader[3];
            if (p == UINT32_MAX || p >= c->constants.size())
              break;
            const auto &pName = c->constants[c->constants[p].a].text;
            c = nullptr;
            for (const auto *cand : *classes)
              if (cand && cand->name == pName)
                c = cand;
          }
        }
      }
      return std::monostate{};
    }
    if (command == "updatevariables") {
      if (node->scriptReceiver && arena->callScript && classes) {
        for (const auto *c = node->scriptReceiver->runtimeClass; c;) {
          bool found = false;
          for (const auto &list : c->methods)
            for (const auto &m : list)
              if (c->constants[m.words[1]].text == "updatevariables" &&
                  c->constants[m.words[2]].text == "()V")
                found = true;
          if (found) {
            auto r =
                arena->callScript(node->scriptReceiver, "updatevariables", {});
            if (!r.ok)
              return "GameRef.queueEvent: updatevariables: " + r.error;
            break;
          }
          const auto p = c->classHeader[3];
          if (p == UINT32_MAX || p >= c->constants.size())
            break;
          const auto &pName = c->constants[c->constants[p].a].text;
          c = nullptr;
          for (const auto *cand : *classes)
            if (cand && cand->name == pName)
              c = cand;
        }
      }
      return std::monostate{};
    }
    if (command == "sethorn") {
      int32_t hornVal = 0;
      input >> std::setbase(0) >> hornVal;
      inst.hornOn = hornVal;
      return std::monostate{};
    }
    if (command == "setvel") {
      float vx = 0, vy = 0, vz = 0, wx = 0, wy = 0, wz = 0;
      input >> vx >> vy >> vz >> wx >> wy >> wz;
      inst.linearVelocity = {vx, vy, vz};
      inst.angularVelocity = {wx, wy, wz};
      return std::monostate{};
    }
    if (command == "transmission") {
      int32_t tr = 0;
      input >> std::setbase(0) >> tr;
      inst.transmissionMode = tr;
      return std::monostate{};
    }
    if (command == "cruise") {
      inst.cruiseControl = !inst.cruiseControl;
      return std::monostate{};
    }
    if (inst.isCar && (command == "brake" || command == "start")) {
      // 00458c00: brake sets 0x40000; start clears it and 0x4.
      inst.forcedBrake = command == "brake";
      return std::monostate{};
    }
    if (inst.isCar && command == "reset") {
      float before[3]{};
      if (inst.carPhysics && inst.carPhysics->root &&
          inst.carPhysics->root->current < 2)
        for (unsigned i = 0; i < 3; ++i)
          before[i] = inst.carPhysics->root->state[inst.carPhysics->root->current]
                          .vel[i];
      if (inst.carPhysics)
        resetCarToRest(*inst.carPhysics);
      static unsigned resets = 0;
      if (resets++ < 8) {
        float length[4]{};
        if (inst.carPhysics)
          for (size_t i = 0; i < inst.carPhysics->wheels.size() && i < 4; ++i)
            if (inst.carPhysics->wheels[i])
              length[i] = inst.carPhysics->wheels[i]->suspension.length;
        runlog::event(runlog::EventKind::Note,
                      "car command 'reset' #%u: root velocity before=(%.2f,%.2f,"
                      "%.2f), suspension lengths=(%.3f,%.3f,%.3f,%.3f); "
                      "velocities zeroed, wheel slip cleared, body asleep; "
                      "steering geometry (0044f4e0/0044fa70) and controller "
                      "state resets are not ported",
                      resets, before[0], before[1], before[2], length[0],
                      length[1], length[2], length[3]);
      }
      return std::monostate{};
    }
    if (inst.isCar && (command == "suspend" || command == "wakeup")) {
      if (inst.carPhysics) {
        if (command == "suspend")
          suspendCar(*inst.carPhysics);
        else
          wakeCar(*inst.carPhysics);
      }
      return std::monostate{};
    }
    if (command == "filter" || command == "reload" || command == "start" ||
        command == "stop" || command == "brake" || command == "setsteer" ||
        command == "steerhelp" || command == "asr" || command == "abs" ||
        command == "esp" || command == "difflock" || command == "render" ||
        command == "hide" || command == "activate" || command == "deactivate" ||
        command == "osd" || command == "damage_multiplier" ||
        command == "control" || command == "lost") {
      static std::set<std::string> reportedNoEffect;
      if (reportedNoEffect.insert(command).second)
        runlog::event(runlog::EventKind::Note,
                      "car command '%s' accepted without effect: not ported",
                      command.c_str());
      return std::monostate{};
    }
    {
      static std::set<std::string> reportedCarCommands;
      if (reportedCarCommands.insert(command).second)
        runlog::event(runlog::EventKind::Note,
                      "car command unrecognized: '%s' (device %s)",
                      command.c_str(), node->nativeDevice.c_str());
    }
  }
  if (!node->nativeDevice.empty())
    return std::string{"GameRef.queueEvent: native device unsupported: "} +
           node->nativeDevice;
  // Stock also traverses subscriptions when the target's own mask rejects an
  // event. Their lifecycle is not yet established for all supported nodes.
  return std::string{"GameRef.queueEvent: event delivery unsupported"};
}

NativeResult gameRefCreate(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *arena,
                           const std::vector<const TufaClass *> *) {
  if (args.size() != 4)
    return std::string{"invalid GameRef.create call"};
  for (const auto &arg : args)
    if (!std::holds_alternative<const VmObject *>(arg))
      return std::string{"invalid GameRef.create call"};
  if (!arena || !arena->gameState || !arena->gameState->world ||
      !arena->gameState->packs || !arena->gameState->resources)
    return std::string{"GameRef.create: missing game world"};
  auto &state = *arena->gameState;
  auto *target = nativeResourceState(receiver, arena);
  if (!target)
    return std::string{"GameRef.create: receiver has no native link"};
  const auto *scriptObject = std::get<const VmObject *>(args[1]);
  const auto *scriptRef = nativeResourceState(scriptObject, arena);
  if (!scriptRef || !scriptRef->words[2])
    return std::string{"GameRef.create: missing script resource link"};
  const uint32_t scriptId = scriptRef->words[2];
  PackResource resource;
  std::string error;
  if (!state.packs->lookup(scriptId, resource, error) || !resource.packBytes ||
      resource.entry.resType != 8 ||
      resource.entry.payloadOffset > resource.packBytes->size() ||
      resource.packBytes->size() - resource.entry.payloadOffset <
          resource.entry.payloadSize)
    return std::string{"GameRef.create: script resource unavailable"};
  const std::string payload(
      resource.packBytes->begin() + resource.entry.payloadOffset,
      resource.packBytes->begin() + resource.entry.payloadOffset +
          resource.entry.payloadSize);
  if (payload != "native cursor\r\n") {
    const int32_t targetHandle =
        static_cast<int32_t>(target - arena->resourceStates.data()) + 1;
    std::optional<uint32_t> parentId;
    if (const auto *parentObject = std::get<const VmObject *>(args[0])) {
      const auto *parentState = nativeResourceState(parentObject, arena);
      if (!parentState)
        return std::string{"GameRef.create: invalid parent"};
      if (!parentState->words[2])
        return VmValue{static_cast<const VmObject *>(nullptr)};
      parentId = parentState->words[2];
    } else
      parentId = state.world->gameParentId();
    if (!parentId)
      return VmValue{static_cast<const VmObject *>(nullptr)};
    const auto *argumentObject = std::get<const VmObject *>(args[2]);
    const auto *nameObject = std::get<const VmObject *>(args[3]);
    const auto *argument =
        argumentObject ? stringBytes(*arena, argumentObject) : nullptr;
    const auto *name = nameObject ? stringBytes(*arena, nameObject) : nullptr;
    if ((argumentObject && !argument) || (nameObject && !name))
      return std::string{"GameRef.create: unreadable string"};
    auto prepared = prepareGameCreation(
        *state.packs, scriptId, parentId,
        argument ? std::optional<std::string>(*argument) : std::nullopt,
        name ? std::optional<std::string>(*name)
             : std::optional<std::string>("_gameinst"),
        arena->loadScriptPath);
    if (!prepared.request)
      return "GameRef.create: " + prepared.error;
    if (prepared.request->scriptClass.empty() &&
        prepared.request->nativeDevice == "camera") {
      auto parent = state.resources->lookup(*parentId, 1);
      if (!parent.node ||
          (parent.node.worldNode && !parent.node.worldNode->loaded))
        return std::string{
            "GameRef.create: camera parent unavailable or unloaded"};
      if (parent.node.packNode && !parent.node.packNode->loaded()) {
        // FUN_0053a5e0: an unloaded parent is loaded synchronously at
        // priority 1.0 before the child is created.
        auto &pack = *parent.node.packNode;
        pack.field6c = 1.0f;
        std::string loadError;
        if (!state.resources->loadEmptyGameParent(pack, loadError) &&
            !state.resources->loadPassiveSpatialGame(pack, loadError)) {
          GameResourcePayload payload;
          std::string inspectError;
          if (state.resources->inspectGamePayload(pack, payload,
                                                  inspectError) &&
              payload.runtimeGameType && *payload.runtimeGameType != 0) {
            if (const auto failure = loadGroundGameResource(*arena, pack))
              loadError = *failure;
            else
              loadError.clear();
          }
          if (!loadError.empty())
            return "GameRef.create: camera parent load unsupported: " +
                   loadError;
        }
        if (!pack.loaded())
          return std::string{
              "GameRef.create: camera parent load did not complete"};
      }
      auto *script = state.world->findScript(scriptId);
      if (!script)
        return std::string{"GameRef.create: camera type resource unavailable"};
      if (!script->cameraPrototype) {
        const auto config = cameraDeviceConfig(*arena);
        if (!config)
          return std::string{"GameRef.create: camera Config unavailable"};
        script->cameraPrototype = makeCameraDevicePrototype(error);
        if (!script->cameraPrototype)
          return "GameRef.create: " + error;
        script->cameraConfig = std::make_shared<CameraDeviceConfig>(*config);
      }
      if (!script->cameraConfig)
        return std::string{
            "GameRef.create: camera factory configuration unavailable"};
      const auto &config = *script->cameraConfig;
      script->loaded = true;
      state.resources->noteScriptLoaded(scriptId);
      const auto descriptor = prepareGameInstanceDescriptor(*prepared.request);
      auto *node =
          parent.node.worldNode
              ? state.world->addChild(*parent.node.worldNode, descriptor.name,
                                      descriptor.flags)
              : state.resources->addGameChild(
                    *parent.node.packNode, descriptor.name, descriptor.flags);
      if (!node)
        return std::string{"GameRef.create: camera game node unavailable"};
      state.world->noteInstanceCreated(*node);
      node->flags |= 0x200;
      state.world->markLoaded(*node);
      node->loadPriority = 1.0f;
      node->nativeDevice = "camera";
      node->eventSubscriptionsKnownEmpty = true;
      setLinkOwner(node->scriptLink, script);
      const auto cameraArgs = parseCameraDeviceArgs(
          prepared.request->constructorArgument.value_or(""),
          config.nearDistance, config.farDistance);
      auto camera = prepareCameraDeviceResources(
          *state.world, *state.resources, *node, config, cameraArgs,
          script->cameraPrototype, error);
      if (!camera) {
        state.world->destroy(*node);
        return "GameRef.create: " + error;
      }
      node->cameraDevice = std::move(camera);
      node->eventMask |= 0x14;
      auto &slot = state.bindings[targetHandle];
      if (!slot)
        slot = std::make_unique<ReceiverBinding>();
      setLinkOwner(slot->link, node);
      arena->resourceStates[static_cast<size_t>(targetHandle) - 1].words[2] =
          node->instanceId;
      return VmValue{static_cast<const VmObject *>(nullptr)};
    }
    if (prepared.request->scriptClass.empty() &&
        prepared.request->nativeDevice == "button") {
      // Stock factory FUN_00436f30 (prototype) and FUN_004373e0 (instance).
      if (!prepared.request->nativeConfigPath)
        return std::string{"GameRef.create: button needs a config path"};
      auto parent = state.resources->lookup(*parentId, 1);
      if (!parent.node || (parent.node.worldNode && !parent.node.worldNode->loaded) ||
          (parent.node.packNode && !parent.node.packNode->loaded()))
        return std::string{"GameRef.create: button parent unavailable or unloaded"};
      ScriptNode *script = state.world->findScript(scriptId);
      if (!script)
        script = state.world->addScript(scriptId, "");
      if (!script)
        return std::string{"GameRef.create: button script allocation failed"};
      std::string buttonError;
      if (!loadButtonScriptPrototype(*state.resources, *script,
                                     *prepared.request->nativeConfigPath,
                                     buttonError))
        return "GameRef.create: button prototype: " + buttonError;
      script->loaded = true;
      state.resources->noteScriptLoaded(scriptId);
      const auto &proto = *script->button;
      const auto desc = prepareGameInstanceDescriptor(*prepared.request);
      WorldNode *node =
          parent.node.worldNode
              ? state.world->addChild(*parent.node.worldNode, desc.name,
                                      desc.flags)
              : state.resources->addGameChild(*parent.node.packNode, desc.name,
                                              desc.flags);
      if (!node)
        return std::string{"GameRef.create: button node allocation failed"};
      state.world->noteInstanceCreated(*node);
      node->flags |= 0x200;
      state.world->markLoaded(*node);
      node->loadPriority = 1.0f;
      node->eventSubscriptionsKnownEmpty = true;
      node->nativeDevice = "button";
      setLinkOwner(node->scriptLink, script);
      // FUN_004373e0: "%f,%f,%f,%f,%f,%f" = position + euler angles.
      const auto placement = stockScan(
          prepared.request->constructorArgument.value_or(""),
          "%f,%f,%f,%f,%f,%f");
      float position[3]{}, ypr[3]{};
      for (size_t i = 0; i < placement.values.size() && i < 6; ++i)
        (i < 3 ? position[i] : ypr[i - 3]) = placement.values[i].f;
      if (proto.renderId) {
        const auto type = state.resources->lookup(proto.renderId);
        if (!type.node || !renderRefCreateTypeTemplate(type.node)) {
          state.world->destroy(*node);
          return std::string{"GameRef.create: button render resource unsupported"};
        }
        WorldNode *render =
            state.world->addTypedChild(*node, 3, "r_object", 0x200);
        if (!render) {
          state.world->destroy(*node);
          return std::string{"GameRef.create: button render allocation failed"};
        }
        render->bounds = node->bounds;
        render->field6c = node->field6c;
        render->field70 = node->field70;
        state.world->markLoaded(*render);
        render->loadPriority = 1.0f;
        auto instance = std::make_shared<SceneRenderInstance>(type.node.owner);
        instance->setBoneMatrix(position, ypr);
        if (!state.resources->bindSceneRenderInstance(
                state.resources->resolve(render), instance)) {
          state.world->destroy(*node);
          return std::string{"GameRef.create: button render binding failed"};
        }
      }
      static bool reportedButtonGap = false;
      if (!reportedButtonGap) {
        reportedButtonGap = true;
        runlog::event(
            runlog::EventKind::Note,
            "gap: button body proto and cursor-event subscription (mask "
            "0x10010, FUN_0048d3d0) are not simulated; render child only");
      }
      auto &slot = state.bindings[targetHandle];
      if (!slot)
        slot = std::make_unique<ReceiverBinding>();
      setLinkOwner(slot->link, node);
      arena->resourceStates[static_cast<size_t>(targetHandle) - 1].words[2] =
          node->instanceId;
      return VmValue{static_cast<const VmObject *>(nullptr)};
    }
    if (!arena->loadScriptPath || !arena->callScript || !arena->ensureClass)
      return std::string{"GameRef.create: script creation context unavailable"};
    if (!prepared.request->nativeDevice.empty() &&
        prepared.request->nativeDevice != "car" &&
        prepared.request->nativeDevice != "part")
      return "GameRef.create: unsupported script native device '" +
             prepared.request->nativeDevice + "' (script resource 0x" +
             [&] {
               char b[16];
               std::snprintf(b, sizeof b, "%08x", scriptId);
               return std::string(b);
             }() +
             ", payload \"" +
             [&] {
               std::string t;
               for (char c : payload) {
                 if (c == '\r')
                   t += "\\r";
                 else if (c == '\n')
                   t += "\\n";
                 else
                   t += c;
               }
               return t;
             }() +
             "\")";
    auto &slot = state.bindings[targetHandle];
    if (!slot)
      slot = std::make_unique<ReceiverBinding>();
    auto &binding = *slot;
    const VmObject *scriptReceiver = nullptr;
    std::string callbackError;
    binding.getInfo = [&](int kind) -> std::optional<int32_t> {
      if (!scriptReceiver)
        return std::nullopt;
      auto result =
          arena->callScript(scriptReceiver, "getInfo", {int32_t{kind}});
      if (!result.ok || result.voidReturn ||
          !std::holds_alternative<int32_t>(result.value)) {
        callbackError = result.error;
        return std::nullopt;
      }
      return std::get<int32_t>(result.value);
    };
    binding.initializeReceiver =
        [&](WorldNode &node) -> std::optional<std::string> {
      auto allocated = vmAllocateNative(*arena, prepared.request->scriptClass);
      if (!allocated.ok ||
          !std::holds_alternative<const VmObject *>(allocated.value))
        return "script allocation: " + allocated.error;
      scriptReceiver = std::get<const VmObject *>(allocated.value);
      VmField *ptr = nullptr;
      for (auto &field : scriptReceiver->fields)
        if (field.name == "ptr" && field.descriptor == "I")
          ptr = &field;
      if (!ptr || !std::holds_alternative<int32_t>(ptr->value) ||
          std::get<int32_t>(ptr->value) != 0)
        return "script receiver ptr unavailable";
      if (arena->resourceStates.size() >= static_cast<size_t>(INT32_MAX))
        return "script receiver handles exhausted";
      const int32_t h = static_cast<int32_t>(arena->resourceStates.size() + 1);
      ptr->value = h;
      arena->resourceStates.push_back({scriptReceiver, {}});
      arena->resourceStates.back().words[2] = node.instanceId;
      auto scriptBinding = std::make_unique<ReceiverBinding>();
      setLinkOwner(scriptBinding->link, &node);
      state.bindings.emplace(h, std::move(scriptBinding));
      node.scriptReceiver = scriptReceiver;
      int32_t scriptIdArgument;
      std::memcpy(&scriptIdArgument, &node.instanceId, sizeof scriptIdArgument);
      auto result =
          arena->callScript(scriptReceiver, "<init>", {scriptIdArgument});
      if (!result.ok)
        return "script constructor: " + result.error;
      return std::nullopt;
    };
    const auto cameraSettings = prepared.request->nativeDevice == "car"
                                    ? carCameraSettings(*arena)
                                    : std::nullopt;
    const auto created = createNativeInstance(
        *state.world, *prepared.request, &binding, state.resources, nullptr,
        cameraSettings ? &*cameraSettings : nullptr);
    binding.getInfo = nullptr;
    binding.initializeReceiver = nullptr;
    if (created.status != CreationStatus::Created)
      return "GameRef.create: " +
             (callbackError.empty() ? created.detail : callbackError);
    arena->resourceStates[static_cast<size_t>(targetHandle) - 1].words[2] =
        created.instanceId;
    return VmValue{scriptReceiver};
  }
  if (!state.world->configNode() || !state.world->configNode()->scriptReceiver)
    return std::string{"GameRef.create: linked Config unavailable"};
  const auto sensitivity =
      vmActiveStaticField(*arena, "java.util.Config", "mouseSensitivity");
  const auto systemCursor =
      vmActiveStaticField(*arena, "java.util.Config", "SysCursor");
  if (!sensitivity || !systemCursor ||
      !std::holds_alternative<float>(*sensitivity) ||
      !std::holds_alternative<int32_t>(*systemCursor))
    return std::string{"GameRef.create: cursor Config fields unavailable"};
  uint32_t fontId = 0;
  if (!state.packs->resolveLiteral("frontend.rpk", 0x23, fontId, error))
    return "GameRef.create: cursor resource unavailable: " + error;
  const auto font = state.resources->lookup(fontId);
  if (!font.node)
    return std::string{"GameRef.create: cursor font link unavailable"};
  std::optional<uint32_t> parentId;
  if (const auto *parentObject = std::get<const VmObject *>(args[0])) {
    const auto *parentState = nativeResourceState(parentObject, arena);
    if (!parentState)
      return std::string{"GameRef.create: parent native link unavailable"};
    if (!parentState->words[2])
      return VmValue{static_cast<const VmObject *>(nullptr)};
    parentId = parentState->words[2];
  } else {
    parentId = state.world->gameParentId();
  }
  if (!parentId)
    return VmValue{static_cast<const VmObject *>(nullptr)};
  const auto parent = state.resources->lookup(*parentId, 1);
  if (!parent.node ||
      (parent.node.worldNode && !parent.node.worldNode->loaded) ||
      (parent.node.packNode && !parent.node.packNode->loaded()))
    return std::string{"GameRef.create: parent unavailable or unloaded"};
  const auto *nameObject = std::get<const VmObject *>(args[3]);
  const auto *name = nameObject ? stringBytes(*arena, nameObject) : nullptr;
  if (nameObject && !name)
    return std::string{"GameRef.create: name string unavailable"};
  const std::string rawName = name ? *name : "_gameinst";
  const std::string instanceName =
      rawName.substr(0, std::min<size_t>(30, rawName.find('\0')));
  ScriptNode *script = state.world->findScript(scriptId);
  if (!script)
    return std::string{"GameRef.create: cursor script node unavailable"};
  script->loaded = true;
  state.resources->noteScriptLoaded(scriptId);
  WorldNode *node =
      parent.node.worldNode
          ? state.world->addChild(*parent.node.worldNode, instanceName, 0x21)
          : state.resources->addGameChild(*parent.node.packNode, instanceName,
                                          0x21);
  if (!node)
    return std::string{"GameRef.create: cursor node allocation failed"};
  state.world->noteInstanceCreated(*node);
  node->flags |= 0x200;
  state.world->markLoaded(*node);
  node->loadPriority = 1.0f;
  node->eventSubscriptionsKnownEmpty = true;
  node->nativeDevice = "cursor";
  node->cursor = std::make_unique<CursorInstance>();
  setLinkOwner(node->cursor->fontA, font.node.owner);
  setLinkOwner(node->cursor->fontB, font.node.owner);
  setLinkOwner(node->scriptLink, script);
  node->eventMask |= 0x10;
  state.world->setCursorConfiguration(std::get<float>(*sensitivity),
                                      std::get<int32_t>(*systemCursor));
  state.world->registerHook(GameHook::Kind1c, *node);
  const int32_t handle =
      static_cast<int32_t>(target - arena->resourceStates.data()) + 1;
  auto &binding = state.bindings[handle];
  if (!binding)
    binding = std::make_unique<ReceiverBinding>();
  setLinkOwner(binding->link, node);
  target->words[2] = node->instanceId;
  return VmValue{static_cast<const VmObject *>(nullptr)};
}

NativeResult controllerUserReset(const std::vector<VmValue> &args,
                                 const VmObject *receiver, VmArena *arena,
                                 const std::vector<const TufaClass *> *) {
  if (!args.empty())
    return std::string{"invalid Controller.user_Reset call"};
  if (!arena || !arena->gameState || !arena->gameState->world)
    return std::string{"Controller.user_Reset: no game world state"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::monostate{};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second->link.owner)
    return std::monostate{};
  auto *node = game.world->nodeFor(binding->second->link.owner);
  if (!node || !node->loaded || !node->playerInput)
    return std::string{"Controller.user_Reset: player device unsupported"};
  node->playerInput->reset();
  return std::monostate{};
}

NativeResult controllerUserDel(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (args.size() != 3 || !std::holds_alternative<int32_t>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]) ||
      !std::holds_alternative<int32_t>(args[2]))
    return std::string{"invalid Controller.user_Del call"};
  if (!arena || !arena->gameState || !arena->gameState->world)
    return std::string{"Controller.user_Del: no game world state"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return VmValue{int32_t{0}};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second->link.owner)
    return VmValue{int32_t{0}};
  auto *node = game.world->nodeFor(binding->second->link.owner);
  if (!node || !node->loaded || !node->playerInput)
    return std::string{"Controller.user_Del: player device unsupported"};
  node->playerInput->remove(std::get<int32_t>(args[0]),
                            std::get<int32_t>(args[1]),
                            std::get<int32_t>(args[2]));
  return VmValue{int32_t{0}};
}

NativeResult
controllerUserSetAxisSmooth(const std::vector<VmValue> &args,
                            const VmObject *receiver, VmArena *arena,
                            const std::vector<const TufaClass *> *) {
  if (args.size() != 6 || !std::holds_alternative<int32_t>(args[0]))
    return std::string{"invalid Controller.user_SetAxisSmooth call"};
  std::array<float, 5> values{};
  for (size_t i = 0; i < values.size(); ++i) {
    const auto *value = std::get_if<float>(&args[i + 1]);
    if (!value)
      return std::string{"invalid Controller.user_SetAxisSmooth call"};
    values[i] = *value;
  }
  if (!arena || !arena->gameState || !arena->gameState->world)
    return std::string{"Controller.user_SetAxisSmooth: no game world state"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return VmValue{int32_t{1}};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second->link.owner)
    return VmValue{int32_t{1}};
  auto *node = game.world->nodeFor(binding->second->link.owner);
  if (!node || !node->loaded || !node->playerInput)
    return std::string{
        "Controller.user_SetAxisSmooth: player device unsupported"};
  if (!node->playerInput->setSmooth(std::get<int32_t>(args[0]), values))
    return std::string{"Controller.user_SetAxisSmooth: axis out of range"};
  return VmValue{int32_t{1}};
}

NativeResult controllerUserAdd(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (args.size() != 7 || !std::holds_alternative<int32_t>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]) ||
      !std::holds_alternative<int32_t>(args[2]))
    return std::string{"invalid Controller.user_Add call"};
  std::array<float, 4> range{};
  for (size_t i = 0; i < range.size(); ++i) {
    const auto *value = std::get_if<float>(&args[i + 3]);
    if (!value)
      return std::string{"invalid Controller.user_Add call"};
    range[i] = *value;
  }
  if (!arena || !arena->gameState || !arena->gameState->world)
    return std::string{"Controller.user_Add: no game world state"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return VmValue{int32_t{0}};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second->link.owner)
    return VmValue{int32_t{0}};
  auto *node = game.world->nodeFor(binding->second->link.owner);
  if (!node || !node->loaded || !node->playerInput)
    return std::string{"Controller.user_Add: player device unsupported"};
  const int32_t axis = std::get<int32_t>(args[0]);
  const int32_t device = std::get<int32_t>(args[1]);
  const int32_t physicalAxis = std::get<int32_t>(args[2]);
  if (device < 0 || physicalAxis < 0)
    return VmValue{int32_t{-1}};
  bool available = false;
  for (const auto &mapping : node->playerInput->mappings())
    available |= mapping.axis == 0;
  if (!available)
    return VmValue{int32_t{0}};
  if (axis < 0 || static_cast<size_t>(axis) >= node->playerInput->axes().size())
    return std::string{"Controller.user_Add: axis out of range"};
  if (!node->playerInput->sampleInput || !node->playerInput->inputType)
    return std::string{"Controller.user_Add: physical input unavailable"};
  const auto sample = node->playerInput->sampleInput(device, physicalAxis);
  if (!sample)
    return "Controller.user_Add: physical input unavailable for device " +
           std::to_string(device) + " axis " + std::to_string(physicalAxis);
  const auto type = node->playerInput->inputType(device, physicalAxis);
  if (!type)
    return std::string{"Controller.user_Add: physical input unavailable"};
  const auto added =
      node->playerInput->add(axis, device, physicalAxis, *sample, range, *type);
  return VmValue{*added};
}

NativeResult inputGetDeviceName(const std::vector<VmValue> &args,
                                const VmObject *, VmArena *arena,
                                const std::vector<const TufaClass *> *classes) {
  if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
    return std::string{"invalid Input.getDeviceName call"};
  if (!arena || !arena->gameState || !arena->gameState->world)
    return std::string{"Input.getDeviceName: no game world state"};
  const auto &devices = arena->gameState->world->inputDevices();
  if (!devices)
    return std::string{
        "Input.getDeviceName: platform device enumeration unavailable"};
  const int32_t index = std::get<int32_t>(args[0]);
  if (index < 0 || static_cast<size_t>(index) >= devices->size())
    return VmValue{static_cast<const VmObject *>(nullptr)};
  const std::string &name = (*devices)[static_cast<size_t>(index)];
  if (name.find('\0') != std::string::npos)
    return std::string{"Input.getDeviceName: embedded NUL in device name"};
  const TufaClass *stringClass = nullptr;
  if (classes)
    for (const auto *candidate : *classes)
      if (candidate && candidate->name == "java.lang.String")
        stringClass = candidate;
  if (!stringClass)
    return std::string{"Input.getDeviceName: String class unavailable"};
  for (const auto &entry : arena->strings)
    if (entry.first == name && entry.second->runtimeClass == stringClass)
      return VmValue{entry.second};
  if (arena->objects.size() >= arena->limit)
    return std::string{"Input.getDeviceName: object arena full"};
  auto object = std::make_unique<VmObject>();
  object->runtimeClass = stringClass;
  const VmObject *identity = object.get();
  arena->objects.push_back(std::move(object));
  arena->strings.emplace_back(name, identity);
  return VmValue{identity};
}

NativeResult gameRefIsScripted(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *classes) {
  if (args.size() != 1 || !std::holds_alternative<const VmObject *>(args[0]) ||
      !arena || !arena->gameState || !arena->gameState->world ||
      !arena->gameState->resources || !classes)
    return std::string{"GameRef.isScripted: invalid context"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"GameRef.isScripted: invalid native link"};
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto &game = *arena->gameState;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second ||
      !binding->second->link.owner)
    return VmValue{int32_t{0}};
  const auto node = game.resources->resolve(binding->second->link.owner);
  if (!node)
    return std::string{"GameRef.isScripted: unresolved node"};
  if (node.type != 1 && node.type != 8)
    return VmValue{int32_t{0}};
  const TufaClass *scriptClass = nullptr;
  const auto *argument = std::get<const VmObject *>(args[0]);
  if (node.type == 1) {
    if (!node.worldNode || !node.worldNode->loaded)
      return std::string{"GameRef.isScripted: resource load path unsupported"};
    auto &instance = *node.worldNode;
    if (!instance.field6c ||
        instance.loadPriority < std::min(*instance.field6c, 1.0f))
      return std::string{
          "GameRef.isScripted: load priority update unsupported"};
    game.world->noteRecentUse(instance, instance.type, instance.stamp74,
                              instance.recentUseQueued);
    if (!instance.scriptReceiver || !instance.scriptReceiver->runtimeClass)
      return VmValue{int32_t{0}};
    scriptClass = instance.scriptReceiver->runtimeClass;
  } else {
    if (!node.packNode || !node.script || !game.packs)
      return std::string{"GameRef.isScripted: script load context unavailable"};
    auto &resource = *node.packNode;
    if (resource.stamp7c < game.world->frameCounter() ||
        (resource.field6c && *resource.field6c < 1.0f))
      resource.field6c = 1.0f;
    else if (!resource.field6c)
      return std::string{"GameRef.isScripted: unknown script load value"};
    if (!resource.loaded() && resource.payloadSize == 0) {
      std::string error;
      if (!game.resources->loadEmptyResource(resource, error))
        return "GameRef.isScripted: " + error;
    }
    if (!resource.loaded()) {
      if (!arena->loadScriptPath)
        return std::string{"GameRef.isScripted: script loader unavailable"};
      auto prepared = prepareGameCreation(*game.packs, node.id, {}, {},
                                          "_gameinst", arena->loadScriptPath);
      if (!prepared.request) {
        PackResource raw;
        std::string rawError;
        if (game.packs->lookup(node.id, raw, rawError) && raw.packBytes &&
            raw.entry.payloadOffset <= raw.packBytes->size() &&
            raw.entry.payloadSize <=
                raw.packBytes->size() - raw.entry.payloadOffset)
          return "GameRef.isScripted: " + prepared.error + ": " +
                 std::string(raw.packBytes->begin() + raw.entry.payloadOffset,
                             raw.packBytes->begin() + raw.entry.payloadOffset +
                                 raw.entry.payloadSize);
        return "GameRef.isScripted: " + prepared.error;
      }
      if (!game.world->classResolver() ||
          !game.world->classResolver()(prepared.request->scriptClass))
        return std::string{"GameRef.isScripted: script class unavailable"};
      if (prepared.request->nativeDevice == "part") {
        if (!prepared.request->nativeConfigPath)
          return std::string{
              "GameRef.isScripted: part config path unavailable"};
        std::string partError;
        if (!loadPartScriptPrototype(*game.resources, *node.script,
                                     *prepared.request->nativeConfigPath,
                                     partError))
          return "GameRef.isScripted: " + partError;
      } else if (prepared.request->nativeDevice == "car") {
        if (!prepared.request->nativeConfigPath)
          return std::string{"GameRef.isScripted: car config path unavailable"};
        std::string error;
        const auto cameraSettings = carCameraSettings(*arena);
        if (!loadCarScriptPrototype(*game.resources, *node.script,
                                    *prepared.request->nativeConfigPath, error,
                                    cameraSettings ? &*cameraSettings
                                                   : nullptr))
          return "GameRef.isScripted: " + error;
      } else if (!prepared.request->nativeDevice.empty())
        return std::string{
            "GameRef.isScripted: native script loader unsupported"};
      node.script->className = prepared.request->scriptClass;
      node.script->loaded = true;
      game.resources->noteScriptLoaded(node.id);
    }
    if (resource.stamp74 != game.world->frameCounter() &&
        !resource.recentUseQueued)
      resource.stateWord = (resource.stateWord & ~uint32_t{0x60}) | 0x10;
    game.world->noteRecentUse(*node.owner, resource.type, resource.stamp74,
                              resource.recentUseQueued);
    if (node.script->className.empty())
      return VmValue{int32_t{0}};
    if (!argument)
      return VmValue{int32_t{1}};
    if (!arena->ensureClass)
      return std::string{
          "GameRef.isScripted: class initialization unavailable"};
    auto ensured = arena->ensureClass(node.script->className);
    if (!ensured.ok)
      return "GameRef.isScripted: " + ensured.error;
    for (const auto *c : *classes)
      if (c && c->name == node.script->className)
        scriptClass = c;
    if (!scriptClass)
      return std::string{"GameRef.isScripted: loaded class unavailable"};
  }
  if (!argument)
    return VmValue{int32_t{1}};
  const auto *name = stringBytes(*arena, argument);
  if (!name || name->size() > 253 || name->find('\0') != std::string::npos)
    return std::string{"GameRef.isScripted: unsupported class name"};
  const TufaClass *expected = nullptr;
  for (const auto *c : *classes)
    if (c && c->name == *name)
      expected = c;
  if (!expected)
    return VmValue{int32_t{0}};
  const TufaClass *current = scriptClass;
  bool interfacesPresent = false;
  for (size_t depth = 0; current && depth <= classes->size(); ++depth) {
    if (current == expected)
      return VmValue{int32_t{1}};
    interfacesPresent |= !current->interfaces.empty();
    const auto parent = current->classHeader[3];
    if (parent == UINT32_MAX) {
      if (interfacesPresent)
        return std::string{
            "GameRef.isScripted: interface matching unsupported"};
      return VmValue{int32_t{0}};
    }
    if (parent >= current->constants.size() ||
        current->constants[parent].kind != 4 ||
        current->constants[parent].a >= current->constants.size())
      return std::string{"GameRef.isScripted: invalid class hierarchy"};
    const auto &parentName =
        current->constants[current->constants[parent].a].text;
    current = nullptr;
    for (const auto *c : *classes)
      if (c && c->name == parentName)
        current = c;
    if (!current)
      return std::string{"GameRef.isScripted: missing parent class"};
  }
  return std::string{"GameRef.isScripted: cyclic class hierarchy"};
}

NativeResult inputCreateHotkey(const std::vector<VmValue> &args,
                               const VmObject *, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!arena || !arena->hotkeys || args.size() != 6 ||
      !std::holds_alternative<int32_t>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]) ||
      !std::holds_alternative<const VmObject *>(args[2]) ||
      !std::holds_alternative<const VmObject *>(args[3]) ||
      !std::holds_alternative<const VmObject *>(args[4]) ||
      !std::holds_alternative<int32_t>(args[5]))
    return std::string{"Input.createHotkey: invalid context or arguments"};
  for (size_t i : {size_t{2}, size_t{3}, size_t{4}}) {
    const auto *object = std::get<const VmObject *>(args[i]);
    if (!object)
      continue;
    bool owned = false;
    for (const auto &candidate : arena->objects)
      if (candidate.get() == object)
        owned = true;
    if (!owned)
      return std::string{"Input.createHotkey: object is not owned by the VM"};
  }
  for (auto &entry : arena->hotkeys->entries)
    if (entry.flags == 0) {
      entry = {std::get<int32_t>(args[1]),
               std::get<int32_t>(args[0]),
               std::get<const VmObject *>(args[2]),
               std::get<const VmObject *>(args[4]),
               0,
               std::get<const VmObject *>(args[3]),
               std::get<int32_t>(args[5]),
               0};
      break;
    }
  return std::monostate{};
}
NativeResult inputDeleteHotkey(const std::vector<VmValue> &args,
                               const VmObject *, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!arena || !arena->hotkeys || args.size() != 1 ||
      !std::holds_alternative<const VmObject *>(args[0]))
    return std::string{"Input.deleteHotkey: invalid context or arguments"};
  const auto *hotkey = std::get<const VmObject *>(args[0]);
  for (auto &entry : arena->hotkeys->entries)
    if (entry.hotkey == hotkey) {
      entry.flags = 0;
      break;
    }
  return std::monostate{};
}
NativeResult inputFlushHotkeys(const std::vector<VmValue> &args,
                               const VmObject *, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!arena || !arena->hotkeys || !args.empty())
    return std::string{"Input.flushHotkeys: invalid context or arguments"};
  for (auto &entry : arena->hotkeys->entries)
    if (entry.flags)
      entry.initialized = 0;
  return std::monostate{};
}

NativeResult inputCheckHotkeys(const std::vector<VmValue> &args,
                               const VmObject *, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!arena || !arena->hotkeys || !arena->gameState ||
      !arena->gameState->world || args.size() != 2 ||
      !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[1]))
    return std::string{"Input.checkHotkeys: invalid context or arguments"};
  const auto *controller = std::get<const VmObject *>(args[0]);
  const auto *osd = std::get<const VmObject *>(args[1]);
  auto &game = *arena->gameState;
  if (controller) {
    const auto *link = nativeResourceState(controller, arena);
    if (!link)
      return std::string{"Input.checkHotkeys: controller link unavailable"};
    const int32_t handle =
        static_cast<int32_t>(link - arena->resourceStates.data()) + 1;
    const auto binding = game.bindings.find(handle);
    if (binding != game.bindings.end() && binding->second &&
        binding->second->link.owner) {
      auto *node = game.world->nodeFor(binding->second->link.owner);
      if (!node || node->nativeDevice != "player" || !node->playerInput)
        return std::string{
            "Input.checkHotkeys: controller getInfo unsupported"};
    }
  }
  struct Update {
    HotkeyState::Entry *entry;
    VmField *state;
    int32_t pressed;
    bool fire;
  };
  std::vector<Update> updates;
  for (auto &entry : arena->hotkeys->entries) {
    if (!entry.flags || (entry.osd && entry.osd != osd))
      continue;
    std::optional<float> value = 0.0f;
    if (entry.flags & 1) {
      if (!arena->hotkeys->virtualAxis)
        return std::string{
            "Input.checkHotkeys: virtual axis sampling unavailable"};
      value = arena->hotkeys->virtualAxis(controller, entry.key);
    } else if (entry.flags & 2) {
      if (!arena->hotkeys->physicalKey)
        return std::string{
            "Input.checkHotkeys: physical key sampling unavailable"};
      value = arena->hotkeys->physicalKey(entry.key);
    }
    if (!value || !std::isfinite(*value))
      return std::string{"Input.checkHotkeys: input sample unsupported"};
    VmField *state = nullptr;
    if (!entry.hotkey)
      return std::string{"Input.checkHotkeys: null hotkey"};
    for (auto &field : entry.hotkey->fields)
      if (field.name == "state" && field.descriptor == "I") {
        if (state)
          return std::string{"Input.checkHotkeys: ambiguous state"};
        state = &field;
      }
    if (!state || !std::holds_alternative<int32_t>(state->value))
      return std::string{"Input.checkHotkeys: state field unavailable"};
    const int32_t pressed = static_cast<double>(*value) > 0.2 ? 1 : 0;
    const bool fire = entry.initialized && entry.state != pressed &&
                      (entry.eventFilter & (pressed ? 1 : 2));
    updates.push_back({&entry, state, pressed, fire});
  }
  HotkeyState::Entry *first = nullptr;
  for (auto &update : updates) {
    auto &entry = *update.entry;
    if (!entry.initialized)
      entry.initialized = 1;
    else if (update.fire && !first)
      first = &entry;
    entry.state = update.pressed;
    update.state->value = update.pressed;
  }
  if (!first || !first->handler)
    return std::monostate{};
  const auto *link = nativeResourceState(first->handler, arena);
  if (!link)
    return std::string{"Input.checkHotkeys: handler link unavailable"};
  const int32_t handle =
      static_cast<int32_t>(link - arena->resourceStates.data()) + 1;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second ||
      !binding->second->link.owner)
    return std::monostate{};
  auto *node = game.world->nodeFor(binding->second->link.owner);
  if (!node || !node->loaded || !node->eventSubscriptionsKnownEmpty)
    return std::string{
        "Input.checkHotkeys: handler event delivery unsupported"};
  if (!(node->eventMask & 0x100000))
    return std::monostate{};
  if (!node->nativeDevice.empty() || !node->scriptReceiver ||
      !arena->callScript)
    return std::string{
        "Input.checkHotkeys: handler script delivery unavailable"};
  // FUN_0047cd40 sends event 0x100000 through FUN_00426800; like cursor
  // delivery, its handler runs independently and may park in a modal dialog.
  if (arena->scheduler) {
    struct HandlerGuard {
      OwnerLink link;
      explicit HandlerGuard(LinkOwner *owner) { setLinkOwner(link, owner); }
      ~HandlerGuard() { setLinkOwner(link, nullptr); }
    };
    auto guard = std::make_shared<HandlerGuard>(node);
    std::shared_ptr<VmHostTask> task;
    const auto started = arena->scheduler->startHostTask(
        node->scriptReceiver, "handleEvent", {first->hotkey},
        [guard] { return guard->link.owner != nullptr; }, task);
    if (!started.ok)
      return "Input.checkHotkeys: " + started.error;
    return std::monostate{};
  }
  const auto result =
      arena->callScript(node->scriptReceiver, "handleEvent", {first->hotkey});
  if (!result.ok)
    return "Input.checkHotkeys: " + result.error;
  return std::monostate{};
}

bool TimerState::setTimeWarp(float scale, double milliseconds,
                             std::string &error) {
  error.clear();
  if (!std::isfinite(scale) || !std::isfinite(milliseconds)) {
    error = "invalid time warp clock";
    return false;
  }
  if (scale < 0)
    return true;
  const double origin = milliseconds * static_cast<double>(0.001f) -
                        (scale > 0 ? static_cast<double>(now_) / scale : 0.0);
  if (!std::isfinite(origin) || !std::isfinite(static_cast<float>(origin))) {
    error = "invalid time warp origin";
    return false;
  }
  timeScale_ = scale;
  timeOrigin_ = static_cast<float>(origin);
  return true;
}
bool TimerState::add(LinkOwner *target, float delay, int32_t id,
                     std::string &error, uint32_t event, int32_t param) {
  error.clear();
  if (!std::isfinite(delay) || !std::isfinite(now_ + delay)) {
    error = "timer deadline is not finite";
    return false;
  }
  if (!target || !target->value)
    return true;
  auto timer = std::make_unique<Timer>();
  timer->deadline = now_ + delay;
  timer->id = id;
  timer->event = event;
  timer->param = param;
  setLinkOwner(timer->target, target);
  timers_.push_back(std::move(timer));
  return true;
}
void TimerState::removeAll(LinkOwner *target) {
  if (!target || !target->value)
    return;
  for (auto &timer : timers_)
    if (timer->target.cachedValue == target->value)
      setLinkOwner(timer->target, nullptr);
}
bool TimerState::advance(float now, const Delivery &delivery,
                         std::string &error) {
  error.clear();
  if (!std::isfinite(now) || now < now_ || advancing_) {
    error = "unsupported timer clock or reentrancy";
    return false;
  }
  if (!delivery) {
    error = "timer delivery unavailable";
    return false;
  }
  now_ = now;
  advancing_ = true;
  size_t remaining = timers_.size();
  auto it = timers_.begin();
  while (remaining-- && it != timers_.end()) {
    auto current = it++;
    auto &timer = **current;
    if (!timer.target.owner) {
      timers_.erase(current);
      continue;
    }
    if (timer.deadline > now_)
      continue;
    if (!delivery(*timer.target.owner, timer.id, timer.event, timer.param,
                  error)) {
      advancing_ = false;
      return false;
    }
    timers_.erase(current);
  }
  advancing_ = false;
  return true;
}
bool TimerState::advanceFrame(float elapsed, float timeScale, float maxDelta,
                              float maxStep, const PhysicsStep &physics,
                              const Delivery &delivery, const Drain &drain,
                              std::string &error) {
  error.clear();
  if (!physics || !delivery || !drain) {
    error = "simulation physics step unavailable";
    return false;
  }
  if (!std::isfinite(elapsed) || !std::isfinite(timeScale) ||
      !std::isfinite(maxDelta) || !std::isfinite(maxStep) || elapsed < 0 ||
      timeScale < 0 || maxDelta < 0 || maxStep <= 0) {
    error = "invalid simulation clock inputs";
    return false;
  }
  float remaining = std::min(elapsed * timeScale, maxDelta);
  if (!std::isfinite(remaining)) {
    error = "invalid simulation elapsed time";
    return false;
  }
  size_t steps = 0;
  bool stepped = false;
  while (remaining > 0) {
    if (++steps > 10000) {
      error = "simulation subdivision limit";
      return false;
    }
    float step = remaining < maxStep ? remaining : maxStep;
    if (remaining >= maxStep && remaining - maxStep <= maxStep)
      step = remaining * 0.5f;
    const float slice = step;
    step += partial_;
    if (step < 0.0001f) {
      partial_ = step;
      break;
    }
    if (!physics(step, error))
      return false;
    partial_ = 0;
    const float next = now_ + step;
    if (!std::isfinite(next) || !advance(next, delivery, error))
      return false;
    if (!drain(error))
      return false;
    stepped = true;
    remaining -= slice;
    if (remaining < 0)
      remaining = 0;
  }
  return stepped || drain(error);
}
namespace {
LinkOwner *timerTarget(const VmObject *receiver, VmArena *arena,
                       std::string &error) {
  const auto *resource = nativeResourceState(receiver, arena);
  if (!resource || !arena->gameState) {
    error = "timer receiver link unavailable";
    return nullptr;
  }
  const int32_t handle =
      static_cast<int32_t>(resource - arena->resourceStates.data()) + 1;
  const auto binding = arena->gameState->bindings.find(handle);
  return binding == arena->gameState->bindings.end() || !binding->second
             ? nullptr
             : binding->second->link.owner;
}
} // namespace
NativeResult gameTypeAddTimer(const std::vector<VmValue> &args,
                              const VmObject *receiver, VmArena *arena,
                              const std::vector<const TufaClass *> *) {
  if (!arena || !arena->timers || args.size() != 2 ||
      !std::holds_alternative<float>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]))
    return std::string{"GameType.addTimer: invalid context or arguments"};
  std::string error;
  auto *target = timerTarget(receiver, arena, error);
  if (!error.empty())
    return error;
  if (!arena->timers->add(target, std::get<float>(args[0]),
                          std::get<int32_t>(args[1]), error))
    return error;
  return std::monostate{};
}
NativeResult gameTypeRemoveAllTimers(const std::vector<VmValue> &args,
                                     const VmObject *receiver, VmArena *arena,
                                     const std::vector<const TufaClass *> *) {
  if (!arena || !arena->timers || !args.empty())
    return std::string{
        "GameType.removeAllTimers: invalid context or arguments"};
  std::string error;
  auto *target = timerTarget(receiver, arena, error);
  if (!error.empty())
    return error;
  arena->timers->removeAll(target);
  return std::monostate{};
}

bool dispatchTimerEvent(VmArena &arena, LinkOwner &target, int32_t id,
                        uint32_t event, int32_t param,
                        const std::vector<const TufaClass *> &classes,
                        const std::vector<VmStaticFields> &states,
                        std::string &error) {
  error.clear();
  // Stock FUN_0048b750(id 0x80000080) requests never reach the script upcall;
  // FUN_004674f0 case 0x80 dispatches them natively (object random sound).
  if (id == ObjectSoundRuntime::kTimerId) {
    if (!arena.objectSound) {
      error = "object sound timer host state unavailable";
      return false;
    }
    return arena.objectSound->deliver(target, event, param, error);
  }
  if (!arena.gameState || !arena.gameState->world) {
    error = "timer game world unavailable";
    return false;
  }
  auto &game = *arena.gameState;
  auto *node = game.world->nodeFor(&target);
  if (!node || !node->loaded || !node->eventSubscriptionsKnownEmpty) {
    error = "timer event target unsupported";
    return false;
  }
  if (!(node->eventMask & 0x80))
    return true;
  if (!node->nativeDevice.empty() || !node->scriptReceiver) {
    error = "timer native event delivery unsupported";
    return false;
  }
  const VmField *mode = nullptr;
  for (const auto &field : node->scriptReceiver->fields)
    if (field.name == "asyncMode" && field.descriptor == "I") {
      if (mode) {
        error = "timer asyncMode ambiguous";
        return false;
      }
      mode = &field;
    }
  if (!mode || !std::holds_alternative<int32_t>(mode->value)) {
    error = "timer asyncMode unavailable";
    return false;
  }
  const auto *script = node->scriptReceiver;
  struct Guard {
    OwnerLink link;
    ~Guard() { setLinkOwner(link, nullptr); }
  } guard;
  setLinkOwner(guard.link, &target);
  auto result = vmHostAllocate(arena, "java.util.resource.GameRef",
                               &nativeRegistry(), &classes, &states);
  if (!result.ok || !std::holds_alternative<const VmObject *>(result.value)) {
    error = "timer source allocation: " + result.error;
    return false;
  }
  if (!guard.link.owner) {
    error = "timer target destroyed during allocation";
    return false;
  }
  const auto *source = std::get<const VmObject *>(result.value);
  const auto initialized =
      invokeNative("java.util.resource.ResourceRef", "newNative", "()V", {},
                   source, &arena, &classes);
  if (!std::holds_alternative<std::monostate>(initialized)) {
    error = "timer source link allocation failed";
    return false;
  }
  auto *resource = nativeResourceState(source, &arena);
  if (!resource) {
    error = "timer source link unavailable";
    return false;
  }
  const int32_t handle =
      static_cast<int32_t>(resource - arena.resourceStates.data()) + 1;
  auto binding = std::make_unique<ReceiverBinding>();
  setLinkOwner(binding->link, guard.link.owner);
  resource->words[2] = binding->link.cachedValue;
  game.bindings.emplace(handle, std::move(binding));
  const char *method =
      std::get<int32_t>(mode->value) ? "handleEventAsync" : "handleEvent";
  result = vmHostCall(arena, script, method, {source, int32_t{0x80}, id},
                      &nativeRegistry(), &classes, &states);
  if (!result.ok) {
    error = "timer event: " + result.error;
    return false;
  }
  return true;
}

namespace {
std::optional<GameHook> callbackKind(int32_t kind) {
  if (kind == 8)
    return GameHook::Kind8;
  if (kind == 9)
    return GameHook::Kind9;
  if (kind == 0x1c)
    return GameHook::Kind1c;
  return std::nullopt;
}
NativeResult changeCallback(const std::vector<VmValue> &args,
                            const VmObject *receiver, VmArena *arena,
                            bool add) {
  if (!arena || !arena->gameState || !arena->gameState->world ||
      args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
    return std::string{"GameType callback: invalid context or argument"};
  const auto kind = callbackKind(std::get<int32_t>(args[0]));
  if (!kind)
    return std::monostate{};
  auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"GameType callback: native link unavailable"};
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto &game = *arena->gameState;
  auto it = game.bindings.find(handle);
  LinkOwner *target = it == game.bindings.end() || !it->second
                          ? nullptr
                          : it->second->link.owner;
  if (add) {
    if (!game.world->registerHook(*kind, target))
      return std::string{"GameType callback: target unavailable"};
  } else
    game.world->unregisterHook(*kind, target);
  return std::monostate{};
}
} // namespace
NativeResult gameTypeRegisterCallback(const std::vector<VmValue> &args,
                                      const VmObject *receiver, VmArena *arena,
                                      const std::vector<const TufaClass *> *) {
  return changeCallback(args, receiver, arena, true);
}
NativeResult
gameTypeUnregisterCallback(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *arena,
                           const std::vector<const TufaClass *> *) {
  return changeCallback(args, receiver, arena, false);
}
NativeResult
gameTypeUnregisterCallbacks(const std::vector<VmValue> &args, const VmObject *,
                            VmArena *, const std::vector<const TufaClass *> *) {
  if (!args.empty())
    return std::string{"GameType.unregisterCallbacks: invalid arguments"};
  return std::monostate{};
}

NativeResult inputLastKey(const std::vector<VmValue> &args, const VmObject *,
                          VmArena *arena,
                          const std::vector<const TufaClass *> *) {
  if (!args.empty() || !arena || !arena->hotkeys || !arena->hotkeys->lastKey)
    return std::string{"Input.lastKey: buffered keyboard unavailable"};
  const auto key = arena->hotkeys->lastKey();
  if (!key)
    return std::string{"Input.lastKey: unsupported keyboard event"};
  if (*key & 0xff0000) {
    arena->hotkeys->characterRing[arena->hotkeys->characterWrite] =
        static_cast<uint8_t>(*key >> 16);
    arena->hotkeys->characterWrite = (arena->hotkeys->characterWrite + 1) %
                                     arena->hotkeys->characterRing.size();
  }
  return VmValue{static_cast<int32_t>(*key)};
}

NativeResult inputGetAxis(const std::vector<VmValue> &args, const VmObject *,
                          VmArena *arena,
                          const std::vector<const TufaClass *> *) {
  if (args.size() != 2 || !std::holds_alternative<int32_t>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]))
    return std::string{"invalid Input.getAxis call"};
  const int32_t device = std::get<int32_t>(args[0]);
  const int32_t axis = std::get<int32_t>(args[1]);
  if (!arena || !arena->gameState || !arena->gameState->world)
    return std::string{"Input.getAxis: device enumeration unavailable"};
  const auto &devices = arena->gameState->world->inputDevices();
  if (!devices)
    return std::string{"Input.getAxis: device enumeration unavailable"};
  if (device < 0 || static_cast<size_t>(device) >= devices->size())
    return VmValue{0.0f};
  if ((*devices)[static_cast<size_t>(device)] == "SysMouse")
    return VmValue{0.0f};
  if ((*devices)[static_cast<size_t>(device)] != "SysKeyboard")
    return std::string{"Input.getAxis: device type unsupported"};
  if (axis < 0 || axis >= 256)
    return VmValue{0.0f};
  if (!arena->hotkeys || !arena->hotkeys->physicalKey)
    return std::string{"Input.getAxis: keyboard sampling unavailable"};
  const auto sample = arena->hotkeys->physicalKey(axis);
  if (!sample || !std::isfinite(*sample))
    return std::string{"Input.getAxis: keyboard sample unsupported"};
  return VmValue{*sample};
}

namespace {
struct DispatchGuard {
  OwnerLink link;
  explicit DispatchGuard(LinkOwner *owner = nullptr) {
    setLinkOwner(link, owner);
  }
  ~DispatchGuard() { setLinkOwner(link, nullptr); }
};

struct DispatchTask {
  struct promise_type {
    bool value = false;
    std::exception_ptr exception;
    std::coroutine_handle<> continuation = std::noop_coroutine();
    DispatchTask get_return_object() {
      return DispatchTask{
          std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    struct Final {
      bool await_ready() noexcept { return false; }
      std::coroutine_handle<>
      await_suspend(std::coroutine_handle<promise_type> h) noexcept {
        return h.promise().continuation;
      }
      void await_resume() noexcept {}
    };
    Final final_suspend() noexcept { return {}; }
    void return_value(bool result) noexcept { value = result; }
    void unhandled_exception() noexcept {
      exception = std::current_exception();
    }
  };
  using Handle = std::coroutine_handle<promise_type>;
  Handle handle{};
  explicit DispatchTask(Handle h) : handle(h) {}
  DispatchTask() = default;
  DispatchTask(const DispatchTask &) = delete;
  DispatchTask &operator=(const DispatchTask &) = delete;
  DispatchTask(DispatchTask &&other) noexcept
      : handle(std::exchange(other.handle, {})) {}
  DispatchTask &operator=(DispatchTask &&other) noexcept {
    if (this != &other) {
      if (handle)
        handle.destroy();
      handle = std::exchange(other.handle, {});
    }
    return *this;
  }
  ~DispatchTask() {
    if (handle)
      handle.destroy();
  }
  struct Awaiter {
    Handle child;
    explicit Awaiter(Handle h) : child(h) {}
    Awaiter(const Awaiter &) = delete;
    Awaiter(Awaiter &&other) noexcept : child(std::exchange(other.child, {})) {}
    ~Awaiter() {
      if (child)
        child.destroy();
    }
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> parent) noexcept {
      child.promise().continuation = parent;
      return child;
    }
    bool await_resume() const {
      if (child.promise().exception)
        std::rethrow_exception(child.promise().exception);
      return child.promise().value;
    }
  };
  Awaiter operator co_await() && noexcept {
    return Awaiter{std::exchange(handle, {})};
  }
};

struct DispatchContext {
  VmArena &arena;
  VmScheduler &scheduler;
  std::vector<const TufaClass *> classesCopy;
  std::vector<VmStaticFields> statesCopy;
  const std::vector<const TufaClass *> &classes;
  const std::vector<VmStaticFields> &states;
  std::string error;
  std::shared_ptr<bool> active = std::make_shared<bool>(true);
  std::shared_ptr<VmHostTask> pending;
  std::coroutine_handle<> suspended{};
  DispatchContext(VmArena &a, VmScheduler &s,
                  const std::vector<const TufaClass *> &c,
                  const std::vector<VmStaticFields> &v)
      : arena(a), scheduler(s),
        classesCopy(s.classes() ? std::vector<const TufaClass *>{} : c),
        statesCopy(s.staticFields() ? std::vector<VmStaticFields>{} : v),
        classes(s.classes() ? *s.classes() : classesCopy),
        states(s.staticFields() ? *s.staticFields() : statesCopy) {}
};

struct HostDispatch {
  DispatchContext &context;
  const VmObject *receiver;
  std::string method;
  std::vector<VmValue> arguments;
  std::function<bool()> alive;
  VmResult start{};
  bool await_ready() {
    const auto active = context.active;
    start = context.scheduler.startHostTask(
        receiver, method, std::move(arguments),
        [active, alive = std::move(alive)] { return *active && alive(); },
        context.pending);
    if (start.ok && !context.pending) {
      start = {false, int32_t{0}, "host task unavailable"};
    }
    return !start.ok || context.pending->finished;
  }
  void await_suspend(std::coroutine_handle<> h) noexcept {
    context.suspended = h;
  }
  VmResult await_resume() {
    context.suspended = {};
    if (!start.ok)
      return start;
    auto task = std::move(context.pending);
    return task->result;
  }
};

struct DispatchDriver {
  DispatchContext context;
  DispatchTask root;
  bool started = false;
  DispatchDriver(VmArena &a, VmScheduler &s,
                 const std::vector<const TufaClass *> &c,
                 const std::vector<VmStaticFields> &v)
      : context(a, s, c, v) {}
  ~DispatchDriver() { *context.active = false; }
  bool finished() const { return root.handle && root.handle.done(); }
  bool resume(std::string &error) {
    error.clear();
    if (!started) {
      started = true;
      root.handle.resume();
    } else if (context.pending) {
      if (!context.pending->finished)
        return true;
      context.suspended.resume();
    }
    if (finished() && root.handle.promise().exception) {
      try {
        std::rethrow_exception(root.handle.promise().exception);
      } catch (const std::exception &e) {
        error = std::string{"animation dispatch: "} + e.what();
      } catch (...) {
        error = "animation dispatch: unknown exception";
      }
      return false;
    }
    if (finished() && !root.handle.promise().value) {
      error = context.error;
      return false;
    }
    return true;
  }
};
} // namespace

static DispatchTask
dispatchCursorSubscribers(DispatchContext &context, WorldNode &source,
                          std::string payload, WorldNode *sourceOverride,
                          std::function<bool()> outerAlive) {
  auto &arena = context.arena;
  const auto &classes = context.classes;
  const auto &states = context.states;
  auto &error = context.error;
  auto &game = *arena.gameState;
  runlog::event(runlog::EventKind::CursorEvent, "payload %s", payload.c_str());
  struct Pending {
    OwnerLink target;
    uint32_t event;
    std::string payload, method;
    ~Pending() { setLinkOwner(target, nullptr); }
  };
  auto sourceOwner = std::make_shared<DispatchGuard>(&source);
  auto sourceGuard = std::make_shared<DispatchGuard>(
      sourceOverride ? sourceOverride : &source);
  std::vector<std::unique_ptr<Pending>> pending;
  if ((source.eventMask & 0x10000) && source.scriptReceiver) {
    auto item = std::make_unique<Pending>();
    setLinkOwner(item->target, &source);
    item->event = 0x10000;
    item->payload = payload;
    pending.push_back(std::move(item));
  }
  for (const auto &entry : source.eventSubscriptions) {
    if (!entry->target.owner || !(entry->mask & 0x10000))
      continue;
    if (entry->mask & 0xc0000000u) {
      error = "cursor notification flags unsupported";
      co_return false;
    }
    auto item = std::make_unique<Pending>();
    setLinkOwner(item->target, entry->target.owner);
    item->event = (entry->replacementMask & 0x0fffffffu)
                      ? entry->replacementMask & 0x0fffffffu
                      : 0x10000u;
    item->payload = (entry->replacementMask & 0x10000000u)
                        ? entry->replacementPayload
                        : payload;
    item->method = entry->method;
    pending.push_back(std::move(item));
  }
  for (const auto &item : pending) {
    if (!sourceOwner->link.owner || !sourceGuard->link.owner || !outerAlive()) {
      error = "cursor event source destroyed";
      co_return false;
    }
    auto *target = game.world->nodeFor(item->target.owner);
    if (!target)
      continue;
    if (!target->loaded ||
        (target != sourceOwner->link.owner &&
         !target->eventSubscriptionsKnownEmpty) ||
        !target->nativeDevice.empty()) {
      error = "cursor notification target unsupported";
      co_return false;
    }
    if (!(target->eventMask & item->event))
      continue;
    if (!target->scriptReceiver)
      continue;
    const VmField *mode = nullptr;
    for (const auto &field : target->scriptReceiver->fields)
      if (field.name == "asyncMode" && field.descriptor == "I") {
        if (mode) {
          error = "cursor asyncMode ambiguous";
          co_return false;
        }
        mode = &field;
      }
    if (!mode || !std::holds_alternative<int32_t>(mode->value)) {
      error = "cursor asyncMode unavailable";
      co_return false;
    }
    const auto *script = target->scriptReceiver;
    const bool async = std::get<int32_t>(mode->value) != 0;
    auto ref = vmHostAllocate(arena, "java.util.resource.GameRef",
                              &nativeRegistry(), &classes, &states);
    if (!ref.ok || !std::holds_alternative<const VmObject *>(ref.value)) {
      error = "cursor source allocation: " + ref.error;
      co_return false;
    }
    const auto *object = std::get<const VmObject *>(ref.value);
    if (!std::holds_alternative<std::monostate>(
            invokeNative("java.util.resource.ResourceRef", "newNative", "()V",
                         {}, object, &arena, &classes))) {
      error = "cursor source link unavailable";
      co_return false;
    }
    auto *resource = nativeResourceState(object, &arena);
    if (!resource) {
      error = "cursor source state unavailable";
      co_return false;
    }
    const int32_t handle =
        static_cast<int32_t>(resource - arena.resourceStates.data()) + 1;
    if (!sourceGuard->link.owner || !item->target.owner) {
      error = "cursor event target destroyed";
      co_return false;
    }
    auto binding = std::make_unique<ReceiverBinding>();
    setLinkOwner(binding->link, sourceGuard->link.owner);
    resource->words[2] = binding->link.cachedValue;
    game.bindings.emplace(handle, std::move(binding));
    if (arena.ensureClass) {
      const auto ensured = arena.ensureClass("java.lang.String");
      if (!ensured.ok) {
        error = "cursor event string class: " + ensured.error;
        co_return false;
      }
    }
    const auto text = internStringResult(item->payload, &arena, &classes);
    if (!std::holds_alternative<VmValue>(text)) {
      error = "cursor event string unavailable";
      co_return false;
    }
    const auto *textObject =
        std::get_if<const VmObject *>(&std::get<VmValue>(text));
    if (!textObject) {
      error = "cursor event string invalid";
      co_return false;
    }
    const std::string method =
        !item->method.empty() ? item->method
                              : (async ? "handleEventAsync" : "handleEvent");
    if (!sourceGuard->link.owner || !item->target.owner ||
        !sourceOwner->link.owner || !outerAlive()) {
      error = "cursor event target destroyed";
      co_return false;
    }
    auto targetGuard = std::make_shared<DispatchGuard>(item->target.owner);
    // Stock starts each event dispatch on its own thread (FUN_00425490 ->
    // FUN_0041f340, body run by FUN_0041fbc0) and does not join it. Awaiting
    // here deadlocks a modal handler: its wait() parks this task, and the
    // click that would notify it can only arrive on a later dispatch. Start
    // the task and continue; the scheduler owns it and ticks it each frame.
    // Liveness is node-based, not tied to this coroutine's lifetime.
    std::shared_ptr<VmHostTask> task;
    const auto started = context.scheduler.startHostTask(
        script, method,
        {object, static_cast<int32_t>(item->event), *textObject},
        [sourceGuard, targetGuard] {
          return sourceGuard->link.owner && targetGuard->link.owner;
        },
        task);
    if (!started.ok) {
      error = "cursor notification " + method + " [" + item->payload +
              "]: " + started.error;
      co_return false;
    }
    if (!task) {
      error = "cursor notification " + method + " [" + item->payload +
              "]: host task unavailable";
      co_return false;
    }
    if (!sourceGuard->link.owner || !sourceOwner->link.owner || !outerAlive()) {
      error = "cursor event source destroyed";
      co_return false;
    }
  }
  co_return true;
}

static DispatchTask dispatchAnimationCallbacks(DispatchContext &context,
                                               float delta) {
  auto &arena = context.arena;
  const auto &classes = context.classes;
  auto &error = context.error;
  error.clear();
  if (!arena.gameState || !arena.gameState->world || !std::isfinite(delta)) {
    error = "animation callback world or delta unavailable";
    co_return false;
  }
  auto &world = *arena.gameState->world;
  world.pruneDetachedHooks(GameHook::Kind1c);
  std::vector<std::shared_ptr<DispatchGuard>> pending;
  for (const auto *node : world.hooks(GameHook::Kind1c)) {
    if (!node) {
      error = "animation callback external target unsupported";
      co_return false;
    }
    pending.push_back(
        std::make_shared<DispatchGuard>(const_cast<WorldNode *>(node)));
  }
  for (const auto &guard : pending) {
    if (!guard->link.owner)
      continue;
    auto *node = world.nodeFor(guard->link.owner);
    if (!node || node->type != 1 || !node->loaded || !node->field6c ||
        node->loadPriority < std::min(*node->field6c, 1.0f)) {
      error = "animation callback loading unsupported";
      co_return false;
    }
    world.noteRecentUse(*node, node->type, node->stamp74,
                        node->recentUseQueued);
    if (node->nativeDevice == "player" && node->player) {
      arena.gameState->playerAnimationDelta = delta;
      if (node->player->animationOverlay.cachedValue) {
        error = "player animation overlay unsupported";
        co_return false;
      }
      for (const auto &text : node->player->debugText)
        if (text.cachedValue) {
          error = "player debug text cleanup unsupported";
          co_return false;
        }
      continue;
    }
    if (node->nativeDevice == "cursor" && node->cursor) {
      if (node->cursor->controller.cachedValue) {
        auto &cursor = *node->cursor;
        if (!arena.gameState->mouseHelp || *arena.gameState->mouseHelp != 0) {
          error = "active cursor MouseHelp unsupported";
          co_return false;
        }
        auto *controller = world.nodeFor(cursor.controller.owner);
        if (!world.systemCursor() || !world.cursorInput() || !controller ||
            !controller->playerInput || !arena.gameState->resources ||
            !arena.gameState->renderer || cursor.pointer.owner) {
          error = "active cursor input or camera unavailable";
          co_return false;
        }
        const auto input = world.cursorInput()();
        const auto button = controller->playerInput->sampleAxis(45);
        if (!button) { error = "active cursor button unavailable"; co_return false; }
        if (!input) {
          // A host letterbox bar is outside the game viewport. Ignore presses
          // there and cancel a press dragged out of the content rectangle.
          cursor.leftDown = *button > 0.2f;
          cursor.dragging = true;
          setLinkOwner(cursor.pressedBox, nullptr);
          continue;
        }
        if (!std::isfinite((*input)[0]) || !std::isfinite((*input)[1])) {
          error = "active cursor input unavailable"; co_return false;
        }
        const float oldX = cursor.x, oldY = cursor.y;
        cursor.x = std::clamp((*input)[0], -1.0f, 1.0f);
        cursor.y = std::clamp((*input)[1], -1.0f, 1.0f);
        const bool movedThisFrame = cursor.x != oldX || cursor.y != oldY;
        if (!(cursor.flags & 1))
          continue;
        WorldNode *hit = nullptr;
        std::array<float, 3> contact{};
        int width = 0, height = 0;
        if (!arena.gameState->renderer->outputSize(width, height, error) ||
            width <= 0 || height <= 0)
          co_return false;
        // Stock picks from the list tail: viewports and cameras are walked
        // highest priority first (FUN_004fdc40 returns the +0xbc tail and
        // FUN_004fdca0 steps via the +8 previous link), so a modal dialog's
        // OSD viewport wins over the viewport behind it.
        auto pickViewports = world.activeViewports();
        std::reverse(pickViewports.begin(), pickViewports.end());
        for (auto *viewport : pickViewports) {
          if (!viewport->viewport || !viewport->viewport->renderAttachments)
            continue;
          auto pickCameras = viewport->viewport->renderAttachments->active();
          std::reverse(pickCameras.begin(), pickCameras.end());
          for (auto *owner : pickCameras) {
            auto *camera = world.nodeFor(owner);
            if (!camera) {
              error = "cursor camera unavailable";
              co_return false;
            }
            auto resolved = arena.gameState->resources->resolve(camera);
            auto scene =
                resolved
                    ? arena.gameState->resources->sceneRenderInstance(resolved)
                    : nullptr;
            if (!scene) {
              error = "cursor camera scene unavailable";
              co_return false;
            }
            const auto type =
                arena.gameState->resources->lookup(scene->typeId(), 12);
            if (type.status != LookupStatus::Found || !type.node.worldNode ||
                !type.node.worldNode->camera) {
              error = "cursor camera type unavailable";
              co_return false;
            }
            scene->processRootBoneUpdate();
            const auto &matrix = scene->derivedRootBoneMatrix();
            const float aspect = viewport->viewport->width * width /
                                 (viewport->viewport->height * height);
            const auto view =
                cameraViewState(*type.node.worldNode->camera, matrix, aspect);
            if (!view) {
              error = "cursor camera projection unavailable";
              co_return false;
            }
            auto *sector = cameraTraversalRoot(*camera);
            if (!sector) {
              error = "cursor camera sector unavailable";
              co_return false;
            }
            std::vector<PhysicsBox> boxes;
            std::vector<WorldNode *> candidates;
            for (auto *box : world.staticBoxes()) {
              if (!box || !box->loaded || !box->physicsBox ||
                  box->physicsBox->geometryFlags != 5 ||
                  box->physicsBox->position[0] !=
                      box->physicsBox->position[1]) {
                error = "cursor box unsupported";
                co_return false;
              }
              const WorldNode *ancestor = box;
              bool hiddenByLeaf = false;
              while (ancestor) {
                // FUN_0049e710 refuses to descend through a type-1 node
                // with WORLDTREELEAF (+0x54 bit0x40). Stock Group.deactivate
                // uses precisely this flag to remove hidden gadgets from picks.
                if (ancestor->type == 1 && (ancestor->flags & 0x40u)) hiddenByLeaf = true;
                if (ancestor == sector) break;
                ancestor = world.parent(*ancestor);
              }
              if (!ancestor || hiddenByLeaf)
                continue;
              candidates.push_back(box);
              boxes.push_back(*box->physicsBox);
            }
            const auto picked =
                pickCameraBox({cursor.x, cursor.y}, *viewport->viewport, matrix,
                              view->projection, boxes);
            if (picked) {
              hit = candidates[picked->index];
              contact = picked->localContact;
              break;
            }
          }
          if (hit)
            break;
        }
        if (*button > 0.2f && !cursor.leftDown) {
          for (const WorldNode* ancestor = hit; ancestor; ancestor = world.parent(*ancestor))
            runlog::event(runlog::EventKind::Note, "cursor pick ancestry: node=0x%08x name=%s flags=0x%x class=%s",
                          ancestor->value, ancestor->name.c_str(), ancestor->flags,
                          ancestor->scriptReceiver && ancestor->scriptReceiver->runtimeClass ? ancestor->scriptReceiver->runtimeClass->name.c_str() : "native");
        }
        if (hit && !world.parent(*hit)) {
          error = "cursor box parent unavailable";
          co_return false;
        }
        auto parentId = [&](WorldNode *box) -> uint32_t {
          const auto *parent = box ? world.parent(*box) : nullptr;
          return parent ? parent->value : 0;
        };
        auto hitGuard = std::make_shared<DispatchGuard>(hit);
        const bool hadPressed = cursor.pressedBox.owner != nullptr;
        auto pressedGuard =
            std::make_shared<DispatchGuard>(cursor.pressedBox.owner);
        auto emit = [&](WorldNode &target, std::string payload,
                        WorldNode *origin = nullptr) -> DispatchTask {
          auto alive = [guard, hitGuard, pressedGuard, hadHit = hit != nullptr,
                        hadPressed] {
            return guard->link.owner && (!hadHit || hitGuard->link.owner) &&
                   (!hadPressed || pressedGuard->link.owner);
          };
          if (!(co_await dispatchCursorSubscribers(
                  context, target, std::move(payload), origin, alive)))
            co_return false;
          if (!alive()) {
            error = "cursor event destroyed a live cursor or hit";
            co_return false;
          }
          co_return true;
        };
        const bool down = *button > 0.2f;
        if (hit != cursor.hoveredBox.owner) {
          auto *previous = world.nodeFor(cursor.hoveredBox.owner);
          if (previous) {
            const auto payload =
                "16 " + std::to_string(static_cast<int32_t>(node->value)) +
                " " + std::to_string(static_cast<int32_t>(parentId(previous))) +
                " " + std::to_string(static_cast<int32_t>(previous->value)) +
                " " + std::to_string(movedThisFrame);
            auto *parent = const_cast<WorldNode *>(world.parent(*previous));
            if (parent && !(co_await emit(*parent, payload)))
              co_return false;
            if (!(co_await emit(*node, payload)))
              co_return false;
          }
          setLinkOwner(cursor.hoveredBox, hit);
          if (hit) {
            const auto payload =
                "15 " + std::to_string(static_cast<int32_t>(node->value)) +
                " " + std::to_string(static_cast<int32_t>(parentId(hit))) +
                " " + std::to_string(static_cast<int32_t>(hit->value)) + " " +
                std::to_string(movedThisFrame);
            auto *parent = const_cast<WorldNode *>(world.parent(*hit));
            if (parent && !(co_await emit(*parent, payload)))
              co_return false;
            if (!(co_await emit(*node, payload)))
              co_return false;
          }
        }
        const float dragX = cursor.x - cursor.pressPosition[0],
                    dragY = cursor.y - cursor.pressPosition[1];
        if (cursor.leftDown && !cursor.dragging &&
            dragX * dragX + dragY * dragY >= 0.02f * 0.02f) {
          auto *pressed = world.nodeFor(cursor.pressedBox.owner);
          auto *parent = pressed
                             ? const_cast<WorldNode *>(world.parent(*pressed))
                             : nullptr;
          const auto drag =
              "9 " + std::to_string(static_cast<int32_t>(node->value)) + " " +
              std::to_string(
                  static_cast<int32_t>(pressed ? pressed->value : 0));
          if (!(co_await emit(*node, drag)))
            co_return false;
          if (parent && !(co_await emit(*parent, drag)))
            co_return false;
          cursor.dragging = true;
        }
        if (down != cursor.leftDown) {
          if (down) {
            setLinkOwner(cursor.pressedBox, hit);
            cursor.dragging = false;
            cursor.pressContact = contact;
            cursor.pressPosition = {cursor.x, cursor.y};
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer), "1 %d %d %d %.3f %.3f %.3f",
                          static_cast<int32_t>(node->value),
                          static_cast<int32_t>(parentId(hit)),
                          static_cast<int32_t>(hit ? hit->value : 0),
                          contact[0], contact[1], contact[2]);
            auto *parent =
                hit ? const_cast<WorldNode *>(world.parent(*hit)) : nullptr;
            if (parent && !(co_await emit(*parent, buffer)))
              co_return false;
            if (!(co_await emit(*node, buffer)))
              co_return false;
          } else {
            auto *pressed = world.nodeFor(cursor.pressedBox.owner);
            auto *parent = pressed
                               ? const_cast<WorldNode *>(world.parent(*pressed))
                               : nullptr;
            const auto payload =
                "2 " + std::to_string(static_cast<int32_t>(node->value)) + " " +
                std::to_string(static_cast<int32_t>(parentId(pressed))) + " " +
                std::to_string(
                    static_cast<int32_t>(pressed ? pressed->value : 0));
            if (!(co_await emit(*node, payload)))
              co_return false;
            if (parent && !(co_await emit(*parent, payload)))
              co_return false;
            if (pressed && parent && !cursor.dragging) {
              const auto &p = cursor.pressContact;
              const auto &position = pressed->physicsBox->position[0];
              char parentClick[160];
              std::snprintf(
                  parentClick, sizeof(parentClick), "5 %d %d %.3f %.3f %.3f",
                  static_cast<int32_t>(node->value),
                  static_cast<int32_t>(pressed->value), p[0], p[1], p[2]);
              if (!(co_await emit(*parent, parentClick)))
                co_return false;
              char click[200];
              std::snprintf(click, sizeof(click),
                            "5 %d %d %.3f %.3f %.3f %.3f %.3f %.3f",
                            static_cast<int32_t>(node->value),
                            static_cast<int32_t>(parent->value), p[0], p[1],
                            p[2], contact[0] + position[0],
                            contact[1] + position[1], contact[2] + position[2]);
              if (!(co_await emit(*node, click, parent)))
                co_return false;
            } else if (cursor.dragging) {
              const auto dragEnd =
                  "10 " + std::to_string(static_cast<int32_t>(node->value)) +
                  " " +
                  std::to_string(
                      static_cast<int32_t>(pressed ? pressed->value : 0));
              if (!(co_await emit(*node, dragEnd)))
                co_return false;
              if (parent && !(co_await emit(*parent, dragEnd)))
                co_return false;
              if (pressed && parent && hit) {
                auto *currentParent =
                    const_cast<WorldNode *>(world.parent(*hit));
                if (!currentParent) {
                  error = "cursor picked parent unavailable";
                  co_return false;
                }
                const auto drop =
                    "11 " + std::to_string(static_cast<int32_t>(node->value)) +
                    " " +
                    std::to_string(static_cast<int32_t>(currentParent->value)) +
                    " " + std::to_string(static_cast<int32_t>(parent->value)) +
                    " " + std::to_string(static_cast<int32_t>(pressed->value));
                if (!(co_await emit(*currentParent, drop)))
                  co_return false;
                const auto &position = hit->physicsBox->position[0];
                char dropCursor[200];
                std::snprintf(dropCursor, sizeof(dropCursor),
                              "11 %d %d %d %.3f %.3f %.3f %d",
                              static_cast<int32_t>(node->value),
                              static_cast<int32_t>(currentParent->value),
                              static_cast<int32_t>(parent->value),
                              contact[0] + position[0],
                              contact[1] + position[1],
                              contact[2] + position[2],
                              static_cast<int32_t>(pressed->value));
                if (!(co_await emit(*node, dropCursor, currentParent)))
                  co_return false;
              }
            }
            setLinkOwner(cursor.pressedBox, nullptr);
            cursor.dragging = false;
          }
          cursor.leftDown = down;
        }
        continue;
      }
      if (!arena.gameState->mouseHelp) {
        error = "cursor MouseHelp configuration unavailable";
        co_return false;
      }
      if (*arena.gameState->mouseHelp != 0) {
        error = "cursor MouseHelp animation unsupported";
        co_return false;
      }
      if (node->cursor->viewport.cachedValue && !world.systemCursor() &&
          !node->cursor->pointer.cachedValue) {
        error = "software cursor creation unsupported";
        co_return false;
      }
      continue;
    }
    if (!node->nativeDevice.empty()) {
      if ((node->nativeDevice == "car" || node->nativeDevice == "part") &&
          node->carInstance) {
        if (node->scriptReceiver && node->nativeDevice == "car") {
          for (const auto *cls = node->scriptReceiver->runtimeClass; cls;) {
            bool hasAnimate = false;
            for (const auto &list : cls->methods)
              for (const auto &method : list)
                if (cls->constants[method.words[1]].text == "animate" &&
                    cls->constants[method.words[2]].text == "()V")
                  hasAnimate = true;
            if (hasAnimate) {
              const auto result = co_await HostDispatch{
                  context, node->scriptReceiver, "animate", {}, [guard] {
                    return guard->link.owner != nullptr;
                  }};
              if (!result.ok) {
                error = "animation callback: " + result.error;
                co_return false;
              }
              break;
            }
            const auto parent = cls->classHeader[3];
            if (parent == UINT32_MAX || parent >= cls->constants.size() ||
                cls->constants[parent].kind != 4 ||
                cls->constants[parent].a >= cls->constants.size())
              break;
            const auto &parentName =
                cls->constants[cls->constants[parent].a].text;
            cls = nullptr;
            for (const auto *c : classes)
              if (c && c->name == parentName)
                cls = c;
          }
        }
        continue;
      }
      error = "native animation callback unsupported: " + node->nativeDevice;
      co_return false;
    }
    if (!node->scriptReceiver)
      continue;
    const auto result = co_await HostDispatch{
        context, node->scriptReceiver, "animate", {}, [guard] {
          return guard->link.owner != nullptr;
        }};
    if (!result.ok) {
      error = "animation callback: " + result.error;
      co_return false;
    }
  }
  co_return true;
}

static DispatchTask cursorDispatchRoot(DispatchContext &context,
                                       std::shared_ptr<DispatchGuard> source,
                                       std::shared_ptr<DispatchGuard> origin,
                                       std::string payload) {
  if (!context.arena.gameState || !context.arena.gameState->world ||
      !source->link.owner || !origin->link.owner) {
    context.error = "cursor event target destroyed";
    co_return false;
  }
  auto *node = context.arena.gameState->world->nodeFor(source->link.owner);
  auto *overrideNode =
      context.arena.gameState->world->nodeFor(origin->link.owner);
  if (!node || !overrideNode) {
    context.error = "cursor event target destroyed";
    co_return false;
  }
  co_return co_await dispatchCursorSubscribers(
      context, *node, std::move(payload), overrideNode,
      [source, origin] { return source->link.owner && origin->link.owner; });
}

struct CursorDispatch::Impl : DispatchDriver {
  Impl(VmArena &arena, VmScheduler &scheduler, WorldNode &source,
       std::string payload, const std::vector<const TufaClass *> &classes,
       const std::vector<VmStaticFields> &states, WorldNode *origin)
      : DispatchDriver(arena, scheduler, classes, states) {
    root = cursorDispatchRoot(
        context, std::make_shared<DispatchGuard>(&source),
        std::make_shared<DispatchGuard>(origin ? origin : &source),
        std::move(payload));
  }
};

CursorDispatch::CursorDispatch(VmArena &arena, VmScheduler &scheduler,
                               WorldNode &source, std::string payload,
                               const std::vector<const TufaClass *> &classes,
                               const std::vector<VmStaticFields> &states,
                               WorldNode *origin)
    : impl_(std::make_unique<Impl>(arena, scheduler, source, std::move(payload),
                                   classes, states, origin)) {}
CursorDispatch::~CursorDispatch() = default;
bool CursorDispatch::resume(std::string &error) { return impl_->resume(error); }
bool CursorDispatch::finished() const { return impl_->finished(); }

struct AnimationDispatch::Impl : DispatchDriver {
  Impl(VmArena &arena, VmScheduler &scheduler, float delta,
       const std::vector<const TufaClass *> &classes,
       const std::vector<VmStaticFields> &states)
      : DispatchDriver(arena, scheduler, classes, states) {
    root = dispatchAnimationCallbacks(context, delta);
  }
};

AnimationDispatch::AnimationDispatch(
    VmArena &arena, VmScheduler &scheduler, float delta,
    const std::vector<const TufaClass *> &classes,
    const std::vector<VmStaticFields> &states)
    : impl_(std::make_unique<Impl>(arena, scheduler, delta, classes, states)) {}
AnimationDispatch::~AnimationDispatch() = default;
bool AnimationDispatch::resume(std::string &error) {
  return impl_->resume(error);
}
bool AnimationDispatch::finished() const { return impl_->finished(); }

NativeResult gfxNumDisplayModes(const std::vector<VmValue> &args,
                                const VmObject *, VmArena *arena,
                                const std::vector<const TufaClass *> *) {
  if (!args.empty() || !arena || !arena->gameState ||
      !arena->gameState->displayModes)
    return std::string{
        "GfxEngine.numDisplayModes: display inventory unavailable"};
  if (arena->gameState->displayModes->size() > static_cast<size_t>(INT32_MAX))
    return std::string{"GfxEngine.numDisplayModes: too many modes"};
  return VmValue{static_cast<int32_t>(arena->gameState->displayModes->size())};
}
NativeResult gfxSetGlobalEnvmap(const std::vector<VmValue> &args,
                                const VmObject *, VmArena *arena,
                                const std::vector<const TufaClass *> *) {
  if (!arena || !arena->gameState || args.size() != 1 ||
      !std::holds_alternative<const VmObject *>(args[0]))
    return std::string{"GfxEngine.setGlobalEnvmap: invalid call"};
  const auto *ref = std::get<const VmObject *>(args[0]);
  if (!ref) {
    setLinkOwner(arena->gameState->globalEnvmap, nullptr);
    return std::monostate{};
  }
  const auto *state = nativeResourceState(ref, arena);
  if (!state)
    return std::string{"GfxEngine.setGlobalEnvmap: invalid resource handle"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner) {
    setLinkOwner(game.globalEnvmap, nullptr);
    return std::monostate{};
  }
  setLinkOwner(game.globalEnvmap, it->second->link.owner);
  return std::monostate{};
}

NativeResult gfxForceRendering(const std::vector<VmValue> &args,
                               const VmObject *, VmArena *arena,
                               const std::vector<const TufaClass *> *) {
  if (!args.empty() || !arena || !arena->gameState ||
      !arena->gameState->forceRendering)
    return std::string{
        "GfxEngine.forceRendering: synchronous renderer unavailable"};
  std::string error;
  if (!arena->gameState->forceRendering(error))
    return "GfxEngine.forceRendering: " + error;
  return std::monostate{};
}
NativeResult gfxFlush(const std::vector<VmValue> &args, const VmObject *,
                      VmArena *arena, const std::vector<const TufaClass *> *) {
  if (!args.empty() || !arena || !arena->gameState || !arena->gameState->flush)
    return std::string{"GfxEngine.flush: synchronous renderer unavailable"};
  std::string error;
  if (!arena->gameState->flush(error))
    return "GfxEngine.flush: " + error;
  return std::monostate{};
}
NativeResult gfxDisplayModeName(const std::vector<VmValue> &args,
                                const VmObject *, VmArena *arena,
                                const std::vector<const TufaClass *> *classes) {
  if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]) || !arena ||
      !arena->gameState || !arena->gameState->displayModes || !classes)
    return std::string{
        "GfxEngine.displayModeName: display inventory unavailable"};
  std::string name;
  const auto index = std::get<int32_t>(args[0]);
  const auto &modes = *arena->gameState->displayModes;
  if (index >= 0 && static_cast<size_t>(index) < modes.size()) {
    const auto &mode = modes[static_cast<size_t>(index)];
    name = std::to_string(mode[0]) + " " + std::to_string(mode[1]) + " " +
           std::to_string(mode[2]);
  }
  const TufaClass *stringClass = nullptr;
  for (const auto *candidate : *classes)
    if (candidate && candidate->name == "java.lang.String")
      stringClass = candidate;
  if (!stringClass || arena->objects.size() >= arena->limit)
    return std::string{"GfxEngine.displayModeName: String unavailable"};
  auto object = std::make_unique<VmObject>();
  object->runtimeClass = stringClass;
  const VmObject *identity = object.get();
  arena->objects.push_back(std::move(object));
  arena->strings.emplace_back(name, identity);
  return VmValue{identity};
}

NativeResult gfxChangeVideoMode(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
  if (args.size() != 3 || !arena || !arena->gameState || !arena->gameState->changeDisplayMode)
    return std::string{"GfxEngine.changeVideoMode: application display unavailable"};
  PlatformDisplayMode mode{};
  for (size_t i = 0; i < mode.size(); ++i) {
    const auto* value = std::get_if<int32_t>(&args[i]);
    if (!value) return std::string{"GfxEngine.changeVideoMode: integer dimensions/depth required"};
    mode[i] = *value;
  }
  // FUN_0047c3f0 passes the three script arguments to the renderer, vtable +0x14.
  std::string error;
  if (!arena->gameState->changeDisplayMode(mode, error))
    return "GfxEngine.changeVideoMode: " + error;
  return std::monostate{};
}

NativeResult gfxCurrDisplayMode(const std::vector<VmValue> &args,
                                const VmObject *, VmArena *arena,
                                const std::vector<const TufaClass *> *) {
  if (!args.empty() || !arena || !arena->gameState ||
      !arena->gameState->displayModes || !arena->gameState->currentDisplayMode)
    return std::string{
        "GfxEngine.currDisplayMode: current display mode unavailable"};
  auto current = arena->gameState->currentDisplayMode();
  if (!current)
    return std::string{
        "GfxEngine.currDisplayMode: current display mode unavailable"};
  const auto &modes = *arena->gameState->displayModes;
  const auto found = std::find(modes.begin(), modes.end(), *current);
  return VmValue{found == modes.end()
                     ? int32_t{0}
                     : static_cast<int32_t>(found - modes.begin())};
}

namespace {
const CarSlot *findProtoSlot(const CarPrototype &proto, int32_t slotId) {
  for (const auto &slot : proto.config.slots)
    if (slot.state.id == slotId)
      return &slot;
  return nullptr;
}

const CarPrototype *nodePrototype(const WorldNode &node, GameWorld &world) {
  if (!node.carInstance)
    return nullptr;
  const uint32_t scriptId = node.carInstance->prototypeSource.cachedValue
                                ? node.carInstance->prototypeSource.cachedValue
                                : node.scriptLink.cachedValue;
  if (auto *script = world.findScript(scriptId))
    return script->car.get();
  return nullptr;
}

float prototypeMass(const CarPrototype &proto) {
  if (proto.config.centerMass > 0.0f)
    return proto.config.centerMass;
  float sum = 0.0f;
  for (const auto &body : proto.config.bodies)
    if (body.kind == CarBodyRecord::Body && body.shape.mass > 0.0f)
      sum += body.shape.mass;
  return sum;
}

RuntimeSlotState *findRuntimeSlot(CarRuntimeInstance &inst, int32_t slotId) {
  for (auto &slot : inst.slots)
    if (slot && slot->id == slotId)
      return slot.get();
  return nullptr;
}

bool callIfDeclared(VmArena &arena, const VmObject *receiver,
                    const char *method, const char *descriptor,
                    std::vector<VmValue> args, std::string &error) {
  if (!receiver || !receiver->runtimeClass || !arena.callScript)
    return true;
  bool found = false;
  const auto *c = receiver->runtimeClass;
  for (const auto &list : c->methods)
    for (const auto &m : list)
      if (m.words[1] < c->constants.size() &&
          m.words[2] < c->constants.size() &&
          c->constants[m.words[1]].text == method &&
          (!descriptor || c->constants[m.words[2]].text == descriptor))
        found = true;
  if (found || std::string_view(method) == "addpart" ||
      std::string_view(method) == "rempart" ||
      std::string_view(method) == "install" ||
      std::string_view(method) == "remove" ||
      std::string_view(method) == "updatevariables") {
    auto r = arena.callScript(receiver, method, std::move(args));
    if (!r.ok && r.error.find("method not found") == std::string::npos &&
        r.error.find("not declared") == std::string::npos) {
      error = r.error;
      return false;
    }
  }
  return true;
}

bool readFloatFields(const VmObject *object, const char *const (&names)[3],
                     float (&out)[3]) {
  if (!object)
    return false;
  for (size_t i = 0; i < 3; ++i) {
    const VmField *match = nullptr;
    for (const auto &field : object->fields)
      if (field.name == names[i] && field.descriptor == "F")
        match = &field;
    if (!match || !std::holds_alternative<float>(match->value))
      return false;
    out[i] = std::get<float>(match->value);
  }
  return true;
}
} // namespace

// 00470910: slot compatibility between slot `slotId1` of part `proto1` and slot
// `slotId2` of part `proto2`. The comparisons, in stock order: slot A's attach
// list naming part B at slot B; slot B's attach list naming part A at slot A;
// A's attach list against B's `compatible` list; B's attach list against A's.
// Entries are (resource type id, slot id) pairs, so an `attach` line pairs with
// the other side's `compatible` line and never with another `attach` line.
bool carPartSlotsCompatible(const CarPrototype &proto1, int32_t slotId1,
                            const CarPrototype &proto2, int32_t slotId2) {
  const uint32_t typeId1 = proto1.source.cachedValue;
  const uint32_t typeId2 = proto2.source.cachedValue;
  // Stock rejects the same part on the same slot by resource type id (+8). An
  // unlinked prototype has no id, so identity stands in for it there.
  const bool sameType =
      typeId1 == typeId2 && (typeId1 != 0 || &proto1 == &proto2);
  if (sameType && slotId1 == slotId2)
    return false;
  const auto *slot1 = findProtoSlot(proto1, slotId1);
  const auto *slot2 = findProtoSlot(proto2, slotId2);
  if (!slot1 || !slot2)
    return false;
  for (const auto &att : slot1->attachments)
    if (att.resourceId == typeId2 && att.targetSlotId == slotId2)
      return true;
  for (const auto &att : slot2->attachments)
    if (att.resourceId == typeId1 && att.targetSlotId == slotId1)
      return true;
  for (const auto &att : slot1->attachments)
    for (const auto &comp : slot2->compatible)
      if (att.resourceId == comp.resource && att.targetSlotId == comp.value)
        return true;
  for (const auto &att : slot2->attachments)
    for (const auto &comp : slot1->compatible)
      if (att.resourceId == comp.resource && att.targetSlotId == comp.value)
        return true;
  return false;
}

// Rejection details are kept in memory until an install fails, so ordinary
// compatibility probes and successful installs do not flood the run log.
struct InstallTrace {
  struct Candidate {
    uint32_t target;
    int32_t requested;
    int32_t partSlot;
    int32_t targetSlot;
    unsigned reasons;
  };
  Candidate candidates[8]{};
  unsigned count = 0, omitted = 0, examined = 0, reasons = 0;
  void add(uint32_t target, int32_t requested, int32_t partSlot,
           int32_t targetSlot, unsigned reasons) {
    ++examined;
    this->reasons |= reasons;
    if (count < 8)
      candidates[count++] = {target, requested, partSlot, targetSlot, reasons};
    else
      ++omitted;
  }
};
// Candidate reason bits: 1=self/missing instance, 2=missing prototype,
// 4=target full, 8=part already attached, 16=target slot unavailable,
// 32=no compatible pair, 64=part slot unavailable.
static bool targetSlotFree(const RuntimeSlotState &slot, bool requireEmpty) {
  if (slot.attachedNode.owner != nullptr &&
      (requireEmpty || (slot.flags & 1u) == 0))
    return false;
  return (slot.flags & 0x10u) == 0;
}

static PartPhysics *hostPhysics(CarRuntimeInstance &inst) {
  if (inst.partPhysics)
    return inst.partPhysics.get();
  return inst.carPhysics ? inst.carPhysics->part.get() : nullptr;
}

// World position of a candidate target slot, as 00473790 computes it before
// comparing against the install position: the slot reference pose (getInfo
// 0x2f, including the wheel override) with the slot's own offset applied.
static bool targetSlotPosition(CarRuntimeInstance &inst,
                               const RuntimeSlotState &slot,
                               physics::Vec3 &out) {
  auto *physics = hostPhysics(inst);
  if (!physics)
    return false;
  out = slotReferencePosition(*physics, slot.slotType, slot.transform);
  return true;
}

static int32_t
findSlotOnSingleTarget(WorldNode &partNode, WorldNode &targetNode,
                       int32_t &inOutTargetSlotId, GameWorld &world,
                       InstallTrace *trace,
                       const std::array<float, 3> *preferred = nullptr) {
  const int32_t requested = inOutTargetSlotId;
  unsigned reasons = 0;
  auto result = [&](int32_t partSlot) {
    if (trace)
      trace->add(targetNode.instanceId, requested, partSlot, inOutTargetSlotId,
                 reasons);
    return partSlot;
  };
  if (&partNode == &targetNode || !partNode.carInstance ||
      !targetNode.carInstance) {
    reasons |= 1;
    return result(0);
  }
  const auto *partProto = nodePrototype(partNode, world);
  const auto *targetProto = nodePrototype(targetNode, world);
  if (!partProto || !targetProto) {
    reasons |= 2;
    return result(0);
  }
  auto &partInst = *partNode.carInstance;
  auto &targetInst = *targetNode.carInstance;
  // getInfo(0x27) is the target's free slot count; stock gives up before
  // touching any slot record when it is zero (all.c:85992).
  if (static_cast<int32_t>(targetInst.slots.size()) -
          static_cast<int32_t>(targetInst.usedSlots) <=
      0) {
    reasons |= 4;
    return result(0);
  }
  for (const auto &slot : partInst.slots)
    if (slot && (slot->flags & 1u) != 0 &&
        slot->attachedNode.owner != nullptr) {
      reasons |= 8;
      return result(0);
    }
  if (inOutTargetSlotId == 0) {
    // Stock scans the target's slots and keeps the nearest match when a
    // position was supplied; without one the first match wins.
    int32_t bestPartSlot = 0, bestTargetSlot = 0;
    float bestDistance = 0.0f;
    for (const auto &slot2 : targetInst.slots) {
      if (!slot2 || !targetSlotFree(*slot2, false)) {
        reasons |= 16;
        continue;
      }
      for (const auto &slot1 : partInst.slots) {
        if (!slot1 || (slot1->flags & 0x10u) != 0) {
          reasons |= 64;
          continue;
        }
        if (slot1->attachedNode.owner != nullptr && (slot1->flags & 1u) == 0) {
          reasons |= 64;
          continue;
        }
        if (!carPartSlotsCompatible(*targetProto, slot2->id, *partProto,
                                    slot1->id))
          continue;
        if (!preferred) {
          inOutTargetSlotId = slot2->id;
          return result(slot1->id);
        }
        physics::Vec3 position{};
        if (!targetSlotPosition(targetInst, *slot2, position))
          continue;
        const float distance =
            (position[0] - (*preferred)[0]) * (position[0] - (*preferred)[0]) +
            (position[1] - (*preferred)[1]) * (position[1] - (*preferred)[1]) +
            (position[2] - (*preferred)[2]) * (position[2] - (*preferred)[2]);
        if (bestPartSlot == 0 || distance < bestDistance) {
          bestPartSlot = slot1->id;
          bestTargetSlot = slot2->id;
          bestDistance = distance;
        }
      }
    }
    if (bestPartSlot != 0) {
      inOutTargetSlotId = bestTargetSlot;
      return result(bestPartSlot);
    }
    reasons |= 32;
    return result(0);
  }
  auto *targetSlot = findRuntimeSlot(targetInst, inOutTargetSlotId);
  if (!targetSlot || !targetSlotFree(*targetSlot, true)) {
    reasons |= 16;
    return result(0);
  }
  for (const auto &slot1 : partInst.slots) {
    if (!slot1 || (slot1->flags & 0x10u) != 0) {
      reasons |= 64;
      continue;
    }
    if (slot1->attachedNode.owner != nullptr && (slot1->flags & 1u) == 0) {
      reasons |= 64;
      continue;
    }
    if (carPartSlotsCompatible(*partProto, slot1->id, *targetProto,
                               inOutTargetSlotId))
      return result(slot1->id);
  }
  reasons |= 32;
  return result(0);
}

// getInfo(0x31) on a car returns its attached-part list (car+0xdc). Stock keeps
// the list in step with the slot records, so the port reads both: the host slot
// an installed part took, then the car's registry (which also holds parts
// registered before their slot pair was recorded).
static void
forEachInstalledPart(CarRuntimeInstance &carInstance, GameWorld &world,
                     const std::function<void(WorldNode &)> &visit) {
  std::vector<WorldNode *> seen;
  auto visitOnce = [&](WorldNode *node) {
    if (!node || !node->carInstance)
      return;
    for (const auto *other : seen)
      if (other == node)
        return;
    seen.push_back(node);
    visit(*node);
  };
  for (const auto &slot : carInstance.slots)
    if (slot && slot->attachedNode.owner != nullptr)
      visitOnce(world.nodeFor(slot->attachedNode.owner));
  for (const auto &link : carInstance.attachedParts)
    if (link && link->owner != nullptr)
      visitOnce(world.nodeFor(link->owner));
}

// 00473b10: try the requested target, then (when the target is a car) its
// installed parts through getInfo(0x31), then the same two against the part's
// install parent. Stock's own-slots-first rule lives in findSlotOnSingleTarget,
// which walks the part's slots and asks the target for a free one per part slot
// (004720a0 for slot 0, 00470910 for an explicit slot).
static int32_t
findCompatibleSlot(WorldNode &partNode, WorldNode *&inOutTargetNode,
                   int32_t &inOutTargetSlotId, GameWorld &world,
                   InstallTrace *trace,
                   const std::array<float, 3> *preferred = nullptr) {
  if (!partNode.carInstance)
    return 0;
  auto tryTarget = [&](WorldNode &target) -> int32_t {
    int32_t slot2 = inOutTargetSlotId;
    const int32_t slot1 = findSlotOnSingleTarget(partNode, target, slot2, world,
                                                 trace, preferred);
    if (slot1 == 0)
      return 0;
    inOutTargetNode = &target;
    inOutTargetSlotId = slot2;
    return slot1;
  };
  auto tryTargetAndChildren = [&](WorldNode &target) -> int32_t {
    const int32_t slot1 = tryTarget(target);
    if (slot1 != 0)
      return slot1;
    if (target.nativeDevice != "car" || !target.carInstance)
      return 0;
    int32_t found = 0;
    forEachInstalledPart(*target.carInstance, world, [&](WorldNode &child) {
      if (found == 0)
        found = tryTarget(child);
    });
    return found;
  };
  if (inOutTargetNode)
    if (const int32_t slot1 = tryTargetAndChildren(*inOutTargetNode))
      return slot1;
  WorldNode *parentNode =
      world.nodeFor(partNode.carInstance->installParentLink.owner);
  if (parentNode && parentNode != inOutTargetNode)
    if (const int32_t slot1 = tryTargetAndChildren(*parentNode))
      return slot1;
  return 0;
}

int32_t carPartFindCompatibleSlot(WorldNode &partNode, WorldNode *&target,
                                  int32_t &slot, GameWorld &world,
                                  const std::array<float, 3> *preferred) {
  return findCompatibleSlot(partNode, target, slot, world, nullptr, preferred);
}

// FUN_00419860(0xa0000000,...)->+0x4c: the host record whose slots the install
// commits into, plus part+0x44 (the owning car). Stock never reaches the car
// through a sector root, so the port resolves the car from the target itself,
// then the target's and the part's car links, and only then the sector link the
// world tree happened to give the node.
static WorldNode *resolveCarNode(WorldNode *targetNode, WorldNode &partNode,
                                 GameWorld &world) {
  auto isCar = [](WorldNode *node) {
    return node && node->nativeDevice == "car" && node->carInstance;
  };
  auto carLinkNode = [&](WorldNode *node) -> WorldNode * {
    return node && node->carInstance
               ? world.nodeFor(node->carInstance->carLink.owner)
               : nullptr;
  };
  if (isCar(targetNode))
    return targetNode;
  if (WorldNode *car = carLinkNode(targetNode))
    if (isCar(car))
      return car;
  if (WorldNode *car = carLinkNode(&partNode))
    if (isCar(car))
      return car;
  for (WorldNode *node : {targetNode, &partNode}) {
    if (!node)
      continue;
    WorldNode *sector = world.nodeFor(node->sectorLink.owner);
    if (isCar(sector))
      return sector;
  }
  return nullptr;
}

void carPartUpdateMass(WorldNode &node, GameWorld &world) {
  if (!node.carInstance)
    return;
  WorldNode *root = &node;
  if (node.nativeDevice != "car")
    if (WorldNode *car = resolveCarNode(nullptr, node, world))
      root = car;
  auto computeSubtreeMass = [&](auto &self, WorldNode &curr) -> float {
    if (!curr.carInstance)
      return 0.0f;
    const auto *proto = nodePrototype(curr, world);
    float sum = proto ? prototypeMass(*proto) : 0.0f;
    for (const auto &slot : curr.carInstance->slots) {
      if (slot && (slot->flags & 2u) != 0 && slot->attachedNode.owner) {
        if (auto *child = world.nodeFor(slot->attachedNode.owner))
          sum += self(self, *child);
      }
    }
    curr.carInstance->totalMass = sum;
    return sum;
  };
  // The host slots already reach every installed part (flags & 2 carries the
  // part on the car's slot), so the subtree walk is the whole total. Summing
  // the registry as well would count every part twice.
  computeSubtreeMass(computeSubtreeMass, *root);
}

bool carPartInstall(WorldNode &partNode, WorldNode *explicitTarget,
                    int32_t explicitTargetSlotId, VmArena &arena,
                    std::string &error, const std::array<float, 3> *preferred) {
  if (!arena.gameState || !arena.gameState->world || !partNode.carInstance)
    return false;
  auto &world = *arena.gameState->world;
  static std::atomic<uint32_t> nextTrace{0};
  const uint32_t traceId = ++nextTrace;
  InstallTrace trace;
  WorldNode *targetNode = explicitTarget;
  int32_t targetSlotId = explicitTargetSlotId;
  auto parentId = [&](const WorldNode *node) -> int64_t {
    if (!node || !node->carInstance)
      return -1;
    auto *parent = world.nodeFor(node->carInstance->installParentLink.owner);
    return parent ? parent->instanceId : 0;
  };
  auto hasPhysics = [](const WorldNode *node) {
    return node && node->carInstance &&
           (node->carInstance->partPhysics ||
            (node->carInstance->carPhysics &&
             node->carInstance->carPhysics->part));
  };
  const uint32_t partId = partNode.instanceId;
  const uint32_t beforeParent = static_cast<uint32_t>(parentId(&partNode));
  const bool partPhysicsBefore = hasPhysics(&partNode);
  runlog::event(
      runlog::EventKind::Note,
      "asm1 "
      "{\"v\":1,\"k\":\"req\",\"id\":%u,\"part\":%u,\"target\":%u,\"slot\":%d}",
      traceId, partNode.instanceId,
      explicitTarget ? explicitTarget->instanceId : 0, explicitTargetSlotId);
  int32_t partSlotId = findCompatibleSlot(partNode, targetNode, targetSlotId,
                                          world, &trace, preferred);
  const bool targetPhysicsBefore = hasPhysics(targetNode);
  const uint32_t resolvedId = targetNode ? targetNode->instanceId : 0;
  auto finish = [&](const char *reason, bool ok) {
    if (!ok)
      for (unsigned i = 0; i < trace.count; ++i) {
        const auto &c = trace.candidates[i];
        runlog::event(
            runlog::EventKind::Note,
            "asm1 "
            "{\"v\":1,\"k\":\"cand\",\"id\":%u,\"n\":%u,\"candidate\":%u,"
            "\"requested\":%d,\"ps\":%d,\"ts\":%d,\"reasons\":%u}",
            traceId, i, c.target, c.requested, c.partSlot, c.targetSlot,
            c.reasons);
      }
    if (!ok && runlog::active()) {
      auto snapshot = [&](WorldNode *node, unsigned candidate) {
        if (!node || !node->carInstance)
          return;
        const auto *proto = nodePrototype(*node, world);
        const auto &instance = *node->carInstance;
        unsigned written = 0;
        for (const auto &slot : instance.slots) {
          if (!slot || written == 8)
            continue;
          auto *attached = world.nodeFor(slot->attachedNode.owner);
          runlog::event(runlog::EventKind::Note,
                        "asm1 "
                        "{\"v\":1,\"k\":\"slot\",\"id\":%u,\"c\":%u,\"node\":%"
                        "u,\"slot\":%d,\"flags\":%u,\"attached\":%u}",
                        traceId, candidate, node->instanceId, slot->id,
                        slot->flags, attached ? attached->instanceId : 0);
          ++written;
        }
        runlog::event(
            runlog::EventKind::Note,
            "asm1 "
            "{\"v\":1,\"k\":\"inventory\",\"id\":%u,\"c\":%u,\"node\":%u,"
            "\"type\":%u,\"slots\":%zu,\"shown\":%u,\"links\":%zu}",
            traceId, candidate, node->instanceId,
            proto ? proto->source.cachedValue : 0, instance.slots.size(),
            written, instance.attachedParts.size());
      };
      snapshot(world.findInstance(partId), 8);
      for (unsigned i = 0; i < trace.count; ++i)
        snapshot(world.findInstance(trace.candidates[i].target), i);
    }
    runlog::event(
        runlog::EventKind::Note,
        "asm1 "
        "{\"v\":1,\"k\":\"summary\",\"id\":%u,\"examined\":%u,\"reasons\":%u}",
        traceId, trace.examined, trace.reasons);
    auto *livePart = world.findInstance(partId);
    auto *liveTarget = resolvedId ? world.findInstance(resolvedId) : nullptr;
    runlog::event(
        runlog::EventKind::Note,
        "asm1 "
        "{\"v\":1,\"k\":\"state\",\"id\":%u,\"parent0\":%u,\"parent1\":%lld,"
        "\"pp0\":%d,\"pp1\":%d,\"tp0\":%d,\"tp1\":%d,\"omitted\":%u}",
        traceId, beforeParent, static_cast<long long>(parentId(livePart)),
        partPhysicsBefore ? 1 : 0,
        livePart ? (hasPhysics(livePart) ? 1 : 0) : -1,
        targetPhysicsBefore ? 1 : 0,
        liveTarget ? (hasPhysics(liveTarget) ? 1 : 0) : -1,
        ok ? 0 : trace.omitted);
    runlog::event(runlog::EventKind::Note,
                  "asm1 "
                  "{\"v\":1,\"k\":\"result\",\"id\":%u,\"why\":\"%s\","
                  "\"resolved\":%u,\"ps\":%d,\"ts\":%d}",
                  traceId, reason, resolvedId, partSlotId, targetSlotId);
    return ok;
  };
  auto failed = [&](const char *reason) { return finish(reason, false); };
  if (partSlotId == 0 || !targetNode || !targetNode->carInstance)
    return failed("no_slot");
  auto &partInst = *partNode.carInstance;
  auto &targetInst = *targetNode->carInstance;
  auto *partSlot = findRuntimeSlot(partInst, partSlotId);
  auto *targetSlot = findRuntimeSlot(targetInst, targetSlotId);
  if (!partSlot || !targetSlot)
    return failed("slot_missing");
  PartPhysics *targetPhysics = targetInst.partPhysics.get();
  if (!targetPhysics && targetInst.carPhysics)
    targetPhysics = targetInst.carPhysics->part.get();
  if (partInst.partPhysics) {
    const auto slotType = static_cast<uint32_t>(targetSlot->slotType) & 0xffffu;
    if (!targetPhysics) {
      error = std::string("physical slot body is unavailable: part=") +
              (partInst.partPhysics ? "present" : "missing") +
              " target=" + (targetPhysics ? "present" : "missing") +
              " slot=" + std::to_string(targetSlotId) +
              " type=" + std::to_string(slotType);
      return failed("target_physics");
    }
    if (!attachPartPhysics(*partInst.partPhysics, partSlot->transform,
                           *targetPhysics, targetSlot->transform,
                           targetSlot->slotType)) {
      error = "invalid physical part attachment";
      return failed("attach_physics");
    }
    const auto &pose =
        partInst.partPhysics->root->state[partInst.partPhysics->root->current];
    partInst.position = pose.pos;
    partInst.rotation = {pose.rot[0], pose.rot[1], pose.rot[2],
                         pose.rot[4], pose.rot[5], pose.rot[6],
                         pose.rot[8], pose.rot[9], pose.rot[10]};
  }
  if (partInst.installParentLink.owner) {
    world.reparent(partNode, *partInst.installParentLink.owner);
  }
  partInst.stateMode = 1;
  partSlot->flags = (partSlot->flags & ~4u) | 1u;
  setLinkOwner(partSlot->attachedNode, targetNode);
  partSlot->attachedSlotId = targetSlotId;
  ++partInst.usedSlots;

  targetSlot->flags = (targetSlot->flags & ~4u) | 2u;
  setLinkOwner(targetSlot->attachedNode, &partNode);
  targetSlot->attachedSlotId = partSlotId;
  ++targetInst.usedSlots;

  if (targetInst.wheelId != -1) {
    partInst.wheelId = targetInst.wheelId;
  } else if (targetSlot->id > 100 && targetSlot->id < 401) {
    partInst.wheelId = (targetSlot->id - 101) % 10;
  }

  WorldNode *carRoot = resolveCarNode(targetNode, partNode, world);
  if (carRoot && carRoot->carInstance) {
    setLinkOwner(partInst.carLink, carRoot);
    if (carRoot->nativeDevice == "car") {
      // 004469xx enables the engine when the part installed at slot 0x191
      // is linked to the car; removing that link clears the same flag.
      if (targetSlotId == 0x191)
        carRoot->carInstance->engineRunning = true;
      // FUN_00469120 follows the car's chassis link; this port represents
      // the car and its chassis with the same root script instance.
      if (!carRoot->carInstance->chassisObject && carRoot->scriptReceiver)
        carRoot->carInstance->chassisObject = carRoot->scriptReceiver;
      bool exists = false;
      for (const auto &link : carRoot->carInstance->attachedParts)
        if (link->owner == &partNode)
          exists = true;
      if (!exists) {
        auto link = std::make_unique<OwnerLink>();
        setLinkOwner(*link, &partNode);
        carRoot->carInstance->attachedParts.push_back(std::move(link));
      }
      if (carRoot->scriptReceiver && arena.callScript) {
        auto alloc = vmAllocateNative(arena, "java.util.resource.GameRef");
        if (alloc.ok && std::holds_alternative<const VmObject *>(alloc.value)) {
          const auto *refObj = std::get<const VmObject *>(alloc.value);
          auto *mutObj = const_cast<VmObject *>(refObj);
          const int32_t h =
              static_cast<int32_t>(arena.resourceStates.size() + 1);
          for (auto &f : mutObj->fields)
            if (f.name == "ptr" && f.descriptor == "I")
              f.value = h;
          arena.resourceStates.push_back({refObj, {}});
          arena.resourceStates.back().words[2] = partNode.instanceId;
          auto b = std::make_unique<ReceiverBinding>();
          setLinkOwner(b->link, &partNode);
          arena.gameState->bindings.emplace(h, std::move(b));
          if (!callIfDeclared(arena, carRoot->scriptReceiver, "addpart",
                              "(Ljava.util.resource.GameRef;)V",
                              {VmValue{refObj}}, error))
            return failed("addpart_callback");
        }
      }
    }
  } else {
    setLinkOwner(partInst.carLink, targetInst.carLink.owner
                                       ? targetInst.carLink.owner
                                       : targetNode);
  }

  carPartUpdateMass(partNode, world);
  if (!callIfDeclared(arena, partNode.scriptReceiver, "install", "()V", {},
                      error))
    return failed("install_callback");
  return finish("ok", true);
}

bool carPartRemove(WorldNode &partNode, int32_t slotId, VmArena &arena,
                   std::string &error) {
  if (!arena.gameState || !arena.gameState->world || !partNode.carInstance)
    return false;
  auto &world = *arena.gameState->world;
  auto &partInst = *partNode.carInstance;
  RuntimeSlotState *partSlot = nullptr;
  if (slotId == 0) {
    for (auto &slot : partInst.slots) {
      if (slot && (slot->flags & 1u) != 0 && slot->attachedNode.owner) {
        partSlot = slot.get();
        break;
      }
    }
  } else {
    partSlot = findRuntimeSlot(partInst, slotId);
  }
  if (!partSlot || !partSlot->attachedNode.owner)
    return false;
  if ((partSlot->flags & 1u) && partInst.partPhysics) {
    if (!partInst.partPhysics->installedChildren.empty()) {
      error = "removing a part with installed physical children is unsupported";
      return false;
    }
    detachPartPhysics(*partInst.partPhysics);
  }
  WorldNode *otherNode = world.nodeFor(partSlot->attachedNode.owner);
  const int32_t otherSlotId = partSlot->attachedSlotId;
  static std::atomic<uint32_t> nextRemoveTrace{0};
  const uint32_t removeTraceId = nextRemoveTrace.fetch_add(1) + 1;
  runlog::event(runlog::EventKind::Note,
                "asmr {\"v\":1,\"k\":\"req\",\"id\":%u,\"part\":%u,\"slot\":%d,"
                "\"found\":%d,\"tgt\":%u,\"tsl\":%d}",
                removeTraceId, partNode.instanceId, slotId, partSlot->id,
                otherNode ? otherNode->instanceId : 0, otherSlotId);

  partSlot->flags &= ~9u;
  setLinkOwner(partSlot->attachedNode, nullptr);
  partSlot->attachedSlotId = 0;
  if (partInst.usedSlots > 0)
    --partInst.usedSlots;

  if (otherNode && otherNode->carInstance) {
    if (auto *otherSlot =
            findRuntimeSlot(*otherNode->carInstance, otherSlotId)) {
      otherSlot->flags &= ~0xau;
      setLinkOwner(otherSlot->attachedNode, nullptr);
      otherSlot->attachedSlotId = 0;
      if (otherNode->carInstance->usedSlots > 0)
        --otherNode->carInstance->usedSlots;
    }
    carPartUpdateMass(*otherNode, world);
  }

  WorldNode *carRoot = resolveCarNode(otherNode, partNode, world);
  if (carRoot && carRoot->nativeDevice == "car" && carRoot->carInstance) {
    if (otherSlotId == 0x191)
      carRoot->carInstance->engineRunning = false;
    // Detach before destruction: an erased OwnerLink still sits on the
    // part's LinkOwner list (CarRuntimeInstance destructor pattern).
    const size_t registryBefore = carRoot->carInstance->attachedParts.size();
    const unsigned usedBefore = partInst.usedSlots + 1;
    unsigned detached = 0;
    for (auto &link : carRoot->carInstance->attachedParts) {
      if (!link)
        continue;
      if (!link->owner || link->owner == &partNode) {
        setLinkOwner(*link, nullptr);
        ++detached;
      }
    }
    carRoot->carInstance->attachedParts.erase(
        std::remove_if(carRoot->carInstance->attachedParts.begin(),
                       carRoot->carInstance->attachedParts.end(),
                       [&](const auto &link) {
                         return !link->owner || link->owner == &partNode;
                       }),
        carRoot->carInstance->attachedParts.end());
    runlog::event(
        runlog::EventKind::Note,
        "asmr {\"v\":1,\"k\":\"done\",\"id\":%u,\"det\":%u,\"rb\":%zu,"
        "\"ra\":%zu,\"ub\":%u,\"ua\":%u}",
        removeTraceId, detached, registryBefore,
        carRoot->carInstance->attachedParts.size(), usedBefore,
        partInst.usedSlots);
    if (carRoot->scriptReceiver && arena.callScript) {
      auto alloc = vmAllocateNative(arena, "java.util.resource.GameRef");
      if (alloc.ok && std::holds_alternative<const VmObject *>(alloc.value)) {
        const auto *refObj = std::get<const VmObject *>(alloc.value);
        auto *mutObj = const_cast<VmObject *>(refObj);
        const int32_t h = static_cast<int32_t>(arena.resourceStates.size() + 1);
        for (auto &f : mutObj->fields)
          if (f.name == "ptr" && f.descriptor == "I")
            f.value = h;
        arena.resourceStates.push_back({refObj, {}});
        arena.resourceStates.back().words[2] = partNode.instanceId;
        auto b = std::make_unique<ReceiverBinding>();
        setLinkOwner(b->link, &partNode);
        arena.gameState->bindings.emplace(h, std::move(b));
        if (!callIfDeclared(arena, carRoot->scriptReceiver, "rempart",
                            "(Ljava.util.resource.GameRef;)V",
                            {VmValue{refObj}}, error))
          return false;
      }
    }
  }
  partInst.stateMode = 2;
  setLinkOwner(partInst.carLink, &partNode);
  partInst.wheelId = -1;
  carPartUpdateMass(partNode, world);
  if (!callIfDeclared(arena, partNode.scriptReceiver, "remove", "()V", {},
                      error))
    return false;
  return true;
}

NativeResult allocateVector3(VmArena *arena,
                             const std::vector<const TufaClass *> *classes,
                             float x, float y, float z) {
  if (!arena)
    return std::string{"Vector3 allocation: missing arena"};
  if (arena->ensureClass) {
    auto ensured = arena->ensureClass("java.lang.Vector3");
    if (!ensured.ok)
      return "Vector3 allocation: " + ensured.error;
  }
  if (arena->objects.size() >= arena->limit)
    return std::string{"Vector3 allocation: arena full"};
  const TufaClass *cls = nullptr;
  if (classes) {
    for (const auto *c : *classes)
      if (c && c->name == "java.lang.Vector3")
        cls = c;
  }
  if (!cls)
    return std::string{"Vector3 allocation: class unavailable"};
  auto obj = std::make_unique<VmObject>();
  obj->runtimeClass = cls;
  obj->fields.push_back({"x", x, "F"});
  obj->fields.push_back({"y", y, "F"});
  obj->fields.push_back({"z", z, "F"});
  const VmObject *ptr = obj.get();
  arena->objects.push_back(std::move(obj));
  return VmValue{ptr};
}

NativeResult allocateYpr(VmArena *arena,
                         const std::vector<const TufaClass *> *classes, float y,
                         float p, float r) {
  if (!arena)
    return std::string{"Ypr allocation: missing arena"};
  if (arena->ensureClass) {
    auto ensured = arena->ensureClass("java.lang.Ypr");
    if (!ensured.ok)
      return "Ypr allocation: " + ensured.error;
  }
  if (arena->objects.size() >= arena->limit)
    return std::string{"Ypr allocation: arena full"};
  const TufaClass *cls = nullptr;
  if (classes) {
    for (const auto *c : *classes)
      if (c && c->name == "java.lang.Ypr")
        cls = c;
  }
  if (!cls)
    return std::string{"Ypr allocation: class unavailable"};
  auto obj = std::make_unique<VmObject>();
  obj->runtimeClass = cls;
  obj->fields.push_back({"y", y, "F"});
  obj->fields.push_back({"p", p, "F"});
  obj->fields.push_back({"r", r, "F"});
  const VmObject *ptr = obj.get();
  arena->objects.push_back(std::move(obj));
  return VmValue{ptr};
}

NativeResult gameTypeRemNotification(const std::vector<VmValue> &args,
                                     const VmObject *receiver, VmArena *arena,
                                     const std::vector<const TufaClass *> *) {
  if (!arena || !arena->gameState || !arena->gameState->world ||
      args.size() != 2 || !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]))
    return std::string{"GameType.remNotification: invalid arguments"};
  const auto *targetObject = std::get<const VmObject *>(args[0]);
  if (!targetObject)
    return std::monostate{};
  auto &game = *arena->gameState;
  auto resolve = [&](const VmObject *object) -> WorldNode * {
    const auto *state = nativeResourceState(object, arena);
    if (!state)
      return nullptr;
    const int32_t handle =
        static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
    const auto it = game.bindings.find(handle);
    return it == game.bindings.end() || !it->second
               ? nullptr
               : game.world->nodeFor(it->second->link.owner);
  };
  auto *source = resolve(targetObject);
  auto *target = resolve(receiver);
  if (!source || !target)
    return std::monostate{};
  const uint32_t mask = static_cast<uint32_t>(std::get<int32_t>(args[1]));
  auto &entries = source->eventSubscriptions;
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [&](const auto &old) {
                                 return old->target.owner == target &&
                                        (old->mask & mask) != 0;
                               }),
                entries.end());
  return std::monostate{};
}

NativeResult
gameRefCreateNative(const std::vector<VmValue> &args, const VmObject *receiver,
                    VmArena *arena,
                    const std::vector<const TufaClass *> *classes) {
  auto res = gameRefCreate(args, receiver, arena, classes);
  if (const auto *err = std::get_if<std::string>(&res))
    return *err;
  return std::monostate{};
}

NativeResult gameRefGetDetail(const std::vector<VmValue> &args,
                              const VmObject *receiver, VmArena *arena,
                              const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"GameRef.getDetail: invalid call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return VmValue{0.0f};
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = arena->gameState->bindings.find(handle);
  if (it == arena->gameState->bindings.end() || !it->second ||
      !it->second->link.owner)
    return VmValue{0.0f};
  const auto node =
      arena->gameState->resources->resolve(it->second->link.owner);
  if (node.worldNode)
    return VmValue{node.worldNode->loadPriority};
  if (node.packNode)
    return VmValue{node.packNode->loadPriority};
  return VmValue{0.0f};
}

NativeResult gameRefGetFlags(const std::vector<VmValue> &args,
                             const VmObject *receiver, VmArena *arena,
                             const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"GameRef.getFlags: invalid call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return VmValue{int32_t{0}};
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = arena->gameState->bindings.find(handle);
  if (it == arena->gameState->bindings.end() || !it->second ||
      !it->second->link.owner)
    return VmValue{int32_t{0}};
  const auto node =
      arena->gameState->resources->resolve(it->second->link.owner);
  uint32_t flags = node.worldNode  ? node.worldNode->flags
                   : node.packNode ? node.packNode->flags
                                   : 0;
  int32_t signedFlags = 0;
  std::memcpy(&signedFlags, &flags, sizeof signedFlags);
  return VmValue{signedFlags};
}

static NativeResult gameRefGetInfoCommon(int32_t kind, int32_t intArg,
                                         const std::string *strArg,
                                         const VmObject *receiver,
                                         VmArena *arena) {
  if (!arena || !receiver || !arena->gameState ||
      !arena->gameState->resources || !arena->gameState->world)
    return std::string{"GameRef.getInfo: invalid context"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return VmValue{int32_t{0}};
  auto &game = *arena->gameState;
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return VmValue{int32_t{0}};
  const auto resolved = game.resources->resolve(it->second->link.owner);
  if (!resolved)
    return VmValue{int32_t{0}};
  if (resolved.worldNode) {
    auto &node = *resolved.worldNode;
    if (node.nativeDevice == "player" && node.playerInput) {
      if (kind == 0x1a && node.player)
        return VmValue{static_cast<int32_t>(node.player->osd.cachedValue)};
      if (kind == 0x19) {
        const auto sample = node.playerInput->sampleAxis(intArg);
        if (!sample)
          return VmValue{int32_t{0}};
        return VmValue{static_cast<int32_t>(*sample * 127.0f)};
      }
      if (kind == 8 || kind == 0x1c)
        return VmValue{int32_t{1}};
    }
    if ((node.nativeDevice == "car" || node.nativeDevice == "part") &&
        node.carInstance) {
      auto &inst = *node.carInstance;
      const bool isCar = node.nativeDevice == "car";
      const auto *proto = nodePrototype(node, *game.world);
      switch (kind) {
      case 1:
        if (isCar)
          return VmValue{static_cast<int32_t>(inst.physicsRoot.cachedValue)};
        break;
      case 0x18:
        if (isCar)
          for (const auto &seat : inst.seats)
            if (seat && seat->player.owner)
              return VmValue{static_cast<int32_t>(seat->player.cachedValue)};
        return VmValue{int32_t{0}};
      case 0x15:
        if (isCar)
          return VmValue{inst.activateInfo}; // stock car +0x1edc (all.c:68209)
        break;
      case 0xe8: { // GII_TYPE (in stock 00458c00/00475ec0, or script resource
                   // id)
        if (inst.prototypeSource.cachedValue)
          return VmValue{
              static_cast<int32_t>(inst.prototypeSource.cachedValue)};
        if (node.scriptLink.cachedValue)
          return VmValue{static_cast<int32_t>(node.scriptLink.cachedValue)};
        return VmValue{static_cast<int32_t>(isCar ? 5 : 6)};
      }
      case 0xe9:
        if (isCar)
          return VmValue{int32_t{1}};
        return VmValue{static_cast<int32_t>(inst.carLink.cachedValue
                                                ? inst.carLink.cachedValue
                                                : node.instanceId)};
      case 0xea:
        if (isCar)
          return VmValue{static_cast<int32_t>(node.instanceId)};
        return VmValue{inst.wheelId};
      case 0xeb:
        return VmValue{static_cast<int32_t>(node.instanceId)};
      case 0xec:
        return VmValue{int32_t{100}};
      case 0xed:
        return VmValue{static_cast<int32_t>(inst.wear * 100.0f)};
      case 0xee:
        return VmValue{static_cast<int32_t>(inst.tear * 100.0f)};
      case 0xef:
        return VmValue{
            static_cast<int32_t>((1.0f - inst.wear * inst.tear) * 100.0f)};
      case 0xf0:
        return VmValue{static_cast<int32_t>(node.instanceId)};
      case 0xf1:
        return VmValue{int32_t{1}};
      case 0xf2: { // GII_INSTALL_OK
        int32_t targetId = intArg;
        int32_t targetSlotId = 0;
        if (strArg) {
          std::istringstream in(*strArg);
          in >> std::setbase(0) >> targetId >> targetSlotId;
        }
        WorldNode *targetNode =
            targetId ? game.world->findInstance(static_cast<uint32_t>(targetId))
                     : nullptr;
        return VmValue{carPartFindCompatibleSlot(node, targetNode, targetSlotId,
                                                 *game.world)};
      }
      case 0xf3: { // GII_REMOVE_OK
        int32_t slotId = intArg;
        if (strArg) {
          std::istringstream in(*strArg);
          in >> std::setbase(0) >> slotId;
        }
        for (const auto &slot : inst.slots) {
          if (slot && (slot->flags & 2u) != 0 &&
              slot->attachedNode.owner != nullptr)
            return VmValue{int32_t{0}};
        }
        if (slotId == 0) {
          for (const auto &slot : inst.slots)
            if (slot && (slot->flags & 1u) != 0 &&
                slot->attachedNode.owner != nullptr)
              return VmValue{int32_t{1}};
          return VmValue{int32_t{0}};
        }
        const auto *slot = findRuntimeSlot(inst, slotId);
        return VmValue{
            int32_t{slot && slot->attachedNode.owner != nullptr ? 1 : 0}};
      }
      case 0xf5:
        return VmValue{int32_t{(inst.wear < 1.0f || inst.tear < 1.0f) ? 1 : 0}};
      case 0xf6:
        if (isCar) {
          int32_t mask = 0;
          for (const auto &w : inst.wheelExtras) {
            if (w.drive > 0.0f)
              mask |= 1;
          }
          return VmValue{int32_t{mask ? 3 : 0}};
        }
        return VmValue{int32_t{0}};
      case 0xf7:
        return VmValue{proto ? proto->config.category : int32_t{0}};
      case 0xf9:
        return VmValue{static_cast<int32_t>(inst.textureId)};
      case 0x27: // free slot count (car getInfo 0x27, all.c:68237)
        if (!isCar)
          break;
        return VmValue{static_cast<int32_t>(inst.slots.size()) -
                       static_cast<int32_t>(inst.usedSlots)};
      case 0x2f: { // slot reference object (part all.c:88165, car all.c:68272)
        // Stock callers use the result to pick the mount and to read the
        // reference body's pose. Every slot starts with its own part's object
        // as the reference (all.c:82667) and the commit replaces it with the
        // host slot's object (all.c:86420), so the reference is the node
        // installed on the slot, or the node itself when the slot hosts
        // nothing.
        const auto *slot = findRuntimeSlot(inst, intArg);
        if (!slot)
          return VmValue{static_cast<int32_t>(0)};
        const auto *reference =
            slot->attachedNode.owner
                ? game.world->nodeFor(slot->attachedNode.owner)
                : &node;
        return VmValue{
            static_cast<int32_t>(reference ? reference->instanceId : 0)};
      }
      default:
        break;
      }
    }
    if (node.scriptReceiver && arena->callScript) {
      auto r =
          arena->callScript(node.scriptReceiver, "getInfo", {VmValue{kind}});
      if (r.ok && !r.voidReturn && std::holds_alternative<int32_t>(r.value))
        return VmValue{std::get<int32_t>(r.value)};
    }
  }
  if (resolved.script && resolved.script->car) {
    if (kind == 0xf7)
      return VmValue{resolved.script->car->config.category};
    if (kind == 0xe8)
      return VmValue{static_cast<int32_t>(resolved.id)};
  }
  return VmValue{int32_t{0}};
}

NativeResult gameRefGetInfoIntInt(const std::vector<VmValue> &args,
                                  const VmObject *receiver, VmArena *arena,
                                  const std::vector<const TufaClass *> *) {
  if (args.size() != 2 || !std::holds_alternative<int32_t>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]))
    return std::string{"invalid GameRef.getInfo(II)I call"};
  return gameRefGetInfoCommon(std::get<int32_t>(args[0]),
                              std::get<int32_t>(args[1]), nullptr, receiver,
                              arena);
}

NativeResult gameRefGetInfoIntString(const std::vector<VmValue> &args,
                                     const VmObject *receiver, VmArena *arena,
                                     const std::vector<const TufaClass *> *) {
  if (args.size() != 2 || !std::holds_alternative<int32_t>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[1]) || !arena)
    return std::string{"invalid GameRef.getInfo(ILjava.lang.String;)I call"};
  const auto *strObj = std::get<const VmObject *>(args[1]);
  const std::string *str = strObj ? stringBytes(*arena, strObj) : nullptr;
  return gameRefGetInfoCommon(std::get<int32_t>(args[0]), 0, str, receiver,
                              arena);
}

// FUN_00487970 reads MouseCursor.cursor and selector 2 of the native
// cursor (FUN_004625f0), which supplies its normalized x/y coordinates.
NativeResult mouseCursorGetPos(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *classes) {
  if (!args.empty() || !receiver || !arena || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"MouseCursor.getPos: invalid call"};
  const VmObject *ref = nullptr;
  for (const auto &field : receiver->fields)
    if (field.name == "cursor" && std::holds_alternative<const VmObject *>(field.value))
      ref = std::get<const VmObject *>(field.value);
  if (!ref)
    return VmValue{static_cast<const VmObject *>(nullptr)};
  const auto *state = nativeResourceState(ref, arena);
  if (!state)
    return std::string{"MouseCursor.getPos: cursor native link unavailable"};
  const int32_t handle = static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = arena->gameState->bindings.find(handle);
  if (it == arena->gameState->bindings.end() || !it->second || !it->second->link.owner)
    return std::string{"MouseCursor.getPos: cursor binding unavailable"};
  const auto node = arena->gameState->resources->resolve(it->second->link.owner);
  if (!node.worldNode || !node.worldNode->cursor)
    return std::string{"MouseCursor.getPos: native cursor unavailable"};
  return allocateVector3(arena, classes, node.worldNode->cursor->x,
                         node.worldNode->cursor->y, 0.0f);
}

NativeResult gameRefGetPos(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *arena,
                           const std::vector<const TufaClass *> *classes) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"GameRef.getPos: invalid call"};
  float x = 0.0f, y = 0.0f, z = 0.0f;
  if (const auto *state = nativeResourceState(receiver, arena)) {
    const int32_t handle =
        static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
    auto it = arena->gameState->bindings.find(handle);
    if (it != arena->gameState->bindings.end() && it->second &&
        it->second->link.owner) {
      const auto node =
          arena->gameState->resources->resolve(it->second->link.owner);
      if (node.worldNode && node.worldNode->cameraDevice) {
        if (!arena->gameState->world || !node.worldNode->cameraDevice->physicsNode())
          return std::string{"GameRef.getPos: camera physics unavailable"};
        const auto body = arena->gameState->world->dynamicBody(
            *node.worldNode->cameraDevice->physicsNode()).lock();
        if (!body || body->current > 1)
          return std::string{"GameRef.getPos: camera physics unavailable"};
        const auto &position = body->state[body->current].pos;
        x = position[0]; y = position[1]; z = position[2];
      } else if (node.worldNode && node.worldNode->carInstance) {
        x = node.worldNode->carInstance->position[0];
        y = node.worldNode->carInstance->position[1];
        z = node.worldNode->carInstance->position[2];
      } else if (node.worldNode && node.worldNode->physicsBox) {
        x = node.worldNode->physicsBox->position[0][0];
        y = node.worldNode->physicsBox->position[0][1];
        z = node.worldNode->physicsBox->position[0][2];
      } else if (node.packNode && node.packNode->bounds) {
        x = node.packNode->bounds->values[0];
        y = node.packNode->bounds->values[1];
        z = node.packNode->bounds->values[2];
      }
    }
  }
  return allocateVector3(arena, classes, x, y, z);
}

NativeResult gameRefGetOri(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *arena,
                           const std::vector<const TufaClass *> *classes) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"GameRef.getOri: invalid call"};
  float y = 0.0f, p = 0.0f, r = 0.0f;
  if (const auto *state = nativeResourceState(receiver, arena)) {
    const int32_t handle =
        static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
    auto it = arena->gameState->bindings.find(handle);
    if (it != arena->gameState->bindings.end() && it->second &&
        it->second->link.owner) {
      const auto node =
          arena->gameState->resources->resolve(it->second->link.owner);
      if (node.worldNode && node.worldNode->cameraDevice) {
        if (!arena->gameState->world || !node.worldNode->cameraDevice->physicsNode())
          return std::string{"GameRef.getOri: camera physics unavailable"};
        const auto body = arena->gameState->world->dynamicBody(
            *node.worldNode->cameraDevice->physicsNode()).lock();
        if (!body || body->current > 1)
          return std::string{"GameRef.getOri: camera physics unavailable"};
        // FUN_0048b300 reads the current body matrix and calls FUN_00551c90.
        const auto &m = body->state[body->current].rot;
        if (m[10] == 0.0f && m[2] == 0.0f) {
          y = std::atan2(-m[1], m[0]); p = 3.1415927f; r = 0.0f;
        } else {
          y = std::atan2(m[2], m[10]);
          r = std::atan2(m[4], m[5]);
          const float cr = std::cos(r);
          p = cr > 0.0f ? std::atan2(-cr * m[6], m[5])
                         : std::atan2(cr * m[6], -m[5]);
        }
      } else if (node.worldNode && node.worldNode->carInstance) {
        y = node.worldNode->carInstance->ypr[0];
        p = node.worldNode->carInstance->ypr[1];
        r = node.worldNode->carInstance->ypr[2];
      }
    }
  }
  return allocateYpr(arena, classes, y, p, r);
}

NativeResult gameRefGetVel(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *arena,
                           const std::vector<const TufaClass *> *classes) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"GameRef.getVel: invalid call"};
  float vx = 0.0f, vy = 0.0f, vz = 0.0f;
  if (const auto *state = nativeResourceState(receiver, arena)) {
    const int32_t handle =
        static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
    auto it = arena->gameState->bindings.find(handle);
    if (it != arena->gameState->bindings.end() && it->second &&
        it->second->link.owner) {
      const auto node =
          arena->gameState->resources->resolve(it->second->link.owner);
      if (node.worldNode && node.worldNode->carInstance) {
        vx = node.worldNode->carInstance->linearVelocity[0];
        vy = node.worldNode->carInstance->linearVelocity[1];
        vz = node.worldNode->carInstance->linearVelocity[2];
      }
    }
  }
  return allocateVector3(arena, classes, vx, vy, vz);
}

NativeResult gameRefGetScriptInstance(const std::vector<VmValue> &args,
                                      const VmObject *receiver, VmArena *arena,
                                      const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"GameRef.getScriptInstance: invalid call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return VmValue{static_cast<const VmObject *>(nullptr)};
  auto &game = *arena->gameState;
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return VmValue{static_cast<const VmObject *>(nullptr)};
  const auto node = game.resources->resolve(it->second->link.owner);
  if (node.type == 1 && node.worldNode)
    return VmValue{node.worldNode->scriptReceiver};
  if (node.type == 8 && node.script && !node.script->className.empty()) {
    auto allocated = vmAllocateNative(*arena, node.script->className);
    if (allocated.ok &&
        std::holds_alternative<const VmObject *>(allocated.value))
      return VmValue{std::get<const VmObject *>(allocated.value)};
  }
  return VmValue{static_cast<const VmObject *>(nullptr)};
}

NativeResult gameRefIsEmpty(const std::vector<VmValue> &args,
                            const VmObject *receiver, VmArena *arena,
                            const std::vector<const TufaClass *> *classes) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"GameRef.isEmpty: invalid call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return VmValue{int32_t{1}};
  auto &game = *arena->gameState;
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return VmValue{int32_t{1}};
  const auto node = game.resources->resolve(it->second->link.owner);
  if (node.type == 8 && node.packNode && node.script) {
    if (node.packNode->payloadSize == 0)
      return VmValue{int32_t{1}};
    if (!node.packNode->loaded() && classes) {
      (void)gameRefIsScripted({VmValue{static_cast<const VmObject *>(nullptr)}},
                              receiver, arena, classes);
    }
    if (!node.script->className.empty() || node.script->car != nullptr)
      return VmValue{int32_t{0}};
  }
  return VmValue{int32_t{1}};
}

NativeResult gameRefSetActiveCollision(const std::vector<VmValue> &args,
                                       const VmObject *receiver, VmArena *arena,
                                       const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || !args.empty() || !arena->gameState ||
      !arena->gameState->world || !arena->gameState->resources)
    return std::string{"GameRef.setActiveCollision: invalid call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"GameRef.setActiveCollision: native link unavailable"};
  auto &game = *arena->gameState;
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second ||
      !binding->second->link.owner)
    return std::string{"GameRef.setActiveCollision: target unavailable"};
  const auto target = game.resources->resolve(binding->second->link.owner);
  if (!target.worldNode || !game.world->queueCollisionUpdate(*target.worldNode))
    return std::string{"GameRef.setActiveCollision: target unsupported"};
  return std::monostate{};
}

// 0047e350/0047e490 send a six-float pose through 0048b440.
// Rebuild the registered compound in parent-first order and update its
// resource bounds together with the cached script pose.
static bool placeCar(VmGameState &game, CarRuntimeInstance &car,
                     const std::array<float, 3> &position,
                     const std::array<float, 3> &orientation) {
  // 0044c460: the compound root is placed at the ride-height corrected pose;
  // the script-visible pose below keeps the requested one.
  std::array<float, 3> rootPosition = position;
  std::array<float, 3> rootOrientation = orientation;
  if (car.carPhysics && car.carPhysics->root &&
      !carRideHeightPlacement(*car.carPhysics, position, orientation,
                              rootPosition, rootOrientation))
    return false;
  PartSlotDirective directive{};
  for (size_t i = 0; i < 3; ++i)
    directive.orientation[i] = rootOrientation[i];
  PartSlotState transform;
  if (!makePartSlotState(directive, transform))
    return false;
  const auto &r = transform.rotation;
  const physics::Mat34 matrix{r[0], r[1], r[2], 0,    r[3], r[4],
                              r[5], 0,    r[6], r[7], r[8], 0};
  if (car.carPhysics && car.carPhysics->root) {
    auto &compound = *car.carPhysics;
    auto &root = *compound.root;
    if (root.current > 1 || !root.proto || compound.teardownOrder.empty())
      return false;
    auto &state = root.state[root.current];
    state.pos = rootPosition;
    state.rot = matrix;
    state.quat = physics::quatFromMatrix(matrix);
    physics::updateWorldInertia(*root.proto, state);
    root.state[root.current ^ 1] = state;
    // Reverse teardown order is parent-first, including installed parts.
    for (auto it = compound.teardownOrder.rbegin();
         it != compound.teardownOrder.rend(); ++it) {
      auto &body = **it;
      if (&body != &root) {
        if (!body.attachment || body.current > 1 || !body.proto)
          return false;
        auto &childState = body.state[body.current];
        physics::predictChildPose(
            body.attachment->parent->state[body.attachment->parent->current],
            body.attachment->relative, childState);
        physics::updateWorldInertia(*body.proto, childState);
        body.state[body.current ^ 1] = childState;
      }
      const auto observation = compound.bodyObservations.find(&body);
      if (game.world && observation != compound.bodyObservations.end() &&
          observation->second && observation->second->resource.owner) {
        if (auto *node =
                game.world->nodeFor(observation->second->resource.owner)) {
          const auto &p = body.state[body.current].pos;
          const float radius = body.proto->shape.boundRadius;
          node->bounds.mode = 1;
          node->bounds.values = {p[0], p[1], p[2], radius, radius, radius};
        }
      }
    }
    if (runlog::active())
      runlog::event(runlog::EventKind::Note,
                    "car-placement {\"root\":[%.6f,%.6f,%.6f],"
                    "\"cached\":[%.6f,%.6f,%.6f]}",
                    state.pos[0], state.pos[1], state.pos[2], position[0],
                    position[1], position[2]);
  }
  car.position = position;
  car.ypr = orientation;
  car.rotation = transform.rotation;
  return true;
}

NativeResult gameRefSetMatrix(const std::vector<VmValue> &args,
                              const VmObject *receiver, VmArena *arena,
                              const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 2 ||
      !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[1]) || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"GameRef.setMatrix: invalid call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::monostate{};
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = arena->gameState->bindings.find(handle);
  if (it == arena->gameState->bindings.end() || !it->second ||
      !it->second->link.owner)
    return std::monostate{};
  const auto node =
      arena->gameState->resources->resolve(it->second->link.owner);
  const char *const xyz[] = {"x", "y", "z"};
  const char *const ypr[] = {"y", "p", "r"};
  float pos[3]{}, ori[3]{};
  const auto *positionObject = std::get<const VmObject *>(args[0]);
  const auto *orientationObject = std::get<const VmObject *>(args[1]);
  if ((positionObject && !readFloatFields(positionObject, xyz, pos)) ||
      (orientationObject && !readFloatFields(orientationObject, ypr, ori)))
    return std::string{"GameRef.setMatrix: invalid pose"};
  if (node.worldNode && node.worldNode->carInstance &&
      !placeCar(*arena->gameState, *node.worldNode->carInstance,
                {pos[0], pos[1], pos[2]}, {ori[0], ori[1], ori[2]}))
    return std::string{"GameRef.setMatrix: car placement failed"};
  return std::monostate{};
}

NativeResult gameRefSetPos(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *arena,
                           const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 1 ||
      !std::holds_alternative<const VmObject *>(args[0]) || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"GameRef.setPos: invalid call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::monostate{};
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = arena->gameState->bindings.find(handle);
  if (it == arena->gameState->bindings.end() || !it->second ||
      !it->second->link.owner)
    return std::monostate{};
  const auto node =
      arena->gameState->resources->resolve(it->second->link.owner);
  const char *const xyz[] = {"x", "y", "z"};
  float pos[3]{};
  if (readFloatFields(std::get<const VmObject *>(args[0]), xyz, pos) &&
      node.worldNode && node.worldNode->carInstance &&
      !placeCar(*arena->gameState, *node.worldNode->carInstance,
                {pos[0], pos[1], pos[2]}, {}))
    return std::string{"GameRef.setPos: car placement failed"};
  return std::monostate{};
}

NativeResult gameRefSetState(const std::vector<VmValue> &args,
                             const VmObject *receiver, VmArena *arena,
                             const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 4 ||
      !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[1]) ||
      !std::holds_alternative<const VmObject *>(args[2]) ||
      !std::holds_alternative<const VmObject *>(args[3]) || !arena->gameState ||
      !arena->gameState->resources)
    return std::string{"GameRef.setState: invalid call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::monostate{};
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = arena->gameState->bindings.find(handle);
  if (it == arena->gameState->bindings.end() || !it->second ||
      !it->second->link.owner)
    return std::monostate{};
  const auto node =
      arena->gameState->resources->resolve(it->second->link.owner);
  if (!node.worldNode || !node.worldNode->carInstance)
    return std::monostate{};
  const char *const xyz[] = {"x", "y", "z"};
  const char *const ypr[] = {"y", "p", "r"};
  float pos[3]{}, ori[3]{}, vel[3]{}, ang[3]{};
  auto &inst = *node.worldNode->carInstance;
  if (readFloatFields(std::get<const VmObject *>(args[0]), xyz, pos))
    inst.position = {pos[0], pos[1], pos[2]};
  if (readFloatFields(std::get<const VmObject *>(args[1]), ypr, ori))
    inst.ypr = {ori[0], ori[1], ori[2]};
  if (readFloatFields(std::get<const VmObject *>(args[2]), xyz, vel))
    inst.linearVelocity = {vel[0], vel[1], vel[2]};
  if (readFloatFields(std::get<const VmObject *>(args[3]), xyz, ang))
    inst.angularVelocity = {ang[0], ang[1], ang[2]};
  return std::monostate{};
}

NativeResult gameRefSetParent(const std::vector<VmValue> &args,
                              const VmObject *receiver, VmArena *arena,
                              const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 1 ||
      !std::holds_alternative<const VmObject *>(args[0]) || !arena->gameState ||
      !arena->gameState->world || !arena->gameState->resources)
    return std::string{"GameRef.setParent: invalid call"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::monostate{};
  auto &game = *arena->gameState;
  const int32_t handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  auto it = game.bindings.find(handle);
  if (it == game.bindings.end() || !it->second || !it->second->link.owner)
    return std::monostate{};
  WorldNode *node = game.world->nodeFor(it->second->link.owner);
  if (!node)
    return std::monostate{};
  LinkOwner *parentOwner = game.world->defaultParent();
  if (const auto *parentObj = std::get<const VmObject *>(args[0])) {
    if (const auto *parentState = nativeResourceState(parentObj, arena)) {
      const int32_t parentHandle =
          static_cast<int32_t>(parentState - arena->resourceStates.data()) + 1;
      auto pIt = game.bindings.find(parentHandle);
      if (pIt != game.bindings.end() && pIt->second &&
          pIt->second->link.owner) {
        parentOwner = pIt->second->link.owner;
      } else if (parentState->words[2] != 0) {
        auto found = game.resources->lookup(parentState->words[2], 1);
        if (found.node)
          parentOwner = found.node.owner;
      }
    }
  }
  if (parentOwner) {
    game.world->reparent(*node, *parentOwner);
    if (node->carInstance)
      setLinkOwner(node->carInstance->installParentLink, parentOwner);
  }
  return std::monostate{};
}
