#include "camera_device_factory.hpp"
#include "natives/game_type.hpp"
#include "pack_registry.hpp"
#include "resource_world.hpp"
#include "vm_test_harness.hpp"
#include "world_model.hpp"

#include <array>
#include <cstdio>
#include "car_tint.hpp"
#include <memory>
#include <string>

namespace {

const VmObject *makeObject(VmArena &arena) {
  auto object = std::make_unique<VmObject>();
  const VmObject *raw = object.get();
  arena.objects.push_back(std::move(object));
  return raw;
}

const VmObject *bindReceiver(VmArena &arena, VmGameState &game,
                             WorldNode &node) {
  const VmObject *raw = makeObject(arena);
  arena.resourceStates.push_back(VmResourceState{raw, {}, {}});
  const auto handle = static_cast<int32_t>(arena.resourceStates.size());
  raw->fields.emplace_back("ptr", VmValue{handle}, "I");
  auto &binding = game.bindings[handle];
  binding = std::make_unique<ReceiverBinding>();
  setLinkOwner(binding->link, &node);
  node.scriptReceiver = raw;
  return raw;
}

std::string errorOf(const NativeResult &result) {
  const auto *text = std::get_if<std::string>(&result);
  return text ? *text : std::string{};
}

NativeResult activate(VmArena &arena, const VmObject *receiver,
                      const std::string &text) {
  const VmObject *command = makeObject(arena);
  arena.strings.emplace_back(text, command);
  const VmObject *sender = nullptr;
  return gameRefQueueEvent(
      {VmValue{sender}, VmValue{int32_t{0x10}}, VmValue{command}}, receiver,
      &arena, nullptr);
}

} // namespace

