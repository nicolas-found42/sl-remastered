#include "object_runtime.hpp"
#include "car_click_geometry.hpp"
#include "car_geometry.hpp"
#include "game_creation.hpp"

#include "natives/game_type.hpp"
#include "object_sound.hpp"
#include "run_log.hpp"
#include "scene_2d.hpp"
#include "vm.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace object {
namespace {

// An ICFG payload contains the native device name followed by a NUL and the
// class configuration. Instance placement comes from the resource's params,
// not from this type configuration.
// ICFG game-type payloads begin with the owning class name (e.g. "object",
// "trlight") and share the directive set. Return both so the caller can
// dispatch without hard-coding one class.
struct ObjectConfigText {
  std::string_view className;
  std::string_view text;
};

std::optional<ObjectConfigText> objectConfigText(const PackResource &resource) {
  if (!resource.packBytes || resource.entry.payloadSize < 8 ||
      resource.entry.payloadOffset > resource.packBytes->size() ||
      resource.entry.payloadSize >
          resource.packBytes->size() - resource.entry.payloadOffset)
    return std::nullopt;
  const auto *p = resource.packBytes->data() + resource.entry.payloadOffset;
  const size_t size = resource.entry.payloadSize;
  const uint32_t chunkSize = uint32_t(p[4]) | uint32_t(p[5]) << 8 |
                             uint32_t(p[6]) << 16 | uint32_t(p[7]) << 24;
  if (std::memcmp(p, "ICFG", 4) != 0 || chunkSize > size - 8)
    return std::nullopt;
  const auto *data = reinterpret_cast<const char *>(p + 8);
  const auto *end = static_cast<const char *>(std::memchr(data, 0, chunkSize));
  if (!end || end == data)
    return std::nullopt;
  return ObjectConfigText{
      std::string_view(data, end - data),
      std::string_view(end + 1, chunkSize - (end + 1 - data))};
}

} // namespace

ObjectRuntime::SlotResources::~SlotResources() {
  setLinkOwner(primary, nullptr);
  for (auto &variant : variants)
    setLinkOwner(*variant, nullptr);
}
ObjectRuntime::MaterialResources::~MaterialResources() {
  setLinkOwner(collisionSound, nullptr);
  setLinkOwner(resource, nullptr);
  setLinkOwner(impactSound, nullptr);
}

bool bindObjectMaterial(ResourceWorld &resources,
                        const object_config::MaterialDirective &directive,
                        ObjectRuntime &runtime, std::string &error) {
  auto material = std::make_unique<ObjectRuntime::MaterialResources>();
  auto bind = [&](uint32_t id, uint32_t type, OwnerLink &link) {
    const auto result = resources.lookup(id, type, false);
    if (result.status == LookupStatus::StockCrash ||
        result.status == LookupStatus::MountRefused) {
      error = "object material resource lookup: " + result.detail;
      return false;
    }
    if (result.status == LookupStatus::Found)
      setLinkOwner(link, result.node.owner);
    return true;
  };
  // Handler lookup order is generic resource, first sound, impact sound.
  if (!bind(directive.rpkId, 0, material->resource) ||
      !bind(directive.materialId, 6, material->collisionSound) ||
      !bind(directive.detailId, 6, material->impactSound))
    return false;
  material->threshold = directive.value;
  runtime.material = std::move(material);
  error.clear();
  return true;
}

ObjectRuntime::Render::~Render() { setLinkOwner(node, nullptr); }
ObjectRuntime::~ObjectRuntime() = default;

