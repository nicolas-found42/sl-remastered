#pragma once
#include "natives/registry.hpp"

// FUN_00483e60, FUN_00483400 and FUN_004835e0 through the road layer:
// roadAlignToRoad (src/road_alignment.cpp) snaps a position to the nearest
// road spline, roadStartDirection and roadHaltTrafficPath
// (src/road_route.cpp) evaluate the crossing route between two positions.
NativeResult groundAlignToRoad(const std::vector<VmValue>&, const VmObject*, VmArena*,
                               const std::vector<const TufaClass*>*);
NativeResult groundGetStartDirection(const std::vector<VmValue>&, const VmObject*, VmArena*,
                                     const std::vector<const TufaClass*>*);
NativeResult groundHaltTrafficPath(const std::vector<VmValue>&, const VmObject*, VmArena*,
                                   const std::vector<const TufaClass*>*);
