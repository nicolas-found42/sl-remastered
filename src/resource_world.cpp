#include "resource_world.hpp"
#include "game_spatial_payload.hpp"
#include "ground_runtime.hpp"
#include "resource_extp_bounds.hpp"

#include "resource_id.hpp"
#include "resource_texture.hpp"
#include "scene_2d.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <string_view>
#include <tuple>
#include <type_traits>

std::optional<uint64_t> stockTextureUpperBound(uint32_t width,
                                               uint32_t height) {
  if (!width || !height)
    return std::nullopt;
  uint64_t total = 0;
  for (;;) {
    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    if (pixels > (std::numeric_limits<uint64_t>::max() - total) / 4)
      return std::nullopt;
    total += pixels * 4;
    if (width == 1 && height == 1)
      return total;
    width = std::max(uint32_t{1}, width / 2);
    height = std::max(uint32_t{1}, height / 2);
  }
}

std::optional<uint64_t> stockTextureFamilyUpperBound(uint32_t width,
                                                     uint32_t height) {
  if (!width || !height || width > 16384 || height > 16384 ||
      (width & (width - 1)) || (height & (height - 1)))
    return std::nullopt;
  // 00591d85 rounds source dimensions up to powers of two. 005914a9
  // applies device caps and aspect constraints, then rounds compressed formats
  // up to four pixels. A square using the larger input bounds those cases.
  // 005062a0 releases two slots per object; 0053cfb0 can create a secondary.
  const uint32_t side = std::max({width, height, uint32_t{4}});
  const auto slot = stockTextureUpperBound(side, side);
  if (!slot || *slot > std::numeric_limits<uint64_t>::max() / 4)
    return std::nullopt;
  return *slot * 4;
}

StockDeviceTextureCaps hostDeviceTextureCaps(uint32_t rendererMaxTextureSize) {
  return {kD3DTexCapsMipMap, rendererMaxTextureSize, rendererMaxTextureSize, 0};
}

namespace {
uint32_t bitLength(uint32_t value) {
  uint32_t n = 0;
  for (; value; value >>= 1)
    ++n;
  return n;
}
uint32_t roundUpPow2(uint32_t value) {
  uint32_t r = 1;
  while (r < value)
    r <<= 1;
  return r;
}
} // namespace

std::optional<StockTextureShape>
stockDeviceTextureShape(uint32_t width, uint32_t height,
                        const StockDeviceTextureCaps &caps) {
  if (width < 4 || height < 4 || width > 0x40000000u || height > 0x40000000u ||
      !caps.maxWidth || !caps.maxHeight)
    return std::nullopt;
  // 005914a9, type 3 (2-D), 2-D pool: clamp to the device maximum, then the
  // aspect limit.
  uint32_t w = std::min(width, caps.maxWidth);
  uint32_t h = std::min(height, caps.maxHeight);
  uint32_t aspect = caps.maxAspectRatio;
  if (caps.textureCaps & kD3DTexCapsSquareOnly)
    aspect = 1;
  if (aspect) {
    if (static_cast<uint64_t>(aspect) * h < w)
      w = static_cast<uint32_t>(static_cast<uint64_t>(aspect) * h);
    const uint64_t limit = static_cast<uint64_t>(aspect) * w;
    if (limit < h)
      h = static_cast<uint32_t>(limit);
  }
  // Requested mip levels are 0 (all). The "non-power-of-two conditional"
  // exception only applies to a single-level request, so the power-of-two cap
  // decides here.
  if (caps.textureCaps & kD3DTexCapsPow2) {
    w = roundUpPow2(w);
    h = roundUpPow2(h);
  }
  uint32_t levels = 1;
  if ((caps.textureCaps & kD3DTexCapsMipMap) &&
      (!(caps.textureCaps & kD3DTexCapsNonPow2Conditional) ||
       (!(w & (w - 1)) && !(h & (h - 1)))))
    levels = std::max(bitLength(w), bitLength(h));
  return StockTextureShape{w, h, levels};
}

std::optional<uint64_t>
stockTextureSlotUpperBound(const StockTextureShape &shape) {
  if (!shape.width || !shape.height || !shape.levels)
    return std::nullopt;
  uint64_t total = 0;
  uint32_t w = shape.width, h = shape.height;
  for (uint32_t level = 0; level < shape.levels; ++level) {
    const uint64_t bytes = static_cast<uint64_t>(w) * h * 4;
    if (bytes > std::numeric_limits<uint64_t>::max() - total)
      return std::nullopt;
    total += bytes;
    w = std::max(uint32_t{1}, w / 2);
    h = std::max(uint32_t{1}, h / 2);
  }
  return total;
}

std::optional<std::array<uint32_t, 2>>
stockTextureCloneSize(uint32_t width, uint32_t height,
                      uint32_t availableBytes) {
  // Powers of two make the stock x87 aspect ratio exact, avoiding a rounding
  // claim for arbitrary dimensions.
  if (!width || !height || width > 16384 || height > 16384 ||
      (width & (width - 1)) || (height & (height - 1)))
    return std::nullopt;
  if (availableBytes > 0x08000000u)
    return std::array<uint32_t, 2>{width, height};
  const uint32_t limit = availableBytes < 0x04000001u ? 256 : 512;
  if (width > height) {
    const uint32_t nextWidth = std::min(width, limit);
    const uint32_t nextHeight =
        static_cast<uint32_t>((uint64_t{height} * nextWidth) / width);
    if (!nextHeight)
      return std::nullopt;
    return std::array<uint32_t, 2>{nextWidth, nextHeight};
  }
  const uint32_t nextHeight = std::min(height, limit);
  const uint32_t nextWidth =
      static_cast<uint32_t>((uint64_t{width} * nextHeight) / height);
  if (!nextWidth)
    return std::nullopt;
  return std::array<uint32_t, 2>{nextWidth, nextHeight};
}

std::shared_ptr<void>
TextureAllocationInventory::track(std::optional<uint64_t> upperBound) {
  const uint64_t id = state_->next++;
  state_->entries.emplace(id, upperBound);
  return std::shared_ptr<void>(new uint64_t(id), [state = state_](void *value) {
    auto *key = static_cast<uint64_t *>(value);
    state->entries.erase(*key);
    delete key;
  });
}

std::optional<uint64_t> TextureAllocationInventory::upperBound() const {
  uint64_t total = 0;
  for (const auto &entry : state_->entries) {
    if (!entry.second ||
        *entry.second > std::numeric_limits<uint64_t>::max() - total)
      return std::nullopt;
    total += *entry.second;
  }
  return total;
}

size_t TextureAllocationInventory::count() const {
  return state_->entries.size();
}

std::optional<uint64_t> stockMeshVertexBytes(const ScxModel &model) {
  if (model.version != 3 && model.version != 4)
    return std::nullopt;
  uint64_t total = 0;
  for (const auto &mesh : model.meshes) {
    uint64_t stride = 56;
    if (model.version == 4) {
      // 00503400 and 004c68e0 select two UV channels for this
      // single-map material; 004fe980 makes a 40-byte stride.
      if (mesh.vertexFlags != 0x241 || !mesh.materialIndex ||
          *mesh.materialIndex >= model.materials.size())
        return std::nullopt;
      const auto &material = model.materials[*mesh.materialIndex];
      if (material.flags != 0 || material.functionWord != 0x40000000u ||
          material.maps.size() != 1 || material.maps[0].slot != 0 ||
          material.maps[0].channel != 0 ||
          material.maps[0].textureIndex == UINT32_MAX)
        return std::nullopt;
      stride = 40;
    }
    const uint64_t bytes = static_cast<uint64_t>(mesh.vertexCount) * stride;
    if (bytes > std::numeric_limits<uint64_t>::max() - total)
      return std::nullopt;
    total += bytes;
  }
  return total;
}

std::optional<uint64_t> stockMeshVertexBytesUpperBound(const ScxModel &model) {
  if (const auto exact = stockMeshVertexBytes(model))
    return exact;
  if (model.version != 4)
    return std::nullopt;
  // FUN_004fe980 builds the vertex stride from the format flags: position 12,
  // up to 7 blend weights (28), one index word (4), normal 12, diffuse 4,
  // specular 4 and at most 15 texture sets of 16 bytes (240). No format can
  // exceed 304 bytes, so vertexCount * 304 bounds the stock allocation.
  constexpr uint64_t kMaxStride = 12 + 28 + 4 + 12 + 4 + 4 + 240;
  uint64_t total = 0;
  for (const auto &mesh : model.meshes) {
    uint64_t stride = kMaxStride;
    if (mesh.materialIndex && *mesh.materialIndex < model.materials.size()) {
      const auto &material = model.materials[*mesh.materialIndex];
      // 004befc0 / 004c68e0 select position, normal, optional colors and at
      // most three two-float UV sets for type 0 (00503400 / 004fe980).
      if (material.flags == 0)
        stride = 56;
      // 004d71f0 selects 004dbcd0 for type 12 with function word zero:
      // position, normal and one two-float UV set.
      else if (material.flags == 12 && material.functionWord == 0)
        stride = 32;
    }
    const uint64_t bytes = static_cast<uint64_t>(mesh.vertexCount) * stride;
    if (bytes > std::numeric_limits<uint64_t>::max() - total)
      return std::nullopt;
    total += bytes;
  }
  return total;
}

