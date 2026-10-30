#pragma once

// The game's own rigid body core, as read from the stock executable.
// Every function names the stock address it follows. Arithmetic is plain
// float in the stock operation order; the original is x87 (80-bit
// intermediates), so results can differ in the last bits. See
// docs/physics-evidence.md for what is verified and what is not.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace physics {

struct CollisionCore;

using Vec3 = std::array<float, 3>;
// Quaternion stored x, y, z, w (w last), as the stock 0054fb50/0054f760 do.
using Quat = std::array<float, 4>;
// 3x3 matrix stored with a row stride of four floats: m[0..2], m[4..6],
// m[8..10]; the entries 3, 7 and 11 are padding. This is the stock layout.
using Mat34 = std::array<float, 12>;

// Constants read from the stock executable data (see the evidence page).
namespace k {
inline constexpr float zero = 0.0f;               // 005e73cc
inline constexpr float one = 1.0f;                // 005f08f0
inline constexpr float half = 0.5f;               // 005f09d0
inline constexpr float tenth = 0.1f;              // 005f08e8
inline constexpr float skin = 0.05f;              // 005f0c98 (bit pattern 3d4ccccd)
inline constexpr float linearDragPerMass = -0.005f;   // 005f1440 (bba3d70a)
inline constexpr float angularDragPerMass = -0.01f;   // 005f0e54 (bc23d70a)
inline constexpr float minimumStep = 1e-4f;       // 005f09d4 (38d1b717)
inline constexpr float sixth = 0.16666667f;       // immediate 3e2aaaab in 004a7c10
inline constexpr float third = 0.33333334f;       // immediate 3eaaaaab in 004a7c10
inline constexpr float angularDampScale = 100.0f; // 005f09c8
inline constexpr float angularSpeedLimitScale = 0.0025f; // 005f1468 (3b23d70a)
inline constexpr float linearSpeedLimitSquared = 22500.0f; // 005f1464 (46afc800)
inline constexpr float linearSpeedLimit = 150.0f; // 005f0cb0
inline constexpr float sphereInertiaFactor = 2.5f;    // 005f0c0c
inline constexpr float sphereVolumeFactor = 4.1887903f; // 005f1460 (40860a92)
inline constexpr float cylinderAxisFactor = 2.0f; // 005f0ab8
inline constexpr float quarter = 0.25f;           // 005f0c80
inline constexpr float twelfth = 0.083333336f;    // 005f145c (3daaaaab)
inline constexpr float boxInertiaFactor = 3.0f;   // 005f0b08
inline constexpr float boxVolumeFactor = 8.0f;    // 005f0da0
inline constexpr float minusOne = -1.0f;          // 005f0c70
inline constexpr float restSmoothKeep = 0.9f;     // 005f08ec (3f666666)
inline constexpr float restAngularSq = 0.001f;    // 005f09cc (3a83126f)
inline constexpr float restForceSq = 0.01f;       // 005f0c20 (3c23d70a)
inline constexpr float defaultFriction = 0.6f;    // immediate 3f19999a, shape +0x50
inline constexpr float defaultBounce = 0.2f;      // immediate 3e4ccccd, shape +0x58
inline constexpr float airDensity = 1.2928f;       // world ctor 004a5580, 3fa57a78
inline constexpr float gravityY = -9.81f;         // world ctor 004a5580, c11cf5c3
}  // namespace k

// ---------------------------------------------------------------- vectors
Mat34 matrixIdentity();                 // 0054e1d0
Mat34 matrixZero();                     // 0054e1b0
Mat34 matrixTranspose(const Mat34& a);  // 0054e4a0 (copy form)
Mat34 matrixMultiply(const Mat34& a, const Mat34& b);  // 0054e9c0, a*b
Mat34 matrixInverse(const Mat34& a);    // 0054e510 with 0054e3b0
Mat34 matrixFromQuat(const Quat& q);    // 0054eda0
Quat quatFromMatrix(const Mat34& rotation); // 0054ff20
Quat quatMultiply(const Quat& a, const Quat& b);  // 0054fb50, a*b
Quat quatNormalize(const Quat& q);      // 0054f810
Quat quatInverse(const Quat& q);        // 0054fa70
Vec3 matrixTransformRows(const Mat34& m, const Vec3& v);  // 00436880, rows . v
Vec3 transformPoint(const Vec3& pos, const Mat34& rot, const Vec3& local);  // 004b1460

