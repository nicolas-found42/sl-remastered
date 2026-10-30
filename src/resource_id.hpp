#pragma once

#include <cstdint>
#include <vector>

struct ResourceIdMapping {
	uint16_t currentPack;
	uint32_t globalPackCount;
	std::vector<uint16_t> dependencies;
};

uint32_t remapResourceId(uint32_t serializedId, const ResourceIdMapping& mapping);
