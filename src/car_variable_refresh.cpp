#include "car_variable_refresh.hpp"

#include "car_runtime.hpp"
#include "dyno_model.hpp"
#include "natives/game_type.hpp"
#include "resource_world.hpp"
#include "run_log.hpp"

#include <algorithm>
#include <cmath>

namespace {
const VmValue* field(const VmObject& object, std::string_view name) {
  for (const auto& item : object.fields)
    if (item.name == name)
      return &item.value;
  return nullptr;
}

bool number(const VmObject& object, const char* name, float& value,
            std::string& error) {
  const auto* item = field(object, name);
  if (item) {
    if (const auto* floating = std::get_if<float>(item)) {
      value = *floating;
      return true;
    }
    if (const auto* integer = std::get_if<int32_t>(item)) {
      value = static_cast<float>(*integer);
      return true;
    }
  }
  error = std::string("car variable refresh: missing or invalid ") + name;
  return false;
}

bool integer(const VmObject& object, const char* name, int32_t& value,
             std::string& error) {
  const auto* item = field(object, name);
  if (const auto* result = item ? std::get_if<int32_t>(item) : nullptr) {
    value = *result;
    return true;
  }
  error = std::string("car variable refresh: missing or invalid ") + name;
  return false;
}

const VmObject* objectField(const VmObject& receiver, const char* name) {
  const auto* value = field(receiver, name);
  const auto* reference = value ? std::get_if<const VmObject*>(value) : nullptr;
  return reference ? *reference : nullptr;
}

bool transferVariables(CarRuntimeInstance& car, const VmObject& receiver,
                       VmArena& arena, std::string& error) {
  auto& config = *car.variableConfig;
  float mass = 0;
  if (!number(receiver, "engine_inertia", config.enginePower[1], error) ||
      !number(receiver, "starter_torque", car.starterForce, error) ||
      !number(receiver, "engine_friction_fwd", config.enginePower[2], error) ||
      !number(receiver, "engine_friction_rev", config.enginePower[3], error) ||
      !number(receiver, "engine_rpm_idle", config.rpmIdle, error) ||
      !number(receiver, "engine_mass", mass, error))
    return false;
  // 004aebf0 sets both curve multipliers to one and preserves thermal energy
  // while replacing the engine's heat capacity. Zero mass is unsupported here:
  // the original divides by zero, so it must not silently become a default.
  if (!std::isfinite(mass) || mass <= 0) {
    error = "car variable refresh: nonpositive or nonfinite engine mass";
    return false;
  }
  config.enginePower[0] = config.enginePowerCopy = 1;
  const float previousCapacity = car.engineHeatCapacity;
  car.engineHeatCapacity = mass * 8000;
  car.engineTemperature = previousCapacity * car.engineTemperature / car.engineHeatCapacity;
  const float scale = car.engineWear >= 1 ? 0.100000024f
                                          : 1 - car.engineWear * car.engineWear * .6f;
  car.torqueScale = {scale, scale};

  // 004aebf0 consumes the model installed by 004942b0, never a class-name
  // search or a synthesized peak curve. A missing primary becomes one zero
  // point. A missing secondary copies the primary curve verbatim.
  const auto model = car.engineDyno.lock();
  const auto* dyno = model ? model->result() : nullptr;
  car.clutchRpm = dyno ? dyno->maximumTorqueRpm : 0;
  if (!dyno || dyno->primary.torque.empty()) {
    config.torqueCurve = {{0, 0}};
    config.secondaryTorqueCurve = {{0, 0}};
  } else {
    if (!dyno->nitro.torque.empty() && dyno->nitro.torque.size() != dyno->primary.torque.size()) {
      error = "car variable refresh: native torque table sizes differ";
      return false;
    }
    config.torqueCurve.resize(dyno->primary.torque.size());
    config.secondaryTorqueCurve.resize(dyno->primary.torque.size());
    for (size_t i = 0; i < dyno->primary.torque.size(); ++i) {
      const float rpm = static_cast<float>(i) * static_cast<float>(dyno->stepRpm);
      config.torqueCurve[i] = {rpm, dyno->primary.torque[i]};
      config.secondaryTorqueCurve[i] = {rpm, dyno->nitro.torque.empty() ? dyno->primary.torque[i] : dyno->nitro.torque[i]};
    }
  }
  config.torqueSet = true;

  const auto* ratios = objectField(receiver, "ratio");
  if (!ratios) {
    error = "car variable refresh: ratio array unavailable";
    return false;
  }
  // Newer config+24 is neutral; +28..40 are the seven actual gear ratios.
  // A short script array retains the untouched tail of the private copy.
  for (size_t i = 0; i < std::min<size_t>(8, ratios->arrayElements.size()); ++i) {
    const auto* value = std::get_if<float>(&ratios->arrayElements[i]);
    if (!value) {
      error = "car variable refresh: ratio array is not float";
      return false;
    }
    (i ? config.gearRatios[i - 1] : config.neutralRatio) = *value;
  }
  if (!integer(receiver, "gears", config.gearCount, error) ||
      !number(receiver, "rearend_ratio", config.rearEndRatio, error) ||
      !number(receiver, "maxRPM", car.maximumRpm, error) ||
      !number(receiver, "RPM_limit", car.rpmLimit, error) ||
      !number(receiver, "ClutchF", config.clutch, error))
    return false;

  const char* soundNames[] = {"SFX_trans_fwd", "SFX_trans_rev", "SFX_ignition"};
  const char* pitchNames[] = {"rpm_trans_fwd", "rpm_trans_rev", "sfx_starter_rpm"};
  std::optional<std::pair<uint32_t, float>>* sounds[] = {
      &config.gearboxForward, &config.gearboxReverse, &config.ignitionSound};
  for (size_t i = 0; i < 3; ++i) {
    int32_t id = 0;
    float pitch = 0;
    if (!integer(receiver, soundNames[i], id, error) ||
        !number(receiver, pitchNames[i], pitch, error))
      return false;
    if (id && (!arena.gameState || !arena.gameState->resources)) {
      error = "car variable refresh: sound resource world unavailable";
      return false;
    }
    // 004aebf0 resolves type-six resources, including null/missing ids.
    const auto found = id ? arena.gameState->resources->lookup(static_cast<uint32_t>(id), 6)
                          : LookupResult{};
    if (found.status == LookupStatus::MountRefused || found.status == LookupStatus::StockCrash) {
      error = "car variable refresh: sound resource: " + found.detail;
      return false;
    }
    *sounds[i] = found.node ? std::make_optional(std::pair{found.node.id, pitch}) : std::nullopt;
  }
  if (!number(receiver, "tank_nitro", config.nitrousCapacity, error) ||
      !number(receiver, "consumption_nitro", config.nitrousConsumption, error))
    return false;
  if (config.nitrousCapacity <= car.nitrous)
    car.nitrous = config.nitrousCapacity;
  const auto* center = objectField(receiver, "drag_center");
  if (!center) {
    error = "car variable refresh: drag_center unavailable";
    return false;
  }
  if (!number(*center, "x", config.drag[1], error) ||
      !number(*center, "y", config.drag[2], error) ||
      !number(*center, "z", config.drag[3], error) ||
      !number(receiver, "C_drag", config.drag[0], error))
    return false;
  return true;
}
} // namespace