// ----------------------------------------------------------- geometry
// Shape type codes, the switch in 0049ab60 and 004a6010.
enum class ShapeType : uint32_t {
  Empty = 0,      // no collision core (the world's root bodies use it)
  Sphere = 1,     // core is one point, radius in shape +0x10 (004ac370)
  CylinderX = 2,  // core is a segment along X (004a5e80 case 2, 004ac440)
  CylinderY = 3,
  CylinderZ = 4,
  Box = 5,        // 8-vertex polyhedron (004ac1c0)
  Mesh = 6,       // mesh geometry (004ac150 / 004ac330)
  Hull = 7        // vertex-cloud body: only the bounding sphere is used
};

struct MeshTriangle {
  std::array<uint32_t, 3> indices{};
  Vec3 normal{};
};

// The 0xac-byte shape record built by 0049ab60 (only the fields that routine
// writes). Offsets in comments are from the start of the stock record.
struct Shape {
  ShapeType type = ShapeType::Empty;  // +0x08
  float boundRadius = 0;              // +0x0c bounding sphere radius
  float radius = 0;                   // +0x10 skin radius of the core
  float extent[3] = {0, 0, 0};        // +0x14..+0x1c bounding half sizes
  float centerOffset[3] = {0, 0, 0};  // +0x40..+0x48 set to zero by inertia code
  float volume = 0;                   // +0x4c
  float friction = k::defaultFriction;  // +0x50
  float bounce = k::defaultBounce;      // +0x58
  float dim[3] = {0, 0, 0};           // +0x9c..+0xa4 primary parameters
  std::vector<Vec3> vertices;         // +0xa0 count / +0xa4 pointer for types 6, 7
  std::vector<MeshTriangle> triangles; // raw mesh faces at shape +0xa8
  std::shared_ptr<const CollisionCore> core;
  uint32_t flags = 0;                 // +0x34 (bit 0: polyhedron built)
};

// Prototype flag bits (record offset +0x9c of the 0xa0-byte record).
inline constexpr uint32_t kProtoCreated = 0x1;   // set by 0049ab60
inline constexpr uint32_t kProtoNoGravity = 0x2; // copied to body flag 2 (0049b0b0)
inline constexpr uint32_t kProtoStatic = 0x4;    // zero mass
inline constexpr uint32_t kProtoBit8 = 0x8;      // copied to body flag 0x20
inline constexpr uint32_t kProtoNoRotation = 0x40; // copied to body flag 0x100
inline constexpr uint32_t kProtoBuoyant = 0x80;
inline constexpr uint32_t kProtoRoot = 0x100;
inline constexpr uint32_t kProtoSimple = 0x200;
inline constexpr uint32_t kProtoWater = 0x800;
inline constexpr uint32_t kProtoAlways = 0x400;  // set by 0049ab60 after the shape is linked

// The 0xa0-byte rigid prototype built by 0049ab60 ("mass + shape").
struct BodyProto {
  float invMass = 0;                  // +0x14
  Mat34 invInertia{};                 // +0x18 body-space inverse inertia (004a6010)
  Mat34 inertia{};                    // +0x48 its inverse
  Shape shape;                        // +0x78 pointer in stock
  float linearDrag = 0;               // +0x80 = mass * -0.005 (negative means on)
  float angularDrag = 0;              // +0x84 = mass * -0.01
  float volume = 0;                   // +0x88 copy of shape volume (or 0)
  uint32_t flags = 0;                 // +0x9c
  bool massPropertiesSupported = true;  // false when a Mesh type needs 004aa330
};

