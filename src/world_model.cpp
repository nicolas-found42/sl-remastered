#include "world_model.hpp"
#include "camera_device_factory.hpp"
#include "camera_follow_force.hpp"
#include <algorithm>
#include <cmath>

namespace {
uint32_t &globalFrameCounter() {
  static uint32_t value = 1;
  return value;
}
}

uint32_t GameWorld::frameCounter() const { return globalFrameCounter(); }
void GameWorld::setFrameCounter(uint32_t frame) { globalFrameCounter() = frame; }

bool GameWorld::changeCursorVisibility(bool show, std::string &error) {
  const int64_t next = int64_t(cursorDisplayCount_) + (show ? 1 : -1);
  if (next < INT32_MIN || next > INT32_MAX) {
    error = "cursor display count overflow";
    return false;
  }
  if (!cursorSelector_ || !cursorSelector_(0, next >= 0, error)) {
    if (!cursorSelector_) error = "cursor host unavailable";
    return false;
  }
  cursorDisplayCount_ = static_cast<int32_t>(next);
  error.clear();
  return true;
}

CameraResource makeCameraResource(float aspect, float fieldOfView,
                                  float nearDistance, float farDistance,
                                  float lodBias, float lodAmplitude,
                                  int32_t culling, int32_t planeTest) {
  return {aspect, fieldOfView, nearDistance * 10.0f, farDistance * 10.0f,
          lodBias, lodAmplitude, culling, planeTest, {}};
}

LinkOwner *cameraTraversalRoot(WorldNode &cameraRenderInstance) {
  if (cameraRenderInstance.type != 3)
    return nullptr;
  return cameraRenderInstance.sectorLink.owner;
}

CameraRendererFields makeCameraRendererFields(const CameraResource &camera) {
  // 005f2db4 is the stock single-precision degrees-to-half-radians constant.
  constexpr float halfRadians = 0x1.1df242p-7f;
  const double angle = static_cast<double>(camera.fieldOfView) * halfRadians;
  const float farMetric = static_cast<float>(
      std::sqrt(std::cos(angle) / std::sin(angle)) * camera.farDistance);
  return {camera.viewportAspect, camera.fieldOfView, camera.nearDistance,
          farMetric, camera.lodBias, camera.lodAmplitude, camera.planeTest};
}

CameraRendererFields updateCameraRendererFields(const CameraResource &camera,
                                                float fallbackAspect) {
  constexpr float halfRadians = 0x1.1df242p-7f;
  const double angle = static_cast<double>(camera.fieldOfView) * halfRadians;
  CameraRendererFields fields = makeCameraRendererFields(camera);
  if (camera.viewportAspect == 0.0f)
    fields.viewportAspect = fallbackAspect;
  fields.farMetric = static_cast<float>(
      std::sqrt(std::sqrt(std::cos(angle) / std::sin(angle))) *
      camera.farDistance);
  return fields;
}

std::array<float, 16> cameraProjectionMatrix(const CameraRendererFields &fields) {
  constexpr float degreesToRadians = 0x1.1df46ap-6f;
  const float radians = fields.fieldOfView * degreesToRadians;
  const float halfAngle = radians * 0.5f;
  const float cotangent = static_cast<float>(std::cos(halfAngle)) /
                          static_cast<float>(std::sin(halfAngle));
  const float depth = fields.farMetric / (fields.nearDistance - fields.farMetric);
  std::array<float, 16> matrix{};
  matrix[0] = cotangent / fields.viewportAspect;
  matrix[5] = cotangent;
  matrix[10] = depth;
  matrix[11] = -1.0f;
  matrix[14] = depth * fields.nearDistance;
  return matrix;
}

std::optional<std::array<float, 16>>
cameraViewMatrix(const std::array<float, 16> &worldMatrix) {
  double augmented[4][8]{};
  for (size_t row = 0; row < 4; ++row) {
    for (size_t col = 0; col < 4; ++col) {
      const float value = worldMatrix[row * 4 + col];
      if (!std::isfinite(value)) return std::nullopt;
      augmented[row][col] = value;
    }
    augmented[row][row + 4] = 1.0;
  }
  for (size_t col = 0; col < 4; ++col) {
    size_t pivot = col;
    for (size_t row = col + 1; row < 4; ++row)
      if (std::abs(augmented[row][col]) > std::abs(augmented[pivot][col])) pivot = row;
    if (augmented[pivot][col] == 0.0) return std::nullopt;
    for (size_t i = 0; i < 8; ++i) std::swap(augmented[col][i], augmented[pivot][i]);
    const double divisor = augmented[col][col];
    for (double &value : augmented[col]) value /= divisor;
    for (size_t row = 0; row < 4; ++row) {
      if (row == col) continue;
      const double factor = augmented[row][col];
      for (size_t i = 0; i < 8; ++i) augmented[row][i] -= factor * augmented[col][i];
    }
  }
  std::array<float, 16> view{};
  for (size_t row = 0; row < 4; ++row)
    for (size_t col = 0; col < 4; ++col) {
      view[row * 4 + col] = static_cast<float>(augmented[row][col + 4]);
      if (!std::isfinite(view[row * 4 + col])) return std::nullopt;
    }
  return view;
}

std::optional<CameraViewState>
cameraViewState(const CameraResource &camera,
                const std::array<float, 16> &boneWorldMatrix,
                float viewportAspect) {
  const auto view = cameraViewMatrix(boneWorldMatrix);
  if (!view) return std::nullopt;
  const auto fields = updateCameraRendererFields(camera, viewportAspect);
  const auto projection = cameraProjectionMatrix(fields);
  for (const float value : projection)
    if (!std::isfinite(value)) return std::nullopt;
  return CameraViewState{fields, *view, projection};
}

std::optional<std::array<std::array<float, 4>, 5>>
cameraFrustumPlanes(const CameraRendererFields &fields,
                    const std::array<float, 16> &worldMatrix) {
  for (float value : worldMatrix) if (!std::isfinite(value)) return std::nullopt;
  if (!std::isfinite(fields.fieldOfView) || !std::isfinite(fields.viewportAspect) ||
      !std::isfinite(fields.farMetric)) return std::nullopt;
  const double angle = static_cast<double>(fields.fieldOfView) * 0x1.1df242p-7f;
  const double tangent = std::sin(angle) / std::cos(angle);
  const double horizontal = tangent * fields.viewportAspect;
  const double hdenom = std::sqrt(horizontal * horizontal + 1.0);
  const double vdenom = std::sqrt(tangent * tangent + 1.0);
  const double hz = horizontal / hdenom, hx = 1.0 / hdenom;
  const double vz = tangent / vdenom, vy = 1.0 / vdenom;
  std::array<std::array<float, 4>, 5> planes{};
  for (size_t i = 0; i < 3; ++i) {
    planes[0][i] = -worldMatrix[8 + i];
    planes[1][i] = static_cast<float>(hz * worldMatrix[8 + i] + hx * worldMatrix[i]);
    planes[2][i] = static_cast<float>(hz * worldMatrix[8 + i] - hx * worldMatrix[i]);
    planes[3][i] = static_cast<float>(vz * worldMatrix[8 + i] + vy * worldMatrix[4 + i]);
    planes[4][i] = static_cast<float>(vz * worldMatrix[8 + i] - vy * worldMatrix[4 + i]);
  }
  for (size_t i = 0; i < planes.size(); ++i) {
    const auto &p = planes[i];
    const float dot = p[0] * worldMatrix[12] + p[1] * worldMatrix[13] + p[2] * worldMatrix[14];
    planes[i][3] = (dot + (i == 0 ? fields.farMetric : 0.0f)) * 0.1f + 2.0f;
    for (float value : planes[i]) if (!std::isfinite(value)) return std::nullopt;
  }
  return planes;
}

