#include "world_model.hpp"
#include "camera_device_factory.hpp"
#include "camera_follow_force.hpp"
#include "car_physics.hpp"
#include "physics_compound_step.hpp"
#include "ground_runtime.hpp"
#include "resource_world.hpp"
#include "run_log.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace {
uint32_t &globalFrameCounter() {
  static uint32_t value = 1;
  return value;
}
} // namespace

void GameWorld::attachResourceWorld(ResourceWorld &resources) {
  resourceWorlds_.push_back(&resources);
}
void GameWorld::detachResourceWorld(ResourceWorld &resources) {
  resourceWorlds_.erase(
      std::remove(resourceWorlds_.begin(), resourceWorlds_.end(), &resources),
      resourceWorlds_.end());
}

uint32_t GameWorld::frameCounter() const { return globalFrameCounter(); }
void GameWorld::setFrameCounter(uint32_t frame) {
  globalFrameCounter() = frame;
}

bool GameWorld::changeCursorVisibility(bool show, std::string &error) {
  const int64_t next = int64_t(cursorDisplayCount_) + (show ? 1 : -1);
  if (next < INT32_MIN || next > INT32_MAX) {
    error = "cursor display count overflow";
    return false;
  }
  if (!cursorSelector_ || !cursorSelector_(0, next >= 0, error)) {
    if (!cursorSelector_)
      error = "cursor host unavailable";
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
  return {aspect,
          fieldOfView,
          nearDistance * 10.0f,
          farDistance * 10.0f,
          lodBias,
          lodAmplitude,
          culling,
          planeTest,
          {}};
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
  return {camera.viewportAspect, camera.fieldOfView,
          camera.nearDistance,   farMetric,
          camera.lodBias,        camera.lodAmplitude,
          camera.planeTest};
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

std::array<float, 16>
cameraProjectionMatrix(const CameraRendererFields &fields) {
  constexpr float degreesToRadians = 0x1.1df46ap-6f;
  const float radians = fields.fieldOfView * degreesToRadians;
  const float halfAngle = radians * 0.5f;
  const float cotangent = static_cast<float>(std::cos(halfAngle)) /
                          static_cast<float>(std::sin(halfAngle));
  const float depth =
      fields.farMetric / (fields.nearDistance - fields.farMetric);
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
      if (!std::isfinite(value))
        return std::nullopt;
      augmented[row][col] = value;
    }
    augmented[row][row + 4] = 1.0;
  }
  for (size_t col = 0; col < 4; ++col) {
    size_t pivot = col;
    for (size_t row = col + 1; row < 4; ++row)
      if (std::abs(augmented[row][col]) > std::abs(augmented[pivot][col]))
        pivot = row;
    if (augmented[pivot][col] == 0.0)
      return std::nullopt;
    for (size_t i = 0; i < 8; ++i)
      std::swap(augmented[col][i], augmented[pivot][i]);
    const double divisor = augmented[col][col];
    for (double &value : augmented[col])
      value /= divisor;
    for (size_t row = 0; row < 4; ++row) {
      if (row == col)
        continue;
      const double factor = augmented[row][col];
      for (size_t i = 0; i < 8; ++i)
        augmented[row][i] -= factor * augmented[col][i];
    }
  }
  std::array<float, 16> view{};
  for (size_t row = 0; row < 4; ++row)
    for (size_t col = 0; col < 4; ++col) {
      view[row * 4 + col] = static_cast<float>(augmented[row][col + 4]);
      if (!std::isfinite(view[row * 4 + col]))
        return std::nullopt;
    }
  return view;
}

std::optional<CameraViewState>
cameraViewState(const CameraResource &camera,
                const std::array<float, 16> &boneWorldMatrix,
                float viewportAspect) {
  const auto view = cameraViewMatrix(boneWorldMatrix);
  if (!view)
    return std::nullopt;
  const auto fields = updateCameraRendererFields(camera, viewportAspect);
  const auto projection = cameraProjectionMatrix(fields);
  for (const float value : projection)
    if (!std::isfinite(value))
      return std::nullopt;
  return CameraViewState{fields, *view, projection};
}

std::optional<std::array<std::array<float, 4>, 5>>
cameraFrustumPlanes(const CameraRendererFields &fields,
                    const std::array<float, 16> &worldMatrix) {
  for (float value : worldMatrix)
    if (!std::isfinite(value))
      return std::nullopt;
  if (!std::isfinite(fields.fieldOfView) ||
      !std::isfinite(fields.viewportAspect) || !std::isfinite(fields.farMetric))
    return std::nullopt;
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
    planes[1][i] =
        static_cast<float>(hz * worldMatrix[8 + i] + hx * worldMatrix[i]);
    planes[2][i] =
        static_cast<float>(hz * worldMatrix[8 + i] - hx * worldMatrix[i]);
    planes[3][i] =
        static_cast<float>(vz * worldMatrix[8 + i] + vy * worldMatrix[4 + i]);
    planes[4][i] =
        static_cast<float>(vz * worldMatrix[8 + i] - vy * worldMatrix[4 + i]);
  }
  for (size_t i = 0; i < planes.size(); ++i) {
    const auto &p = planes[i];
    const float dot = p[0] * worldMatrix[12] + p[1] * worldMatrix[13] +
                      p[2] * worldMatrix[14];
    planes[i][3] = (dot + (i == 0 ? fields.farMetric : 0.0f)) * 0.1f + 2.0f;
    for (float value : planes[i])
      if (!std::isfinite(value))
        return std::nullopt;
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
  const auto cameraPoint =
      transform({point[0], point[1], point[2], 1.0f}, view);
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
  if (!inverse)
    return std::nullopt;
  for (float value : point)
    if (!std::isfinite(value))
      return std::nullopt;
  for (float value : world)
    if (!std::isfinite(value))
      return std::nullopt;
  std::array<float, 4> camera{};
  const std::array<float, 4> input{point[0], point[1], point[2], 1.0f};
  for (size_t column = 0; column < 4; ++column)
    for (size_t row = 0; row < 4; ++row)
      camera[column] += input[row] * (*inverse)[row * 4 + column];
  if (!std::isfinite(camera[3]) || camera[3] == 0)
    return std::nullopt;
  std::array<float, 3> result{};
  for (size_t column = 0; column < 3; ++column) {
    result[column] = world[12 + column];
    for (size_t row = 0; row < 3; ++row)
      result[column] += camera[row] / camera[3] * world[row * 4 + column];
    if (!std::isfinite(result[column]))
      return std::nullopt;
  }
  return result;
}

std::optional<std::array<float, 2>>
cameraViewportPoint(const std::array<float, 3> &point,
                    const ViewportInstance &viewport, int32_t outputWidth,
                    int32_t outputHeight) {
  if (outputWidth <= 0 || outputHeight <= 0 || !std::isfinite(point[0]) ||
      !std::isfinite(point[1]) || !std::isfinite(point[2]) ||
      point[0] < -1.0f || point[0] > 1.0f || point[1] < -1.0f ||
      point[1] > 1.0f || point[2] < 0.0f || point[2] > 1.0f)
    return std::nullopt;
  const float x =
      (viewport.left + (point[0] + 1.0f) * 0.5f * viewport.width) * outputWidth;
  const float y = (viewport.top + (1.0f - point[1]) * 0.5f * viewport.height) *
                  outputHeight;
  if (!std::isfinite(x) || !std::isfinite(y))
    return std::nullopt;
  return std::array<float, 2>{x, y};
}

