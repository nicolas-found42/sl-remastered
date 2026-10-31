#pragma once

#include "pack_registry.hpp"
#include "world_model.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

class ResourceWorld;

bool readNativeConfig(PackRegistry& packs, const std::string& path,
                      std::string& contents, std::string& error);
bool readNativeDataFile(PackRegistry& packs, const std::string& path,
                        size_t maximumBytes, std::string& contents, std::string& error);
bool loadCarScriptPrototype(ResourceWorld& resources, ScriptNode& source,
                            const std::string& configPath, std::string& error,
                            const CarCameraSettings* cameraSettings = nullptr);
bool loadPartScriptPrototype(ResourceWorld& resources, ScriptNode& source,
                             const std::string& configPath, std::string& error);
bool loadButtonScriptPrototype(ResourceWorld& resources, ScriptNode& source,
                               const std::string& configPath, std::string& error);

// A validated request only: it does not create, allocate, attach or start an
// object.
struct GameCreationRequest {
  uint32_t scriptResourceId = 0;
  // Empty for a type-8 resource with a native device but no script directive.
  std::string scriptClass;
  // Empty means the stock handler's default parent; otherwise this is the
  // supplied parent resource ID.
  std::optional<uint32_t> parentResourceId;
  std::optional<std::string> constructorArgument;
  std::string name;
  // The `native <device>` line of the script resource, if present.
  std::string nativeDevice;
  // Optional config path follows the native device on the type-8 resource line.
  std::optional<std::string> nativeConfigPath = std::nullopt;
};
struct NativePayloadDirective {
  std::string device;
  std::optional<std::string> configPath;
};
std::optional<NativePayloadDirective> parseNativePayloadDirective(const std::string& text);
// Bounded type-8 payload containing only one native directive.
std::optional<NativePayloadDirective> readNativeOnlyGameType(
    PackRegistry& registry, uint32_t scriptResourceId, std::string& error);
struct GameInstanceDescriptor {
  uint32_t type = 1;
  uint32_t flags = 0x21;
  std::string name;
};
// Stock copies at most 30 bytes, despite allocating a 32-byte buffer.
GameInstanceDescriptor
prepareGameInstanceDescriptor(const GameCreationRequest &request);
struct GameCreationResult {
  std::optional<GameCreationRequest> request;
  std::string error;
};
GameCreationResult
prepareGameCreation(PackRegistry &registry, uint32_t scriptResourceId,
                    std::optional<uint32_t> parentResourceId,
                    std::optional<std::string> constructorArgument,
                    std::optional<std::string> name,
                    const std::function<std::optional<std::string>(const std::string &, std::string &)> &pathResolver = {});

// What stock gives the handler as the receiver's `ptr` field: a 16-byte link
// record {previous, next, id, node} that FUN_0047cea0 (ResourceRef.newNative)
// allocated earlier. `link.cachedValue` is the stock id word (record +8). Keep
// the binding at a stable address. Either side may die first: the destructor
// detaches the link, and destroying a node or the world detaches its links.
struct ReceiverBinding {
  ReceiverBinding() = default;
  ~ReceiverBinding() { setLinkOwner(link, nullptr); }
  OwnerLink link;
  // FUN_00427210 asks the receiver script `GameType.getInfo(I)I` three times
  // through FUN_00426470 -> FUN_00425a90 -> FUN_004255e0. The Java argument is
  // the record kind as an int (8, 9, 0x1c; read from the instruction at
  // 00426587). The host cannot run scripts, so the VM supplies this callback.
  // Without it, or when it returns nullopt because the script call failed, the
  // creation is reported unsupported.
  std::function<std::optional<int32_t>(int kind)> getInfo;
  // For GameRef.create, initialize the script receiver after node creation and
  // before the stock getInfo callbacks.
  std::function<std::optional<std::string>(WorldNode &)> initializeReceiver;
};

// Resolved stock factory dependencies. The resource owners outlive creation;
// the resulting node keeps its own stable link to the font.
struct PlayerFactorySetup {
  PlayerConfig config;
  LinkOwner *soundResource = nullptr;
  LinkOwner *fontResource = nullptr;
};

enum class CreationStatus {
  Created,
  // The stock handler would log a fatal error or crash on this input.
  StockError,
  // The stock path continues into machinery the host does not model. The node
  // tree and instance ids are left as they were; a loaded script node stays
  // loaded.
  Unsupported,
};
enum class CreationIssue {
  None,
  NoReceiverLink,
  ScriptClassMismatch,
  ScriptClassMissing,
  ParentNotFound,
  // Unsupported:
  ZeroScriptId,
  ClassResolverMissing,
  ParentNotLoaded,
  NativeDeviceUnsupported,
  InstanceIdsExhausted,
  GetInfoUpcallMissing,
  GetInfoFailed,
  GetInfoRegistration,
};
struct CreationResult {
  CreationStatus status = CreationStatus::StockError;
  CreationIssue issue = CreationIssue::None;
  std::string detail;
  WorldNode *node = nullptr;
  uint32_t instanceId = 0;
};
// Host model of FUN_00481a70 after argument parsing, for a nonzero script id:
// FUN_00546070 (script ref), FUN_0053a2a0 (node creation) and the receiver
// attachment. `request` comes from prepareGameCreation.
CreationResult createNativeInstance(GameWorld &world,
                                    const GameCreationRequest &request,
                                    ReceiverBinding *receiver,
                                    ResourceWorld *resources = nullptr,
                                    const PlayerFactorySetup *player = nullptr,
                                    const CarCameraSettings *cameraSettings = nullptr);
