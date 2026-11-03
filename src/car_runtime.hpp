#pragma once

#include "car_config.hpp"
#include "car_engine.hpp"
#include "car_hud.hpp"
#include "car_geometry.hpp"
#include "car_physics.hpp"
#include "car_sfx_table.hpp"
#include "car_wheel_geometry.hpp"
#include "owner_links.hpp"
#include "wheel.hpp"
#include "resource_id.hpp"
#include <map>
#include <optional>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class ResourceWorld;
class GameWorld;
struct ScriptNode;
struct VmObject;
class DynoModel;

struct CarResourceBinding {
  CarResourceRequest request;
  uint32_t id = 0;
  OwnerLink owner;
  ~CarResourceBinding();
};

enum class CarPrototypeStage { Initializing,
                               PhysicsGeometryReady };

struct CarCameraSettings {
  float distanceScale;
  float fieldOfViewScale;
};

struct CarPrototype {
  OwnerLink source;
  ~CarPrototype();
  CarConfig config;
  std::optional<CarCameraSettings> cameraSettings;
  std::vector<std::unique_ptr<CarResourceBinding>> resources;
  CarSfxTable engineUp, engineDown, exhaust;
  std::vector<CarGeometryPrototype> geometry;
  std::vector<CarWheelPrototype> wheels;
  std::optional<CarGeometryPrototype> clickGeometry;
  CarPrototypeStage stage = CarPrototypeStage::Initializing;
  std::string configPath;
  bool isCar = true;
};

struct RuntimeSlotState {
  RuntimeSlotState() = default;
  RuntimeSlotState(const RuntimeSlotState&) = delete;
  RuntimeSlotState& operator=(const RuntimeSlotState&) = delete;
  int32_t id = 0;
  PartSlotState transform{};
  CarVec3 orientation{};
  int32_t slotType = 0;
  int32_t damageMode = -1;
  float deform = 1.0f;
  uint32_t flags = 0;
  OwnerLink attachedNode;
  int32_t attachedSlotId = 0;
  std::string damage;
  ~RuntimeSlotState();
};

struct CarWheelExtraState {
  CarWheelExtraState();
  float drive = 0.0f;
  float steer = 0.0f;
  float radius = 0.3f;
  // The indexed and named WheelRef setters share the same seventeen values.
  // Friction side fields remain distinct from coefficient2, as in Stock.
  Wheel tire;
  float frictionWearRate = 0.0f; // wheel +0x160; wear is tire.frictionScale
  float variableDamage16c = 0.0f;
  float variableDamageRate170 = 0.0f;
  float rollResistance = 0.015f;
  float bearing = 0.0f;
  float springForce = 20000.0f;
  float dampingBump = 2000.0f;
  float dampingRebound = 2000.0f;
  float restLength = 0.3f;
  float minLength = 0.1f;
  float maxLength = 0.5f;
  float brake = 0.0f;
  float handBrake = 0.0f;
  // 004493c0 initializes brake temperature, cooling and wear.
  WheelBrakeThermalState brakeThermal{20.0f, 20.0f, 0.001f, 50000.0f};
  float width = 0.2f;
  std::array<float, 3> contactPatch{};
  std::array<float, 7> arm{};
  std::array<float, 10> hub{};
  int32_t oppositeWheel = -1;
  std::array<float, 6> instantCenter{};
  std::string damage;
};
struct CarFailureSoundEvent {
  enum class Kind { Engine,
                    Brake };
  Kind kind;
  int wheel; // -1 for engine
  std::array<float, 3> position;
  bool soundEnabled; // stock sound object availability; resolve before playback
};

struct CarSeat {
  // Stock seat +0. FUN_00449680 uses this as the driver-camera record index.
  int32_t cameraPreset = 0;
  OwnerLink player, camera, renderInstance, osd;
  // Stock seat +0x70. FUN_00449680 walks 0x40 of these. An empty slot has a
  // null owner, and that is the only case where stock clears the cached id.
  std::array<OwnerLink, 0x40> links{};
  // FUN_00449680/FUN_004518c0: native HUD text nodes belong to this seat.
  std::array<std::shared_ptr<class SceneRenderInstance>, 13> hudText{};
  std::array<std::shared_ptr<class SceneRenderInstance>, 6> hudModels{};
  ~CarSeat();
};

