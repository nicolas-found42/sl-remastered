#pragma once

#include "ground_runtime.hpp"
#include "owner_links.hpp"
#include "pack_registry.hpp"
#include "scene_render_light.hpp"
#include "world_model.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// Host model of the stock resource world behind ResourceRef.set(I)V: pack mount
// (FUN_00544170), node creation per type (FUN_00536a10), lookup by 32-bit id
// (FUN_00536820 / FUN_00537000) and the link operation (FUN_00545fc0).
// Evidence, offsets and the list of what is not modelled are in
// docs/resource-world-evidence.md. Lifetime: a ResourceWorld references a
// GameWorld and a PackRegistry and must be destroyed before either.

// Stock pack record +0x50: 0 unmounted, 1 while FUN_00544170 runs, 2 once it
// has finished.
enum class MountState : uint8_t { Unmounted = 0, Mounting = 1, Mounted = 2 };

// What a node's payload is for, judged only by the alias the node type has in
// system.rpk. The per-type loaders (reached from FUN_005447d0) were not read,
// so this does not say what the loader does.
enum class PayloadClass : uint8_t { Script, GameOrPhysics, Renderer, Audio };

// One row of the creation switch FUN_00536a10, the flag table at 00619d10 and
// the sector-hook table at 00619d68.
struct NodeTypeInfo {
  uint32_t type;
  const char *systemAlias; // the alias this type has in system.rpk, or "" when
                           // system.rpk has none
  uint32_t stockBytes;     // allocation size in the switch; 0 for type 1, which
                           // comes from a node pool
  uint32_t constructor;    // stock address of the type constructor
  uint32_t baseFlags; // DAT_00619d10[type], ORed into the node flags (+0x54) by
                      // the base constructor
  bool sectorHook;    // DAT_00619d68[type]: FUN_00544fe0 runs after the node is
                      // attached
  bool takesBounds; // the type constructor passes the entry's transform record
                    // to FUN_00544d40
  PayloadClass payload;
};
// Null for a type the stock switch cannot create (4, 11, 15, 16, 17 and
// anything above 0x15): stock logs "Node registration error" and creates
// nothing.
const NodeTypeInfo *nodeTypeInfo(uint32_t type);
std::optional<uint64_t> stockTextureUpperBound(uint32_t width, uint32_t height);
// Earlier loose allowance: four full-size device slots. No longer used by the
// texture cache; kept for its tests and for comparison with
// stockTextureSlotUpperBound below.
std::optional<uint64_t> stockTextureFamilyUpperBound(uint32_t width,
                                                     uint32_t height);

// What the host presents to stock's texture-creation path as a Direct3D 9
// device (fields of D3DCAPS9 that 005914a9 reads: TextureCaps at +0x3c,
// MaxTextureWidth/Height at +0x58/+0x5c, MaxTextureAspectRatio at +0x68).
// Evidence in docs/startup-frontier.md, "Texture counter".
struct StockDeviceTextureCaps {
  uint32_t textureCaps = 0; // D3DPTEXTURECAPS_*: 0x2 pow2, 0x20 square only,
                            // 0x100 nonpow2 conditional, 0x4000 mipmap
  uint32_t maxWidth = 0;
  uint32_t maxHeight = 0;
  uint32_t maxAspectRatio = 0; // 0 means no limit
};
constexpr uint32_t kD3DTexCapsPow2 = 0x2, kD3DTexCapsSquareOnly = 0x20,
                   kD3DTexCapsNonPow2Conditional = 0x100,
                   kD3DTexCapsMipMap = 0x4000;
// The caps the SDL host presents: any size up to the renderer's maximum, mip
// chains allowed, no power-of-two, square-only or aspect restriction.
StockDeviceTextureCaps hostDeviceTextureCaps(uint32_t rendererMaxTextureSize);