namespace {

// Rows follow the case order of the switch in FUN_00536a10. Flags are
// DAT_00619d10 (live read: types 2, 3 and 5 have 0x100, everything else up to
// 0x15 has 0). Sector hooks are DAT_00619d68 (live read: types 1, 2, 3, 0xf,
// 0x10 and 0x11).
const NodeTypeInfo kTypes[] = {
    {1, "ins_game", 0, 0x0053e6e0, 0, true, true, PayloadClass::GameOrPhysics},
    {2, "ins_physics", 0x164, 0x0053f8b0, 0x100, true, true,
     PayloadClass::GameOrPhysics},
    {3, "ins_render", 0x26c, 0x00543000, 0x100, true, true,
     PayloadClass::Renderer},
    {5, "res_mesh", 0xfc, 0x0053b520, 0x100, false, false,
     PayloadClass::Renderer},
    {6, "res_sound", 0x90, 0x0053bf40, 0, false, false, PayloadClass::Audio},
    {7, "res_texture", 0xac, 0x0053d060, 0, false, false,
     PayloadClass::Renderer},
    {8, "typ_game_object", 0xa0, 0x00546ae0, 0, false, false,
     PayloadClass::Script},
    {9, "typ_physics_body", 0x94, 0x005473d0, 0, false, false,
     PayloadClass::GameOrPhysics},
    {10, "typ_physics_constraint", 0x90, 0x00547650, 0, false, false,
     PayloadClass::GameOrPhysics},
    {12, "typ_render_camera", 0xbc, 0x00547f60, 0, false, false,
     PayloadClass::Renderer},
    {13, "typ_render_light", 0x1a0, 0x00549010, 0, false, false,
     PayloadClass::Renderer},
    {14, "typ_render_object", 0x140, 0x0054a5b0, 0, false, false,
     PayloadClass::Renderer},
    {18, "res_viewport", 0xcc, 0x0053d990, 0, false, false,
     PayloadClass::Renderer},
    {19, "res_animation", 0x90, 0x005389e0, 0, false, false,
     PayloadClass::Renderer},
    {20, "ins_render_sound", 0x94, 0x00549430, 0, false, false,
     PayloadClass::Audio},
    {21, "", 0xb8, 0x0053c3d0, 0, false, false, PayloadClass::Renderer},
};

std::string hex8(uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof text, "%08X", value);
  return text;
}

} // namespace

const NodeTypeInfo *nodeTypeInfo(uint32_t type) {
  for (const auto &row : kTypes)
    if (row.type == type)
      return &row;
  return nullptr;
}

LinkOwner &linkOwner(ResourceNode &node) {
  return node.script ? static_cast<LinkOwner &>(*node.script) : node;
}

ResourceWorld::ResourceWorld(GameWorld &world, PackRegistry &packs)
    : world_(world), packs_(packs) {
  world_.attachResourceWorld(*this);
  destroyObserver_ = world_.addNodeDestroyObserver([this](LinkOwner &owner) {
    const auto it = sceneMaterials_.find(owner.value);
    if (it != sceneMaterials_.end() && it->second->owner.owner == &owner)
      sceneMaterials_.erase(it);
    const auto mesh = sceneMeshes_.find(owner.value);
    if (mesh != sceneMeshes_.end() && mesh->second->owner.owner == &owner)
      sceneMeshes_.erase(mesh);
    const auto tex = sceneTextures_.find(owner.value);
    if (tex != sceneTextures_.end() && tex->second->owner.owner == &owner)
      sceneTextures_.erase(tex);
    const auto light = sceneLights_.find(owner.value);
    if (light != sceneLights_.end() && light->second->owner.owner == &owner)
      sceneLights_.erase(light);
    const auto render = sceneRenders_.find(owner.value);
    if (render != sceneRenders_.end() && render->second->owner.owner == &owner)
      sceneRenders_.erase(render);
  });
}

GroundRunSnapshot ResourceWorld::groundSnapshot() const {
  GroundRunSnapshot out;
  for (const auto &node : nodes_) {
    const auto &factory = node->groundFactory;
    if (!factory)
      continue;
    ++out.cities;
    if (groundRoadTablePublished(*factory))
      ++out.roads;
    out.traffic += static_cast<uint32_t>(factory->traffic.size());
    out.pedestrianTypes +=
        static_cast<uint32_t>(factory->pedestrianTypes.size());
    if (factory->waterActive)
      ++out.waterActive;
    out.waterLimits += static_cast<uint32_t>(factory->waterLimits.size());
    out.fogSettings += factory->fogSetting ? 1u : 0u;
    if (factory->pedestrianDensitySet) {
      ++out.densitySet;
      out.density = factory->pedestrianDensity;
      out.densityHi = factory->pedestrianDensityHi;
    }
  }
  return out;
}

std::vector<ResourceNode *>
ResourceWorld::groundSectorsAt(const std::array<float, 3> &point,
                               float margin) const {
  std::vector<ResourceNode *> found;
  for (const auto &node : nodes_) {
    if (node->type != 1 || !node->groundFactory || !node->loaded())
      continue;
    if (node->bounds && node->bounds->mode == 1) {
      bool inside = true;
      for (size_t axis = 0; axis < 3; ++axis)
        if (std::fabs(point[axis] - node->bounds->values[axis]) >
            node->bounds->values[axis + 3] + margin)
          inside = false;
      if (!inside)
        continue;
    }
    found.push_back(node.get());
  }
  return found;
}

GroundWaterNote ResourceWorld::groundWaterNote() const {
  for (const auto &node : nodes_) {
    const auto &factory = node->groundFactory;
    if (!factory || !factory->waterActive)
      continue;
    return GroundWaterNote{true,
                           factory->waterPosX,
                           factory->waterPosY,
                           factory->waterPosZ,
                           factory->waterExtentA,
                           factory->waterExtentB};
  }
  return {};
}

ResourceWorld::~ResourceWorld() {
  world_.detachResourceWorld(*this);
  world_.removeNodeDestroyObserver(destroyObserver_);
  pendingPackLoads_.clear();
  sceneMaterials_.clear();
  sceneMeshes_.clear();
  sceneRenders_.clear();
  for (ResourceNode *node : topLevel_)
    world_.detachExternal(node->handle);
  for (auto &node : nodes_) {
    world_.uncountInstance(node->countedInstance);
    setLinkOwner(node->sectorLink, nullptr);
    setLinkOwner(node->gameTypeLink, nullptr);
    while (node->head)
      setLinkOwner(*node->head, nullptr);
  }
}

WorldNode *ResourceWorld::addGameChild(ResourceNode &parent,
                                       const std::string &name,
                                       uint32_t flags) {
  bool owned = false;
  for (const auto &node : nodes_)
    if (node.get() == &parent) {
      owned = true;
      break;
    }
  if (!owned || parent.type != 1)
    return nullptr;
  return world_.addChildUnderExternal(parent.handle, linkOwner(parent),
                                      parent.type, parent.sectorLink.owner,
                                      name, flags);
}

bool ResourceWorld::inspectGamePayload(ResourceNode &node,
                                       GameResourcePayload &payload,
                                       std::string &error) {
  return parseGamePayload(node, payload, error, true);
}