std::optional<std::vector<CameraClipVertex>>
clipCameraTriangle(const std::array<CameraClipVertex, 3> &triangle) {
  for (const auto &vertex : triangle) {
    for (float component : vertex.position)
      if (!std::isfinite(component))
        return std::nullopt;
    for (float component : vertex.uv)
      if (!std::isfinite(component))
        return std::nullopt;
  }
  std::vector<CameraClipVertex> polygon(triangle.begin(), triangle.end());
  const auto distance = [](const CameraClipVertex &v, int plane) {
    const auto &p = v.position;
    switch (plane) {
    case 0:
      return static_cast<double>(p[3]) + p[0];
    case 1:
      return static_cast<double>(p[3]) - p[0];
    case 2:
      return static_cast<double>(p[3]) + p[1];
    case 3:
      return static_cast<double>(p[3]) - p[1];
    case 4:
      return static_cast<double>(p[2]);
    default:
      return static_cast<double>(p[3]) - p[2];
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
        const double t =
            previousDistance / (previousDistance - currentDistance);
        CameraClipVertex intersection{};
        for (size_t i = 0; i < 4; ++i)
          intersection.position[i] =
              static_cast<float>(previous.position[i] +
                                 t * (static_cast<double>(current.position[i]) -
                                      previous.position[i]));
        for (size_t i = 0; i < 2; ++i)
          intersection.uv[i] = static_cast<float>(
              previous.uv[i] +
              t * (static_cast<double>(current.uv[i]) - previous.uv[i]));
        next.push_back(intersection);
      }
      if (currentInside)
        next.push_back(current);
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

void ViewportRenderAttachments::prepare(LinkOwner &instance,
                                        std::optional<uint32_t> priority) {
  auto matches = [&instance](const auto &entry) {
    return entry->instance.owner == &instance;
  };
  if (std::any_of(pending_.begin(), pending_.end(), matches) ||
      std::any_of(active_.begin(), active_.end(), matches))
    return;
  auto entry = std::make_unique<Entry>();
  setLinkOwner(entry->instance, &instance);
  entry->priority = priority;
  pending_.push_back(std::move(entry));
}

bool ViewportRenderAttachments::activate(LinkOwner &instance,
                                         int32_t priority) {
  active_.erase(
      std::remove_if(active_.begin(), active_.end(),
                     [](const auto &entry) { return !entry->instance.owner; }),
      active_.end());
  auto matches = [&instance](const auto &entry) {
    return entry->instance.owner == &instance;
  };
  auto it = std::find_if(pending_.begin(), pending_.end(), matches);
  if (it == pending_.end())
    return std::any_of(active_.begin(), active_.end(), matches);
  const std::optional<uint32_t> nextPriority =
      priority == -1 ? (*it)->priority
                     : std::optional<uint32_t>(static_cast<uint32_t>(priority));
  if (!active_.empty() &&
      (!nextPriority ||
       std::any_of(active_.begin(), active_.end(),
                   [](const auto &other) { return !other->priority; })))
    return false;
  auto entry = std::move(*it);
  pending_.erase(it);
  entry->priority = nextPriority;
  auto position =
      std::find_if(active_.begin(), active_.end(), [&](const auto &other) {
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
      if (entry->instance.owner == &instance)
        setLinkOwner(entry->instance, nullptr);
    entries.erase(std::remove_if(
                      entries.begin(), entries.end(),
                      [](const auto &entry) { return !entry->instance.owner; }),
                  entries.end());
  };
  removeFrom(pending_);
  removeFrom(active_);
}

void GameWorld::registerViewport(WorldNode &node, int32_t initialPriority) {
  viewportInventory_.prepare(
      node,
      initialPriority == -1
          ? std::nullopt
          : std::optional<uint32_t>(static_cast<uint32_t>(initialPriority)));
}

bool GameWorld::activateViewport(WorldNode &node) {
  return viewportInventory_.activate(node, -1);
}

void GameWorld::deactivateViewport(WorldNode &node) {
  viewportInventory_.remove(node);
}

void GameWorld::logViewportAttachments() {
  static bool logged = false;
  if (logged || !runlog::active())
    return;
  logged = true;
  std::vector<WorldNode *> allViewports;
  for (auto &entry : nodes_)
    if (entry.second && entry.second->viewport &&
        entry.second->viewport->renderAttachments)
      allViewports.push_back(entry.second.get());
  for (auto *node : allViewports) {
    for (auto *entry : node->viewport->renderAttachments->active()) {
      auto *draw = nodeFor(entry);
      runlog::event(
          runlog::EventKind::Note,
          "post-activate viewport 0x%x attachment: node=%p type=%u loaded=%d "
          "id=0x%x",
          node->instanceId, (const void *)draw,
          draw ? static_cast<unsigned>(draw->type) : 0u,
          draw && draw->loaded ? 1 : 0, draw ? draw->instanceId : 0u);
    }
  }
}

std::vector<WorldNode *> GameWorld::activeViewports() {
  std::vector<WorldNode *> result;
  for (auto *owner : viewportInventory_.active()) {
    auto *node = nodeFor(owner);
    if (node && node->type == 18 && node->viewport &&
        node->viewport->list == ViewportList::Active)
      result.push_back(node);
  }
  // HUD attachment inventory: logged once after the player OSD activation
  // (see GameWorld::logViewportAttachments call from the activate path).
  if (runlog::active()) {
    static bool attachmentsLogged = true;
    if (!attachmentsLogged) {
      attachmentsLogged = true;
      for (auto *node : result) {
        if (!node->viewport || !node->viewport->renderAttachments)
          continue;
        for (auto *entry : node->viewport->renderAttachments->active()) {
          auto *draw = nodeFor(entry);
          runlog::event(
              runlog::EventKind::Note,
              "viewport 0x%x attachment: node=%p type=%u loaded=%d id=0x%x",
              node->instanceId, (const void *)draw,
              draw ? static_cast<unsigned>(draw->type) : 0u,
              draw && draw->loaded ? 1 : 0, draw ? draw->instanceId : 0u);
        }
      }
    }
  }
  return result;
}

std::optional<int32_t> PlayerInputState::add(int32_t axis, int32_t device,
                                             int32_t physicalAxis, float sample,
                                             const std::array<float, 4> &range,
                                             int32_t deviceType) {
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

namespace {
// Tail of stock FUN_0054d830: the axis record's +0x20 word is a response
// curve exponent. At or below 1 the smoothed value is returned unchanged;
// above 1 (clamped to 5) the magnitude is raised by repeated multiplication
// with the fourth root of itself, once per quarter step above 1.125.
float axisResponseCurve(float value, float curve) {
  if (curve <= 1.0f)
    return value;
  float magnitude = value;
  float sign = 1.0f;
  if (!(0.0f <= magnitude)) {
    magnitude = -magnitude;
    sign = -1.0f;
  }
  if (magnitude < 1e-05f)
    return 0.0f;
  if (curve < 1.0f)
    return magnitude;
  if (5.0f < curve)
    curve = 5.0f;
  const float root = std::sqrt(magnitude);
  for (; 1.125f < curve; curve -= 0.25f)
    magnitude = static_cast<float>(std::sqrt(root)) * magnitude;
  return sign < 0 ? -magnitude : magnitude;
}
} // namespace

std::optional<float> PlayerInputState::sampleAxis(int32_t axis) {
  if (axis <= 0 || static_cast<size_t>(axis) >= axes_.size() || !sampleInput ||
      !inputType || !std::isfinite(deltaMilliseconds_) ||
      deltaMilliseconds_ < 0.0f) {
    lastBlock_ = axis <= 0 || static_cast<size_t>(axis) >= axes_.size()
                     ? "axis out of range"
                 : !sampleInput ? "no sampleInput reader"
                 : !inputType   ? "no inputType reader"
                                : "invalid frame time";
    return std::nullopt;
  }
  auto &state = axes_[static_cast<size_t>(axis)];
  if (state.remaining[1] || state.remaining[2] || state.deviceType != 0) {
    lastBlock_ = state.remaining[1] || state.remaining[2]
                     ? "axis remaining counters"
                     : "axis deviceType != 0";
    return std::nullopt;
  }
  if (state.stamp == inputFrame_)
    return axisResponseCurve(state.value, state.factors[4]);
  float sum = 0.0f;
  for (auto &mapping : mappings_) {
    if (mapping.axis != axis)
      continue;
    const auto sample = sampleInput(mapping.device, mapping.physicalAxis);
    const auto kind = inputType(mapping.device, mapping.physicalAxis);
    if (!sample || !std::isfinite(*sample) || !kind || *kind != 0) {
      lastBlock_ = !sample ? "device sample unavailable"
                   : !kind ? "device type unavailable"
                           : "mapping is not a digital/keyboard axis";
      return std::nullopt;
    }
    const float lo = mapping.range[0], hi = mapping.range[1];
    if (!std::all_of(mapping.range.begin(), mapping.range.end(),
                     [](float value) { return std::isfinite(value); }) ||
        lo == hi) {
      lastBlock_ = "invalid mapping range";
      return std::nullopt;
    }
    if ((lo <= *sample && *sample <= hi) || (hi <= *sample && *sample <= lo))
      sum +=
          (*sample - lo) * (mapping.range[3] - mapping.range[2]) / (hi - lo) +
          mapping.range[2];
    mapping.sample = *sample;
  }
  sum = std::clamp(sum, -1.0f, 1.0f);
  float step = state.factors[3] * deltaMilliseconds_;
  if (state.threshold <= sum || sum <= -state.threshold)
    step *= sum * state.value >= 0.0f ? state.factors[2] : state.factors[1];
  else
    step *= state.factors[0];
  if (!std::isfinite(step) || step < 0.0f) {
    lastBlock_ = "invalid smoothing step";
    return std::nullopt;
  }
  step = std::min(step, 1.0f);
  const float difference = sum - state.value;
  state.value = std::clamp(state.value + std::clamp(difference, -step, step),
                           -1.0f, 1.0f);
  state.stamp = inputFrame_;
  return axisResponseCurve(state.value, state.factors[4]);
}

std::optional<float> GameWorld::playerControlValue(LinkOwner *owner,
                                                   int32_t axis) {
  auto *node = nodeFor(owner);
  if (!node || !node->loaded || node->nativeDevice != "player" ||
      !node->player || !node->playerInput ||
      node->player->scriptedControlOverride || axis <= 0)
    return std::nullopt;
  if (axis >= 76)
    return 0.0f;
  return node->playerInput->sampleAxis(axis);
}

GameWorld::GameWorld() = default;

GameWorld::~GameWorld() {
  physicsWorld_.bodies.clear();
  objectPhysics_.clear();
  carChildBodies_.clear();
  carBodies_.clear();
  dynamicBodies_.clear();
  for (auto &link : deferredDestroy_)
    setLinkOwner(*link, nullptr);
  for (auto &link : collisionUpdates_)
    setLinkOwner(*link, nullptr);
  for (auto &link : audioListeners_)
    setLinkOwner(*link, nullptr);
  for (auto &entry : nodes_) {
    for (const auto &observer : nodeDestroyObservers_)
      observer.second(*entry.second);
    if (entry.second->player) {
      setLinkOwner(entry.second->player->font, nullptr);
      setLinkOwner(entry.second->player->animationOverlay, nullptr);
      for (auto &link : entry.second->player->debugText)
        setLinkOwner(link, nullptr);
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
  if (nodeFor(&node) != &node)
    return false;
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
                               [](const auto &entry) { return !entry->owner; }),
                entries.end());
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
  if (type != 1 && type != 2 && type != 3 && type != 5 && type != 7 &&
      type != 12 && type != 13 && type != 14 && type != 18)
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

std::optional<ResourceTreeHandle>
GameWorld::attachExternal(ResourceTreeHandle parent, LinkOwner &owner) {
  const auto handle = tree_.add(parent);
  if (handle)
    externalNodes_.emplace(handle->id, &owner);
  return handle;
}

bool GameWorld::detachExternal(ResourceTreeHandle handle) {
  return externalNodes_.count(handle.id) && destroySubtree(handle);
}

WorldNode *
GameWorld::addChildUnderExternal(ResourceTreeHandle parent, LinkOwner &owner,
                                 uint32_t parentType, LinkOwner *parentSector,
                                 const std::string &name, uint32_t flags) {
  return addTypedChildUnderExternal(parent, owner, parentType, parentSector, 1,
                                    name, flags);
}

WorldNode *
GameWorld::addTypedChildUnderExternal(ResourceTreeHandle parent,
                                      LinkOwner &owner, uint32_t parentType,
                                      LinkOwner *parentSector, uint32_t type,
                                      const std::string &name, uint32_t flags) {
  if (type != 1 && type != 2 && type != 3 && type != 5 && type != 7 &&
      type != 12 && type != 13 && type != 14 && type != 18)
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

void GameWorld::noteRecentUse(LinkOwner &owner, uint32_t type, uint32_t &stamp,
                              bool &queued) {
  if (stamp == frameCounter())
    return;
  stamp = frameCounter();
  if (!queued) {
    forgetLoadState(owner);
    recentlyUsed_[type].push_back(&owner);
    queued = true;
  }
}
std::vector<LinkOwner *> GameWorld::recentlyUsed(uint32_t type) const {
  const auto found = recentlyUsed_.find(type);
  return found == recentlyUsed_.end() ? std::vector<LinkOwner *>{}
                                      : found->second;
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

uint64_t
GameWorld::addNodeDestroyObserver(std::function<void(LinkOwner &)> observer) {
  const uint64_t token = nextNodeDestroyObserver_++;
  nodeDestroyObservers_.emplace(token, std::move(observer));
  return token;
}

void GameWorld::removeNodeDestroyObserver(uint64_t token) {
  nodeDestroyObservers_.erase(token);
}

bool GameWorld::registerStaticBox(WorldNode &node,
                                  const std::array<float, 3> &halfExtents) {
  if (nodeFor(&node) != &node || node.type != 2 || node.physicsBox)
    return false;
  for (float extent : halfExtents)
    if (!std::isfinite(extent) || extent < 0)
      return false;
  PhysicsBox box;
  box.halfExtents = halfExtents;
  box.radius = std::sqrt(halfExtents[0] * halfExtents[0] +
                         halfExtents[1] * halfExtents[1] +
                         halfExtents[2] * halfExtents[2]) +
               0.05f;
  if (!std::isfinite(box.radius))
    return false;
  node.physicsBox = box;
  boxGeometries_.push_back(&node);
  boxBodies_.push_back(&node);
  return true;
}

bool GameWorld::registerObjectPhysics(LinkOwner &owner,
                                      object::InstanceHandle physics) {
  auto *node = nodeFor(&owner);
  bool attached = node && node->type == 1;
  if (!node)
    for (const auto &entry : externalNodes_)
      attached |= entry.second == &owner;
  if (!attached || objectPhysics_.count(&owner) || !physics.valid() ||
      !physics.root() || !physics.root()->proto ||
      physics.root()->current > 1 || physics.bodies().empty() ||
      physics.bodies().front() != physics.root())
    return false;
  const auto &bodies = physics.bodies();
  for (auto it = bodies.begin(); it != bodies.end(); ++it) {
    const auto *body = *it;
    if (!body || !body->proto || body->current > 1 ||
        std::find(bodies.begin(), it, body) != it ||
        std::find(physicsWorld_.bodies.begin(), physicsWorld_.bodies.end(),
                  body) != physicsWorld_.bodies.end())
      return false;
  }
  physicsWorld_.bodies.reserve(physicsWorld_.bodies.size() + bodies.size());
  auto inserted = objectPhysics_.emplace(&owner, std::move(physics));
  for (auto *body : inserted.first->second.bodies())
    physicsWorld_.bodies.push_back(body);
  return true;
}

bool GameWorld::unregisterObjectPhysics(LinkOwner &owner) {
  const auto found = objectPhysics_.find(&owner);
  if (found == objectPhysics_.end())
    return false;
  for (const auto *body : found->second.bodies())
    physicsWorld_.bodies.erase(std::remove(physicsWorld_.bodies.begin(),
                                           physicsWorld_.bodies.end(), body),
                               physicsWorld_.bodies.end());
  objectPhysics_.erase(found);
  return true;
}

bool GameWorld::registerDynamicBody(
    WorldNode &node, std::shared_ptr<physics::Body> body,
    std::shared_ptr<const physics::BodyProto> prototype) {
  if (nodeFor(&node) != &node || node.type != 2 || !body || !prototype ||
      body->proto != prototype.get() || dynamicBodies_.count(&node) ||
      carChildBodies_.count(&node) ||
      std::find(physicsWorld_.bodies.begin(), physicsWorld_.bodies.end(),
                body.get()) != physicsWorld_.bodies.end())
    return false;
  auto *raw = body.get();
  dynamicBodies_.emplace(
      &node, std::make_shared<DynamicBodyOwnership>(
                 DynamicBodyOwnership{std::move(prototype), std::move(body)}));
  physicsWorld_.bodies.push_back(raw);
  return true;
}

WorldNode *GameWorld::registerCarPhysics(WorldNode &node,
                                         std::shared_ptr<CarPhysics> car,
                                         std::string &error) {
  error.clear();
  if (nodeFor(&node) != &node || node.type != 1 || !car || !car->root ||
      !car->part || !car->part->root || car->part->rootBodyIndex < 0 ||
      car->teardownOrder.empty() ||
      car->teardownOrder.back() != car->root.get() ||
      std::find(physicsWorld_.bodies.begin(), physicsWorld_.bodies.end(),
                car->root.get()) != physicsWorld_.bodies.end()) {
    error = "car physics: invalid car or already registered";
    return nullptr;
  }
  for (const auto &entry : car->bodyObservations)
    if (!entry.second || entry.second->resource.owner) {
      error = "car physics: body resource already linked";
      return nullptr;
    }
  std::vector<WorldNode *> created;
  auto makeNode = [&](physics::Body *body, const char *name,
                      bool root) -> WorldNode * {
    const auto found = car->bodyObservations.find(body);
    if (!body || !body->proto || body->current > 1 ||
        found == car->bodyObservations.end() || found->second->body != body ||
        !std::isfinite(body->proto->shape.boundRadius) ||
        body->proto->shape.boundRadius < 0)
      return nullptr;
    const auto &pos = body->state[body->current].pos;
    if (!std::all_of(pos.begin(), pos.end(),
                     [](float v) { return std::isfinite(v); }))
      return nullptr;
    auto *child = addTypedChild(node, 2, name, 0x201);
    if (!child)
      return nullptr;
    created.push_back(child);
    child->bounds.mode = 0;
    child->bounds.values = {pos[0],
                            pos[1],
                            pos[2],
                            body->proto->shape.boundRadius,
                            body->proto->shape.boundRadius,
                            body->proto->shape.boundRadius};
    auto owner = std::shared_ptr<physics::Body>(car, body);
    if (root) {
      auto prototype =
          std::shared_ptr<const physics::BodyProto>(car, &car->rootPrototype);
      if (!registerDynamicBody(*child, std::move(owner), std::move(prototype)))
        return nullptr;
      carBodies_.emplace(child, car);
    } else {
      carChildBodies_.emplace(child, std::move(owner));
    }
    setLinkOwner(found->second->resource, child);
    noteInstanceCreated(*child);
    markLoaded(*child);
    child->loadPriority = 1.0f;
    return child;
  };
  auto fail = [&]() -> WorldNode * {
    for (auto it = created.rbegin(); it != created.rend(); ++it)
      destroy(**it);
    error = "car physics: body resource creation failed";
    return nullptr;
  };
  if (!makeNode(car->part->root.get(), "part_comp", false))
    return fail();
  for (auto &child : car->part->children) {
    if (static_cast<int>(child.bodyIndex) == car->part->rootBodyIndex &&
        !makeNode(child.body.get(), "bbone", false))
      return fail();
    if (static_cast<int>(child.bodyIndex) == car->part->steeringBodyIndex &&
        !makeNode(child.body.get(), "steer", false))
      return fail();
  }
  if (car->part->clickBody &&
      !makeNode(car->part->clickBody,
                car->part->argument == 0 ? "cbone" : "click", false))
    return fail();
  auto *root = makeNode(car->root.get(), "car_root", true);
  if (!root)
    return fail();
  for (const auto &wheel : car->wheels) {
    if (!wheel)
      return fail();
    if (wheel->boneBody && !makeNode(wheel->boneBody.get(), "wheelbone", false))
      return fail();
    if (wheel->config.hasWheel &&
        (!makeNode(wheel->hubBody.get(), "hub", false) ||
         !makeNode(wheel->armBody.get(), "a_arm", false)))
      return fail();
  }
  return root;
}

bool GameWorld::unregisterDynamicBody(WorldNode &node) {
  const auto found = dynamicBodies_.find(&node);
  if (found == dynamicBodies_.end())
    return false;
  auto *body = found->second->body.get();
  physicsWorld_.bodies.erase(std::remove(physicsWorld_.bodies.begin(),
                                         physicsWorld_.bodies.end(), body),
                             physicsWorld_.bodies.end());
  carBodies_.erase(&node);
  dynamicBodies_.erase(found);
  return true;
}

std::weak_ptr<physics::Body> GameWorld::dynamicBody(WorldNode &node) const {
  const auto found = dynamicBodies_.find(&node);
  if (found != dynamicBodies_.end())
    return std::shared_ptr<physics::Body>(found->second,
                                          found->second->body.get());
  const auto child = carChildBodies_.find(&node);
  if (child != carChildBodies_.end())
    return child->second;
  return {};
}

void GameWorld::registerAudioListener(WorldNode &node) {
  for (size_t i = audioListeners_.size(); i-- > 0;) {
    if (audioListeners_[i]->owner)
      continue;
    if (i + 1 != audioListeners_.size())
      audioListeners_[i] = std::move(audioListeners_.back());
    audioListeners_.pop_back();
  }
  if (nodeFor(&node) != &node || audioListeners_.size() >= 2)
    return;
  for (const auto &link : audioListeners_)
    if (link->owner == &node)
      return;
  auto link = std::make_unique<OwnerLink>();
  setLinkOwner(*link, &node);
  audioListeners_.push_back(std::move(link));
}

void GameWorld::unregisterAudioListener(WorldNode &node) {
  for (size_t i = 0; i < audioListeners_.size(); ++i) {
    if (audioListeners_[i]->owner != &node)
      continue;
    setLinkOwner(*audioListeners_[i], nullptr);
    if (i + 1 != audioListeners_.size())
      audioListeners_[i] = std::move(audioListeners_.back());
    audioListeners_.pop_back();
    return;
  }
}

bool GameWorld::audioListener(physics::Vec3 &position, physics::Vec3 &forward,
                              physics::Vec3 &up) {
  for (size_t i = audioListeners_.size(); i-- > 0;) {
    if (audioListeners_[i]->owner)
      continue;
    if (i + 1 != audioListeners_.size())
      audioListeners_[i] = std::move(audioListeners_.back());
    audioListeners_.pop_back();
  }
  position = {0, 0, 0};
  forward = {0, 0, -1};
  up = {0, -1, 0};
  if (audioListeners_.empty())
    return true;
  auto *node = nodeFor(audioListeners_[0]->owner);
  auto body = node ? dynamicBody(*node).lock() : nullptr;
  if (!body || body->current > 1)
    return false;
  const auto &state = body->state[body->current];
  position = state.pos;
  const auto &r = state.rot;
  const physics::Vec3 column2{r[2], r[6], r[10]};
  const physics::Vec3 column1{r[1], r[5], r[9]};
  auto length2 = [](const physics::Vec3 &v) {
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
  };
  if (length2(column2) >= 0.01f)
    forward = {-column2[0], -column2[1], -column2[2]};
  if (length2(column1) >= 0.01f)
    up = {-column1[0], -column1[1], -column1[2]};
  return true;
}

namespace {
struct DirectCameraFollow {
  CameraDeviceResources *device = nullptr;
  std::shared_ptr<physics::Body> camera;
  std::shared_ptr<physics::Body> look;
  std::shared_ptr<physics::Body> move;
};

// Stock 00438b60 applies the direct look torque before it updates field of
// view, and this device has no plane or angle fields.
bool bindDirectCameraFollow(GameWorld &world,
                            const CameraDeviceResources &device,
                            DirectCameraFollow &out,
                            const char **block = nullptr) {
  const auto fail = [&](const char *code) {
    if (block)
      *block = code;
    return false;
  };
  if (block)
    *block = nullptr;
  if (!device.lookTarget.owner)
    return fail("look");
  if (!device.moveTarget.owner)
    return fail("move");
  auto *physicsNode = device.physicsNode();
  if (!physicsNode)
    return fail("phys");
  // A follow target is a dynamic body, or a ground's static body (the garage
  // camera looks at and follows its map resource, id 5's ground body).
  const auto targetBody = [&](LinkOwner *owner, std::shared_ptr<physics::Body> &body) {
    if (auto *node = world.nodeFor(owner)) {
      body = world.dynamicBody(*node).lock();
      if (body)
        return true;
      // A car's type-1 node owns no body itself: its physics root is the
      // type-2 child created with it. [INFERENCE] stock follows the car through
      // that root (FUN_00545fc0 only stores the node link).
      if (node->carInstance && node->carInstance->physicsRoot.owner) {
        if (auto *root = world.nodeFor(node->carInstance->physicsRoot.owner)) {
          body = world.dynamicBody(*root).lock();
          if (body)
            return true;
        }
      }
    }
    for (auto *resources : world.resourceWorlds()) {
      const auto resolved = resources->resolve(owner);
      if (resolved.packNode && resolved.packNode->groundFactory &&
          resolved.packNode->groundFactory->preparation &&
          resolved.packNode->groundFactory->preparation->physics) {
        const auto &ground = resolved.packNode->groundFactory->preparation;
        body = std::shared_ptr<physics::Body>(ground->physics,
                                              &ground->physics->body);
        return true;
      }
    }
    return false;
  };
  std::shared_ptr<physics::Body> look, move;
  if (!targetBody(device.lookTarget.owner, look))
    return fail("lnod");
  if (!targetBody(device.moveTarget.owner, move))
    return fail("mnod");
  auto camera = world.dynamicBody(*physicsNode).lock();
  if (!camera)
    return fail("cbod");
  if (!look)
    return fail("lbod");
  if (!move)
    return fail("mbod");
  if (!camera->proto)
    return fail("prot");
  if (camera->current > 1)
    return fail("curr");
  if (camera->attachment)
    return fail("attc");
  if (camera->flags & physics::kBodyChild)
    return fail("chld");
  if (camera->flags & physics::kBodyHasChildren)
    return fail("kids");
  if (camera->flags & physics::kBodyBit10)
    return fail("bitx");
  if ((camera->flags & physics::kBodyActive) == 0)
    return fail("idle");
  if (camera->proto->flags & physics::kProtoStatic)
    return fail("stat");
  if (camera->proto->invMass <= 0.0f)
    return fail("mass");
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
// Stock 00438b60 wakes a sleeping camera body (FUN_004a7990) whenever it has
// to apply a follow force. This port wakes it when the force or torque the
// follow would apply is nonzero, before the step validates its roots.
void wakeFollowCameraIfForced(const CameraDeviceResources &device,
                              physics::Body &camera, const physics::Body &look,
                              const physics::Body &move) {
  if (!(camera.flags & physics::kBodyAsleep) || camera.current > 1)
    return;
  CameraFollowForceInput input;
  input.otherBranchesInactive = true;
  input.command = device.commands;
  input.fieldOfView = device.fieldOfView;
  input.maxForce = device.maxForce;
  input.camera = camera.state[camera.current];
  input.lookTarget = look.state[look.current];
  input.moveTarget = move.state[move.current];
  CameraFollowSmoothing smoothing = device.followSmoothing;
  const auto applied = cameraFollowForce(input, smoothing);
  if (!applied)
    return;
  float magnitude = 0;
  for (size_t i = 0; i < 3; ++i)
    magnitude += applied->force[i] * applied->force[i] +
                 applied->torque[i] * applied->torque[i];
  if (magnitude > 1e-8f)
    physics::wakeDetachedCompound({&camera});
}
} // namespace

bool GameWorld::directCameraFollowReady(const CameraDeviceResources &camera,
                                        const char **block) {
  DirectCameraFollow follow;
  return bindDirectCameraFollow(*this, camera, follow, block);
}

bool GameWorld::armedCameraFollowCount(int &ready, std::string &error) {
  ready = 0;
  for (const auto &entry : nodes_) {
    const auto *device = entry.second->cameraDevice.get();
    if (!device || !device->lookTarget.owner || !device->moveTarget.owner)
      continue;
    if (!directCameraFollowReady(*device)) {
      error = "empty physics: camera follow is unsupported";
      return false;
    }
    ++ready;
  }
  return true;
}

bool GameWorld::integrateArmedCameraFollows(float seconds, bool &otherDynamic,
                                            std::string &error) {
  std::vector<DirectCameraFollow> follows;
  unsigned devicesWithTargets = 0;
  for (const auto &entry : nodes_) {
    auto *device = entry.second->cameraDevice.get();
    if (!device || !device->lookTarget.owner || !device->moveTarget.owner)
      continue;
    ++devicesWithTargets;
    DirectCameraFollow follow;
    const char *bindBlock = nullptr;
    if (!bindDirectCameraFollow(*this, *device, follow, &bindBlock)) {
      static bool bindFailLogged = false;
      if (!bindFailLogged) {
        bindFailLogged = true;
        runlog::event(runlog::EventKind::Note,
                      "camera follow bind failed: node=%u block=%s",
                      entry.second->instanceId, bindBlock ? bindBlock : "?");
      }
      error =
          std::string("empty physics: camera follow is unsupported (bind ") +
          (bindBlock ? bindBlock : "?") + ")";
      return false;
    }
    follows.push_back(std::move(follow));
  }
  {
    static double lastDevLog = -1e9;
    const double now = runlog::monotonicSeconds();
    if (now - lastDevLog >= 5.0) {
      lastDevLog = now;
      runlog::event(runlog::EventKind::Note,
                    "camera follow devices: withTargets=%u bound=%zu",
                    devicesWithTargets, follows.size());
    }
  }
  for (const auto &follow : follows)
    wakeFollowCameraIfForced(*follow.device, *follow.camera, *follow.look,
                             *follow.move);
  {
    static double lastCamLog = -1e9;
    const double now = runlog::monotonicSeconds();
    if (!follows.empty() && now - lastCamLog >= 5.0) {
      lastCamLog = now;
      for (const auto &fp : follows) {
      const auto &f = fp;
      const auto &cp = f.camera->state[f.camera->current].pos;
      const auto &mp = f.move->state[f.move->current].pos;
      const auto &lp = f.look->state[f.look->current].pos;
      const auto &c = f.device->commands;
      runlog::event(runlog::EventKind::Note,
                    "camera follow: cam=%.2f,%.2f,%.2f move=%.2f,%.2f,%.2f "
                    "look=%.2f,%.2f,%.2f dist=%.2f moveOffset=%.2f,%.2f,%.2f "
                    "lookOffset=%.2f,%.2f,%.2f fov=%.1f",
                    cp[0], cp[1], cp[2], mp[0], mp[1], mp[2], lp[0], lp[1],
                    lp[2], c.moveDistance, c.moveOffset[0], c.moveOffset[1],
                    c.moveOffset[2], c.lookOffset[0], c.lookOffset[1],
                    c.lookOffset[2], f.device->fieldOfView);
      }
    }
  }
  otherDynamic = false;
  for (const auto &entry : dynamicBodies_) {
    const auto *body = entry.second->body.get();
    const bool camera = std::any_of(follows.begin(), follows.end(),
                                    [&](const DirectCameraFollow &follow) {
                                      return follow.camera.get() == body;
                                    });
    if (!camera)
      otherDynamic = true;
  }
  if (!std::isfinite(physicsWorld_.pending) || physicsWorld_.pending < 0.0f ||
      !std::isfinite(physicsWorld_.time) ||
      !std::isfinite(physicsWorld_.pending + seconds)) {
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
    // A sleeping camera is skipped by every stock pass.
    if (follow.camera->flags & physics::kBodyAsleep)
      continue;
    if (!physics::accumulateWorldForces(physicsWorld_.params, *follow.camera)) {
      rollback();
      error = "empty physics: camera follow is unsupported (world forces)";
      return false;
    }
  }
  for (const auto &follow : follows) {
    if (follow.camera->flags & physics::kBodyAsleep)
      continue;
    CameraFollowForceInput input;
    input.otherBranchesInactive = true;
    input.command = follow.device->commands;
    input.fieldOfView = follow.device->fieldOfView;
    input.maxForce = follow.device->maxForce;
    input.camera = follow.camera->state[follow.camera->current];
    input.lookTarget = follow.look->state[follow.look->current];
    input.moveTarget = follow.move->state[follow.move->current];
    const char *forceBlock = nullptr;
    const auto applied =
        cameraFollowForce(input, follow.device->followSmoothing, &forceBlock);
    if (!applied) {
      rollback();
      error =
          std::string("empty physics: camera follow is unsupported (force ") +
          (forceBlock ? forceBlock : "?") + ")";
      return false;
    }
    follow.device->followSmoothing = applied->smoothing;
    physics::addForce(*follow.camera, applied->force, nullptr);
    auto &state = follow.camera->state[follow.camera->current];
    for (size_t i = 0; i < 3; ++i)
      state.torque[i] += applied->torque[i];
  }
  for (const auto &follow : follows)
    if (!(follow.camera->flags & physics::kBodyAsleep) &&
        physics::stepBody(*follow.camera, total))
      ++physicsWorld_.stepsDone;
  for (const auto &follow : follows)
    if (!(follow.camera->flags & physics::kBodyAsleep) &&
        physics::commit(*follow.camera) == physics::CommitResult::WentToSleep)
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
  if (!carBodies_.empty() || !carChildBodies_.empty()) {
    error = "camera follow: registered car physics cannot be advanced by "
            "camera-only step";
    return false;
  }
  if (!objectPhysics_.empty()) {
    error = "camera follow: registered object physics is unsupported";
    return false;
  }
  if (!collisionUpdates_.empty()) {
    error = "camera follow: collision queue unsupported";
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
  if (!integrateArmedCameraFollows(seconds, otherDynamic, error))
    return false;
  (void)otherDynamic;
  error.clear();
  return true;
}

std::vector<WorldNode *> GameWorld::carNodes() {
  std::vector<WorldNode *> result;
  for (const auto &entry : nodes_) {
    const auto &car = entry.second->carInstance;
    if (car && car->isCar && car->carPhysics && car->carPhysics->root &&
        std::any_of(carBodies_.begin(), carBodies_.end(),
                    [&](const auto &c) { return c.second == car->carPhysics; }))
      result.push_back(entry.second.get());
  }
  return result;
}

bool GameWorld::stepRegisteredCars(float seconds, const LiveCarStepHooks &hooks,
                                   std::string &error) {
  if (!std::isfinite(seconds) || seconds <= 0.0f) {
    error = "car step: invalid step";
    return false;
  }
  // Scene objects: static, inactive and sleeping roots are skipped by stock
  // (004a3f90/004a3bc0). Awake dynamic objects would need the object step
  // (collision with the car, toppling, sleep); until that exists they stay
  // registered but are not stepped, so they hold their placed pose and do not
  // collide. This is a stated gap, noted once per count change.
  size_t frozenObjects = 0;
  for (const auto &entry : objectPhysics_) {
    auto *objectRoot = entry.second.root();
    if (objectRoot && objectRoot->proto &&
        !(objectRoot->proto->flags & physics::kProtoStatic) &&
        (objectRoot->flags & physics::kBodyActive) &&
        !(objectRoot->flags & physics::kBodyAsleep)) {
      // The gap stays explicit until object force/contact/wake is modelled.
      ++frozenObjects;
    }
  }
  static size_t reportedFrozen = static_cast<size_t>(-1);
  if (frozenObjects != reportedFrozen) {
    reportedFrozen = frozenObjects;
    runlog::event(runlog::EventKind::Note,
                  "car step: %zu awake dynamic scene objects are frozen "
                  "(object physics step unsupported)",
                  frozenObjects);
  }
  std::vector<DirectCameraFollow> follows;
  for (const auto &entry : nodes_) {
    auto *device = entry.second->cameraDevice.get();
    if (!device || !device->lookTarget.owner || !device->moveTarget.owner)
      continue;
    DirectCameraFollow follow;
    const char *block = nullptr;
    if (!bindDirectCameraFollow(*this, *device, follow, &block)) {
      error = std::string("car step: camera follow is unsupported (") +
              (block ? block : "?") + ")";
      return false;
    }
    follows.push_back(std::move(follow));
  }
  for (const auto &follow : follows)
    wakeFollowCameraIfForced(*follow.device, *follow.camera, *follow.look,
                             *follow.move);
  // Roots in registration order, as stock keeps them: a car is its body tree
  // (root first, every child after its parent), a camera is a lone body with
  // its follow callback.
  std::vector<physics::BodyTree> roots;
  physics::MultiRootWork work;
  bool carAsleep = false;
  size_t awakeCars = 0;
  for (auto *body : physicsWorld_.bodies) {
    // Static object bodies (roots and children) stay registered but are not
    // stepped roots.
    bool staticObjectBody = false;
    for (const auto &entry : objectPhysics_)
      for (const auto *objectBody : entry.second.bodies())
        if (objectBody == body)
          staticObjectBody = true;
    if (staticObjectBody)
      continue;
    std::shared_ptr<CarPhysics> car;
    for (const auto &entry : carBodies_)
      if (entry.second && entry.second->root.get() == body) {
        car = entry.second;
        break;
      }
    if (car) {
      physics::BodyTree tree;
      std::function<void(physics::Body *)> add = [&](physics::Body *b) {
        tree.push_back(b);
        const auto children = car->bodyChildren.find(b);
        if (children != car->bodyChildren.end())
          for (auto *child : children->second)
            add(child);
      };
      add(body);
      static std::map<const CarPhysics *, bool> lastAsleep;
      const bool asleepBefore = tree[0]->flags & physics::kBodyAsleep;
      if (hooks.wakeCars)
        physics::wakeDetachedCompound(tree);
      {
        const bool asleepNow = tree[0]->flags & physics::kBodyAsleep;
        const auto seen = lastAsleep.find(car.get());
        if (seen == lastAsleep.end() || seen->second != asleepNow) {
          runlog::event(runlog::EventKind::Note,
                        "car sleep transition: car=%p %s -> %s cause=%s "
                        "pos=%.2f,%.2f,%.2f",
                        static_cast<const void *>(car.get()),
                        seen == lastAsleep.end()
                            ? "new"
                            : (seen->second ? "asleep" : "awake"),
                        asleepNow ? "asleep" : "awake",
                        hooks.wakeCars ? (asleepBefore ? "controls-wake"
                                                       : "controls-held")
                                       : "reset/suspend/contact",
                        double(body->state[body->current].pos[0]),
                        double(body->state[body->current].pos[1]),
                        double(body->state[body->current].pos[2]));
          lastAsleep[car.get()] = asleepNow;
        }
      }
      // A sleeping tree is skipped by every stock pass (004a3f90, 004a3bc0).
      // Its suspensions and contacts then have no registered wheel bodies, so
      // the car's per-frame work is not run while it sleeps.
      if (tree[0]->flags & physics::kBodyAsleep) {
        carAsleep = true;
        continue;
      }
      ++awakeCars;
      roots.push_back(std::move(tree));
      continue;
    }
    const auto follow = std::find_if(
        follows.begin(), follows.end(),
        [&](const DirectCameraFollow &f) { return f.camera.get() == body; });
    if (follow == follows.end()) {
      // A camera whose look and move targets are not both set (the dealer
      // camera before its first look/move) has no follow callback to run;
      // its body stays registered and unstepped, like the camera-only step.
      bool untargetedCamera = false;
      for (const auto &entry : nodes_) {
        const auto *device = entry.second->cameraDevice.get();
        if (!device || !device->physics.owner)
          continue;
        const auto *owned = nodeFor(device->physics.owner);
        const auto found = owned ? dynamicBodies_.find(const_cast<WorldNode *>(owned))
                                 : dynamicBodies_.end();
        if (found != dynamicBodies_.end() && found->second->body.get() == body)
          untargetedCamera = true;
      }
      if (untargetedCamera)
        continue;
      error = "car step: dynamic body is neither a car nor a camera follow";
      return false;
    }
    // A sleeping camera is skipped by every stock pass; it is woken above
    // when a follow force is needed.
    if (body->flags & physics::kBodyAsleep)
      continue;
    roots.push_back({body});
    CameraFollowStep callback;
    callback.camera = follow->camera.get();
    callback.look = follow->look.get();
    callback.move = follow->move.get();
    callback.input.otherBranchesInactive = true;
    callback.input.command = follow->device->commands;
    callback.input.fieldOfView = follow->device->fieldOfView;
    callback.input.maxForce = follow->device->maxForce;
    callback.smoothing = &follow->device->followSmoothing;
    work.cameraCallbacks.push_back(callback);
  }
  if (carAsleep && awakeCars == 0) {
    // Only camera follows advance: the sleeping cars' wheel and contact work
    // is not run, and a follow that targets a car reads its resting pose.
    static bool reported = false;
    if (!reported) {
      reported = true;
      runlog::event(runlog::EventKind::Note,
                    "car step: every car tree is asleep; only camera follows "
                    "advance (stock skips sleeping trees in every pass)");
    }
    bool otherDynamic = false;
    return integrateArmedCameraFollows(seconds, otherDynamic, error);
  }
  // Stock registers wheel constraints per awake body only: sleeping cars'
  // suspensions are left out when awake cars share the step.
  std::vector<physics::SuspensionUpdate *> awakeSuspensions;
  for (auto *suspension : hooks.suspensions)
    if (!(suspension->wheelBody &&
          (suspension->wheelBody->flags & physics::kBodyAsleep)))
      awakeSuspensions.push_back(suspension);
  work.contacts = static_cast<bool>(hooks.gatherContacts);
  work.wheelConstraints = !awakeSuspensions.empty();
  work.gatherContacts = hooks.gatherContacts;
  work.finishContacts = hooks.finishContacts;
  work.onePendingContactPerRoot = hooks.onePendingContactPerRoot;
  work.beforePrediction = hooks.beforePrediction;
  work.suspensions = awakeSuspensions;
  // stepMultiRootWorld registers exactly the stepped roots; the world list
  // also holds the static object bodies.
  physics::World stepWorld = physicsWorld_;
  stepWorld.bodies.clear();
  for (const auto &tree : roots)
    stepWorld.bodies.push_back(tree[0]);
  const auto result =
      physics::stepMultiRootWorld(stepWorld, roots, seconds, work, error);
  physicsWorld_.time = stepWorld.time;
  physicsWorld_.pending = stepWorld.pending;
  physicsWorld_.lastStep = stepWorld.lastStep;
  physicsWorld_.stepsDone = stepWorld.stepsDone;
  if (result == physics::FreeBodyStepResult::Unsupported) {
    if (error.empty())
      error = "car step: physics step is unsupported";
    return false;
  }
  if (result == physics::FreeBodyStepResult::Advanced) {
    // The car body resource nodes carry sphere bounds at the body position
    // (registerCarPhysics); keep them on the committed pose.
    for (const auto &entry : carBodies_)
      if (entry.second && entry.second->root && entry.second->root->current < 2)
        for (size_t axis = 0; axis < 3; ++axis)
          entry.first->bounds.values[axis] =
              entry.second->root->state[entry.second->root->current].pos[axis];
    for (const auto &entry : carChildBodies_)
      if (entry.second && entry.second->current < 2)
        for (size_t axis = 0; axis < 3; ++axis)
          entry.first->bounds.values[axis] =
              entry.second->state[entry.second->current].pos[axis];
  }
  error.clear();
  return true;
}

bool GameWorld::stepEmptyPhysics(float seconds, std::string &error) {
  if (!std::isfinite(seconds) || seconds <= 0.0f) {
    error = "empty physics: invalid step";
    return false;
  }
  if (!carBodies_.empty() || !carChildBodies_.empty()) {
    error = "empty physics: registered car physics is unsupported";
    return false;
  }
  int readyFollows = 0;
  if (!armedCameraFollowCount(readyFollows, error))
    return false;
  if (readyFollows == 0 && !dynamicBodies_.empty()) {
    error = "empty physics: dynamic bodies are unsupported";
    return false;
  }
  if (!objectPhysics_.empty()) {
    error = "empty physics: registered object physics is unsupported";
    return false;
  }
  if (!loadList(9, true).empty() || !loadList(10, true).empty()) {
    error = "empty physics: physics state is unsupported";
    return false;
  }
  for (const auto *owner : loadList(2, true)) {
    const auto *node =
        const_cast<GameWorld *>(this)->nodeFor(const_cast<LinkOwner *>(owner));
    if (!node || !node->physicsBox || node->physicsBox->geometryFlags != 5 ||
        std::find(boxBodies_.begin(), boxBodies_.end(), node) ==
            boxBodies_.end() ||
        std::find(boxGeometries_.begin(), boxGeometries_.end(), node) ==
            boxGeometries_.end()) {
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
  for (const auto &hook : hooks_[static_cast<size_t>(GameHook::Kind9)])
    if (hook->owner) {
      const auto *node = nodeFor(hook->owner);
      if (!node ||
          (node->nativeDevice != "car" && node->nativeDevice != "part")) {
        error = "empty physics: physics callback is unsupported";
        return false;
      }
    }
  if (readyFollows == 0) {
    std::vector<std::pair<WorldNode *, ResourceBounds>> boundsUpdates;
    const float total = physicsWorld_.pending + seconds;
    if (std::isfinite(total) && total >= physics::k::minimumStep) {
      for (const auto &link : collisionUpdates_) {
        if (!link->owner)
          continue;
        auto *queued = nodeFor(link->owner);
        if (!queued) {
          error = "empty physics: collision queue owner unsupported";
          return false;
        }
        if (queued->flags & 0x200000u)
          continue;
        const auto descendants = childOwners(queued->handle);
        if (queued->type != 1 || queued->sectorLink.owner != queued ||
            !descendants) {
          error = "empty physics: collision hierarchy update unsupported";
          return false;
        }
        ResourceBounds bounds = queued->bounds;
        if (!descendants->empty()) {
          std::array<float, 3> lower{}, upper{};
          bool haveBounds = false;
          bool eligibleChild = false;
          for (auto *owner : *descendants) {
            auto *child = nodeFor(owner);
            if (!child || child->type != 2 || !child->physicsBox ||
                !child->loaded || child->bounds.mode > 1) {
              error = "empty physics: collision hierarchy update unsupported";
              return false;
            }
            eligibleChild |= (child->flags & 0x200000u) == 0;
            for (size_t axis = 0; axis < 3; ++axis) {
              const float center = child->bounds.values[axis];
              const float extent = child->bounds.values[axis + 3];
              const float lo = center - extent;
              const float hi = center + extent;
              if (extent < 0 || !std::isfinite(lo) || !std::isfinite(hi)) {
                error = "empty physics: collision bounds unsupported";
                return false;
              }
              if (!haveBounds || lo < lower[axis])
                lower[axis] = lo;
              if (!haveBounds || upper[axis] < hi)
                upper[axis] = hi;
            }
            haveBounds = true;
          }
          bounds.mode = 1;
          for (size_t axis = 0; axis < 3; ++axis) {
            const float extent = (upper[axis] - lower[axis]) * 0.5f;
            const float center = lower[axis] + extent;
            if (!std::isfinite(extent) || !std::isfinite(center)) {
              error = "empty physics: collision bounds unsupported";
              return false;
            }
            bounds.values[axis] = center;
            bounds.values[axis + 3] = extent;
          }
          // 004a2430 rejects large extents before checking children, and skips
          // every child marked 0x200000 without loading its physics resource.
          const float x = bounds.values[3], y = bounds.values[4],
                      z = bounds.values[5];
          const float sizeSquared = x * x + z * z + y * y;
          if (!(sizeSquared > 10000.0f) && eligibleChild) {
            error = "empty physics: collision proxy discovery unsupported";
            return false;
          }
        } else if (bounds.mode != 2) {
          error = "empty physics: collision hierarchy update unsupported";
          return false;
        }
        boundsUpdates.emplace_back(queued, bounds);
      }
    }
    const auto result = physicsWorld_.stepFreeBodies(seconds);
    if (result == physics::FreeBodyStepResult::Unsupported) {
      error = "empty physics: physics step is unsupported";
      return false;
    }
    if (result == physics::FreeBodyStepResult::Advanced) {
      for (const auto &update : boundsUpdates)
        update.first->bounds = update.second;
      for (auto &link : collisionUpdates_) {
        auto *queued = nodeFor(link->owner);
        if (queued && !(queued->flags & 0x200000u))
          queued->flags &= ~uint32_t{0x10000000};
        setLinkOwner(*link, nullptr);
      }
      collisionUpdates_.clear();
    }
    error.clear();
    return true;
  }
  if (!collisionUpdates_.empty()) {
    error = "empty physics: collision queue with camera callbacks unsupported";
    return false;
  }
  // Reject unsupported dynamic roots before advancing the camera or the clock.
  // A failed world step must not leave only the camera integrated.
  for (const auto &entry : dynamicBodies_) {
    const auto *body = entry.second->body.get();
    bool isFollowCamera = false;
    for (const auto &candidate : nodes_) {
      const auto *device = candidate.second->cameraDevice.get();
      if (!device || !device->lookTarget.owner || !device->moveTarget.owner)
        continue;
      DirectCameraFollow follow;
      if (bindDirectCameraFollow(*this, *device, follow) &&
          follow.camera.get() == body) {
        isFollowCamera = true;
        break;
      }
    }
    if (!isFollowCamera) {
      error = "empty physics: dynamic bodies are unsupported";
      return false;
    }
  }
  bool otherDynamic = false;
  if (!integrateArmedCameraFollows(seconds, otherDynamic, error))
    return false;
  if (otherDynamic) {
    error = "empty physics: dynamic bodies are unsupported";
    return false;
  }
  error.clear();
  return true;
}

size_t GameWorld::countNodeType(uint32_t type) const {
  size_t count = 0;
  for (const auto &entry : nodes_)
    if (entry.second->type == type)
      ++count;
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
      auto position = car->position;
      auto velocity = car->linearVelocity;
      if (car->carPhysics && car->carPhysics->root &&
          car->carPhysics->root->current < 2) {
        const auto &state =
            car->carPhysics->root->state[car->carPhysics->root->current];
        position = state.pos;
        velocity = state.vel;
      }
      note.x = position[0];
      note.y = position[1];
      note.z = position[2];
      const float vx = velocity[0];
      const float vy = velocity[1];
      const float vz = velocity[2];
      note.speed = std::sqrt(vx * vx + vy * vy + vz * vz);
      note.gear = car->currentGear;
      note.rpm = car->engineRpm;
    }
    ++note.count;
  }
  return note;
}

std::vector<uint32_t> GameWorld::carInstanceIds() const {
  std::vector<uint32_t> ids;
  for (const auto &entry : nodes_)
    if (entry.second->carInstance && entry.second->carInstance->isCar)
      ids.push_back(entry.second->instanceId);
  return ids;
}

bool GameWorld::hasNodeType(uint32_t type) const {
  for (const auto &entry : nodes_)
    if (entry.second->type == type)
      return true;
  return false;
}

bool GameWorld::hasNodeFlag(uint32_t flag) const {
  for (const auto &entry : nodes_)
    if ((entry.second->flags & flag) != 0)
      return true;
  return false;
}

bool GameWorld::hasLoadedNodeType(uint32_t type) const {
  for (const auto &entry : nodes_)
    if (entry.second->type == type && entry.second->loaded)
      return true;
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
  carChildBodies_.erase(&node);
  boxGeometries_.erase(
      std::remove(boxGeometries_.begin(), boxGeometries_.end(), &node),
      boxGeometries_.end());
  boxBodies_.erase(std::remove(boxBodies_.begin(), boxBodies_.end(), &node),
                   boxBodies_.end());
  uncountInstance(node.countedInstance);
  auto &recent = recentlyUsed_[node.type];
  recent.erase(std::remove(recent.begin(), recent.end(), &node), recent.end());
  node.cameraFrame.reset();
  setLinkOwner(node.cameraViewport, nullptr);
  for (const auto &observer : nodeDestroyObservers_)
    observer.second(node);
  playerInputs_.erase(
      std::remove(playerInputs_.begin(), playerInputs_.end(), &node),
      playerInputs_.end());
  if (node.player) {
    setLinkOwner(node.player->font, nullptr);
    setLinkOwner(node.player->animationOverlay, nullptr);
    for (auto &link : node.player->debugText)
      setLinkOwner(link, nullptr);
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
  unregisterObjectPhysics(node);
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
        unregisterObjectPhysics(*external->second);
        forgetLoadState(*external->second);
        for (auto &entry : recentlyUsed_) {
          auto &recent = entry.second;
          recent.erase(
              std::remove(recent.begin(), recent.end(), external->second),
              recent.end());
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
  if (nodeFor(&node) != &node ||
      (node.type != 1 && node.type != 3 && node.type != 12))
    return false;
  auto link = std::make_unique<OwnerLink>();
  setLinkOwner(*link, &node);
  deferredDestroy_.push_back(std::move(link));
  return true;
}
bool GameWorld::queueCollisionUpdate(WorldNode &node) {
  if (nodeFor(&node) != &node || node.type != 1)
    return false;
  if (node.flags & 0x10000000u)
    return true;
  auto link = std::make_unique<OwnerLink>();
  setLinkOwner(*link, &node);
  collisionUpdates_.push_back(std::move(link));
  node.flags |= 0x10000000u;
  return true;
}

bool GameWorld::reparent(WorldNode &node, LinkOwner &newParent) {
  if (!nodes_.count(node.handle.id) || &node == defaultParent())
    return false;
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
  if (!parentHandle.id)
    return false;
  if (!tree_.move(node.handle, parentHandle))
    return false;
  const auto propagateSector = [&](auto &self, WorldNode &current,
                                   const WorldNode *parentNode) -> void {
    if (sectorHookedType(current.type)) {
      if (parentNode)
        attachSector(current, *parentNode);
      else if (current.flags & 0x10)
        setLinkOwner(current.sectorLink, &current);
    }
    const auto kids = children(current);
    if (kids) {
      for (auto *child : *kids) {
        if (child)
          self(self, *child, &current);
      }
    }
  };
  propagateSector(propagateSector, node, parentWorld);
  return true;
}

bool GameWorld::drainDeferredDestroy(std::string &error) {
  if (drainingDestroy_) {
    error = "deferred destroy is already draining";
    return false;
  }
  struct DrainGuard {
    bool &flag;
    ~DrainGuard() { flag = false; }
  } guard{drainingDestroy_};
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
