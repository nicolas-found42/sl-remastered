#pragma once
#include "world_model.hpp"
#include <optional>
#include <vector>

// 0053dee0 EXTP tail: keep the parent's center and expand each half extent
// in child order until it contains each child's box. Only finite mode-1 boxes
// with nonnegative extents are supported; no pack loading is performed here.
std::optional<ResourceBounds> expandExtpBounds(const ResourceBounds &parent,
                                               const std::vector<ResourceBounds> &children);
