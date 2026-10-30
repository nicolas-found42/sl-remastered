#pragma once

#include "pack_registry.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Bounded parser for mesh, flags and ordered textures in a type-14 payload.
struct ResourceMaterialResult {
	bool ok = false;
	uint32_t mesh = 0;
	std::vector<uint32_t> textures;
	uint32_t flags = 0;
	std::string error;
};
ResourceMaterialResult resourceMaterialParse(const PackResource& resource);