// Shape of the device texture 005914a9 (D3DXCheckTextureRequirements, 2-D case,
// pool not scratch, no auto-mip usage, all mip levels requested) leaves for a
// source of width x height. Width and height of 4 or more only: the
// compressed-format rounding to multiples of four is not modelled below that.
struct StockTextureShape {
  uint32_t width, height, levels;
};
std::optional<StockTextureShape>
stockDeviceTextureShape(uint32_t width, uint32_t height,
                        const StockDeviceTextureCaps &caps);
// Bytes the counter DAT_0065c234 gains for one device texture of that shape,
// summing 004f6230's per-level product floor(w * h * bytesPerPixel) with
// bytesPerPixel = 4.0, the largest value in its table (A8R8G8B8, X8R8G8B8 and
// the default branch; R8G8B8 is 3.0, 16-bit formats 2.0, DXT2-5 1.0, DXT1 0.5).
// Exact for 32-bit formats, an upper bound for the rest.
std::optional<uint64_t>
stockTextureSlotUpperBound(const StockTextureShape &shape);
// Secondary texture dimensions from 00506750 / 00507260 / 00507b40.
std::optional<std::array<uint32_t, 2>>
stockTextureCloneSize(uint32_t width, uint32_t height, uint32_t availableBytes);

// Tracks represented texture allocations until their last owner releases them.
// An unknown allocation makes the total unknown; this ledger does not prove
// that every stock renderer allocation has been represented.
class TextureAllocationInventory {
public:
  std::shared_ptr<void> track(std::optional<uint64_t> upperBound);
  std::optional<uint64_t> upperBound() const;
  size_t count() const;

private:
  struct State {
    uint64_t next = 0;
    std::unordered_map<uint64_t, std::optional<uint64_t>> entries;
  };
  std::shared_ptr<State> state_ = std::make_shared<State>();
};

// sectorHookedType (DAT_00619d68 for any type value, including those the switch
// cannot create) is in world_model.hpp.

// A node that a pack mount created. The LinkOwner value is the node's resource
// id (+0x50). A type-8 node has its links on the GameWorld ScriptNode instead
// (see linkOwner()), so that GameType creation and ResourceRef links share one
// referrer list.
struct SoundSample;
struct ResourceTextureCache;
struct PreparedSpatialGamePayload;
struct ResourceNode : LinkOwner {
  ResourceTreeHandle handle;
  uint32_t type = 0;          // +0x4c
  uint32_t flags = 0;         // +0x54: entry flags | NodeTypeInfo::baseFlags
  uint16_t payloadSlot = 0; // pack containing this descriptor's payload
  uint32_t payloadOffset = 0; // +0x58, file offset in the pack
  uint32_t payloadSize = 0;   // +0x5c
  // +0x60, only for type 8 (FUN_00544010 passes the entry alias only for
  // scripts), at most 30 bytes.
  std::string name;
  // +0x64 after FUN_00544b90(0): 0x40 is "on the unloaded list". The mount
  // never loads a node.
  std::shared_ptr<SoundSample>
      sound; // type-6 decoded host buffer, released with the node
  std::optional<uint32_t>
      soundBufferBytes; // stock audio buffer accounting, unknown until loaded
  std::shared_ptr<ResourceTextureCache>
      texture; // type-7 host texture, released with the node
  uint32_t stateWord = 0x40;
  bool countedInstance = false;
  size_t registeredPayloadChildren = 0;
  bool payloadRegistrationFailed = false;
  std::vector<ResourceNode *> payloadChildren;
  size_t preparedPayloadActions = 0;
  OwnerLink gameTypeLink;
  // In-progress 00463220 inputs. Cleared by reset. Not the factory result.
  std::shared_ptr<GroundPreparation> groundPreparation;
  // Stock stores 00463220's return at the city resource +0x124.
  std::shared_ptr<GroundFactoryResult> groundFactory;
  // Decoded passive type-1 payload, retained for the node's loaded lifetime.
  std::shared_ptr<PreparedSpatialGamePayload> spatialPayload;
  uint32_t eventMask = 0;
  std::vector<std::unique_ptr<EventSubscription>> eventSubscriptions;
  float loadPriority = 0.0f; // +0x68
  uint32_t parentId =
      0; // remapped id the entry named; 0 means the default parent
  ResourceNode *parent = nullptr; // null when the parent is the default parent
  std::vector<ResourceNode *> children;
  // A packed transform creates mode 1 and supplies six bounds values.
  std::optional<ResourceBounds> bounds;
  // Constructor defaults depend on the stock frame counter; unknown until
  // the corresponding initialization path is modeled.
  std::optional<float> field6c;
  std::optional<float> field70;
  uint32_t stamp74 = 0;
  bool recentUseQueued = false;
  uint32_t stamp78 = 0;
  uint32_t stamp7c = 0;
  float visibilityBias = 0;
  float visibilityGain = 1;
  std::optional<uint32_t> polygonCount;
  // +0xc0 for types with the sector hook: the owner this node's sector link
  // points at.
  OwnerLink sectorLink;
  ScriptNode *script = nullptr; // type 8 only
  bool loaded() const { return stateWord & 1; }
};

