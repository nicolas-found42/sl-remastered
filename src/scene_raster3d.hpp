#pragma once
#include "owner_links.hpp"
#include "raster3d.hpp"
#include <memory>
#include <unordered_map>
#include <vector>

class ResourceWorld;
class Render2D;
struct ResourceNode;
struct ViewportInstance;
struct CameraFog;
struct GamePolygonGroup;
struct PreparedSpatialGamePayload;

// Converts the packed POLY vertex records to the arrays consumed by Raster3D.
bool sceneGroundMesh(const GamePolygonGroup &group, ScxMesh &mesh,
                     std::string &error);

class SceneRaster3D {
public:
  ~SceneRaster3D();
  void addObject(LinkOwner &owner);
  void remove(LinkOwner &owner);
  void draw(ResourceWorld &resources, Render2D &renderer,
            const std::vector<ResourceNode *> &ground,
            const std::array<float, 16> &view,
            const std::array<float, 16> &projection,
            const ViewportInstance &viewport, const CameraFog *fog);

private:
  struct GroundMeshCache {
    std::weak_ptr<PreparedSpatialGamePayload> payload;
    std::vector<ScxMesh> meshes;
  };
  std::unordered_map<uint32_t, GroundMeshCache> groundMeshes_;
  std::vector<std::unique_ptr<OwnerLink>> objects_;
  std::unordered_map<uint32_t, Raster3DTexture> textures_;
  Raster3D raster_;
};
