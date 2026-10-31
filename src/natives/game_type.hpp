#pragma once

#include "game_creation.hpp"
#include "natives/registry.hpp"
#include "pack_registry.hpp"
#include "platform.hpp"
#include "world_model.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

struct VmStaticFields;
class FrameVisibilityState;
class VmVideoControl {
public:
  virtual ~VmVideoControl() = default;
  virtual bool open(const std::string &path, bool loop, double now,
                    std::string &error) = 0;
  virtual void close() = 0;
  virtual bool playing() const = 0;
};
// Host-owned state behind GameType.createNativeInstance. The host keeps `packs`
// and `world` alive for the arena's lifetime. One ReceiverBinding per receiver
// handle (the receiver's `ptr` field), so a second call on the same receiver
// moves the same link, as the stock handler does with the receiver's link
// record.
class ResourceWorld;
class Render2D;
struct ResourceNode;
struct VmGameState {
  PackRegistry *packs = nullptr;
  GameWorld *world = nullptr;
  std::map<int32_t, std::unique_ptr<ReceiverBinding>> bindings;
  // Every receiver `getInfo(kind)` script call the handler made, with the
  // answer, in order.
  std::vector<std::pair<int32_t, int32_t>> getInfoLog;
  // Pack mount and id lookup behind ResourceRef.set(I)V. Optional; the host
  // destroys it before `world` and `packs`.
  ResourceWorld *resources = nullptr;
  // Host-owned renderer. It must outlive all world nodes holding CameraFrame.
  Render2D *renderer = nullptr;
  FrameVisibilityState *visibility = nullptr;
  VmVideoControl *video = nullptr;
  float playerAnimationDelta = 0.0f;
  int32_t gameTimeOfDay = 0;
  bool gameNight = true;
  std::optional<int32_t> mouseHelp{};
  std::optional<std::vector<PlatformDisplayMode>> displayModes{};
  std::function<std::optional<PlatformDisplayMode>()> currentDisplayMode{};
  // Host implements the stock seven resource/render iterations synchronously.
  std::function<bool(std::string &)> forceRendering{};
  // Host implements stock flush 0047c2d0: the seven passes, then the forced
  // 00537ed0(1) aging sweep.
  std::function<bool(std::string &)> flush{};
  int32_t textDefaultColor = -1;
  OwnerLink globalEnvmap{};
};
NativeResult gfxSetGlobalEnvmap(const std::vector<VmValue> &args,
                                const VmObject *receiver, VmArena *arena,
                                const std::vector<const TufaClass *> *classes);
NativeResult gfxForceRendering(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *classes);
NativeResult gfxFlush(const std::vector<VmValue> &args,
                      const VmObject *receiver, VmArena *arena,
                      const std::vector<const TufaClass *> *classes);