std::optional<std::array<float, 3>>
projectCameraPoint(const std::array<float, 3> &point,
                   const std::array<float, 16> &view,
                   const std::array<float, 16> &projection) {
  const auto transform = [](const std::array<float, 4> &v,
                            const std::array<float, 16> &m) {
    std::array<float, 4> out{};
    for (size_t column = 0; column < 4; ++column)
      for (size_t row = 0; row < 4; ++row)
        out[column] += v[row] * m[row * 4 + column];
    return out;
  };
  const auto cameraPoint = transform({point[0], point[1], point[2], 1.0f}, view);
  const auto clip = transform(cameraPoint, projection);
  if (!std::isfinite(clip[3]) || clip[3] <= 0.0f)
    return std::nullopt;
  std::array<float, 3> result{};
  for (size_t i = 0; i < 3; ++i) {
    result[i] = clip[i] / clip[3];
    if (!std::isfinite(result[i]))
      return std::nullopt;
  }
  return result;
}

std::optional<std::array<float, 3>>
unprojectCameraPoint(const std::array<float, 3> &point,
                     const std::array<float, 16> &world,
                     const std::array<float, 16> &projection) {
  const auto inverse = cameraViewMatrix(projection);
  if (!inverse) return std::nullopt;
  for (float value : point) if (!std::isfinite(value)) return std::nullopt;
  for (float value : world) if (!std::isfinite(value)) return std::nullopt;
  std::array<float, 4> camera{};
  const std::array<float, 4> input{point[0], point[1], point[2], 1.0f};
  for (size_t column = 0; column < 4; ++column)
    for (size_t row = 0; row < 4; ++row) camera[column] += input[row] * (*inverse)[row * 4 + column];
  if (!std::isfinite(camera[3]) || camera[3] == 0) return std::nullopt;
  std::array<float, 3> result{};
  for (size_t column = 0; column < 3; ++column) {
    result[column] = world[12 + column];
    for (size_t row = 0; row < 3; ++row) result[column] += camera[row] / camera[3] * world[row * 4 + column];
    if (!std::isfinite(result[column])) return std::nullopt;
  }
  return result;
}

std::optional<std::array<float, 2>>
cameraViewportPoint(const std::array<float, 3> &point,
                    const ViewportInstance &viewport, int32_t outputWidth,
                    int32_t outputHeight) {
  if (outputWidth <= 0 || outputHeight <= 0 ||
      !std::isfinite(point[0]) || !std::isfinite(point[1]) ||
      !std::isfinite(point[2]) || point[0] < -1.0f || point[0] > 1.0f ||
      point[1] < -1.0f || point[1] > 1.0f || point[2] < 0.0f || point[2] > 1.0f)
    return std::nullopt;
  const float x = (viewport.left + (point[0] + 1.0f) * 0.5f * viewport.width) * outputWidth;
  const float y = (viewport.top + (1.0f - point[1]) * 0.5f * viewport.height) * outputHeight;
  if (!std::isfinite(x) || !std::isfinite(y))
    return std::nullopt;
  return std::array<float, 2>{x, y};
}

std::optional<std::vector<CameraClipVertex>>
clipCameraTriangle(const std::array<CameraClipVertex, 3> &triangle) {
  for (const auto &vertex : triangle) {
    for (float component : vertex.position)
      if (!std::isfinite(component)) return std::nullopt;
    for (float component : vertex.uv)
      if (!std::isfinite(component)) return std::nullopt;
  }
  std::vector<CameraClipVertex> polygon(triangle.begin(), triangle.end());
  const auto distance = [](const CameraClipVertex &v, int plane) {
    const auto &p = v.position;
    switch (plane) {
    case 0: return static_cast<double>(p[3]) + p[0];
    case 1: return static_cast<double>(p[3]) - p[0];
    case 2: return static_cast<double>(p[3]) + p[1];
    case 3: return static_cast<double>(p[3]) - p[1];
    case 4: return static_cast<double>(p[2]);
    default: return static_cast<double>(p[3]) - p[2];
    }
  };
  for (int plane = 0; plane < 6 && !polygon.empty(); ++plane) {
    std::vector<CameraClipVertex> next;
    auto previous = polygon.back();
    double previousDistance = distance(previous, plane);
    for (const auto &current : polygon) {
      const double currentDistance = distance(current, plane);
      const bool previousInside = previousDistance >= 0.0f;
      const bool currentInside = currentDistance >= 0.0f;
      if (previousInside != currentInside) {
        const double t = previousDistance / (previousDistance - currentDistance);
        CameraClipVertex intersection{};
        for (size_t i = 0; i < 4; ++i)
          intersection.position[i] = static_cast<float>(previous.position[i] +
              t * (static_cast<double>(current.position[i]) - previous.position[i]));
        for (size_t i = 0; i < 2; ++i)
          intersection.uv[i] = static_cast<float>(previous.uv[i] +
              t * (static_cast<double>(current.uv[i]) - previous.uv[i]));
        next.push_back(intersection);
      }
      if (currentInside) next.push_back(current);
      previous = current;
      previousDistance = currentDistance;
    }
    polygon = std::move(next);
  }
  return polygon;
}

ViewportRenderAttachments::~ViewportRenderAttachments() {
  for (auto &entry : pending_)
    setLinkOwner(entry->instance, nullptr);
  for (auto &entry : active_)
    setLinkOwner(entry->instance, nullptr);
}

void ViewportRenderAttachments::prepare(LinkOwner &instance, std::optional<uint32_t> priority) {
  auto matches = [&instance](const auto &entry) { return entry->instance.owner == &instance; };
  if (std::any_of(pending_.begin(), pending_.end(), matches) ||
      std::any_of(active_.begin(), active_.end(), matches))
    return;
  auto entry = std::make_unique<Entry>();
  setLinkOwner(entry->instance, &instance);
  entry->priority = priority;
  pending_.push_back(std::move(entry));
}

bool ViewportRenderAttachments::activate(LinkOwner &instance, int32_t priority) {
  active_.erase(std::remove_if(active_.begin(), active_.end(), [](const auto &entry) {
    return !entry->instance.owner;
  }), active_.end());
  auto matches = [&instance](const auto &entry) { return entry->instance.owner == &instance; };
  auto it = std::find_if(pending_.begin(), pending_.end(), matches);
  if (it == pending_.end())
    return std::any_of(active_.begin(), active_.end(), matches);
  const std::optional<uint32_t> nextPriority = priority == -1
      ? (*it)->priority : std::optional<uint32_t>(static_cast<uint32_t>(priority));
  if (!active_.empty() && (!nextPriority || std::any_of(active_.begin(), active_.end(),
      [](const auto &other) { return !other->priority; }))) return false;
  auto entry = std::move(*it);
  pending_.erase(it);
  entry->priority = nextPriority;
  auto position = std::find_if(active_.begin(), active_.end(), [&](const auto &other) {
    return *other->priority >= *entry->priority;
  });
  active_.insert(position, std::move(entry));
  return true;
}

