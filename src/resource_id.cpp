#include "resource_id.hpp"

uint32_t remapResourceId(uint32_t serializedId, const ResourceIdMapping& mapping) {
	if (mapping.currentPack == 0xffff) return serializedId;
	const uint32_t localId = serializedId & 0xffff;
	const uint32_t packIndex = serializedId >> 16;
	if (localId == 0 || packIndex >= mapping.globalPackCount) return 0;
	if (packIndex == 0) return (static_cast<uint32_t>(mapping.currentPack) << 16) | localId;
	if (packIndex > mapping.dependencies.size()) return 0;
	return (static_cast<uint32_t>(mapping.dependencies[packIndex - 1]) << 16) | localId;
}
