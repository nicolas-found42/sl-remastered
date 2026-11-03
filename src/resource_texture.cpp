#include "resource_texture.hpp"
#include "dds.hpp"
#include "ptx.hpp"
#include "rpk.hpp"
#include <png.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <locale>
#include <memory>
#include <sstream>
#include <string_view>
#include <system_error>

namespace {
ResourceTextureResult fail(const char *error) {
  return {false, {}, {}, {}, error};
}
struct PngReader {
  const uint8_t *data;
  size_t size;
  size_t pos;
  std::vector<uint8_t> rgba;
  std::vector<png_bytep> rows;
};
void readPng(png_structp png, png_bytep destination, png_size_t count) {
  auto *input = static_cast<PngReader *>(png_get_io_ptr(png));
  if (count > input->size - input->pos)
    png_error(png, "truncated PNG");
  std::memcpy(destination, input->data + input->pos, count);
  input->pos += count;
}
bool decodePng(const std::vector<uint8_t> &data, ResourceTexturePixels &pixels,
               std::string &error) {
  auto input = std::make_unique<PngReader>();
  input->data = data.data();
  input->size = data.size();
  input->pos = 0;
  png_structp png =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png) {
    error = "PNG decoder unavailable";
    return false;
  }
  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    error = "PNG metadata unavailable";
    return false;
  }
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, nullptr);
    error = "PNG decode failed";
    return false;
  }
  png_set_read_fn(png, input.get(), readPng);
  png_read_info(png, info);
  const png_uint_32 width = png_get_image_width(png, info),
                    height = png_get_image_height(png, info);
  if (!width || !height || width > 16384 || height > 16384 ||
      static_cast<uint64_t>(width) * height > 64u * 1024u * 1024u)
    png_error(png, "unsupported dimensions");
  const int depth = png_get_bit_depth(png, info),
            color = png_get_color_type(png, info);
  if (depth == 16)
    png_set_strip_16(png);
  if (color == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png);
  if (color == PNG_COLOR_TYPE_GRAY && depth < 8)
    png_set_expand_gray_1_2_4_to_8(png);
  const bool transparency = png_get_valid(png, info, PNG_INFO_tRNS) != 0;
  if (transparency)
    png_set_tRNS_to_alpha(png);
  if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);
  if (!(color & PNG_COLOR_MASK_ALPHA) && !transparency)
    png_set_add_alpha(png, 255, PNG_FILLER_AFTER);
  png_set_interlace_handling(png);
  png_read_update_info(png, info);
  if (png_get_rowbytes(png, info) != static_cast<size_t>(width) * 4)
    png_error(png, "unsupported pixel layout");
  input->rgba.resize(static_cast<size_t>(width) * height * 4);
  input->rows.resize(height);
  for (size_t y = 0; y < height; ++y)
    input->rows[y] = input->rgba.data() + y * static_cast<size_t>(width) * 4;
  png_read_image(png, input->rows.data());
  png_read_end(png, info);
  png_destroy_read_struct(&png, &info, nullptr);
  pixels.width = width;
  pixels.height = height;
  pixels.rgba = std::move(input->rgba);
  return true;
}
// Stock TGA textures (particles.rpk asphalt/sand, screenshots): type 2
// uncompressed and type 10 RLE true-color at 24/32 bpp, plus type 3
// 8-bit grayscale (frontend fonts). BGR(A) becomes RGBA; the descriptor
// origin bits select the row and column order.
bool decodeTga(const std::vector<uint8_t> &data, ResourceTexturePixels &pixels,
               std::string &error) {
  auto u16 = [&data](size_t at) {
    return static_cast<uint32_t>(data[at]) | static_cast<uint32_t>(data[at + 1])
                                                 << 8;
  };
  if (data.size() < 18) {
    error = "TGA header truncated";
    return false;
  }
  const uint32_t idLength = data[0];
  const uint8_t colorMapType = data[1];
  const uint8_t imageType = data[2];
  const uint32_t width = u16(12), height = u16(14);
  const uint8_t bpp = data[16];
  const uint8_t descriptor = data[17];
  if (colorMapType != 0) {
    error = "TGA color-mapped images unsupported";
    return false;
  }
  if (descriptor & 0xc0) {
    error = "interleaved TGA unsupported";
    return false;
  }
  const bool grayscale = imageType == 3;
  const bool rle = imageType == 10;
  if ((!grayscale && imageType != 2 && !rle) || (grayscale && bpp != 8) ||
      (!grayscale && bpp != 24 && bpp != 32)) {
    error = "unsupported TGA image type or pixel depth";
    return false;
  }
  if (!width || !height || width > 16384 || height > 16384 ||
      static_cast<uint64_t>(width) * height > 64u * 1024u * 1024u) {
    error = "unsupported TGA dimensions";
    return false;
  }
  const size_t stride = grayscale ? 1 : static_cast<size_t>(bpp / 8);
  size_t at = 18 + idLength;
  const size_t pixelCount = static_cast<size_t>(width) * height;
  std::vector<uint8_t> raw;
  if (rle) {
    raw.reserve(pixelCount * stride);
    while (raw.size() < pixelCount * stride && at < data.size()) {
      const uint8_t header = data[at++];
      const size_t count = (header & 0x7f) + 1;
      if (count > (pixelCount * stride - raw.size()) / stride) {
        error = "TGA RLE packet exceeds image";
        return false;
      }
      if (header & 0x80) {
        if (at + stride > data.size()) {
          error = "TGA RLE packet truncated";
          return false;
        }
        for (size_t i = 0; i < count; ++i)
          raw.insert(raw.end(), data.begin() + at, data.begin() + at + stride);
        at += stride;
      } else {
        if (at + count * stride > data.size()) {
          error = "TGA raw packet truncated";
          return false;
        }
        raw.insert(raw.end(), data.begin() + at,
                   data.begin() + at + count * stride);
        at += count * stride;
      }
    }
    if (raw.size() < pixelCount * stride) {
      error = "TGA pixel data truncated";
      return false;
    }
  } else {
    if (at + pixelCount * stride > data.size()) {
      error = "TGA pixel data truncated";
      return false;
    }
    raw.assign(data.begin() + at, data.begin() + at + pixelCount * stride);
  }
  const bool topDown = (descriptor & 0x20) != 0;
  pixels.width = width;
  pixels.height = height;
  pixels.rgba.resize(pixelCount * 4);
  for (uint32_t row = 0; row < height; ++row) {
    const uint32_t y = topDown ? row : height - 1 - row;
    for (uint32_t x = 0; x < width; ++x) {
      const uint32_t sourceX = (descriptor & 0x10) ? width - 1 - x : x;
      const size_t src = (static_cast<size_t>(y) * width + sourceX) * stride;
      const size_t dst = (static_cast<size_t>(row) * width + x) * 4;
      if (grayscale) {
        pixels.rgba[dst] = pixels.rgba[dst + 1] = pixels.rgba[dst + 2] =
            raw[src];
        pixels.rgba[dst + 3] = 255;
      } else {
        pixels.rgba[dst] = raw[src + 2];
        pixels.rgba[dst + 1] = raw[src + 1];
        pixels.rgba[dst + 2] = raw[src];
        pixels.rgba[dst + 3] = stride == 4 ? raw[src + 3] : 255;
      }
    }
  }
  return true;
}