std::vector<LinkOwner *> ViewportRenderAttachments::active() const {
  std::vector<LinkOwner *> result;
  for (const auto &entry : active_)
    if (entry->instance.owner)
      result.push_back(entry->instance.owner);
  return result;
}

void ViewportRenderAttachments::remove(LinkOwner &instance) {
  const auto removeFrom = [&](auto &entries) {
    for (auto &entry : entries)
      if (entry->instance.owner == &instance) setLinkOwner(entry->instance, nullptr);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [](const auto &entry) {
      return !entry->instance.owner;
    }), entries.end());
  };
  removeFrom(pending_);
  removeFrom(active_);
}

void GameWorld::registerViewport(WorldNode &node, int32_t initialPriority) {
  viewportInventory_.prepare(node, initialPriority == -1 ? std::nullopt
      : std::optional<uint32_t>(static_cast<uint32_t>(initialPriority)));
}

bool GameWorld::activateViewport(WorldNode &node) {
  return viewportInventory_.activate(node, -1);
}

void GameWorld::deactivateViewport(WorldNode &node) { viewportInventory_.remove(node); }

std::vector<WorldNode *> GameWorld::activeViewports() {
  std::vector<WorldNode *> result;
  for (auto *owner : viewportInventory_.active()) {
    auto *node = nodeFor(owner);
    if (node && node->type == 18 && node->viewport && node->viewport->list == ViewportList::Active)
      result.push_back(node);
  }
  return result;
}

std::optional<int32_t> PlayerInputState::add(
    int32_t axis, int32_t device, int32_t physicalAxis, float sample,
    const std::array<float, 4> &range, int32_t deviceType) {
  if (device < 0 || physicalAxis < 0)
    return -1;
  if (axis < 0 || static_cast<size_t>(axis) >= axes_.size())
    return std::nullopt;
  for (size_t i = 0; i < mappings_.size(); ++i) {
    auto &mapping = mappings_[i];
    if (mapping.axis != 0)
      continue;
    mapping = {axis, device, physicalAxis, sample, range};
    axes_[static_cast<size_t>(axis)].deviceType = deviceType;
    ++mappingCount_;
    return static_cast<int32_t>(i + 1);
  }
  return 0;
}

void PlayerInputState::remove(int32_t axis, int32_t device,
                              int32_t physicalAxis) {
  for (auto &mapping : mappings_) {
    if (mapping.axis == axis && mapping.device == device &&
        mapping.physicalAxis == physicalAxis) {
      mapping.axis = 0;
      --mappingCount_;
      return;
    }
  }
}

void PlayerInputState::reset() {
  for (auto &mapping : mappings_)
    mapping.axis = 0;
  axes_.fill(PlayerAxis{});
  mappingCount_ = 0;
}

bool PlayerInputState::setSmooth(int32_t axis,
                                 const std::array<float, 5> &values) {
  if (axis < 0 || static_cast<size_t>(axis) >= axes_.size())
    return false;
  auto &target = axes_[static_cast<size_t>(axis)];
  target.threshold = values[0];
  target.factors[0] = values[1];
  target.factors[1] = values[2];
  target.factors[2] = values[3];
  target.factors[4] = values[4];
  return true;
}

void PlayerInputState::beginFrame(float deltaSeconds) {
  deltaMilliseconds_ = deltaSeconds * 1000.0f;
  ++inputFrame_;
}

std::optional<float> PlayerInputState::sampleAxis(int32_t axis) {
  if (axis <= 0 || static_cast<size_t>(axis) >= axes_.size() || !sampleInput || !inputType ||
      !std::isfinite(deltaMilliseconds_) || deltaMilliseconds_ < 0.0f)
    return std::nullopt;
  auto &state = axes_[static_cast<size_t>(axis)];
  if (state.remaining[1] || state.remaining[2] || state.factors[4] != 1.0f || state.deviceType != 0)
    return std::nullopt;
  if (state.stamp == inputFrame_) return state.value;
  float sum = 0.0f;
  for (auto &mapping : mappings_) {
    if (mapping.axis != axis) continue;
    const auto sample = sampleInput(mapping.device, mapping.physicalAxis);
    const auto kind = inputType(mapping.device, mapping.physicalAxis);
    if (!sample || !std::isfinite(*sample) || !kind || *kind != 0) return std::nullopt;
    const float lo = mapping.range[0], hi = mapping.range[1];
    if (!std::all_of(mapping.range.begin(), mapping.range.end(), [](float value) { return std::isfinite(value); }) ||
        lo == hi) return std::nullopt;
    if ((lo <= *sample && *sample <= hi) || (hi <= *sample && *sample <= lo))
      sum += (*sample - lo) * (mapping.range[3] - mapping.range[2]) / (hi - lo) + mapping.range[2];
    mapping.sample = *sample;
  }
  sum = std::clamp(sum, -1.0f, 1.0f);
  float step = state.factors[3] * deltaMilliseconds_;
  if (state.threshold <= sum || sum <= -state.threshold)
    step *= sum * state.value >= 0.0f ? state.factors[2] : state.factors[1];
  else
    step *= state.factors[0];
  if (!std::isfinite(step) || step < 0.0f) return std::nullopt;
  step = std::min(step, 1.0f);
  const float difference = sum - state.value;
  state.value = std::clamp(state.value + std::clamp(difference, -step, step), -1.0f, 1.0f);
  state.stamp = inputFrame_;
  return state.value;
}

std::optional<float> GameWorld::playerControlValue(LinkOwner *owner, int32_t axis) {
  auto *node = nodeFor(owner);
  if (!node || !node->loaded || node->nativeDevice != "player" || !node->player ||
      !node->playerInput || node->player->scriptedControlOverride || axis <= 0)
    return std::nullopt;
  if (axis >= 76) return 0.0f;
  return node->playerInput->sampleAxis(axis);
}

GameWorld::GameWorld() = default;

GameWorld::~GameWorld() {
  physicsWorld_.bodies.clear();
  dynamicBodies_.clear();
  for (auto &link : deferredDestroy_) setLinkOwner(*link, nullptr);
  for (auto &entry : nodes_) {
    for (const auto &observer : nodeDestroyObservers_)
      observer.second(*entry.second);
    if (entry.second->player) {
      setLinkOwner(entry.second->player->font, nullptr);
      setLinkOwner(entry.second->player->animationOverlay, nullptr);
      for (auto &link : entry.second->player->debugText) setLinkOwner(link, nullptr);
    }
    if (entry.second->cursor) {
      setLinkOwner(entry.second->cursor->controller, nullptr);
      setLinkOwner(entry.second->cursor->activationTarget, nullptr);
      setLinkOwner(entry.second->cursor->fontA, nullptr);
      setLinkOwner(entry.second->cursor->fontB, nullptr);
    }
    entry.second->carInstance.reset();
    setLinkOwner(entry.second->scriptLink, nullptr);
    setLinkOwner(entry.second->sectorLink, nullptr);
    while (entry.second->head)
      setLinkOwner(*entry.second->head, nullptr);
  }
  for (auto &entry : scripts_)
    while (entry.second->head)
      setLinkOwner(*entry.second->head, nullptr);
  setLinkOwner(playerFactorySoundA_, nullptr);
  setLinkOwner(playerFactorySoundB_, nullptr);
}

