#pragma once
#include "owner_links.hpp"
#include "raster3d.hpp"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

class ResourceWorld;
struct SceneRenderLight;
class Render2D;
struct ResourceNode;
struct ViewportInstance;
struct CameraFog;
struct GamePolygonGroup;
struct PreparedSpatialGamePayload;
class SceneMaterialResources;

// Converts the packed POLY vertex records to the arrays consumed by Raster3D.
bool sceneGroundMesh(const GamePolygonGroup &group, ScxMesh &mesh,
                     std::string &error);

// Stock group-local byte indices select mesh.bonePalette, not the skeleton
// directly. There are 0..3 explicit weights plus the unclamped residual.
// Palette matrices are column-major world * inverseBind, with metre translations;
// FUN_00503400 scales raw SCX positions by 0.1, so this API restores raw units.
// Indexed weighted shader normals are normalized; the one-influence shader
// and the fixed-function blend retain their directly transformed normals.
// Invalid/missing skin maps fail rather than silently using an identity palette.
bool sceneSkinMesh(const ScxMesh &mesh,
                   const std::vector<std::array<float, 16>> &palette,
                   std::vector<float> &positions, std::vector<float> &normals,
                   std::string &error);

// Stock FUN_0052df50 marks a render object whose type-14 record flags have bit
// 0 set (every skydome material carries "flags 1"); FUN_00527d40 then queues
// its batches only on the camera's background list (+0x548), which
// FUN_0050eba0 draws before every other list.
bool sceneBackgroundMaterial(uint32_t materialFlags);
// The background list's begin (FUN_005151e0 -> FUN_004bb5f0(0,0) and
// FUN_004bb050): lighting off, the view keeps only its rotation, ZFUNC ALWAYS,
// no depth write, fog off. FUN_004bb120 restores the state afterwards.
void sceneApplyBackgroundPass(Raster3DDraw &draw);

// Converts a bound scene light into the raster world light (FUN_004bb7f0
// ambient/diffuse terms, direction inverted towards the light).
Raster3DLight sceneWorldRasterLight(const SceneRenderLight &light);

// The texture the draw samples for a car body slot (FUN_00467ff0 swap).
uint32_t scenePaintTexture(uint32_t materialId, uint32_t defaultTextureId,
                           uint32_t currentTextureId);

// The draw path's body-slot sample: the cfg texture is swapped for the
// class-selected paint one (FUN_00467ff0), every other slot samples its own.
const Raster3DTexture *
sceneBodyTexture(const std::function<const Raster3DTexture *(uint32_t)> &sample,
                 uint32_t materialId, uint32_t defaultTextureId,
                 uint32_t currentTextureId);

// Resolve a bound texture table slot, preserving native first-match replacement
// when multiple entries name the same old resource.
const Raster3DTexture *sceneBodyTexture(
    const std::function<const Raster3DTexture *(uint32_t)> &sample,
    const SceneMaterialResources &material, size_t textureIndex,
    uint32_t defaultTextureId, uint32_t currentTextureId);

class SceneRaster3D {
public:
  ~SceneRaster3D();
  void addObject(LinkOwner &owner);
  void remove(LinkOwner &owner);
  void draw(ResourceWorld &resources, Render2D &renderer,
            const std::vector<ResourceNode *> &ground,
            const std::array<float, 16> &view,
            const std::array<float, 16> &projection,
            const ViewportInstance &viewport, const CameraFog *fog,
            const Raster3DLight *worldLight, uint32_t globalEnvmapId = 0);

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