// 0049ab60(mass, type, p4, p5, p6, meshVertices). meshVertices stands for the
// mesh handle argument (param_7): nullptr is a zero handle. The stock call
// loads vertices through 004b5c30; this port takes them already decoded.
// Returns nullopt only for an unknown type code (stock falls out of the
// switch and returns the prototype with its defaults; we refuse instead).
std::optional<BodyProto> createBodyProto(float mass, uint32_t type, float p4, float p5,
                                         float p6, const std::vector<Vec3>* meshVertices,
                                         std::string& error);

// ---------------------------------------------------------------- bodies
// One 0xd4-byte state record. Offsets in comments are stock offsets.
struct State {
  Vec3 pos{};              // 0x00
  Vec3 vel{};              // 0x0c
  Vec3 shift{};            // 0x18 added to the position at the end of every step
  float time = 0;          // 0x24
  Vec3 force{};            // 0x28 force accumulator
  float unknown34[2] = {0, 0};  // 0x34 not read by anything ported so far
  uint32_t stepFlags = 0;  // 0x3c bit 0 set once this record was produced by a step
  Mat34 rot = matrixIdentity();  // 0x40 rotation matrix
  Vec3 omega{};            // 0x70 angular velocity
  Vec3 angMom{};           // 0x7c angular momentum
  Vec3 torque{};           // 0x88 torque accumulator
  Quat quat{0, 0, 0, 1};   // 0x94
  Mat34 invInertiaWorld{}; // 0xa4
};
static_assert(sizeof(State) == 0xd4, "stock state record is 0xd4 bytes");
static_assert(offsetof(State, pos) == 0x00 && offsetof(State, vel) == 0x0c &&
              offsetof(State, shift) == 0x18 && offsetof(State, time) == 0x24 &&
              offsetof(State, force) == 0x28 && offsetof(State, stepFlags) == 0x3c &&
              offsetof(State, rot) == 0x40 && offsetof(State, omega) == 0x70 &&
              offsetof(State, angMom) == 0x7c && offsetof(State, torque) == 0x88 &&
              offsetof(State, quat) == 0x94 && offsetof(State, invInertiaWorld) == 0xa4,
              "state field offsets follow 004a7c10");

// Body flag bits (stock body offset +0x58).
inline constexpr uint32_t kBodyActive = 0x1;
inline constexpr uint32_t kBodyNoGravity = 0x2;
inline constexpr uint32_t kBodyChild = 0x4;     // attached to a parent (kinematic)
inline constexpr uint32_t kBodyHasChildren = 0x8;
inline constexpr uint32_t kBodyBit10 = 0x10;
inline constexpr uint32_t kBodyAsleep = 0x80;
inline constexpr uint32_t kBodyNoRotation = 0x100;

struct RelativeBodyPose {
  Vec3 position{};
  RelativeBodyPose() = default;
  RelativeBodyPose(const Vec3& point, const Quat& quaternion) : position(point) {
    setOrientation(quaternion);
  }
  const Quat& orientation() const { return orientation_; }
  const Mat34& rotation() const { return rotation_; }
  void setOrientation(const Quat& quaternion) {
    orientation_ = quaternion;
    rotation_ = matrixFromQuat(quaternion);
  }
  // 00492030 writes the matrix and then derives its quaternion (0054ff20).
  void setRotation(const Mat34& matrix) {
    rotation_ = matrix;
    orientation_ = quatFromMatrix(matrix);
  }
private:
  Quat orientation_{0, 0, 0, 1};
  Mat34 rotation_ = matrixIdentity();
};
struct Body;
struct BodyAttachment {
  Body* parent;
  RelativeBodyPose relative;
};

struct Body {
  const BodyProto* proto = nullptr;  // +0x5c
  uint32_t flags = 0;                // +0x58
  uint32_t current = 0;              // +0x20c index of the state being built from
  State state[2];                    // +0x64 and +0x138
  Vec3 activity{};                   // +0x0c smoothed force used by the rest test
  std::optional<BodyAttachment> attachment;
};

