#include "camera_traversal.hpp"
#include "resource_world.hpp"

#include <cmath>
#include <cstdio>

int main() {
  GameWorld world;
  auto *root = world.addRoot();
  if (!root)
    return 1;
  auto *render = world.addTypedChild(*root, 3, "render", 0);
  auto *game = world.addChild(*root, "game", 0);
  if (!render || !game)
    return 1;
  game->visibilityGain = 2;
  game->visibilityBias = 1;
  if (!updateCameraType1Visibility(*game, 1, 2) || game->field6c != 5.0f ||
      game->field70 != 5.0f || game->stamp7c != 1)
    return 1;
  if (!updateCameraType1Visibility(*game, 1, 1) || game->field6c != 5.0f ||
      game->field70 != 5.0f)
    return 1;
  game->loadPriority = 1;
  if (!updateCameraType1Visibility(*game, 2, 0) || game->loadPriority != 0 ||
      game->field6c != 1.0f || game->field70 != 1.0f)
    return 1;
  SceneCameraCandidates candidates;
  const CameraTraversalParams params{{0, 0, 0}, 1, 0,     1,  1,
                                     0,         0, false, {}, {}};
  const auto camera = makeCameraResource(1.0f, 60.0f, 1.0f, 100.0f, 0, 1, 0, 0);
  const std::array<float, 16> cameraWorld{1, 0, 0, 0, 0,  1,  0,  0,
                                          0, 0, 1, 0, 10, 20, 30, 1};
  const auto viewState = cameraViewState(camera, cameraWorld, 1.0f);
  const auto assembled =
      viewState
          ? cameraTraversalParams(camera, *viewState, cameraWorld, 1, 0, true)
          : std::nullopt;
  if (!assembled ||
      assembled->cameraPosition != std::array<float, 3>{1, 2, 3} ||
      !assembled->cameraBound || !assembled->frustumPlanes ||
      assembled->planeTest != 0 || assembled->frustumChecks != true)
    return 1;
  std::string error;
  auto unknownFrustum = params;
  unknownFrustum.frustumChecks.reset();
  if (traverseCameraCandidates(world, *render, candidates, unknownFrustum,
                               error) ||
      error != "camera traversal: plane-test inputs unavailable")
    return 1;
  if (!traverseCameraCandidates(world, *render, candidates, params, error)) {
    std::printf("FAIL: %s\n", error.c_str());
    return 1;
  }
  int count = 0;
  candidates.drain(0, true, [&](LinkOwner &owner) {
    if (&owner == render)
      ++count;
    return 1u;
  });
  if (count != 1 || render->field6c != 1.0f || render->field70 != 1.0f)
    return 1;
  // FUN_00510fc0: a non-zero plane-test mode is stored by Camera.create and
  // never rejected. It forces frustum checks on even when the caller passes
  // false. The root camera instance bounds reset happens in
  // CameraFrame::draw on the camera instance, not on the walk root.
  CameraTraversalParams planeParams = params;
  planeParams.planeTest = 2;
  planeParams.frustumChecks = false;
  planeParams.frustumPlanes = std::array<std::array<float, 4>, 5>{};
  CameraType1Records planeRecords;
  SceneCameraCandidates planeCandidates;
  if (!traverseCameraCandidates(world, *render, planeCandidates, planeParams,
                                error, &planeRecords)) {
    std::printf("FAIL plane-test: %s\n", error.c_str());
    return 1;
  }
  int planeCount = 0;
  planeCandidates.drain(0, true, [&](LinkOwner &owner) {
    if (&owner == render)
      ++planeCount;
    return 1u;
  });
  if (planeCount != 1)
    return 1;
  // Type-3 nodes use relation 2 regardless of their bounds (00511180).
  render->bounds.mode = 0;
  render->bounds.values = {100, 0, 0, 1, 0, 0};
  auto outsideParams = params;
  ResourceBounds smallCamera{};
  smallCamera.mode = 0;
  smallCamera.values = {0, 0, 0, 1, 0, 0};
  outsideParams.cameraBound = smallCamera;
  SceneCameraCandidates outsideCandidates;
  if (!traverseCameraCandidates(world, *render, outsideCandidates,
                                outsideParams, error))
    return 1;
  int outsideCount = 0;
  outsideCandidates.drain(0, true, [&](LinkOwner &) {
    ++outsideCount;
    return 1u;
  });
  if (outsideCount != 1)
    return 1;
  render->bounds.mode = 2;
  error.clear();
  if (traverseCameraCandidates(world, *game, candidates, params, error) ||
      error != "camera traversal: type-1 game: records unavailable")
    return 1;
  if (!traverseCameraCandidates(world, *render, candidates, params, error) ||
      !error.empty())
    return 1;
  auto *child = world.addTypedChild(*game, 3, "child", 0);
  if (!child)
    return 1;
  world.markLoaded(*game);
  CameraType1Records records;
  SceneCameraCandidates subtreeCandidates;
  if (!traverseCameraCandidates(world, *game, subtreeCandidates, params, error,
                                &records)) {
    std::printf("FAIL subtree: %s\n", error.c_str());
    return 1;
  }
  int subtreeCount = 0;
  subtreeCandidates.drain(0, true, [&](LinkOwner &owner) {
    if (&owner == child)
      ++subtreeCount;
    return 1u;
  });
  if (subtreeCount != 1)
    return 1;
  if (!traverseCameraCandidates(world, *game, subtreeCandidates, params, error,
                                &records))
    return 1;
  CameraTraversalParams partialParams = params;
  partialParams.frustumChecks = true;
  partialParams.frustumPlanes = std::array<std::array<float, 4>, 5>{};
  CameraType1Records partialRecords;
  SceneCameraCandidates partialCandidates;
  if (!traverseCameraCandidates(world, *game, partialCandidates, partialParams,
                                error, &partialRecords))
    return 1;
  int partialCount = 0;
  partialCandidates.drain(0, true, [&](LinkOwner &owner) {
    if (&owner == child)
      ++partialCount;
    return 1u;
  });
  if (partialCount != 1)
    return 1;
  auto *partialChild = world.addTypedChild(*game, 3, "partial late", 0);
  if (!partialChild ||
      !traverseCameraCandidates(world, *game, partialCandidates, partialParams,
                                error, &partialRecords))
    return 1;
  partialCandidates.drain(0, true, [&](LinkOwner &owner) {
    if (&owner == partialChild)
      ++partialCount;
    return 1u;
  });
  if (partialCount != 2)
    return 1;
  game->flags |= 0x100u;
  if (traverseCameraCandidates(world, *game, partialCandidates, partialParams,
                               error, &partialRecords) ||
      error.find("node flags unsupported") == std::string::npos)
    return 1;
  game->flags &= ~0x100u;
  auto *hidden = world.addChild(*root, "hidden", 0x40u);
  auto *hiddenRender =
      hidden ? world.addTypedChild(*hidden, 3, "hidden render", 0) : nullptr;
  auto *hiddenPhysics =
      hidden ? world.addTypedChild(*hidden, 2, "hidden physics", 0) : nullptr;
  if (!hidden || !hiddenRender || !hiddenPhysics)
    return 1;
  world.markLoaded(*hidden);
  world.markLoaded(*hiddenRender);
  world.markLoaded(*hiddenPhysics);
  CameraType1Records hiddenRecords;
  SceneCameraCandidates hiddenCandidates;
  if (!traverseCameraCandidates(world, *hidden, hiddenCandidates, partialParams,
                                error, &hiddenRecords)) {
    std::printf("FAIL hidden: %s\n", error.c_str());
    return 1;
  }
  if (hidden->loadPriority != 0 || hiddenRender->field6c != 1 ||
      hiddenRender->field70 != 0 || hiddenPhysics->field6c != 1 ||
      hiddenPhysics->field70 != 0)
    return 1;
  int hiddenCount = 0;
  hiddenCandidates.drain(0, true, [&](LinkOwner &) {
    ++hiddenCount;
    return 0u;
  });
  if (hiddenCount != 0)
    return 1;
  hidden->flags &= ~0x40u;
  if (!traverseCameraCandidates(world, *hidden, hiddenCandidates, params, error,
                                &hiddenRecords))
    return 1;
  hidden->flags |= 0x40u;
  // Hiding a node that has this camera's record destroys the record
  // (FUN_00511e60) instead of failing the traversal.
  // The cleanup walk never touches the camera's candidate records: the hidden
  // render child keeps its record.
  if (!hiddenCandidates.hasRecord(*hiddenRender))
    return 1;
  if (!hiddenRecords.hasRecord(*hidden))
    return 1;
  if (!traverseCameraCandidates(world, *hidden, hiddenCandidates, params, error,
                                &hiddenRecords) ||
      hiddenRecords.hasRecord(*hidden)) {
    std::printf("FAIL hidden cleanup: %s\n", error.c_str());
    return 1;
  }
  // An outside subtree without a record still runs the same cleanup.
  hidden->flags &= ~0x40u;
  hidden->bounds.mode = 1;
  hidden->bounds.values = {100, 0, 0, 1, 1, 1};
  if (!traverseCameraCandidates(world, *hidden, hiddenCandidates, outsideParams,
                                error, &hiddenRecords) ||
      hiddenRecords.hasRecord(*hidden)) {
    std::printf("FAIL outside cleanup: %s\n", error.c_str());
    return 1;
  }
  auto *cached = world.addChild(*root, "cached", 0);
  if (!cached)
    return 1;
  world.markLoaded(*cached);
  CameraType1Records cacheRecords;
  SceneCameraCandidates cacheCandidates;
  if (!traverseCameraCandidates(world, *cached, cacheCandidates, params, error,
                                &cacheRecords))
    return 1;
  auto *lateChild = world.addTypedChild(*cached, 3, "late child", 0);
  if (!lateChild || !traverseCameraCandidates(world, *cached, cacheCandidates,
                                              params, error, &cacheRecords))
    return 1;
  int cacheCount = 0;
  cacheCandidates.drain(0, true, [&](LinkOwner &) {
    ++cacheCount;
    return 1u;
  });
  if (cacheCount != 0)
    return 1;
  cached->flags |= 0x40000u;
  if (!traverseCameraCandidates(world, *cached, cacheCandidates, params, error,
                                &cacheRecords))
    return 1;
  cacheCandidates.drain(0, true, [&](LinkOwner &) {
    ++cacheCount;
    return 1u;
  });
  if (cacheCount != 1 || (cached->flags & 0x40000u))
    return 1;
  auto *incomplete = world.addChild(*root, "incomplete", 0);
  if (!incomplete || !world.addTypedChild(*incomplete, 5, "unsupported", 0))
    return 1;
  world.markLoaded(*incomplete);
  CameraType1Records incompleteRecords;
  SceneCameraCandidates incompleteCandidates;
  for (int i = 0; i < 2; ++i) {
    if (traverseCameraCandidates(world, *incomplete, incompleteCandidates,
                                 params, error, &incompleteRecords) ||
        error != "camera traversal: node type unsupported")
      return 1;
  }
  {
    PackRegistry packs("ref/raw");
    uint16_t slot = 0xffff;
    if (!packs.registerPackName("system.rpk", slot, error) || slot != 0)
      return 1;
    ResourceWorld resources(world, packs);
    const auto found = resources.lookup(4, 1);
    if (!found.node.packNode)
      return 1;
    auto &mounted = *found.node.packNode;
    CameraType1Records unloadedRecords;
    SceneCameraCandidates unloadedCandidates;
    const auto unloadedState = mounted.stateWord;
    if (mounted.loaded() ||
        traverseCameraCandidates(world, mounted, unloadedCandidates, params,
                                 error, &unloadedRecords, &resources) ||
        error != "camera traversal: mounted type-1 camera bound unavailable" ||
        mounted.stateWord != unloadedState || mounted.loaded())
      return 1;
    if (!mounted.bounds || mounted.bounds->mode != 1)
      return 1;
    const auto &values = mounted.bounds->values;
    ResourceBounds cameraBound;
    cameraBound.mode = 0;
    cameraBound.values = {values[0],
                          values[1],
                          values[2],
                          std::sqrt(values[3] * values[3] +
                                    values[4] * values[4] +
                                    values[5] * values[5]) +
                              1.0f,
                          0,
                          0};
    auto mountedParams = params;
    mountedParams.cameraBound = cameraBound;
    auto mountedOutside = mountedParams;
    mountedOutside.cameraBound->values[0] =
        values[0] + 4 * cameraBound.values[3] + 100;
    mountedOutside.cameraBound->values[3] = 1;
    CameraType1Records outsideMountedRecords;
    SceneCameraCandidates outsideMountedCandidates;
    if (!traverseCameraCandidates(world, mounted, outsideMountedCandidates,
                                  mountedOutside, error, &outsideMountedRecords,
                                  &resources) ||
        mounted.loaded() || outsideMountedRecords.hasRecord(mounted))
      return 1;
    if (!traverseCameraCandidates(world, mounted, unloadedCandidates,
                                  mountedParams, error, &unloadedRecords,
                                  &resources) ||
        !mounted.loaded())
      return 1;
    if (!traverseCameraCandidates(world, mounted, outsideMountedCandidates,
                                  mountedOutside, error, &outsideMountedRecords,
                                  &resources) ||
        outsideMountedRecords.hasRecord(mounted))
      return 1;
    // FUN_00511e60 destroys the camera's existing record for the outside node.
    if (!unloadedRecords.hasRecord(mounted))
      return 1;
    if (!traverseCameraCandidates(world, mounted, unloadedCandidates,
                                  mountedOutside, error, &unloadedRecords,
                                  &resources) ||
        unloadedRecords.hasRecord(mounted)) {
      std::printf("FAIL mounted cleanup: %s\n", error.c_str());
      return 1;
    }
    // Outside polygon-bearing nodes still run cleanup. A fabricated count
    // without a spatial payload cannot be submitted as a ground candidate.
    mounted.polygonCount = 1;
    if (!traverseCameraCandidates(world, mounted, outsideMountedCandidates,
                                  mountedOutside, error, &outsideMountedRecords,
                                  &resources) ||
        traverseCameraCandidates(world, mounted, outsideMountedCandidates,
                                 mountedParams, error, &outsideMountedRecords,
                                 &resources) ||
        error !=
            "camera traversal: mounted type-1 ground candidate unsupported")
      return 1;
    mounted.polygonCount = 0;
    CameraType1Records mountedRecords;
    SceneCameraCandidates mountedCandidates;
    auto *mountedChild = resources.addGameChild(mounted, "mounted child", 0);
    if (!mountedChild)
      return 1;
    world.markLoaded(*mountedChild);
    auto *mountedRender = world.addTypedChild(*mountedChild, 3, "render", 0);
    if (!mountedRender ||
        !traverseCameraCandidates(world, mounted, outsideMountedCandidates,
                                  mountedOutside, error, &outsideMountedRecords,
                                  &resources) ||
        mountedRender->field70 != 0 || mountedRender->field6c != 1)
      return 1;
    if (!mountedRender || !traverseCameraCandidates(
                              world, mounted, mountedCandidates, mountedParams,
                              error, &mountedRecords, &resources)) {
      std::printf("FAIL mounted: %s\n", error.c_str());
      return 1;
    }
    int mountedCount = 0;
    mountedCandidates.drain(0, true, [&](LinkOwner &owner) {
      if (&owner == mountedRender)
        ++mountedCount;
      return 1u;
    });
    if (mountedCount != 1)
      return 1;
    mountedParams.frustumChecks = true;
    const std::array<float, 4> plane{1, 0, 0, values[0] + values[3] + 1};
    mountedParams.frustumPlanes =
        std::array<std::array<float, 4>, 5>{plane, plane, plane, plane, plane};
    if (!traverseCameraCandidates(world, mounted, mountedCandidates,
                                  mountedParams, error, &mountedRecords,
                                  &resources))
      return 1;
    mountedCandidates.drain(0, true, [&](LinkOwner &owner) {
      if (&owner == mountedRender)
        ++mountedCount;
      return 1u;
    });
    if (mountedCount != 2)
      return 1;
  }
  std::puts("camera_traversal_test: ok");
  return 0;
}
