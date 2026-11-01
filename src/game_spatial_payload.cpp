#include "game_spatial_payload.hpp"

#include "car_physics.hpp"
#include "ground_runtime.hpp"
#include "run_log.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <unordered_set>
#include <utility>

bool prepareSpatialGamePayload(const GameResourcePayload &payload,
                               PreparedSpatialGamePayload &out,
                               std::string &error) {
  if ((payload.gameType && *payload.gameType != 0) ||
      (payload.runtimeGameType && *payload.runtimeGameType != 0) ||
      payload.parameters ||
      std::any_of(payload.actions.begin(), payload.actions.end(),
                  [](const auto &action) {
                    return action.kind == GamePayloadAction::Kind::GameType &&
                           action.value != 0;
                  })) {
    error = "spatial payload: factory actions unsupported";
    return false;
  }
  size_t externalIndex = 0;
  bool externalPending = false;
  for (const auto &action : payload.actions) {
    if (action.kind == GamePayloadAction::Kind::ExternalPack) {
      if (externalIndex >= payload.externalTypes.size() ||
          payload.externalTypes[externalIndex++] != action.value) {
        error = "spatial payload: inconsistent external pack actions";
        return false;
      }
      externalPending = true;
    } else if (action.kind == GamePayloadAction::Kind::ExternalPackEnd) {
      externalPending = false;
    }
  }
  if (externalPending || externalIndex != payload.externalTypes.size()) {
    error = "spatial payload: inconsistent external pack actions";
    return false;
  }
  PreparedSpatialGamePayload result;
  for (const auto &child : payload.children) {
    if (child.entry.resType != 1) {
      error = "spatial payload: child resource unsupported";
      return false;
    }
    result.children.push_back(child);
  }
  for (const auto &chunk : payload.geometryChunks) {
    if (const auto *physics = std::get_if<GamePhysicsChunk>(&chunk)) {
      if (result.physics) {
        error = "spatial payload: multiple PHYS chunks unsupported";
        return false;
      }
      GamePhysicsGeometry geometry;
      if (!decodeGamePhysicsGeometry(*physics, geometry, error))
        return false;
      result.physics = std::move(geometry);
    } else {
      if (result.polygons) {
        error = "spatial payload: multiple POLY chunks unsupported";
        return false;
      }
      const auto &polygons = std::get<GamePolygonChunk>(chunk);
      // Stock 0053e1a0 reads the POLY version word and discards it.
      result.polygons = polygons;
    }
  }
  out = std::move(result);
  error.clear();
  return true;
}

SpatialPhysicsCandidates
queryPassiveSpatialCandidates(ResourceWorld &resources, ResourceNode &root,
                              const std::array<float, 3> &minimum,
                              const std::array<float, 3> &maximum) {
  SpatialPhysicsCandidates result;
  result.detail =
      "dynamic bodies and collision material resolution unsupported";
  for (size_t axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) ||
        minimum[axis] > maximum[axis]) {
      result.detail = "invalid spatial query bounds";
      return result;
    }
  }
  if (!root.loaded() || resources.resolve(&root).packNode != &root ||
      root.type != 1) {
    result.detail = "spatial root is not loaded";
    return result;
  }
  result.passiveTraversalComplete = true;
  std::vector<ResourceNode *> pending{&root};
  while (!pending.empty()) {
    auto *node = pending.back();
    pending.pop_back();
    if (!node->bounds || node->bounds->mode != 1) {
      result.detail = "spatial node bounds unsupported";
      result.passiveTraversalComplete = false;
      continue;
    }
    bool overlap = true;
    bool valid = true;
    for (size_t axis = 0; axis < 3; ++axis) {
      const float center = node->bounds->values[axis];
      const float extent = node->bounds->values[axis + 3];
      if (!std::isfinite(center) || !std::isfinite(extent) || extent < 0) {
        result.detail = "spatial node bounds unsupported";
        result.passiveTraversalComplete = false;
        valid = false;
        break;
      }
      if (maximum[axis] < center - extent || minimum[axis] > center + extent)
        overlap = false;
    }
    if (!valid || !overlap)
      continue;
    if (!node->loaded()) {
      std::string error;
      if (!resources.loadPassiveSpatialGame(*node, error)) {
        result.detail = "spatial load incomplete: " + error;
        result.passiveTraversalComplete = false;
        continue;
      }
    }
    if (node->spatialPayload && node->spatialPayload->physics)
      result.candidates.push_back({node->value, node->spatialPayload, node});
    for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
      // 004a2010 excludes marked children before dispatching by node type.
      if (*it && ((*it)->flags & 0x200000u))
        continue;
      if (!*it || (*it)->parent != node || (*it)->type != 1) {
        result.detail = "spatial child unsupported";
        result.passiveTraversalComplete = false;
        continue;
      }
      pending.push_back(*it);
    }
  }
  return result;
}