// 0049b0b0: build a body from a prototype. Both states start equal. The body
// flags come from the prototype flags; the world inverse inertia is derived
// with 004a76d0. A supplied orientation is converted to a quaternion and back
// as stock does, without a normalization step.
Body createBody(const BodyProto& proto, const Vec3& pos,
                const Mat34* orientation = nullptr, float creationTime = 0);

// 004a8eb0 / 004a76d0: invInertiaWorld = R * invInertia * R^T.
void updateWorldInertia(const BodyProto& proto, State& s);

// 004a6600: add a force at an offset point; torque += offset x force.
void addForce(Body& body, const Vec3& force, const Vec3* offset);

struct WorldParams {
  bool gravityEnabled = true;                // world +0x78
  Vec3 gravity{0.0f, k::gravityY, 0.0f};     // world +0x104
  float airDensity = k::airDensity;          // world +0x1b4
};

// First part of 004a3f90: drag, gravity and buoyancy forces for a body that
// is active, awake, not static and (child or has mass). Water-plane and
// clip-plane contact in the rest of that routine is not ported. Returns false
// when the gate rejects the body.
bool accumulateWorldForces(const WorldParams& world, Body& body);

// 004a7c10: integrate `in` over dt into `out`. Supports the RK4 branch, the
// no-rotation branch and the simple branch. Returns false (leaving `out`
// untouched) for a child body, which needs the parent chain (not ported).
bool integrate(const BodyProto& proto, uint32_t bodyFlags, const State& in, State& out,
               float dt);

// 004a3bc0 for a lone root body: integrate state[current] into
// state[current ^ 1] under the stock skip conditions. Returns true when the
// body was integrated.
bool stepBody(Body& body, float dt);

enum class CommitResult { Flipped, WentToSleep, Unchanged };
// 004a4ed0 for a lone root body. Unchanged: the outer gate failed (inactive
// body, or no state produced by a step, or static). Otherwise the rest test
// runs; WentToSleep means stock would call 004a7a80, which is not ported, and
// the buffers are not flipped. Flipped: the other buffer became current and
// its force, shift and torque were cleared and its world inertia refreshed.
CommitResult commit(Body& body);
// State/flag portion of004a7a80. Tree traversal and world-list moves belong
// to the caller; this copies the cleared current state to the other buffer.
void sleepBodyState(Body& body);

// The part of 004a5190 that runs when no contact and no joint exists: time
// accumulation, forces, integration, commit. Collision detection (00498dd0),
// contact handling (004a2d10, 004a35d0) and the constraint solver (004a3900)
// are not ported.
enum class FreeBodyStepResult { Advanced, Accumulated, Unsupported };
struct World {
  WorldParams params;
  std::vector<Body*> bodies;  // stock keeps a linked list under the root body
  float time = 0;             // world +0x00
  float pending = 0;          // world +0x04
  float lastStep = 0;         // world +0x0c
  uint32_t stepsDone = 0;     // world +0x2c counter of integrated bodies
  // Accumulated keeps a sub-minimum timestep for a later call. Unsupported
  // leaves the world untouched.
  FreeBodyStepResult stepFreeBodies(float dt);
};

// -------------------------------------------------------------- collision
// The closest core points of two sphere cores (single-vertex polyhedra), the
// 0x101 case of 004af970. `normal` points from b toward a and is left as the
// raw difference when the distance is zero.
struct CorePair {
  float distance = 0;
  Vec3 pointA{};  // closest point in A's local coordinates
  Vec3 pointB{};  // closest point in B's local coordinates
  Vec3 normal{};
};
CorePair sphereCoreClosest(const State& a, const State& b);

// First test 004a2d10 applies to a tracked pair once the core distance is
// known: the pair is dropped when the distance is exactly zero or beyond the
// two skin radii plus 0.05 (005f0c98). Everything after that test (the
// bisection on time, the contact point and normal updates) is not ported.
bool pairDropped(float distance, float radiusA, float radiusB);

}  // namespace physics