class VmScheduler;
// Arena, scheduler and world outlive the dispatch. Resume after scheduler
// ticks; true means pending or successful, and finished() distinguishes
// completion. Destroying a pending dispatch cancels its host task on the next
// scheduler tick.
class CursorDispatch {
public:
  CursorDispatch(VmArena &, VmScheduler &, WorldNode &, std::string,
                 const std::vector<const TufaClass *> &,
                 const std::vector<VmStaticFields> &,
                 WorldNode *sourceOverride = nullptr);
  ~CursorDispatch();
  CursorDispatch(const CursorDispatch &) = delete;
  CursorDispatch &operator=(const CursorDispatch &) = delete;
  bool resume(std::string &error);
  bool finished() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class AnimationDispatch {
public:
  AnimationDispatch(VmArena &, VmScheduler &, float,
                    const std::vector<const TufaClass *> &,
                    const std::vector<VmStaticFields> &);
  ~AnimationDispatch();
  AnimationDispatch(const AnimationDispatch &) = delete;
  AnimationDispatch &operator=(const AnimationDispatch &) = delete;
  bool resume(std::string &error);
  bool finished() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Handler at 00481a70. Parses (GameRef parent|null, int scriptResourceId,
// String argument|null, String name|null), runs prepareGameCreation and
// createNativeInstance, and on success stores the new node's instance id in the
// receiver's resource state (`words[2]`). Unsupported and stock-error outcomes
// become a VM error with detail.
NativeResult
gameTypeCreateNativeInstance(const std::vector<VmValue> &args,
                             const VmObject *receiver, VmArena *arena,
                             const std::vector<const TufaClass *> *classes);

// Handler at 00486c80, GameRef.setFlags(I)V. ORs the bits into the receiver's
// world node; bit 0x10 also points the node's sector link at itself. A receiver
// whose link has no node is left alone, as in stock. A receiver with no native
// link at all is an error (stock dereferences a null record).
NativeResult gameRefSetFlags(const std::vector<VmValue> &args,
                             const VmObject *receiver, VmArena *arena,
                             const std::vector<const TufaClass *> *classes);

NativeResult gameRefClearFlags(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *classes);
NativeResult gameRefQueueEvent(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *classes);
// Load the existing bounded ground factory without registering a notification.
// A failed partially prepared factory must be reset before retrying.
std::optional<std::string> loadGroundGameResource(
    VmArena &arena, ResourceNode &node,
    const std::vector<VmStaticFields> *staticFields = nullptr);
NativeResult
gameTypeAddNotification(const std::vector<VmValue> &args,
                        const VmObject *receiver, VmArena *arena,
                        const std::vector<const TufaClass *> *classes);
NativeResult gameRefCreate(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *arena,
                           const std::vector<const TufaClass *> *classes);
NativeResult controllerUserReset(const std::vector<VmValue> &args,
                                 const VmObject *receiver, VmArena *arena,
                                 const std::vector<const TufaClass *> *classes);
NativeResult controllerUserDel(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *classes);
NativeResult
controllerUserSetAxisSmooth(const std::vector<VmValue> &args,
                            const VmObject *receiver, VmArena *arena,
                            const std::vector<const TufaClass *> *classes);
NativeResult controllerUserAdd(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *arena,
                               const std::vector<const TufaClass *> *classes);
NativeResult inputGetDeviceName(const std::vector<VmValue> &args,
                                const VmObject *receiver, VmArena *arena,
                                const std::vector<const TufaClass *> *classes);
NativeResult
gameTypeSetEventMask(const std::vector<VmValue> &args, const VmObject *receiver,
                     VmArena *arena,
                     const std::vector<const TufaClass *> *classes);
NativeResult
gameTypeClearEventMask(const std::vector<VmValue> &args,
                       const VmObject *receiver, VmArena *arena,
                       const std::vector<const TufaClass *> *classes);

NativeResult gameRefIsScripted(const std::vector<VmValue> &, const VmObject *,
                               VmArena *,
                               const std::vector<const TufaClass *> *);

NativeResult inputCreateHotkey(const std::vector<VmValue> &, const VmObject *,
                               VmArena *,
                               const std::vector<const TufaClass *> *);
NativeResult inputDeleteHotkey(const std::vector<VmValue> &, const VmObject *,
                               VmArena *,
                               const std::vector<const TufaClass *> *);
NativeResult inputFlushHotkeys(const std::vector<VmValue> &, const VmObject *,
                               VmArena *,
                               const std::vector<const TufaClass *> *);
NativeResult inputCheckHotkeys(const std::vector<VmValue> &, const VmObject *,
                               VmArena *,
                               const std::vector<const TufaClass *> *);
NativeResult gameTypeAddTimer(const std::vector<VmValue> &, const VmObject *,
                              VmArena *,
                              const std::vector<const TufaClass *> *);
NativeResult gameTypeRemoveAllTimers(const std::vector<VmValue> &,
                                     const VmObject *, VmArena *,
                                     const std::vector<const TufaClass *> *);
bool dispatchTimerEvent(VmArena &, LinkOwner &, int32_t, uint32_t event,
                        int32_t param, const std::vector<const TufaClass *> &,
                        const std::vector<VmStaticFields> &, std::string &);
NativeResult gameTypeRegisterCallback(const std::vector<VmValue> &,
                                      const VmObject *, VmArena *,
                                      const std::vector<const TufaClass *> *);
NativeResult gameTypeUnregisterCallback(const std::vector<VmValue> &,
                                        const VmObject *, VmArena *,
                                        const std::vector<const TufaClass *> *);
NativeResult
gameTypeUnregisterCallbacks(const std::vector<VmValue> &, const VmObject *,
                            VmArena *, const std::vector<const TufaClass *> *);
NativeResult inputLastKey(const std::vector<VmValue> &, const VmObject *,
                          VmArena *, const std::vector<const TufaClass *> *);
NativeResult inputGetAxis(const std::vector<VmValue> &, const VmObject *,
                          VmArena *, const std::vector<const TufaClass *> *);

NativeResult gfxNumDisplayModes(const std::vector<VmValue> &, const VmObject *,
                                VmArena *,
                                const std::vector<const TufaClass *> *);
NativeResult gfxDisplayModeName(const std::vector<VmValue> &, const VmObject *,
                                VmArena *,
                                const std::vector<const TufaClass *> *);

NativeResult gfxCurrDisplayMode(const std::vector<VmValue> &, const VmObject *,
                                VmArena *,
                                const std::vector<const TufaClass *> *);

NativeResult gameTypeRemNotification(const std::vector<VmValue> &,
                                     const VmObject *, VmArena *,
                                     const std::vector<const TufaClass *> *);
NativeResult gameRefCreateNative(const std::vector<VmValue> &, const VmObject *,
                                 VmArena *,
                                 const std::vector<const TufaClass *> *);
NativeResult gameRefGetDetail(const std::vector<VmValue> &, const VmObject *,
                              VmArena *,
                              const std::vector<const TufaClass *> *);
NativeResult gameRefGetFlags(const std::vector<VmValue> &, const VmObject *,
                             VmArena *, const std::vector<const TufaClass *> *);
NativeResult gameRefGetInfoIntInt(const std::vector<VmValue> &,
                                  const VmObject *, VmArena *,
                                  const std::vector<const TufaClass *> *);
NativeResult gameRefGetInfoIntString(const std::vector<VmValue> &,
                                     const VmObject *, VmArena *,
                                     const std::vector<const TufaClass *> *);
NativeResult gameRefGetOri(const std::vector<VmValue> &, const VmObject *,
                           VmArena *, const std::vector<const TufaClass *> *);
NativeResult gameRefGetPos(const std::vector<VmValue> &, const VmObject *,
                           VmArena *, const std::vector<const TufaClass *> *);
NativeResult gameRefGetScriptInstance(const std::vector<VmValue> &,
                                      const VmObject *, VmArena *,
                                      const std::vector<const TufaClass *> *);
NativeResult gameRefGetVel(const std::vector<VmValue> &, const VmObject *,
                           VmArena *, const std::vector<const TufaClass *> *);
NativeResult gameRefIsEmpty(const std::vector<VmValue> &, const VmObject *,
                            VmArena *, const std::vector<const TufaClass *> *);
NativeResult gameRefSetActiveCollision(const std::vector<VmValue> &,
                                       const VmObject *, VmArena *,
                                       const std::vector<const TufaClass *> *);
NativeResult gameRefSetMatrix(const std::vector<VmValue> &, const VmObject *,
                              VmArena *,
                              const std::vector<const TufaClass *> *);
NativeResult gameRefSetParent(const std::vector<VmValue> &, const VmObject *,
                              VmArena *,
                              const std::vector<const TufaClass *> *);
NativeResult gameRefSetPos(const std::vector<VmValue> &, const VmObject *,
                           VmArena *, const std::vector<const TufaClass *> *);
NativeResult gameRefSetState(const std::vector<VmValue> &, const VmObject *,
                             VmArena *, const std::vector<const TufaClass *> *);

struct CarPrototype;
bool carPartSlotsCompatible(const CarPrototype &proto1, int32_t slotId1,
                            const CarPrototype &proto2, int32_t slotId2);
// `preferred`, when supplied, is the install position stock scores candidates
// by (00473790/004720a0 keep the nearest slot); null keeps the first match.
int32_t
carPartFindCompatibleSlot(WorldNode &partNode, WorldNode *&inOutTargetNode,
                          int32_t &inOutTargetSlotId, GameWorld &world,
                          const std::array<float, 3> *preferred = nullptr);
bool carPartInstall(WorldNode &partNode, WorldNode *explicitTarget,
                    int32_t explicitTargetSlotId, VmArena &arena,
                    std::string &error,
                    const std::array<float, 3> *preferred = nullptr);
bool carPartRemove(WorldNode &partNode, int32_t slotId, VmArena &arena,
                   std::string &error);
void carPartUpdateMass(WorldNode &node, GameWorld &world);
NativeResult allocateVector3(VmArena *arena,
                             const std::vector<const TufaClass *> *classes,
                             float x, float y, float z);
NativeResult allocateYpr(VmArena *arena,
                         const std::vector<const TufaClass *> *classes, float y,
                         float p, float r);