bool GameWorld::registerHook(GameHook kind, WorldNode &node) {
  if (nodeFor(&node) != &node) return false;
  return registerHook(kind, &node);
}

bool GameWorld::registerHook(GameHook kind, LinkOwner *owner) {
  auto link = std::make_unique<OwnerLink>();
  setLinkOwner(*link, owner);
  hooks_[static_cast<size_t>(kind)].push_back(std::move(link));
  return true;
}

void GameWorld::unregisterHook(GameHook kind, LinkOwner *owner) {
  for (const auto &link : hooks_[static_cast<size_t>(kind)]) {
    if (link->owner == owner) {
      setLinkOwner(*link, nullptr);
      return;
    }
  }
}

void GameWorld::pruneDetachedHooks(GameHook kind) {
  auto &entries = hooks_[static_cast<size_t>(kind)];
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [](const auto &entry) { return !entry->owner; }), entries.end());
}

std::vector<const WorldNode *> GameWorld::hooks(GameHook kind) const {
  std::vector<const WorldNode *> result;
  for (const auto &link : hooks_[static_cast<size_t>(kind)]) {
    const WorldNode *target = nullptr;
    for (const auto &entry : nodes_)
      if (entry.second.get() == link->owner) {
        target = entry.second.get();
        break;
      }
    result.push_back(target);
  }
  return result;
}

bool sectorHookedType(uint32_t type) {
  return type == 1 || type == 2 || type == 3 || (type >= 0xf && type <= 0x11);
}

WorldNode *GameWorld::install(ResourceTreeHandle handle,
                              const std::string &name, uint32_t flags,
                              bool allocateId) {
  std::optional<uint32_t> id = 0;
  if (allocateId)
    id = ids_.allocate();
  if (!id) {
    tree_.remove(handle);
    return nullptr;
  }
  auto node = std::make_unique<WorldNode>();
  node->handle = handle;
  node->instanceId = *id;
  node->value = *id;
  node->flags = flags;
  node->name = name;
  node->stamp78 = frameCounter() - 1;
  node->stamp7c = frameCounter() - 1;
  if (frameCounter() != 0) {
    node->field6c = 0.0f;
    node->field70 = 0.0f;
  }
  WorldNode *raw = node.get();
  nodes_[handle.id] = std::move(node);
  if (allocateId)
    byInstance_[*id] = raw;
  return raw;
}

WorldNode *GameWorld::addRoot() {
  if (defaultParent())
    return nullptr;
  const auto handle = tree_.add(tree_.root());
  if (!handle)
    return nullptr;
  WorldNode *node = install(*handle, "nulltype", 0, false);
  if (node) {
    node->type = 0;
    node->loaded = true;
    node->loadPriority = 1.0f;
    trackLoadState(*node, 0, true);
  }
  return node;
}

WorldNode *GameWorld::defaultParent() {
  const auto roots = tree_.children(tree_.root());
  if (!roots || roots->empty())
    return nullptr;
  return nodes_.at(roots->front().id).get();
}

WorldNode *GameWorld::findInstance(uint32_t instanceId) {
  const auto found = byInstance_.find(instanceId);
  return found == byInstance_.end() ? nullptr : found->second;
}

WorldNode *GameWorld::findByReceiver(const VmObject *receiver) {
  if (!receiver)
    return nullptr;
  for (const auto &entry : nodes_)
    if (entry.second && entry.second->scriptReceiver == receiver)
      return entry.second.get();
  return nullptr;
}

ScriptNode *GameWorld::findScript(uint32_t resourceId) {
  const auto found = scripts_.find(resourceId);
  return found == scripts_.end() ? nullptr : found->second.get();
}

ScriptNode *GameWorld::addScript(uint32_t resourceId,
                                 const std::string &className) {
  auto &slot = scripts_[resourceId];
  if (slot)
    return nullptr;
  slot = std::make_unique<ScriptNode>();
  slot->resourceId = resourceId;
  slot->value = resourceId;
  slot->className = className;
  return slot.get();
}

ScriptNode *GameWorld::classScript(const std::string &className) {
  const auto cached = classScripts_.find(className);
  if (cached != classScripts_.end())
    return findScript(cached->second);
  WorldNode *parent = defaultParent();
  if (!parent || !parent->loaded)
    return nullptr;
  const auto handle = tree_.add(parent->handle);
  if (!handle)
    return nullptr;
  const std::string descriptor = "L" + className + ";";
  WorldNode *node = install(*handle, descriptor.substr(0, 30), 2);
  if (!node)
    return nullptr;
  node->type = 8;
  node->loaded = true;
  node->loadPriority = 1.0f;
  ScriptNode *script = addScript(node->instanceId, className);
  if (!script) {
    destroy(*node);
    return nullptr;
  }
  script->loaded = true;
  trackLoadState(*script, 8, true);
  classScripts_.emplace(className, node->instanceId);
  return script;
}

WorldNode *GameWorld::nodeFor(LinkOwner *owner) {
  if (!owner)
    return nullptr;
  const auto found = byInstance_.find(owner->value);
  return found != byInstance_.end() &&
                 static_cast<LinkOwner *>(found->second) == owner
             ? found->second
             : nullptr;
}

void GameWorld::orFlags(WorldNode &node, uint32_t bits) {
  node.flags |= bits;
  // FUN_00544f40 relinks to the node the receiver's record points at, which is
  // the node itself. Already linked to itself (the usual case under the type-0
  // root) is the function's early return.
  if (bits & 0x10)
    setLinkOwner(node.sectorLink, &node);
}

void GameWorld::attachSector(WorldNode &node, const WorldNode &parent) {
  // FUN_00544fe0(this = node, parent): the table at 00619d68 is indexed by the
  // parent's type.
  if (!sectorHookedType(parent.type) || (node.flags & 0x10))
    setLinkOwner(node.sectorLink, &node);
  else
    setLinkOwner(node.sectorLink, parent.sectorLink.owner);
}

WorldNode *GameWorld::addChild(WorldNode &parent, const std::string &name,
                               uint32_t flags) {
  return addTypedChild(parent, 1, name, flags);
}

