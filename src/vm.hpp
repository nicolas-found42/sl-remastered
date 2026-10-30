#pragma once

#include "tufa.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// The host keeps objects, classes, static field owners, and field ordering
// stable while a tree runs.
struct VmObject;
struct NativeEntry;
class FileState;
using VmResourceResolver =
    std::function<std::optional<uint32_t>(std::string_view, uint32_t)>;
// Float literals preserve binary32 bits; general arithmetic and x87 rounding
// parity are unsupported.
using VmValue = std::variant<int32_t, float, const VmObject *, std::monostate>;
struct VmField {
  std::string name;
  VmValue value;
  std::string descriptor;
  VmField(std::string name, VmValue value, std::string descriptor = {})
      : name(std::move(name)), value(std::move(value)),
        descriptor(std::move(descriptor)) {}
};
struct VmObject {
  const TufaClass *runtimeClass = nullptr;
  mutable std::vector<VmField> fields;
  // Nonempty only for bounded, one-dimensional reference arrays.
  std::string arrayDescriptor{};
  mutable std::vector<VmValue> arrayElements{};
};
// The host supplies initialized static values and keeps them alive during
// execution.
struct VmStaticFields {
  const TufaClass *owner;
  mutable std::vector<VmField> fields;
};
// Shared identity models an attached native resource, without narrowing a host
// pointer.
struct VmResourceAttachment {
  uint32_t type = 0;
};
struct VmResourceState {
  const VmObject *owner;
  uint32_t words[4]{};
  std::shared_ptr<VmResourceAttachment> attachment{};
};
struct VmMeasurementState {
  float value = 0.0f;
  float product = 0.0f;
  float quotient = 0.0f;
};
// The music player the Sound natives drive. The host owns it and keeps it alive
// during execution.
struct VmMusicControl {
  virtual ~VmMusicControl() = default;
  virtual void changeMusicSet(int32_t set, float musicChannelVolume) = 0;
  virtual void nextTrack() = 0;
  virtual void prevTrack() = 0;
  virtual int32_t setMode(int32_t mode) = 0;
  virtual int32_t getMode() const = 0;
  virtual void setMusicVolume(float volume) = 0;
};
class SoundEffects;
struct VmSoundState {
  float volumes[3]{1.0f, 1.0f, 1.0f};
  VmMusicControl *music = nullptr;
  SoundEffects *effects = nullptr;
};
struct VmRandomState {
  uint32_t state = 1;
};
struct VmDisplaySize {
  int width = 0;
  int height = 0;
};
struct VmGameState;
struct VmExecutionState;
class VmScheduler;
struct VmResult {
  bool ok = false;
  VmValue value = int32_t{0};
  std::string error;
  bool voidReturn = false;
};

// Lets a native call a script method on an arena object, as stock FUN_004255e0
// does for `getInfo`. The method is found by name and by the parameter types of
// the arguments, in the receiver's class and its ancestors. vmExecute installs
// it for the duration of a call and removes it on return.
using VmScriptCall =
    std::function<VmResult(const VmObject *receiver, const std::string &name,
                           std::vector<VmValue> arguments)>;
// Host hook for stock's lazy class use. Stock loads a class the first time a
// script names it (FUN_00410890) and initializes it on first use (FUN_00403e80:
// ancestors first, then static initializers). The VM calls this when a script
// names a class that is missing from the class registry it was given, or whose
// static state does not exist yet. On success the host must have added the
// class and its ancestors to that registry and its static state to the static
// field table, by running vmInitializeClass. The class may still be
// initializing (reentrant use sees partial state, as in stock). On failure the
// result's error is reported as is.
using VmClassEnsure = std::function<VmResult(const std::string &className)>;
// Arena objects and referenced classes must outlive every value returned by the
// VM.
struct HotkeyState;
class TimerState;
struct VmArena {
  TimerState *timers = nullptr;
  HotkeyState *hotkeys = nullptr;
  // Optional host-owned file state must outlive the arena's execution.
  FileState *fileState = nullptr;
  VmMeasurementState *measurementState = nullptr;
  VmSoundState *soundState = nullptr;
  // One host-owned state per execution thread; independent stock threads do not
  // share it.
  VmRandomState *randomState = nullptr;
  // Milliseconds since the host's stock-equivalent performance-timer origin.
  // Math.randomize fails explicitly when no clock is provided.
  std::function<double()> performanceMilliseconds;
  VmDisplaySize *displaySize = nullptr;
  // Host-owned world state for GameType.createNativeInstance
  // (src/natives/game_type.hpp).
  VmExecutionState *activeExecution = nullptr;
  VmGameState *gameState = nullptr;
  VmScheduler *scheduler = nullptr;
  // Filled by vmExecute while it runs; empty outside a call.
  VmScriptCall callScript;
  // Optional. Without it a class that is not registered and initialized is an
  // error, as before.
  VmClassEnsure ensureClass;
  std::function<std::optional<std::string>(const std::string&, std::string&)> loadScriptPath;
  // Native table that static initializer code uses. vmExecute fills it for the
  // duration of a call when empty.
  const std::vector<NativeEntry> *natives = nullptr;
  // Classes whose static initialization failed. Stock logs the error and keeps
  // the class marked initialized. This port refuses every later use instead of
  // continuing with partial state.
  std::vector<std::pair<const TufaClass *, std::string>> failedInitializations;
  // Nesting of vmInitializeClass calls that run static initializer code.
  unsigned initDepth = 0;
  // Set when a vmExecute call fails: the class and first source line of each
  // failing tree, innermost first.
  std::vector<std::string> failureTrace;
  // Script call depth of the native that is running, so a callback keeps
  // counting toward the depth limit.
  unsigned nativeDepth = 0;
  // Resolver is host-owned and must outlive execution.
  const VmResourceResolver *resourceResolver = nullptr;
  std::vector<std::unique_ptr<VmObject>> objects;
  // Literal bytes are interned by identity for this arena's lifetime; stock GC
  // is not modeled.
  std::vector<std::pair<std::string, const VmObject *>> strings;
  std::vector<VmResourceState> resourceStates;
  size_t limit = std::numeric_limits<size_t>::max();
  size_t arrayElementLimit = 65536;
};
std::optional<VmValue> vmActiveStaticField(const VmArena &arena,
                                           const std::string &className,
                                           const std::string &fieldName);


