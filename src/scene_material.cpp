#include "resource_id.hpp"
#include "resource_material.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"

SceneMaterialResources::SceneMaterialResources(
    LinkOwner *mesh, const std::vector<LinkOwner *> &textures, uint32_t flags)
    : flags_(flags) {
  textures_.reserve(textures.size());
  for (size_t i = 0; i < textures.size(); ++i)
    textures_.push_back(std::make_unique<OwnerLink>());
  setLinkOwner(mesh_, mesh);
  for (size_t i = 0; i < textures.size(); ++i)
    setLinkOwner(*textures_[i], textures[i]);
}

std::unique_ptr<SceneMaterialResources> SceneMaterialResources::clone() const {
  std::vector<LinkOwner *> owners;
  owners.reserve(textures_.size());
  for (const auto &link : textures_)
    owners.push_back(link->owner);
  auto result =
      std::make_unique<SceneMaterialResources>(mesh_.owner, owners, flags_);
  result->defaults_ = defaults_;
  return result;
}

SceneMaterialResources::~SceneMaterialResources() {
  setLinkOwner(mesh_, nullptr);
  for (auto &link : textures_)
    setLinkOwner(*link, nullptr);
}

bool SceneMaterialResources::replace(uint32_t oldId, LinkOwner *replacement,
                                     const std::function<void()> &refresh) {
  OwnerLink *match = nullptr;
  if (mesh_.cachedValue == oldId)
    match = &mesh_;
  else {
    for (auto &texture : textures_) {
      if (texture->cachedValue == oldId) {
        match = texture.get();
        break;
      }
    }
  }
  if (!match)
    return false;
  setLinkOwner(*match, replacement);
  refresh();
  return true;
}

SceneMaterialLoadResult sceneLoadMaterial(ResourceWorld &world, uint32_t id) {
  SceneMaterialLoadResult result;
  const auto node = world.lookup(id, 14);
  if (node.status != LookupStatus::Found || !node.node.packNode) {
    result.error = "type-14 pack resource unavailable";
    return result;
  }
  PackResource payload;
  if (!world.packs().lookup(id, payload, result.error))
    return result;
  if (node.node.packNode->payloadSlot != static_cast<uint16_t>(id >> 16) ||
      node.node.packNode->payloadOffset != payload.entry.payloadOffset ||
      node.node.packNode->payloadSize != payload.entry.payloadSize) {
    result.error = "embedded material payload unsupported";
    return result;
  }
  const auto parsed = resourceMaterialParse(payload);
  if (!parsed.ok) {
    result.error = parsed.error;
    return result;
  }
  PackView view;
  if (world.packs().view(static_cast<uint16_t>(id >> 16), view, result.error) !=
      PackViewStatus::Ok)
    return result;
  const ResourceIdMapping mapping{
      view.slot, static_cast<uint32_t>(world.packs().slotCount()),
      view.dependencies};
  const uint32_t meshId = remapResourceId(parsed.mesh, mapping);
  if (!meshId) {
    result.error = "invalid material resource ID";
    return result;
  }
  const auto mesh = world.lookup(meshId, 5);
  if (mesh.status != LookupStatus::Found) {
    result.error = "material mesh unavailable";
    return result;
  }
  std::vector<LinkOwner *> textures;
  textures.reserve(parsed.textures.size());
  for (uint32_t rawId : parsed.textures) {
    if (!rawId) {
      textures.push_back(nullptr);
      continue;
    }
    const uint32_t textureId = remapResourceId(rawId, mapping);
    if (!textureId) {
      result.error = "invalid material resource ID";
      return result;
    }
    const auto texture = world.lookup(textureId, 7);
    if (texture.status != LookupStatus::Found) {
      result.error = "material texture unavailable";
      return result;
    }
    textures.push_back(texture.node.owner);
  }
  result.material = std::make_unique<SceneMaterialResources>(
      mesh.node.owner, textures, parsed.flags);
  result.material->setLod(parsed.lodBias, parsed.lodAmplitude);
  result.flags = parsed.flags;
  result.error.clear();
  return result;
}

WorldNode *sceneCloneMaterial(ResourceWorld &world, const ResolvedNode &source,
                              std::string &error) {
  const auto material = world.sceneMaterial(source);
  if (!material || source.type != 14) {
    error = "source material is not bound";
    return nullptr;
  }
  ResourceTreeHandle handle{};
  uint32_t flags = 0;
  float priority = 0;
  if (source.packNode) {
    handle = source.packNode->handle;
    flags = source.packNode->flags;
    priority = source.packNode->loadPriority;
  } else if (source.worldNode) {
    handle = source.worldNode->handle;
    flags = source.worldNode->flags;
    priority = source.worldNode->loadPriority;
  } else {
    error = "source node unavailable";
    return nullptr;
  }
  auto clone = std::shared_ptr<SceneMaterialResources>(material->clone());
  LinkOwner *parentOwner = world.world().parentOwner(handle);
  const auto parent = world.resolve(parentOwner);
  WorldNode *node = nullptr;
  if (parent.worldNode)
    node =
        world.world().addTypedChild(*parent.worldNode, 14, "", flags | 0x200);
  else if (parent.packNode)
    node = world.world().addTypedChildUnderExternal(
        parent.packNode->handle, *parent.owner, parent.type,
        parent.packNode->sectorLink.owner, 14, "", flags | 0x200);
  else if (world.world().defaultParent())
    node = world.world().addTypedChild(*world.world().defaultParent(), 14, "",
                                       flags | 0x200);
  if (!node) {
    error = "material clone parent or allocation unavailable";
    return nullptr;
  }
  ResolvedNode target{node, 14, node->instanceId, nullptr, node, nullptr};
  if (!world.bindSceneMaterial(target, std::move(clone))) {
    world.world().destroy(*node);
    error = "material clone binding failed";
    return nullptr;
  }
  world.world().markLoaded(*node);
  node->loadPriority = priority;
  error.clear();
  return node;
}