bool ResourceWorld::parseGamePayload(ResourceNode &node,
                                     GameResourcePayload &payload,
                                     std::string &error, bool parseTail) {
  if (resolve(&node).packNode != &node || node.type != 1) {
    error = "type-1 payload requires a mounted game node";
    return false;
  }
  PackView view;
  const uint16_t slot = node.payloadSlot;
  if (packs_.view(slot, view, error) != PackViewStatus::Ok)
    return false;
  const ResourceIdMapping mapping{
      slot, static_cast<uint32_t>(packs_.slotCount()), view.dependencies};
  const auto &bytes = *view.bytes;
  const size_t start = node.payloadOffset, length = node.payloadSize;
  if (start > bytes.size() || length > bytes.size() - start) {
    error = "type-1 payload is truncated";
    return false;
  }
  const uint8_t *data = bytes.data() + start;
  size_t pos = 0;
  auto fail = [&] {
    error = "type-1 payload format is unsupported or truncated";
    return false;
  };
  auto u32 = [&](uint32_t &v) {
    if (length - pos < 4)
      return false;
    v = uint32_t(data[pos]) | uint32_t(data[pos + 1]) << 8 |
        uint32_t(data[pos + 2]) << 16 | uint32_t(data[pos + 3]) << 24;
    pos += 4;
    return true;
  };
  GameResourcePayload result;
  uint32_t extra, count;
  if (!u32(extra) || extra > (length - pos) / 4)
    return fail();
  pos += size_t(extra) * 4;
  if (!u32(count) || count > (length - pos) / 23)
    return fail();
  for (uint32_t i = 0; i < count; ++i) {
    RpkEntry entry;
    uint32_t bits;
    uint8_t aliasLen;
    if (!u32(entry.superId) || !u32(entry.typeId) || length - pos < 2)
      return fail();
    entry.resType = data[pos++];
    entry.flags = data[pos++];
    if (!u32(bits) || !u32(entry.payloadOffset) || !u32(entry.payloadSize) ||
        pos == length)
      return fail();
    std::memcpy(&entry.compat, &bits, 4);
    aliasLen = data[pos++];
    if (aliasLen > length - pos)
      return fail();
    entry.alias.assign(reinterpret_cast<const char *>(data + pos), aliasLen);
    pos += aliasLen;
    if (entry.flags & 1) {
      std::array<float, 12> values{};
      for (float &value : values) {
        if (!u32(bits))
          return fail();
        std::memcpy(&value, &bits, 4);
      }
      entry.transform = values;
    }
    const uint32_t childId = remapResourceId(entry.typeId, mapping);
    const uint8_t runtimeFlags = entry.flags | 8;
    result.children.push_back(
        {std::move(entry), childId, node.value, runtimeFlags});
  }
  while (parseTail && pos < length) {
    uint32_t tag, size;
    if (!u32(tag) || !u32(size) || size > length - pos)
      return fail();
    if (tag == 0x00445352) { // RSD\0: text read by 0053dee0
      std::string_view text(reinterpret_cast<const char *>(data + pos), size);
      while (!text.empty()) {
        const size_t end = text.find_first_of("\r\n");
        const auto line = text.substr(0, end);
        size_t split = line.find_first_of(" \t");
        if (split != std::string_view::npos) {
          std::string key(line.substr(0, split));
          for (char &c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          auto value = line.substr(split);
          const size_t first = value.find_first_not_of(" \t");
          value = first == std::string_view::npos ? std::string_view{}
                                                  : value.substr(first);
          const size_t space = value.find_first_of(" \t");
          if (space != std::string_view::npos)
            value = value.substr(0, space);
          if (key == "gametype") {
            const bool hex = value.size() >= 2 && value[0] == '0' &&
                             (value[1] == 'x' || value[1] == 'X');
            if (hex)
              value.remove_prefix(2);
            uint32_t id;
            const auto parsed = std::from_chars(
                value.data(), value.data() + value.size(), id, hex ? 16 : 10);
            if (value.empty() || parsed.ec != std::errc{} ||
                parsed.ptr != value.data() + value.size())
              return fail();
            result.gameType = id;
            result.runtimeGameType = remapResourceId(id, mapping);
            result.actions.push_back(
                {GamePayloadAction::Kind::GameType, *result.runtimeGameType});
          } else if (key == "params") {
            if (value.size() > 2048)
              return fail();
            result.parameters = std::string(value);
          }
        }
        if (end == std::string_view::npos)
          break;
        text.remove_prefix(end + 1);
      }
    } else if (tag == 0x50545845) { // EXTP
      if (size % 4)
        return fail();
      const size_t end = pos + size;
      while (pos < end) {
        uint32_t value;
        if (!u32(value))
          return fail();
        result.externalTypes.push_back(value);
        result.actions.push_back(
            {GamePayloadAction::Kind::ExternalPack, value});
      }
      result.actions.push_back({GamePayloadAction::Kind::ExternalPackEnd, 0});
      continue;
    } else if (tag == 0x53594850 || tag == 0x594c4f50) {
      const size_t end = pos + size;
      auto word = [&](uint32_t &value) { return end - pos >= 4 && u32(value); };
      auto records = [&](auto &out, uint32_t count) {
        using Record = typename std::decay_t<decltype(out)>::value_type;
        if (count >
            (end - pos) / (sizeof(uint32_t) * std::tuple_size<Record>::value))
          return false;
        out.resize(count);
        for (auto &record : out)
          for (auto &value : record)
            if (!word(value))
              return false;
        return true;
      };
      uint32_t count = 0;
      if (tag == 0x53594850) {
        GamePhysicsChunk chunk;
        if (!word(count) || !records(chunk.records, count))
          return fail();
        if (count && (!word(count) || !records(chunk.triples, count)))
          return fail();
        if (pos != end)
          return fail();
        result.geometryChunks.emplace_back(std::move(chunk));
      } else {
        GamePolygonChunk chunk;
        if (!word(chunk.version) || !word(count) || count > (end - pos) / 12)
          return fail();
        chunk.groups.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
          GamePolygonGroup group;
          uint32_t vertices = 0, triangles = 0;
          if (!word(group.kind) || !word(vertices) || !word(triangles) ||
              !records(group.records, vertices) ||
              !records(group.triples, triangles))
            return fail();
          chunk.groups.push_back(std::move(group));
        }
        if (pos != end)
          return fail();
        result.geometryChunks.emplace_back(std::move(chunk));
      }
      continue;
    } else
      return fail();
    pos += size;
  }
  payload = std::move(result);
  error.clear();
  return true;
}

bool ResourceWorld::registerGamePayloadChildren(ResourceNode &node,
                                                std::string &error) {
  if (node.payloadRegistrationFailed) {
    error = "type-1 registration after failure unsupported";
    return false;
  }
  if (node.flags & 0x80000) {
    error = "type-1 child propagation and loading are unsupported";
    return false;
  }
  GameResourcePayload payload;
  if (!parseGamePayload(node, payload, error, false))
    return false;
  const uint16_t slot = node.payloadSlot;
  PackView view;
  if (packs_.view(slot, view, error) != PackViewStatus::Ok)
    return false;
  MountResult result;
  while (node.registeredPayloadChildren < payload.children.size()) {
    const auto &child = payload.children[node.registeredPayloadChildren];
    RpkEntry entry = child.entry;
    entry.flags = child.runtimeFlags;
    bool crashed = false;
    ResourceNode *created =
        createNode(slot, view, entry, result, crashed, &node);
    if (crashed) {
      node.payloadRegistrationFailed = true;
      error = result.detail;
      return false;
    }
    if (created)
      node.payloadChildren.push_back(created);
    ++node.registeredPayloadChildren;
  }
  error.clear();
  return true;
}

bool ResourceWorld::prepareGamePayload(ResourceNode &node,
                                       GameResourcePayload &payload,
                                       std::string &error) {
  GameResourcePayload parsed;
  if (!inspectGamePayload(node, parsed, error))
    return false;
  PackView view;
  if (packs_.view(node.payloadSlot, view, error) != PackViewStatus::Ok)
    return false;
  for (const auto &action : parsed.actions) {
    if (action.kind == GamePayloadAction::Kind::ExternalPack &&
        action.value > view.dependencies.size()) {
      error = "type-1 external pack index is out of range";
      return false;
    }
  }
  if (!registerGamePayloadChildren(node, error))
    return false;
  while (node.preparedPayloadActions < parsed.actions.size()) {
    const auto &action = parsed.actions[node.preparedPayloadActions];
    if (action.kind == GamePayloadAction::Kind::ExternalPack) {
      const uint16_t slot = action.value == 0
                                ? node.payloadSlot
                                : view.dependencies[action.value - 1];
      const MountResult mounted = mountPack(slot);
      if (mounted.state != MountState::Mounted) {
        error = mounted.detail.empty() ? "external pack mount incomplete"
                                       : mounted.detail;
        return false;
      }
    } else if (action.kind == GamePayloadAction::Kind::ExternalPackEnd) {
      const auto childOwners = world_.childOwners(node.handle);
      if (!node.bounds || !childOwners) {
        error = "type-1 EXTP bounds unavailable";
        return false;
      }
      std::vector<ResourceBounds> childBounds;
      childBounds.reserve(childOwners->size());
      for (auto *owner : *childOwners) {
        const auto child = resolve(owner);
        if (!child.packNode || !child.packNode->bounds) {
          error = "type-1 EXTP child bounds unavailable";
          return false;
        }
        childBounds.push_back(*child.packNode->bounds);
      }
      const auto expanded = expandExtpBounds(*node.bounds, childBounds);
      if (!expanded) {
        error = "type-1 EXTP bounds unsupported";
        return false;
      }
      node.bounds = *expanded;
    } else {
      if (node.gameTypeLink.cachedValue != action.value) {
        setLinkOwner(node.gameTypeLink, nullptr);
        if (action.value == 0) {
          ++node.preparedPayloadActions;
          continue;
        }
        const LookupResult found = lookup(action.value, 8);
        if (found.status == LookupStatus::StockCrash ||
            found.status == LookupStatus::MountRefused) {
          error = found.detail;
          return false;
        }
        if (found.status == LookupStatus::NotFound) {
          // Stock leaves the link empty; this preparation stage is not ready
          // until the nonzero game type resolves, and must allow a retry.
          error = "type-1 game type not found";
          return false;
        }
        setLinkOwner(node.gameTypeLink, found.node.owner);
      }
    }
    ++node.preparedPayloadActions;
  }
  payload = std::move(parsed);
  error.clear();
  return true;
}

bool ResourceWorld::loadEmptyGameParent(ResourceNode &node,
                                        std::string &error) {
  if (resolve(&node).packNode != &node || node.type != 1 || node.parent ||
      !world_.defaultParent() || !world_.defaultParent()->loaded) {
    error = "empty game load needs a mounted type-1 child of the loaded root";
    return false;
  }
  if (node.loaded())
    return true;
  PackResource resource;
  if (!packs_.lookup(node.value, resource, error) || !resource.packBytes ||
      resource.entry.payloadSize != 8 ||
      resource.entry.payloadOffset > resource.packBytes->size() ||
      resource.packBytes->size() - resource.entry.payloadOffset < 8 ||
      !std::all_of(
          resource.packBytes->begin() + resource.entry.payloadOffset,
          resource.packBytes->begin() + resource.entry.payloadOffset + 8,
          [](uint8_t byte) { return byte == 0; })) {
    GameResourcePayload payload;
    if (inspectGamePayload(node, payload, error))
      error =
          "type-1 payload needs child, game type and external resource loading";
    return false;
  }
  node.stateWord = (node.stateWord & ~uint32_t{0x50}) | 0x21;
  world_.countInstance(node.countedInstance);
  world_.trackLoadState(linkOwner(node), node.type, true);
  return true;
}

