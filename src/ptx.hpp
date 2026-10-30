#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct PtxLevel {
	size_t colorOffset;
	size_t colorSize;
	size_t alphaOffset;
	size_t alphaSize;
};

struct PtxImage {
	uint32_t width;
	uint32_t height;
	std::vector<PtxLevel> levels;
};

struct PtxPixels {
	uint32_t width;
	uint32_t height;
	std::vector<uint8_t> rgba;
};

bool ptxParse(const uint8_t* data, size_t size, PtxImage& image, std::string& error);
bool ptxDecode(const uint8_t* data, size_t size, const PtxLevel& level, PtxPixels& pixels, std::string& error);
