#pragma once
#include "natives/registry.hpp"

// FUN_00483e60, FUN_00483400 and FUN_004835e0 through the road manager.
// The spline query, route solver and traffic propagation the stock handlers
// call (FUN_00583a00, FUN_00583840, FUN_005826f0, FUN_00583fd0) are not ported:
// no implementation of that layer exists in the tree, so each handler reports
// its named gap and leaves the road state untouched.
NativeResult groundAlignToRoad(const std::vector<VmValue>&, const VmObject*, VmArena*,
                               const std::vector<const TufaClass*>*);
NativeResult groundGetStartDirection(const std::vector<VmValue>&, const VmObject*, VmArena*,
                                     const std::vector<const TufaClass*>*);
NativeResult groundHaltTrafficPath(const std::vector<VmValue>&, const VmObject*, VmArena*,
                                   const std::vector<const TufaClass*>*);
