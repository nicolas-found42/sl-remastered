#pragma once

#include "vm.hpp"

// Returns false without mutation when the stock handler requires an unimplemented clone.
// A null source represents a null script reference and leaves the receiver unchanged.
bool resourceDuplicateShared(VmResourceState& receiver, const VmResourceState* source);
