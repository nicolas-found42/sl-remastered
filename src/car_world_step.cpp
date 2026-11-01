#include "car_world_step.hpp"
#include "vm.hpp"
#include "car_physics.hpp"
#include "car_runtime.hpp"
#include "car_hud.hpp"
#include "scene_2d.hpp"
#include "car_wheel_runtime.hpp"
#include "physics_contact_geometry.hpp"
#include "physics_core_pair.hpp"
#include "physics_polyhedron.hpp"
#include "resource_world.hpp"
#include "run_log.hpp"
#include "wheel_tire_step.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace {
// Fixed buffer for one trace line; additions past the end are dropped.
struct TraceLine {
  char text[runlog::kTraceLineSize];
  size_t length = 0;
  TraceLine() { text[0] = 0; }
#if defined(__GNUC__)
  __attribute__((format(printf, 2, 3)))
#endif
  void add(const char* format, ...) {
    if (length + 1 >= sizeof text)
      return;
    va_list args;
    va_start(args, format);
    const int written =
        std::vsnprintf(text + length, sizeof text - length, format, args);
    va_end(args);
    if (written > 0)
      length += std::min<size_t>(static_cast<size_t>(written),
                                 sizeof text - length - 1);
  }
};
} // namespace

bool CarWorldStepper::considerDynamicPair(
    physics::Body* objectBody, physics::Body* other,
    physics::ContactGeometryWorld& geometryWorld,
    std::vector<physics::DirectContact>& contacts,
    std::set<DynamicPairKey>& seen, std::set<DynamicPairKey>& pendingErase,
    std::string& why) {
  using namespace physics;
  if (!objectBody || !other || objectBody == other || !objectBody->proto ||
      !other->proto)
    return true;
  // FUN_0049c8f0: wheel and swept shapes use real core queries below.
  // Segment-only/no-core special branches remain outside this dispatcher.
  if ((objectBody->flags | other->flags) &
      (0x20u | 0x400u | 0x10000u | 0x20000u)) {
    if (!notedDynamicSkip_) {
      notedDynamicSkip_ = true;
      runlog::event(runlog::EventKind::Note,
                    "object step: segment-only/no-core special body pairs "
                    "(flags 0x20/0x400/0x10000/0x20000) remain unsupported");
    }
    return true;
  }
  const auto& shapeA = objectBody->proto->shape;
  const auto& shapeB = other->proto->shape;
  if (objectBody->current > 1 || other->current > 1 ||
      !std::isfinite(shapeA.boundRadius) || shapeA.boundRadius < 0 ||
      !std::isfinite(shapeB.boundRadius) || shapeB.boundRadius < 0) {
    why = "car step: dynamic pair bounds unsupported";
    return false;
  }
  // 0049c8f0 bound rejection on the predicted states.
  const auto& sa = objectBody->state[objectBody->current ^ 1];
  const auto& sb = other->state[other->current ^ 1];
  const Vec3 delta{sa.pos[0] - sb.pos[0], sa.pos[1] - sb.pos[1],
                   sa.pos[2] - sb.pos[2]};
  const auto sweptLength = [](const Body& body) {
    if (!(body.flags & 0x800u))
      return 0.0f;
    const auto& a = body.state[body.current].pos;
    const auto& b = body.state[body.current ^ 1].pos;
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
  };
  const float radius = shapeA.boundRadius + shapeB.boundRadius + k::skin +
                       sweptLength(*objectBody) + sweptLength(*other);
  const float distance2 =
      delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
  if (radius * radius < distance2)
    return true;
  // Non-sphere shapes need their collision core (built before traversal).
  if ((shapeA.type != ShapeType::Sphere && !shapeA.core) ||
      (shapeB.type != ShapeType::Sphere && !shapeB.core))
    return true;
  const bool ordered = objectBody < other;
  Body* first = ordered ? objectBody : other;
  Body* second = ordered ? other : objectBody;
  const DynamicPairKey key{first, second};
  seen.insert(key);
  auto entry = dynamicPairs_.find(key);
  if (entry == dynamicPairs_.end()) {
    auto retained = std::make_unique<RetainedDynamicPair>();
    if (!createDynamicBodyPair(*first, *second, retained->pair, why)) {
      why = "car step: dynamic pair unsupported: " + why;
      return false;
    }
    retained->firstPrototype = first->proto;
    retained->secondPrototype = second->proto;
    entry = dynamicPairs_.emplace(key, std::move(retained)).first;
  }
  RetainedDynamicPair& retained = *entry->second;
  if (retained.pair.contact.a->proto != retained.firstPrototype ||
      retained.pair.contact.b->proto != retained.secondPrototype) {
    pendingErase.insert(key);
    seen.erase(key);
    return true;
  }
  Vec3 point{};
  const auto result =
      updateDynamicBodyPair(retained.pair, geometryWorld, point);
  if (result == ContactGeometryResult::Unsupported) {
    why = "car step: dynamic pair update unsupported";
    return false;
  }
  if (result == ContactGeometryResult::Dropped) {
    pendingErase.insert(key);
    seen.erase(key);
    return true;
  }
  // Offer retained 0049b830 geometry while it updates: the pending impact
  // pass and resting normal-row solve own correction state, not traversal.
  contacts.push_back(retained.pair.contact);
  lastOffers_.push_back(OfferedContact{nullptr, nullptr, &retained});
  return true;
}

