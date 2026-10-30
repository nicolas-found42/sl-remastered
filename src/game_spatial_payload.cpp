#include "game_spatial_payload.hpp"
#include "ground_runtime.hpp"
#include <cmath>
#include <utility>

bool prepareSpatialGamePayload(const GameResourcePayload &payload,
                               PreparedSpatialGamePayload &out, std::string &error) {
  if (!payload.actions.empty() || payload.gameType || payload.runtimeGameType ||
      payload.parameters || !payload.externalTypes.empty()) {
    error="spatial payload: factory or external pack actions unsupported";
    return false;
  }
  PreparedSpatialGamePayload result;
  for (const auto &child : payload.children) {
    if (child.entry.resType != 1) {
      error="spatial payload: child resource unsupported";
      return false;
    }
    result.children.push_back(child);
  }
  for (const auto &chunk : payload.geometryChunks) {
    if (const auto *physics=std::get_if<GamePhysicsChunk>(&chunk)) {
      if (result.physics) { error="spatial payload: multiple PHYS chunks unsupported"; return false; }
      GamePhysicsGeometry geometry;
      if (!decodeGamePhysicsGeometry(*physics,geometry,error)) return false;
      result.physics=std::move(geometry);
    } else {
      if (result.polygons) { error="spatial payload: multiple POLY chunks unsupported"; return false; }
      const auto &polygons=std::get<GamePolygonChunk>(chunk);
      if (polygons.version!=1) { error="spatial payload: POLY version unsupported"; return false; }
      result.polygons=polygons;
    }
  }
  out=std::move(result);
  error.clear();
  return true;
}

SpatialPhysicsCandidates queryPassiveSpatialCandidates(
    ResourceWorld &resources, ResourceNode &root,
    const std::array<float, 3> &minimum, const std::array<float, 3> &maximum) {
  SpatialPhysicsCandidates result;
  result.detail = "dynamic bodies and collision material resolution unsupported";
  for (size_t axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) ||
        minimum[axis] > maximum[axis]) {
      result.detail = "invalid spatial query bounds";
      return result;
    }
  }
  if (!root.loaded() || resources.resolve(&root).packNode != &root || root.type != 1) {
    result.detail = "spatial root is not loaded";
    return result;
  }
  std::vector<ResourceNode *> pending{&root};
  while (!pending.empty()) {
    auto *node = pending.back();
    pending.pop_back();
    if (!node->bounds || node->bounds->mode != 1) {
      result.detail = "spatial node bounds unsupported";
      continue;
    }
    bool overlap = true;
    bool valid = true;
    for (size_t axis = 0; axis < 3; ++axis) {
      const float center = node->bounds->values[axis];
      const float extent = node->bounds->values[axis + 3];
      if (!std::isfinite(center) || !std::isfinite(extent) || extent < 0) {
        result.detail = "spatial node bounds unsupported";
        valid = false;
        break;
      }
      if (maximum[axis] < center - extent || minimum[axis] > center + extent)
        overlap = false;
    }
    if (!valid || !overlap) continue;
    if (!node->loaded()) {
      std::string error;
      if (!resources.loadPassiveSpatialGame(*node, error)) {
        result.detail = "spatial load incomplete: " + error;
        continue;
      }
    }
    if (node->spatialPayload && node->spatialPayload->physics)
      result.candidates.push_back({node->value, node->spatialPayload});
    for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
      if (!*it || (*it)->parent != node || (*it)->type != 1) {
        result.detail = "spatial child unsupported";
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
      !ground_->physics || !ground_->files.materials || !payload_->physics)
    return false;
  const auto *leaf = static_cast<const ResourceNode *>(leaf_.owner);
  const auto *sector = static_cast<const ResourceNode *>(sector_.owner);
  const auto *body = static_cast<const WorldNode *>(body_.owner);
  return leaf->loaded() && leaf->spatialPayload == payload_ &&
         leaf->sectorLink.owner == sector && sector->loaded() &&
         sector->groundFactory && sector->groundFactory->preparation == ground_ &&
         sector->groundFactory->city.owner == sector &&
         ground_->bodyResource.owner == body && body->loaded &&
         body->groundBody == ground_->physics &&
         ground_->physics->body.proto == &ground_->physics->prototype &&
         faceIndex_ < payload_->physics->triangles.size() &&
         materialIndex_ < ground_->files.materials->materials.size() &&
         payload_->physics->triangles[faceIndex_].materialWord == materialIndex_;
}