// Bounded type-1 payload metadata; parsing it does not create or load nodes.
struct GameResourceChild {
  RpkEntry entry;
  uint32_t runtimeId = 0;
  uint32_t parentId = 0;
  uint8_t runtimeFlags = 0;
};
struct GamePayloadAction {
  enum class Kind { ExternalPack, ExternalPackEnd, GameType };
  Kind kind;
  uint32_t value;
};
struct GamePhysicsChunk {
  std::vector<std::array<uint32_t, 7>> records;
  std::vector<std::array<uint32_t, 3>> triples;
};
struct GamePolygonGroup {
  uint32_t kind = 0;
  std::vector<std::array<uint32_t, 14>> records;
  std::vector<std::array<uint32_t, 3>> triples;
};
struct GamePolygonChunk {
  uint32_t version = 0;
  std::vector<GamePolygonGroup> groups;
};
struct GameResourcePayload {
  std::vector<GameResourceChild> children;
  std::optional<uint32_t> gameType;
  std::optional<uint32_t> runtimeGameType;
  std::optional<std::string> parameters;
  std::vector<uint32_t> externalTypes;
  std::vector<GamePayloadAction> actions;
  // Geometry order is preserved within this list; its interleaving with
  // actions is not represented or applied.
  std::vector<std::variant<GamePhysicsChunk, GamePolygonChunk>> geometryChunks;
};

class SceneMaterialResources;
class SceneRenderInstance;
struct ScxModel;
std::optional<uint64_t> stockMeshVertexBytes(const ScxModel &model);

// What the VM or a native gets back from a lookup. `owner` is the object that
// links attach to. A type-8 node found through a mount has both packNode and
// script; a script node made by GameType creation has only script.
struct ResolvedNode {
  LinkOwner *owner = nullptr;
  uint32_t type = 0;
  uint32_t id = 0;
  ResourceNode *packNode = nullptr;
  WorldNode *worldNode = nullptr;
  ScriptNode *script = nullptr;
  explicit operator bool() const { return owner != nullptr; }
};

enum class MountStatus {
  Mounted,
  // FUN_00544170 returns early when the state is not 0, including while the
  // pack is being mounted.
  AlreadyMounted,
  // FUN_0054c170 said the file does not exist: stock leaves the state at 0, so
  // a later lookup tries again.
  FileMissing,
  // The host refuses a file the stock reader would read without checking. The
  // state stays 0.
  Invalid,
  // The stock path continues into machinery the host does not model. The state
  // stays 1.
  Unsupported,
  // Stock dereferences a null pointer. The state stays 1 and the nodes created
  // so far stay.
  StockCrash,
};
struct MountResult {
  MountStatus status = MountStatus::Mounted;
  MountState state = MountState::Unmounted;
  size_t nodesCreated = 0;
  // Entries for which the type switch creates nothing.
  size_t entriesSkipped = 0;
  std::string detail;
};

enum class LookupStatus { Found, NotFound, StockCrash, MountRefused };
struct LookupResult {
  LookupStatus status = LookupStatus::NotFound;
  ResolvedNode node;
  std::string detail;
};