bool ResourceWorld::loadPassiveSpatialGame(ResourceNode &node,
                                           std::string &error) {
  if (resolve(&node).packNode != &node || node.type != 1 ||
      (node.parent
           ? !node.parent->loaded()
           : !world_.defaultParent() || !world_.defaultParent()->loaded)) {
    error = "passive type-1 load needs a loaded parent";
    return false;
  }
  if (node.loaded()) {
    if (node.spatialPayload) {
      error.clear();
      return true;
    }
    error = "type-1 resource already loaded by another factory";
    return false;
  }
  if (node.groundPreparation || node.groundFactory || node.spatialPayload) {
    error = "passive type-1 resource has other preparation";
    return false;
  }
  GameResourcePayload payload;
  auto prepared = std::make_shared<PreparedSpatialGamePayload>();
  if (!inspectGamePayload(node, payload, error) ||
      !prepareSpatialGamePayload(payload, *prepared, error))
    return false;
  if (prepared->polygons && prepared->polygons->groups.size() > UINT32_MAX) {
    error = "passive type-1 polygon count overflow";
    return false;
  }
  if (!payload.actions.empty()) {
    // Stock 0053dee0 processes EXTP mounts and expands the owner bounds.
    // Complete those side effects before publishing a passive loaded node.
    GameResourcePayload applied;
    if (!prepareGamePayload(node, applied, error))
      return false;
  } else if (!registerGamePayloadChildren(node, error)) {
    return false;
  }
  node.polygonCount =
      prepared->polygons
          ? static_cast<uint32_t>(prepared->polygons->groups.size())
          : 0;
  node.spatialPayload = std::move(prepared);
  node.stateWord = (node.stateWord & ~uint32_t{0x50}) | 0x21;
  node.loadPriority = 1.0f;
  world_.countInstance(node.countedInstance);
  world_.trackLoadState(linkOwner(node), node.type, true);
  error.clear();
  return true;
}

bool ResourceWorld::loadListenerGame(ResourceNode &node, std::string &error) {
  if (resolve(&node).packNode != &node || node.type != 1 ||
      (node.parent
           ? !node.parent->loaded()
           : !world_.defaultParent() || !world_.defaultParent()->loaded)) {
    error = "listener type-1 load needs a loaded parent";
    return false;
  }
  if (node.loaded()) {
    error = "listener type-1 resource already loaded";
    return false;
  }
  if (node.groundPreparation || node.groundFactory || node.spatialPayload) {
    error = "listener type-1 resource has other preparation";
    return false;
  }
  node.polygonCount = 0;
  node.spatialPayload = std::make_shared<PreparedSpatialGamePayload>();
  node.stateWord = (node.stateWord & ~uint32_t{0x50}) | 0x21;
  node.loadPriority = 1.0f;
  world_.countInstance(node.countedInstance);
  world_.trackLoadState(linkOwner(node), node.type, true);
  error.clear();
  return true;
}

bool ResourceWorld::unloadPassiveSpatialGame(ResourceNode &node,
                                             std::string &error) {
  if (resolve(&node).packNode != &node || node.type != 1 || !node.loaded() ||
      !node.spatialPayload || node.groundFactory || node.groundPreparation ||
      node.flags & 0x200 || node.recentUseQueued) {
    error = "passive type-1 unload state unsupported";
    return false;
  }
  const auto children = world_.childOwners(node.handle);
  if (!children || !children->empty() || !node.payloadChildren.empty()) {
    error = "passive type-1 unload with children unsupported";
    return false;
  }
  node.stateWord = (node.stateWord & ~uint32_t{0x31}) | 0x40;
  node.loadPriority = 0;
  world_.uncountInstance(node.countedInstance);
  world_.trackLoadState(linkOwner(node), node.type, false);
  node.spatialPayload.reset();
  node.polygonCount.reset();
  error.clear();
  return true;
}

bool ResourceWorld::resetPreparedGamePayload(ResourceNode &node,
                                             std::string &error) {
  if (resolve(&node).packNode != &node || node.type != 1) {
    error = "type-1 reset requires a mounted game resource";
    return false;
  }
  if (node.groundFactory) {
    error = "type-1 reset factory result unsupported";
    return false;
  }
  WorldNode *groundBody = nullptr;
  if (node.groundPreparation && node.groundPreparation->bodyResource.owner) {
    groundBody = world_.nodeFor(node.groundPreparation->bodyResource.owner);
    if (!groundBody || groundBody->type != 2 ||
        world_.parentOwner(groundBody->handle) != &linkOwner(node)) {
      error = "type-1 reset ground body ownership unsupported";
      return false;
    }
    const auto bodyChildren = world_.childOwners(groundBody->handle);
    if (!bodyChildren || !bodyChildren->empty()) {
      error = "type-1 reset ground body has children";
      return false;
    }
  }
  WorldNode *groundRender = nullptr;
  if (node.groundPreparation && node.groundPreparation->renderResource.owner) {
    groundRender = world_.nodeFor(node.groundPreparation->renderResource.owner);
    if (!groundRender || groundRender->type != 3 ||
        world_.parentOwner(groundRender->handle) != &linkOwner(node)) {
      error = "type-1 reset ground render ownership unsupported";
      return false;
    }
    const auto renderChildren = world_.childOwners(groundRender->handle);
    if (!renderChildren || !renderChildren->empty()) {
      error = "type-1 reset ground render has children";
      return false;
    }
  }
  std::vector<ResourceNode *> pending{&node};
  for (size_t i = 0; i < pending.size(); ++i) {
    ResourceNode &current = *pending[i];
    if (current.type != 1 || current.loaded() || current.countedInstance ||
        current.recentUseQueued || current.payloadRegistrationFailed ||
        (current.flags & 0x200)) {
      error = "type-1 reset requires unloaded unretained resources";
      return false;
    }
    const auto children = world_.childOwners(current.handle);
    if (!children ||
        (i != 0 &&
         (children->size() != current.payloadChildren.size() ||
          current.children.size() != current.payloadChildren.size()))) {
      error = "type-1 reset has unowned children at resource " +
              std::to_string(current.value) + " (tree " +
              (children ? std::to_string(children->size()) : "missing") +
              ", registered " + std::to_string(current.payloadChildren.size()) +
              ", packed " + std::to_string(current.children.size()) + ")";
      return false;
    }
    for (ResourceNode *child : current.payloadChildren) {
      if (!child || child->parent != &current ||
          std::find(current.children.begin(), current.children.end(), child) ==
              current.children.end() ||
          std::find(children->begin(), children->end(), &linkOwner(*child)) ==
              children->end() ||
          std::find(pending.begin(), pending.end(), child) != pending.end()) {
        error = "type-1 reset child ownership unsupported";
        return false;
      }
      if (child->value && std::count_if(nodes_.begin(), nodes_.end(),
                                        [child](const auto &entry) {
                                          return entry->value == child->value;
                                        }) != 1) {
        error = "type-1 reset duplicate child id unsupported";
        return false;
      }
      pending.push_back(child);
    }
  }
  if (groundRender && !world_.destroy(*groundRender)) {
    error = "type-1 reset ground render destruction failed";
    return false;
  }
  if (groundBody && !world_.destroy(*groundBody)) {
    error = "type-1 reset ground body destruction failed";
    return false;
  }
  for (size_t i = pending.size(); i-- > 1;) {
    ResourceNode *child = pending[i];
    world_.detachExternal(child->handle);
    setLinkOwner(child->sectorLink, nullptr);
    setLinkOwner(child->gameTypeLink, nullptr);
    while (child->head)
      setLinkOwner(*child->head, nullptr);
    auto &siblings = child->parent->children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), child),
                   siblings.end());
    const auto index = byId_.find(child->value);
    if (index != byId_.end() && index->second == child) {
      byId_.erase(index);
    }
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
                                [child](const auto &entry) {
                                  return entry.get() == child;
                                }),
                 nodes_.end());
  }
  node.groundPreparation.reset();
  node.payloadChildren.clear();
  node.registeredPayloadChildren = 0;
  node.preparedPayloadActions = 0;
  setLinkOwner(node.gameTypeLink, nullptr);
  error.clear();
  return true;
}

bool ResourceWorld::unloadEmptyGameParent(ResourceNode &node,
                                          std::string &error) {
  if (resolve(&node).packNode != &node || node.type != 1 || node.parent ||
      node.flags & 0x200 || node.recentUseQueued ||
      node.registeredPayloadChildren || node.preparedPayloadActions ||
      node.gameTypeLink.owner || node.gameTypeLink.cachedValue ||
      node.groundPreparation || node.groundFactory) {
    error = "type-1 unload state unsupported";
    return false;
  }
  const auto children = world_.childOwners(node.handle);
  if (!children || !children->empty()) {
    error = "type-1 unload with children unsupported";
    return false;
  }
  PackResource resource;
  if (!packs_.lookup(node.value, resource, error) || !resource.packBytes ||
      resource.entry.payloadSize != 8 ||
      resource.entry.payloadOffset > resource.packBytes->size() ||
      resource.packBytes->size() - resource.entry.payloadOffset < 8 ||
      !std::all_of(
          resource.packBytes->begin() + resource.entry.payloadOffset,
          resource.packBytes->begin() + resource.entry.payloadOffset + 8,
          [](uint8_t byte) { return byte == 0; })) {
    error = "type-1 unload payload or factory unsupported";
    return false;
  }
  if (node.loaded()) {
    node.stateWord = (node.stateWord & ~uint32_t{0x31}) | 0x40;
    node.loadPriority = 0.0f;
    world_.uncountInstance(node.countedInstance);
    world_.trackLoadState(linkOwner(node), node.type, false);
  }
  error.clear();
  return true;
}