namespace {
physics::Vec3 vec(const object_config::Vec3 &v) { return {v.x, v.y, v.z}; }

int lodFor(const object_config::ObjectConfig &config, float visibility) {
  if (!config.hasLods || config.lods.thresholds.empty())
    return 0;
  const auto &thresholds = config.lods.thresholds;
  size_t level = 0;
  while (level + 1 < thresholds.size() && visibility >= thresholds[level])
    ++level;
  return static_cast<int>(level);
}

bool reconcile(ResourceWorld &resources, ResourceNode &node,
               ObjectRuntime &runtime, float visibility, std::string &error,
               std::optional<int32_t> gameTime) {
  if (!std::isfinite(visibility)) {
    error = "object LOD visibility is non-finite";
    return false;
  }
  auto &world = resources.world();
  if (runtime.root && runtime.root->current > 1) {
    error = "object physics pose unavailable";
    return false;
  }
  if (runtime.resources.size() != runtime.config.slots.size()) {
    error = "object render resources unavailable";
    return false;
  }
  if (!gameTime) {
    for (const auto &slot : runtime.config.slots)
      if (slot.kind == object_config::SlotKind::Render &&
          (slot.modifiers.flags & 4u)) {
        error = "object render game clock unavailable";
        return false;
      }
  }
  const bool night = gameTime && (*gameTime < 21600 || *gameTime > 75599);
  for (const auto &entry : runtime.renders) {
    auto *child = world.nodeFor(entry->node.owner);
    if (child && !resources.sceneRenderInstance(resources.resolve(child))) {
      error = "object render instance unavailable";
      return false;
    }
  }
  const int lod = lodFor(runtime.config, visibility);
  std::vector<std::unique_ptr<ObjectRuntime::Render>> added;
  auto rollback = [&] {
    for (auto &entry : added)
      if (auto *child = world.nodeFor(entry->node.owner))
        world.destroy(*child);
  };
  for (size_t i = 0; i < runtime.config.slots.size(); ++i) {
    const auto &slot = runtime.config.slots[i];
    if (slot.kind != object_config::SlotKind::Render ||
        lod < slot.modifiers.lodNear || lod > slot.modifiers.lodFar)
      continue;
    const auto existing =
        std::find_if(runtime.renders.begin(), runtime.renders.end(),
                     [i](const auto &r) { return r->slot == i; });
    if (existing != runtime.renders.end())
      continue;
    const uint32_t flags = slot.modifiers.flags;
    if ((flags & 4u) && (night == ((flags & 0x40u) != 0)))
      continue;
    auto &links = *runtime.resources[i];
    LinkOwner *type = links.primary.owner;
    if (!links.variants.empty())
      type = links
                 .variants[static_cast<size_t>(runtime.config.instance.id) %
                           links.variants.size()]
                 ->owner;
    if (!type) {
      auto entry = std::make_unique<ObjectRuntime::Render>();
      entry->slot = i;
      added.push_back(std::move(entry));
      continue;
    }
    std::optional<uint32_t> color;
    if (!slot.modifiers.colors.empty()) {
      const auto &selected =
          slot.modifiers
              .colors[static_cast<size_t>(runtime.config.instance.id) %
                      slot.modifiers.colors.size()];
      if (!selected) {
        // FUN_004656d0 (the colors directive handler) allocates count*4 bytes,
        // parses what the 256-byte line buffer holds and never errors: tail
        // entries the buffer cut off stay uninitialized and are read as-is.
        // The port must not abort the frame either. The stock value is
        // indeterminate heap, so this is a logged difference, not parity.
        static bool colorHoleLogged = false;
        if (!colorHoleLogged) {
          colorHoleLogged = true;
          runlog::event(runlog::EventKind::Note,
                        "object color entry truncated by the 256-byte stock "
                        "line buffer; stock reads it uninitialized, using 0");
        }
        color = 0;
      } else {
        color = *selected;
      }
    }
    auto *child = world.addTypedChildUnderExternal(node.handle, node, node.type,
                                                   node.sectorLink.owner, 3,
                                                   "obj_r_o", 0x200);
    if (!child) {
      rollback();
      error = "object render allocation failed";
      return false;
    }
    auto entry = std::make_unique<ObjectRuntime::Render>();
    entry->slot = i;
    setLinkOwner(entry->node, child);
    added.push_back(std::move(entry));
    child->bounds = node.bounds.value_or(ResourceBounds{});
    child->field6c = node.field6c;
    child->field70 = node.field70;
    auto scene = std::make_shared<SceneRenderInstance>(type);
    // Stock's render-type creation leaves the material bound before the
    // first draw. Ground and native RenderRef creation load it eagerly; ICFG
    // object renders must do the same or the camera draw rejects them.
    // Type-13 light slots are loaded by the camera draw itself.
    const auto typeResolved = resources.resolve(type);
    if (!typeResolved) {
      rollback();
      error = "object render type unresolved";
      return false;
    }
    if (typeResolved.type == 14 && !resources.sceneMaterial(typeResolved)) {
      auto loaded = sceneLoadMaterial(resources, typeResolved.id);
      if (!loaded.material ||
          !resources.bindSceneMaterial(typeResolved,
                                       std::shared_ptr<SceneMaterialResources>(
                                           std::move(loaded.material)))) {
        rollback();
        error =
            "object render material 0x" +
            [](uint32_t v) {
              char b[9];
              std::snprintf(b, sizeof b, "%08x", v);
              return std::string{b};
            }(typeResolved.id) +
            ": " +
            (loaded.error.empty() ? std::string{"bind failed"} : loaded.error);
        return false;
      }
    }
    if (color)
      scene->setInstanceColor(*color);
    const auto rotation =
        matrixFromEuler(vec(runtime.config.instance.orientation));
    auto position = physics::transformPoint(
        vec(runtime.config.instance.position), rotation, vec(slot.position));
    auto orientation = physics::matrixMultiply(
        rotation, matrixFromEuler(vec(slot.orientation)));
    auto &created = *added.back();
    if (runtime.root) {
      if (runtime.root->current > 1) {
        rollback();
        error = "object physics pose unavailable";
        return false;
      }
      const auto &state = runtime.root->state[runtime.root->current];
      const auto inverse =
          physics::matrixTranspose(runtime.initialRootRotation);
      physics::Vec3 delta{position[0] - runtime.initialRootPosition[0],
                          position[1] - runtime.initialRootPosition[1],
                          position[2] - runtime.initialRootPosition[2]};
      created.localPosition = physics::matrixTransformRows(inverse, delta);
      created.localRotation = physics::matrixMultiply(inverse, orientation);
      position =
          physics::transformPoint(state.pos, state.rot, created.localPosition);
      orientation = physics::matrixMultiply(state.rot, created.localRotation);
    } else {
      created.localPosition = position;
      created.localRotation = orientation;
    }
    scene->setBoneTransform(position, orientation);
    if (!resources.bindSceneRenderInstance(resources.resolve(child), scene)) {
      rollback();
      error = "object render binding failed";
      return false;
    }
    world.markLoaded(*child);
    child->loadPriority = 1.0f;
    world.noteInstanceCreated(*child);
  }
  // Stock walks existing render records backwards and removes by swapping the
  // last record into the hole before appending newly eligible slots.
  for (size_t i = runtime.renders.size(); i-- > 0;) {
    const auto &slot = runtime.config.slots[runtime.renders[i]->slot];
    if (lod >= slot.modifiers.lodNear && lod <= slot.modifiers.lodFar)
      continue;
    if (auto *child = world.nodeFor(runtime.renders[i]->node.owner))
      world.destroy(*child);
    if (i + 1 != runtime.renders.size())
      runtime.renders[i] = std::move(runtime.renders.back());
    runtime.renders.pop_back();
  }
  for (auto &entry : added)
    runtime.renders.push_back(std::move(entry));
  for (const auto &entry : runtime.renders) {
    auto *child = world.nodeFor(entry->node.owner);
    if (!child)
      continue;
    auto scene = resources.sceneRenderInstance(resources.resolve(child));
    if (!scene) {
      error = "object render instance unavailable";
      return false;
    }
    auto position = entry->localPosition;
    auto rotation = entry->localRotation;
    if (runtime.root) {
      const auto &state = runtime.root->state[runtime.root->current];
      position = physics::transformPoint(state.pos, state.rot, position);
      rotation = physics::matrixMultiply(state.rot, rotation);
    }
    scene->setBoneTransform(position, rotation);
  }
  runtime.lod = lod;
  error.clear();
  return true;
}
} // namespace