WorldNode *GameWorld::addTypedChild(WorldNode &parent, uint32_t type,
                                    const std::string &name, uint32_t flags) {
  if (type != 1 && type != 2 && type != 3 && type != 5 && type != 7 && type != 12 &&
      type != 13 && type != 14 && type != 18)
    return nullptr;
  const auto handle = tree_.add(parent.handle);
  if (!handle)
    return nullptr;
  WorldNode *node = install(*handle, name, flags);
  if (node) {
    node->type = type;
    if (type == 2 || type == 3 || type == 5)
      node->flags |= 0x100;
    if (sectorHookedType(type))
      attachSector(*node, parent);
    trackLoadState(*node, node->type, false);
  }
  return node;
}

std::optional<ResourceTreeHandle> GameWorld::attachExternal(
    ResourceTreeHandle parent, LinkOwner &owner) {
  const auto handle = tree_.add(parent);
  if (handle)
    externalNodes_.emplace(handle->id, &owner);
  return handle;
}

bool GameWorld::detachExternal(ResourceTreeHandle handle) {
  return externalNodes_.count(handle.id) && destroySubtree(handle);
}

WorldNode *GameWorld::addChildUnderExternal(ResourceTreeHandle parent,
                                            LinkOwner &owner,
                                            uint32_t parentType,
                                            LinkOwner *parentSector,
                                            const std::string &name,
                                            uint32_t flags) {
  return addTypedChildUnderExternal(parent, owner, parentType, parentSector, 1,
                                    name, flags);
}

WorldNode *GameWorld::addTypedChildUnderExternal(ResourceTreeHandle parent,
                                                 LinkOwner &owner,
                                                 uint32_t parentType,
                                                 LinkOwner *parentSector,
                                                 uint32_t type,
                                                 const std::string &name,
                                                 uint32_t flags) {
  if (type != 1 && type != 2 && type != 3 && type != 5 && type != 7 && type != 12 &&
      type != 13 && type != 14 && type != 18)
    return nullptr;
  const auto found = externalNodes_.find(parent.id);
  if (found == externalNodes_.end() || found->second != &owner)
    return nullptr;
  const auto handle = tree_.add(parent);
  if (!handle)
    return nullptr;
  WorldNode *node = install(*handle, name, flags);
  if (node) {
    node->type = type;
    if (type == 2 || type == 3 || type == 5)
      node->flags |= 0x100;
    if (sectorHookedType(type)) {
      if (!sectorHookedType(parentType) || (flags & 0x10))
        setLinkOwner(node->sectorLink, node);
      else
        setLinkOwner(node->sectorLink, parentSector);
    }
    trackLoadState(*node, node->type, false);
  }
  return node;
}

std::optional<std::vector<LinkOwner *>>
GameWorld::childOwners(ResourceTreeHandle parent) const {
  const auto handles = tree_.children(parent);
  if (!handles)
    return std::nullopt;
  std::vector<LinkOwner *> result;
  for (const auto handle : *handles) {
    const auto local = nodes_.find(handle.id);
    if (local != nodes_.end())
      result.push_back(local->second.get());
    else {
      const auto external = externalNodes_.find(handle.id);
      if (external != externalNodes_.end())
        result.push_back(external->second);
    }
  }
  return result;
}

LinkOwner *GameWorld::parentOwner(ResourceTreeHandle child) const {
  const auto parent = tree_.parent(child);
  if (!parent)
    return nullptr;
  const auto local = nodes_.find(parent->id);
  if (local != nodes_.end())
    return local->second.get();
  const auto external = externalNodes_.find(parent->id);
  return external == externalNodes_.end() ? nullptr : external->second;
}

void GameWorld::forgetLoadState(LinkOwner &owner) {
  for (auto *lists : {&loadedNodes_, &unloadedNodes_})
    for (auto &entry : *lists) {
      auto &nodes = entry.second;
      nodes.erase(std::remove(nodes.begin(), nodes.end(), &owner), nodes.end());
    }
}

void GameWorld::trackLoadState(LinkOwner &owner, uint32_t type, bool loaded) {
  forgetLoadState(owner);
  (loaded ? loadedNodes_ : unloadedNodes_)[type].push_back(&owner);
}

std::vector<LinkOwner *> GameWorld::loadList(uint32_t type, bool loaded) const {
  const auto &lists = loaded ? loadedNodes_ : unloadedNodes_;
  const auto found = lists.find(type);
  return found == lists.end() ? std::vector<LinkOwner *>{} : found->second;
}

void GameWorld::markLoaded(WorldNode &node) {
  node.loaded = true;
  trackLoadState(node, node.type, true);
}

void GameWorld::noteRecentUse(LinkOwner &owner, uint32_t type, uint32_t &stamp, bool &queued) {
  if (stamp == frameCounter()) return;
  stamp = frameCounter();
  if (!queued) {
    forgetLoadState(owner);
    recentlyUsed_[type].push_back(&owner);
    queued = true;
  }
}
std::vector<LinkOwner *> GameWorld::recentlyUsed(uint32_t type) const {
  const auto found = recentlyUsed_.find(type);
  return found == recentlyUsed_.end() ? std::vector<LinkOwner *>{} : found->second;
}

void GameWorld::finishRecentUse(LinkOwner &owner, uint32_t type) {
  auto &recent = recentlyUsed_[type];
  recent.erase(std::remove(recent.begin(), recent.end(), &owner), recent.end());
  trackLoadState(owner, type, true);
}

bool GameWorld::attachConfig(WorldNode &node) {
  if (nodeFor(&node) != &node)
    return false;
  setLinkOwner(configLink_, &node);
  return true;
}

WorldNode *GameWorld::configNode() { return nodeFor(configLink_.owner); }

bool GameWorld::attachGameParent(LinkOwner &node) {
  bool found = nodeFor(&node) != nullptr;
  for (const auto &entry : externalNodes_)
    found |= entry.second == &node;
  if (!found)
    return false;
  setLinkOwner(gameParentLink_, &node);
  return true;
}

std::optional<uint32_t> GameWorld::gameParentId() const {
  if (!gameParentLink_.owner)
    return std::nullopt;
  return gameParentLink_.cachedValue;
}

void GameWorld::initPlayerFactory(LinkOwner &soundResource) {
  setLinkOwner(playerFactorySoundA_, &soundResource);
  setLinkOwner(playerFactorySoundB_, &soundResource);
}

bool GameWorld::attachPlayerInput(WorldNode &node) {
  if (nodeFor(&node) != &node || !node.playerInput)
    return false;
  playerInputs_.insert(playerInputs_.begin(), &node);
  return true;
}

uint64_t GameWorld::addNodeDestroyObserver(std::function<void(LinkOwner &)> observer) {
  const uint64_t token = nextNodeDestroyObserver_++;
  nodeDestroyObservers_.emplace(token, std::move(observer));
  return token;
}

void GameWorld::removeNodeDestroyObserver(uint64_t token) {
  nodeDestroyObservers_.erase(token);
}

bool GameWorld::registerStaticBox(WorldNode &node, const std::array<float, 3> &halfExtents) {
  if (nodeFor(&node) != &node || node.type != 2 || node.physicsBox) return false;
  for (float extent : halfExtents) if (!std::isfinite(extent) || extent < 0) return false;
  PhysicsBox box;
  box.halfExtents = halfExtents;
  box.radius = std::sqrt(halfExtents[0] * halfExtents[0] + halfExtents[1] * halfExtents[1] + halfExtents[2] * halfExtents[2]) + 0.05f;
  if (!std::isfinite(box.radius)) return false;
  node.physicsBox = box;
  boxGeometries_.push_back(&node);
  boxBodies_.push_back(&node);
  return true;
}

