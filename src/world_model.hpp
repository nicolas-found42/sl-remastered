#pragma once

#include "part_config.hpp"
#include "car_runtime.hpp"

#include "instance_ids.hpp"
#include "owner_links.hpp"
#include "resource_tree.hpp"

#include <cstdint>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct VmObject;
struct GroundRuntime;
class CameraFrame;

// Host model of the part of the stock resource world that
// GameType.createNativeInstance touches on the Dummy path. See
// docs/game-creation-evidence.md. Stock node layouts are not reproduced; the
// comments name the stock offsets only where they identify a field.

// DAT_00619d68 for any type value: the types whose children inherit a sector
// link instead of starting their own (live read: 1, 2, 3, 0xf, 0x10 and 0x11).
// Type 0, the world root, is not hooked.
bool sectorHookedType(uint32_t type);

// Stock type-8 "script" resource node (FUN_00546ae0). The LinkOwner value is
// the pack resource id (stock node +0x50), which is what a link caches.
struct PartPrototype {
  OwnerLink source;
  OwnerLink partResource;
  OwnerLink decalsResource;
  OwnerLink soundResource;
  std::vector<PartSlotRecord> slots;
  uint32_t initializationCount = 1;
  ~PartPrototype() {
    setLinkOwner(source, nullptr);
    setLinkOwner(partResource, nullptr);
    setLinkOwner(decalsResource, nullptr);
    setLinkOwner(soundResource, nullptr);
  }
};
class CameraDeviceResources;
struct CameraDeviceConfig;
class CameraDeviceResources;
struct ScriptNode : LinkOwner {
  uint32_t resourceId = 0;
  std::string className;
  bool loaded = false;
  std::unique_ptr<PartPrototype> part;
  std::unique_ptr<CarPrototype> car;
  std::shared_ptr<const physics::BodyProto> cameraPrototype;
  std::shared_ptr<const CameraDeviceConfig> cameraConfig;
};

enum class ViewportList { Pending, Active, Removed };
enum class GameHook { Kind8, Kind9, Kind1c };
// The renderer keeps newly created instances pending, then inserts active
// instances in unsigned priority order (00516760 and 00516900).
class ViewportRenderAttachments {
public:
  ~ViewportRenderAttachments();
  void prepare(LinkOwner &instance, std::optional<uint32_t> initialPriority);
  bool activate(LinkOwner &instance, int32_t priority);
  void remove(LinkOwner &instance);
  std::vector<LinkOwner *> active() const;
private:
  struct Entry {
    OwnerLink instance;
    std::optional<uint32_t> priority;
  };
  std::vector<std::unique_ptr<Entry>> pending_;
  std::vector<std::unique_ptr<Entry>> active_;
};
struct ViewportInstance {
  int32_t flags;
  float left, top, width, height;
  ViewportList list = ViewportList::Pending;
  std::shared_ptr<ViewportRenderAttachments> renderAttachments;
};

