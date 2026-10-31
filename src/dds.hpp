#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct DdsLevel {
  uint32_t width;
  uint32_t height;
  size_t offset;
  size_t size;
};

// The input buffer owns the blocks; level offsets remain valid while it is
// unchanged.
struct DdsImage {
  enum class Format { Dxt1, Dxt3, Dxt5 };
  Format format = Format::Dxt1;
  std::vector<DdsLevel> levels;
};

bool ddsParseCompressed(const uint8_t *data, size_t size, DdsImage &image,
                        std::string &error);

// Reads bounded 2D DXT1 DDS files. It does not decode pixels.
bool ddsParseDxt1(const uint8_t *data, size_t size, DdsImage &image,
                  std::string &error);

struct DdsPixels {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;
};

bool ddsDecodeDxt1(const uint8_t *data, size_t size, const DdsLevel &level,
                   DdsPixels &pixels, std::string &error);

bool ddsDecodeDxt3(const uint8_t *data, size_t size, const DdsLevel &level,
                   DdsPixels &pixels, std::string &error);

bool ddsDecodeDxt5(const uint8_t *data, size_t size, const DdsLevel &level,
                   DdsPixels &pixels, std::string &error);

// Decodes any level of a parsed compressed DDS (DXT1/3/5).
bool ddsDecodeLevel(const uint8_t *data, size_t size, const DdsImage &image,
                    const DdsLevel &level, DdsPixels &pixels,
                    std::string &error);
