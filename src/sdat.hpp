#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SdatUnsaved {
	uint32_t ordinal = 0;
	uint32_t kind = 0;
	uint32_t size = 0;
	uint64_t blobOffset = 0;
};

struct SdatPack {
	uint32_t ordinal = 0;
	std::string path;
};

struct SdatFile {
	uint8_t type = 0;
	uint8_t flags = 0;
	uint64_t payloadSize = 0;
	std::vector<SdatUnsaved> unsaved;
	std::vector<SdatPack> packs;
	uint32_t zeroOrdinals = 0;
	uint32_t duplicateOrdinals = 0;
	uint32_t largeOrdinals = 0;
};

struct SdatResult {
	bool ok = false;
	SdatFile file;
	std::string error;
	uint64_t errorOffset = 0;
};

SdatResult sdatParse(const uint8_t* data, size_t size);
SdatResult sdatLoadFile(const std::string& path);
