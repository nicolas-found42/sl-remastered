#pragma once
#include "natives/registry.hpp"

#include <array>

// FUN_00482d30 (Navigator.updateNavigator) placement maths, factored out so
// tests can drive the stock formulas without a live game world.
struct NavigatorClamp {
  float targetX;
  float targetZ;
  float offsetX;
  float offsetZ;
};

// FUN_00482d30 mode 0: clamp the camera target (player position + offsets)
// to the map rectangle built from the script's top/left/size fields and the
// camera bone height. Returns the clamped target and the offset field values
// stock writes back (new offset = old offset + bound - target).
NavigatorClamp navigatorClampTarget(float targetX, float targetZ,
                                    float offsetX, float offsetZ, float camY,
                                    float top, float left, float size);

// FUN_00482c40: project the marker point through the map camera
// (004fdb40 -> 00513720), scale normalized x/y into the stock limits
// (+/-0.9 x, +/-0.85 y) preserving the direction from the centre, and
// unproject (004fdb70 -> 00513840). In-out on point; false when the point
// cannot be projected or the unproject fails.
bool navigatorClampMarkerPoint(std::array<float, 3>& point,
                               const std::array<float, 16>& view,
                               const std::array<float, 16>& projection,
                               const std::array<float, 16>& cameraWorld);

NativeResult navigatorUpdate(const std::vector<VmValue>&, const VmObject*, VmArena*, const std::vector<const TufaClass*>*);