bool refreshCarVariables(WorldNode& node, VmArena& arena,
                         const VmScriptCall& callScript, std::string& error) {
  if (!node.carInstance || !arena.gameState || !arena.gameState->world) {
    error = "car variable refresh: live Car/world unavailable";
    return false;
  }
  auto& car = *node.carInstance;
  car.variablesDirty = false;
  auto& world = *arena.gameState->world;
  const uint32_t id = car.prototypeSource.cachedValue ? car.prototypeSource.cachedValue
                                                      : node.scriptLink.cachedValue;
  const auto* source = world.findScript(id);
  if (!source || !source->car) {
    error = "car variable refresh: prototype unavailable";
    return false;
  }
  if (!car.variableConfig)
    car.variableConfig = source->car->config;
  const auto* receiver = car.chassisObject ? car.chassisObject : node.scriptReceiver;
  if (!receiver)
    return true;
  int32_t suspended = 0;
  if (!integer(*receiver, "suspend_update", suspended, error))
    return false;
  runlog::event(runlog::EventKind::Note,
                "car variables consume: car=0x%08x suspend_update=%d", node.instanceId, suspended);
  if (suspended)
    return true;
  if (!resetCarWheelVariables(car, source->car->config, error))
    return false;
  if (!callScript) {
    error = "car variable refresh: script caller unavailable";
    return false;
  }
  const auto result = callScript(receiver, "updatevariables", {});
  if (!result.ok) {
    error = "car variable refresh: " + result.error;
    return false;
  }
  if (!transferVariables(car, *receiver, arena, error))
    return false;
  runlog::event(runlog::EventKind::Note,
                "car variables transferred: car=0x%08x inertia=%g starter=%g gears=%d first=%g rearend=%g maxRPM=%g limit=%g",
                node.instanceId, car.variableConfig->enginePower[1], car.starterForce,
                car.variableConfig->gearCount, car.variableConfig->gearRatios[0],
                car.variableConfig->rearEndRatio, car.maximumRpm, car.rpmLimit);
  static bool remainingNoted = false;
  if (!remainingNoted) {
    remainingNoted = true;
    runlog::event(runlog::EventKind::Note,
                  "car variable refresh: 004aebf0 installed-part contributions, wheel owner links and 00498020 post-refresh remain gaps");
  }
  return true;
}

bool bindAddedCarEngine(WorldNode& carNode, WorldNode& added, VmArena& arena,
                        std::string& error) {
  if (carNode.nativeDevice != "car")
    return true;
  if (!carNode.carInstance || !arena.gameState || !arena.gameState->world) {
    error = "Car addpart Dyno binding: live world unavailable";
    return false;
  }
  auto& car = *carNode.carInstance;
  auto& world = *arena.gameState->world;
  WorldNode* engine = nullptr;
  for (const auto& slot : car.slots)
    if (slot && slot->id == 0x191) {
      engine = world.nodeFor(slot->attachedNode.owner);
      break;
    }
  const VmObject* receiver = carNode.scriptReceiver;
  if (engine) {
    if (engine->instanceId != added.instanceId)
      return true;
    setLinkOwner(car.installedEngine, engine);
    receiver = engine->scriptReceiver;
  }
  const auto* dyno = receiver ? objectField(*receiver, "dynodata") : nullptr;
  if (!dyno)
    return true; // Original leaves the preceding native pointer untouched.
  auto* resource = nativeResourceState(dyno, &arena);
  car.engineDyno = resource ? resource->dyno : std::weak_ptr<DynoModel>{};
  runlog::event(runlog::EventKind::Note,
                "Car addpart Dyno binding: car=0x%08x engine=0x%08x native=%d",
                carNode.instanceId, engine ? engine->instanceId : 0,
                !car.engineDyno.expired());
  return true;
}
