#pragma once

#include "owner_links.hpp"
#include "resource_texture.hpp"
#include "scx.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// FUN_0052ced0 selects this stock render-object kind from a loaded type-14
// resource's flags. The kind is not a draw order or a portable object handle.
enum class SceneRenderKind : uint8_t { Kind0 = 0, Kind2 = 2, Kind3 = 3, Kind4 = 4, Kind5 = 5, Kind6 = 6 };
SceneRenderKind sceneRenderKindFromFlags(uint32_t flags);

// Collection offsets in the stock view object, not an ordering guarantee.
enum class ScenePass : uint16_t { Pass41c = 0x41c, Pass448 = 0x448, Pass474 = 0x474,
                                  Pass4cc = 0x4cc, Pass50c = 0x50c, Pass548 = 0x548 };
struct ScenePassSelection {
	ScenePass passes[2]{};
	uint8_t count = 0;
	bool customCollection = false;
};

// FUN_00527d40: entry flags are the renderer material's word at +8. A custom
// collection is selected only if the resource flag and collection both exist.
ScenePassSelection scenePasses(uint32_t entryFlags, bool specialView,
                               bool resourceFlag8, bool hasCustomCollection);

// Stock renderer material type 0 translates the SCX function word's top two
// bits into the entry flag word. Other material types are not covered here.
uint32_t sceneType0EntryFlags(uint32_t functionWord);

class ResourceWorld;
// FUN_0054b850 classification of a test sphere against a sphere bound.
// Returns the stock result bits 1, 2, 4 or 8, or zero for invalid host input.
uint32_t sceneSphereRelation(const std::array<float, 3>& boundCenter, float boundRadius,
                             const std::array<float, 3>& center, float radius);
struct SceneVisibilityFields {
	std::optional<float> field6c;
	std::optional<float> field70;
	uint32_t stamp78 = 0;
	uint32_t stamp7c = 0;
};
// The type-3 virtual setters used by 00513cc0. Does not compute their input.
bool sceneUpdateType3Visibility(SceneVisibilityFields& fields, uint32_t frame,
                                float input, float bias, float gain);
struct ResourceBounds;
// FUN_005100b0: camera sphere from the renderer far metric and bone matrix.
bool sceneCameraBound(float farMetric, const std::array<float, 16>& cameraWorld, ResourceBounds& out);
// FUN_0054ba20: classify a test box against a sphere or box camera bound.
uint32_t sceneBoxRelation(const ResourceBounds& bound, const std::array<float, 3>& center,
                          const std::array<float, 3>& extents);
uint32_t sceneBoundPlaneRelation(const ResourceBounds& bound, const std::array<float, 4>& plane);
// Computes 00513cc0's normal (zero override) input. cameraFactor is the
// renderer's field at +0x398; worldScale is DAT_006188c4.
std::optional<float> sceneVisibilityInput(const ResourceBounds& bounds,
                                          const std::array<float, 3>& cameraPosition,
                                          float cameraFactor, float lodBias,
                                          float lodAmplitude, float worldScale);
// Queue of candidates already accepted by the stock visibility stage. Lower
// scores are processed first; unprocessed candidates are discarded each frame.
class SceneVisibleQueue {
public:
	~SceneVisibleQueue();
	SceneVisibleQueue(const SceneVisibleQueue&) = delete;
	SceneVisibleQueue& operator=(const SceneVisibleQueue&) = delete;
	SceneVisibleQueue() = default;
	bool push(LinkOwner& instance, float score);
	void detach(LinkOwner& instance);
	void drain(uint32_t budget, bool unlimited, const std::function<uint32_t(LinkOwner&)>& create);
private:
	struct Entry { OwnerLink instance; float score = 0; };
	std::vector<std::unique_ptr<Entry>> entries_;
};
enum class SceneCandidateResult { Queued, AlreadyPresent, BelowThreshold, Unsupported };
// Bounded FUN_00511180 type-3 branch. scoreState is the shared priority
// carried between traversed nodes; mode 2 reuses it. Mode 1 has extra stock
// side effects and is not covered here.
SceneCandidateResult sceneQueueType3(SceneVisibleQueue& queue, LinkOwner& instance,
                                    uint32_t boundMode, float field6c, float field70,
                                    float threshold, float& scoreState, uint32_t& recordFlags,
                                    bool fullyInside = false);