std::string_view trim(std::string_view value) {
  const auto first = value.find_first_not_of(" \t\r");
  if (first == std::string_view::npos)
    return {};
  const auto last = value.find_last_not_of(" \t\r");
  return value.substr(first, last - first + 1);
}
} // namespace

ResourceTextureResult resourceTextureLookup(const std::vector<uint8_t> &rpk,
                                            uint16_t localId,
                                            const std::filesystem::path &root) {
  const auto parsed = rpkParse(rpk.data(), rpk.size());
  if (!parsed.ok)
    return fail("invalid RPK");
  const RpkEntry *entry = nullptr;
  for (const auto &candidate : parsed.pack.entries) {
    if (candidate.typeId == localId) {
      if (entry)
        return fail("duplicate resource ID");
      entry = &candidate;
    }
  }
  if (!entry)
    return fail("resource ID not found");
  if (entry->resType != 7)
    return fail("resource is not a texture");
  const uint64_t end =
      static_cast<uint64_t>(entry->payloadOffset) + entry->payloadSize;
  const size_t tableHeader = 16 + parsed.pack.dependencies.size() * 64;
  const uint32_t tableSize =
      static_cast<uint32_t>(rpk[tableHeader]) |
      (static_cast<uint32_t>(rpk[tableHeader + 1]) << 8) |
      (static_cast<uint32_t>(rpk[tableHeader + 2]) << 16) |
      (static_cast<uint32_t>(rpk[tableHeader + 3]) << 24);
  const uint64_t tableEnd = static_cast<uint64_t>(tableHeader) + 16 + tableSize;
  if (end > rpk.size() || entry->payloadOffset < tableEnd ||
      entry->payloadSize == 0)
    return fail("invalid texture payload bounds");
  const std::string_view payload(
      reinterpret_cast<const char *>(rpk.data() + entry->payloadOffset),
      entry->payloadSize);
  if (payload.find('\0') != std::string_view::npos)
    return fail("invalid texture payload");
  std::string source;
  std::optional<float> lodBias;
  std::optional<float> lodAmplitude;
  for (size_t pos = 0; pos < payload.size();) {
    const size_t newline = payload.find('\n', pos);
    auto line = trim(payload.substr(pos, newline == std::string_view::npos
                                             ? payload.size() - pos
                                             : newline - pos));
    pos = newline == std::string_view::npos ? payload.size() : newline + 1;
    const size_t split = line.find_first_of(" \t");
    const auto key = line.substr(0, split);
    const auto value = split == std::string_view::npos
                           ? std::string_view{}
                           : trim(line.substr(split));
    if (key == "sourcefile") {
      if (!source.empty() || value.empty())
        return fail("invalid sourcefile directive");
      source = std::string(value);
    } else if (key == "flags") {
      std::istringstream input{std::string(value)};
      input.imbue(std::locale::classic());
      int flags = 0;
      if (!(input >> flags))
        return fail(("invalid texture flags: " + std::string(value)).c_str());
      // FUN_0053cc70 scans a decimal integer into resource +0x10. Like
      // sscanf("%d"), it accepts a numeric prefix (including ".000").
      // Source selection and image loading do not branch on this word.
    } else if (key == "lod_bias" || key == "lod_amp") {
      auto &field = key == "lod_bias" ? lodBias : lodAmplitude;
      if (field || value.empty())
        return fail("invalid texture LOD directive");
      std::istringstream input{std::string(value)};
      input.imbue(std::locale::classic());
      float bias;
      if (!(input >> bias) || !std::isfinite(bias))
        return fail("invalid lod_bias directive");
      input >> std::ws;
      if (!input.eof())
        return fail("invalid lod_bias directive");
      field = bias;
    }
  }
  if (source.empty())
    return fail("missing sourcefile directive");
  std::replace(source.begin(), source.end(), '\\', '/');
  if (source.front() == '/' || source.find(':') != std::string::npos)
    return fail("unsafe sourcefile path");
  const std::filesystem::path relative(source);
  for (const auto &part : relative)
    if (part == "." || part == ".." || part.empty())
      return fail("unsafe sourcefile path");
  std::error_code ec;
  const auto base = std::filesystem::canonical(root, ec);
  if (ec)
    return fail("invalid resource root");
  const auto target = std::filesystem::canonical(base / relative, ec);
  if (ec)
    return fail(("texture file not found: " + relative.string() +
                 " (" + ec.message() + ")").c_str());
  auto prefix = base.begin(), component = target.begin();
  for (; prefix != base.end() && component != target.end();
       ++prefix, ++component)
    if (*prefix != *component)
      return fail("sourcefile escapes resource root");
  if (prefix != base.end() || !std::filesystem::is_regular_file(target, ec) ||
      ec)
    return fail("unsafe sourcefile path");
  return {true, target, lodBias, lodAmplitude, {}};
}