SceneRenderInstance::SceneRenderInstance(LinkOwner *type) {
  setLinkOwner(type_, type);
}
SceneRenderInstance::~SceneRenderInstance() {
  detachPhysicsBone();
  setLinkOwner(type_, nullptr);
}
bool SceneRenderInstance::attachPhysicsBone(GameWorld &world, WorldNode &node) {
  auto body = world.dynamicBody(node);
  if (node.type != 2 || body.expired())
    return false;
  detachPhysicsBone();
  prepareUnattachedGroundBone();
  setLinkOwner(physicsNode_, &node);
  ++node.physicsBoneAttachments;
  physicsBody_ = std::move(body);
  physicsBoneAttached_ = true;
  return true;
}
void SceneRenderInstance::detachPhysicsBone() {
  if (physicsNode_.owner) {
    auto *node = static_cast<WorldNode *>(physicsNode_.owner);
    if (node->physicsBoneAttachments)
      --node->physicsBoneAttachments;
  }
  setLinkOwner(physicsNode_, nullptr);
  physicsBody_.reset();
  physicsBoneAttached_ = false;
}
bool SceneRenderInstance::refreshPhysicsBone() {
  if (!physicsNode_.owner)
    return false;
  const auto body = physicsBody_.lock();
  if (!body || body->current > 1)
    return false;
  const auto &state = body->state[body->current];
  setBoneTransform(state.pos, state.rot);
  return true;
}
void SceneRenderInstance::prepareUnattachedGroundBone() {
  if (!groundBone_)
    groundBone_ = SceneGroundBone{"bone00", 0x8000};
}
void SceneRenderInstance::setBoneTranslation(float x, float y, float z) {
  const float position[3]{x, y, z};
  const float identity[3]{};
  setBoneMatrix(position, identity);
}
void SceneRenderInstance::setBoneTransform(const std::array<float, 3> &position,
                                           const std::array<float, 12> &m) {
  boneMatrix_ = {{m[0], m[4], m[8], 0, m[1], m[5], m[9], 0, m[2], m[6], m[10],
                  0, position[0] * 10.0f, position[1] * 10.0f,
                  position[2] * 10.0f, 1}};
  rootBoneUpdatePending_ = true;
}
void SceneRenderInstance::setBoneMatrix(const float position[3],
                                        const float ypr[3]) {
  // 0054ecd0 keeps sin(yaw) and sin(pitch) unrounded and stores the other
  // sines and cosines as floats; 0054f4c0 transposes the basis into the 4x4
  // and scales the position by 10 (DAT_005f37a0).
  const double s0 = std::sin(ypr[0]), s1 = std::sin(ypr[1]);
  const float c0 = static_cast<float>(std::cos(ypr[0])),
              c1 = static_cast<float>(std::cos(ypr[1]));
  const float s2 = static_cast<float>(std::sin(ypr[2])),
              c2 = static_cast<float>(std::cos(ypr[2]));
  const float t1 = static_cast<float>(static_cast<double>(s2) * s1);
  const float t2 = static_cast<float>(static_cast<double>(c2) * s1);
  const float m0 = static_cast<float>((static_cast<double>(s2) * s1) * s0 +
                                      static_cast<double>(c2) * c0);
  const float m1 = static_cast<float>((static_cast<double>(c2) * s1) * s0 -
                                      static_cast<double>(s2) * c0);
  const float m2 = static_cast<float>(static_cast<double>(c1) * s0);
  const float m4 = static_cast<float>(static_cast<double>(s2) * c1);
  const float m5 = static_cast<float>(static_cast<double>(c2) * c1);
  const float m6 = static_cast<float>(-s1);
  const float m8 = static_cast<float>(static_cast<double>(t1) * c0 -
                                      static_cast<double>(c2) * s0);
  const float m9 = static_cast<float>(static_cast<double>(t2) * c0 +
                                      static_cast<double>(s2) * s0);
  const float m10 = static_cast<float>(static_cast<double>(c1) * c0);
  boneMatrix_ = {{m0, m4, m8, 0, m1, m5, m9, 0, m2, m6, m10, 0,
                  position[0] * 10.0f, position[1] * 10.0f, position[2] * 10.0f,
                  1}};
  rootBoneUpdatePending_ = true;
}
void SceneRenderInstance::processRootBoneUpdate() {
  if (!rootBoneUpdatePending_)
    return;
  derivedRootBoneMatrix_ = boneMatrix_;
  rootBoneUpdatePending_ = false;
}
void SceneRenderInstance::refreshSingleBoneDrawMatrix() {
  drawMatrix_ = derivedRootBoneMatrix_;
}