SpatialGroundFace::~SpatialGroundFace() {
  setLinkOwner(leaf_, nullptr);
  setLinkOwner(sector_, nullptr);
  setLinkOwner(body_, nullptr);
}

bool SpatialGroundFace::valid() const {
  if (!leaf_.owner || !sector_.owner || !body_.owner || !payload_ || !ground_ ||
      !ground_->physics || !payload_->physics ||
      (!ground_->files.materials && !ground_->fallbackMaterial))
    return false;
  const auto *leaf = static_cast<const ResourceNode *>(leaf_.owner);
  const auto *sector = static_cast<const ResourceNode *>(sector_.owner);
  const auto *body = static_cast<const WorldNode *>(body_.owner);
  return leaf->loaded() && leaf->spatialPayload == payload_ &&
         leaf->sectorLink.owner == sector && sector->loaded() &&
         sector->groundFactory &&
         sector->groundFactory->preparation == ground_ &&
         sector->groundFactory->city.owner == sector &&
         ground_->bodyResource.owner == body && body->loaded &&
         body->groundBody == ground_->physics &&
         ground_->physics->body.proto == &ground_->physics->prototype &&
         faceIndex_ < payload_->physics->triangles.size() &&
         (!ground_->files.materials ||
          materialIndex_ < ground_->files.materials->materials.size()) &&
         payload_->physics->triangles[faceIndex_].materialWord ==
             materialIndex_;
}

const GamePhysicsTriangle *SpatialGroundFace::face() const {
  return valid() ? &payload_->physics->triangles[faceIndex_] : nullptr;
}
const GroundMaterial *SpatialGroundFace::material() const {
  // Without a material file the face carries only the ground's fallback
  // render/sound material (no friction table): no per-material tire data.
  return valid() && ground_->files.materials
             ? &ground_->files.materials->materials[materialIndex_]
             : nullptr;
}
const GroundRuntime *SpatialGroundFace::body() const {
  return valid() ? ground_->physics.get() : nullptr;
}
std::shared_ptr<GroundRuntime> SpatialGroundFace::bodyOwner() const {
  return valid() ? ground_->physics : nullptr;
}
const GamePhysicsGeometry *SpatialGroundFace::geometry() const {
  return valid() ? &*payload_->physics : nullptr;
}

