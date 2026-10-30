#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct RpkDependency {
	uint32_t id = 0;
	std::string path;
	uint32_t trailer = 0;
};

struct RpkEntry {
	uint32_t superId = 0;
	uint32_t typeId = 0;
	uint8_t resType = 0;
	uint8_t flags = 0;
	float compat = 0.0f;
	uint32_t payloadOffset = 0;
	uint32_t payloadSize = 0;
	std::string alias;
	std::optional<std::array<float, 12>> transform;
};

struct RpkPack {
	uint32_t version = 0;
	uint32_t serviceA = 0;
	uint32_t serviceB = 0;
	std::vector<RpkDependency> dependencies;
	std::vector<RpkEntry> entries;
	uint32_t payloadsOutOfRange = 0;
	uint32_t payloadsOverlappingTable = 0;
};

struct RpkResult {
	bool ok = false;
	RpkPack pack;
	std::string error;
	uint64_t errorOffset = 0;
};

RpkResult rpkParse(const uint8_t* data, size_t size);
RpkResult rpkLoadFile(const std::string& path);