struct VmExecution {
  std::shared_ptr<VmExecutionState> state;
};

VmExecution vmStart(const TufaClass &cls, const std::vector<TufaNode> &tree,
                    std::vector<VmValue> locals,
                    const std::vector<NativeEntry> *natives,
                    const std::vector<const TufaClass *> *classes,
                    const std::vector<VmStaticFields> *staticFields,
                    VmArena *arena);
VmResult vmResume(VmExecution &execution, size_t nodeBudget, bool &finished);
// Allocate and initialize instance fields during an active native call; constructor runs separately.
VmResult vmAllocateNative(VmArena &arena, const std::string &className);
// Host-side operations outside an active VM execution. Constructors are called separately.
VmResult vmHostAllocate(VmArena &arena, const std::string &className,
                        const std::vector<NativeEntry> *natives,
                        const std::vector<const TufaClass *> *classes,
                        const std::vector<VmStaticFields> *staticFields);
VmResult vmHostCall(VmArena &arena, const VmObject *receiver, const std::string &name,
                    std::vector<VmValue> arguments, const std::vector<NativeEntry> *natives,
                    const std::vector<const TufaClass *> *classes,
                    const std::vector<VmStaticFields> *staticFields);

class VmScheduler {
public:
  using Clock = std::function<double()>;
  VmScheduler(VmArena &arena, const std::vector<NativeEntry> *natives,
              const std::vector<const TufaClass *> *classes,
              const std::vector<VmStaticFields> *staticFields, Clock clock);
  ~VmScheduler();
  VmScheduler(const VmScheduler &) = delete;
  VmScheduler &operator=(const VmScheduler &) = delete;
  VmResult createThread(const VmObject *owner, std::string name);
  VmResult setPriority(const VmObject *owner, int32_t priority);
  VmResult start(const VmObject *owner);
  VmResult stop(const VmObject *owner);
  VmResult sleep(float seconds);
  VmResult wait(const VmObject *object);
  VmResult notify(const VmObject *object, bool all);
  VmResult tick(size_t nodeBudget, int minPriority = 0);
  size_t threadCount() const;
  std::vector<std::string> threadNames() const;
  std::vector<std::pair<std::string, size_t>> threadProgress() const;
  std::vector<std::pair<std::string, const VmObject *>> threadWaits() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Initializes the class and its not yet initialized ancestors, parents first
// and then interfaces, as stock FUN_00403e80 does. Static field initializers
// run in declaration order: literal trees directly, and any other tree as code
// in the bounded VM, which needs an arena (and natives through arena->natives).
// Like stock, each class's static state is registered before its initializers
// run, so code that runs during initialization sees partial state of a class
// that is still initializing. If the initialization fails, the states this call
// registered are removed, the failing class is recorded in
// arena->failedInitializations and every later attempt fails loudly. Stock
// instead logs the error and keeps the class initialized. A class that an
// ancestor's initializer initializes again is an error here; stock would run
// its static initializers twice. Resolver and arena interning are not rolled
// back.
VmResult vmInitializeClass(const TufaClass &cls,
                           const std::vector<const TufaClass *> &classes,
                           std::vector<VmStaticFields> &staticFields,
                           const VmResourceResolver *resourceResolver = nullptr,
                           VmArena *arena = nullptr);

// Executes the supported subset of tree operations.
VmResult vmExecute(const std::vector<TufaNode> &tree,
                   std::vector<VmValue> locals);
// Script calls support exact-class receivers. The supported qualified native
// call uses classes.
VmResult vmExecute(const TufaClass &cls, const std::vector<TufaNode> &tree,
                   std::vector<VmValue> locals,
                   const std::vector<NativeEntry> *natives = nullptr,
                   const std::vector<const TufaClass *> *classes = nullptr,
                   const std::vector<VmStaticFields> *staticFields = nullptr,
                   VmArena *arena = nullptr);
