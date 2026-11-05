#include "natives/navigator.hpp"
#include "owner_links.hpp"
#include "scene_2d.hpp"
#include "world_model.hpp"

#include <cmath>
#include <cstdio>

namespace {
int failures = 0;
#define CHECK(condition)                                       \
  do {                                                         \
    if (!(condition)) {                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #condition); \
      ++failures;                                              \
    }                                                          \
  } while (0)

bool near(float a, float b, float epsilon = 1e-3f) {
  return std::fabs(a - b) <= epsilon;
}
} // namespace

int main() {
  // FUN_00482d30 mode 0 map rectangle: top/left scale by DAT_005f09c8 = 100,
  // the extent by DAT_005f0da0 = 8, the camera height by DAT_005f0cdc = 5
  // (x bounds times DAT_005f0c78 = 1.3333334).
  {
    // Inside the rectangle: target and offsets unchanged.
    const auto inside =
        navigatorClampTarget(500.0f, 600.0f, 10.0f, 20.0f, 0.0f, 1.0f, 2.0f,
                             3.0f);
    CHECK(near(inside.targetX, 500.0f) && near(inside.targetZ, 600.0f));
    CHECK(near(inside.offsetX, 10.0f) && near(inside.offsetZ, 20.0f));
    // Below zMin: offset grows by the gap and the target sits on the bound.
    const auto low = navigatorClampTarget(500.0f, 50.0f, 10.0f, 20.0f, 0.0f,
                                          1.0f, 2.0f, 3.0f);
    CHECK(near(low.targetZ, 100.0f) && near(low.offsetZ, 70.0f));
    CHECK(near(low.targetX, 500.0f) && near(low.offsetX, 10.0f));
    // Above zMax (top*100 + size*800).
    const auto high = navigatorClampTarget(500.0f, 3000.0f, 10.0f, 20.0f, 0.0f,
                                           1.0f, 2.0f, 3.0f);
    CHECK(near(high.targetZ, 2500.0f) && near(high.offsetZ, -480.0f));
    // Above xMax (left*100 + size*800).
    const auto farX = navigatorClampTarget(2700.0f, 600.0f, 10.0f, 20.0f, 0.0f,
                                           1.0f, 2.0f, 3.0f);
    CHECK(near(farX.targetX, 2600.0f) && near(farX.offsetX, -90.0f));
    // Camera height moves the bounds: z by camY*5, x by camY*5*4/3.
    const auto raised = navigatorClampTarget(100.0f, 100.0f, 0.0f, 0.0f, 10.0f,
                                             1.0f, 2.0f, 3.0f);
    CHECK(near(raised.targetZ, 150.0f) && near(raised.offsetZ, 50.0f));
    CHECK(near(raised.targetX, 266.66667f));
    CHECK(near(raised.offsetX, 166.66667f));
  }

  // FUN_00482d30 mode 0 camera bone: translation patched in place, rotation
  // and every other element kept.
  {
    LinkOwner type;
    SceneRenderInstance instance(&type);
    const float position[3]{0.0f, 0.0f, 0.0f};
    const float ypr[3]{0.5f, 0.25f, -0.75f};
    instance.setBoneMatrix(position, ypr);
    const auto rotated = instance.boneMatrix();
    instance.setBoneTranslationKeepRotation(12.5f, rotated[13], -7.25f);
    const auto& patched = instance.boneMatrix();
    bool rotationKept = true;
    for (size_t index = 0; index < 12; ++index)
      rotationKept = rotationKept && patched[index] == rotated[index];
    CHECK(rotationKept);
    CHECK(near(patched[12], 1.25f));
    CHECK(patched[13] == rotated[13]);
    CHECK(near(patched[14], -0.725f));
    CHECK(instance.rootBoneUpdatePending());
    instance.processRootBoneUpdate();
    CHECK(!instance.rootBoneUpdatePending());
    bool derivedMatches = true;
    for (size_t index = 0; index < 16; ++index)
      derivedMatches =
          derivedMatches && instance.derivedRootBoneMatrix()[index] ==
                                instance.boneMatrix()[index];
    CHECK(derivedMatches);
  }

  // FUN_00482d30 mode 1 camera bone: euler (yaw, -1.5, 0) basis with the
  // translation offset by basis[2]*4.5 (x), basis[6]*4.5 (y) and
  // basis[10]*4.5 (z), scaled by 10 into the bone matrix.
  {
    LinkOwner type;
    SceneRenderInstance instance(&type);
    instance.setNavigatorCameraBone(100.0f, 200.0f, 0.0f);
    const auto& matrix = instance.boneMatrix();
    const float s1 = static_cast<float>(std::sin(-1.5));
    const float c1 = static_cast<float>(std::cos(-1.5));
    // basis[2] = cos(pitch) sin(yaw), basis[6] = -sin(pitch),
    // basis[10] = cos(pitch) cos(yaw).
    const float positionX = 100.0f * 0.01f;
    const float positionY = -s1 * 4.5f;
    const float positionZ = 200.0f * 0.01f + c1 * 4.5f;
    CHECK(near(matrix[12], positionX * 10.0f));
    CHECK(near(matrix[13], positionY * 10.0f));
    CHECK(near(matrix[14], positionZ * 10.0f));
    // Basis elements of the (0, -1.5, 0) rotation.
    CHECK(near(matrix[0], 1.0f)); // cos(yaw)
    CHECK(near(matrix[5], c1));   // cos(pitch) cos(roll)
    CHECK(near(matrix[6], s1));
    CHECK(near(matrix[9], -s1));
    CHECK(near(matrix[10], c1));
    CHECK(matrix[15] == 1.0f);
    CHECK(instance.rootBoneUpdatePending());
  }

  // Mode 1 retains the followed yaw; basis column 2 sets all three offsets.
  {
    LinkOwner type;
    SceneRenderInstance instance(&type);
    const float yaw = 0.7f;
    instance.setNavigatorCameraBone(-278.5f, 1033.0f, yaw);
    const auto& matrix = instance.boneMatrix();
    const float cp = std::cos(-1.5f);
    CHECK(near(matrix[12], (-2.785f + cp * std::sin(yaw) * 4.5f) * 10));
    CHECK(near(matrix[13], -std::sin(-1.5f) * 45));
    CHECK(near(matrix[14], (10.33f + cp * std::cos(yaw) * 4.5f) * 10));
    CHECK(near(matrix[0], std::cos(yaw)));
  }

  // setBoneMatrix stays the pure yaw rotation after the eulerBasis
  // refactor: column-major basis of FUN_0054ecd0 + FUN_0054f4c0.
  {
    LinkOwner type;
    SceneRenderInstance instance(&type);
    const float position[3]{1.0f, 2.0f, 3.0f};
    const float yaw = 0.7f;
    const float ypr[3]{yaw, 0.0f, 0.0f};
    instance.setBoneMatrix(position, ypr);
    const auto& matrix = instance.boneMatrix();
    const float c = static_cast<float>(std::cos(yaw));
    const float s = static_cast<float>(std::sin(yaw));
    CHECK(near(matrix[0], c));
    CHECK(near(matrix[2], -s));
    CHECK(near(matrix[5], 1.0f));
    CHECK(near(matrix[8], s));
    CHECK(near(matrix[10], c));
    CHECK(matrix[12] == 10.0f && matrix[13] == 20.0f && matrix[14] == 30.0f);
  }

  // FUN_00482c40 marker clamp with an identity frame: x scales into
  // +/-0.9 first, then y into +/-0.85, both preserving the direction.
  {
    const std::array<float, 16> identity{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    {
      std::array<float, 3> point{0.5f, 0.5f, -5.0f};
      CHECK(navigatorClampMarkerPoint(point, identity, identity, identity));
      CHECK(point[0] == 0.5f && point[1] == 0.5f && point[2] == -5.0f);
    }
    {
      std::array<float, 3> point{2.0f, 0.5f, -5.0f};
      CHECK(navigatorClampMarkerPoint(point, identity, identity, identity));
      CHECK(near(point[0], 0.9f));
      CHECK(near(point[1], 0.225f)); // 0.5 * 0.9/2
      CHECK(near(point[2], -5.0f));
    }
    {
      std::array<float, 3> point{0.5f, 2.0f, -5.0f};
      CHECK(navigatorClampMarkerPoint(point, identity, identity, identity));
      CHECK(near(point[0], 0.2125f)); // 0.5 * 0.85/2
      CHECK(near(point[1], 0.85f));
    }
    {
      std::array<float, 3> point{2.0f, 2.0f, -5.0f};
      CHECK(navigatorClampMarkerPoint(point, identity, identity, identity));
      // x clamps first, then the surviving y drives a second scale.
      CHECK(near(point[0], 0.85f));
      CHECK(near(point[1], 0.85f));
    }
    {
      std::array<float, 3> point{-2.0f, -2.0f, -5.0f};
      CHECK(navigatorClampMarkerPoint(point, identity, identity, identity));
      CHECK(near(point[0], -0.85f));
      CHECK(near(point[1], -0.85f));
    }
  }

  // Same clamp through the real cameraViewState projection (fieldOfView is
  // degrees, as in the stock camera payload): an interior marker round-trips
  // unchanged, a boundary marker lands on the stock limit.
  {
    CameraResource camera;
    camera.viewportAspect = 4.0f / 3.0f;
    camera.fieldOfView = 60.0f;
    camera.nearDistance = 2.0f;
    camera.farDistance = 2000.0f;
    const std::array<float, 16> world{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -50, 1}};
    const auto state = cameraViewState(camera, world, 4.0f / 3.0f);
    CHECK(state.has_value());
    if (state) {
      std::array<float, 3> interior{5.0f, 0.031f, -60.0f};
      const auto original = interior;
      const auto originalProjection = projectCameraPoint(
          original, state->view, state->projection);
      CHECK(originalProjection.has_value());
      if (originalProjection)
        CHECK(std::fabs((*originalProjection)[0]) < 0.9f &&
              std::fabs((*originalProjection)[1]) < 0.85f);
      CHECK(navigatorClampMarkerPoint(interior, state->view, state->projection,
                                      world));
      const auto reprojection = projectCameraPoint(interior, state->view,
                                                   state->projection);
      CHECK(reprojection.has_value());
      if (reprojection && originalProjection) {
        for (size_t axis = 0; axis < 3; ++axis)
          CHECK(near((*reprojection)[axis], (*originalProjection)[axis],
                     1e-5f));
      }
      std::array<float, 3> boundary{400.0f, 0.031f, -60.0f};
      CHECK(navigatorClampMarkerPoint(boundary, state->view, state->projection,
                                      world));
      const auto boundaryProjection = projectCameraPoint(
          boundary, state->view, state->projection);
      CHECK(boundaryProjection.has_value());
      if (boundaryProjection) {
        CHECK(near(std::fabs((*boundaryProjection)[0]), 0.9f, 1e-4f));
        CHECK((*boundaryProjection)[0] > 0.0f);
      }
    }
  }

  if (failures) {
    std::printf("Navigator placement tests FAILED: %d\n", failures);
    return 1;
  }
  std::puts("Navigator placement tests passed");
  return 0;
}