// What a node would give a caller that needs its payload. No type has a loader
// here yet; every request is answered with a named state and never with an
// invented value.
enum class PayloadStatus { Deferred, Unsupported };
struct PayloadRequest {
  PayloadStatus status = PayloadStatus::Deferred;
  std::string detail;
};

struct GroundRunSnapshot {
  uint32_t cities = 0;
  uint32_t roads = 0;
  uint32_t traffic = 0;
  uint32_t pedestrianTypes = 0;
  uint32_t waterActive = 0;
  uint32_t waterLimits = 0;
  uint32_t fogSettings = 0;
  uint32_t densitySet = 0;
  float density = 0.f;
  float densityHi = 0.f;
};

// First city-owned factory with waterActive. Empty when none.
struct GroundWaterNote {
  bool active = false;
  float posX = 0.f;
  float posY = 0.f;
  float posZ = 0.f;
  float extentA = 0.f;
  float extentB = 0.f;
};

class ResourceWorld {
public:
  ResourceWorld(GameWorld &world, PackRegistry &packs);
  ResourceWorld(const ResourceWorld &) = delete;
  ResourceWorld &operator=(const ResourceWorld &) = delete;
  ~ResourceWorld();

  GameWorld &world() { return world_; }
  PackRegistry &packs() { return packs_; }

  MountState mountState(uint16_t slot) const;
  // FUN_00544170 on one slot. The slot must exist in the PackRegistry.
  MountResult mountPack(uint16_t slot);