bool resourceTextureLoad(const std::vector<uint8_t> &rpk, uint16_t localId,
                         const std::filesystem::path &root,
                         ResourceTexturePixels &pixels, std::string &error) {
  pixels = {};
  const auto resource = resourceTextureLookup(rpk, localId, root);
  if (!resource.ok) {
    error = resource.error;
    return false;
  }
  std::string extension = resource.path.extension().string();
  std::transform(
      extension.begin(), extension.end(), extension.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (extension != ".dds" && extension != ".ptx" && extension != ".png" &&
      extension != ".tga") {
    error = "unsupported texture format";
    return false;
  }
  std::error_code ec;
  const auto size = std::filesystem::file_size(resource.path, ec);
  if (ec || size > 256u * 1024u * 1024u) {
    error = "texture file size unavailable or too large";
    return false;
  }
  std::ifstream input(resource.path, std::ios::binary);
  if (!input) {
    error = "texture file open failed";
    return false;
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)), {});
  if (input.bad() || data.size() != size) {
    error = "texture file read failed";
    return false;
  }
  if (extension == ".dds") {
    DdsImage image;
    DdsPixels decoded;
    if (!ddsParseCompressed(data.data(), data.size(), image, error) ||
        !ddsDecodeLevel(data.data(), data.size(), image, image.levels[0],
                        decoded, error))
      return false;
    pixels.width = decoded.width;
    pixels.height = decoded.height;
    pixels.rgba = std::move(decoded.rgba);
    pixels.authoredMipChain = true;
    for (size_t i = 1; i < image.levels.size(); ++i) {
      DdsPixels mip;
      if (!ddsDecodeLevel(data.data(), data.size(), image, image.levels[i], mip,
                          error)) {
        pixels = {};
        return false;
      }
      pixels.mipLevels.push_back({mip.width, mip.height, std::move(mip.rgba)});
    }
  } else if (extension == ".png") {
    if (!decodePng(data, pixels, error))
      return false;
  } else if (extension == ".tga") {
    if (!decodeTga(data, pixels, error))
      return false;
  } else {
    PtxImage image{};
    PtxPixels decoded{};
    if (!ptxParse(data.data(), data.size(), image, error) ||
        image.levels.empty()) {
      if (error.empty())
        error = "PTX has no mip levels";
      return false;
    }
    if (!ptxDecode(data.data(), data.size(), image.levels[0], decoded, error))
      return false;
    pixels.width = decoded.width;
    pixels.height = decoded.height;
    pixels.rgba = std::move(decoded.rgba);
    pixels.authoredMipChain = true;
    for (size_t i = 1; i < image.levels.size(); ++i) {
      PtxPixels mip{};
      if (!ptxDecode(data.data(), data.size(), image.levels[i], mip, error)) {
        pixels = {};
        return false;
      }
      pixels.mipLevels.push_back({mip.width, mip.height, std::move(mip.rgba)});
    }
  }
  pixels.lodBias = resource.lodBias;
  pixels.lodAmplitude = resource.lodAmplitude;
  error.clear();
  return true;
}