bool ResourceWorld::loadEmptyResource(ResourceNode &node, std::string &error) {
  if (resolve(&node).packNode != &node || (node.type != 6 && node.type != 8) ||
      (node.type == 8 && !node.script) || node.payloadSize != 0) {
    error = "empty resource load requires a mounted type-6 or type-8 node with "
            "no payload";
    return false;
  }
  if (!node.loaded()) {
    if (node.script)
      node.script->loaded = true;
    node.stateWord = (node.stateWord & ~uint32_t{0x50}) | 0x21;
    node.loadPriority = 1.0f;
    world_.trackLoadState(linkOwner(node), node.type, true);
  }
  error.clear();
  return true;
}

void ResourceWorld::noteScriptLoaded(uint32_t id) {
  const auto found = byId_.find(id);
  if (found == byId_.end() || !found->second->script)
    return;
  ResourceNode &node = *found->second;
  node.stateWord = (node.stateWord & ~uint32_t{0x50}) | 0x21;
  node.loadPriority = 1.0f;
  world_.trackLoadState(linkOwner(node), node.type, true);
}

bool ResourceWorld::setTextureDemand(ResourceNode &node, float demand,
                                     std::string &error) {
  if (resolve(&node).packNode != &node || node.type != 7 ||
      !std::isfinite(demand) || !std::isfinite(node.visibilityBias) ||
      !std::isfinite(node.visibilityGain) ||
      !std::isfinite(node.loadPriority) || node.loadPriority < 0.0f ||
      node.loadPriority > 1.0f) {
    error = "texture demand state unsupported";
    return false;
  }
  const float desired = std::clamp(
      node.visibilityBias + node.visibilityGain * demand, 0.0f, 1.0f);
  const float difference = desired - node.loadPriority;
  const float value =
      difference >= 0.0f
          ? (1.0f - node.loadPriority * 0.9f) * difference * 0.1f +
                node.loadPriority
          : (difference * 0.3f + 1.0f) * node.loadPriority;
  if (!std::isfinite(value) ||
      (node.stamp78 >= world_.frameCounter() && !node.field6c)) {
    error = "texture demand value unavailable";
    return false;
  }
  if (node.stamp78 < world_.frameCounter() || *node.field6c < value)
    node.field6c = value;
  error.clear();
  return true;
}

std::vector<uint32_t> ResourceWorld::cachedTextureIds() const {
  std::vector<uint32_t> ids;
  for (const auto &node : nodes_)
    if (node->type == 7 && node->texture)
      ids.push_back(node->value);
  return ids;
}

std::optional<uint64_t>
ResourceWorld::stockSceneMeshUpperBound(std::string *reason) const {
  if (reason)
    reason->clear();
  uint64_t total = 0;
  size_t runtimeMeshes = 0;
  for (const auto &item : sceneMeshes_) {
    const auto &slot = *item.second;
    if (!slot.owner.owner || !slot.mesh) {
      if (reason)
        *reason = "mesh binding unavailable: " + std::to_string(item.first);
      return std::nullopt;
    }
    bool packNode = false;
    for (const auto &node : nodes_)
      if (node.get() == slot.owner.owner && node->type == 5) {
        packNode = true;
        break;
      }
    if (!packNode)
      ++runtimeMeshes;
    const auto bytes = stockMeshVertexBytesUpperBound(*slot.mesh);
    if (!bytes || *bytes > std::numeric_limits<uint64_t>::max() - total) {
      if (reason)
        *reason = "unsupported mesh version or size: " +
                  std::to_string(item.first) + " (scx version " +
                  std::to_string(slot.mesh->version) + ", " +
                  std::to_string(slot.mesh->meshes.size()) + " meshes" +
                  (slot.mesh->meshes.empty()
                       ? std::string{}
                       : ", first vertexFlags " +
                             std::to_string(
                                 slot.mesh->meshes[0].vertexFlags)) +
                  ")";
      return std::nullopt;
    }
    total += *bytes;
  }
  // Include every runtime type-5 node, not just those on the load lists: recent
  // use moves nodes out of the loaded list.
  if (runtimeMeshes != world_.countNodeType(5)) {
    if (reason)
      *reason = "runtime meshes: bound " + std::to_string(runtimeMeshes) +
                " of " + std::to_string(world_.countNodeType(5));
    return std::nullopt;
  }
  for (const auto &node : nodes_)
    if (node->type == 5 && node->loaded() && node->payloadSize) {
      const auto found = sceneMeshes_.find(node->value);
      if (found == sceneMeshes_.end() ||
          found->second->owner.owner != node.get()) {
        if (reason)
          *reason = "unbound loaded pack mesh: " + std::to_string(node->value);
        return std::nullopt;
      }
    }
  return total;
}

bool ResourceWorld::stockSceneBatchBuffersAbsent() const {
  size_t runtimeMaterials = 0;
  for (const auto &item : sceneMaterials_) {
    const auto &slot = *item.second;
    // FUN_00527950's batch holder (FUN_005093b0) is only created for objects
    // whose flag 0x8000 is clear (stock branch near 00528f00), so 0x8000
    // objects own no batch buffers.
    if (!slot.owner.owner || !slot.material)
      return false;
    bool packNode = false;
    for (const auto &node : nodes_)
      if (node.get() == slot.owner.owner && node->type == 14) {
        packNode = true;
        break;
      }
    if (!packNode)
      ++runtimeMaterials;
  }
  if (runtimeMaterials != world_.countNodeType(14))
    return false;
  for (const auto &node : nodes_)
    if (node->type == 14 && node->loaded() && node->payloadSize) {
      const auto found = sceneMaterials_.find(node->value);
      if (found == sceneMaterials_.end() ||
          found->second->owner.owner != node.get())
        return false;
    }
  return true;
}

bool ResourceWorld::forceRenderPreflight(std::string &error,
                                         std::optional<int32_t> instanceMax,
                                         std::optional<int32_t> soundMax,
                                         std::optional<int32_t> textureMax,
                                         std::optional<int32_t> meshMax) const {
  if (world_.hasNodeFlag(0x06000000u)) {
    error = "pending world resource work unsupported";
    return false;
  }
  for (const auto &node : nodes_)
    if (node->flags & 0x06000000u) {
      error = "pending pack resource work unsupported: id " +
              std::to_string(node->value) + " type " +
              std::to_string(node->type) + " flags " +
              std::to_string(node->flags);
      return false;
    }
  for (uint32_t type : {1u, 5u, 6u, 7u}) {
    bool loaded = world_.hasLoadedNodeType(type);
    size_t nodes = world_.countNodeType(type);
    for (const auto &node : nodes_)
      if (node->type == type && node->loaded())
        loaded = true;
    if (type == 1 && instanceMax && *instanceMax > 0) {
      for (const auto &node : nodes_)
        if (node->type == 1)
          ++nodes;
      // Include the represented creation counter as a conservative allowance.
      const uint64_t upper =
          static_cast<uint64_t>(nodes) + world_.createdInstances();
      if (upper < static_cast<uint64_t>(*instanceMax))
        continue;
    }
    if (type == 5 && meshMax && *meshMax > 0 &&
        stockSceneBatchBuffersAbsent()) {
      const auto upper = stockSceneMeshUpperBound();
      if (upper && *upper < static_cast<uint64_t>(*meshMax))
        continue;
    }
    std::string textureWhy;
    if (type == 7 && !textureMax)
      textureWhy = "no texture limit supplied";
    else if (type == 7 && *textureMax <= 0)
      textureWhy = "texture limit not positive";
    else if (type == 7 && world_.hasLoadedNodeType(7))
      textureWhy = "world has a loaded runtime type-7 node";
    if (type == 7 && textureMax && *textureMax > 0 &&
        !world_.hasLoadedNodeType(7)) {
      size_t families = 0;
      bool known = true;
      for (const auto &node : nodes_)
        if (node->type == 7) {
          if (node->texture) {
            ++families;
            if (!node->texture->allocation) {
              known = false;
              textureWhy =
                  "no allocation record for id " + std::to_string(node->value);
            }
          } else if (node->loaded() && node->payloadSize) {
            known = false;
            textureWhy = "loaded without a host texture: id " +
                         std::to_string(node->value);
          }
        }
      auto represented = [&](LinkOwner *owner) {
        for (const auto &node : nodes_)
          if (node.get() == owner && node->type == 7)
            return true;
        return false;
      };
      for (auto *owner : world_.loadList(7, true))
        if (!represented(owner)) {
          known = false;
          textureWhy = "load-list entry not a pack node";
        }
      for (auto *owner : world_.recentlyUsed(7))
        if (!represented(owner)) {
          known = false;
          textureWhy = "recent-use entry not a pack node";
        }
      const auto upper = textureAllocations_.upperBound();
      if (known && families == textureAllocations_.count() && upper &&
          *upper < static_cast<uint64_t>(*textureMax))
        continue;
      if (known && textureWhy.empty())
        textureWhy = "families " + std::to_string(families) + " vs records " +
                     std::to_string(textureAllocations_.count()) +
                     ", upper bound " +
                     (upper ? std::to_string(*upper) : "unknown") + ", limit " +
                     std::to_string(*textureMax);
    }
    if (type == 6 && soundMax && *soundMax > 0 &&
        !world_.hasLoadedNodeType(6)) {
      uint64_t upper = 0;
      bool known = true;
      for (const auto &node : nodes_)
        if (node->type == 6) {
          if (node->sound) {
            if (!node->soundBufferBytes || !*node->soundBufferBytes)
              known = false;
            else
              upper += *node->soundBufferBytes;
          } else if (node->loaded() && node->payloadSize)
            known = false;
        }
      auto represented = [&](LinkOwner *owner) {
        for (const auto &node : nodes_)
          if (node.get() == owner && node->type == 6)
            return true;
        return false;
      };
      for (auto *owner : world_.loadList(6, true))
        if (!represented(owner))
          known = false;
      for (auto *owner : world_.recentlyUsed(6))
        if (!represented(owner))
          known = false;
      if (known && upper < static_cast<uint64_t>(*soundMax))
        continue;
    }
    if (loaded || (type == 7 && textureAllocations_.count()) ||
        !world_.loadList(type, true).empty() ||
        !world_.recentlyUsed(type).empty()) {
      error = "resource pressure accounting unavailable for loaded type " +
              std::to_string(type);
      for (const auto &node : nodes_)
        if (node->type == type && node->loaded()) {
          error += " (first pack id " + std::to_string(node->value) + ")";
          break;
        }
      if (!textureWhy.empty())
        error += " [" + textureWhy + "]";
      return false;
    }
  }
  error.clear();
  return true;
}