  // FUN_00536820: id 0 finds nothing; an unmounted pack id is mounted first;
  // the tree is then searched by id. `type` 0 accepts any type, otherwise the
  // node type must match. A miss appends the id to missingIds() unless `quiet`
  // (flag bit 0 of FUN_00537000).
  LookupResult lookup(uint32_t id, uint32_t type = 0, bool quiet = false);
  // The node's resource id as the id index sees it. For node pointers that this
  // world does not own, null.
  ResolvedNode resolve(const LinkOwner *owner);
  // Creates a game instance beneath a mounted node in the shared world tree.
  // This models parentage and sector inheritance only; it does not load the
  // parent.
  WorldNode *addGameChild(ResourceNode &parent, const std::string &name,
                          uint32_t flags);
  // Bounded FUN_0053e860/0053dee0 path: a type-1 child of the loaded root
  // whose payload is exactly two zero words. Other payloads remain unsupported.
  bool loadEmptyGameParent(ResourceNode &node, std::string &error);
  // Bounded 0053ec20 unload for an empty parent without children or a factory.
  bool unloadEmptyGameParent(ResourceNode &node, std::string &error);
  // Passive type-1 subset: parent must already be loaded. Geometry is retained;
  // rendering and collision consumers still reject unsupported effects.
  bool loadPassiveSpatialGame(ResourceNode &node, std::string &error);
  bool unloadPassiveSpatialGame(ResourceNode &node, std::string &error);
  // Discard only unloaded preparation with uniquely indexed type-1 children.
  // Other child types, duplicate IDs and failed registration cannot be reset.
  // Mounted dependencies stay mounted.
  bool resetPreparedGamePayload(ResourceNode &node, std::string &error);
  // The subset of 0053dee0 needed to inspect packed children and RSD/EXTP.
  // PHYS and POLY are retained as raw words, without applying their effects.
  bool inspectGamePayload(ResourceNode &node, GameResourcePayload &payload,
                          std::string &error);
  // Registers the parsed type-1 children without loading the parent.
  bool registerGamePayloadChildren(ResourceNode &node, std::string &error);
  // Prepares child registration, external packs, and the game-type link.
  // A missing nonzero game type leaves preparation incomplete for retry. Stock
  // leaves its link empty and continues. This does not run the factory, expand
  // EXTP bounds, or mark the node loaded.
  bool prepareGamePayload(ResourceNode &node, GameResourcePayload &payload,
                          std::string &error);
  // FUN_00537240 fast path, bounded to mounted type-6 and type-8 nodes.
  bool loadEmptyResource(ResourceNode &node, std::string &error);
  void noteScriptLoaded(uint32_t id);
  // Bounded 00537ed0 aging; refuses other pending maintenance branches.
  // maxAge 8 is stock's unforced per-pass threshold; flush's 00537ed0(1)
  // forces 0 (evict anything unused this frame).
  bool ageRecentUses(std::string &error, uint32_t maxAge = 8);
  bool setTextureDemand(ResourceNode &node, float demand, std::string &error);
  TextureAllocationInventory &textureAllocations() {
    return textureAllocations_;
  }
  std::vector<uint32_t> cachedTextureIds() const;
  std::optional<uint64_t>
  stockSceneMeshUpperBound(std::string *reason = nullptr) const;
  bool stockSceneBatchBuffersAbsent() const;
  bool forceRenderPreflight(std::string &error,
                            std::optional<int32_t> instanceMax = {},
                            std::optional<int32_t> soundMax = {},
                            std::optional<int32_t> textureMax = {},
                            std::optional<int32_t> meshMax = {}) const;
  // Bounded in-memory completion of stock's asynchronous pack read for
  // mounted type-6 sounds and bounded type-14 fonts; the frame drain loads
  // payloads.
  bool queueSoundPrecache(ResourceNode &node, std::string &error);
  bool queueFontLoad(const ResolvedNode &node, std::string &error);
  bool completePendingPackLoads(uint32_t loadRate, std::string &error);
  size_t pendingPackLoads() const { return pendingPackLoads_.size(); }
  // Associates a loaded host material with a type-14 node. The identity link
  // detaches if the node is destroyed, preventing reuse of a stale ID.
  bool bindSceneMaterial(const ResolvedNode &node,
                         std::shared_ptr<SceneMaterialResources> material);
  std::shared_ptr<SceneMaterialResources>
  sceneMaterial(const ResolvedNode &node);
  void unbindSceneMaterial(const ResolvedNode &node);
  bool setSceneMaterialRefresh(const ResolvedNode &node,
                               std::function<void()> refresh);
  void refreshSceneMaterial(const ResolvedNode &node);
  bool hasActiveSceneMaterialForMesh(uint32_t meshId) const;
  bool bindSceneMesh(const ResolvedNode &node, std::shared_ptr<ScxModel> mesh);
  std::shared_ptr<ScxModel> sceneMesh(const ResolvedNode &node);
  bool bindSceneTexture(const ResolvedNode &node,
                        std::shared_ptr<ResourceTextureCache> texture);
  std::shared_ptr<ResourceTextureCache> sceneTexture(const ResolvedNode &node);
  bool bindSceneLight(const ResolvedNode &node, SceneRenderLight light);
  const SceneRenderLight *sceneLight(const ResolvedNode &node);
  bool bindSceneRenderInstance(const ResolvedNode &node,
                               std::shared_ptr<SceneRenderInstance> instance);
  std::shared_ptr<SceneRenderInstance>
  sceneRenderInstance(const ResolvedNode &node);

  // Children of the default parent that mounts created, in stock order.
  const std::vector<ResourceNode *> &defaultParentChildren() const {
    return topLevel_;
  }
  // world+0x106ed0: ids that a lookup could not resolve.
  const std::vector<uint32_t> &missingIds() const { return missing_; }
  // Messages stock sends to FUN_00551330 on the paths modelled here.
  const std::vector<std::string> &log() const { return log_; }
  size_t nodeCount() const { return nodes_.size(); }
  // Cities that own a factory result. density is the last city that stored
  // one, in node creation order. One Free Ride city makes that the live value.
  GroundRunSnapshot groundSnapshot() const;
  GroundWaterNote groundWaterNote() const;

