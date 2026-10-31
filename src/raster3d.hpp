#pragma once

#include "scx.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// A software 3D rasterizer for the host. It draws into an RGBA8 framebuffer
// owned by Raster3D. Render2D then uploads that framebuffer as one texture and
// presents it (Raster3D::present). Nothing here changes Render2D. See
// docs/render3d-evidence.md for what is stock-derived and what is a host
// choice.
//
// Matrix convention: row vectors, as everywhere else in the host (point * model
// * view * projection), with the translation in elements 12..14. The projection
// is the stock one from cameraViewState (right-handed, D3D clip volume: -w <= x
// <= w, -w <= y <= w, 0 <= z <= w).

class Render2D;

// RGBA8 texture. File loaders preserve stored mip levels; build generates a
// box-filtered chain for callers that supply only raw RGBA pixels.
struct Raster3DTexture {
  struct Level {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
  };
  std::vector<Level> levels; // levels[0] is the full-size image
  bool hasAlpha = false;     // some texel alpha is below 255
  static bool build(uint32_t width, uint32_t height, const uint8_t *rgba,
                    size_t size, Raster3DTexture &out, std::string &error);
};

// Decodes a .dds (DXT1, DXT3, DXT5; first level only) or .ptx file into RGBA8
// pixels.
struct Raster3DPixels {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba;
};
bool raster3dDecodeDxt(const uint8_t *data, size_t size, Raster3DPixels &out,
                       std::string &error);
bool raster3dDecodeDxtTexture(const uint8_t *data, size_t size,
                              Raster3DTexture &out, std::string &error);
bool raster3dLoadTextureFile(const std::filesystem::path &path,
                             Raster3DTexture &out, std::string &error);

enum class Raster3DBlend : uint8_t {
  Opaque = 0,
  AlphaTest = 1,
  AlphaBlend = 2
};
// Which on-screen winding is discarded. Clockwise and counter-clockwise are as
// seen in the final image.
enum class Raster3DCull : uint8_t {
  None = 0,
  Clockwise = 1,
  CounterClockwise = 2
};
enum class Raster3DFilter : uint8_t { Point = 0, Bilinear = 1, Trilinear = 2 };
enum class Raster3DAddress : uint8_t { Wrap = 0, Clamp = 1 };

struct Raster3DMaterial {
  Raster3DBlend blend = Raster3DBlend::Opaque;
  uint8_t alphaRef = 0x38; // AlphaTest passes when alpha*255 >= alphaRef
  Raster3DCull cull = Raster3DCull::None;
  bool depthTest = true; // z <= stored z
  bool depthWrite = true;
  const Raster3DTexture *texture = nullptr; // null: flat diffuse colour
  Raster3DAddress addressU = Raster3DAddress::Wrap,
                  addressV = Raster3DAddress::Wrap;
  Raster3DFilter filter = Raster3DFilter::Bilinear;
  std::array<float, 4> diffuse{{1, 1, 1, 1}}; // RGBA multiplier
  bool useVertexColor =
      false; // multiply by the draw's vertex colours when they are present
  bool perspectiveCorrect = true; // false is a diagnostic: affine
                                  // interpolation, to show the difference
  // Linear camera-space depth fog; disabled when far <= near.
  float fogNear = 0, fogFar = 0;
  std::array<float, 3> fogColor{{0, 0, 0}};
};

struct Raster3DViewport {
  float left = 0, top = 0, width = 1, height = 1;
}; // fractions of the output

struct Raster3DLight {
  bool enabled = false;
  std::array<float, 3> toLight{
      {0.4f, 0.8f, 0.5f}}; // world-space direction towards the light
  float ambient = 0.45f, diffuse = 0.65f;
};
struct Raster3DFixedDraw;