int main() {
  PackRegistry packs("ref/raw");
  GameWorld world;
  auto *root = world.addRoot();
  ResourceWorld resources(world, packs);
  VmGameState game{&packs, &world, {}, {}};
  game.resources = &resources;
  VmArena arena;
  arena.gameState = &game;
  CHECK(root);
  if (!root)
    return 1;

  auto *player = world.addChild(*root, "activate player", 0x21);
  auto *camera = world.addChild(*root, "activate camera", 0x21);
  auto *render = world.addTypedChild(*root, 3, "activate render", 0);
  auto *car = world.addChild(*root, "activate car", 0x21);
  CHECK(player && camera && render && car);
  if (!player || !camera || !render || !car)
    return 1;
  world.markLoaded(*player);
  world.markLoaded(*car);
  player->nativeDevice = "player";
  player->player = std::make_unique<PlayerInstance>();
  const VmObject *playerScript = makeObject(arena);
  player->scriptReceiver = playerScript;
  car->nativeDevice = "car";
  car->eventMask = 0x10;
  car->carInstance = std::make_unique<CarRuntimeInstance>();
  car->carInstance->isCar = true;
  auto *carRoot = world.addTypedChild(*car, 2, "car_root", 0);
  CHECK(carRoot);
  if (!carRoot)
    return 1;
  world.markLoaded(*carRoot);
  setLinkOwner(car->carInstance->physicsRoot, carRoot);
  car->carInstance->seats.push_back(std::make_unique<CarSeat>());
  CarSeat *seat = car->carInstance->seats.front().get();
  setLinkOwner(seat->player, player);
  auto *script = world.addScript(0x151, "activate.Car");
  CHECK(script);
  if (script) {
    script->car = std::make_unique<CarPrototype>();
    CarDriver driver;
    driver.hasExtCamera = true;
    driver.extCamera = {10.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f,
                        6.0f,  7.0f, 8.0f, 9.0f, 11.0f};
    script->car->config.drivers.push_back(driver);
    script->car->cameraSettings = CarCameraSettings{0.5f, 2.0f};
    // A render body carries the colorscheme palette. Shipped palettes are
    // 0x00RRGGBB (cf. Taxi's "colors 1 0x00f3be00"); FUN_00475ec0 selects
    // colours[scheme % count] and FUN_0048c8c0 stores it on the render
    // resource, which the port keeps as CarRuntimeInstance::bodyTint.
    // Two render bodies with different palettes: stock walks every record.
    for (const auto &entry :
         {std::pair<uint32_t, std::vector<uint32_t>>{
              0x151, {0x00f3be00, 0x00ff0000}},
          std::pair<uint32_t, std::vector<uint32_t>>{
              0x152, {0x000000ff, 0x0000ff00, 0x00123456}}}) {
      CarBodyRecord renderBody;
      renderBody.kind = CarBodyRecord::Render;
      renderBody.renderResource = entry.first;
      for (const auto c : entry.second)
        renderBody.colors.push_back(c);
      script->car->config.bodies.push_back(renderBody);
    }
    setLinkOwner(car->carInstance->prototypeSource, script);
  }
  camera->nativeDevice = "camera";
  camera->cameraDevice = std::make_unique<CameraDeviceResources>(world);
  const VmObject *carRef = bindReceiver(arena, game, *car);

  int32_t carActivateGetInfo = -1;
  const VmObject *carActivateReceiver = nullptr;
  arena.callScript = [&](const VmObject *receiver, const std::string &name,
                         std::vector<VmValue> arguments) {
    if (name == "getInfo" && !arguments.empty()) {
      if (const auto *kind = std::get_if<int32_t>(&arguments[0]))
        carActivateGetInfo = *kind;
      carActivateReceiver = receiver;
    }
    VmResult result;
    result.ok = true;
    result.voidReturn = true;
    return result;
  };

  char command[96];
  std::snprintf(command, sizeof command, "activate 0x%x 0x%x 0x%x",
                static_cast<unsigned>(player->instanceId),
                static_cast<unsigned>(camera->instanceId),
                static_cast<unsigned>(render->instanceId));
  // FUN_00475ec0 colorscheme: every body gets colours[scheme % count] of its
  // OWN palette, stored per render resource.
  {
    const auto tint = [&](uint32_t res) -> int64_t {
      const auto &m = car->carInstance->bodyTints;
      const auto f = m.find(res);
      return f == m.end() ? -1 : int64_t(f->second);
    };
    CHECK(std::holds_alternative<std::monostate>(
        activate(arena, carRef, "colorscheme 0")));
    CHECK(tint(0x151) == 0x00f3be00 && tint(0x152) == 0x000000ff);
    CHECK(std::holds_alternative<std::monostate>(
        activate(arena, carRef, "colorscheme 4")));
    // 4 % 2 == 0 for the first body, 4 % 3 == 1 for the second.
    CHECK(tint(0x151) == 0x00f3be00 && tint(0x152) == 0x0000ff00);
    CHECK(std::holds_alternative<std::monostate>(
        activate(arena, carRef, "colorscheme 5")));
    // 5 % 2 == 1, 5 % 3 == 2.
    CHECK(tint(0x151) == 0x00ff0000 && tint(0x152) == 0x00123456);
    CHECK(car->carInstance->colorIndex == 5);
    // Draw-side: the same lookup+multiply the raster pass uses must give the
    // two parts different diffuse colours, each from its own record.
    {
      std::array<float, 4> d0{{1, 1, 1, 1}}, d1{{1, 1, 1, 1}};
      const auto t0 = lookupBodyTint(car->carInstance->bodyTints, 0x151);
      const auto t1 = lookupBodyTint(car->carInstance->bodyTints, 0x152);
      CHECK(t0 && t1);
      applyBodyTint(d0, *t0);
      applyBodyTint(d1, *t1);
      CHECK(d0[0] == 1.0f && d0[1] == 0.0f && d0[2] == 0.0f);
      CHECK(d1[0] == float(0x12) / 255.0f && d0[3] == 1.0f && d1[3] == 1.0f);
      CHECK(!lookupBodyTint(car->carInstance->bodyTints, 0x153));
    }
    car->carInstance->bodyTints.clear();
  }
  const auto linked = activate(arena, carRef, command);
  const auto linkedError = errorOf(linked);
  const bool carActivateLinks = seat->player.owner == player &&
                                seat->camera.owner == camera &&
                                seat->renderInstance.owner == render;
  CHECK(carActivateLinks);
  CHECK(carActivateGetInfo == 0x1a && carActivateReceiver == playerScript);
  CHECK(car->carInstance->seatCameraMode == 0);
  CHECK(car->carInstance->seatCameraSeat == 0);
  CHECK(car->carInstance->seatCameraMarker == 0);
  const auto pose = car->carInstance->seatCameraPose;
  const std::array<float, 3> lookOffset{4.0f, 5.0f, 6.0f};
  const std::array<float, 3> moveOffset{1.0f, 2.0f, 3.0f};
  const std::array<float, 3> moveRotation{7.0f, 8.0f, 9.0f};
  const std::array<float, 3> lookDirection{0.0f, 0.0f, -1.0f};
  CHECK(pose.has_value());
  CHECK(pose && pose->target == carRoot->instanceId &&
        pose->lookValue == -1.0f);
  CHECK(pose && pose->lookOffset == lookOffset &&
        pose->moveOffset == moveOffset);
  CHECK(pose && pose->moveDistance == 5.5f &&
        pose->moveRotation == moveRotation);
  CHECK(pose && pose->hasZoom && pose->zoom == 20.0f);
  CHECK(camera->cameraDevice->lookTarget.owner == carRoot);
  CHECK(camera->cameraDevice->moveTarget.owner == carRoot);
  const auto &cameraState = camera->cameraDevice->commands;
  CHECK(cameraState.lookOffset == lookOffset &&
        cameraState.lookDirection == lookDirection);
  CHECK(cameraState.moveOffset == moveOffset &&
        cameraState.moveDistance == 5.5f);
  CHECK(cameraState.moveRotation == moveRotation &&
        cameraState.zoomTarget == 20.0f);
  CHECK(seat->osd.cachedValue == 0);
  CHECK(seat->player.cachedValue != 0);
  CHECK(linkedError.empty());
  CHECK(linkedError.find("car activate gauges unsupported") ==
        std::string::npos);
  CHECK(linkedError.find("driver skeleton unsupported") == std::string::npos);
  CHECK(linkedError.find("camera pose unsupported") == std::string::npos);
  CHECK(car->carInstance->activateInfo == 0);
  CHECK(std::holds_alternative<std::monostate>(linked));

  const auto again = errorOf(activate(arena, carRef, command));
  CHECK(again.empty());
  CHECK(again.find("car activate gauges unsupported") == std::string::npos);
  CHECK(again.find("driver skeleton unsupported") == std::string::npos);
  CHECK(again.find("camera pose unsupported") == std::string::npos);
  CHECK(car->carInstance->activateInfo == 0);
  CHECK(car->carInstance->seatCameraPose &&
        car->carInstance->seatCameraPose->lookOffset == lookOffset);
  CHECK(car->carInstance->seatCameraMode == 0);
  CHECK(seat->player.owner == player && seat->camera.owner == camera &&
        seat->renderInstance.owner == render);

  auto *splitCar = world.addChild(*root, "activate split camera seats", 0x21);
  auto *otherPlayer = world.addChild(*root, "other seat player", 0x21);
  CHECK(splitCar && otherPlayer);
  if (splitCar && otherPlayer) {
    world.markLoaded(*otherPlayer);
    world.markLoaded(*splitCar);
    otherPlayer->nativeDevice = "player";
    splitCar->nativeDevice = "car";
    splitCar->eventMask = 0x10;
    splitCar->carInstance = std::make_unique<CarRuntimeInstance>();
    auto &split = *splitCar->carInstance;
    split.isCar = true;
    split.seats.push_back(std::make_unique<CarSeat>());
    split.seats.push_back(std::make_unique<CarSeat>());
    auto *physicsRoot = world.addTypedChild(*splitCar, 2, "car_root", 0);
    CHECK(physicsRoot);
    if (!physicsRoot)
      return 1;
    world.markLoaded(*physicsRoot);
    setLinkOwner(split.physicsRoot, physicsRoot);
    split.carPhysics = std::make_shared<CarPhysics>();
    std::string bodyError;
    auto bodyProto =
        physics::createBodyProto(1, 5, 1, 1, 1, nullptr, bodyError);
    CHECK(bodyProto.has_value());
    if (!bodyProto)
      return 1;
    split.carPhysics->rootPrototype = std::move(*bodyProto);
    split.carPhysics->root =
        std::make_unique<physics::Body>(physics::createBody(
            split.carPhysics->rootPrototype, {0, 0, 0}, nullptr, 0));
    setLinkOwner(split.prototypeSource, script);
    setLinkOwner(split.seats[0]->player, otherPlayer);
    setLinkOwner(split.seats[1]->player, player);
    const VmObject *splitRef = bindReceiver(arena, game, *splitCar);
    const auto splitResult = activate(arena, splitRef, command);
    const auto splitError = errorOf(splitResult);
    CHECK(split.seatCameraMode == 1);
    CHECK(split.seatCameraSeat == 1);
    CHECK(split.seatCameraMarker == -1);
    CHECK(split.seats[1]->camera.owner == camera);
    CHECK(splitError.empty());
    CHECK(std::holds_alternative<std::monostate>(splitResult));
    CHECK(split.seatCameraPose &&
          split.seatCameraPose->target == physicsRoot->instanceId);
    CHECK(split.seatCameraPose && split.seatCameraPose->lookOffset ==
                                      (std::array<float, 3>{0, 0, -20}));
    CHECK(split.seatCameraPose && split.seatCameraPose->moveDistance == 0.01f);
    const float radius = split.carPhysics->rootPrototype.shape.boundRadius;
    CHECK(
        split.seatCameraPose &&
        split.seatCameraPose->moveRotation ==
            (std::array<float, 3>{0, -(radius * 0.1f + 1), -(radius + 0.5f)}));
    CHECK(split.seatCameraPose && split.seatCameraPose->hasZoom &&
          split.seatCameraPose->zoom == 100);
    CHECK(camera->cameraDevice->lookTarget.owner == physicsRoot);
    CHECK(camera->cameraDevice->moveTarget.owner == physicsRoot);
    const auto &chase = camera->cameraDevice->commands;
    CHECK(chase.lookOffset == (std::array<float, 3>{0, 0, -20}));
    CHECK(chase.lookDirection == (std::array<float, 3>{0, 0, -1}));
    CHECK(chase.moveOffset == (std::array<float, 3>{0, 0, 0}));
    CHECK(chase.moveRotation ==
          (std::array<float, 3>{0, -(radius * 0.1f + 1), -(radius + 0.5f)}));
    CHECK(chase.moveDistance == 0.01f && chase.zoomTarget == 100);

    // The activating seat need not be the seat selected for the chase view.
    // 00449714 selects the last seat containing both a player and a camera.
    char otherCommand[96];
    std::snprintf(otherCommand, sizeof otherCommand, "activate 0x%x 0 0",
                  static_cast<unsigned>(otherPlayer->instanceId));
    CHECK(std::holds_alternative<std::monostate>(
        activate(arena, splitRef, otherCommand)));
    CHECK(split.seatCameraMode == 1 && split.seatCameraSeat == 1);
    CHECK(camera->cameraDevice->lookTarget.owner == physicsRoot);
    CHECK(camera->cameraDevice->moveTarget.owner == physicsRoot);
    CHECK(camera->cameraDevice->commands.moveDistance == 0.01f);
  }

  auto *emptyCar = world.addChild(*root, "activate car without a seat", 0x21);
  CHECK(emptyCar);
  if (emptyCar) {
    world.markLoaded(*emptyCar);
    emptyCar->nativeDevice = "car";
    emptyCar->eventMask = 0x10;
    emptyCar->carInstance = std::make_unique<CarRuntimeInstance>();
    emptyCar->carInstance->isCar = true;
    const VmObject *emptyRef = bindReceiver(arena, game, *emptyCar);
    const auto missing = activate(arena, emptyRef, command);
    const auto missingError = errorOf(missing);
    CHECK(missingError.find("car activate seat unavailable") !=
          std::string::npos);
    CHECK(!std::holds_alternative<std::monostate>(missing));
  }

  auto *occupied = world.addChild(*root, "activate occupied render", 0x21);
  auto *oldRender = world.addTypedChild(*root, 3, "old driver render", 0);
  auto *newPlayer = world.addChild(*root, "new driver", 0x21);
  CHECK(occupied && oldRender && newPlayer);
  if (occupied && oldRender && newPlayer) {
    world.markLoaded(*newPlayer);
    world.markLoaded(*occupied);
    newPlayer->nativeDevice = "player";
    occupied->nativeDevice = "car";
    occupied->eventMask = 0x10;
    occupied->carInstance = std::make_unique<CarRuntimeInstance>();
    occupied->carInstance->isCar = true;
    occupied->carInstance->seats.push_back(std::make_unique<CarSeat>());
    CarSeat *busy = occupied->carInstance->seats.front().get();
    setLinkOwner(busy->renderInstance, oldRender);
    const VmObject *occupiedRef = bindReceiver(arena, game, *occupied);
    char replace[96];
    std::snprintf(replace, sizeof replace, "activate 0x%x 0x%x 0x%x",
                  static_cast<unsigned>(newPlayer->instanceId),
                  static_cast<unsigned>(camera->instanceId),
                  static_cast<unsigned>(render->instanceId));
    const auto teardown = errorOf(activate(arena, occupiedRef, replace));
    const bool carActivateTeardown = busy->player.owner == newPlayer &&
                                     busy->camera.owner == camera &&
                                     busy->renderInstance.owner == oldRender;
    CHECK(carActivateTeardown);
    CHECK(teardown.find("car activate driver pose teardown unsupported") !=
          std::string::npos);
  }

  auto *osdCar = world.addChild(*root, "activate osd gauges", 0x21);
  CHECK(osdCar);
  if (osdCar) {
    world.markLoaded(*osdCar);
    osdCar->nativeDevice = "car";
    osdCar->eventMask = 0x10;
    osdCar->carInstance = std::make_unique<CarRuntimeInstance>();
    osdCar->carInstance->isCar = true;
    osdCar->carInstance->seats.push_back(std::make_unique<CarSeat>());
    CarSeat *osdSeat = osdCar->carInstance->seats.front().get();
    osdSeat->osd.cachedValue = 1;
    const VmObject *osdRef = bindReceiver(arena, game, *osdCar);
    char osdCommand[96];
    std::snprintf(osdCommand, sizeof osdCommand, "activate 0x%x 0 0",
                  static_cast<unsigned>(player->instanceId));
    const auto osdResult = activate(arena, osdRef, osdCommand);
    const auto osdError = errorOf(osdResult);
    CHECK(osdSeat->osd.cachedValue != 0);
    CHECK(osdError.find("car activate gauges unsupported") !=
          std::string::npos);
    CHECK(osdError.find("car activate info 0x15 unsupported") ==
          std::string::npos);
    CHECK(!std::holds_alternative<std::monostate>(osdResult));
  }

  auto *idleCar = world.addChild(*root, "activate no player id", 0x21);
  CHECK(idleCar);
  if (idleCar) {
    world.markLoaded(*idleCar);
    idleCar->nativeDevice = "car";
    idleCar->eventMask = 0x10;
    idleCar->carInstance = std::make_unique<CarRuntimeInstance>();
    idleCar->carInstance->isCar = true;
    idleCar->carInstance->seats.push_back(std::make_unique<CarSeat>());
    CarSeat *idleSeat = idleCar->carInstance->seats.front().get();
    const VmObject *idleRef = bindReceiver(arena, game, *idleCar);
    const auto idleResult = activate(arena, idleRef, "activate 0 0 0");
    CHECK(idleSeat->osd.cachedValue == 0);
    CHECK(idleSeat->player.cachedValue == 0);
    CHECK(std::holds_alternative<std::monostate>(idleResult));
    CHECK(errorOf(idleResult).empty());
    CHECK(idleCar->carInstance->activateInfo == 0);
    CHECK(idleCar->carInstance->seatCameraMode == 0);
  }

  std::printf("%s\n", failures ? "car activate test failed"
                               : "car activate test passed");
  return failures ? 1 : 0;
}