  // FUN_005447d0 is what loads a node. It is not modelled; this names why.
  PayloadRequest requestPayload(const ResolvedNode &node) const;

private:
  TextureAllocationInventory textureAllocations_;
  bool stockBatchHistoryUnknown_ = false;
  struct PackState {
    MountState state = MountState::Unmounted;
  };
  ResourceNode *createNode(uint16_t slot, const PackView &view,
                           const RpkEntry &entry, MountResult &result,
                           bool &crashed, ResourceNode *explicitParent = nullptr);
  bool parseGamePayload(ResourceNode &node, GameResourcePayload &payload,
                        std::string &error, bool parseTail);
  ResolvedNode wrap(ResourceNode &node);
  // FUN_00537000 without the mount step.
  ResolvedNode findOnly(uint32_t id, uint32_t type, bool quiet);
  void attachSector(ResourceNode &node, const ResourceNode *parentNode,
                    const WorldNode *parentWorld, bool parentIsRoot);

  GameWorld &world_;
  PackRegistry &packs_;
  uint64_t destroyObserver_ = 0;
  struct SceneMaterialSlot {
    ~SceneMaterialSlot() { setLinkOwner(owner, nullptr); }
    OwnerLink owner;
    std::shared_ptr<SceneMaterialResources> material;
    std::function<void()> refresh;
  };
  std::unordered_map<uint32_t, std::unique_ptr<SceneMaterialSlot>>
      sceneMaterials_;
  struct SceneMeshSlot {
    ~SceneMeshSlot() { setLinkOwner(owner, nullptr); }
    OwnerLink owner;
    std::shared_ptr<ScxModel> mesh;
  };
  std::unordered_map<uint32_t, std::unique_ptr<SceneMeshSlot>> sceneMeshes_;
  struct SceneTextureSlot {
    ~SceneTextureSlot() { setLinkOwner(owner, nullptr); }
    OwnerLink owner;
    std::shared_ptr<ResourceTextureCache> texture;
  };
  std::unordered_map<uint32_t, std::unique_ptr<SceneTextureSlot>> sceneTextures_;
  struct SceneLightSlot {
    ~SceneLightSlot() { setLinkOwner(owner, nullptr); }
    OwnerLink owner;
    SceneRenderLight light;
  };
  std::unordered_map<uint32_t, std::unique_ptr<SceneLightSlot>> sceneLights_;
  struct SceneRenderSlot {
    ~SceneRenderSlot() { setLinkOwner(owner, nullptr); }
    OwnerLink owner;
    std::shared_ptr<SceneRenderInstance> instance;
  };
  std::unordered_map<uint32_t, std::unique_ptr<SceneRenderSlot>> sceneRenders_;
  struct PendingPackLoad {
    ~PendingPackLoad() { setLinkOwner(owner, nullptr); }
    OwnerLink owner;
  };
  std::vector<std::unique_ptr<PendingPackLoad>> pendingPackLoads_;
  std::vector<PackState> states_;
  std::vector<std::unique_ptr<ResourceNode>> nodes_;
  std::unordered_map<uint32_t, ResourceNode *> byId_;
  std::vector<ResourceNode *> topLevel_;
  std::vector<uint32_t> missing_;
  std::vector<std::string> log_;
};

// The LinkOwner that references to this node are attached to.
LinkOwner &linkOwner(ResourceNode &node);

enum class RefSetStatus {
  // FUN_00545fc0 compared the id with link +8 and found them equal.
  Unchanged,
  // The old node link was detached and the new id was 0.
  Cleared,
  Linked,
  // The lookup found nothing. An old node link is already detached by then.
  NotFound,
  StockCrash,
  MountRefused,
};
struct RefSetResult {
  RefSetStatus status = RefSetStatus::Unchanged;
  ResolvedNode node;
  std::string detail;
};
// FUN_00545fc0(link, id), the whole of ResourceRef.set(I)V after the receiver's
// link record is found. `link.cachedValue` is the record's id word (+8) and
// `link.owner` its node (+0xc). The two can disagree after a failed lookup,
// exactly as in stock: an id with no node is kept as it was.
RefSetResult resourceRefSet(ResourceWorld &resources, OwnerLink &link,
                            uint32_t id);