bool ResourceWorld::ageRecentUses(std::string &error, uint32_t maxAge) {
  error.clear();
  struct Pending {
    ResolvedNode node;
  };
  std::vector<Pending> pending;
  const uint32_t frame = world_.frameCounter();
  for (uint32_t type = 1; type < 22; ++type)
    for (auto *owner : world_.recentlyUsed(type)) {
      const auto node = resolve(owner);
      if (!node || node.type != type) {
        error = "recent-use node unavailable";
        return false;
      }
      const auto stamp = node.worldNode  ? node.worldNode->stamp74
                         : node.packNode ? node.packNode->stamp74
                                         : frame;
      if (frame - stamp <= maxAge)
        continue;
      if (type == 1 && node.worldNode) {
        auto &n = *node.worldNode;
        if (!n.loaded || !n.recentUseQueued ||
            (n.stamp78 >= frame && !n.field6c)) {
          error = "recent-use type-1 state unsupported";
          return false;
        }
      } else if (type == 5 && node.worldNode) {
        auto &n = *node.worldNode;
        if (!n.loaded || !n.recentUseQueued ||
            (n.stamp7c >= frame && !n.field6c)) {
          error = "recent-use runtime mesh state unsupported";
          return false;
        }
      } else if (type == 14 && node.worldNode) {
        auto &n = *node.worldNode;
        if (!n.loaded || !n.recentUseQueued ||
            !std::isfinite(n.visibilityBias) ||
            !std::isfinite(n.visibilityGain) ||
            (n.stamp78 >= frame && !n.field6c)) {
          error = "recent-use runtime material state unsupported";
          return false;
        }
      } else if ((type == 5 || type == 6 || type == 7 || type == 8 ||
                  type == 14) &&
                 node.packNode) {
        auto &n = *node.packNode;
        const uint32_t stamp = type == 7 || type == 14 ? n.stamp78 : n.stamp7c;
        if (!n.loaded() || !n.recentUseQueued || (n.stateWord & 0x20) ||
            (stamp >= frame && !n.field6c)) {
          error =
              "recent-use resource state unsupported: " + std::to_string(type);
          return false;
        }
        if (type == 7 && (!n.texture || !std::isfinite(n.visibilityBias) ||
                          !std::isfinite(n.visibilityGain) ||
                          !std::isfinite(n.loadPriority) ||
                          n.loadPriority < 0.0f || n.loadPriority > 1.0f)) {
          error = "recent-use texture state unsupported";
          return false;
        }
      } else {
        // Stock 00537ed0 ages every family 1..21 through the node's own
        // vtable+0x14; script-created runtime nodes enter the same recent
        // list via the FUN_005447d0 tail (all.c:240634-240692). The host
        // models demand bookkeeping only for the types above; other
        // families still leave the recent list like stock's A->B move.
      }
      pending.push_back({node});
    }
  for (auto &item : pending) {
    auto &node = item.node;
    if (node.worldNode) {
      auto &n = *node.worldNode;
      if (node.type == 5) {
        // Type-5 vtable +0x14 is 00544780: set demand to zero, without
        // updating stamp7c. FUN_00537ed0 then moves it to the loaded list.
        if (n.stamp7c < frame || *n.field6c < 0.0f)
          n.field6c = 0.0f;
      } else if (node.type == 14) {
        // 0054a830 clamps bias + gain * zero and does not update stamp78.
        const float demand = std::clamp(n.visibilityBias, 0.0f, 1.0f);
        if (n.stamp78 < frame || *n.field6c < demand)
          n.field6c = demand;
      } else if (node.type == 1) {
        if (n.stamp78 < frame || *n.field6c < n.visibilityBias)
          n.field6c = n.visibilityBias;
        n.stamp78 = frame;
      }
      // Unmodeled families: stock calls the type's own vtable+0x14 here;
      // the host carries no demand field for them, so only the list move.
      n.recentUseQueued = false;
    } else {
      auto &n = *node.packNode;
      if (node.type == 7) {
        if (!setTextureDemand(n, 0.0f, error))
          return false;
      } else if (node.type == 5 || node.type == 6 || node.type == 8 ||
                 node.type == 14) {
        const float visibility = node.type == 14 && n.visibilityBias >= 0.0f
                                     ? std::min(n.visibilityBias, 1.0f)
                                     : 0.0f;
        const uint32_t stamp = node.type == 14 ? n.stamp78 : n.stamp7c;
        if (stamp < frame || *n.field6c < visibility)
          n.field6c = visibility;
      }
      n.stateWord = (n.stateWord & ~uint32_t{0x50}) | 0x20;
      n.recentUseQueued = false;
    }
    world_.finishRecentUse(*node.owner, node.type);
  }
  return true;
}

bool ResourceWorld::bindSceneMaterial(
    const ResolvedNode &node,
    std::shared_ptr<SceneMaterialResources> material) {
  if (!material || !node.owner || node.type != 14)
    return false;
  const auto current = resolve(node.owner);
  if (!current || current.id != node.id || current.type != 14)
    return false;
  auto slot = std::make_unique<SceneMaterialSlot>();
  setLinkOwner(slot->owner, node.owner);
  slot->material = std::move(material);
  sceneMaterials_[node.id] = std::move(slot);
  return true;
}

std::shared_ptr<SceneMaterialResources>
ResourceWorld::sceneMaterial(const ResolvedNode &node) {
  if (!node.owner || node.type != 14)
    return {};
  const auto it = sceneMaterials_.find(node.id);
  if (it == sceneMaterials_.end() || it->second->owner.owner != node.owner)
    return {};
  return it->second->material;
}

void ResourceWorld::unbindSceneMaterial(const ResolvedNode &node) {
  const auto it = sceneMaterials_.find(node.id);
  if (node.owner && it != sceneMaterials_.end() &&
      it->second->owner.owner == node.owner)
    sceneMaterials_.erase(it);
}

bool ResourceWorld::setSceneMaterialRefresh(const ResolvedNode &node,
                                            std::function<void()> refresh) {
  if (!node.owner)
    return false;
  const auto it = sceneMaterials_.find(node.id);
  if (it == sceneMaterials_.end() || it->second->owner.owner != node.owner)
    return false;
  it->second->refresh = std::move(refresh);
  return true;
}

void ResourceWorld::refreshSceneMaterial(const ResolvedNode &node) {
  if (!node.owner)
    return;
  const auto it = sceneMaterials_.find(node.id);
  if (it != sceneMaterials_.end() && it->second->owner.owner == node.owner &&
      it->second->refresh) {
    auto refresh = it->second->refresh;
    refresh();
  }
}

bool ResourceWorld::bindSceneMesh(const ResolvedNode &node,
                                  std::shared_ptr<ScxModel> mesh) {
  if (!mesh || !node.owner || node.type != 5)
    return false;
  const auto current = resolve(node.owner);
  if (!current || current.id != node.id || current.type != 5)
    return false;
  auto slot = std::make_unique<SceneMeshSlot>();
  setLinkOwner(slot->owner, node.owner);
  slot->mesh = std::move(mesh);
  sceneMeshes_[node.id] = std::move(slot);
  return true;
}

std::shared_ptr<ScxModel> ResourceWorld::sceneMesh(const ResolvedNode &node) {
  if (!node.owner || node.type != 5)
    return {};
  const auto it = sceneMeshes_.find(node.id);
  if (it == sceneMeshes_.end() || it->second->owner.owner != node.owner)
    return {};
  return it->second->mesh;
}

bool ResourceWorld::bindSceneTexture(
    const ResolvedNode &node, std::shared_ptr<ResourceTextureCache> texture) {
  if (!texture || !node.owner || node.type != 7)
    return false;
  const auto current = resolve(node.owner);
  if (!current || current.id != node.id || current.type != 7)
    return false;
  auto slot = std::make_unique<SceneTextureSlot>();
  setLinkOwner(slot->owner, node.owner);
  slot->texture = std::move(texture);
  sceneTextures_[node.id] = std::move(slot);
  return true;
}