// FUN_0048a3b0 / FUN_0043c0a0: seat-local stock model and mode-4 lifecycle.
// Model indices are cluster, needle, ABS, ASR, handbrake and engine warning.
bool carHudModel(ResourceWorld& resources, GameWorld& world, CarSeat& seat,
                 size_t index, uint32_t material, const std::array<float, 3>& position,
                 float roll, bool visible, std::string& error);

// The look, move, and zoom FUN_0048ce30 queues on the seat camera.
struct SeatCameraPose {
  uint32_t target = 0;
  std::array<float, 3> lookOffset{};
  float lookValue = -1.0f;
  std::array<float, 3> moveOffset{};
  float moveDistance = 0;
  std::array<float, 3> moveRotation{};
  bool hasZoom = false;
  float zoom = 0;
};

struct CarControlSnapshot {
  // 0044e690 reads these floating axes before the integer axes.
  std::array<float, 4> continuous{}; // axes 1, 0x1c, 0x1d, 0x30
  std::array<int32_t, 7>
      digital{}; // axes 0x31, 0x1f, 0x32, 0x36, 7, 0x27, 0x28
};
// Inputs not produced by the player device, read from the live physics step.
struct CarControlStepContext {
  float time = 0, step = 0;
  float drivenWheelSpeed = 0;      // stock car +0x1e14, radians/second
  float displayedEngineSpeed = 0;  // +0x1e1c, radians/second
  bool automatic = false;          // stock flag 0x10
  bool semiautomatic = false;      // stock flag 0x20
  bool automaticClutch = false;    // stock flag 0x40, transmission bit 1 clear
  bool engineRunning = false;      // stock flag 0x4000
  bool automaticInhibited = false; // stock flags 0x40004
  bool positiveMass = true;
};

// Persistent shift state used by 0044e690, 0044d040 and 0044d330.
struct CarShiftState {
  bool buttonHeld = false;
  bool reversing = false;
  float clutchUntil = -1, upUntil = -1, downUntil = -1;
};

// Values supplied by the live stock car-physics state.
struct CarDrivetrainStepContext {
  float step = 0, rpmLimit = 0, maximumRpm = 0, powerScale = 0;
  float engineInertiaFactor = 0, brakeFactor = 0;
  float starterForce = 50; // stock prototype +0xcc; script may override it
  bool engineRunning = false, sleeping = false, damageEnabled = true;
  bool intermittentCut = false, nitrous = false;
};

std::optional<CarControlSnapshot> sampleCarControls(GameWorld& world,
                                                    const CarSeat& seat);

