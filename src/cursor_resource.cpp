#include "cursor_resource.hpp"
#include <fstream>
#include <iterator>
#include <limits>

namespace {
struct Reader {
  std::vector<uint8_t> bytes;
  bool range(size_t p, size_t n) const { return p <= bytes.size() && n <= bytes.size() - p; }
  bool u16(size_t p, uint16_t &v) const {
    if (!range(p, 2)) return false;
    v = static_cast<uint16_t>(bytes[p] | (static_cast<uint16_t>(bytes[p + 1]) << 8));
    return true;
  }
  bool u32(size_t p, uint32_t &v) const {
    if (!range(p, 4)) return false;
    v = static_cast<uint32_t>(bytes[p]) | (static_cast<uint32_t>(bytes[p + 1]) << 8) |
        (static_cast<uint32_t>(bytes[p + 2]) << 16) | (static_cast<uint32_t>(bytes[p + 3]) << 24);
    return true;
  }
};
bool entry(const Reader &r, size_t base, uint32_t dir, uint16_t id, uint32_t &out) {
  uint16_t named, count;
  if (!r.range(base, dir) || !r.u16(base + dir + 12, named) || !r.u16(base + dir + 14, count)) return false;
  const size_t total = static_cast<size_t>(named) + count;
  if (!r.range(base + dir + 16, total * 8)) return false;
  for (size_t i = 0; i < total; ++i) {
    uint32_t name, data;
    if (!r.u32(base + dir + 16 + i * 8, name) || !r.u32(base + dir + 20 + i * 8, data)) return false;
    if (name == id) { out = data; return true; }
  }
  return false;
}
bool resource(const Reader &r, size_t base, uint32_t baseRva, uint16_t type,
              uint16_t id, size_t &offset, size_t &size) {
  uint32_t typeDir, idDir, data, address, length;
  uint16_t named, count;
  if (!entry(r, base, 0, type, typeDir) || !(typeDir & 0x80000000u) ||
      !entry(r, base, typeDir & 0x7fffffffu, id, idDir) || !(idDir & 0x80000000u)) return false;
  const size_t lang = base + (idDir & 0x7fffffffu);
  if (!r.u16(lang + 12, named) || !r.u16(lang + 14, count) || named != 0 || count != 1 ||
      !r.u32(lang + 20, data) || (data & 0x80000000u) ||
      !r.u32(base + data, address) || !r.u32(base + data + 4, length) || address < baseRva) return false;
  const size_t relative = static_cast<size_t>(address - baseRva);
  if (!r.range(base, relative) || !r.range(base + relative, length)) return false;
  offset = base + relative; size = length; return true;
}
}

bool loadStockCursor(const std::filesystem::path &exe, uint16_t groupId,
                     CursorPixels &out, std::string &error) {
  out = {};
  const auto fail = [&]() { error = "unsupported or malformed stock cursor resource"; return false; };
  std::ifstream file(exe, std::ios::binary);
  if (!file) { error = "cannot open stock executable for cursor"; return false; }
  Reader r{{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()}};
  uint32_t pe, signature, rsrcRva;
  uint16_t sections, optSize, magic;
  if (!r.u32(0x3c, pe)) return fail();
  const size_t peOffset = pe;
  if (!r.u32(peOffset, signature) || signature != 0x4550 ||
      !r.u16(peOffset + 6, sections) || !r.u16(peOffset + 20, optSize) || !r.u16(peOffset + 24, magic) ||
      magic != 0x10b || optSize < 116 || !r.u32(peOffset + 24 + 112, rsrcRva)) return fail();
  const size_t sectionTable = peOffset + 24 + optSize;
  if (!r.range(sectionTable, static_cast<size_t>(sections) * 40)) return fail();
  size_t base = std::numeric_limits<size_t>::max();
  for (size_t i = 0; i < sections; ++i) {
    const size_t s = sectionTable + i * 40;
    uint32_t virtualSize, va, rawSize, rawOffset;
    if (!r.u32(s + 8, virtualSize) || !r.u32(s + 12, va) ||
        !r.u32(s + 16, rawSize) || !r.u32(s + 20, rawOffset)) return fail();
    if (rsrcRva >= va && rsrcRva - va < rawSize) {
      base = static_cast<size_t>(rawOffset) + (rsrcRva - va); break;
    }
  }
  if (base == std::numeric_limits<size_t>::max()) return fail();
  size_t group, groupSize;
  uint16_t reserved, type, count, imageId;
  if (!resource(r, base, rsrcRva, 12, groupId, group, groupSize) || groupSize != 20 ||
      !r.u16(group, reserved) || reserved || !r.u16(group + 2, type) || type != 2 ||
      !r.u16(group + 4, count) || count != 1 || !r.u16(group + 18, imageId)) return fail();
  size_t image, imageSize;
  uint16_t hotX, hotY, planes, bits;
  uint32_t header, width, doubledHeight, compression, colors;
  if (!resource(r, base, rsrcRva, 1, imageId, image, imageSize) || imageSize < 108 ||
      !r.u16(image, hotX) || !r.u16(image + 2, hotY) || !r.u32(image + 4, header) || header != 40 ||
      !r.u32(image + 8, width) || !r.u32(image + 12, doubledHeight) ||
      !r.u16(image + 16, planes) || !r.u16(image + 18, bits) ||
      !r.u32(image + 20, compression) || !r.u32(image + 36, colors) ||
      planes != 1 || bits != 4 || compression != 0 || (colors != 0 && colors != 16) ||
      width == 0 || width > 256 || doubledHeight == 0 || (doubledHeight & 1) || doubledHeight > 512)
    return fail();
  const uint32_t height = doubledHeight / 2;
  if (hotX >= width || hotY >= height) return fail();
  const size_t xorStride = ((static_cast<size_t>(width) * 4 + 31) / 32) * 4;
  const size_t andStride = ((static_cast<size_t>(width) + 31) / 32) * 4;
  const size_t xorOffset = image + 108, andOffset = xorOffset + xorStride * height;
  if (108 + (xorStride + andStride) * height > imageSize) return fail();
  CursorPixels pixels{width, height, hotX, hotY, std::vector<uint8_t>(static_cast<size_t>(width) * height * 4)};
  for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
    const size_t row = height - 1 - y;
    const uint8_t packed = r.bytes[xorOffset + row * xorStride + x / 2];
    const uint8_t index = x % 2 ? packed & 15 : packed >> 4;
    const bool mask = (r.bytes[andOffset + row * andStride + x / 8] & (0x80u >> (x % 8))) != 0;
    const size_t color = image + 44 + static_cast<size_t>(index) * 4;
    const size_t dest = (static_cast<size_t>(y) * width + x) * 4;
    if (mask && (r.bytes[color] || r.bytes[color + 1] || r.bytes[color + 2])) return fail();
    pixels.rgba[dest] = r.bytes[color + 2]; pixels.rgba[dest + 1] = r.bytes[color + 1];
    pixels.rgba[dest + 2] = r.bytes[color]; pixels.rgba[dest + 3] = mask ? 0 : 255;
  }
  out = std::move(pixels); error.clear(); return true;
}