bool GameWorld::registerDynamicBody(WorldNode &node, std::shared_ptr<physics::Body> body,
                                    std::shared_ptr<const physics::BodyProto> prototype) {
  if (nodeFor(&node) != &node || node.type != 2 || !body || !prototype || body->proto != prototype.get() ||
      dynamicBodies_.count(&node) ||
      std::find(physicsWorld_.bodies.begin(), physicsWorld_.bodies.end(), body.get()) != physicsWorld_.bodies.end())
    return false;
  auto *raw = body.get();
  dynamicBodies_.emplace(&node, std::make_shared<DynamicBodyOwnership>(DynamicBodyOwnership{std::move(prototype), std::move(body)}));
  physicsWorld_.bodies.push_back(raw);
  return true;
}

bool GameWorld::unregisterDynamicBody(WorldNode &node) {
  const auto found = dynamicBodies_.find(&node);
  if (found == dynamicBodies_.end()) return false;
  auto *body = found->second->body.get();
  physicsWorld_.bodies.erase(std::remove(physicsWorld_.bodies.begin(), physicsWorld_.bodies.end(), body), physicsWorld_.bodies.end());
  dynamicBodies_.erase(found);
  return true;
}

std::weak_ptr<physics::Body> GameWorld::dynamicBody(WorldNode &node) const {
  const auto found = dynamicBodies_.find(&node);
  if (found == dynamicBodies_.end()) return {};
  return std::shared_ptr<physics::Body>(found->second, found->second->body.get());
}

namespace {
struct DirectCameraFollow {
  CameraDeviceResources *device = nullptr;
  std::shared_ptr<physics::Body> camera;
  std::shared_ptr<physics::Body> look;
  std::shared_ptr<physics::Body> move;
};

// Stock 00438b60 applies the direct look torque before it updates field of
// view, and this device has no height, plane, or angle fields. A nonzero roll
// would add torque that cameraFollowForce does not compute.
bool bindDirectCameraFollow(GameWorld &world, const CameraDeviceResources &device,
                            DirectCameraFollow &out, const char **block = nullptr) {
  const auto fail = [&](const char *code) {
    if (block) *block = code;
    return false;
  };
  if (block) *block = nullptr;
  if (device.commands.roll != 0.0f) return fail("roll");
  if (!device.lookTarget.owner) return fail("look");
  if (!device.moveTarget.owner) return fail("move");
  auto *physicsNode = device.physicsNode();
  auto *lookNode = world.nodeFor(device.lookTarget.owner);
  auto *moveNode = world.nodeFor(device.moveTarget.owner);
  if (!physicsNode) return fail("phys");
  if (!lookNode) return fail("lnod");
  if (!moveNode) return fail("mnod");
  auto camera = world.dynamicBody(*physicsNode).lock();
  auto look = world.dynamicBody(*lookNode).lock();
  auto move = world.dynamicBody(*moveNode).lock();
  if (!camera) return fail("cbod");
  if (!look) return fail("lbod");
  if (!move) return fail("mbod");
  if (!camera->proto) return fail("prot");
  if (camera->current > 1) return fail("curr");
  if (camera->attachment) return fail("attc");
  if (camera->flags & physics::kBodyChild) return fail("chld");
  if (camera->flags & physics::kBodyHasChildren) return fail("kids");
  if (camera->flags & physics::kBodyBit10) return fail("bitx");
  if ((camera->flags & physics::kBodyActive) == 0) return fail("idle");
  if (camera->proto->flags & physics::kProtoStatic) return fail("stat");
  if (camera->proto->invMass <= 0.0f) return fail("mass");
  CameraFollowForceInput input;
  input.otherBranchesInactive = true;
  input.command = device.commands;
  input.fieldOfView = device.fieldOfView;
  input.maxForce = device.maxForce;
  input.camera = camera->state[camera->current];
  input.lookTarget = look->state[look->current];
  input.moveTarget = move->state[move->current];
  const char *forceBlock = nullptr;
  if (!cameraFollowForce(input, device.followSmoothing, &forceBlock))
    return fail(forceBlock ? forceBlock : "fout");
  out.device = const_cast<CameraDeviceResources *>(&device);
  out.camera = std::move(camera);
  out.look = std::move(look);
  out.move = std::move(move);
  return true;
}
}

bool GameWorld::directCameraFollowReady(const CameraDeviceResources &camera,
                                        const char **block) {
  DirectCameraFollow follow;
  return bindDirectCameraFollow(*this, camera, follow, block);
}

bool GameWorld::armedCameraFollowCount(int &ready, std::string &error) {
  ready = 0;
  for (const auto &entry : nodes_) {
    const auto *device = entry.second->cameraDevice.get();
    if (!device || !device->lookTarget.owner || !device->moveTarget.owner) continue;
    if (!directCameraFollowReady(*device)) {
      error = "empty physics: camera follow is unsupported";
      return false;
    }
    ++ready;
  }
  return true;
}

