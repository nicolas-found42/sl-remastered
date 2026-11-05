#pragma once
#include "game_physics_geometry.hpp"
#include "physics_contact_geometry.hpp"
#include "physics_suspension.hpp"
#include <optional>
#include <string>
#include <vector>

// Side-effect-free preparation for the passive 0053dee0 subset. It retains
// child descriptors and complete POLY data, and decodes PHYS. EXTP and
// zero-game-type actions are not performed here: callers must apply them
// before publishing the load. Nonzero factory actions are unsupported.
struct PreparedSpatialGamePayload {
  std::vector<GameResourceChild> children;
  std::optional<GamePhysicsGeometry> physics;
  std::optional<GamePolygonChunk> polygons;
};
bool prepareSpatialGamePayload(const GameResourcePayload &payload,
                               PreparedSpatialGamePayload &out,
                               std::string &error);

// Retained PHYS candidates from the packed type-1 tree. This is not a complete
// collision query: dynamic bodies, material resolution, and solver contact
// assignment are not represented, so absence of candidates cannot mean clear.
struct SpatialPhysicsCandidate {
  uint32_t resourceId = 0;
  std::shared_ptr<const PreparedSpatialGamePayload> payload;
  // Borrowed while ResourceWorld remains unchanged; payload keeps PHYS alive.
  ResourceNode *node = nullptr;
};
struct SpatialPhysicsCandidates {
  std::vector<SpatialPhysicsCandidate> candidates;
  bool complete = false;
  // Only the requested passive tree/bounds were traversed successfully.
  bool passiveTraversalComplete = false;
  std::string detail;
};
SpatialPhysicsCandidates
queryPassiveSpatialCandidates(ResourceWorld &resources, ResourceNode &root,
                              const std::array<float, 3> &minimum,
                              const std::array<float, 3> &maximum);

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
  const LinkOwner *leafOwner() const { return leaf_.owner; }

private:
  SpatialGroundFace() = default;
  friend std::unique_ptr<SpatialGroundFace>
  resolveSpatialGroundFace(ResourceWorld &, ResourceNode &, size_t,
                           std::string &);
  OwnerLink leaf_, sector_, body_;
  std::shared_ptr<PreparedSpatialGamePayload> payload_;
  std::shared_ptr<GroundPreparation> ground_;
  size_t faceIndex_ = 0;
  size_t materialIndex_ = 0;
};
std::unique_ptr<SpatialGroundFace>
resolveSpatialGroundFace(ResourceWorld &resources, ResourceNode &leaf,
                         size_t faceIndex, std::string &error);

enum class WheelGroundContactStatus { Hit, Miss, Unsupported };
struct WheelGroundContactResult;
class WheelGroundContact {
public:
  const physics::SuspensionContactCandidate *candidate() const;
  // Retained selected contact usable before commit or immediately after the
  // predicted wheel state becomes current. A subsequent prediction invalidates
  // this witness; callers must retain the selected witness, not any hit.
  const physics::SuspensionContactCandidate *tireCandidate() const;
  const GroundMaterial *groundMaterial() const;
  // The selected contact of a persistent suspension contact: static ground
  // data that stays valid after the wheel moves, as long as the face is
  // loaded and the bodies are the ones it was made for. The pose-gated
  // tireCandidate() is the strict form used by the unit-level checks.
  const physics::SuspensionContactCandidate *retainedCandidate() const;
  const GroundMaterial *retainedMaterial() const;
  // Why tireCandidate() is null (diagnostics): the first failing check.
  const char *tireBlock() const;
  // Borrowed face identity remains owned by this witness; check valid() before use.
  const SpatialGroundFace* groundFace() const { return face_.get(); }

private:
  bool validGeometry() const;
  friend WheelGroundContactResult makeWheelGroundContact(
      std::unique_ptr<SpatialGroundFace>, std::shared_ptr<physics::Body>,
      std::shared_ptr<const physics::BodyProto>, std::shared_ptr<physics::Body>,
      std::shared_ptr<const physics::BodyProto>, bool);
  std::unique_ptr<SpatialGroundFace> face_;
  std::shared_ptr<physics::Body> root_, wheel_;
  std::shared_ptr<const physics::BodyProto> rootPrototype_, wheelPrototype_;
  physics::Vec3 predictedPosition_{};
  physics::Mat34 predictedRotation_{};
  physics::Vec3 priorPosition_{};
  physics::Mat34 priorRotation_{};
  float predictedTime_ = 0, priorTime_ = 0;
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
WheelGroundContactResult
makeWheelGroundContact(std::unique_ptr<SpatialGroundFace> face,
                       std::shared_ptr<physics::Body> root,
                       std::shared_ptr<const physics::BodyProto> rootPrototype,
                       std::shared_ptr<physics::Body> wheel,
                       std::shared_ptr<const physics::BodyProto> wheelPrototype,
                       bool parentFaceAccepted);

// Bounded static-ground traversal for one attached wheel. The collector visits
// faces in tree/record order (004a1560 -> 004a0aa0 -> 0049fa60). A complete
// passive traversal does not cover dynamic bodies, other roots, or solver
// registration, and is not a clearance result.
struct WheelGroundContacts {
  std::vector<std::unique_ptr<WheelGroundContact>> contacts;
  bool passiveTraversalComplete = false;
  std::string detail;
};
WheelGroundContacts collectWheelGroundContacts(
    ResourceWorld &resources, ResourceNode &root,
    std::shared_ptr<physics::Body> parent,
    std::shared_ptr<const physics::BodyProto> parentPrototype,
    std::shared_ptr<physics::Body> wheel,
    std::shared_ptr<const physics::BodyProto> wheelPrototype);

struct CarPhysics;
struct CarBodyObservation;
struct GroundRuntime;
// A geometry candidate, not an admitted contact. Owns the ground face and car;
// observation detects part removal/reindexing. CarWorldStepper owns admission.
// Check valid() before accessing pair after any world or compound mutation.
struct CarGroundFacePair {
  std::shared_ptr<CarPhysics> car;
  std::shared_ptr<CarBodyObservation> bodyObservation;
  std::shared_ptr<GroundRuntime> ground;
  std::unique_ptr<SpatialGroundFace> face;
  physics::Body *body = nullptr;
  const physics::BodyProto *prototype = nullptr;
  physics::GroundFacePair pair;
  bool valid() const;
};
struct CarWheelGroundHit {
  physics::Body *wheel = nullptr;
  std::unique_ptr<WheelGroundContact> contact;
};
struct CarGroundContacts {
  std::vector<std::unique_ptr<CarGroundFacePair>> pairs;
  std::vector<CarWheelGroundHit> wheels;
  // Covers only supported passive tree traversal and attached-body filters.
  bool passiveTraversalComplete = false;
  // Dynamic roots and other spatial roots are not included.
  bool complete = false;
  std::string detail;
};
CarGroundContacts collectCarGroundContacts(ResourceWorld &resources,
                                           ResourceNode &spatialRoot,
                                           std::shared_ptr<CarPhysics> car);