std::unique_ptr<SpatialGroundFace>
resolveSpatialGroundFace(ResourceWorld &resources, ResourceNode &leaf,
                         size_t faceIndex, std::string &error) {
  if (resources.resolve(&leaf).packNode != &leaf || !leaf.loaded() ||
      !leaf.spatialPayload || !leaf.spatialPayload->physics ||
      faceIndex >= leaf.spatialPayload->physics->triangles.size()) {
    error = "loaded spatial face unavailable";
    return nullptr;
  }
  const auto sector = resources.resolve(leaf.sectorLink.owner);
  if (!sector.packNode || sector.type != 1 || !sector.packNode->loaded() ||
      !sector.packNode->groundFactory ||
      sector.packNode->groundFactory->city.owner != sector.packNode ||
      !sector.packNode->groundFactory->preparation) {
    error = "spatial ground sector unavailable";
    return nullptr;
  }
  auto ground = sector.packNode->groundFactory->preparation;
  auto *body = resources.world().nodeFor(ground->bodyResource.owner);
  if (!ground->physics || !body || body->type != 2 || !body->loaded ||
      resources.world().parentOwner(body->handle) != sector.owner ||
      body->groundBody != ground->physics ||
      ground->physics->body.proto != &ground->physics->prototype) {
    error = "spatial ground body ownership unsupported";
    return nullptr;
  }
  const auto word =
      leaf.spatialPayload->physics->triangles[faceIndex].materialWord;
  // A ground without a material file (bindGroundFallbackMaterial) accepts any
  // material word; the explicit gap is that its faces yield no tire material.
  if (ground->files.materials || !ground->fallbackMaterial) {
    if (!groundContactMaterial(*ground, word, error))
      return nullptr;
  }
  auto result = std::unique_ptr<SpatialGroundFace>(new SpatialGroundFace);
  result->payload_ = leaf.spatialPayload;
  result->ground_ = std::move(ground);
  result->faceIndex_ = faceIndex;
  result->materialIndex_ = word;
  setLinkOwner(result->leaf_, &leaf);
  setLinkOwner(result->sector_, sector.owner);
  setLinkOwner(result->body_, body);
  error.clear();
  return result;
}

bool WheelGroundContact::validGeometry() const {
  return face_ && face_->valid() && root_ && wheel_ &&
         root_->proto == rootPrototype_.get() &&
         wheel_->proto == wheelPrototype_.get() && !root_->attachment &&
         !(root_->flags & 0x24u) && !(rootPrototype_->flags & 0x24u) &&
         (root_->flags & physics::kBodyHasChildren) && wheel_->attachment &&
         wheel_->attachment->parent == root_.get() &&
         (wheel_->flags & physics::kBodyChild) && !(wheel_->flags & 0x21020u) &&
         (wheel_->flags & 0x40u) && !(wheelPrototype_->flags & 0x20u) &&
         wheel_->current <= 1 &&
         wheel_->state[predictedIndex_].pos == predictedPosition_ &&
         wheel_->state[predictedIndex_].rot == predictedRotation_ &&
         wheel_->state[predictedIndex_].time == predictedTime_ &&
         wheel_->state[predictedIndex_ ^ 1].pos == priorPosition_ &&
         wheel_->state[predictedIndex_ ^ 1].rot == priorRotation_ &&
         wheel_->state[predictedIndex_ ^ 1].time == priorTime_ &&
         wheelPrototype_->shape.dim[0] == halfLength_;
}

const physics::SuspensionContactCandidate *
WheelGroundContact::candidate() const {
  return validGeometry() && (wheel_->current ^ 1) == predictedIndex_
             ? &candidate_
             : nullptr;
}

const physics::SuspensionContactCandidate *
WheelGroundContact::tireCandidate() const {
  if (const auto *pending = candidate())
    return pending;
  if (!validGeometry() || wheel_->current != predictedIndex_ ||
      !std::isfinite(priorTime_) || !std::isfinite(predictedTime_) ||
      !(predictedTime_ > priorTime_))
    return nullptr;
  const auto &prior = wheel_->state[predictedIndex_ ^ 1];
  if (prior.time != priorTime_ || prior.pos != priorPosition_ ||
      prior.rot != priorRotation_)
    return nullptr;
  return &candidate_;
}