struct Raster3DDraw {
  const float *positions = nullptr; // vertexCount * 3
  size_t vertexCount = 0;
  const float *uvs = nullptr;     // vertexCount * 2, optional
  const float *normals = nullptr; // vertexCount * 3, optional (used only when
                                  // the light is enabled)
  const uint8_t *colors =
      nullptr; // vertexCount * 4 bytes, D3DCOLOR order (B, G, R, A), optional
  const uint32_t *indices = nullptr;
  size_t indexCount = 0; // multiple of 3
  float positionScale =
      1.0f; // stock mesh scale is 0.1 (scene_2d.cpp, sceneDrawCameraMesh)
  std::array<float, 16> model{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  std::array<float, 16> view{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  std::array<float, 16> projection{
      {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  Raster3DViewport viewport;
  Raster3DMaterial material;
  Raster3DLight light;
  const Raster3DFixedDraw *fixed =
      nullptr; // explicit multi-stage fixed function
};

struct Raster3DStats {
  uint64_t trianglesSubmitted = 0, trianglesRejectedOutside = 0,
           trianglesClipped = 0;
  uint64_t trianglesCulled = 0, trianglesDegenerate = 0,
           trianglesRasterized = 0;
  uint64_t pixelsTested = 0, pixelsDepthRejected = 0, pixelsAlphaRejected = 0,
           pixelsWritten = 0;
};

class Raster3D {
public:
  // Creates a framebuffer of the given size; returns false with an error for an
  // invalid size.
  bool resize(uint32_t width, uint32_t height, std::string &error);
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255,
             float depth = 1.0f);
  // Draws indexed triangles. Fails (and draws nothing) on invalid input or
  // unsupported states.
  bool draw(const Raster3DDraw &draw, std::string &error);
  const std::vector<uint8_t> &pixels() const {
    return color_;
  } // RGBA, row 0 at the top
  const std::vector<float> &depth() const { return depth_; }
  const Raster3DStats &stats() const { return stats_; }
  void resetStats() { stats_ = {}; }
  // Uploads the framebuffer through Render2D and draws it over the whole
  // output. The caller owns clear() and present() of the Render2D. The texture
  // is created on first use and re-created if the framebuffer size or the
  // Render2D changes.
  bool blit(Render2D &renderer, std::string &error);
  // Writes the framebuffer as a 24-bit BMP.
  bool saveBmp(const std::filesystem::path &path, std::string &error) const;

private:
  template <bool Fixed>
  bool drawImpl(const Raster3DDraw &draw, std::string &error);
  uint32_t width_ = 0, height_ = 0;
  std::vector<uint8_t> color_;
  std::vector<float> depth_;
  Raster3DStats stats_;
  Render2D *uploadRenderer_ = nullptr;
  size_t uploadTexture_ = 0;
  uint32_t uploadWidth_ = 0, uploadHeight_ = 0;
};

// Maps a point already divided by w (NDC) to output pixels with the stock
// viewport mapping (cameraViewportPoint in world_model.cpp), without its
// inside-the-volume restriction.
std::array<float, 2> raster3dViewportPoint(float ndcX, float ndcY,
                                           const Raster3DViewport &viewport,
                                           uint32_t outputWidth,
                                           uint32_t outputHeight);

// Homogeneous clip against the six D3D planes. Positions are clip-space (x, y,
// z, w). attr carries per-vertex attributes (up to 7 floats, linearly
// interpolated). Returns the polygon (0 to 9 vertices).
struct Raster3DClipVertex {
  std::array<float, 4> position{};
  std::array<float, 7> attr{};
};
std::vector<Raster3DClipVertex>
raster3dClipTriangle(const std::array<Raster3DClipVertex, 3> &triangle);

// How an SCX material becomes rasterizer states. The SCX "flags" word is the
// stock material type (004befc0 switch) and "functionWord" its option bits; see
// docs/render3d-evidence.md.
struct Raster3DScxOptions {
  bool allowApproximations = false; // accept types whose extra layers
                                    // (specular, env, mix) are not drawn
  bool ignoreUnknownFunctionBits = false;
};
struct Raster3DScxMaterial {
  Raster3DMaterial state;                   // texture left null
  std::optional<uint32_t> baseTextureIndex; // SCX textureIndex of the map that
                                            // supplies the base colour
  std::optional<uint32_t> baseSlot;
  std::vector<std::string> notes; // what was approximated or ignored
};
bool raster3dScxMaterial(const ScxMaterial &material,
                         const Raster3DScxOptions &options,
                         Raster3DScxMaterial &out, std::string &error);

// ---------------------------------------------------------------------------
// Fixed-function state record and stage combiner for the stock ground passes.
//
// Evidence: FUN_004bb5f0 (cached lighting-mode setter, all.c:135837),
// FUN_004e9c20 (per-stage device readback cache, all.c:166446), FUN_004c90a0
// (mode 3, all.c:144903), FUN_004dfe40 (mode 0, all.c:159151), FUN_005026da0
// region (engine FVF 0x15003f vertex loop, all.c:186172 and :214699). See
// docs/render3d-evidence.md.
//
// Contract: every stock-settable slot is std::optional. There is no modeled
// device default; consuming a slot that is unset (or marked unknown) fails
// explicitly. Mode application writes only the fields the evidenced stock
// branch writes and preserves every inherited field. Raw (state id, value)
// writes that have no typed meaning yet are preserved verbatim in `raw` so no
// observed state is silently dropped.

enum class Raster3DTexOp : uint32_t {
  // D3DTOP values the ground passes actually set (census evidence); anything
  // else
  // is preserved raw and fails the evaluator if consumed.
  Disable = 1,
  SelectArg1 = 2,
  SelectArg2 = 3,
  Modulate = 4,
  Add = 7,
  BlendTextureAlpha = 13,
  MultiplyAdd = 25,
};
enum class Raster3DTexArg : uint32_t {
  Diffuse = 0,
  Current = 1,
  Texture = 2,
  TFactor = 3,
  Specular = 4,
  Temp = 5,
  Complement = 0x10,     // 1 - x, D3DTA_COMPLEMENT
  AlphaReplicate = 0x40, // alpha replicated to RGB, D3DTA_ALPHAREPLICATE
};

struct Raster3DTexStage {
  std::optional<Raster3DTexOp> colorOp, alphaOp;
  std::optional<Raster3DTexArg> colorArg1, colorArg2, alphaArg1, alphaArg2;
  std::optional<Raster3DTexArg> colorArg0, alphaArg0;
  std::optional<Raster3DTexArg> resultArg; // D3DTSS_RESULTARG: CURRENT or TEMP
  bool textureBound = false; // an actual texture binding was recorded for this
                             // stage; never synthesized
  const Raster3DTexture *boundTexture = nullptr; // resolved binding identity
  // Slots the applied pass leaves branch-dependent (e.g. mode 3 with unobserved
  // capability): 1 colorOp, 2 alphaOp, 4 colorArg1, 8 colorArg2, 16 alphaArg1,
  // 32 alphaArg2, 64 colorArg0, 128 alphaArg0. Consuming a marked slot fails
  // even if an inherited value is present.
  uint32_t unknown = 0;
};

struct Raster3DFixedState {
  std::array<Raster3DTexStage, 8> stages;
  std::optional<bool>
      lighting; // render state 0x89, written through the FUN_004bb5f0 cache
  std::array<bool, 8> lightSlotEnabled{}; // tracked device light slots
                                          // (FUN_004bb5f0/FUN_004bb4b0 model)
  std::vector<std::array<uint32_t, 3>>
      raw; // {stage, state id, value} writes with no typed slot yet
  std::vector<std::array<uint32_t, 3>>
      samplerRaw; // stage, sampler state, value
  std::array<std::optional<std::array<float, 16>>, 8> textureTransforms;
  std::optional<std::array<uint32_t, 26>> deviceLight0;
  std::optional<std::array<uint32_t, 17>> deviceMaterial;
  std::optional<uint32_t> textureFactor;
  uint32_t dirtyFlags = 0; // stock dirty bits; FUN_004bb5f0 sets 0x08
};

// Mode 3 (FUN_004c90a0). Stock order: all mode-3 passes across POLY groups (the
// +474 collection) run before any mode-0 pass (+448).
struct Raster3DMode3Input {
  std::optional<uint32_t> capability00617928; // stock values 0, 1, 2
  std::optional<uint32_t>
      capability00617930; // selected when +0x4c texture exists
  std::optional<bool>
      materialMapAt4c; // whether the stock material's +0x4c texture is present
  std::optional<uint8_t>
      textureFactorAlpha; // result of the stock float-to-int conversion
  // Original material slot presence and flags are distinct from the actual
  // resolved device textures, which can be stock fallback resources.
  std::array<std::optional<bool>, 2> sourceTexturePresent{};
  std::array<uint8_t, 3> sourceAddressFlags{}; // +0x6c, +0x90, +0x48
  int passOrdinal = 0;                         // ordinal in the +474 collection
  std::array<const Raster3DTexture *, 8>
      textures{}; // actual bindings only; a null entry means unbound
};

// Exact device inputs prepared from one selected stock projector. Texture
// pointers are the actual post-resolution bindings; null is a real unbinding.
// Light/material words and matrices are the device values produced by
// FUN_004e8d20, not inferred defaults.
struct Raster3DProjectorBinding {
  std::array<std::optional<const Raster3DTexture *>, 2> textures{};
  std::array<std::optional<std::array<float, 16>>, 2> transforms{};
  std::optional<std::array<uint32_t, 26>> light0;
  std::optional<std::array<uint32_t, 17>> material;
};
// Mode 0 (FUN_004dfe40).
struct Raster3DMode0Input {
  // Projector selection status actually observed for this group; Unknown is not
  // a decision.
  enum class Projector : uint8_t {
    Unknown,
    None,
    Present
  } projector = Projector::Unknown;
  const Raster3DProjectorBinding *primary = nullptr;
  const Raster3DProjectorBinding *secondary = nullptr;
  std::optional<bool> alphaTestStateActive;
  std::optional<uint32_t> alphaReference;
  // FUN_004e7ad0 defaults: states 0x22, 0x1b, 0x13, 0x14, 0xab.
  std::optional<std::array<uint32_t, 5>> defaultBlendStates;
  // FUN_004e9460 default sampler states: MIN, MAG, MIP.
  std::optional<std::array<uint32_t, 3>> resetSampler;
  // Exact implementation of the device +0x10c(0,0) hook, called for each
  // selected projector in stock order. Required; no assumed no-op.
  void (*projectorModeHook)(void *, Raster3DFixedState &) = nullptr;
  void *projectorModeHookContext = nullptr;
  bool dualTexture = false; // actual DAT_00649fd4 == 1
  int passOrdinal = 0;      // ordinal in the +448 collection
};

class Raster3DGroundStateMachine {
public:
  // FUN_004bb5f0 semantics: no-op when the cached mode byte equals `enable` and
  // force is false; otherwise stores the mode, applies it, and sets dirty bit
  // 0x08. Turning lighting off disables the tracked light slots; turning it on
  // preserves slot state (the stock reapplies its recorded light configuration,
  // which the host does not model further).
  bool applyLightingMode(Raster3DFixedState &state, bool enable, bool force,
                         std::string &error);
  // Records evidenced stage and sampler writes for the represented branches.
  // Missing capability, material layout or bindings fails; this is not a
  // complete implementation of the associated render-state changes.
  bool applyMode3(Raster3DFixedState &state, const Raster3DMode3Input &input,
                  std::string &error);
  // Applies the evidenced mode-0 branch selected by the caller-observed
  // projector status and mono/dual setting; combinations the stock text does
  // not represent fail explicitly.
  bool applyMode0(Raster3DFixedState &state, const Raster3DMode0Input &input,
                  std::string &error);
  std::optional<uint8_t> cachedLightingMode() const { return cachedLighting_; }

private:
  std::optional<uint8_t> cachedLighting_; // the +0x360 cache byte
};

// Engine ground vertex, FVF 0x15003f: 3f position, 3f normal, two packed words,
// three float2 UV sets; 56-byte stride (the FUN_005026da0 loop walks records of
// 14 floats). Packed colour split exactly as stock reads the POLY source words
// [6] and [7]:
//   diffuse  = word0 & 0xff000000
//   specular = (word0 & 0x00ffffff) | (word1 << 24)
struct Raster3DGroundVertex {
  std::array<float, 3> position, normal;
  uint32_t packedWord0 = 0, packedWord1 = 0;
  std::array<std::array<float, 2>, 3> uv;
  uint32_t diffusePacked() const { return packedWord0 & 0xff000000u; }
  uint32_t specularPacked() const {
    return (packedWord0 & 0x00ffffffu) | (packedWord1 << 24);
  }
};
// Decodes `count` 56-byte FVF 0x15003f records. Fails on a null data or a
// non-finite float.
bool raster3dGroundFvf(const void *data, size_t count,
                       std::vector<Raster3DGroundVertex> &out,
                       std::string &error);

// Per-fragment input of the fixed-function combiner. `current` is the running
// result (stage 0 starts from diffuse, D3D fixed-function convention).
struct Raster3DCombinerInput {
  std::array<float, 4> diffuse{{1, 1, 1, 1}};
  std::array<float, 4> specular{{0, 0, 0, 0}};
  std::array<float, 4> texture{
      {1, 1, 1, 1}}; // sampled texel of the stage's bound texture
  std::array<float, 4> factor{{1, 1, 1, 1}}; // D3DTA_TFACTOR source
  std::array<float, 4> temp{};
  bool tempDefined = false;
};
// Evaluates one texture stage against `current` (in/out). The color op runs on
// RGB with COLORARG1/2, the alpha op on alpha with ALPHAARG1/2. An unset or
// unknown-marked slot consumed here, an unbound texture sampled, or a raw-only
// op fails explicitly.
bool raster3dCombineStage(const Raster3DTexStage &stage, unsigned stageIndex,
                          const Raster3DCombinerInput &input,
                          std::array<float, 4> &current, std::string &error);

// Explicitly resolved fixed-function inputs. Coordinate triples are (u,v,q)
// per vertex; q=1 for ordinary UVs, otherwise sampling divides u and v by q.
// Point or bilinear min/mag sampling; mip selection is independently explicit.
enum class Raster3DMipFilter : uint8_t { None, Point, Linear };
struct Raster3DFixedDrawStage {
  const Raster3DTexture *texture = nullptr;
  const float *coordinates = nullptr;
  Raster3DAddress addressU = Raster3DAddress::Wrap;
  Raster3DAddress addressV = Raster3DAddress::Wrap;
  Raster3DFilter filter = Raster3DFilter::Point;
  Raster3DMipFilter mipFilter = Raster3DMipFilter::None;
  std::optional<float> mipLodBias;
  std::optional<uint32_t> maxMipLevel;
};
struct Raster3DFixedDraw {
  const Raster3DFixedState *state = nullptr;
  std::array<Raster3DFixedDrawStage, 8> stages;
  const uint8_t *specular = nullptr; // vertexCount D3DCOLOR values (BGRA)
  bool lightingResolved =
      false; // colours reflect the actual device lighting state
};

// Applies an observed D3D camera-space texture matrix. The projected form
// outputs (x,y,z); the COUNT2 form outputs (x,y,1). Both use row vectors.
bool raster3dProjectorCoordinates(const float *cameraPositions, size_t count,
                                  const std::array<float, 16> &textureMatrix,
                                  bool projected, float *coordinates,
                                  std::string &error);