const GamePhysicsTriangle *SpatialGroundFace::face() const {
  return valid() ? &payload_->physics->triangles[faceIndex_] : nullptr;
}
const GroundMaterial *SpatialGroundFace::material() const {
  return valid() ? &ground_->files.materials->materials[materialIndex_] : nullptr;
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

std::unique_ptr<SpatialGroundFace> resolveSpatialGroundFace(
    ResourceWorld &resources, ResourceNode &leaf, size_t faceIndex, std::string &error) {
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
  const auto word = leaf.spatialPayload->physics->triangles[faceIndex].materialWord;
  if (!groundContactMaterial(*ground, word, error)) return nullptr;
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

const physics::SuspensionContactCandidate *WheelGroundContact::candidate() const {
  if (!face_ || !face_->valid() || !root_ || !wheel_ ||
      root_->proto != rootPrototype_.get() || wheel_->proto != wheelPrototype_.get() ||
      root_->attachment || (root_->flags & 0x24u) || (rootPrototype_->flags & 0x24u) ||
      !(root_->flags & physics::kBodyHasChildren) ||
      !wheel_->attachment || wheel_->attachment->parent != root_.get() ||
      !(wheel_->flags & physics::kBodyChild) || (wheel_->flags & 0x21020u) ||
      !(wheel_->flags & 0x40u) || (wheelPrototype_->flags & 0x20u) ||
      wheel_->current > 1 || (wheel_->current ^ 1) != predictedIndex_ ||
      wheel_->state[predictedIndex_].pos != predictedPosition_ ||
      wheel_->state[predictedIndex_].rot != predictedRotation_ ||
      wheelPrototype_->shape.dim[0] != halfLength_) return nullptr;
  return &candidate_;
}

WheelGroundContactResult makeWheelGroundContact(
    std::unique_ptr<SpatialGroundFace> face, std::shared_ptr<physics::Body> root,
    std::shared_ptr<const physics::BodyProto> rootPrototype,
    std::shared_ptr<physics::Body> wheel,
    std::shared_ptr<const physics::BodyProto> wheelPrototype, bool parentFaceAccepted) {
  WheelGroundContactResult result;
  result.detail = "wheel contact prerequisites unsupported";
  if (!parentFaceAccepted || !face || !face->valid() || !root || !wheel ||
      !rootPrototype || !wheelPrototype || root == wheel ||
      root->proto != rootPrototype.get() || wheel->proto != wheelPrototype.get() ||
      root->attachment || (root->flags & 0x24u) || (rootPrototype->flags & 0x24u) ||
      !(root->flags & physics::kBodyHasChildren) ||
      !wheel->attachment || wheel->attachment->parent != root.get() ||
      !(wheel->flags & physics::kBodyChild) || (wheel->flags & 0x20020u) ||
      (wheelPrototype->flags & 0x20u) || !(wheel->flags & 0x40u) ||
      (wheel->flags & 0x1000u) || wheel->current > 1 ||
      (wheelPrototype->shape.type != physics::ShapeType::CylinderY &&
       wheelPrototype->shape.type != physics::ShapeType::CylinderZ)) return result;
  auto ground = face->bodyOwner();
  if (!ground || ground->body.current > 1 || ground->body.attachment ||
      ground->body.state[ground->body.current ^ 1].pos != physics::Vec3{} ||
      ground->body.state[ground->body.current ^ 1].rot != physics::matrixIdentity()) {
    result.detail = "non-origin or transformed ground body unsupported";
    return result;
  }
  const auto &state = wheel->state[wheel->current ^ 1];
  const float radius = wheelPrototype->shape.dim[0];
  if (!std::isfinite(radius) || radius <= 0) return result;
  for (float value : state.pos) if (!std::isfinite(value)) return result;
  for (float value : state.rot) if (!std::isfinite(value)) return result;
  for (size_t i = 0; i < 3; ++i) for (size_t j = 0; j < 3; ++j) {
    float dot = 0;
    for (size_t k = 0; k < 3; ++k) dot += state.rot[k*4+i] * state.rot[k*4+j];
    if (std::fabs(dot - (i == j ? 1.f : 0.f)) > 0.0001f) return result;
  }
  physics::Vec3 axis{};
  axis[wheelPrototype->shape.type == physics::ShapeType::CylinderY ? 1 : 2] = radius;
  const auto offset = physics::matrixTransformRows(state.rot, axis);
  physics::Vec3 start{}, end{};
  for (size_t i = 0; i < 3; ++i) {
    start[i] = state.pos[i] - offset[i];
    end[i] = state.pos[i] + offset[i];
  }
  const auto hit = queryGamePhysicsFace(*face->geometry(), face->faceIndex(), start, end);
  if (hit.status == GamePhysicsHitStatus::Unsupported) {
    result.detail = "wheel face intersection unsupported";
    return result;
  }
  if (hit.status == GamePhysicsHitStatus::Miss) {
    result.status = WheelGroundContactStatus::Miss;
    result.detail = "one accepted face missed; world coverage incomplete";
    return result;
  }
  physics::Vec3 delta{}, local{};
  for (size_t i = 0; i < 3; ++i) delta[i] = hit.point[i] - state.pos[i];
  for (size_t i = 0; i < 3; ++i)
    local[i] = state.rot[i]*delta[0] + state.rot[4+i]*delta[1] + state.rot[8+i]*delta[2];
  auto contact = std::make_unique<WheelGroundContact>();
  contact->face_ = std::move(face);
  contact->root_ = std::move(root);
  contact->wheel_ = std::move(wheel);
  contact->rootPrototype_ = std::move(rootPrototype);
  contact->wheelPrototype_ = std::move(wheelPrototype);
  contact->predictedPosition_ = state.pos;
  contact->predictedRotation_ = state.rot;
  contact->predictedIndex_ = contact->wheel_->current ^ 1;
  contact->halfLength_ = radius;
  contact->candidate_.a = contact->wheel_.get();
  contact->candidate_.b = &ground->body;
  contact->candidate_.pointA = local;
  contact->candidate_.pointB = hit.point;
  contact->candidate_.normal = hit.normal;
  contact->candidate_.otherOwner = std::shared_ptr<physics::Body>(ground, &ground->body);
  contact->candidate_.otherPrototype = std::shared_ptr<const physics::BodyProto>(ground, &ground->prototype);
  contact->candidate_.surface = std::make_shared<physics::SuspensionContactSurface>(
      physics::SuspensionContactSurface{hit.materialWord, hit.materialWord});
  result.status = WheelGroundContactStatus::Hit;
  result.detail = "one accepted face hit; world coverage incomplete";
  result.contact = std::move(contact);
  return result;
}