bool GameWorld::integrateArmedCameraFollows(float seconds, bool &otherDynamic, std::string &error) {
  std::vector<DirectCameraFollow> follows;
  for (const auto &entry : nodes_) {
    auto *device = entry.second->cameraDevice.get();
    if (!device || !device->lookTarget.owner || !device->moveTarget.owner) continue;
    DirectCameraFollow follow;
    if (!bindDirectCameraFollow(*this, *device, follow)) {
      error = "empty physics: camera follow is unsupported";
      return false;
    }
    follows.push_back(std::move(follow));
  }
  otherDynamic = false;
  for (const auto &entry : dynamicBodies_) {
    const auto *body = entry.second->body.get();
    const bool camera = std::any_of(follows.begin(), follows.end(),
                                    [&](const DirectCameraFollow &follow) {
                                      return follow.camera.get() == body;
                                    });
    if (!camera) otherDynamic = true;
  }
  if (!std::isfinite(physicsWorld_.pending) || physicsWorld_.pending < 0.0f ||
      !std::isfinite(physicsWorld_.time) || !std::isfinite(physicsWorld_.pending + seconds)) {
    error = "empty physics: invalid step";
    return false;
  }
  const float total = physicsWorld_.pending + seconds;
  if (total < physics::k::minimumStep) {
    physicsWorld_.pending = total;
    error.clear();
    return true;
  }
  const float oldPending = physicsWorld_.pending;
  const float oldTime = physicsWorld_.time;
  const float oldLast = physicsWorld_.lastStep;
  const uint32_t oldSteps = physicsWorld_.stepsDone;
  std::vector<physics::Body> oldBodies;
  std::vector<CameraFollowSmoothing> oldSmoothing;
  oldBodies.reserve(follows.size());
  oldSmoothing.reserve(follows.size());
  for (const auto &follow : follows) {
    oldBodies.push_back(*follow.camera);
    oldSmoothing.push_back(follow.device->followSmoothing);
  }
  const auto rollback = [&] {
    for (size_t i = 0; i < follows.size(); ++i) {
      *follows[i].camera = oldBodies[i];
      follows[i].device->followSmoothing = oldSmoothing[i];
    }
    physicsWorld_.pending = oldPending;
    physicsWorld_.time = oldTime;
    physicsWorld_.lastStep = oldLast;
    physicsWorld_.stepsDone = oldSteps;
  };
  physicsWorld_.pending = 0.0f;
  physicsWorld_.lastStep = total;
  for (const auto &follow : follows) {
    if (!physics::accumulateWorldForces(physicsWorld_.params, *follow.camera)) {
      rollback();
      error = "empty physics: camera follow is unsupported";
      return false;
    }
  }
  for (const auto &follow : follows) {
    CameraFollowForceInput input;
    input.otherBranchesInactive = true;
    input.command = follow.device->commands;
    input.fieldOfView = follow.device->fieldOfView;
    input.maxForce = follow.device->maxForce;
    input.camera = follow.camera->state[follow.camera->current];
    input.lookTarget = follow.look->state[follow.look->current];
    input.moveTarget = follow.move->state[follow.move->current];
    const auto applied = cameraFollowForce(input, follow.device->followSmoothing);
    if (!applied) {
      rollback();
      error = "empty physics: camera follow is unsupported";
      return false;
    }
    follow.device->followSmoothing = applied->smoothing;
    physics::addForce(*follow.camera, applied->force, nullptr);
    auto &state = follow.camera->state[follow.camera->current];
    for (size_t i = 0; i < 3; ++i) state.torque[i] += applied->torque[i];
  }
  for (const auto &follow : follows)
    if (physics::stepBody(*follow.camera, total)) ++physicsWorld_.stepsDone;
  for (const auto &follow : follows)
    if (physics::commit(*follow.camera) == physics::CommitResult::WentToSleep)
      physics::sleepBodyState(*follow.camera);
  physicsWorld_.time += total;
  if (!std::isfinite(physicsWorld_.time)) {
    rollback();
    error = "empty physics: camera follow is unsupported";
    return false;
  }
  error.clear();
  return true;
}

bool GameWorld::stepArmedCameraFollow(float seconds, std::string &error) {
  if (!std::isfinite(seconds) || seconds <= 0.0f) {
    error = "camera follow: invalid step";
    return false;
  }
  // Stock 00438b60 adds the camera force while other bodies exist. A child
  // link (flag 4) is a joint, and this step does not solve joints, wheel
  // contacts, or collision. Those bodies stay registered and unstepped.
  for (const auto &entry : dynamicBodies_) {
    const auto *body = entry.second->body.get();
    if (body && body->attachment) {
      error = "camera follow: joints are unsupported";
      return false;
    }
  }
  bool otherDynamic = false;
  if (!integrateArmedCameraFollows(seconds, otherDynamic, error)) return false;
  (void)otherDynamic;
  error.clear();
  return true;
}

bool GameWorld::stepEmptyPhysics(float seconds, std::string &error) {
  if (!std::isfinite(seconds) || seconds <= 0.0f) {
    error = "empty physics: invalid step";
    return false;
  }
  int readyFollows = 0;
  if (!armedCameraFollowCount(readyFollows, error)) return false;
  if (readyFollows == 0 && !dynamicBodies_.empty()) {
    error = "empty physics: dynamic bodies are unsupported";
    return false;
  }
  if (!loadList(9, true).empty() || !loadList(10, true).empty()) {
    error = "empty physics: physics state is unsupported";
    return false;
  }
  for (const auto *owner : loadList(2, true)) {
    const auto *node = const_cast<GameWorld *>(this)->nodeFor(const_cast<LinkOwner *>(owner));
    if (!node || !node->physicsBox || node->physicsBox->geometryFlags != 5 ||
        std::find(boxBodies_.begin(), boxBodies_.end(), node) == boxBodies_.end() ||
        std::find(boxGeometries_.begin(), boxGeometries_.end(), node) == boxGeometries_.end()) {
      error = "empty physics: physics state is unsupported";
      return false;
    }
    const auto descendants = children(*node);
    if (!descendants || !descendants->empty()) {
      error = "empty physics: box children are unsupported";
      return false;
    }
  }
  for (const auto &entry : nodes_) {
    const auto &node = *entry.second;
    if (!node.nativeDevice.empty() && node.nativeDevice != "cursor" &&
        node.nativeDevice != "player" && node.nativeDevice != "car" &&
        node.nativeDevice != "part" &&
        !(readyFollows > 0 && node.nativeDevice == "camera")) {
      error = "empty physics: native device physics state is unsupported";
      return false;
    }
  }
  for (const auto &hook : hooks_[static_cast<size_t>(GameHook::Kind9)]) if (hook->owner) {
    const auto *node = nodeFor(hook->owner);
    if (!node || (node->nativeDevice != "car" && node->nativeDevice != "part")) {
      error = "empty physics: physics callback is unsupported";
      return false;
    }
  }
  if (readyFollows == 0) {
    if (physicsWorld_.stepFreeBodies(seconds) == physics::FreeBodyStepResult::Unsupported) {
      error = "empty physics: physics step is unsupported";
      return false;
    }
    error.clear();
    return true;
  }
  bool otherDynamic = false;
  if (!integrateArmedCameraFollows(seconds, otherDynamic, error)) return false;
  if (otherDynamic) {
    error = "empty physics: dynamic bodies are unsupported";
    return false;
  }
  error.clear();
  return true;
}

size_t GameWorld::countNodeType(uint32_t type) const {
  size_t count = 0;
  for (const auto &entry : nodes_) if (entry.second->type == type) ++count;
  return count;
}

PlayerCarNote GameWorld::playerCarNote() const {
  PlayerCarNote note;
  for (const auto &entry : nodes_) {
    const auto &car = entry.second->carInstance;
    if (!car || !car->isCar)
      continue;
    if (note.count == 0) {
      note.instanceId = entry.second->instanceId;
      note.x = car->position[0];
      note.y = car->position[1];
      note.z = car->position[2];
      const float vx = car->linearVelocity[0];
      const float vy = car->linearVelocity[1];
      const float vz = car->linearVelocity[2];
      note.speed = std::sqrt(vx * vx + vy * vy + vz * vz);
      note.gear = car->currentGear;
      note.rpm = car->engineRpm;
    }
    ++note.count;
  }
  return note;
}

bool GameWorld::hasNodeType(uint32_t type) const {
  for (const auto &entry : nodes_) if (entry.second->type == type) return true;
  return false;
}

bool GameWorld::hasNodeFlag(uint32_t flag) const {
  for (const auto &entry : nodes_)
    if ((entry.second->flags & flag) != 0) return true;
  return false;
}

bool GameWorld::hasLoadedNodeType(uint32_t type) const {
  for (const auto &entry : nodes_)
    if (entry.second->type == type && entry.second->loaded) return true;
  return false;
}

void GameWorld::noteInstanceCreated(WorldNode &node) {
  countInstance(node.countedInstance);
}

void GameWorld::countInstance(bool &counted) {
  if (!counted) {
    counted = true;
    ++createdInstances_;
  }
}

