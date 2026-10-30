#pragma once
#include "game_physics_geometry.hpp"
#include "physics_suspension.hpp"
#include <optional>
#include <string>
#include <vector>

// Side-effect-free preparation for the passive 0053dee0 subset. It retains
// child descriptors and complete POLY data, and decodes PHYS. It does not
// register children, instantiate a factory, set bounds or mark a node loaded.
struct PreparedSpatialGamePayload {
  std::vector<GameResourceChild> children;
  std::optional<GamePhysicsGeometry> physics;
  std::optional<GamePolygonChunk> polygons;
};
bool prepareSpatialGamePayload(const GameResourcePayload &payload,
                               PreparedSpatialGamePayload &out, std::string &error);

// Retained PHYS candidates from the packed type-1 tree. This is not a complete
// collision query: dynamic bodies, material resolution, and solver contact
// assignment are not represented, so absence of candidates cannot mean clear.
struct SpatialPhysicsCandidate {
  uint32_t resourceId = 0;
  std::shared_ptr<const PreparedSpatialGamePayload> payload;
};
struct SpatialPhysicsCandidates {
  std::vector<SpatialPhysicsCandidate> candidates;
  bool complete = false;
  std::string detail;
};
SpatialPhysicsCandidates queryPassiveSpatialCandidates(
    ResourceWorld &resources, ResourceNode &root,
    const std::array<float, 3> &minimum, const std::array<float, 3> &maximum);

struct GroundRuntime;
struct GroundPreparation;
struct GroundMaterial;

// A single loaded face and the inherited sector ground that owns its material
// table. Links invalidate this view on node destruction; unloading the leaf
// also invalidates it, even though its payload is retained safely.
class SpatialGroundFace {
public:
  ~SpatialGroundFace();
  SpatialGroundFace(const SpatialGroundFace &) = delete;
  SpatialGroundFace &operator=(const SpatialGroundFace &) = delete;
  bool valid() const;
  const GamePhysicsTriangle *face() const;
  const GroundMaterial *material() const;
  const GroundRuntime *body() const;
  std::shared_ptr<GroundRuntime> bodyOwner() const;
  const GamePhysicsGeometry *geometry() const;
  size_t faceIndex() const { return faceIndex_; }
private:
  SpatialGroundFace() = default;
  friend std::unique_ptr<SpatialGroundFace> resolveSpatialGroundFace(
      ResourceWorld &, ResourceNode &, size_t, std::string &);
  OwnerLink leaf_, sector_, body_;
  std::shared_ptr<PreparedSpatialGamePayload> payload_;
  std::shared_ptr<GroundPreparation> ground_;
  size_t faceIndex_ = 0;
  size_t materialIndex_ = 0;
};
std::unique_ptr<SpatialGroundFace> resolveSpatialGroundFace(
    ResourceWorld &resources, ResourceNode &leaf, size_t faceIndex, std::string &error);

enum class WheelGroundContactStatus { Hit, Miss, Unsupported };
struct WheelGroundContactResult;
class WheelGroundContact {
public:
  const physics::SuspensionContactCandidate *candidate() const;
private:
  friend WheelGroundContactResult makeWheelGroundContact(
      std::unique_ptr<SpatialGroundFace>, std::shared_ptr<physics::Body>,
      std::shared_ptr<const physics::BodyProto>, std::shared_ptr<physics::Body>,
      std::shared_ptr<const physics::BodyProto>, bool);
  std::unique_ptr<SpatialGroundFace> face_;
  std::shared_ptr<physics::Body> root_, wheel_;
  std::shared_ptr<const physics::BodyProto> rootPrototype_, wheelPrototype_;
  physics::Vec3 predictedPosition_{};
  physics::Mat34 predictedRotation_{};
  uint32_t predictedIndex_ = 0;
  float halfLength_ = 0;
  physics::SuspensionContactCandidate candidate_;
};
struct WheelGroundContactResult {
  WheelGroundContactStatus status = WheelGroundContactStatus::Unsupported;
  std::unique_ptr<WheelGroundContact> contact;
  std::string detail;
};
// parentFaceAccepted asserts the caller ran 004a0aa0's parent face filters.
// This one-face result does not establish complete world collision coverage.
WheelGroundContactResult makeWheelGroundContact(
    std::unique_ptr<SpatialGroundFace> face, std::shared_ptr<physics::Body> root,
    std::shared_ptr<const physics::BodyProto> rootPrototype,
    std::shared_ptr<physics::Body> wheel,
    std::shared_ptr<const physics::BodyProto> wheelPrototype, bool parentFaceAccepted);