std::shared_ptr<ResourceTextureCache>
ResourceWorld::sceneTexture(const ResolvedNode &node) {
  if (!node.owner || node.type != 7)
    return {};
  const auto it = sceneTextures_.find(node.id);
  if (it == sceneTextures_.end() || it->second->owner.owner != node.owner)
    return {};
  return it->second->texture;
}

bool ResourceWorld::loadSceneLight(ResourceNode &node, std::string &error) {
  if (resolve(&node).packNode != &node || node.type != 13) {
    error = "light load requires a mounted type-13 resource";
    return false;
  }
  if (node.loaded()) {
    if (!sceneLight(resolve(&node))) {
      error = "loaded light data unavailable";
      return false;
    }
    error.clear();
    return true;
  }
  PackResource resource;
  if (!packs_.lookup(node.value, resource, error))
    return false;
  auto parsed = sceneParseLight(resource);
  if (!parsed.ok) {
    error = parsed.error;
    return false;
  }
  PackView view;
  if (packs_.view(node.payloadSlot, view, error) != PackViewStatus::Ok)
    return false;
  const ResourceIdMapping mapping{
      view.slot, static_cast<uint32_t>(packs_.slotCount()), view.dependencies};
  for (auto *id : {&parsed.light.attHTextureId, &parsed.light.attVTextureId}) {
    if (*id && **id != 0) {
      const uint32_t mapped =
          remapResourceId(static_cast<uint32_t>(**id), mapping);
      if (!mapped) {
        error = "light attenuation texture id cannot be mapped";
        return false;
      }
      **id = static_cast<int32_t>(mapped);
    }
  }
  if (!bindSceneLight(resolve(&node), std::move(parsed.light))) {
    error = "light attenuation texture resource unavailable";
    return false;
  }
  node.stateWord = (node.stateWord & ~uint32_t{0x50}) | 0x21;
  node.loadPriority = 1.0f;
  world_.trackLoadState(linkOwner(node), node.type, true);
  error.clear();
  return true;
}

bool ResourceWorld::bindSceneLight(const ResolvedNode &node,
                                   SceneRenderLight light) {
  if (!node.owner || node.type != 13)
    return false;
  const auto current = resolve(node.owner);
  if (!current || current.id != node.id || current.type != 13)
    return false;
  LinkOwner *attH = nullptr, *attV = nullptr;
  const auto resolveTexture = [&](const std::optional<int32_t> &id,
                                  LinkOwner *&owner) {
    if (!id || *id == 0)
      return true;
    const auto texture = lookup(static_cast<uint32_t>(*id), 7);
    if (texture.status != LookupStatus::Found || !texture.node.owner)
      return false;
    owner = texture.node.owner;
    return true;
  };
  if (!resolveTexture(light.attHTextureId, attH) ||
      !resolveTexture(light.attVTextureId, attV))
    return false;
  auto slot = std::make_unique<SceneLightSlot>();
  setLinkOwner(slot->owner, node.owner);
  setLinkOwner(slot->attH, attH);
  setLinkOwner(slot->attV, attV);
  slot->light = light;
  sceneLights_[node.id] = std::move(slot);
  return true;
}

const SceneRenderLight *ResourceWorld::sceneLight(const ResolvedNode &node) {
  if (!node.owner || node.type != 13)
    return nullptr;
  const auto it = sceneLights_.find(node.id);
  if (it == sceneLights_.end() || it->second->owner.owner != node.owner ||
      (it->second->light.attHTextureId && *it->second->light.attHTextureId &&
       !it->second->attH.owner) ||
      (it->second->light.attVTextureId && *it->second->light.attVTextureId &&
       !it->second->attV.owner))
    return nullptr;
  return &it->second->light;
}

const SceneRenderLight* ResourceWorld::firstSceneLight(uint32_t& kind) const {
  // Deterministic pick: the cfg's configured light (RenderRef.setLight, stock
  // FUN_00486ab0 -> FUN_0048c9d0) first, else the lowest bound handle, like
  // stock's ordered light-record array (FUN_004bb430 stores the array,
  // FUN_004bb4b0 walks it by index). Source kinds 0/4/5 are inactive in stock
  // (FUN_00508890) and stay excluded. Attenuation-texture slots must be
  // loaded, like sceneLight().
  const SceneLightSlot* best = nullptr;
  uint32_t bestHandle = 0xffffffffu;
  for (const auto& entry : sceneLights_) {
    const auto& slot = entry.second;
    if (!slot || !slot->owner.owner || entry.first >= bestHandle)
      continue;
    if (entry.first != configuredLightHandle_ &&
        (slot->light.sourceKind == 0 || slot->light.sourceKind == 4 ||
         slot->light.sourceKind == 5))
      continue;
    if ((slot->light.attHTextureId && *slot->light.attHTextureId &&
         !slot->attH.owner) ||
        (slot->light.attVTextureId && *slot->light.attVTextureId &&
         !slot->attV.owner))
      continue;
    bestHandle = entry.first;
    best = slot.get();
  }
  if (!best)
    return nullptr;
  kind = bestHandle;
  return &best->light;
}

bool ResourceWorld::hasActiveSceneMaterialForMesh(uint32_t meshId) const {
  for (const auto &entry : sceneMaterials_) {
    const auto &slot = *entry.second;
    if (slot.owner.owner && slot.refresh && slot.material &&
        slot.material->meshId() == meshId)
      return true;
  }
  return false;
}

bool ResourceWorld::bindSceneRenderInstance(
    const ResolvedNode &node, std::shared_ptr<SceneRenderInstance> instance) {
  if (!instance || !node.owner || node.type != 3)
    return false;
  const auto current = resolve(node.owner);
  if (!current || current.id != node.id || current.type != 3)
    return false;
  auto slot = std::make_unique<SceneRenderSlot>();
  setLinkOwner(slot->owner, node.owner);
  slot->instance = std::move(instance);
  sceneRenders_[node.id] = std::move(slot);
  return true;
}

std::shared_ptr<SceneRenderInstance>
ResourceWorld::sceneRenderInstance(const ResolvedNode &node) {
  if (!node.owner || node.type != 3)
    return {};
  const auto it = sceneRenders_.find(node.id);
  if (it == sceneRenders_.end() || it->second->owner.owner != node.owner)
    return {};
  return it->second->instance;
}

MountState ResourceWorld::mountState(uint16_t slot) const {
  return slot < states_.size() ? states_[slot].state : MountState::Unmounted;
}

ResolvedNode ResourceWorld::wrap(ResourceNode &node) {
  ResolvedNode result;
  result.owner = &linkOwner(node);
  result.type = node.type;
  result.id = node.value;
  result.packNode = &node;
  result.script = node.script;
  return result;
}

ResolvedNode ResourceWorld::resolve(const LinkOwner *owner) {
  ResolvedNode result;
  if (!owner)
    return result;
  auto *mutableOwner = const_cast<LinkOwner *>(owner);
  if (ScriptNode *script = world_.findScript(owner->value);
      script && static_cast<LinkOwner *>(script) == owner) {
    result.owner = mutableOwner;
    result.type = 8;
    result.id = owner->value;
    result.script = script;
    const auto found = byId_.find(owner->value);
    if (found != byId_.end() && found->second->script == script)
      result.packNode = found->second;
    return result;
  }
  if (WorldNode *node = world_.nodeFor(mutableOwner)) {
    result.owner = mutableOwner;
    result.type = node->type;
    result.id = node->instanceId;
    result.worldNode = node;
    return result;
  }
  const auto found = byId_.find(owner->value);
  if (found != byId_.end() &&
      static_cast<const LinkOwner *>(found->second) == owner)
    return wrap(*found->second);
  return result;
}

void ResourceWorld::attachSector(ResourceNode &node,
                                 const ResourceNode *parentNode,
                                 const WorldNode *parentWorld,
                                 bool parentIsRoot) {
  // FUN_00544fe0(this = node, parent): a parent whose type is not hooked, or a
  // node carrying flag 0x10, makes the node its own sector root (+0xcc =
  // itself, +0xc8 = its id). Otherwise the parent's sector link is copied.
  const uint32_t parentType = parentIsRoot  ? 0
                              : parentNode  ? parentNode->type
                              : parentWorld ? parentWorld->type
                                            : 0;
  if (!sectorHookedType(parentType) || (node.flags & 0x10)) {
    setLinkOwner(node.sectorLink, &node);
    return;
  }
  LinkOwner *inherited = parentNode    ? parentNode->sectorLink.owner
                         : parentWorld ? parentWorld->sectorLink.owner
                                       : nullptr;
  setLinkOwner(node.sectorLink, inherited);
}

