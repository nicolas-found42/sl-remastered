#pragma once
#include "physics_body.hpp"
#include "resource_world.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// 0053dee0 loads PHYS records; 0049fa60 uses each face as a material word,
// three vertex indices and a normal. Positions are stored as three floats.
// No resource-node transforms, material lookup or collision behavior is
// applied.
struct GamePhysicsTriangle {
  uint32_t materialWord = 0;
  std::array<uint32_t, 3> vertices{};
  std::array<float, 3> normal{};
};
struct GamePhysicsGeometry {
  std::vector<std::array<float, 3>> vertices;
  std::vector<GamePhysicsTriangle> triangles;
};
bool decodeGamePhysicsGeometry(const GamePhysicsChunk &chunk,
                               GamePhysicsGeometry &out, std::string &error);

// Bounded 0049fa60 face test: the directed segment must cross from the
// non-positive to positive side of the stored normal. An in-triangle start
// on the plane can produce Hit with fraction zero. Near-coplanar crossings
// use stock clipping; invalid geometry remains Unsupported.
enum class GamePhysicsHitStatus { Miss, Hit, Unsupported };
struct GamePhysicsHit {
  GamePhysicsHitStatus status = GamePhysicsHitStatus::Unsupported;
  float fraction = 0;
  std::array<float, 3> point{}, normal{};
  uint32_t materialWord = 0;
};
GamePhysicsHit queryGamePhysicsFace(const GamePhysicsGeometry &geometry,
                                    size_t face,
                                    const std::array<float, 3> &start,
                                    const std::array<float, 3> &end);

enum class ParentFaceMask { Accepted, Rejected, Unsupported };
// 004a0aa0 parent mask for a root box or compound mesh, including sweep
// and orientation. Inherited rejection is the upstream face mask entry.
// This does not establish complete world traversal or pair registration.
ParentFaceMask queryParentFace(const GamePhysicsGeometry &geometry, size_t face,
                               const physics::Body &parent,
                               const ResourceBounds &nodeBounds,
                               bool inheritedRejected);

// 004b3850 directed neighbor membership transition for two extant records.
// Both node bounds and their sector identity are supplied by the manager.
// This does not enumerate records or establish complete neighbor coverage.
enum class SectorNeighborTransition { Keep, Add, Remove, Unsupported };
SectorNeighborTransition updateSectorNeighbor(bool linked, bool sameSector,
                                              const ResourceBounds &source,
                                              const ResourceBounds &target);