struct CarRuntimeInstance {
  bool isCar = false;
  OwnerLink prototypeSource;
  OwnerLink carLink;
  OwnerLink physicsRoot;
  OwnerLink installParentLink;
  // One stable seat per stock driver/controller record.
  std::vector<std::unique_ptr<CarSeat>> seats;
  // FUN_00448e60 writes these on the car. The mode is +0x1f30. The seat
  // index (+0x1f34) and marker (+0x1f38) are written only when the mode is 1.
  int32_t seatCameraMode = 0;
  int32_t seatCameraSeat = 0;
  int32_t seatCameraMarker = 0;
  // Present after FUN_0048ce30 stores the pose. Empty until then.
  std::optional<SeatCameraPose> seatCameraPose;
  // FUN_00449680 writes 0 here, at car +0x1edc, after the osd branch.
  int32_t activateInfo = 0;
  // Stock car +0x13cc. FUN_0048ce30 writes this id into the look and move
  // commands.
  uint32_t cameraPoseTarget = 0;
  CarSeat* findSeat(uint32_t playerId) const;
  CarSeat* selectSeat(uint32_t playerId) const;
  const VmObject* chassisObject = nullptr;
  int32_t stateMode = 3;
  int32_t wheelId = -1;
  uint32_t partFlags = 0;
  uint32_t usedSlots = 0;
  int32_t colorIndex = 0;
  // Stock FUN_00475ec0 colorscheme iterates EVERY body record: when a record
  // has colours, FUN_0048c8c0 stores colours[scheme % count] on that record's
  // render resource (+0xcc). Keyed here by render resource id, so each drawn
  // part uses its own body's tint.
  std::map<uint32_t, uint32_t> bodyTints;
  uint32_t renderType = 0;
  uint32_t textureId = 0;
  uint32_t defaultTextureId = 0; // cfg texture replaced by Part.setTexture (FUN_00467ff0)
  uint32_t meshId = 0;
  float wear = 1.0f;
  float damageMultiplier = 1.0f;
  float tear = 1.0f;
  float maxWear = 1.0f;
  float dynamicPrice = 0.0f;
  float totalMass = 0.0f;
  float driveTorque = 0.0f;
  float sfxLoopParamA = 0.0f;
  float sfxLoopParamB = 0.0f;
  std::array<float, 3> cooling{};
  float hornPitch = 1.0f;
  int32_t hornIndex = 0;
  float nitroPitch = 1.0f;
  float sfxExhaustMinVol = 0.0f;
  float steerWheelRadius = 0.2f;
  float steerWheelLock = 1.0f;
  float ackermann = 1.0f;
  float crimeLevel = 0.0f;
  bool cruiseControl = false;
  std::array<float, 3> position{0.0f, 0.0f, 0.0f};
  std::array<float, 3> ypr{0.0f, 0.0f, 0.0f};
  std::array<float, 9> rotation{1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                0.0f, 0.0f, 0.0f, 1.0f};
  std::array<float, 3> linearVelocity{0.0f, 0.0f, 0.0f};
  std::array<float, 3> angularVelocity{0.0f, 0.0f, 0.0f};
  std::vector<std::unique_ptr<RuntimeSlotState>> slots;
  std::vector<std::unique_ptr<OwnerLink>> attachedParts;
  std::shared_ptr<PartPhysics> partPhysics;
  std::shared_ptr<CarPhysics> carPhysics;
  int32_t suspendUpdate = 0;
  // Native updatevariables command sets +0x70 bit0x800000; consumed at frame start.
  bool variablesDirty = false;
  // 00448430/newer004aebf0 makes the shared prototype configuration private
  // before the first variable refresh. Installed parts tune this Car only.
  std::optional<CarConfig> variableConfig;
  // Non-owning native cfg+c30 binding. DynoData's VM resource owns the model.
  std::weak_ptr<DynoModel> engineDyno;
  OwnerLink installedEngine;
  const CarConfig& effectiveConfig(const CarConfig& prototype) const {
    return variableConfig ? *variableConfig : prototype;
  }
  float mileage = 0.0f;
  float smoothedSpeed = 0.0f; // stock car +0x1f74
  std::array<float, 2> torqueScale{1.0f, 1.0f};
  int32_t hornOn = 0;
  int32_t transmissionMode = 0;
  float steerHelp = 0.0f;
  float asr = 0.0f;
  float abs = 0.0f; // reciprocal slip thresholds, prototype +0xa28/+0xa2c
  float previousAbsSlip = 0, previousAsrSlip = 0;
  bool absActive = false, asrActive = false; // stock flags 0x01000000/0x02000000
  float diffLock = 0.0f;
  float steeringState = 0.0f;
  float brakeState = 0.0f;
  float handbrakeState = 0.0f;
  float throttleState = 0.0f;
  float clutchState = 0.0f;
  int32_t currentGear = 0;
  float currentGearRatio = 0;          // car +0x1dd8, updated by 00454500
  float previousAutomaticClutch = 0;   // stock +0x1f44; reset by transmission command
  float previousClutchEngineSpeed = 0; // stock +0x1f48
  float clutchRpm = 5000;              // 00446180 prototype +0x10
  CarShiftState shiftState;
  float engineAngularSpeed = 0;   // stock +0x1e18
  float displayedEngineSpeed = 0; // stock +0x1e1c
  float drivenWheelSpeed = 0;     // stock +0x1e14
  float previousEngineRpm = 0;    // stock +0x1f4c
  float couplingForce = 0;        // stock +0x1e24
  // 00446180 prototype defaults at +0x14/+0x18. Script engine updates
  // can replace these independently of the torque curve.
  float maximumRpm = 7000.0f, rpmLimit = 10000.0f;
  // 00454500 reads these globals; both initial image values are 4.0 and
  // stock config options may override them.
  float engineInertiaFactor = 4.0f, wheelBrakeFactor = 4.0f;
  float starterForce = 50.0f;
  float engineWear = 0, engineWearRate = 5e-8f;
  float clutchWear = 0, clutchWearRate = 5e-8f;
  float engineCoolingWear = 0, engineCoolingWearRate = 5e-6f;
  float transmissionWear = 0, transmissionWearRate = 5e-8f;
  float engineHeatCapacity = 320000, engineTemperature = 20;
  float ambientEngineTemperature = 20, engineCooling = 9.375e-5f;
  std::array<float, 3> engineCoolingParameters{10, 50, 0.01f};
  float internalDamageFactor = 1.0f;
  float engineFrictionMultiplier = 1.0f;
  bool engineFailureSoundEnabled = false, brakeFailureSoundEnabled = false;
  std::vector<CarFailureSoundEvent> failureSounds;
  bool engineRunning = false;      // car +0x70 bit 0x4000
  bool damageEnabled = true;       // inverse of car +0x70 bit 0x400
  bool drivetrainSleeping = false; // car +0x70 bit 1
  bool forcedBrake = false;        // car +0x70 bit 0x40000
  CarDrivetrainStepContext drivetrainContext(const CarConfig& config,
                                             float step) const;
  // Advances player controls and transmission transactionally. Returns false
  // without modifying this car when an unimplemented stock branch is needed.
  bool advanceControls(const CarControlSnapshot& input, const CarConfig& config,
                       const CarControlStepContext& context,
                       std::string& error);
  // Applies the supported 00454500 engine, clutch and brake branches to the
  // actual persistent wheel objects. Failure leaves the car unchanged.
  bool advanceDrivetrain(const CarConfig& config,
                         const CarDrivetrainStepContext& context,
                         std::string& error);
  // 004493c0 copies wheel prototype controls into the live car instance.
  void initializeWheelDrivetrain();
  float engineRpm = 0.0f;
  float nitrous = 0.0f; // stock car +0x1ef8: remaining tank contents
  int32_t coolingFan = 0;
  int32_t headlightsOn = 0;
  int32_t flareState = 0;
  CarSfxTable sfxEngineUp, sfxEngineDown, sfxExhaust;
  std::array<const VmObject*, 16> wheelRefs{};
  std::array<CarWheel, 16> wheelStates{};
  std::array<CarWheelExtraState, 16> wheelExtras{};
  std::array<float, 16> wheelSteeringAngles{};
  ~CarRuntimeInstance();
};

// 0044f4e0 angle calculation only. A present wheel runtime represents the
// stock suspension object, independent of whether a tire is fitted. Missing
// suspension objects preserve the previous angle. No bodies are rotated or
// woken by this isolated calculation.
std::optional<std::array<float, 16>>
calculateCarSteeringAngles(const CarRuntimeInstance& car,
                           const CarConfig& chassis);

// Keeps successfully linked requests in result even when a later request is
// unsupported. A false return never represents a usable complete car.
bool loadCarPrototype(ResourceWorld& world, ScriptNode& source,
                      const ResourceIdMapping& mapping, std::string_view text,
                      std::unique_ptr<CarPrototype>& result, std::string& error,
                      bool isCar = true);

// Reads the loaded chassis tank_nitro and the installed slot-401 engine's
// dynodata. No installed system is distinct from an installed empty system.
CarHudOptionalValues carHudOptionalValues(GameWorld& world,
                                          const CarRuntimeInstance& car);

// The pre-script block of 00448430/newer004aebf0. Call only after the
// suspend_update guard, before the script's updatevariables callback.
bool resetCarWheelVariables(CarRuntimeInstance& car, const CarConfig& config,
                             std::string& error);