void GameWorld::uncountInstance(bool &counted) {
  if (counted) {
    --createdInstances_;
    counted = false;
  }
}

void GameWorld::release(WorldNode &node) {
  unregisterDynamicBody(node);
  boxGeometries_.erase(std::remove(boxGeometries_.begin(), boxGeometries_.end(), &node), boxGeometries_.end());
  boxBodies_.erase(std::remove(boxBodies_.begin(), boxBodies_.end(), &node), boxBodies_.end());
  uncountInstance(node.countedInstance);
  auto &recent = recentlyUsed_[node.type];
  recent.erase(std::remove(recent.begin(), recent.end(), &node), recent.end());
  node.cameraFrame.reset();
  setLinkOwner(node.cameraViewport, nullptr);
  for (const auto &observer : nodeDestroyObservers_)
    observer.second(node);
  playerInputs_.erase(std::remove(playerInputs_.begin(), playerInputs_.end(), &node),
                      playerInputs_.end());
  if (node.player) {
    setLinkOwner(node.player->font, nullptr);
    setLinkOwner(node.player->animationOverlay, nullptr);
    for (auto &link : node.player->debugText) setLinkOwner(link, nullptr);
  }
  if (node.cursor) {
    setLinkOwner(node.cursor->controller, nullptr);
    setLinkOwner(node.cursor->hoveredBox, nullptr);
    setLinkOwner(node.cursor->pressedBox, nullptr);
    setLinkOwner(node.cursor->activationTarget, nullptr);
    setLinkOwner(node.cursor->fontA, nullptr);
    setLinkOwner(node.cursor->fontB, nullptr);
    setLinkOwner(node.cursor->viewport, nullptr);
    setLinkOwner(node.cursor->camera, nullptr);
    setLinkOwner(node.cursor->target, nullptr);
    setLinkOwner(node.cursor->pointer, nullptr);
    setLinkOwner(node.cursor->style, nullptr);
  }
  node.carInstance.reset();
  if (node.type == 8) {
    auto script = scripts_.find(node.instanceId);
    if (script != scripts_.end()) {
      forgetLoadState(*script->second);
      classScripts_.erase(script->second->className);
      while (script->second->head)
        setLinkOwner(*script->second->head, nullptr);
      scripts_.erase(script);
    }
  }
  setLinkOwner(node.scriptLink, nullptr);
  forgetLoadState(node);
  setLinkOwner(node.sectorLink, nullptr);
  while (node.head)
    setLinkOwner(*node.head, nullptr);
  ids_.release(node.instanceId);
  byInstance_.erase(node.instanceId);
}

bool GameWorld::destroySubtree(ResourceTreeHandle handle) {
  if (!tree_.contains(handle))
    return false;
  std::vector<ResourceTreeHandle> doomed{handle};
  for (size_t i = 0; i < doomed.size(); ++i) {
    const auto kids = tree_.children(doomed[i]);
    if (kids)
      doomed.insert(doomed.end(), kids->begin(), kids->end());
  }
  if (!tree_.remove(handle))
    return false;
  for (const auto handle : doomed) {
    const auto found = nodes_.find(handle.id);
    if (found == nodes_.end()) {
      const auto external = externalNodes_.find(handle.id);
      if (external != externalNodes_.end()) {
        forgetLoadState(*external->second);
        for (auto &entry : recentlyUsed_) {
          auto &recent = entry.second;
          recent.erase(std::remove(recent.begin(), recent.end(), external->second), recent.end());
        }
        externalNodes_.erase(external);
      }
      continue;
    }
    release(*found->second);
    nodes_.erase(found);
  }
  return true;
}

bool GameWorld::queueDestroy(WorldNode &node) {
  if (nodeFor(&node) != &node || (node.type != 1 && node.type != 3 && node.type != 12)) return false;
  auto link = std::make_unique<OwnerLink>();
  setLinkOwner(*link, &node);
  deferredDestroy_.push_back(std::move(link));
  return true;
}

bool GameWorld::reparent(WorldNode &node, LinkOwner &newParent) {
  if (!nodes_.count(node.handle.id) || &node == defaultParent()) return false;
  ResourceTreeHandle parentHandle{};
  WorldNode *parentWorld = nodeFor(&newParent);
  if (parentWorld) {
    parentHandle = parentWorld->handle;
  } else {
    for (const auto &entry : externalNodes_) {
      if (entry.second == &newParent) {
        parentHandle = ResourceTreeHandle{entry.first};
        break;
      }
    }
  }
  if (!parentHandle.id) return false;
  if (!tree_.move(node.handle, parentHandle)) return false;
  const auto propagateSector = [&](auto &self, WorldNode &current, const WorldNode *parentNode) -> void {
    if (sectorHookedType(current.type)) {
      if (parentNode) attachSector(current, *parentNode);
      else if (current.flags & 0x10) setLinkOwner(current.sectorLink, &current);
    }
    const auto kids = children(current);
    if (kids) {
      for (auto *child : *kids) {
        if (child) self(self, *child, &current);
      }
    }
  };
  propagateSector(propagateSector, node, parentWorld);
  return true;
}

bool GameWorld::drainDeferredDestroy(std::string &error) {
  if (drainingDestroy_) { error = "deferred destroy is already draining"; return false; }
  struct DrainGuard { bool &flag; ~DrainGuard() { flag = false; } } guard{drainingDestroy_};
  drainingDestroy_ = true;
  while (!deferredDestroy_.empty()) {
    auto &link = *deferredDestroy_.front();
    auto *target = nodeFor(link.owner);
    if (link.owner && !target) {
      error = "deferred destroy target is unsupported";
      return false;
    }
    if (target && !destroy(*target)) {
      error = "deferred destroy cannot remove target subtree";
      return false;
    }
    setLinkOwner(link, nullptr);
    deferredDestroy_.erase(deferredDestroy_.begin());
  }
  error.clear();
  return true;
}

bool GameWorld::destroy(WorldNode &node) {
  if (!nodes_.count(node.handle.id))
    return false;
  // Mounted nodes belong to ResourceWorld, which must remove them itself.
  std::vector<ResourceTreeHandle> pending{node.handle};
  for (size_t i = 0; i < pending.size(); ++i) {
    if (externalNodes_.count(pending[i].id))
      return false;
    const auto children = tree_.children(pending[i]);
    if (children)
      pending.insert(pending.end(), children->begin(), children->end());
  }
  return destroySubtree(node.handle);
}

std::optional<std::vector<WorldNode *>>
GameWorld::children(const WorldNode &node) const {
  const auto kids = tree_.children(node.handle);
  if (!kids)
    return std::nullopt;
  std::vector<WorldNode *> result;
  for (const auto handle : *kids) {
    const auto found = nodes_.find(handle.id);
    if (found != nodes_.end())
      result.push_back(found->second.get());
  }
  return result;
}

const WorldNode *GameWorld::parent(const WorldNode &node) const {
  const auto handle = tree_.parent(node.handle);
  if (!handle)
    return nullptr;
  const auto found = nodes_.find(handle->id);
  return found == nodes_.end() ? nullptr : found->second.get();
}