bool CarWorldStepper::step(float seconds, std::string& error) {
  const VmMeasurementState stockMeasurement{};
  const auto& measurement = measurement_ ? *measurement_ : stockMeasurement;
  // Object instances come and go with node load/destroy; retained per-body
  // contact state must not outlive the bodies it names.
  if (world_.objectPhysicsGeneration() != lastObjectGeneration_) {
    lastObjectGeneration_ = world_.objectPhysicsGeneration();
    objectPairs_.clear();
    dynamicPairs_.clear();
  }
  // Stock camera input/zoom callback 004386a0 precedes the force callback
  // 00438b60 and physics prediction, including cameras without follow targets.
  world_.advanceCameraZoom(seconds);
  const auto cars = world_.carNodes();
  if (cars.empty()) {
    if (world_.dynamicBodyCount() == 0)
      return world_.stepEmptyPhysics(seconds, error);
    return world_.stepArmedCameraFollow(seconds, error);
  }
  // Every registered car is stepped in the same world step, as stock does
  // (004a5190 walks all registered roots). Stock collides through the sector
  // the body is queued in (004b3c40 / 004b4250). The node the car's sector
  // link names is the game root, not a city, so each car's static ground is
  // the union of the loaded city sectors whose bounds contain it.
  struct StepCar {
    CarRuntimeInstance* instance;
    std::shared_ptr<CarPhysics> car;
    decltype(resources_.groundSectorsAt({}, 0)) sectors;
    CarSeat* seat = nullptr;
    const CarPrototype* prototype = nullptr;
    float smoothedSpeed = 0;
  };
  std::vector<StepCar> stepCars;
  size_t primaryIndex = 0;
  for (auto* carNode : cars) {
    StepCar entry;
    entry.instance = carNode->carInstance.get();
    entry.car = entry.instance->carPhysics;
    entry.smoothedSpeed = entry.instance->smoothedSpeed;
    const auto& state = entry.car->root->state[entry.car->root->current];
    // FUN_00454500 +0x1f74 is a per-frame 0.8/0.2 speed filter.
    // +0x20fc integrates that speed in world-distance units, not HUD units.
    if (seconds >= 0.0001f) {
      const auto& v = state.vel;
      entry.smoothedSpeed = entry.smoothedSpeed * 0.8f +
                            std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) * 0.2f;
    }
    entry.sectors = resources_.groundSectorsAt(
        {state.pos[0], state.pos[1], state.pos[2]}, 6.0f);
    // FUN_004a3f90/004a3bc0 do not integrate a sleeping root. A newly
    // constructed Car sleeps while Scripts assemble and place it; it need
    // not stand in a loaded City sector yet. Awake roots still require one.
    if (entry.sectors.empty() &&
        !(entry.car->root->flags & physics::kBodyAsleep)) {
      error = "car step: no loaded ground sector contains the car at (" +
              std::to_string(state.pos[0]) + ", " + std::to_string(state.pos[1]) +
              ", " + std::to_string(state.pos[2]) + ")";
      return false;
    }
    const auto source = resources_.resolve(entry.instance->prototypeSource.owner);
    entry.prototype = source.script ? source.script->car.get() : nullptr;
    for (const auto& candidate : entry.instance->seats)
      if (candidate && candidate->player.owner)
        entry.seat = candidate.get();
    if (entry.seat)
      primaryIndex = stepCars.size();
    // FUN_004518c0 formats a single stock gear-table character. The cars.rpk
    // font owns the ordinal glyph artwork; never synthesize suffix strings.
    if (entry.seat && entry.seat->hudText[0]) {
      auto text = *entry.seat->hudText[0]->textState();
      text.text = carHudGear(entry.instance->currentGear);
      entry.seat->hudText[0]->setTextState(std::move(text));
      // 0x452560 loads car +0x13bc, NOT the wheel-record array (+0x13e4).
      // Body +0x20c selects a 0xd4-byte state: +0x64 +0x0c is State.vel.
      const auto& velocity = entry.car->root->state[entry.car->root->current].vel;
      const float speed = std::sqrt(velocity[0] * velocity[0] +
                                    velocity[1] * velocity[1] +
                                    velocity[2] * velocity[2]);
      if (entry.seat->hudText[1]) {
        auto speedText = *entry.seat->hudText[1]->textState();
        speedText.text = carHudValue(carHudSpeed(), speed / measurement.product);
        entry.seat->hudText[1]->setTextState(std::move(speedText));
      }
      if (entry.seat->hudText[2]) {
        auto scoreText = *entry.seat->hudText[2]->textState();
        // all.c:47614 initializes 0063c73c = 10 / distance-unit length.
        scoreText.text = carHudValue(carHudScore(),
                                     entry.instance->mileage * measurement.quotient);
        entry.seat->hudText[2]->setTextState(std::move(scoreText));
      }
    }
    if (entry.seat && entry.seat->hudText[0]) {
      auto& runtime = *entry.instance;
      auto& seat = *entry.seat;
      const float rpm = std::fabs(runtime.displayedEngineSpeed * 9.549296f);
      const uint32_t pack = seat.hudText[0]->typeId() & 0xffff0000u;
      if (!carHudModel(resources_, world_, seat, 1, pack | 0x56,
                       {1.43f, -4.36f, -3.58f}, carHudNeedle(rpm), true, error))
        return false;
      const auto optional = carHudOptionalValues(world_, runtime);
      const std::array<std::optional<float>, 2> values{optional.nitro, optional.turbo};
      for (size_t i = 0; i < values.size(); ++i)
        if (values[i] && seat.hudText[11 + i]) {
          auto text = *seat.hudText[11 + i]->textState();
          const CarHudElement bar{0x5c, 2, -0.17f, i ? 0.91f : 0.8f,
                                  0xff000000, "%01.0f", 0, 8, 0, 8};
          text.text = carHudValue(bar, *values[i]);
          seat.hudText[11 + i]->setTextState(std::move(text));
        }
      const std::array<bool, 4> lamps{
          runtime.absActive, runtime.asrActive, carHudLamp(runtime.handbrakeState),
          carHudWarning(runtime.engineTemperature, rpm, runtime.maximumRpm, runtime.rpmLimit)};
      for (size_t i = 0; i < lamps.size(); ++i)
        if (!carHudModel(resources_, world_, seat, i + 2, pack | (0x61u + uint32_t(i)),
                         {3.24f, -2.4f, -3.58f}, 0, lamps[i], error))
          return false;
    }
    stepCars.push_back(std::move(entry));
  }
  // Diagnostics and the cartrace line describe the seated car (else the first).
  CarRuntimeInstance& instance = *stepCars[primaryIndex].instance;
  const std::shared_ptr<CarPhysics> car = stepCars[primaryIndex].car;
  const auto sectors = stepCars[primaryIndex].sectors;

  {
    static bool sectorsLogged = false;
    if (!sectorsLogged) {
      sectorsLogged = true;
      for (auto* sector : sectors)
        if (sector->bounds)
          runlog::event(
              runlog::EventKind::Note,
              "car ground sector bounds mode=%d center=%.2f,%.2f,%.2f "
              "half=%.2f,%.2f,%.2f",
              int(sector->bounds->mode), sector->bounds->values[0],
              sector->bounds->values[1], sector->bounds->values[2],
              sector->bounds->values[3], sector->bounds->values[4],
              sector->bounds->values[5]);
    }
  }
  // 004a5e80 builds each shape's collision core on first use. The car's
  // prototypes (including installed parts') are owned by its compound.
  for (const auto& stepCar : stepCars)
    for (const auto& entry : stepCar.car->bodyChildren) {
      const auto* body = entry.first;
      if (!body || !body->proto || body->proto->shape.core)
        continue;
      auto* proto = const_cast<physics::BodyProto*>(body->proto);
      if (!physics::buildShapeCore(*proto, proto->shape.core, error)) {
        error = "car step: collision core: " + error;
        return false;
      }
    }

  static bool wheelsLogged = false;
  if (!wheelsLogged) {
    wheelsLogged = true;
    for (size_t i = 0; i < car->wheels.size(); ++i) {
      const auto& wheel = car->wheels[i];
      if (!wheel)
        continue;
      const auto& extra = instance.wheelExtras[i];
      runlog::event(
          runlog::EventKind::Note,
          "car wheel %zu: hasWheel=%d susp(force=%g damp=%g rebound=%g rest=%g "
          "min=%g max=%g) extra(force=%g bump=%g rebound=%g rest=%g min=%g "
          "max=%g drive=%g radius=%g friction=%g stiff=%g maxLoad=%g) "
          "runtime radius=%g",
          i, int(wheel->config.hasWheel), wheel->suspension.settings.force,
          wheel->suspension.settings.damping,
          wheel->suspension.settings.reboundDamping,
          wheel->suspension.settings.restLength,
          wheel->suspension.settings.minLength,
          wheel->suspension.settings.maxLength, extra.springForce,
          extra.dampingBump, extra.dampingRebound, extra.restLength,
          extra.minLength, extra.maxLength, extra.drive, extra.radius,
          extra.friction, extra.stiffness, extra.maxLoad, wheel->radius);
    }
    for (size_t i = 0; i < car->wheels.size(); ++i) {
      const auto& extra = instance.wheelExtras[i];
      if (car->wheels[i])
        runlog::event(runlog::EventKind::Note,
                      "car wheel %zu extras: drive=%g radius=%g/%g friction=%g "
                      "steer=%g brake=%g handbrake=%g inertia=%g",
                      i, extra.drive, extra.radius, car->wheels[i]->radius,
                      extra.friction, extra.steer, extra.brake, extra.handBrake,
                      car->wheels[i]->effectiveInverseInertia);
    }
    runlog::event(runlog::EventKind::Note,
                  "car drivetrain: engineRunning=%d gear=%d maxRpm=%g "
                  "rpmLimit=%g seats=%zu mass=%g",
                  int(instance.engineRunning), instance.currentGear,
                  instance.maximumRpm, instance.rpmLimit, instance.seats.size(),
                  car->mass);
  }
  {
    static double lastNote = -1e9;
    const double now = runlog::monotonicSeconds();
    if (now - lastNote >= 0.25) {
      lastNote = now;
      const auto& root = *car->root;
      const auto& velocity = root.state[root.current].vel;
      runlog::event(
          runlog::EventKind::Note,
          "car live: asleep=%d controls=%s throttle=%.2f brake=%.2f "
          "clutch=%.2f "
          "steer=%.2f gear=%d engine=%.0frad/s speed=%.2f pos=%.1f,%.1f,%.1f",
          int((root.flags & physics::kBodyAsleep) != 0),
          lastControls_ ? "sampled" : "none", instance.throttleState,
          instance.brakeState, instance.clutchState, instance.steeringState,
          instance.currentGear, instance.engineAngularSpeed,
          std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1] +
                    velocity[2] * velocity[2]),
          root.state[root.current].pos[0], root.state[root.current].pos[1],
          root.state[root.current].pos[2]);
      const auto wheelSpeed = [&](size_t i) {
        return car->wheels.size() > i && car->wheels[i]
                   ? car->wheels[i]->angularSpeed
                   : 0.0f;
      };
      runlog::event(runlog::EventKind::Note,
                    "car live wheels: witnesses=%zu tire ok/rejected=%llu/%llu "
                    "w0=%.1f w1=%.1f w2=%.1f w3=%.1f rad/s",
                    witnesses_.size(),
                    static_cast<unsigned long long>(tireSteps_),
                    static_cast<unsigned long long>(tireRejects_),
                    wheelSpeed(0), wheelSpeed(1), wheelSpeed(2), wheelSpeed(3));
      {
        // Wheel frames: is the wheel body where the suspension puts it
        // (anchor - length, downward) or elsewhere?
        static double lastFrameLog = -1e9;
        const double nowFrame = runlog::monotonicSeconds();
        if (nowFrame - lastFrameLog >= 2.0) {
          lastFrameLog = nowFrame;
          const auto& rootP = root.state[root.current].pos;
          for (size_t i = 0; i < car->wheels.size(); ++i) {
            const auto& w = car->wheels[i];
            if (!w || !w->suspensionBody)
              continue;
            const auto& c = w->suspension;
            const auto* wb = c.wheelBody;
            const auto rel = (wb && wb->attachment)
                                 ? wb->attachment->relative.position
                                 : physics::Vec3{};
            const auto wP = wb ? wb->state[wb->current].pos : physics::Vec3{};
            const auto sP =
                w->suspensionBody->state[w->suspensionBody->current].pos;
            runlog::event(
                runlog::EventKind::Note,
                "wheel frame %zu: rootY=%.3f wheelY=%.3f relY=%.3f "
                "anchorY=%.3f len=%.3f radius=%.3f suspBodyY=%.3f "
                "wheelBelowRoot=%.3f relPos=%.3f,%.3f,%.3f "
                "rootRotY=(%.3f,%.3f,%.3f) contactBodyY=%.3f",
                i, rootP[1], wP[1], rel[1], c.anchor[1], c.length, w->radius,
                sP[1], rootP[1] - wP[1],
                (wb && wb->attachment) ? wb->attachment->relative.position[0]
                                       : 0.0f,
                (wb && wb->attachment) ? wb->attachment->relative.position[1]
                                       : 0.0f,
                (wb && wb->attachment) ? wb->attachment->relative.position[2]
                                       : 0.0f,
                root.state[root.current].rot[0],
                root.state[root.current].rot[4],
                root.state[root.current].rot[8],
                c.contactBody
                    ? c.contactBody->state[c.contactBody->current].pos[1]
                    : 0.0f);
          }
        }
      }
      for (size_t i = 0; i < car->wheels.size(); ++i) {
        const auto& w = car->wheels[i];
        if (!w)
          continue;
        const auto& c = w->suspension;
        const auto found = witnesses_.find(c.wheelBody);
        runlog::event(
            runlog::EventKind::Note,
            "car live wheel %zu: witness=%d contactBody=%d len=%.3f "
            "force=%.0f drive=%.2f w=%.1f contactY=%.3f wheelY=%.3f "
            "dist=%.3f max=%.3f sep=%d",
            i, int(found != witnesses_.end() && found->second),
            c.contactBody ? 1 : 0, c.length, c.force,
            i < instance.wheelExtras.size() ? instance.wheelExtras[i].drive
                                            : -1.0f,
            w->angularSpeed, c.contactPoint[1],
            c.wheelBody ? c.wheelBody->state[c.wheelBody->current].pos[1]
                        : 0.0f,
            std::sqrt(c.distanceSquared), c.settings.maxLength,
            int(c.settings.maxLength * c.settings.maxLength <=
                c.distanceSquared));
      }
    }
  }
  LiveCarStepHooks hooks;
  hooks.onePendingContactPerRoot = true;
  for (const auto& stepCar : stepCars)
    for (const auto& seat : stepCar.instance->seats)
      if (seat && seat->player.owner) {
        static bool reason = false;
        if (!reason && !sampleCarControls(world_, *seat)) {
          reason = true;
          auto* player = world_.nodeFor(seat->player.owner);
          std::string axisReason = "-";
          if (player && player->playerInput) {
            auto probe = *player->playerInput;
            for (int32_t axis :
                 {1, 0x1c, 0x1d, 0x30, 0x31, 0x1f, 0x32, 0x36, 7, 0x27, 0x28})
              if (!probe.sampleAxis(axis)) {
                axisReason =
                    "axis " + std::to_string(axis) + ": " + probe.lastBlock();
                break;
              }
          }
          runlog::event(
              runlog::EventKind::Note,
              "car controls unavailable: input block \"%s\" playerNode=%d "
              "loaded=%d device=%s playerBlock=%d playerInput=%d "
              "scriptedOverride=%d",
              axisReason.c_str(), player ? 1 : 0,
              player ? int(player->loaded) : 0,
              player ? player->nativeDevice.c_str() : "-",
              player && player->player ? 1 : 0,
              player && player->playerInput ? 1 : 0,
              player && player->player
                  ? int(player->player->scriptedControlOverride)
                  : -1);
        }
        if (const auto controls = sampleCarControls(world_, *seat)) {
          // FUN_0044f4e0's caller (all.c 61330) wakes a sleeping car only
          // when |throttle| > 0.01, |steering target - current| > 0.01 or
          // the gear request changed; a idle controller never wakes it.
          // Raw axes stand in for the processed throttle/steering fields.
          const float steerNow = controls->continuous[0];
          const float steerThen = lastControls_ ? lastControls_->continuous[0] : 0.0f;
          const bool gearChanged =
              lastControls_ && lastControls_->digital != controls->digital;
          if (std::fabs(controls->continuous[1]) > 0.01f ||
              std::fabs(steerNow - steerThen) > 0.01f || gearChanged)
            hooks.wakeCars = true;
          lastControls_ = *controls;
        }
        break;
      }
  std::vector<CarWheelRuntime*> wheels, primaryWheels;
  for (const auto& stepCar : stepCars)
    for (const auto& wheel : stepCar.car->wheels)
      if (wheel && wheel->suspensionBody && wheel->config.hasWheel) {
        wheels.push_back(wheel.get());
        if (stepCar.car == car)
          primaryWheels.push_back(wheel.get());
        hooks.suspensions.push_back(&wheel->suspension);
      }
  // What the hooks saw this step; written as one cartrace line at the end.
  struct StepScratch {
    char tire[4] = {'-', '-', '-',
                    '-'};               // o tire step ok, r rejected, - no witness
    char sel[4] = {'-', '-', '-', '-'}; // S selected, R rejected, - no hit
    float selDiag[4][4] = {};           // length, distance, aux, stored distanceSquared
    float candA[4] = {};                // candidate wheel-local contact point y
    float candB[4] = {};                // candidate world contact point y
    float steer[4] = {};                // applied steering angle per wheel (rad)
    // Worst contact by separation (most negative), for identifying bogus cores.
    float minSeparation = 0;
    physics::Vec3 minSepNormal{};
    char minSepBody[96] = "-";
    char minSepFace[200] = "-";
    physics::Vec3 minSepPointB{};
    int hits[4] = {}; // wheel face hits this step
    size_t pairs = 0; // body/face pairs of the last gather
    std::array<unsigned short, 8> iterContacts{}, iterPlayerContacts{}, iterPlayerCorrections{}, iterWorldCorrections{};
    std::array<float, 8> iterPlayerImpulse{};
    std::array<float, 8> iterMaxImpulse{};
    // Separation range of each pass's contacts, and how many were corrected.
    std::array<float, 8> iterSepMin{}, iterSepMax{};
    std::array<unsigned short, 8> iterCorrected{};
    // Accumulated positional shift (0.021 correction) of the car root after
    // each pass, y component.
    std::array<float, 8> iterRootShiftY{};
    float worstImpulse = 0, worstSeparation = 0;
    physics::Vec3 worstNormal{};
    char worstBody[96] = "-";
    char worstFace[200] =
        "-"; // material word and triangle of the worst contact
    physics::Vec3 worstPartPos{}, worstPointB{}, worstPointA{};
  } scratch;
  // Stock runs the car callback (00454500 and 0044e690) in the world step's
  // callback pass, before prediction: player controls, wheel steering, the
  // drivetrain, then each wheel's tire force from the contact selected during
  // the previous frame's collision traversal.
  hooks.beforePrediction = [&, seconds](std::string& why) {
    for (auto& stepCar : stepCars) {
      if (stepCar.car->root->flags & physics::kBodyAsleep)
        continue;
      CarRuntimeInstance& instance = *stepCar.instance;
      const std::shared_ptr<CarPhysics>& car = stepCar.car;
      const CarPrototype* prototype = stepCar.prototype;
      CarSeat* seat = stepCar.seat;
      if (!prototype) {
        why = "car step: the car prototype configuration is unavailable";
        return false;
      }
      const CarConfig& config = prototype->config;
      if (seat) {
        if (const auto controls = sampleCarControls(world_, *seat)) {
          CarControlStepContext context;
          context.time = world_.physicsTime();
          context.step = seconds;
          context.drivenWheelSpeed = instance.drivenWheelSpeed;
          context.displayedEngineSpeed = instance.displayedEngineSpeed;
          // Stock "transmission N" sets car flags: bit0 enables the automatic
          // gearbox, bit2 selects semiautomatic (0x20) over automatic (0x10).
          context.automatic =
              (instance.transmissionMode & 1) != 0 &&
              (instance.transmissionMode & 4) == 0;
          context.semiautomatic = (instance.transmissionMode & 5) == 5;
          context.engineRunning = instance.engineRunning;
          context.positiveMass = car->mass > 0;
          std::string detail;
          if (!instance.advanceControls(*controls, config, context, detail)) {
            why = "car step: controls: " + detail;
            return false;
          }
        }
      }
      if (const auto angles = calculateCarSteeringAngles(instance, config))
        for (size_t i = 0; i < car->wheels.size(); ++i)
          if (car->wheels[i] && car->wheels[i]->suspensionBody) {
            if (!applyWheelSteeringOrientation(*car->wheels[i], (*angles)[i])) {
              why = "car step: wheel steering orientation unsupported";
              return false;
            }
            if (i < 4)
              scratch.steer[i] = (*angles)[i];
          }
      std::string detail;
      if (!instance.advanceDrivetrain(
              config, instance.drivetrainContext(config, seconds), detail)) {
        why = "car step: drivetrain: " + detail;
        return false;
      }
      // Stock globals 0060e02c..0060e048 and 0060e020 (read from the exe).
      static constexpr WheelSlipParameters slipParameters{1.0f, 1.0f, 0.3f, 0.5f,
                                                          0.1f, 0.1f, 0.1f, 0.2f};
      static constexpr float angularGain = 2.8f;
      for (size_t i = 0; i < car->wheels.size(); ++i) {
        auto& wheel = car->wheels[i];
        if (!wheel || !wheel->suspensionBody)
          continue;
        const auto found = witnesses_.find(wheel->suspension.wheelBody);
        if (found == witnesses_.end() || !found->second)
          continue;
        const auto& extra = instance.wheelExtras[i];
        Wheel coefficients;
        std::copy(extra.pacejka.begin(), extra.pacejka.end(),
                  coefficients.pacejka.begin());
        coefficients.setSliction(extra.sliction);
        coefficients.setFriction(extra.friction);
        coefficients.setStiffness(extra.stiffness);
        coefficients.setMaxLoad(extra.maxLoad);
        coefficients.setFrictn_x(extra.frictionX);
        coefficients.setLoadSmooth(extra.loadSmooth);
        // A contact that does not match this predicted pose (wheel in the air,
        // or a face that unloaded) carries no tire force this frame.
        if (stepLiveGroundTire(*found->second, *wheel, coefficients,
                               slipParameters, angularGain, extra.rollResistance,
                               seconds, true)) {
          ++tireSteps_;
          if (&instance == stepCars[primaryIndex].instance)
            scratch.tire[i < 4 ? i : 3] = 'o';
        } else {
          ++tireRejects_;
          if (&instance == stepCars[primaryIndex].instance)
            scratch.tire[i < 4 ? i : 3] = 'r';
          static double lastReject = -1e9;
          const double nowReject = runlog::monotonicSeconds();
          if (nowReject - lastReject >= 1.0) {
            lastReject = nowReject;
            const auto& c = wheel->suspension;
            runlog::event(runlog::EventKind::Note,
                          "car tire rejected: wheel %zu block=\"%s\" "
                          "candidate=%d material=%d "
                          "contactBody=%d length=%g max=%g force=%g",
                          i, found->second->tireBlock(),
                          found->second->tireCandidate() ? 1 : 0,
                          found->second->groundMaterial() ? 1 : 0,
                          c.contactBody ? 1 : 0, c.length, c.settings.maxLength,
                          c.force);
          }
        }
      }
    }
    return true;
  };
  const auto bodyLabel = [&](const physics::Body* body) {
    if (body == car->root.get())
      return std::string("root");
    for (size_t i = 0; i < car->wheels.size(); ++i)
      if (car->wheels[i] && car->wheels[i]->suspension.wheelBody == body)
        return "wheel" + std::to_string(i);
    for (size_t i = 0; i < car->wheels.size(); ++i)
      if (car->wheels[i] && car->wheels[i]->suspensionBody.get() == body)
        return "suspBody" + std::to_string(i);
    char buffer[96];
    const auto rel = body->attachment ? body->attachment->relative.position
                                      : physics::Vec3{};
    std::snprintf(
        buffer, sizeof buffer,
        "part@%p rel=%.2f,%.2f,%.2f dim=%.2f,%.2f,%.2f t=%d pf=%x f=%x",
        (const void*)body, rel[0], rel[1], rel[2],
        body->proto ? body->proto->shape.dim[0] : 0.0f,
        body->proto ? body->proto->shape.dim[1] : 0.0f,
        body->proto ? body->proto->shape.dim[2] : 0.0f,
        body->proto ? int(body->proto->shape.type) : -1,
        body->proto ? unsigned(body->proto->flags) : 0u, unsigned(body->flags));
    return std::string(buffer);
  };
  physics::ContactGeometryWorld geometryWorld;
  hooks.gatherContacts = [&](std::vector<physics::DirectContact>& contacts,
                             std::string& why) {
    contacts.clear();
    CarGroundContacts found;
    for (const auto& stepCar : stepCars) {
      // A tree that fell asleep skips every pass (004a3f90/004a3bc0); its
      // bodies are not registered roots this step, so gathering contacts
      // for it would name unregistered contact bodies. With awake scene
      // objects keeping the full step alive, this is reachable in-run.
      if (stepCar.car->root->flags & physics::kBodyAsleep)
        continue;
      for (auto* sector : stepCar.sectors) {
        auto part = collectCarGroundContacts(resources_, *sector, stepCar.car);
        if (!part.passiveTraversalComplete) {
          why = "car step: ground traversal incomplete: " + part.detail;
          return false;
        }
        for (auto& pair : part.pairs)
          found.pairs.push_back(std::move(pair));
        for (auto& wheel : part.wheels)
          found.wheels.push_back(std::move(wheel));
      }
    }
    for (auto& hit : found.wheels) {
      CarWheelRuntime* runtime = nullptr;
      for (auto* candidate : wheels)
        if (candidate->suspension.wheelBody == hit.wheel)
          runtime = candidate;
      const auto* candidate = hit.contact ? hit.contact->candidate() : nullptr;
      if (!runtime || !candidate) {
        why = "car step: wheel contact without a suspension";
        return false;
      }
      physics::SuspensionSelectionDiag diag;
      physics::Body* const prevContact = runtime->suspension.contactBody;
      const auto selected = physics::selectSuspensionContact(
          runtime->suspension, *candidate, &diag);
      if (selected == physics::SuspensionContactResult::Unsupported) {
        why = "car step: wheel contact selection unsupported";
        return false;
      }
      size_t wheelIndex = 4; // 4 = not a wheel of the diagnosed car
      for (size_t i = 0; i < car->wheels.size(); ++i)
        if (car->wheels[i].get() == runtime)
          wheelIndex = i;
      if (wheelIndex < 4) {
        scratch.candA[wheelIndex] = candidate->pointA[1];
        scratch.candB[wheelIndex] = candidate->pointB[1];
        if (selected == physics::SuspensionContactResult::Selected)
          runlog::event(
              runlog::EventKind::Note,
              "car wheel %zu selected: length=%.3f dist=%.3f stored=%.4f "
              "prevBody=%p newBody=%p",
              wheelIndex, runtime->suspension.length, diag.distance,
              runtime->suspension.distanceSquared, (void*)prevContact,
              (void*)candidate->b);
        ++scratch.hits[wheelIndex];
        scratch.sel[wheelIndex] =
            selected == physics::SuspensionContactResult::Selected ? 'S' : 'R';
        if (selected == physics::SuspensionContactResult::Rejected) {
          scratch.selDiag[wheelIndex][0] = diag.length;
          scratch.selDiag[wheelIndex][1] = diag.distance;
          scratch.selDiag[wheelIndex][2] = diag.auxiliary;
          scratch.selDiag[wheelIndex][3] = diag.storedDistanceSquared;
        }
      }
      // The witness must describe the contact the suspension holds, as of the
      // latest prediction. A repeated gather meets the same ground face with a
      // slightly different predicted wheel pose and the strict "closer" test
      // may reject it; static ground carries the same data, so the held
      // contact is refreshed from the new candidate and the witness replaced.
      auto& held = runtime->suspension;
      const bool sameFace =
          held.contactBody == candidate->b && candidate->surface &&
          held.contactSurface &&
          held.contactFeature == candidate->surface->featureIndex &&
          held.contactSurface->materialWord == candidate->surface->materialWord;
      if (selected == physics::SuspensionContactResult::Selected || sameFace) {
        if (selected != physics::SuspensionContactResult::Selected) {
          held.contactPoint = candidate->pointB;
          held.normal = candidate->normal;
          held.contactSurface = candidate->surface;
          held.contactOwner = candidate->otherOwner;
          held.contactPrototype = candidate->otherPrototype;
        }
        witnesses_[hit.wheel] = std::move(hit.contact);
      }
    }
    // Probe geometry at the vanish transition: wheel 2 had candidates last
    // gather and none now -- log its probe endpoints and rotation.
    if (primaryWheels.size() > 2) {
      auto* rt2 = primaryWheels[2];
      physics::Body* wb2 = rt2->suspension.wheelBody;
      const bool hadHit =
          std::any_of(found.wheels.begin(), found.wheels.end(),
                      [&](const auto& h) { return h.wheel == wb2; });
      static bool lastHadHit = false;
      if (lastHadHit && !hadHit && wb2 && wb2->current < 2) {
        static unsigned probeLogged = 0;
        if (probeLogged < 8) {
          ++probeLogged;
          const auto& ws = wb2->state[wb2->current ^ 1];
          const float radius = wb2->proto ? wb2->proto->shape.dim[0] : 0.0f;
          physics::Vec3 axis{};
          axis[wb2->proto &&
                       wb2->proto->shape.type == physics::ShapeType::CylinderY
                   ? 1
                   : 2] = radius;
          const auto off = physics::matrixTransformRows(ws.rot, axis);
          physics::Vec3 start{}, end{};
          for (unsigned k = 0; k < 3; ++k) {
            start[k] = ws.pos[k] - off[k];
            end[k] = ws.pos[k] + off[k];
          }
          runlog::event(
              runlog::EventKind::Note,
              "car wheel 2 probe: start=%.3f,%.3f,%.3f end=%.3f,%.3f,%.3f "
              "wy=%.3f type=%d quat=%.3f,%.3f,%.3f,%.3f",
              start[0], start[1], start[2], end[0], end[1], end[2], ws.pos[1],
              wb2->proto ? (int)wb2->proto->shape.type : -1, ws.quat[0],
              ws.quat[1], ws.quat[2], ws.quat[3]);
        }
      }
      lastHadHit = hadHit;
    }
    lastOffers_.clear();
    std::set<PairKey> seen;
    // Two loaded city sectors can carry the same face; a body must meet each
    // triangle once or its contact is applied twice. Keyed by the triangle's
    // vertices.
    std::set<std::array<float, 10>> triangles;
    for (auto& fresh : found.pairs) {
      const auto& tri = fresh->pair.triangle;
      std::array<float, 10> signature{};
      signature[0] = static_cast<float>(
          reinterpret_cast<std::uintptr_t>(fresh->body) & 0xffffff);
      for (size_t v = 0; v < 3 && v < tri.vertices.size(); ++v)
        for (size_t a = 0; a < 3; ++a)
          signature[1 + v * 3 + a] = tri.vertices[v].position[a];
      if (!triangles.insert(signature).second)
        continue;
      const PairKey key{fresh->body, fresh->face->leafOwner(),
                        fresh->face->faceIndex()};
      auto entry = pairs_.find(key);
      if (entry == pairs_.end())
        entry = pairs_.emplace(key, std::move(fresh)).first;
      CarGroundFacePair& pair = *entry->second;
      if (!pair.valid()) {
        pendingErase_.insert(key);
        continue;
      }
      physics::Vec3 point{};
      const auto result =
          physics::updateGroundFacePair(pair.pair, geometryWorld, point);
      if (result == physics::ContactGeometryResult::Unsupported) {
        why = "car step: ground face pair update unsupported";
        return false;
      }
      if (result == physics::ContactGeometryResult::Dropped) {
        pendingErase_.insert(key);
        seen.erase(key);
        continue;
      }
      // FUN_004a2d10 replaces an invalid cached-feature row before the
      // new state-6 impact stage; an unchanged state 7 uses normal forces.
      if (!pair.pair.contact.resting)
        pair.correctedOnce = false;
      pair.pair.contact.resting = pair.correctedOnce;
      pendingErase_.erase(key);
      // Offered to the corrector: 004a35d0 writes state 7 after the vfunc
      // regardless of its result. finishContacts promotes selected records;
      // the accumulator/lifecycle snapshots restore that state on failure.
      seen.insert(key);
      contacts.push_back(pair.pair.contact);
      lastOffers_.push_back(OfferedContact{&pair, nullptr, nullptr});
    }
    // Pairs whose face the wheel/body no longer overlaps are scheduled for
    // removal (transactionally, with the rest of the collector state).
    for (const auto& entry : pairs_)
      if (!seen.count(entry.first))
        pendingErase_.insert(entry.first);

    // --- Stepped dynamic scene objects ------------------------------------
    // Stock 004a5190 steps every registered root, so awake objects (cones,
    // barrels, trash cans) fall, collide and sleep like the cars. Their
    // ground traversal is the same 004a1560 -> 004a0aa0 -> 0049fa60 passive
    // walk the car uses, with 0049f4d0 body/face records retained across
    // gathers, plus 0049b830 body/body records against the other stepped
    // roots.
    const auto& objects = world_.steppedObjectPhysics();
    std::set<PairKey> seenObject;
    std::set<DynamicPairKey> seenDynamic;
    std::vector<physics::Body*> carBodies;
    for (const auto& stepCar : stepCars) {
      // Sleeping cars remain registered narrow-phase targets; only their
      // own ground traversal/force/prediction passes are skipped.
      carBodies.push_back(stepCar.car->root.get());
      for (const auto& childEntry : stepCar.car->bodyChildren)
        // The map keys the bodies as const pointers; the bodies themselves
        // are the same mutable physics bodies the car tree steps.
        if (childEntry.first)
          carBodies.push_back(const_cast<physics::Body*>(childEntry.first));
    }
    // Bounding proxies never produce contacts; only narrow-phase geometry
    // can wake a sleeping tree through the registered direct corrector.
    const auto degenerate = [](const physics::Body* body) {
      const auto& s = body->proto->shape;
      // 0049a050/00498f50 put the aggregate core on the compound root, so a
      // root is not geometry-less in stock either: the compound path (type
      // 5/6) ends by building an 8-vertex core with zero features
      // (004ac2b0 at 0x499f76, this = shape+0x20) and rewriting the shape to
      // type 6 with radius 0, dims 0, extents from the child box and
      // boundRadius sqrt(low^2/high^2) -- exactly finalizeCompound. What
      // makes a compound root take no body pair is the feature count: the
      // dispatcher returns 0 when shape core 0x28 == 0 (0x49cf58/0x49d10b)
      // *and* only reaches that check after the child fan-out, so stock's
      // own contact work for these roots is the children. The port skips the
      // root pair and pairs the children separately, so the observable
      // pairing is the same; the mesh vertex/triangle test is used only
      // because the port stores that featureless core in shape.core rather
      // than as mesh geometry.
      return (s.type == physics::ShapeType::Mesh ||
              s.type == physics::ShapeType::Hull) &&
             s.vertices.empty() && s.triangles.empty();
    };
    // FUN_004a5190 traverses every registered root against other roots;
    // FUN_0049c8f0 fans compound pairs out to their leaf bodies. Cars are
    // registered roots too, even when there are no scene objects nearby.
    for (size_t i = 0; i < stepCars.size(); ++i) {
      for (size_t j = i + 1; j < stepCars.size(); ++j) {
        const auto& firstCar = *stepCars[i].car;
        const auto& secondCar = *stepCars[j].car;
        if ((firstCar.root->flags & physics::kBodyAsleep) &&
            (secondCar.root->flags & physics::kBodyAsleep))
          continue;
        for (const auto& first : firstCar.bodyChildren)
          for (const auto& second : secondCar.bodyChildren) {
            auto* a = const_cast<physics::Body*>(first.first);
            auto* b = const_cast<physics::Body*>(second.first);
            if (!a || !b || degenerate(a) || degenerate(b))
              continue;
            if (!considerDynamicPair(a, b, geometryWorld, contacts,
                                     seenDynamic, pendingDynamicErase_, why))
              return false;
          }
      }
    }
    // Stock resolves an object through the sector its body is queued in; the
    // port has no body-sector queue, so each object uses the loaded sectors
    // whose bounds contain its predicted centre (the same approximation the
    // car step uses).
    for (size_t handleIndex = 0; handleIndex < objects.size(); ++handleIndex) {
      auto* handle = objects[handleIndex];
      auto* objectRoot = handle->root();
      if (!objectRoot || !objectRoot->proto || objectRoot->current > 1) {
        why = "car step: object root unavailable";
        return false;
      }
      // 004a5e80 builds each shape's collision core on first use.
      for (auto* body : handle->bodies()) {
        if (!body || !body->proto || body->proto->shape.core)
          continue;
        auto* proto = const_cast<physics::BodyProto*>(body->proto);
        if (!physics::buildShapeCore(*proto, proto->shape.core, error)) {
          why = "car step: object collision core: " + error;
          return false;
        }
      }
      if ((objectRoot->flags & physics::kBodyAsleep) == 0) {
        const auto& rootState = objectRoot->state[objectRoot->current ^ 1];
        const float rootRadius = objectRoot->proto->shape.boundRadius;
        const auto objectSectors =
            resources_.groundSectorsAt(rootState.pos, rootRadius + 2.0f);
        if (objectSectors.empty()) {
          why = "car step: object has no loaded ground sector";
          return false;
        }
        std::array<float, 3> minimum{}, maximum{};
        for (size_t axis = 0; axis < 3; ++axis) {
          minimum[axis] = rootState.pos[axis] - rootRadius;
          maximum[axis] = rootState.pos[axis] + rootRadius;
        }
        // Object slots all attach to the compound root (00465bd0), so the tree
        // is the root plus its direct children in body-table order.
        std::vector<physics::Body*> leaves;
        for (auto* body : handle->bodies())
          if (body && body != objectRoot && body->attachment &&
              body->attachment->parent == objectRoot)
            leaves.push_back(body);
        const bool compound = !leaves.empty();
        if (!compound)
          leaves.push_back(objectRoot);
        for (auto* sector : objectSectors) {
          auto candidates = queryPassiveSpatialCandidates(resources_, *sector,
                                                          minimum, maximum);
          if (!candidates.passiveTraversalComplete) {
            // A spatial node whose payload needs factory actions the port
            // cannot prepare (shop triggers and similar) has unknown collision
            // coverage. Keep returned candidates but log the missing coverage;
            // do not claim those omitted nodes are collision-free in stock.
            if (candidates.detail.find("spatial load incomplete") ==
                std::string::npos) {
              why = "car step: object ground traversal incomplete: " +
                    candidates.detail;
              return false;
            }
            static bool objectSkipNoted = false;
            if (!objectSkipNoted) {
              objectSkipNoted = true;
              runlog::event(runlog::EventKind::Note,
                            "object step: spatial nodes with unpreparable "
                            "factory actions have unknown object collision coverage "
                            "(%s)",
                            candidates.detail.c_str());
            }
          }
          for (const auto& spatial : candidates.candidates) {
            auto& node = *spatial.node;
            const auto& geometry = *spatial.payload->physics;
            for (size_t faceIndex = 0; faceIndex < geometry.triangles.size();
                 ++faceIndex) {
              if (compound) {
                const auto mask = queryParentFace(geometry, faceIndex,
                                                  *objectRoot, *node.bounds,
                                                  false);
                if (mask == ParentFaceMask::Rejected)
                  continue;
                if (mask == ParentFaceMask::Unsupported) {
                  why = "car step: object parent face filter unsupported";
                  return false;
                }
              }
              for (auto* leaf : leaves) {
                if (leaf->current > 1 ||
                    !std::isfinite(leaf->proto->shape.boundRadius) ||
                    leaf->proto->shape.boundRadius < 0) {
                  why = "car step: object spatial bounds unsupported";
                  return false;
                }
                bool outside = false;
                for (size_t axis = 0; axis < 3; ++axis) {
                  const float coordinate =
                      leaf->state[leaf->current ^ 1].pos[axis];
                  if (!std::isfinite(coordinate)) {
                    why = "car step: object spatial bounds unsupported";
                    return false;
                  }
                  if (leaf->proto->shape.boundRadius +
                          node.bounds->values[axis + 3] <
                      std::fabs(coordinate - node.bounds->values[axis])) {
                    outside = true;
                    break;
                  }
                }
                if (outside)
                  continue;
                std::string faceError;
                auto face = resolveSpatialGroundFace(resources_, node,
                                                     faceIndex, faceError);
                if (!face) {
                  why = "car step: object ground face unavailable: " + faceError;
                  return false;
                }
                // Two loaded city sectors can carry the same face; a body must
                // meet each triangle once or its contact is applied twice.
                const auto& triangle = geometry.triangles[faceIndex];
                std::array<float, 10> signature{};
                signature[0] = static_cast<float>(
                    reinterpret_cast<std::uintptr_t>(leaf) & 0xffffff);
                bool badVertex = false;
                for (size_t v = 0; v < 3 && v < triangle.vertices.size(); ++v) {
                  if (triangle.vertices[v] >= geometry.vertices.size()) {
                    badVertex = true;
                    break;
                  }
                  for (size_t a = 0; a < 3; ++a)
                    signature[1 + v * 3 + a] =
                        geometry.vertices[triangle.vertices[v]][a];
                }
                if (badVertex)
                  continue;
                if (!triangles.insert(signature).second)
                  continue;
                const PairKey key{leaf, face->leafOwner(), faceIndex};
                auto entry = objectPairs_.find(key);
                if (entry == objectPairs_.end()) {
                  auto ground = face->bodyOwner();
                  if (!ground) {
                    why = "car step: object ground body unavailable";
                    return false;
                  }
                  std::array<physics::Vec3, 3> vertices{};
                  for (size_t v = 0; v < 3; ++v) {
                    if (triangle.vertices[v] >= geometry.vertices.size()) {
                      why = "car step: object ground face vertex unavailable";
                      return false;
                    }
                    vertices[v] = geometry.vertices[triangle.vertices[v]];
                  }
                  auto retained = std::make_unique<ObjectGroundFacePair>();
                  if (!physics::createGroundFacePair(*leaf, ground->body,
                                                     vertices, triangle.normal,
                                                     retained->pair, faceError)) {
                    why = "car step: object ground pair unsupported: " +
                          faceError;
                    return false;
                  }
                  retained->face = std::move(face);
                  retained->prototype = leaf->proto;
                  entry = objectPairs_.emplace(key, std::move(retained)).first;
                }
                ObjectGroundFacePair& pair = *entry->second;
                if (!pair.valid()) {
                  pendingObjectErase_.insert(key);
                  continue;
                }
                physics::Vec3 point{};
                const auto result = physics::updateGroundFacePair(
                    pair.pair, geometryWorld, point);
                if (result == physics::ContactGeometryResult::Unsupported) {
                  why = "car step: object ground pair update unsupported";
                  return false;
                }
                if (result == physics::ContactGeometryResult::Dropped) {
                  pendingObjectErase_.insert(key);
                  seenObject.erase(key);
                  continue;
                }
                if (!pair.pair.contact.resting)
                  pair.correctedOnce = false;
                pair.pair.contact.resting = pair.correctedOnce;
                pendingObjectErase_.erase(key);
                seenObject.insert(key);
                contacts.push_back(pair.pair.contact);
                lastOffers_.push_back(OfferedContact{nullptr, &pair, nullptr});
              }
            }
          }
        }
      } // Sleeping candidates skip static ground, not awake body pairs.
      // 0049b830 body/body records against the bodies of the stepped cars.
      for (auto* body : handle->bodies()) {
        if (!body || degenerate(body))
          continue;
        for (auto* other : carBodies) {
          if (!other || degenerate(other))
            continue;
          if (!considerDynamicPair(body, other, geometryWorld, contacts,
                                   seenDynamic, pendingDynamicErase_, why))
            return false;
        }
      }
    }
    for (size_t i = 0; i < objects.size(); ++i) {
      for (size_t j = i + 1; j < objects.size(); ++j) {
        if ((objects[i]->root()->flags & physics::kBodyAsleep) != 0 &&
            (objects[j]->root()->flags & physics::kBodyAsleep) != 0)
          continue;
        for (auto* body : objects[i]->bodies())
          for (auto* other : objects[j]->bodies()) {
            if (!body || !other || degenerate(body) || degenerate(other))
              continue;
            if (!considerDynamicPair(body, other, geometryWorld, contacts,
                                     seenDynamic, pendingDynamicErase_, why))
              return false;
          }
      }
    }
    for (const auto& entry : objectPairs_)
      if (!seenObject.count(entry.first))
        pendingObjectErase_.insert(entry.first);
    for (const auto& entry : dynamicPairs_)
      if (!seenDynamic.count(entry.first))
        pendingDynamicErase_.insert(entry.first);

    scratch.pairs = std::max(scratch.pairs, lastOffers_.size());
    // Enumerate the near-rest support contacts once in a while: what holds
    // the root at its resting height.
    {
      static double lastSupportLog = -1e9;
      const double now = runlog::monotonicSeconds();
      const auto& rp = car->root->state[car->root->current].pos;
      const float speed =
          std::sqrt(car->root->state[car->root->current].vel[0] *
                        car->root->state[car->root->current].vel[0] +
                    car->root->state[car->root->current].vel[1] *
                        car->root->state[car->root->current].vel[1] +
                    car->root->state[car->root->current].vel[2] *
                        car->root->state[car->root->current].vel[2]);
      if (now - lastSupportLog >= 2.0) {
        // Nearest contact to touching, whoever it is: what carries the load.
        const physics::DirectContact* nearest = nullptr;
        for (const auto& c : contacts)
          if (!nearest || c.separation < nearest->separation)
            nearest = &c;
        if (nearest)
          runlog::event(runlog::EventKind::Note,
                        "nearest contact: %s sep=%.3f imp=%.0f rootY=%.3f",
                        bodyLabel(nearest->a).substr(0, 110).c_str(),
                        nearest->separation, nearest->normalImpulse, rp[1]);
      }
      if (now - lastSupportLog >= 2.0 && speed < 1.0f) {
        lastSupportLog = now;
        static std::set<std::string> supportLogged;
        for (const auto& c : contacts) {
          if (c.separation > 0.05f)
            continue;
          std::string label = bodyLabel(c.a).substr(0, 90);
          std::string key =
              label + "@" + std::to_string((int)(c.separation * 4.0f));
          if (supportLogged.insert(key).second)
            runlog::event(runlog::EventKind::Note,
                          "support: %s sep=%.3f imp=%.0f root=%.3f",
                          label.c_str(), c.separation, c.normalImpulse, rp[1]);
        }
      }
    }
    return true;
  };
  size_t contactIterations = 0;
  float maxImpulse = 0;
  hooks.finishContacts = [&](const std::vector<physics::DirectContact>& done) {
    if (!done.empty() && !notedWheelEdgewalk_) {
      notedWheelEdgewalk_ = true;
      runlog::event(runlog::EventKind::Note,
                    "car step: dynamic wheel contact subgap: FUN_0049c540 wheel face crossing uses a convex-face fallback rather than the complete cached feature edgewalk");
    }
    const size_t slot = std::min<size_t>(contactIterations, 7);
    ++contactIterations;
    // done[i] maps to lastOffers_[i]: every offered contact's write-back
    // target, in collector order (car and object ground pairs, dynamic pairs).
    const auto offerFace = [&](size_t index) -> const SpatialGroundFace* {
      if (index >= lastOffers_.size())
        return nullptr;
      if (lastOffers_[index].carPair)
        return lastOffers_[index].carPair->face.get();
      if (lastOffers_[index].objectPair)
        return lastOffers_[index].objectPair->face.get();
      return nullptr;
    };
    const auto offerGroundPair =
        [&](size_t index) -> const physics::GroundFacePair* {
      if (index >= lastOffers_.size())
        return nullptr;
      if (lastOffers_[index].carPair)
        return &lastOffers_[index].carPair->pair;
      if (lastOffers_[index].objectPair)
        return &lastOffers_[index].objectPair->pair;
      return nullptr;
    };

    float iterationMax = 0;
    float sepMin = 0, sepMax = 0;
    unsigned correctedCount = 0, playerContacts = 0, playerCorrections = 0, worldCorrections = 0;
    float playerImpulse = 0;
    const auto belongsToPlayer = [&](const physics::Body* body) {
      while (body && body->attachment)
        body = body->attachment->parent;
      return body == car->root.get();
    };
    for (size_t ci = 0; ci < done.size(); ++ci) {
      const auto& c = done[ci];
      worldCorrections += c.correctionResult != 0;
      const bool playerContact = belongsToPlayer(c.a) || belongsToPlayer(c.b);
      if (playerContact) {
        ++playerContacts;
        playerCorrections += c.correctionResult != 0;
        playerImpulse = std::max(playerImpulse, std::fabs(c.normalImpulse));
      }
      // Retain every first-pass player contact and subsequent corrections:
      // Cartrace's single worst impulse cannot establish wall separation or
      // distinguish a renewed body impact from a stale retained record.
      if (playerContact && (contactIterations == 1 || c.correctionResult != 0)) {
        auto* channel = runlog::traceChannel("contacttrace");
        if (channel && std::getenv("SLRR_CAR_TRACE"))
          channel->stream.store(true, std::memory_order_relaxed);
        const auto* face = offerFace(ci);
        const auto* groundPair = offerGroundPair(ci);
        const auto* dynamic = ci < lastOffers_.size() ? lastOffers_[ci].dynamicPair : nullptr;
        const auto featureA = groundPair ? groundPair->bodyFeature : dynamic ? dynamic->pair.firstFeature
                                                                             : std::nullopt;
        const auto featureB = groundPair ? groundPair->triangleFeature : dynamic ? dynamic->pair.secondFeature
                                                                                 : std::nullopt;
        TraceLine detail;
        detail.add("{\"kind\":\"body-contact\",\"tick\":%llu,\"pass\":%zu,"
                   "\"a\":\"%p\",\"b\":\"%p\",\"leaf\":\"%p\",\"face\":%zu,"
                   "\"features\":[%lld,%lld],\"resting\":%d,\"correction\":%d,"
                   "\"sep\":%.9g,\"impulse\":%.9g,\"n\":[%.9g,%.9g,%.9g],"
                   "\"pa\":[%.9g,%.9g,%.9g],\"pb\":[%.9g,%.9g,%.9g]}",
                   static_cast<unsigned long long>(tick_), contactIterations,
                   static_cast<void*>(c.a), static_cast<void*>(c.b),
                   face ? static_cast<const void*>(face->leafOwner()) : nullptr,
                   face ? face->faceIndex() : 0,
                   featureA ? static_cast<long long>(*featureA) : -1LL,
                   featureB ? static_cast<long long>(*featureB) : -1LL,
                   int(c.resting), c.correctionResult, c.separation, c.normalImpulse,
                   c.normal[0], c.normal[1], c.normal[2],
                   c.pointA[0], c.pointA[1], c.pointA[2],
                   c.pointB[0], c.pointB[1], c.pointB[2]);
        runlog::traceSanitizeJson(detail.text, sizeof detail.text);
        runlog::tracePush(channel, detail.text);
      }
      sepMin = ci ? std::min(sepMin, c.separation) : c.separation;
      sepMax = ci ? std::max(sepMax, c.separation) : c.separation;
      correctedCount += c.normalImpulse != 0;
      if (contactIterations == 1 && c.separation < scratch.minSeparation) {
        scratch.minSeparation = c.separation;
        scratch.minSepNormal = c.normal;
        scratch.minSepPointB = c.pointB;
        std::snprintf(scratch.minSepBody, sizeof scratch.minSepBody, "%s",
                      bodyLabel(c.a).substr(0, 90).c_str());
        if (offerFace(ci) && offerFace(ci)->face()) {
          const auto& t = offerGroundPair(ci)->triangle;
          std::snprintf(
              scratch.minSepFace, sizeof scratch.minSepFace,
              "mat=0x%x [%.2f,%.2f,%.2f] [%.2f,%.2f,%.2f] [%.2f,%.2f,%.2f]",
              offerFace(ci)->face()->materialWord,
              t.vertices[0].position[0], t.vertices[0].position[1],
              t.vertices[0].position[2], t.vertices[1].position[0],
              t.vertices[1].position[1], t.vertices[1].position[2],
              t.vertices[2].position[0], t.vertices[2].position[1],
              t.vertices[2].position[2]);
        }
      }
      const float impulse = std::fabs(c.normalImpulse);
      iterationMax = std::max(iterationMax, impulse);
      if (impulse > scratch.worstImpulse) {
        if (offerFace(ci) && offerFace(ci)->face()) {
          const auto& t = offerGroundPair(ci)->triangle;
          std::snprintf(
              scratch.worstFace, sizeof scratch.worstFace,
              "mat=0x%x [%.2f,%.2f,%.2f] [%.2f,%.2f,%.2f] [%.2f,%.2f,%.2f]",
              offerFace(ci)->face()->materialWord,
              t.vertices[0].position[0], t.vertices[0].position[1],
              t.vertices[0].position[2], t.vertices[1].position[0],
              t.vertices[1].position[1], t.vertices[1].position[2],
              t.vertices[2].position[0], t.vertices[2].position[1],
              t.vertices[2].position[2]);
        }
        scratch.worstImpulse = impulse;
        scratch.worstSeparation = c.separation;
        scratch.worstPartPos = c.a->state[c.a->current ^ 1].pos;
        scratch.worstPointB = c.pointB;
        scratch.worstPointA = c.pointA;
        scratch.worstNormal = c.normal;
        std::snprintf(scratch.worstBody, sizeof scratch.worstBody, "%s",
                      bodyLabel(c.a).substr(0, 90).c_str());
      }
    }
    maxImpulse = std::max(maxImpulse, iterationMax);
    scratch.iterPlayerCorrections[slot] = static_cast<unsigned short>(playerCorrections);
    scratch.iterWorldCorrections[slot] = static_cast<unsigned short>(worldCorrections);
    scratch.iterPlayerContacts[slot] = static_cast<unsigned short>(playerContacts);
    scratch.iterPlayerImpulse[slot] = playerImpulse;
    scratch.iterContacts[slot] = static_cast<unsigned short>(done.size());
    scratch.iterMaxImpulse[slot] = iterationMax;
    scratch.iterSepMin[slot] = sepMin;
    scratch.iterSepMax[slot] = sepMax;
    scratch.iterCorrected[slot] = static_cast<unsigned short>(correctedCount);
    scratch.iterRootShiftY[slot] = car->root->state[car->root->current].shift[1];
    for (size_t i = 0; i < done.size() && i < lastOffers_.size(); ++i) {
      const auto& offer = lastOffers_[i];
      if (offer.carPair) {
        offer.carPair->pair.contact = done[i];
        if (done[i].processed) offer.carPair->correctedOnce = true;
      } else if (offer.objectPair) {
        offer.objectPair->pair.contact = done[i];
        if (done[i].processed) offer.objectPair->correctedOnce = true;
      } else if (offer.dynamicPair) {
        offer.dynamicPair->pair.contact = done[i];
        if (done[i].processed) offer.dynamicPair->pair.contact.resting = true;
      }
    }
  };
  const auto speedOf = [&] {
    const auto& v = car->root->state[car->root->current].vel;
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  };
  const physics::Vec3 prePos = car->root->state[car->root->current].pos;
  const physics::Vec3 preVel = car->root->state[car->root->current].vel;
  const bool asleepBeforeStep = (car->root->flags & physics::kBodyAsleep) != 0;
  const float speedBefore = speedOf();
  const double wallNow = runlog::monotonicSeconds();
  const double wallDelta = lastWall_ > 0 ? wallNow - lastWall_ : 0;
  lastWall_ = wallNow;
  const uint64_t tick = ++tick_;
  // Retained-contact accumulators and lifecycle flags snapshot: restored if
  // the step fails, because the corrector mutates them in place.
  pendingErase_.clear();
  accumulatorSnapshot_.clear();
  for (const auto& entry : pairs_)
    accumulatorSnapshot_[entry.first] = {
        entry.second->pair.contact.normalImpulse,
        entry.second->pair.contact.frictionForce,
        entry.second->correctedOnce ? 1.0f : 0.0f,
        entry.second->pair.contact.normalForce,
        entry.second->pair.contact.resting ? 1.0f : 0.0f};
  objectAccumulatorSnapshot_.clear();
  for (const auto& entry : objectPairs_)
    objectAccumulatorSnapshot_[entry.first] = {
        entry.second->pair.contact.normalImpulse,
        entry.second->pair.contact.frictionForce,
        entry.second->correctedOnce ? 1.0f : 0.0f,
        entry.second->pair.contact.normalForce,
        entry.second->pair.contact.resting ? 1.0f : 0.0f};
  dynamicAccumulatorSnapshot_.clear();
  for (const auto& entry : dynamicPairs_)
    dynamicAccumulatorSnapshot_[entry.first] = {
        entry.second->pair.contact.normalImpulse,
        entry.second->pair.contact.frictionForce,
        entry.second->pair.contact.normalForce,
        entry.second->pair.contact.resting ? 1.0f : 0.0f};
  // State 7 normal-row multipliers belong to this world solve, not the
  // retained pair lifetime. Failure restores the pre-step snapshot below.
  for (auto& entry : pairs_) {
    entry.second->pair.contact.normalForce = 0;
    entry.second->pair.contact.frictionForce = 0;
  }
  for (auto& entry : objectPairs_) {
    entry.second->pair.contact.normalForce = 0;
    entry.second->pair.contact.frictionForce = 0;
  }
  for (auto& entry : dynamicPairs_) {
    entry.second->pair.contact.normalForce = 0;
    entry.second->pair.contact.frictionForce = 0;
  }
  const bool stepped = world_.stepRegisteredCars(seconds, hooks, error);
  if (const auto delta = physics::corePairDiagnosticsDelta(); !delta.empty())
    runlog::event(runlog::EventKind::Note,
                  "core pair walks gave up or hit the loop cap (site=line "
                  "counts, cumulative): %s",
                  delta.c_str());
  // The camera follow devices are armed by the seat camera pose; stock steps
  // them every frame (00438b60) while the car moves. stepEmptyPhysics did this
  // only when no car was registered, which froze the chase camera at its spawn
  // pose once a car existed.
  if (stepped) {
    if (seconds >= 0.0001f) {
      for (const auto& entry : stepCars) {
        auto& runtime = *entry.instance;
        runtime.smoothedSpeed = entry.smoothedSpeed;
        // FUN_00454500 excludes damage-disabled cars (flag 0x400).
        if (runtime.damageEnabled &&
            (runtime.smoothedSpeed > 0.01f ||
             std::fabs(runtime.engineAngularSpeed) > 0.01f ||
             std::fabs(runtime.drivenWheelSpeed) > 0.01f))
          runtime.mileage = seconds * runtime.smoothedSpeed + runtime.mileage;
      }
    }
    // Pairs still scheduled were not re-detected this step: remove them.
    // Re-detected ones returned to resting state 7 above.
    for (const auto& key : pendingErase_)
      pairs_.erase(key);
    for (const auto& key : pendingObjectErase_)
      objectPairs_.erase(key);
    for (const auto& key : pendingDynamicErase_)
      dynamicPairs_.erase(key);
  } else {
    for (const auto& snap : accumulatorSnapshot_) {
      auto found = pairs_.find(snap.first);
      if (found == pairs_.end())
        continue;
      found->second->pair.contact.normalImpulse = snap.second[0];
      found->second->pair.contact.frictionForce = snap.second[1];
      found->second->correctedOnce = snap.second[2] != 0.0f;
      found->second->pair.contact.normalForce = snap.second[3];
      found->second->pair.contact.resting = snap.second[4] != 0.0f;
    }
    for (const auto& snap : objectAccumulatorSnapshot_) {
      auto found = objectPairs_.find(snap.first);
      if (found == objectPairs_.end())
        continue;
      found->second->pair.contact.normalImpulse = snap.second[0];
      found->second->pair.contact.frictionForce = snap.second[1];
      found->second->correctedOnce = snap.second[2] != 0.0f;
      found->second->pair.contact.normalForce = snap.second[3];
      found->second->pair.contact.resting = snap.second[4] != 0.0f;
    }
    for (const auto& snap : dynamicAccumulatorSnapshot_) {
      auto found = dynamicPairs_.find(snap.first);
      if (found == dynamicPairs_.end())
        continue;
      found->second->pair.contact.normalImpulse = snap.second[0];
      found->second->pair.contact.frictionForce = snap.second[1];
      found->second->pair.contact.normalForce = snap.second[2];
      found->second->pair.contact.resting = snap.second[3] != 0.0f;
    }
  }
  pendingErase_.clear();
  pendingObjectErase_.clear();
  pendingDynamicErase_.clear();

  {
    // One structured line per step. The ring is always on (formatting is a few
    // microseconds); SLRR_CAR_TRACE=1 also streams every line to
    // cartrace-full.jsonl in the run directory.
    static runlog::TraceChannel* channel = runlog::traceChannel("cartrace");
    if (channel && std::getenv("SLRR_CAR_TRACE"))
      channel->stream.store(true, std::memory_order_relaxed);
    const auto& root = car->root->state[car->root->current];
    {
      static unsigned rootQuatLogged = 0;
      if (rootQuatLogged < 14 && tick >= 3 && tick <= 10) {
        ++rootQuatLogged;
        runlog::event(runlog::EventKind::Note,
                      "car root tick %u: flags=0x%x quat=%.4f,%.4f,%.4f,%.4f "
                      "angMom=%.3f,%.3f,%.3f",
                      (unsigned)tick, car->root->flags, root.quat[0],
                      root.quat[1], root.quat[2], root.quat[3], root.angMom[0],
                      root.angMom[1], root.angMom[2]);
      }
    }
    const float speedAfter = speedOf();
    std::string tags;
    const auto flag = [&](const char* tag) {
      if (!tags.empty())
        tags += ',';
      tags += tag;
    };
    if (!stepped)
      flag("step_failed");
    bool finite = true;
    for (size_t i = 0; i < 3; ++i)
      finite &= std::isfinite(root.pos[i]) && std::isfinite(root.vel[i]) &&
                std::isfinite(root.omega[i]);
    if (!finite)
      flag("nonfinite");
    if (stepped && std::fabs(speedAfter - speedBefore) > 8.0f)
      flag("speed_jump");
    if (contactIterations >= 20)
      flag("iteration_cap");
    if (!(seconds > 0) || seconds > 0.1f)
      flag("dt_spike");

    TraceLine line;
    line.add("{\"tick\":%llu,\"car\":\"%p\",\"f\":%u,\"wall\":%.4f,"
             "\"wdt\":%.4f,\"dt\":%.4f,\"pt\":%.4f,\"ok\":%d,",
             static_cast<unsigned long long>(tick),
             static_cast<const void*>(car.get()),
             runlog::g_frame.load(std::memory_order_relaxed), wallNow,
             wallDelta, seconds, world_.physicsTime(), stepped ? 1 : 0);
    line.add("\"pre\":{\"p\":[%.3f,%.3f,%.3f],\"v\":[%.3f,%.3f,%.3f]},",
             prePos[0], prePos[1], prePos[2], preVel[0], preVel[1], preVel[2]);
    line.add("\"post\":{\"p\":[%.3f,%.3f,%.3f],\"v\":[%.3f,%.3f,%.3f],"
             "\"w\":[%.3f,%.3f,%.3f]},\"spd\":[%.2f,%.2f],\"asleep\":%d,",
             root.pos[0], root.pos[1], root.pos[2], root.vel[0], root.vel[1],
             root.vel[2], root.omega[0], root.omega[1], root.omega[2],
             speedBefore, speedAfter,
             int((car->root->flags & physics::kBodyAsleep) != 0));
    // Only report the wake source observed in this step. Wakes that occurred
    // before entry (for example a queued reset) have no attributable source.
    line.add("\"woke_by\":%s,",
             asleepBeforeStep && hooks.wakeCars && stepped ? "\"controls\"" : "null");
    line.add("\"ctl\":{\"src\":%d,\"thr\":%.2f,\"brk\":%.2f,\"clu\":%.2f,"
             "\"str\":%.2f,\"gear\":%d,\"eng\":%.1f,\"mode\":%d,\"shift\":%d,\"driven\":%.3f},",
             lastControls_ ? 1 : 0, instance.throttleState, instance.brakeState,
             instance.clutchState, instance.steeringState, instance.currentGear,
             instance.engineAngularSpeed, instance.transmissionMode,
             lastControls_ ? lastControls_->digital[2] : 0, instance.drivenWheelSpeed);
    line.add("\"wh\":[");
    bool firstWheel = true;
    for (size_t i = 0; i < car->wheels.size() && i < 4; ++i) {
      const auto& w = car->wheels[i];
      if (!w || !w->suspensionBody)
        continue;
      const auto& u = w->suspension;
      const auto found = witnesses_.find(u.wheelBody);
      line.add("%s{\"i\":%zu,\"w\":%.1f,\"l\":%.3f,\"F\":%.0f,\"cb\":%d,"
               "\"wi\":%d,\"s\":\"%c\",\"t\":\"%c\",\"h\":%d,\"cy\":%.3f,"
               "\"wy\":%.3f,\"sl\":%.2f,\"sa\":%.3f,\"ls\":%.3f,\"aa\":%.3f,"
               "\"rj\":[%.3f,%.3f,%.3f,%.3f],"
               "\"ca\":%.3f,\"cbd\":%.3f}",
               firstWheel ? "" : ",", i, w->angularSpeed, u.length, u.force,
               u.contactBody ? 1 : 0,
               int(found != witnesses_.end() && found->second), scratch.sel[i],
               scratch.tire[i], scratch.hits[i], u.contactPoint[1],
               u.wheelBody ? u.wheelBody->state[u.wheelBody->current].pos[1]
                           : 0.0f,
               w->slipMagnitude, scratch.steer[i], w->longitudinalSlip,
               w->slipAngle, scratch.selDiag[i][0], scratch.selDiag[i][1],
               scratch.selDiag[i][2], scratch.selDiag[i][3], scratch.candA[i],
               scratch.candB[i]);
      firstWheel = false;
    }
    line.add("],\"col\":{\"pairs\":%zu,\"iter\":%zu,\"n\":[", scratch.pairs,
             contactIterations);
    const size_t shownIterations = std::min<size_t>(contactIterations, 8);
    for (size_t i = 0; i < shownIterations; ++i)
      line.add("%s%u", i ? "," : "", unsigned(scratch.iterContacts[i]));
    line.add("],\"player_n\":[");
    for (size_t i = 0; i < shownIterations; ++i)
      line.add("%s%u", i ? "," : "", unsigned(scratch.iterPlayerContacts[i]));
    line.add("],\"player_corrected\":[");
    for (size_t i = 0; i < shownIterations; ++i)
      line.add("%s%u", i ? "," : "", unsigned(scratch.iterPlayerCorrections[i]));
    line.add("],\"world_corrected\":[");
    for (size_t i = 0; i < shownIterations; ++i)
      line.add("%s%u", i ? "," : "", unsigned(scratch.iterWorldCorrections[i]));
    line.add("],\"player_imp\":[");
    for (size_t i = 0; i < shownIterations; ++i)
      line.add("%s%.0f", i ? "," : "", scratch.iterPlayerImpulse[i]);
    line.add("],\"imp\":[");
    for (size_t i = 0; i < shownIterations; ++i)
      line.add("%s%.0f", i ? "," : "", scratch.iterMaxImpulse[i]);
    line.add("],\"sep\":[");
    for (size_t i = 0; i < shownIterations; ++i)
      line.add("%s[%.3f,%.3f,%u]", i ? "," : "", scratch.iterSepMin[i],
               scratch.iterSepMax[i], unsigned(scratch.iterCorrected[i]));
    line.add("],\"shy\":[");
    for (size_t i = 0; i < shownIterations; ++i)
      line.add("%s%.3f", i ? "," : "", scratch.iterRootShiftY[i]);
    line.add(
        "],\"worst\":{\"imp\":%.0f,\"sep\":%.3f,\"n\":[%.2f,%.2f,%.2f],"
        "\"on\":\"%s\",\"face\":\"%s\","
        "\"wp\":[%.3f,%.3f,%.3f],\"pb\":[%.3f,%.3f,%.3f],"
        "\"pa\":[%.3f,%.3f,%.3f]},\"minsep\":{"
        "\"sep\":%.3f,\"n\":[%.2f,%.2f,%.2f],\"on\":\"%s\","
        "\"face\":\"%s\",\"pb\":[%.3f,%.3f,%.3f]}}",
        scratch.worstImpulse, scratch.worstSeparation, scratch.worstNormal[0],
        scratch.worstNormal[1], scratch.worstNormal[2], scratch.worstBody,
        scratch.worstFace, scratch.worstPartPos[0], scratch.worstPartPos[1],
        scratch.worstPartPos[2], scratch.worstPointB[0], scratch.worstPointB[1],
        scratch.worstPointB[2], scratch.worstPointA[0], scratch.worstPointA[1],
        scratch.worstPointA[2], scratch.minSeparation, scratch.minSepNormal[0],
        scratch.minSepNormal[1], scratch.minSepNormal[2], scratch.minSepBody,
        scratch.minSepFace, scratch.minSepPointB[0], scratch.minSepPointB[1],
        scratch.minSepPointB[2]);
    if (!tags.empty())
      line.add(",\"anom\":\"%s\"", tags.c_str());
    if (!stepped) {
      std::string clean = error.substr(0, 160);
      for (char& c : clean)
        if (c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20)
          c = '\'';
      line.add(",\"err\":\"%s\"", clean.c_str());
    }
    line.add("}");
    if (line.length + 2 >= sizeof line.text) // truncated: keep the JSON valid
      std::snprintf(line.text, sizeof line.text,
                    "{\"tick\":%llu,\"truncated\":1%s}",
                    static_cast<unsigned long long>(tick),
                    tags.empty() ? "" : ",\"anom\":\"see anomaly note\"");
    runlog::traceSanitizeJson(line.text, sizeof line.text);
    runlog::tracePush(channel, line.text);
    // Geometry identities and vectors allow road-boundary transitions to be
    // paired with Cartrace without growing its bounded line buffer.
    auto* contactChannel = runlog::traceChannel("contacttrace");
    if (contactChannel && std::getenv("SLRR_CAR_TRACE"))
      contactChannel->stream.store(true, std::memory_order_relaxed);
    TraceLine road;
    road.add("{\"kind\":\"wheels\",\"tick\":%llu,\"car\":\"%p\",\"wh\":[",
             static_cast<unsigned long long>(tick), static_cast<const void*>(car.get()));
    bool firstContact = true;
    for (size_t i = 0; i < car->wheels.size() && i < 4; ++i) {
      const auto& wheel = car->wheels[i];
      if (!wheel) continue;
      const auto found = witnesses_.find(wheel->suspension.wheelBody);
      const auto* face = found != witnesses_.end() && found->second
                             ? found->second->groundFace()
                             : nullptr;
      if (!face || !face->valid()) continue;
      const auto& contact = wheel->suspension;
      road.add("%s{\"i\":%zu,\"leaf\":\"%p\",\"face\":%zu,"
               "\"p\":[%.5f,%.5f,%.5f],\"n\":[%.5f,%.5f,%.5f]}",
               firstContact ? "" : ",", i, static_cast<const void*>(face->leafOwner()),
               face->faceIndex(), contact.contactPoint[0], contact.contactPoint[1],
               contact.contactPoint[2], contact.normal[0], contact.normal[1], contact.normal[2]);
      firstContact = false;
    }
    road.add("]}");
    runlog::tracePush(contactChannel, road.text);
    if (!tags.empty()) {
      runlog::event(runlog::EventKind::Note,
                    "car anomaly tick %llu: %s (steps in cartrace.jsonl)",
                    static_cast<unsigned long long>(tick), tags.c_str());
      // Keep an immutable copy of the steps leading to the first anomalies and
      // refresh the live file (rate limited so a stuck anomaly stays cheap).
      static double lastFlush = -1e9;
      if (anomalyDumps_ < 6) {
        char name[48];
        std::snprintf(name, sizeof name, "cartrace-anomaly-%u.jsonl",
                      anomalyDumps_);
        ++anomalyDumps_;
        runlog::traceSave("cartrace", name);
      }
      if (wallNow - lastFlush >= 0.5) {
        lastFlush = wallNow;
        runlog::traceFlush();
      }
    }
  }
  return stepped;
}
