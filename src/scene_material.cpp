#include "resource_id.hpp"
#include "resource_material.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include "skeleton.hpp"
#include "car_runtime.hpp"

SceneMaterialResources::SceneMaterialResources(
    LinkOwner* mesh, const std::vector<LinkOwner*>& textures, uint32_t flags)
    : flags_(flags) {
  textures_.reserve(textures.size());
  for (size_t i = 0; i < textures.size(); ++i)
    textures_.push_back(std::make_unique<OwnerLink>());
  setLinkOwner(mesh_, mesh);
  for (size_t i = 0; i < textures.size(); ++i)
    setLinkOwner(*textures_[i], textures[i]);
}

std::unique_ptr<SceneMaterialResources> SceneMaterialResources::clone() const {
  std::vector<LinkOwner*> owners;
  owners.reserve(textures_.size());
  for (const auto& link : textures_)
    owners.push_back(link->owner);
  auto result =
      std::make_unique<SceneMaterialResources>(mesh_.owner, owners, flags_);
  result->defaults_ = defaults_;
  result->skeletonBones_ = skeletonBones_;
  result->skeletonPending_ = skeletonPending_;
  return result;
}

SceneMaterialResources::~SceneMaterialResources() {
  setLinkOwner(mesh_, nullptr);
  for (auto& link : textures_)
    setLinkOwner(*link, nullptr);
}