struct WorldNode;
bool sceneUpdateType3NodeVisibility(WorldNode& node, uint32_t frame, float input);
std::optional<float> sceneTransparentDepth(const std::array<float, 16>& objectMatrix,
                                           const std::array<float, 16>& cameraWorld);
// Camera-local records for submitted runtime type-3 nodes. Discovery and
// renderer-object creation remain the caller's responsibility.
class SceneCameraCandidates {
public:
	~SceneCameraCandidates();
	SceneCandidateResult submit(WorldNode& node, float threshold, float& scoreState, bool fullyInside = false);
	bool markMaterialCreated(LinkOwner& node);
	bool markEmptyCreated(LinkOwner& node);
	bool hasRecord(const LinkOwner& node) const;
	bool remove(LinkOwner& node);
	void discardPending();
	void drain(uint32_t budget, bool unlimited, const std::function<uint32_t(LinkOwner&)>& create);
private:
	struct Record { OwnerLink owner; uint32_t flags = 0; };
	SceneVisibleQueue queue_;
	std::vector<std::unique_ptr<Record>> records_;
};
// Loads a bounded sourcefile mesh or copies a bound runtime mesh.
bool sceneLoadMesh(ResourceWorld& world, uint32_t id, const std::filesystem::path& root,
                   ScxModel& model, std::string& error, bool allowFontFlags = false);

// Scales positions of an already-copied SCX model. Returns false for malformed
// host data, leaving the model unchanged.
bool sceneScaleMesh(ScxModel& model, float x, float y, float z);

// Defaults of the fields copied by stock for the supported three-directive
// material. Non-default skeleton, shadow texture, and effect data are unsupported.
struct SceneMaterialDefaults {
	uint32_t bones = 0;
	float lodBias = 0;
	float lodAmplitude = 1;
	std::array<uint32_t, 21> shadowWords = [] {
		std::array<uint32_t, 21> words{};
		words[11] = 0x41200000;
		words[16] = 0x41200000;
		return words;
	}();
};

// Links and their owners must remain alive until this object is destroyed.
class SceneMaterialResources {
public:
	SceneMaterialResources(LinkOwner* mesh, const std::vector<LinkOwner*>& textures, uint32_t flags = 0);
	~SceneMaterialResources();
	std::unique_ptr<SceneMaterialResources> clone() const;
	SceneMaterialResources(const SceneMaterialResources&) = delete;
	SceneMaterialResources& operator=(const SceneMaterialResources&) = delete;
	uint32_t meshId() const { return mesh_.cachedValue; }
	uint32_t flags() const { return flags_; }
	const SceneMaterialDefaults& defaults() const { return defaults_; }
	uint32_t textureId(size_t index) const { return textures_.at(index)->cachedValue; }
	size_t textureCount() const { return textures_.size(); }
	// Refresh is called once for every match, including an unchanged link.
	bool replace(uint32_t oldId, LinkOwner* replacement, const std::function<void()>& refresh);
private:
	uint32_t flags_ = 0;
	SceneMaterialDefaults defaults_;
	OwnerLink mesh_;
	std::vector<std::unique_ptr<OwnerLink>> textures_;
};

struct SceneTextState {
	std::string text;
	int32_t alignment = 2;
	uint32_t color = 0xffffffffu;
	float advanceScale = 1.0f;
};

struct CameraClipVertex;
// Conservative normalized UV error bound for affine interpolation of a
// perspective-projected triangle with strictly positive clip-space W.
std::optional<float> scenePerspectiveUvErrorBound(const std::array<CameraClipVertex, 3>& triangle);

