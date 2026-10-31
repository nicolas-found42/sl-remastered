#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct ResourceTextureResult {
  bool ok = false;
  std::filesystem::path path;
  std::optional<float> lodBias;
  std::optional<float> lodAmplitude;
  std::string error;
};

ResourceTextureResult resourceTextureLookup(const std::vector<uint8_t> &rpk,
                                            uint16_t localId,
                                            const std::filesystem::path &root);

struct ResourceTextureMip {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;
};

struct ResourceTexturePixels {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;
  // File-supplied levels after the base, in descending size order.
  std::vector<ResourceTextureMip> mipLevels;
  std::optional<float> lodBias;
  std::optional<float> lodAmplitude;
};

class Render2D;
// Renderer must outlive this cache. The handle's deleter releases the SDL
// texture.
struct ResourceTextureCache {
  ResourceTexturePixels pixels;
  Render2D *renderer = nullptr;
  std::shared_ptr<size_t> handle;
  std::shared_ptr<void>
      allocation; // inventory lifetime follows the cached host texture
};

// Decodes a type-7 PNG, TGA, PTX or DXT1 DDS resource, retaining file mips.
bool resourceTextureLoad(const std::vector<uint8_t> &rpk, uint16_t localId,
                         const std::filesystem::path &root,
                         ResourceTexturePixels &pixels, std::string &error);