bool SceneMaterialResources::replace(uint32_t oldId, LinkOwner* replacement,
                                     const std::function<void()>& refresh) {
  OwnerLink* match = nullptr;
  if (mesh_.cachedValue == oldId)
    match = &mesh_;
  else {
    for (auto& texture : textures_) {
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

// 00541400's "skeleton <path>" handling: open the skl under the pack root
// (same source-path safety rules as mesh sourcefile, FUN_005411a0 scan) and
// parse it with the stock text parser FUN_00531e90.
static std::vector<SkeletonBone> sceneLoadSkeletonFile(
    ResourceWorld& world, const std::string& source, std::string& error) {
  if (source.empty()) {
    error = "missing material skeleton";
    return {};
  }
  std::string path = source;
  std::replace(path.begin(), path.end(), '\\', '/');
  if (path.front() == '/' || path.find(':') != std::string::npos) {
    error = "unsafe material skeleton";
    return {};
  }
  const std::filesystem::path relative(path);
  for (const auto& part : relative)
    if (part == "." || part == ".." || part.empty()) {
      error = "unsafe material skeleton";
      return {};
    }
  std::error_code ec;
  const auto base = std::filesystem::canonical(world.packs().root(), ec);
  if (ec) {
    error = "invalid resource root";
    return {};
  }
  const auto target = std::filesystem::canonical(base / relative, ec);
  if (ec) {
    error = "material skeleton file not found: " + source;
    return {};
  }
  auto parent = base.begin(), child = target.begin();
  for (; parent != base.end() && child != target.end(); ++parent, ++child)
    if (*parent != *child) {
      error = "material skeleton escapes resource root";
      return {};
    }
  if (parent != base.end() || !std::filesystem::is_regular_file(target, ec) ||
      ec) {
    error = "unsafe material skeleton";
    return {};
  }
  const auto fileSize = std::filesystem::file_size(target, ec);
  if (ec || fileSize > 256u * 1024u) {
    error = "material skeleton file unavailable or too large";
    return {};
  }
  std::ifstream input(target, std::ios::binary);
  if (!input) {
    error = "material skeleton file unavailable";
    return {};
  }
  std::string text(fileSize, '\0');
  input.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (!input && !input.eof()) {
    error = "material skeleton file unavailable";
    return {};
  }
  text.resize(static_cast<size_t>(input.gcount()));
  const auto parsed = parseSkeleton(text);
  if (!parsed.ok) {
    error = "invalid material skeleton: " + parsed.error;
    return {};
  }
  return std::move(parsed.bones);
}
SceneMaterialLoadResult sceneLoadMaterial(ResourceWorld& world, uint32_t id) {
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
  std::vector<LinkOwner*> textures;
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
  if (parsed.bones)
    result.material->setBones(parsed.bones);
  if (!parsed.skeleton.empty()) {
    // 00541400: a declared skeleton that fails to load leaves material +0x38
    // empty; the instance stays skinned-static until sceneEnsureSkeleton
    // reports the explicit error.
    auto bones = sceneLoadSkeletonFile(world, parsed.skeleton, result.error);
    if (bones.empty()) {
      result.material->setSkeletonPending();
      result.error.clear();
    } else
      result.material->setSkeletonBones(std::move(bones));
  }
  result.flags = parsed.flags;
  result.error.clear();
  return result;
}

bool sceneEnsureSkeleton(ResourceWorld& world, SceneRenderInstance& instance,
                         std::string& error) {
  if (instance.skeletonPose()) {
    error.clear();
    return true;
  }
  const auto type = world.lookup(instance.typeId(), 14);
  const auto material = type.status == LookupStatus::Found
                            ? world.sceneMaterial(type.node)
                            : nullptr;
  if (!material) {
    // Unbound materials render through their own path; no skeleton to build.
    error.clear();
    return true;
  }
  if (material->skeletonPending()) {
    error = "material skeleton unavailable";
    return false;
  }
  if (!material->skeletonBoneCount()) {
    // FUN_00541400: materials without a skeleton use the single-bone path.
    error.clear();
    return true;
  }
  instance.setSkeletonPose(
      std::make_shared<SkeletonPose>(material->skeletonBones()));
  error.clear();
  return true;
}
WorldNode* sceneCloneMaterial(ResourceWorld& world, const ResolvedNode& source,
                              std::string& error) {
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
  LinkOwner* parentOwner = world.world().parentOwner(handle);
  const auto parent = world.resolve(parentOwner);
  WorldNode* node = nullptr;
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

SceneRenderInstance::SceneRenderInstance(LinkOwner* type) {
  setLinkOwner(type_, type);
}
SceneRenderInstance::~SceneRenderInstance() {
  setLinkOwner(driverCar_, nullptr);
  detachPhysicsBone();
  setLinkOwner(type_, nullptr);
}
bool SceneRenderInstance::attachPhysicsBone(GameWorld& world, WorldNode& node) {
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
    auto* node = static_cast<WorldNode*>(physicsNode_.owner);
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
  const auto& state = body->state[body->current];
  if (driverCar_.owner && skeletonPose()) {
    const auto* car = static_cast<const WorldNode*>(driverCar_.owner);
    // [INFERENCE] Runtime normalized steering times wheel lock supplies the
    // wheel-parent angle; stock target-frame attachment is recovered, but its
    // complete steering-device numerical transfer is not yet matched.
    if (car->carInstance)
      skeletonPose()->updateSteering(car->carInstance->steeringState *
                                     car->carInstance->steerWheelLock);
  }
  setBoneTransform(state.pos, state.rot);
  // FUN_00448f20 seat frame remains relative to the car's physics root.
  const auto root = boneMatrix_;
  boneMatrix_.fill(0);
  for (size_t c = 0; c < 4; ++c)
    for (size_t r = 0; r < 4; ++r)
      for (size_t k = 0; k < 4; ++k)
        boneMatrix_[c * 4 + r] += root[k * 4 + r] * attachmentOffset_[c * 4 + k];
  return true;
}
namespace {
// FUN_0054ecd0: 3x4 basis from (yaw, pitch, roll) in the stock layout
// ([0..2], [4..6], [8..10]; [3], [7], [11] are the unwritten padding the
// stock function leaves behind). Shared by setBoneMatrix and the
// Navigator.updateNavigator mode-1 camera bone.
void eulerBasis(const float ypr[3], std::array<float, 12>& out) {
  const double s0 = std::sin(ypr[0]), s1 = std::sin(ypr[1]);
  const float c0 = static_cast<float>(std::cos(ypr[0])),
              c1 = static_cast<float>(std::cos(ypr[1]));
  const float s2 = static_cast<float>(std::sin(ypr[2])),
              c2 = static_cast<float>(std::cos(ypr[2]));
  const float t1 = static_cast<float>(static_cast<double>(s2) * s1);
  const float t2 = static_cast<float>(static_cast<double>(c2) * s1);
  out[0] = static_cast<float>((static_cast<double>(s2) * s1) * s0 +
                              static_cast<double>(c2) * c0);
  out[1] = static_cast<float>((static_cast<double>(c2) * s1) * s0 -
                              static_cast<double>(s2) * c0);
  out[2] = static_cast<float>(static_cast<double>(c1) * s0);
  out[4] = static_cast<float>(static_cast<double>(s2) * c1);
  out[5] = static_cast<float>(static_cast<double>(c2) * c1);
  out[6] = static_cast<float>(-s1);
  out[8] = static_cast<float>(static_cast<double>(t1) * c0 -
                              static_cast<double>(c2) * s0);
  out[9] = static_cast<float>(static_cast<double>(t2) * c0 +
                              static_cast<double>(s2) * s0);
  out[10] = static_cast<float>(static_cast<double>(c1) * c0);
}
} // namespace

void SceneRenderInstance::prepareUnattachedGroundBone() {
  if (!groundBone_)
    groundBone_ = SceneGroundBone{"bone00", 0x8000};
}
void SceneRenderInstance::setBoneTranslation(float x, float y, float z) {
  const float position[3]{x, y, z};
  const float identity[3]{};
  setBoneMatrix(position, identity);
}
void SceneRenderInstance::setBoneTransform(const std::array<float, 3>& position,
                                           const std::array<float, 12>& m) {
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
  std::array<float, 12> basis;
  eulerBasis(ypr, basis);
  boneMatrix_ = {{basis[0], basis[4], basis[8], 0, basis[1], basis[5],
                  basis[9], 0, basis[2], basis[6], basis[10], 0,
                  position[0] * 10.0f, position[1] * 10.0f,
                  position[2] * 10.0f, 1}};
  rootBoneUpdatePending_ = true;
}
void SceneRenderInstance::setBoneTranslationKeepRotation(float x, float y,
                                                         float z) {
  // FUN_00482d30 mode 0 patches bone[12] and bone[14] of the current camera
  // bone and leaves every other element, including the pending flag path of
  // FUN_0048c270 (+0xf0 = 1), in place.
  boneMatrix_[12] = x * 0.1f;
  boneMatrix_[13] = y;
  boneMatrix_[14] = z * 0.1f;
  rootBoneUpdatePending_ = true;
}
void SceneRenderInstance::setNavigatorCameraBone(float targetX, float targetZ,
                                                 float yaw) {
  // FUN_00482d30 mode 1: euler (yaw, -1.5, 0) basis, then the translation is
  // the target scaled by DAT_005f0c20 = 0.01 and offset by the basis
  // elements the stock function reads back: [2] * DAT_005f13c0 = 4.5 for x,
  // [6] * 4.5 for y and [10] * 4.5 for z. FUN_0054f4c0 scales the position by
  // 10 into the bone matrix.
  const float ypr[3]{yaw, -1.5f, 0.0f};
  std::array<float, 12> basis;
  eulerBasis(ypr, basis);
  const std::array<float, 3> position{
      targetX * 0.01f + basis[2] * 4.5f, basis[6] * 4.5f,
      targetZ * 0.01f + basis[10] * 4.5f};
  setBoneTransform(position, basis);
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