struct SceneGroundBone {
	std::string name;
	uint32_t id = 0;
	std::array<float, 16> matrix{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
};

class GameWorld;
struct WorldNode;
namespace physics { struct Body; }

class SceneRenderInstance {
public:
	explicit SceneRenderInstance(LinkOwner* type);
	~SceneRenderInstance();
	SceneRenderInstance(const SceneRenderInstance&) = delete;
	SceneRenderInstance& operator=(const SceneRenderInstance&) = delete;
	uint32_t typeId() const { return type_.cachedValue; }
	// Bounded first-bone case from 005411b0 and 00540270; no attachment.
	void prepareUnattachedGroundBone();
	const std::optional<SceneGroundBone>& groundBone() const { return groundBone_; }
	const std::array<float, 16>& boneMatrix() const { return boneMatrix_; }
	const std::array<float, 16>& derivedRootBoneMatrix() const { return derivedRootBoneMatrix_; }
	const std::array<float, 16>& drawMatrix() const { return drawMatrix_; }
	bool rootBoneUpdatePending() const { return rootBoneUpdatePending_; }
	void setBoneTranslation(float x, float y, float z);
	void processRootBoneUpdate();
	void refreshSingleBoneDrawMatrix();
	bool attachPhysicsBone(GameWorld& world, WorldNode& physicsNode);
	void detachPhysicsBone();
	bool refreshPhysicsBone();
	bool hasPhysicsBoneAttachment() const { return physicsBoneAttached_; }
	const std::optional<SceneTextState>& textState() const { return text_; }
	void setTextState(SceneTextState text) { text_ = std::move(text); }
private:
	OwnerLink type_;
	OwnerLink physicsNode_;
	bool physicsBoneAttached_ = false;
	std::weak_ptr<physics::Body> physicsBody_;
	std::optional<SceneGroundBone> groundBone_;
	std::array<float, 16> boneMatrix_{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
	std::array<float, 16> derivedRootBoneMatrix_{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
	std::array<float, 16> drawMatrix_{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
	bool rootBoneUpdatePending_ = false;
	std::optional<SceneTextState> text_;
};

class ResourceWorld;
struct SceneMaterialLoadResult {
	std::unique_ptr<SceneMaterialResources> material;
	uint32_t flags = 0;
	std::string error;
};
// Loads the bounded type-14 mesh/texture/flags payload. Destroy the result
// before the ResourceWorld, which owns its linked nodes.
SceneMaterialLoadResult sceneLoadMaterial(ResourceWorld& world, uint32_t id);

// Refreshes the first linked texture into host RGBA pixels. Failure clears the
// output; this does not implement the stock renderer's complete cache refresh.
bool sceneRefreshTexture(ResourceWorld& world, const SceneMaterialResources& material,
                         const std::filesystem::path& root, ResourceTexturePixels& pixels,
	                         std::string& error, bool requireCached = false);

struct RenderVertex;
// Caller supplies the 2D projection; stock viewport and camera transforms are
// not yet implemented. Only meshes with one UV pair per vertex are read.
bool sceneProjectMesh(const ScxMesh& mesh, const float projection[8],
                      std::vector<RenderVertex>& vertices, std::string& error);
// Projects a mesh after a row-vector world transform. Projection remains
// caller-supplied; this is not the stock camera or viewport implementation.
bool sceneProjectTransformedMesh(const ScxMesh& mesh, const std::array<float, 16>& world,
                                 const float projection[8], std::vector<RenderVertex>& vertices,
                                 std::string& error);
class Render2D;
// Draw one already-loaded mesh using an explicit projection and texture.
// This does not discover viewport entries or refresh changed resources.
bool sceneDrawSingleBoneMesh(Render2D& renderer, size_t textureId, const ScxMesh& mesh,
                             SceneRenderInstance& instance, const float projection[8],
                             std::string& error);
struct ViewportInstance;
struct CameraFog;
// The bounded camera path requires all vertices inside the frustum and equal
// clip W within each triangle; clipping and perspective-correct UVs are not
// supplied by Render2D.
bool sceneDrawCameraMesh(Render2D& renderer, size_t textureId, const ScxMesh& mesh,
                         SceneRenderInstance& instance, const std::array<float, 16>& view,
                         const std::array<float, 16>& projection, const ViewportInstance& viewport,
                         std::string& error, const CameraFog* fog = nullptr,
                         const ResourceTexturePixels* texturePixels = nullptr);

struct ResolvedNode;
struct WorldNode;
// Clones the bound, bounded material into a new type-14 world node under the
// same parent. Other stock type-14 fields and renderer cache are not modeled.
WorldNode* sceneCloneMaterial(ResourceWorld& world, const ResolvedNode& source, std::string& error);
WorldNode* sceneCloneMesh(ResourceWorld& world, const ResolvedNode& source, std::string& error);
WorldNode *sceneCloneRenderLight(ResourceWorld &world, const ResolvedNode &source,
                                 std::string &error);


// Host-side snapshot of the subset of type-14 cache data modeled here.
struct SceneMaterialCache {
	uint32_t flags = 0;
	uint32_t meshId = 0;
	uint32_t textureId = 0;
	ScxModel mesh;
	ResourceTexturePixels texture;
};
// Rebuilds this bounded snapshot; it does not update a live GPU renderer.
// A failed refresh clears the previous snapshot.
bool sceneRefreshMaterial(ResourceWorld& world, const SceneMaterialResources& material,
                          const std::filesystem::path& root, SceneMaterialCache& cache,
                          std::string& error, bool requireCached = false);

// ResourceWorld and Render2D must outlive the object. This bounded renderer
// reloads its source snapshot on each draw, so resource replacement and mesh
// changes cannot silently leave a stale GPU texture or mesh.
class SceneMaterialRenderer {
public:
	static std::unique_ptr<SceneMaterialRenderer> create(ResourceWorld& resources, Render2D& renderer,
	                                                     WorldNode& node, std::string& error, bool requireCached = false);
	~SceneMaterialRenderer();
	SceneMaterialRenderer(const SceneMaterialRenderer&) = delete;
	SceneMaterialRenderer& operator=(const SceneMaterialRenderer&) = delete;
	bool draw(const std::array<float, 16>& view, const std::array<float, 16>& projection,
	          const ViewportInstance& viewport, std::string& error, bool requireCached = false,
	          const CameraFog* fog = nullptr);
	std::optional<float> transparentDepth(const std::array<float, 16>& cameraWorld, std::string& error, bool requireCached = false);
	std::optional<bool> opaque(std::string& error, bool requireCached = false);
	std::optional<uint32_t> textMaterialId() const;
	LinkOwner* owner() const { return node_.owner; }
private:
	SceneMaterialRenderer(ResourceWorld& resources, Render2D& renderer);
	ResourceWorld& resources_;
	Render2D& renderer_;
	OwnerLink node_;
	std::optional<uint32_t> textMaterialId_;
};
// Owns the bounded material objects for one camera. Resources and renderer
// must outlive this object. Supports the rectangle transparent pass and the
// later text pass, grouped by font material.
class SceneCameraRenderer {
public:
	SceneCameraRenderer(ResourceWorld& resources, Render2D& renderer)
		: resources_(resources), renderer_(renderer) {}
	SceneCameraCandidates& candidates() { return candidates_; }
	bool remove(LinkOwner& node);
	bool draw(const std::array<float, 16>& view, const std::array<float, 16>& projection,
	          const ViewportInstance& viewport, std::string& error, bool requireCached = false,
	          const CameraFog* fog = nullptr);
private:
	ResourceWorld& resources_;
	Render2D& renderer_;
	SceneCameraCandidates candidates_;
	std::vector<std::unique_ptr<SceneMaterialRenderer>> objects_;
	void eraseObject(size_t index);
};