ResourceNode *ResourceWorld::createNode(uint16_t slot, const PackView &view,
                                        const RpkEntry &entry,
                                        MountResult &result, bool &crashed,
                                        ResourceNode *explicitParent) {
  const ResourceIdMapping mapping{
      slot, static_cast<uint32_t>(packs_.slotCount()), view.dependencies};
  const uint32_t id = remapResourceId(entry.typeId, mapping);
  const uint32_t parentId = explicitParent
                                ? explicitParent->value
                                : remapResourceId(entry.superId, mapping);

  // FUN_00536900: the node's own pack is mounted first; for a node of the pack
  // being mounted this does nothing.
  const uint32_t ownSlot = id >> 16;
  if (ownSlot != 0xffff && ownSlot < packs_.slotCount() &&
      mountState(static_cast<uint16_t>(ownSlot)) == MountState::Unmounted)
    mountPack(static_cast<uint16_t>(ownSlot));

  const NodeTypeInfo *info = nodeTypeInfo(entry.resType);
  if (!info) {
    // FUN_00536a10 logs at 00619600 and returns 0. Only a type-8 entry passes
    // its alias, so the name is null here.
    log_.push_back("Node registration error: " + view.name + ":" +
                   hex8(id & 0xffff) +
                   ", ((null)) type=" + std::to_string(entry.resType));
    ++result.entriesSkipped;
    return nullptr;
  }

  // Parent: id 0 is the default parent. Otherwise FUN_00536a10 mounts the
  // parent's pack when it has not been mounted (a dependency mount) and asks
  // FUN_00537000 for a node of the child's own type.
  ResolvedNode parent;
  bool parentIsRoot = false;
  if (explicitParent) {
    parent = wrap(*explicitParent);
  } else if (parentId == 0) {
    if (!world_.defaultParent()) {
      result.status = MountStatus::StockCrash;
      result.detail =
          "no default parent: stock reads world+0x11c and dereferences null";
      crashed = true;
      return nullptr;
    }
    parentIsRoot = true;
  } else {
    const uint32_t parentSlot = parentId >> 16;
    if (parentSlot != 0xffff && parentSlot < packs_.slotCount() &&
        mountState(static_cast<uint16_t>(parentSlot)) ==
            MountState::Unmounted) {
      const MountResult dependency =
          mountPack(static_cast<uint16_t>(parentSlot));
      if (dependency.status == MountStatus::StockCrash ||
          dependency.status == MountStatus::Unsupported) {
        result.status = dependency.status;
        result.detail = dependency.detail;
        crashed = true;
        return nullptr;
      }
    }
    parent = findOnly(parentId, info->type, false);
    if (!parent) {
      log_.push_back("Unregistrable node: " + view.name + ":" +
                     hex8(id & 0xffff) + " (invalid parent " +
                     std::to_string(parentSlot) + ":" +
                     hex8(parentId & 0xffff) + ")");
      result.status = MountStatus::StockCrash;
      result.detail =
          "Unregistrable node: parent not found; stock then dereferences null";
      crashed = true;
      return nullptr;
    }
    if (!parent.packNode) {
      result.status = MountStatus::Unsupported;
      result.detail = "parent is a node the pack mount did not create";
      crashed = true;
      return nullptr;
    }
  }

  auto owned = std::make_unique<ResourceNode>();
  ResourceNode &node = *owned;
  node.value = id;
  node.type = info->type;
  node.flags = entry.flags | info->baseFlags;
  node.payloadSlot = slot;
  node.payloadOffset = entry.payloadOffset;
  node.payloadSize = entry.payloadSize;
  node.parentId = parentId;
  node.stamp78 = world_.frameCounter() - 1;
  node.stamp7c = world_.frameCounter() - 1;
  if (info->type == 1)
    node.polygonCount = 0;
  if (world_.frameCounter() != 0) {
    node.field6c = 0.0f;
    node.field70 = 0.0f;
  }
  if (info->type == 8 && !entry.alias.empty())
    node.name = entry.alias.substr(0, 30);
  if (entry.transform && info->takesBounds) {
    ResourceBounds bounds;
    bounds.mode = 1;
    for (size_t i = 0; i < bounds.values.size(); ++i)
      bounds.values[i] = (*entry.transform)[i];
    node.bounds = bounds;
  }
  if (info->type == 8 && id) {
    node.script = world_.findScript(id);
    if (!node.script)
      node.script = world_.addScript(id, std::string());
  }
  const ResourceTreeHandle parentHandle = parent.packNode
                                              ? parent.packNode->handle
                                              : world_.defaultParent()->handle;
  const auto handle = world_.attachExternal(parentHandle, linkOwner(node));
  if (!handle) {
    result.status = MountStatus::Unsupported;
    result.detail = "cannot attach mounted node to world tree";
    crashed = true;
    return nullptr;
  }
  node.handle = *handle;
  world_.trackLoadState(linkOwner(node), node.type, false);
  node.parent = parent.packNode;
  if (parent.packNode)
    parent.packNode->children.push_back(&node);
  else
    topLevel_.push_back(&node);
  if (info->sectorHook)
    attachSector(node, parent.packNode, nullptr, parentIsRoot);

  nodes_.push_back(std::move(owned));
  if (id && !byId_.count(id))
    byId_[id] = &node;
  ++result.nodesCreated;
  return &node;
}

MountResult ResourceWorld::mountPack(uint16_t slot) {
  MountResult result;
  if (slot >= packs_.slotCount()) {
    result.status = MountStatus::Invalid;
    result.detail = "no such pack slot";
    return result;
  }
  if (states_.size() <= slot)
    states_.resize(slot + size_t{1});
  if (states_[slot].state != MountState::Unmounted) {
    result.status = MountStatus::AlreadyMounted;
    result.state = states_[slot].state;
    return result;
  }
  PackView view;
  std::string error;
  const PackViewStatus viewed = packs_.view(slot, view, error);
  if (states_.size() < packs_.slotCount())
    states_.resize(packs_.slotCount());
  if (viewed != PackViewStatus::Ok) {
    result.status = viewed == PackViewStatus::FileMissing
                        ? MountStatus::FileMissing
                        : MountStatus::Invalid;
    result.detail = error;
    return result;
  }
  states_[slot].state = MountState::Mounting;
  result.state = MountState::Mounting;
  bool crashed = false;
  for (const RpkEntry &entry : view.pack->entries) {
    createNode(slot, view, entry, result, crashed);
    if (crashed)
      return result;
  }
  states_[slot].state = MountState::Mounted;
  result.state = MountState::Mounted;
  return result;
}

ResolvedNode ResourceWorld::findOnly(uint32_t id, uint32_t type, bool quiet) {
  ResolvedNode found;
  const auto packed = byId_.find(id);
  if (packed != byId_.end())
    found = wrap(*packed->second);
  else if (WorldNode *node = world_.findInstance(id)) {
    found.owner = node;
    found.type = node->type;
    found.id = node->instanceId;
    found.worldNode = node;
  } else if (ScriptNode *script = world_.findScript(id)) {
    found.owner = script;
    found.type = 8;
    found.id = id;
    found.script = script;
  }
  if (found && type != 0 && found.type != type)
    return {};
  if (!found && !quiet)
    missing_.push_back(id);
  return found;
}

LookupResult ResourceWorld::lookup(uint32_t id, uint32_t type, bool quiet) {
  LookupResult result;
  if (id == 0)
    return result;
  const uint32_t slot = id >> 16;
  if (slot != 0xffff && slot < packs_.slotCount() &&
      mountState(static_cast<uint16_t>(slot)) == MountState::Unmounted) {
    const MountResult mounted = mountPack(static_cast<uint16_t>(slot));
    if (mounted.status == MountStatus::StockCrash) {
      result.status = LookupStatus::StockCrash;
      result.detail = mounted.detail;
      return result;
    }
    if (mounted.status == MountStatus::Invalid ||
        mounted.status == MountStatus::Unsupported) {
      result.status = LookupStatus::MountRefused;
      result.detail = mounted.detail;
      return result;
    }
  }
  result.node = findOnly(id, type, quiet);
  result.status = result.node ? LookupStatus::Found : LookupStatus::NotFound;
  return result;
}

PayloadRequest ResourceWorld::requestPayload(const ResolvedNode &node) const {
  PayloadRequest request;
  if (!node.packNode) {
    request.status = PayloadStatus::Unsupported;
    request.detail = "node has no pack payload";
    return request;
  }
  const NodeTypeInfo *info = nodeTypeInfo(node.type);
  request.status = PayloadStatus::Deferred;
  request.detail = std::string("type ") + std::to_string(node.type) + " (" +
                   (info ? info->systemAlias : "?") +
                   "): the payload loader reached from FUN_005447d0 is not "
                   "modelled; the node keeps only offset " +
                   std::to_string(node.packNode->payloadOffset) + " and size " +
                   std::to_string(node.packNode->payloadSize);
  return request;
}

RefSetResult resourceRefSet(ResourceWorld &resources, OwnerLink &link,
                            uint32_t id) {
  RefSetResult result;
  // FUN_00545fc0 begins by comparing the id word (+8) with the new id, whether
  // or not a node is linked.
  if (link.cachedValue == id)
    return result;
  const bool wasLinked = link.owner != nullptr;
  if (wasLinked)
    setLinkOwner(link, nullptr);
  if (id == 0) {
    result.status = wasLinked ? RefSetStatus::Cleared : RefSetStatus::Unchanged;
    return result;
  }
  const LookupResult looked = resources.lookup(id);
  switch (looked.status) {
  case LookupStatus::StockCrash:
    result.status = RefSetStatus::StockCrash;
    result.detail = looked.detail;
    return result;
  case LookupStatus::MountRefused:
    result.status = RefSetStatus::MountRefused;
    result.detail = looked.detail;
    return result;
  case LookupStatus::NotFound:
    result.status = RefSetStatus::NotFound;
    return result;
  case LookupStatus::Found:
    break;
  }
  setLinkOwner(link, looked.node.owner);
  link.cachedValue = id;
  result.status = RefSetStatus::Linked;
  result.node = looked.node;
  return result;
}