// The input block constructed by the stock player factory (FUN_0054d580).
// Physical device sampling and frame-dependent axis updates are separate.
struct PlayerAxis {
  float value = 0.0f;
  int32_t deviceType = 0;
  uint32_t stamp = 0;
  float threshold = 0.1f;
  std::array<float, 5> factors{{1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
  std::array<uint32_t, 3> remaining{};
};
struct PlayerMapping {
  int32_t axis = 0;
  int32_t device = 0;
  int32_t physicalAxis = 0;
  float sample = 0.0f;
  std::array<float, 4> range{};
};
class PlayerInputState {
public:
  // Platform readers for FUN_00557430 and FUN_00557990, respectively.
  std::function<std::optional<float>(int32_t, int32_t)> sampleInput;
  std::function<std::optional<int32_t>(int32_t, int32_t)> inputType;
  // FUN_0054d650: returns a one-based slot, 0 when full, -1 for a negative
  // physical device or axis. A null result means the virtual axis is outside
  // the host's safe range (stock indexes it without checking).
  std::optional<int32_t> add(int32_t axis, int32_t device,
                             int32_t physicalAxis, float sample,
                             const std::array<float, 4> &range,
                             int32_t deviceType);
  void remove(int32_t axis, int32_t device, int32_t physicalAxis);
  bool setSmooth(int32_t axis, const std::array<float, 5> &values);
  void reset();
  // Stock input frame counter and keyboard/button sampling; unsupported analog or
  // advanced smoothing paths return nullopt.
  void beginFrame(float deltaSeconds);
  std::optional<float> sampleAxis(int32_t axis);

  const std::array<PlayerAxis, 76> &axes() const { return axes_; }
  const std::array<PlayerMapping, 152> &mappings() const { return mappings_; }
  int32_t mappingCount() const { return mappingCount_; }

private:
  std::array<PlayerAxis, 76> axes_{};
  std::array<PlayerMapping, 152> mappings_{};
  int32_t mappingCount_ = 0;
  uint32_t inputFrame_ = 0;
  float deltaMilliseconds_ = 0.0f;
};

struct PlayerConfig {
  int32_t display = 0;
  float damageMultiplier = 0.0f;
  float changeDistance = 0.0f;
  float changeVelocity = 0.0f;
};
struct PlayerInstance {
  PlayerConfig config;
  int32_t firstArgument = 0;
  int32_t playerNumber = 0;
  std::string name;
  OwnerLink font;
  OwnerLink animationOverlay;
  OwnerLink renderInstance;
  OwnerLink osd;
  OwnerLink controllable;
  OwnerLink viewport;
  OwnerLink camera;
  OwnerLink renderTarget;
  bool driverPoseParityReported = false;
  bool scriptedControlOverride = false;
  std::array<OwnerLink, 8> debugText;
  ~PlayerInstance() {
    setLinkOwner(renderInstance, nullptr);
    setLinkOwner(osd, nullptr);
    setLinkOwner(controllable, nullptr);
    setLinkOwner(viewport, nullptr);
    setLinkOwner(camera, nullptr);
    setLinkOwner(renderTarget, nullptr);
  }
};
// Static type-5 box dimensions supplied to 0049ab60 after the 0.5 scale.
// Collision, picking and simulation remain separate supported operations.
struct PhysicsBox {
  std::array<float, 3> halfExtents{};
  std::array<std::array<float, 3>, 2> position{};
  float radius = 0;
  uint32_t geometryFlags = 5; // zero mass (4) and created-box flag (1)
};
struct EventSubscription {
  OwnerLink target;
  uint32_t mask = 0;
  uint32_t replacementMask = 0;
  std::string replacementPayload;
  std::string method;
  ~EventSubscription() { setLinkOwner(target, nullptr); }
};
struct CursorInstance {
  OwnerLink controller;
  OwnerLink activationTarget;
  OwnerLink fontA;
  OwnerLink fontB;
  OwnerLink viewport;
  OwnerLink camera;
  OwnerLink target;
  OwnerLink pointer;
  OwnerLink style;
  OwnerLink hoveredBox;
  OwnerLink pressedBox;
  int32_t mode = 0;
  char glyph = 0;
  float x = 0.0f;
  float y = 0.0f;
  int32_t color = -1;
  uint32_t flags = 3;
  bool active = false;
  bool leftDown = false;
  bool dragging = false;
  std::array<float, 3> pressContact{};
  std::array<float, 2> pressPosition{};
  float radius = 3.0f;
};

// Type-12 camera resource payload initialized by 00539500 and 004861e0.
// Renderer-side projection matrices and viewport attachments are separate.
struct CameraFog {
  int32_t color;
  float nearDistance;
  float farDistance;
};
struct CameraResource {
  float viewportAspect = 0;
  float fieldOfView = 0;
  float nearDistance = 0;
  float farDistance = 0;
  float lodBias = 0;
  float lodAmplitude = 0;
  int32_t culling = 0;
  int32_t planeTest = 0;
  std::optional<CameraFog> fog;
};
// Stock 00544d40 stores one mode word followed by six float words.
struct ResourceBounds {
  uint32_t mode = 2;
  std::array<float, 6> values{};
};
CameraResource makeCameraResource(float viewportAspect, float fieldOfView,
                                  float nearDistance, float farDistance,
                                  float lodBias, float lodAmplitude,
                                  int32_t culling, int32_t planeTest);

// Fields copied or derived at 0050dee7-0050df7a. This is not the full
// renderer object or its view/projection matrices.
struct CameraRendererFields {
  float viewportAspect;
  float fieldOfView;
  float nearDistance;
  float farMetric;
  float lodBias;
  float lodAmplitude;
  int32_t planeTest;
};
CameraRendererFields makeCameraRendererFields(const CameraResource &camera);
CameraRendererFields updateCameraRendererFields(const CameraResource &camera,
                                               float fallbackAspect);
// Perspective matrix from 004ba4b0 -> 0058eadb. Host trigonometry may round
// differently from the x87 instructions used by the original.
std::array<float, 16> cameraProjectionMatrix(const CameraRendererFields &fields);
// Inverts the root bone's world matrix. Singular or nonfinite inputs return
// null instead of propagating the original's undefined divide-by-zero result.
std::optional<std::array<float, 16>>
cameraViewMatrix(const std::array<float, 16> &worldMatrix);
struct CameraViewState {
  CameraRendererFields fields;
  std::array<float, 16> view;
  std::array<float, 16> projection;
};
// Builds the supported camera frame state from its resource, current bone
// matrix and actual viewport aspect. Invalid matrices stop the caller.
std::optional<CameraViewState>
cameraViewState(const CameraResource &camera,
                const std::array<float, 16> &boneWorldMatrix,
                float viewportAspect);
// Five post-adjustment far and side planes from 0050fcc0 -> 00510fc0.
// Rejects nonfinite inputs; x87 rounding differs from host arithmetic.
std::optional<std::array<std::array<float, 4>, 5>>
cameraFrustumPlanes(const CameraRendererFields &fields,
                    const std::array<float, 16> &boneWorldMatrix);
// Produces normalized coordinates after the homogeneous divide for points
// in front of the camera. Clipping and viewport mapping are separate.
std::optional<std::array<float, 3>>
projectCameraPoint(const std::array<float, 3> &point,
                   const std::array<float, 16> &view,
                   const std::array<float, 16> &projection);
// Unprojects normalized camera coordinates through inverse projection and the
// camera world transform (004fdb70 -> 00513840). Rejects singular matrices.
std::optional<std::array<float, 3>>
unprojectCameraPoint(const std::array<float, 3> &point,
                     const std::array<float, 16> &world,
                     const std::array<float, 16> &projection);
// Maps a point inside the D3D clip volume to the viewport in output pixels.
// A point outside returns null; this does not clip intersecting triangles.
std::optional<std::array<float, 2>>
cameraViewportPoint(const std::array<float, 3> &normalized,
                    const ViewportInstance &viewport, int32_t outputWidth,
                    int32_t outputHeight);
struct CameraClipVertex {
  std::array<float, 4> position;
  std::array<float, 2> uv;
};
// Clip a triangle in homogeneous coordinates to the D3D view volume. The
// returned polygon is ordered; callers triangulate it as a fan.
std::optional<std::vector<CameraClipVertex>>
clipCameraTriangle(const std::array<CameraClipVertex, 3> &triangle);

// Stock type-1 node (FUN_0053e6e0). The LinkOwner value is the instance id
// (+0x50), and its list is the list of links that point at the node (+0x48).
struct WorldNode : LinkOwner {
  ResourceTreeHandle handle;
  uint32_t type = 1;
  uint32_t instanceId = 0;
  // +0x54. Descriptor flags 0x21, or 0x200 once the creation function has run.
  uint32_t flags = 0;
  std::string name;
  // Stock +0x64 bit 0, set by FUN_00544b90(1).
  bool loaded = false;
  // True only for instances counted by the supported creation paths.
  bool countedInstance = false;
  // Stock +0x68, set to 1.0f by the creation function.
  float loadPriority = 0.0f;
  // Stock +0x6c and +0x70. The base constructor can leave these dependent on
  // malloc contents when the global frame counter is zero; null preserves
  // that uncertainty until initialization or inheritance is established.
  std::optional<float> field6c;
  std::optional<float> field70;
  uint32_t stamp74 = 0;
  uint32_t stamp78 = 0;
  uint32_t stamp7c = 0;
  bool recentUseQueued = false;
  float visibilityBias = 0.0f;
  float visibilityGain = 1.0f;
  std::optional<ViewportInstance> viewport;
  std::optional<CameraResource> camera;
  std::optional<PhysicsBox> physicsBox;
  // Shared with the incomplete ground preparation so the body outlives this node.
  std::shared_ptr<GroundRuntime> groundBody;
  // The host frame and its viewport link exist only for a created camera
  // render instance. Host renderer and ResourceWorld must outlive this node.
  std::shared_ptr<CameraFrame> cameraFrame;
  OwnerLink cameraViewport;
  ResourceBounds bounds;
  // Native-created type-1 payload starts with zero POLY records (0053dd80).
  std::optional<uint32_t> polygonCount = 0;
  uint32_t eventMask = 0;
  std::vector<std::unique_ptr<EventSubscription>> eventSubscriptions;
  // Link at +0x110 to the type-8 script node (FUN_00429060).
  OwnerLink scriptLink;
  // Link at +0xc0 to the spatial sector root (FUN_00544fe0 when the node is
  // created, FUN_00544f40 for GameRef.WORLDTREEROOT). Under the type-0 world
  // root a node becomes its own sector root.
  OwnerLink sectorLink;
  // The `native <device>` line of the script resource (FUN_005461a0). Stock
  // builds the device instance through its factory table and stores it at
  // +0xc; the port records the name until the device factories are ported.
  std::string nativeDevice;
  // Populated only once the player factory has created its input block.
  std::unique_ptr<PlayerInputState> playerInput;
  std::unique_ptr<PlayerInstance> player;
  std::unique_ptr<CursorInstance> cursor;
  std::unique_ptr<CarRuntimeInstance> carInstance;
  std::shared_ptr<CameraDeviceResources> cameraDevice;
  // The attached script receiver and whether its subscription list is known
  // empty. Unknown subscription state must not be treated as an empty list.
  const VmObject *scriptReceiver = nullptr;
  bool eventSubscriptionsKnownEmpty = false;
  // True once +0x128 holds a script receiver. The receiver itself belongs to
  // the VM. +0x124, the native instance, stays null: a `script <class>` payload
  // has no factory.
  bool hasReceiver = false;
};

// 005100b0 follows the camera render instance's sector link to choose the
// traversal root. A null result includes missing or unsupported instances.
LinkOwner *cameraTraversalRoot(WorldNode &cameraRenderInstance);

struct PlayerCarNote {
  uint32_t count = 0;
  uint32_t instanceId = 0;
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  float speed = 0.f;
  int32_t gear = 0;
  float rpm = 0.f;
};

class GameWorld {
public:
  GameWorld();
  GameWorld(const GameWorld &) = delete;
  GameWorld &operator=(const GameWorld &) = delete;
  ~GameWorld();

  // DAT_006200a4 starts at 1 in the stock executable and is advanced by
  // the main loop. The host loop must update it as frames advance.
  uint32_t frameCounter() const;
  void setFrameCounter(uint32_t frame);

  // Stands in for stock FUN_00410890 (class lookup by name). Without it,
  // loading a script node is unsupported.
  using ClassExists = std::function<bool(const std::string &)>;
  void setClassResolver(ClassExists resolver) {
    classExists_ = std::move(resolver);
  }
  const ClassExists &classResolver() const { return classExists_; }

  // FUN_00535e70: the world constructor's only top-level node. A type-0 base
  // object named `nulltype` with an all-zero descriptor (type 0, id 0, flags
  // 0), marked loaded with priority 1.0. It has no instance id, is not found by
  // id, and has no sector link: stock allocates only 0x80 bytes for it. Returns
  // null when a root exists.
  WorldNode *addRoot();
  // First top-level node: stock *(world+0x11c) when its next pointer is not
  // null. The type-0 root.
  WorldNode *defaultParent();
  WorldNode *findInstance(uint32_t instanceId);
  WorldNode *findByReceiver(const VmObject *receiver);
  ScriptNode *findScript(uint32_t resourceId);
  ScriptNode *addScript(uint32_t resourceId, const std::string &className);
  ScriptNode *classScript(const std::string &className);

  // Creates a type-1 child of parent in stock order (appended) and runs the
  // sector rule of FUN_00544fe0: a parent whose type is not hooked (the type-0
  // root) or a node with flag 0x10 makes the node its own sector root;
  // otherwise the parent's sector link is copied. Returns null when ids are
  // exhausted.
  WorldNode *addChild(WorldNode &parent, const std::string &name,
                      uint32_t flags);
  WorldNode *addTypedChild(WorldNode &parent, uint32_t type,
                           const std::string &name, uint32_t flags);
  // ResourceWorld and GameWorld share this tree, so pack-mounted nodes can
  // be parents of instances without transferring ownership of either node.
  std::optional<ResourceTreeHandle> attachExternal(ResourceTreeHandle parent,
                                                    LinkOwner &owner);
  bool detachExternal(ResourceTreeHandle handle);
  WorldNode *addChildUnderExternal(ResourceTreeHandle parent, LinkOwner &owner,
                                   uint32_t parentType, LinkOwner *parentSector,
                                   const std::string &name, uint32_t flags);
  WorldNode *addTypedChildUnderExternal(ResourceTreeHandle parent, LinkOwner &owner,
                                        uint32_t parentType, LinkOwner *parentSector,
                                        uint32_t type, const std::string &name,
                                        uint32_t flags);
  std::optional<std::vector<LinkOwner *>> childOwners(ResourceTreeHandle parent) const;
  LinkOwner *parentOwner(ResourceTreeHandle child) const;
  // FUN_00537d80 / FUN_00537de0 keep per-type loaded and unloaded lists.
  void trackLoadState(LinkOwner &owner, uint32_t type, bool loaded);
  void forgetLoadState(LinkOwner &owner);
  std::vector<LinkOwner *> loadList(uint32_t type, bool loaded) const;
  void markLoaded(WorldNode &node);
  void noteRecentUse(LinkOwner &owner, uint32_t type, uint32_t &stamp, bool &queued);
  std::vector<LinkOwner *> recentlyUsed(uint32_t type) const;
  void finishRecentUse(LinkOwner &owner, uint32_t type);
  void registerViewport(WorldNode &node, int32_t initialPriority);
  bool activateViewport(WorldNode &node);
  void deactivateViewport(WorldNode &node);
  std::vector<WorldNode *> activeViewports();
  bool attachConfig(WorldNode &node);
  WorldNode *configNode();
  bool attachGameParent(LinkOwner &node);
  std::optional<uint32_t> gameParentId() const;
  void setCursorConfiguration(float sensitivity, int32_t systemCursor) {
    cursorSensitivity_ = sensitivity * 1000.0f;
    systemCursor_ = systemCursor;
  }
  float cursorSensitivity() const { return cursorSensitivity_; }
  int32_t systemCursor() const { return systemCursor_; }
  using CursorInput = std::function<std::optional<std::array<float, 3>>() >;
  void setCursorInput(CursorInput input) { cursorInput_ = std::move(input); }
  const CursorInput &cursorInput() const { return cursorInput_; }
  const std::vector<WorldNode *> &staticBoxes() const { return boxBodies_; }
  using CursorSelector = std::function<bool(int32_t, bool, std::string &)>;
  void setCursorSelector(CursorSelector selector) { cursorSelector_ = std::move(selector); }
  const CursorSelector &cursorSelector() const { return cursorSelector_; }
  bool changeCursorVisibility(bool show, std::string &error);
  int32_t cursorDisplayCount() const { return cursorDisplayCount_; }
  void initPlayerFactory(LinkOwner &soundResource);
  bool attachPlayerInput(WorldNode &node);
  const std::vector<WorldNode *> &playerInputs() const { return playerInputs_; }
  void setInputDevices(std::vector<std::string> names) { inputDevices_ = std::move(names); }
  const std::optional<std::vector<std::string>> &inputDevices() const { return inputDevices_; }
  void setInputReaders(std::function<std::optional<float>(int32_t, int32_t)> sample,
                       std::function<std::optional<int32_t>(int32_t, int32_t)> type) {
    inputSample_ = std::move(sample);
    inputType_ = std::move(type);
  }
  void configurePlayerInput(PlayerInputState &input) const {
    input.sampleInput = inputSample_;
    input.inputType = inputType_;
  }
  // ResourceWorld removes its own observer before it is destroyed.
  uint64_t addNodeDestroyObserver(std::function<void(LinkOwner &)> observer);
  void removeNodeDestroyObserver(uint64_t token);
  // Removes the node and its descendants, detaches every link that points at
  // them and releases their ids.
  bool destroy(WorldNode &node);
  bool reparent(WorldNode &node, LinkOwner &newParent);
  // 00427620 appends linked records; 004277f0 drains them in order.
  // Only the modeled camera resource and render instance are supported.
  bool queueDestroy(WorldNode &node);
  bool drainDeferredDestroy(std::string &error);
  size_t deferredDestroyCount() const { return deferredDestroy_.size(); }

  // The world node that a link owner is, or null (a script node, or a node of
  // another world).
  WorldNode *nodeFor(LinkOwner *owner);
  // FUN_00427210 appends a link for each nonzero getInfo result. Detached
  // links remain in the list after their target is destroyed.
  bool registerHook(GameHook kind, WorldNode &node);
  bool registerHook(GameHook kind, LinkOwner *owner);
  void unregisterHook(GameHook kind, LinkOwner *owner);
  void pruneDetachedHooks(GameHook kind);
  std::vector<const WorldNode *> hooks(GameHook kind) const;
  // Stock FUN_00486c80 after its argument parsing: flags (+0x54) |= bits. Bit
  // 0x10 (GameRef.WORLDTREEROOT) runs FUN_00544f40, which points the node's own
  // sector link (+0xc0) at the node itself.
  void orFlags(WorldNode &node, uint32_t bits);

  size_t nodeCount() const { return nodes_.size(); }
  // Cars are nodes whose runtime is marked isCar. count 1 carries that node's
  // instance id and host position. More than one does not pick a winner.
  PlayerCarNote playerCarNote() const;
  bool hasNodeFlag(uint32_t flag) const;
  bool hasNodeType(uint32_t type) const;
  size_t countNodeType(uint32_t type) const;
  bool hasLoadedNodeType(uint32_t type) const;
  std::optional<std::vector<WorldNode *>> children(const WorldNode &node) const;
  const WorldNode *parent(const WorldNode &node) const;
  // Supported subset of stock DAT_00765f60: created and loaded type-1 instances.
  // Bounded 004a5190 step. Empty physics and static boxes use the free-body
  // step. An armed direct camera follow is integrated with cameraFollowForce
  // after world forces. Other dynamic bodies still fail the call after that
  // camera step.
  bool stepEmptyPhysics(float seconds, std::string &error);
  // Applies cameraFollowForce while other dynamic bodies stay registered.
  // Does not solve collision, joints, or wheel contacts.
  bool stepArmedCameraFollow(float seconds, std::string &error);
  // True when stepEmptyPhysics will apply cameraFollowForce to this camera.
  // On false, *block is a 4-character code for the first failed predicate.
  bool directCameraFollowReady(const CameraDeviceResources &camera,
                               const char **block = nullptr);
  float physicsTime() const { return physicsWorld_.time; }
  // Stock player getInfo(0x43): normalized sampled value for axes 1..75.
  // nullopt means the target, axis-zero scratch value, or input mode is unsupported.
  std::optional<float> playerControlValue(LinkOwner *player, int32_t axis);
  bool registerStaticBox(WorldNode &node, const std::array<float, 3> &halfExtents);
  bool registerDynamicBody(WorldNode &node, std::shared_ptr<physics::Body> body,
                           std::shared_ptr<const physics::BodyProto> prototype);
  bool unregisterDynamicBody(WorldNode &node);
  std::weak_ptr<physics::Body> dynamicBody(WorldNode &node) const;
  size_t dynamicBodyCount() const { return dynamicBodies_.size(); }
  size_t staticBoxGeometryCount() const { return boxGeometries_.size(); }
  size_t staticBoxBodyCount() const { return boxBodies_.size(); }
  uint32_t createdInstances() const { return createdInstances_; }
  void noteInstanceCreated(WorldNode &node);
  void countInstance(bool &counted);
  void uncountInstance(bool &counted);

private:
  WorldNode *install(ResourceTreeHandle handle, const std::string &name,
                     uint32_t flags, bool allocateId = true);
  void attachSector(WorldNode &node, const WorldNode &parent);
  bool destroySubtree(ResourceTreeHandle handle);
  void release(WorldNode &node);
  bool armedCameraFollowCount(int &ready, std::string &error);
  bool integrateArmedCameraFollows(float seconds, bool &otherDynamic, std::string &error);
  ResourceTree tree_;
  InstanceIds ids_;
  std::unordered_map<uint64_t, std::unique_ptr<WorldNode>> nodes_;
  std::unordered_map<uint64_t, LinkOwner *> externalNodes_;
  std::unordered_map<uint32_t, WorldNode *> byInstance_;
  std::map<uint32_t, std::unique_ptr<ScriptNode>> scripts_;
  std::map<std::string, uint32_t> classScripts_;
  ClassExists classExists_;
  std::array<std::vector<std::unique_ptr<OwnerLink>>, 3> hooks_;
  std::vector<std::unique_ptr<OwnerLink>> deferredDestroy_;
  bool drainingDestroy_ = false;
  std::vector<WorldNode *> boxGeometries_;
  std::vector<WorldNode *> boxBodies_;
  struct DynamicBodyOwnership {
    std::shared_ptr<const physics::BodyProto> prototype;
    std::shared_ptr<physics::Body> body;
  };
  std::map<WorldNode *, std::shared_ptr<DynamicBodyOwnership>> dynamicBodies_;
  physics::World physicsWorld_;
  std::map<uint32_t, std::vector<LinkOwner *>> recentlyUsed_;
  std::map<uint32_t, std::vector<LinkOwner *>> loadedNodes_;
  std::map<uint32_t, std::vector<LinkOwner *>> unloadedNodes_;
  ViewportRenderAttachments viewportInventory_;
  OwnerLink configLink_;
  OwnerLink gameParentLink_;
  OwnerLink playerFactorySoundA_;
  OwnerLink playerFactorySoundB_;
  float cursorSensitivity_ = 0.0f;
  int32_t systemCursor_ = 0;
  CursorSelector cursorSelector_;
  int32_t cursorDisplayCount_ = 0;
  CursorInput cursorInput_;
  std::vector<WorldNode *> playerInputs_;
  std::optional<std::vector<std::string>> inputDevices_;
  std::function<std::optional<float>(int32_t, int32_t)> inputSample_;
  std::function<std::optional<int32_t>(int32_t, int32_t)> inputType_;
  std::map<uint64_t, std::function<void(LinkOwner &)>> nodeDestroyObservers_;
  uint64_t nextNodeDestroyObserver_ = 1;
  uint32_t createdInstances_ = 0;
};