const physics::SuspensionContactCandidate *
WheelGroundContact::retainedCandidate() const {
  if (!face_ || !face_->valid() || !root_ || !wheel_ || !rootPrototype_ ||
      !wheelPrototype_ || root_->proto != rootPrototype_.get() ||
      wheel_->proto != wheelPrototype_.get() || wheel_->current > 1)
    return nullptr;
  return &candidate_;
}
const GroundMaterial *WheelGroundContact::retainedMaterial() const {
  return retainedCandidate() ? face_->material() : nullptr;
}

const char *WheelGroundContact::tireBlock() const {
  if (tireCandidate())
    return "none";
  if (!face_ || !face_->valid())
    return "ground face no longer valid";
  if (!root_ || !wheel_)
    return "bodies released";
  if (root_->proto != rootPrototype_.get() ||
      wheel_->proto != wheelPrototype_.get())
    return "prototype changed";
  if (wheel_->current > 1)
    return "wheel state index";
  if (wheel_->state[predictedIndex_].pos != predictedPosition_)
    return "wheel predicted position changed";
  if (wheel_->state[predictedIndex_].rot != predictedRotation_)
    return "wheel predicted rotation changed";
  if (wheel_->state[predictedIndex_].time != predictedTime_)
    return "wheel predicted time changed";
  if (wheel_->state[predictedIndex_ ^ 1].pos != priorPosition_)
    return "wheel prior position changed";
  if (wheel_->state[predictedIndex_ ^ 1].rot != priorRotation_)
    return "wheel prior rotation changed";
  if (wheel_->state[predictedIndex_ ^ 1].time != priorTime_)
    return "wheel prior time changed";
  if (wheel_->current != predictedIndex_ &&
      (wheel_->current ^ 1) != predictedIndex_)
    return "wheel buffer index unrelated";
  if (wheel_->current != predictedIndex_)
    return "witness not yet committed";
  if (!(predictedTime_ > priorTime_))
    return "no time advance";
  return "wheel geometry check";
}

const GroundMaterial *WheelGroundContact::groundMaterial() const {
  return tireCandidate() ? face_->material() : nullptr;
}

