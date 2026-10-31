#pragma once

#include "scene_2d.hpp"
#include "world_model.hpp"

struct VmArena;
struct VmStaticFields;
struct CameraTraversalParams {
  std::array<float, 3> cameraPosition;
  float cameraFactor;
  float lodBias;
  float lodAmplitude;
  float worldScale;
  float threshold;
  std::optional<int32_t> planeTest;
  std::optional<bool> frustumChecks;
  std::optional<ResourceBounds> cameraBound;
  // Five post-adjustment camera planes from +0xdc through +0x11c.
  std::optional<std::array<std::array<float, 4>, 5>> frustumPlanes;
};
// Assemble traversal inputs from the current camera frame; threshold,
// worldScale and frustumChecks are live renderer values supplied by caller.
std::optional<CameraTraversalParams>
cameraTraversalParams(const CameraResource &camera,
                      const CameraViewState &state,
                      const std::array<float, 16> &boneWorldMatrix,
                      float worldScale, float threshold, bool frustumChecks);

// Type-1 virtual setters 0053eff0/0053f040, followed by the 00513cc0 stamp.
bool updateCameraType1Visibility(WorldNode &node, uint32_t frame, float input);

class CameraType1Records {
public:
  ~CameraType1Records();
  CameraType1Records() = default;
  CameraType1Records(const CameraType1Records &) = delete;
  CameraType1Records &operator=(const CameraType1Records &) = delete;
  // Returns true when the unchanged root record can be skipped.
  bool beginVisit(LinkOwner &owner, bool isRoot, bool rootDirty,
                  bool fullyInside = true);
  void finishVisit(LinkOwner &owner, bool allComplete);
  bool hasRecord(const LinkOwner &owner) const;
  // FUN_00511e60: the camera's record for a hidden node is destroyed (stock
  // swaps the last record into its slot). Returns false when none exists.
  bool removeRecord(const LinkOwner &owner);

private:
  struct Record {
    OwnerLink owner;
    uint32_t flags = 1;
  };
  std::vector<std::unique_ptr<Record>> records_;
};

// Bounded 00510fc0/00511180 path. Handles loaded zero-POLY type-1 nodes with
// mode-2 bounds or fully contained mode-1 boxes, and runtime type-3 nodes.
// Mode-2 intersecting bounds retain partial traversal; other intersecting
// frustum bounds and cleanup remain unsupported.
bool traverseCameraCandidates(
    GameWorld &world, LinkOwner &root, SceneCameraCandidates &candidates,
    const CameraTraversalParams &params, std::string &error,
    CameraType1Records *type1Records = nullptr,
    ResourceWorld *resources = nullptr, VmArena *arena = nullptr,
    const std::vector<VmStaticFields> *staticFields = nullptr);