bool updateObjectLod(ResourceWorld &resources, ResourceNode &node,
                     float visibility, std::string &error,
                     std::optional<int32_t> gameTime) {
  if (!node.objectRuntime) {
    error = "object runtime unavailable";
    return false;
  }
  return reconcile(resources, node, *node.objectRuntime, visibility, error,
                   gameTime);
}

std::optional<std::string>
loadObjectGameResource(VmArena &arena, ResourceNode &node,
                       const PackResource &typeResource,
                       GameResourcePayload &payload) {
  if (!arena.gameState || !arena.gameState->world ||
      !arena.gameState->resources)
    return std::string{"object loader: resource world unavailable"};
  auto &resources = *arena.gameState->resources;
  auto &world = *arena.gameState->world;
  const auto resolved = resources.resolve(&linkOwner(node));
  if (resolved.packNode != &node || node.type != 1)
    return std::string{"object loader: invalid resource"};

  PackView view;
  std::string error;
  if (resources.packs().view(static_cast<uint16_t>(typeResource.id >> 16), view,
                             error) != PackViewStatus::Ok)
    return std::string{"object loader: object pack unavailable"};

  const auto text = objectConfigText(typeResource);
  if (!text)
    return std::string{"object config parse: invalid ICFG payload"};
  std::vector<object_config::ObjectConfigLine> lines;
  if (!object_config::parseObjectConfig(text->text, lines, error))
    return std::string{"object config parse: "} + error;
  uint32_t systemRpkId = 0;
  if (std::any_of(lines.begin(), lines.end(),
                  [](const auto &line) { return line.key == "material"; }) &&
      !resources.packs().resolveLiteral("system.rpk", 3, systemRpkId, error))
    return std::string{"object material default resource: "} + error;
  const ResourceIdMapping mapping{
      view.slot, static_cast<uint32_t>(resources.packs().slotCount()),
      view.dependencies};

  object_config::ObjectConfig config;
  if (!payload.parameters || !object_config::parseInstanceHeader(
                                 *payload.parameters, config.instance, error))
    return std::string{"object config parse: instance header: "} +
           (payload.parameters ? error : "missing params");
  for (const auto &line : lines) {
    if (!object_config::applyObjectConfigLine(line, 0, mapping, systemRpkId,
                                              config, error))
      return std::string{"object config directive '"} + line.key +
             "': " + error;
  }

  // Keep branches whose effects are not implemented explicit.
  auto unsupported = [](const char *directive) {
    return std::string{"object construction unsupported: "} + directive;
  };
  if (config.hasPowerup)
    return unsupported("powerup");
  if (config.hasSfxLoop)
    return unsupported("sfx_loop");
  if (!config.sfxRandom.empty() && !arena.objectSound)
    return unsupported("sfx_random sound host unavailable");
  for (const auto &slot : config.slots) {
    if (slot.modifiers.hasFlags &&
        (slot.kind != object_config::SlotKind::Render ||
         (slot.modifiers.flags & ~0x44u) != 0))
      return unsupported("flags");
    if (slot.kind == object_config::SlotKind::Part)
      return unsupported("part");
    // Body meshes and type-9 geometry resources are resolved below.
  }
  for (const auto &line : lines)
    if (line.key == "init" || line.key == "basetexture")
      return unsupported(line.key.c_str());

  if (node.objectRuntime || node.loaded())
    return std::string{"object loader: resource already loaded"};

  auto physics = ObjectPhysics::create();
  bool hasBodies = false;
  const auto collisionLoader = [&](const std::string &path, ScxModel &model,
                                   std::string &detail) {
    std::string bytes;
    if (!readNativeDataFile(resources.packs(), path,
                            std::numeric_limits<int32_t>::max(), bytes, detail))
      return false;
    auto parsed =
        scxParse(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
    if (!parsed.ok) {
      detail = parsed.error;
      return false;
    }
    model = std::move(parsed.model);
    return true;
  };
  for (size_t i = 0; i < config.slots.size(); ++i) {
    const auto &slot = config.slots[i];
    if (slot.kind != object_config::SlotKind::Body &&
        slot.kind != object_config::SlotKind::Trigger)
      continue;
    hasBodies = true;
    int body = -1;
    if (slot.shape.kind == object_config::ShapeKind::Mesh) {
      ScxModel model;
      std::vector<physics::Vec3> vertices;
      std::vector<physics::MeshTriangle> triangles;
      if (!collisionLoader(slot.shape.name, model, error) ||
          !decodeCarCollisionMesh(model, vertices, triangles, error))
        return std::string{"object body construction: "} + error;
      body =
          physics->addMeshBody(slot, i, vertices, std::move(triangles), error);
    } else if (slot.shape.kind == object_config::ShapeKind::ReuseRegistry) {
      const auto found = resources.lookup(slot.shape.meshId, 9);
      PackView geometryPack;
      if (found.status != LookupStatus::Found || !found.node.packNode) {
        error = found.detail.empty() ? "physics geometry resource unavailable"
                                     : found.detail;
        return std::string{"object body construction: "} + error;
      }
      const auto &geometryNode = *found.node.packNode;
      if (resources.packs().view(geometryNode.payloadSlot, geometryPack,
                                 error) != PackViewStatus::Ok ||
          !geometryPack.bytes)
        return std::string{"object body construction: "} + error;
      const auto &bytes = *geometryPack.bytes;
      const size_t offset = geometryNode.payloadOffset;
      const size_t size = geometryNode.payloadSize;
      if (offset > bytes.size() || size > bytes.size() - offset)
        return std::string{"object body construction: invalid physics geometry "
                           "resource bounds"};
      CarGeometryPrototype prototype;
      if (!loadCarGeometryResource(
              std::string_view(
                  reinterpret_cast<const char *>(bytes.data() + offset), size),
              collisionLoader, prototype, error))
        return std::string{"object body construction: "} + error;
      auto clone = cloneGeometryResource(prototype, slot.shape.mass);
      body = physics->addResourceBody(slot, i, std::move(*clone.body), error);
    } else {
      body = physics->addBody(slot, i, nullptr, error);
    }
    if (body < 0)
      return std::string{"object body construction: "} + error;
  }
  auto runtime =
      std::make_shared<ObjectRuntime>(std::move(config), std::move(physics));
  for (const auto &material : runtime->config.materials)
    if (!bindObjectMaterial(resources, material, *runtime, error))
      return error;
  runtime->resources.reserve(runtime->config.slots.size());
  for (const auto &slot : runtime->config.slots) {
    auto links = std::make_unique<ObjectRuntime::SlotResources>();
    if (slot.kind == object_config::SlotKind::Render) {
      auto primary = slot.lightKeyword
                         ? LookupResult{}
                         : resources.lookup(slot.renderId, 14, true);
      if (primary.status != LookupStatus::Found)
        primary = resources.lookup(slot.renderId, 13, true);
      if (primary.status == LookupStatus::Found)
        setLinkOwner(links->primary, primary.node.owner);
      for (uint32_t id : slot.modifiers.rendertypes) {
        auto link = std::make_unique<OwnerLink>();
        const auto variant = resources.lookup(id, 14, true);
        if (variant.status == LookupStatus::Found)
          setLinkOwner(*link, variant.node.owner);
        links->variants.push_back(std::move(link));
      }
    }
    runtime->resources.push_back(std::move(links));
  }
  if (!runtime->config.sfxRandom.empty()) {
    runtime->sound = std::make_unique<ObjectSoundRuntime>();
    auto &sound = *runtime->sound;
    sound.owner.value = node.value;
    sound.position = vec(runtime->config.instance.position);
    sound.entries.reserve(runtime->config.sfxRandom.size());
    for (const auto &directive : runtime->config.sfxRandom) {
      auto link = std::make_unique<ObjectRuntime::SlotResources>();
      const auto found = resources.lookup(directive.resourceId, 6, true);
      if (found.status == LookupStatus::StockCrash ||
          found.status == LookupStatus::MountRefused)
        return std::string{"object sound resource lookup: "} + found.detail;
      if (found.status == LookupStatus::Found)
        setLinkOwner(link->primary, found.node.owner);
      ObjectSoundRuntime::Entry entry;
      entry.resourceId = directive.resourceId;
      entry.clockMin = directive.param2;
      entry.clockMax = directive.param3;
      entry.baseDelay = directive.pitchMin;
      entry.delayVariance = directive.pitchSpan;
      entry.positional = link->primary.owner != nullptr;
      sound.entries.push_back(entry);
      runtime->soundResources.push_back(std::move(link));
    }
    sound.randomSound = sound.entries.front().positional;
    ObjectRuntime *const owner = runtime.get();
    ResourceWorld *const resourceWorld = &resources;
    sound.alive = [worldPtr = &world, handle = node.handle] {
      return worldPtr->containsResource(handle);
    };
    sound.sampleAt = [owner, resourceWorld](
                         size_t index) -> std::shared_ptr<const SoundSample> {
      if (index >= owner->soundResources.size())
        return {};
      const auto resolved =
          resourceWorld->resolve(owner->soundResources[index]->primary.owner);
      return resolved.packNode && resolved.packNode->loaded()
                 ? resolved.packNode->sound
                 : nullptr;
    };
    sound.requestSound = [owner, resourceWorld](size_t index,
                                                std::string &err) {
      if (index >= owner->soundResources.size()) {
        err = "object sound: sound index out of range";
        return false;
      }
      const auto resolved =
          resourceWorld->resolve(owner->soundResources[index]->primary.owner);
      if (!resolved) {
        err.clear();
        return true;
      }
      if (resolved.type != 6 || !resolved.packNode) {
        err = "object sound: sound resource unavailable";
        return false;
      }
      auto &packed = *resolved.packNode;
      auto &world = resourceWorld->world();
      if (!packed.payloadSize) {
        err = "object sound: empty sound resource unsupported";
        return false;
      }
      if (!packed.field6c && packed.stamp7c >= world.frameCounter()) {
        err = "object sound: sound demand unavailable";
        return false;
      }
      if (packed.stamp7c < world.frameCounter() ||
          (packed.field6c && *packed.field6c < 1.0f))
        packed.field6c = 1.0f;
      if (!packed.loaded())
        return resourceWorld->queueSoundPrecache(packed, err);
      if (!packed.sound || packed.loadPriority < 1.0f) {
        err = "object sound: loaded sound refresh unsupported";
        return false;
      }
      if (packed.stamp74 != world.frameCounter() && !packed.recentUseQueued)
        packed.stateWord = (packed.stateWord & ~uint32_t{0x60}) | 0x10;
      world.noteRecentUse(*resolved.owner, resolved.type, packed.stamp74,
                          packed.recentUseQueued);
      err.clear();
      return true;
    };
    sound.positionAt = [owner](std::array<float, 3> &position,
                               std::string &err) {
      if (!owner->root) {
        err.clear();
        return true;
      }
      if (owner->root->current > 1) {
        err = "object sound: invalid root pose";
        return false;
      }
      position = owner->root->state[owner->root->current].pos;
      err.clear();
      return true;
    };
  }
  if (hasBodies) {
    auto instance = runtime->physics->instantiate(
        vec(runtime->config.instance.position),
        vec(runtime->config.instance.orientation), error);
    if (!instance)
      return std::string{"object body construction: "} + error;
    runtime->root = instance->root.get();
    if (runtime->root->current > 1)
      return std::string{"object body construction: invalid root pose"};
    // 00465bd0 changes the negative sentinel to 1 for a static body when
    // the class init bit 0 is set. Positive and zero values are retained.
    if ((runtime->config.initValue & 1) && runtime->damageRemaining < 0.0f &&
        runtime->root->proto &&
        (runtime->root->proto->flags & ::physics::kProtoStatic))
      runtime->damageRemaining = 1.0f;
    runtime->initialRootPosition =
        runtime->root->state[runtime->root->current].pos;
    runtime->initialRootRotation =
        runtime->root->state[runtime->root->current].rot;
    if (!world.registerObjectPhysics(node, adoptInstance(std::move(*instance))))
      return unsupported("physics world registration");
  }
  if (!reconcile(resources, node, *runtime, node.field70.value_or(0.0f), error,
                 arena.gameState->gameTimeOfDay)) {
    runtime->root = nullptr;
    if (hasBodies)
      world.unregisterObjectPhysics(node);
    return std::string{"object render construction: "} + error;
  }
  if (runtime->sound && !arena.objectSound->arm(*runtime->sound, error)) {
    for (const auto &render : runtime->renders)
      if (auto *child = world.nodeFor(render->node.owner))
        world.destroy(*child);
    runtime->root = nullptr;
    if (hasBodies)
      world.unregisterObjectPhysics(node);
    return std::string{"object sound construction: "} + error;
  }
  node.stateWord = (node.stateWord & ~uint32_t{0x50}) | 0x21;
  node.loadPriority = 1.0f;
  world.countInstance(node.countedInstance);
  world.trackLoadState(linkOwner(node), node.type, true);
  node.objectRuntime = std::move(runtime);
  payload = GameResourcePayload{};
  return std::nullopt;
}

} // namespace object