WheelGroundContactResult
makeWheelGroundContact(std::unique_ptr<SpatialGroundFace> face,
                       std::shared_ptr<physics::Body> root,
                       std::shared_ptr<const physics::BodyProto> rootPrototype,
                       std::shared_ptr<physics::Body> wheel,
                       std::shared_ptr<const physics::BodyProto> wheelPrototype,
                       bool parentFaceAccepted) {
  WheelGroundContactResult result;
  result.detail = "wheel contact prerequisites unsupported";
  if (!parentFaceAccepted || !face || !face->valid() || !root || !wheel ||
      !rootPrototype || !wheelPrototype || root == wheel ||
      root->proto != rootPrototype.get() ||
      wheel->proto != wheelPrototype.get() || root->attachment ||
      (root->flags & 0x24u) || (rootPrototype->flags & 0x24u) ||
      !(root->flags & physics::kBodyHasChildren) || !wheel->attachment ||
      wheel->attachment->parent != root.get() ||
      !(wheel->flags & physics::kBodyChild) || (wheel->flags & 0x20020u) ||
      (wheelPrototype->flags & 0x20u) || !(wheel->flags & 0x40u) ||
      (wheel->flags & 0x1000u) || wheel->current > 1 ||
      (wheelPrototype->shape.type != physics::ShapeType::CylinderY &&
       wheelPrototype->shape.type != physics::ShapeType::CylinderZ))
    return result;
  auto ground = face->bodyOwner();
  if (!ground || ground->body.current > 1 || ground->body.attachment ||
      ground->body.state[ground->body.current ^ 1].pos != physics::Vec3{} ||
      ground->body.state[ground->body.current ^ 1].rot !=
          physics::matrixIdentity()) {
    result.detail = "non-origin or transformed ground body unsupported";
    return result;
  }
  const auto &state = wheel->state[wheel->current ^ 1];
  const float radius = wheelPrototype->shape.dim[0];
  if (!std::isfinite(radius) || radius <= 0)
    return result;
  for (float value : state.pos)
    if (!std::isfinite(value))
      return result;
  for (float value : state.rot)
    if (!std::isfinite(value))
      return result;
  for (size_t i = 0; i < 3; ++i)
    for (size_t j = 0; j < 3; ++j) {
      float dot = 0;
      for (size_t k = 0; k < 3; ++k)
        dot += state.rot[k * 4 + i] * state.rot[k * 4 + j];
      if (std::fabs(dot - (i == j ? 1.f : 0.f)) > 0.0001f)
        return result;
    }
  physics::Vec3 axis{};
  axis[wheelPrototype->shape.type == physics::ShapeType::CylinderY ? 1 : 2] =
      radius;
  const auto offset = physics::matrixTransformRows(state.rot, axis);
  physics::Vec3 start{}, end{};
  for (size_t i = 0; i < 3; ++i) {
    start[i] = state.pos[i] - offset[i];
    end[i] = state.pos[i] + offset[i];
  }
  const auto hit =
      queryGamePhysicsFace(*face->geometry(), face->faceIndex(), start, end);
  if (hit.status == GamePhysicsHitStatus::Unsupported) {
    result.detail = "wheel face intersection unsupported";
    // Keep the exact rejected PHYS query, rather than diagnosing a wheel
    // contact refusal from a later screenshot or rounded Cartrace pose.
    auto* channel = runlog::traceChannel("contacttrace");
    if (channel && std::getenv("SLRR_CAR_TRACE"))
      channel->stream.store(true, std::memory_order_relaxed);
    const auto& triangle = face->geometry()->triangles[face->faceIndex()];
    const auto& vertices = face->geometry()->vertices;
    if (std::any_of(triangle.vertices.begin(), triangle.vertices.end(),
                    [&](size_t index) { return index >= vertices.size(); })) {
      runlog::event(runlog::EventKind::Error,
                    "wheel face intersection: invalid vertex indices on face %zu", face->faceIndex());
      return result;
    }
    const auto& a = vertices[triangle.vertices[0]];
    const auto& b = vertices[triangle.vertices[1]];
    const auto& c = vertices[triangle.vertices[2]];
    char line[1024];
    std::snprintf(line, sizeof line,
                  "{\"kind\":\"wheel-face-refusal\",\"wheel\":\"%p\",\"leaf\":\"%p\","
                  "\"face\":%zu,\"time\":%.9g,\"start\":[%.9g,%.9g,%.9g],"
                  "\"end\":[%.9g,%.9g,%.9g],\"n\":[%.9g,%.9g,%.9g],"
                  "\"vertices\":[[%.9g,%.9g,%.9g],[%.9g,%.9g,%.9g],[%.9g,%.9g,%.9g]]}",
                  static_cast<void*>(wheel.get()), static_cast<const void*>(face->leafOwner()),
                  face->faceIndex(), state.time, start[0], start[1], start[2],
                  end[0], end[1], end[2], triangle.normal[0], triangle.normal[1], triangle.normal[2],
                  a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
    runlog::tracePush(channel, line);
    return result;
  }
  if (hit.status == GamePhysicsHitStatus::Miss) {
    result.status = WheelGroundContactStatus::Miss;
    result.detail = "one accepted face missed; world coverage incomplete";
    return result;
  }
  physics::Vec3 delta{}, local{};
  for (size_t i = 0; i < 3; ++i)
    delta[i] = hit.point[i] - state.pos[i];
  for (size_t i = 0; i < 3; ++i)
    local[i] = state.rot[i] * delta[0] + state.rot[4 + i] * delta[1] +
               state.rot[8 + i] * delta[2];
  auto contact = std::make_unique<WheelGroundContact>();
  contact->face_ = std::move(face);
  contact->root_ = std::move(root);
  contact->wheel_ = std::move(wheel);
  contact->rootPrototype_ = std::move(rootPrototype);
  contact->wheelPrototype_ = std::move(wheelPrototype);
  contact->predictedPosition_ = state.pos;
  contact->predictedRotation_ = state.rot;
  contact->predictedTime_ = state.time;
  const auto &prior = contact->wheel_->state[contact->wheel_->current];
  contact->priorPosition_ = prior.pos;
  contact->priorRotation_ = prior.rot;
  contact->priorTime_ = prior.time;
  contact->predictedIndex_ = contact->wheel_->current ^ 1;
  contact->halfLength_ = radius;
  contact->candidate_.a = contact->wheel_.get();
  contact->candidate_.b = &ground->body;
  contact->candidate_.pointA = local;
  contact->candidate_.pointB = hit.point;
  contact->candidate_.normal = hit.normal;
  contact->candidate_.otherOwner =
      std::shared_ptr<physics::Body>(ground, &ground->body);
  contact->candidate_.otherPrototype =
      std::shared_ptr<const physics::BodyProto>(ground, &ground->prototype);
  contact->candidate_.surface =
      std::make_shared<physics::SuspensionContactSurface>(
          physics::SuspensionContactSurface{hit.materialWord,
                                            hit.materialWord});
  result.status = WheelGroundContactStatus::Hit;
  result.detail = "one accepted face hit; world coverage incomplete";
  result.contact = std::move(contact);
  return result;
}

WheelGroundContacts collectWheelGroundContacts(
    ResourceWorld &resources, ResourceNode &root,
    std::shared_ptr<physics::Body> parent,
    std::shared_ptr<const physics::BodyProto> parentPrototype,
    std::shared_ptr<physics::Body> wheel,
    std::shared_ptr<const physics::BodyProto> wheelPrototype) {
  WheelGroundContacts result;
  result.detail = "wheel contact prerequisites unsupported";
  if (!parent || !parentPrototype || !wheel || !wheelPrototype ||
      parent == wheel || parent->proto != parentPrototype.get() ||
      wheel->proto != wheelPrototype.get() || parent->current > 1 ||
      wheel->current > 1 || parent->attachment || (parent->flags & 0x24u) ||
      (parentPrototype->flags & 0x24u) ||
      !(parent->flags & physics::kBodyHasChildren) || !wheel->attachment ||
      wheel->attachment->parent != parent.get() ||
      !(wheel->flags & physics::kBodyChild) || (wheel->flags & 0x21020u) ||
      !(wheel->flags & 0x40u) || (wheelPrototype->flags & 0x20u) ||
      (wheelPrototype->shape.type != physics::ShapeType::CylinderY &&
       wheelPrototype->shape.type != physics::ShapeType::CylinderZ) ||
      (parentPrototype->shape.type != physics::ShapeType::Box &&
       parentPrototype->shape.type != physics::ShapeType::Mesh))
    return result;
  const float radius = parentPrototype->shape.boundRadius;
  const float wheelRadius = wheelPrototype->shape.boundRadius;
  if (!std::isfinite(radius) || radius < 0 || !std::isfinite(wheelRadius) ||
      wheelRadius < 0 || !std::isfinite(wheelPrototype->shape.dim[0]) ||
      wheelPrototype->shape.dim[0] <= 0)
    return result;
  std::array<float, 3> minimum{}, maximum{};
  const auto &position = parent->state[parent->current ^ 1].pos;
  const auto &wheelPosition = wheel->state[wheel->current ^ 1].pos;
  for (size_t i = 0; i < 3; ++i) {
    if (!std::isfinite(position[i]) || !std::isfinite(wheelPosition[i]))
      return result;
    minimum[i] = position[i] - radius;
    maximum[i] = position[i] + radius;
  }
  auto candidates =
      queryPassiveSpatialCandidates(resources, root, minimum, maximum);
  result.passiveTraversalComplete = candidates.passiveTraversalComplete;
  result.detail = candidates.detail;
  for (const auto &candidate : candidates.candidates) {
    auto &node = *candidate.node;
    const auto &geometry = *candidate.payload->physics;
    // 004a0aa0 repeats the node sphere/AABB rejection for each child.
    bool wheelOverlaps = true;
    for (size_t axis = 0; axis < 3; ++axis)
      if (wheelRadius + node.bounds->values[axis + 3] <
          std::fabs(wheelPosition[axis] - node.bounds->values[axis]))
        wheelOverlaps = false;
    if (!wheelOverlaps)
      continue;
    for (size_t face = 0; face < geometry.triangles.size(); ++face) {
      const auto mask =
          queryParentFace(geometry, face, *parent, *node.bounds, false);
      if (mask == ParentFaceMask::Rejected)
        continue;
      if (mask == ParentFaceMask::Unsupported) {
        result.passiveTraversalComplete = false;
        result.detail = "parent face filter unsupported";
        continue;
      }
      std::string error;
      auto resolved = resolveSpatialGroundFace(resources, node, face, error);
      if (!resolved) {
        result.passiveTraversalComplete = false;
        result.detail = "ground face unavailable: " + error;
        continue;
      }
      auto contact =
          makeWheelGroundContact(std::move(resolved), parent, parentPrototype,
                                 wheel, wheelPrototype, true);
      if (contact.status == WheelGroundContactStatus::Unsupported) {
        result.passiveTraversalComplete = false;
        result.detail = contact.detail;
      } else if (contact.status == WheelGroundContactStatus::Hit) {
        result.contacts.push_back(std::move(contact.contact));
      }
    }
  }
  return result;
}

bool CarGroundFacePair::valid() const {
  return car && car->root && bodyObservation && bodyObservation->body == body &&
         body && body->proto == prototype && ground && face && face->valid() &&
         face->bodyOwner() == ground && pair.contact.a == body &&
         pair.contact.b == &ground->body;
}

CarGroundContacts collectCarGroundContacts(ResourceWorld &resources,
                                           ResourceNode &spatialRoot,
                                           std::shared_ptr<CarPhysics> car) {
  using namespace physics;
  CarGroundContacts result;
  result.detail = "car ground traversal prerequisites unsupported";
  if (!car || !car->root || car->root->proto != &car->rootPrototype ||
      car->root->current > 1 || car->root->attachment ||
      !std::isfinite(car->rootPrototype.shape.boundRadius) ||
      car->rootPrototype.shape.boundRadius < 0)
    return result;
  // Validate the indexed tree before using raw body/prototype pointers.
  std::unordered_set<Body *> seen;
  std::function<bool(Body *)> validate = [&](Body *body) {
    const auto entry = car->bodyChildren.find(body);
    const auto observation = car->bodyObservations.find(body);
    if (!body || !seen.insert(body).second || !body->proto ||
        body->current > 1 || entry == car->bodyChildren.end() ||
        observation == car->bodyObservations.end() || !observation->second ||
        observation->second->body != body ||
        bool(body->flags & kBodyHasChildren) != !entry->second.empty())
      return false;
    for (auto *child : entry->second)
      if (!child || !child->attachment || child->attachment->parent != body ||
          !(child->flags & kBodyChild) || !validate(child))
        return false;
    return true;
  };
  if (!validate(car->root.get()) || seen.size() != car->bodyChildren.size())
    return result;
  std::array<float, 3> minimum{}, maximum{};
  const auto &position = car->root->state[car->root->current ^ 1].pos;
  for (size_t i = 0; i < 3; ++i) {
    if (!std::isfinite(position[i]))
      return result;
    minimum[i] = position[i] - car->rootPrototype.shape.boundRadius;
    maximum[i] = position[i] + car->rootPrototype.shape.boundRadius;
  }
  auto candidates =
      queryPassiveSpatialCandidates(resources, spatialRoot, minimum, maximum);
  result.passiveTraversalComplete = candidates.passiveTraversalComplete;
  result.detail = candidates.detail;
  auto incomplete = [&](std::string detail) {
    result.passiveTraversalComplete = false;
    result.detail = std::move(detail);
  };
  for (const auto &spatial : candidates.candidates) {
    auto &node = *spatial.node;
    const auto &geometry = *spatial.payload->physics;
    for (size_t faceIndex = 0; faceIndex < geometry.triangles.size();
         ++faceIndex) {
      std::function<void(Body *)> visit = [&](Body *body) {
        if (body != car->root.get() &&
            ((body->flags & 0x20020u) || (body->proto->flags & 0x20u)))
          return; // 004a0aa0 excludes these child branches.
        const auto &children = car->bodyChildren.at(body);
        if (!children.empty()) {
          const auto mask =
              queryParentFace(geometry, faceIndex, *body, *node.bounds, false);
          if (mask == ParentFaceMask::Rejected)
            return;
          if (mask == ParentFaceMask::Unsupported) {
            incomplete("compound parent face filter unsupported");
            return;
          }
          for (auto *child : children)
            visit(child);
          return;
        }
        if (body->current > 1 ||
            !std::isfinite(body->proto->shape.boundRadius) ||
            body->proto->shape.boundRadius < 0) {
          incomplete("child spatial bounds unsupported");
          return;
        }
        for (size_t axis = 0; axis < 3; ++axis) {
          const float coordinate = body->state[body->current ^ 1].pos[axis];
          if (!std::isfinite(coordinate)) {
            incomplete("child spatial bounds unsupported");
            return;
          }
          if (body->proto->shape.boundRadius + node.bounds->values[axis + 3] <
              std::fabs(coordinate - node.bounds->values[axis]))
            return;
        }
        std::string error;
        auto face = resolveSpatialGroundFace(resources, node, faceIndex, error);
        if (!face) {
          incomplete("ground face unavailable: " + error);
          return;
        }
        if (body->flags & 0x40u) {
          auto parent = std::shared_ptr<Body>(car, car->root.get());
          auto parentProto =
              std::shared_ptr<const BodyProto>(car, &car->rootPrototype);
          auto wheel = std::shared_ptr<Body>(car, body);
          auto wheelProto = std::shared_ptr<const BodyProto>(car, body->proto);
          auto contact = makeWheelGroundContact(
              std::move(face), std::move(parent), std::move(parentProto),
              std::move(wheel), std::move(wheelProto), true);
          if (contact.status == WheelGroundContactStatus::Unsupported)
            incomplete(contact.detail);
          else if (contact.status == WheelGroundContactStatus::Hit)
            result.wheels.push_back({body, std::move(contact.contact)});
          return;
        }
        if (body->flags & 0x800u) {
          incomplete("segment-only child collision unsupported");
          return;
        }
        auto ground = face->bodyOwner();
        if (!ground) {
          incomplete("ground body unavailable");
          return;
        }
        const auto &triangle = geometry.triangles[faceIndex];
        std::array<Vec3, 3> vertices{};
        for (size_t i = 0; i < 3; ++i) {
          if (triangle.vertices[i] >= geometry.vertices.size()) {
            incomplete("ground face vertex unavailable");
            return;
          }
          vertices[i] = geometry.vertices[triangle.vertices[i]];
        }
        auto retained = std::make_unique<CarGroundFacePair>();
        if (!createGroundFacePair(*body, ground->body, vertices,
                                  triangle.normal, retained->pair, error)) {
          incomplete("ground pair unsupported: " + error);
          return;
        }
        retained->car = car;
        retained->bodyObservation = car->bodyObservations.at(body);
        retained->ground = std::move(ground);
        retained->face = std::move(face);
        retained->body = body;
        retained->prototype = body->proto;
        result.pairs.push_back(std::move(retained));
      };
      visit(car->root.get());
    }
  }
  return result;
}
