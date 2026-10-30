#include "resource_texture_cache.hpp"

#include "render2d.hpp"
#include "run_log.hpp"
#include "resource_texture.hpp"
#include "resource_world.hpp"

#include <memory>

bool cacheResourceTexture(ResourceWorld& resources, Render2D& renderer, const ResolvedNode& resolved, float demand, std::string& error) {
	if (!resolved ||
	    resolved.type != 7 || !resolved.packNode) {
		error = "texture renderer or resource unavailable"; return false;
	}
	auto& node = *resolved.packNode;
	if (node.flags & 0x2000000u) { error = "pending texture load unsupported"; return false; }
	if (node.loaded() && (!node.texture || node.texture->renderer != &renderer)) {
		error = "unsupported loaded texture"; return false;
	}
	if (node.texture && node.loadPriority != 0.0f) { error = "texture LOD refresh unsupported"; return false; }
	if (!resources.setTextureDemand(node, demand, error)) return false;
	if (!node.texture) {
		if (node.loaded() || node.loadPriority != 0.0f) { error = "texture initial state unsupported"; return false; }
		PackResource resource;
		if (!resources.packs().lookup(node.value, resource, error) || !resource.packBytes) return false;
		auto cache = std::make_shared<ResourceTextureCache>();
		if (!resourceTextureLoad(*resource.packBytes, static_cast<uint16_t>(node.value & 0xffff),
		                         resources.packs().root(), cache->pixels, error)) return false;
		auto handle = renderer.texture(cache->pixels.width, cache->pixels.height,
		                                     cache->pixels.rgba.data(), cache->pixels.rgba.size(), error);
		if (!handle) return false;
		// Stock's loader 00506650 creates one device texture through D3DX and adds its levels to the
		// counter DAT_0065c234 (slot +0x4c; +0x50 is only ever zeroed in this class family). The
		// ledger holds that slot for the device the host presents. Dimensions or caps outside the
		// verified subset record "unknown", which fails closed in the pressure preflight.
		std::optional<uint64_t> slotBytes;
		if (const uint32_t maxSize = renderer.maxTextureSize())
			if (const auto shape = stockDeviceTextureShape(cache->pixels.width, cache->pixels.height,
			                                               hostDeviceTextureCaps(maxSize)))
				slotBytes = stockTextureSlotUpperBound(*shape);
		cache->allocation = resources.textureAllocations().track(slotBytes);
		cache->renderer = &renderer;
		cache->handle = std::shared_ptr<size_t>(new size_t(*handle), [renderer = &renderer](size_t* id) {
			std::string ignored; renderer->releaseTexture(*id, ignored); delete id;
		});
		node.visibilityBias = cache->pixels.lodBias.value_or(0.0f);
		node.visibilityGain = cache->pixels.lodAmplitude.value_or(1.0f);
		runlog::event(runlog::EventKind::Resource, "texture 0x%08x cached %ux%u", static_cast<unsigned>(node.value),
		              static_cast<unsigned>(cache->pixels.width), static_cast<unsigned>(cache->pixels.height));
		node.texture = std::move(cache);
		node.stateWord = (node.stateWord & ~uint32_t{0x50}) | 0x21;
		resources.world().trackLoadState(node, node.type, true);
	}
	if (node.stamp74 != resources.world().frameCounter() && !node.recentUseQueued)
		node.stateWord = (node.stateWord & ~uint32_t{0x60}) | 0x10;
	resources.world().noteRecentUse(*resolved.owner, node.type, node.stamp74, node.recentUseQueued);
	error.clear(); return true;
}

namespace {

bool texturePixelsFromSource(ResourceWorld &world, const ResolvedNode &source,
                             ResourceTexturePixels &pixels, std::string &error) {
  if (source.packNode && source.packNode->texture) {
    pixels = source.packNode->texture->pixels;
    return true;
  }
  const auto bound = world.sceneTexture(source);
  if (bound) {
    pixels = bound->pixels;
    return true;
  }
  if (!source.packNode) {
    error = "source texture unavailable";
    return false;
  }
  PackResource resource;
  if (!world.packs().lookup(source.packNode->value, resource, error) ||
      !resource.packBytes) {
    error = "source texture pack lookup failed";
    return false;
  }
  return resourceTextureLoad(*resource.packBytes,
                             static_cast<uint16_t>(source.packNode->value & 0xffff),
                             world.packs().root(), pixels, error);
}

} // namespace

WorldNode *sceneCloneTexture(ResourceWorld &world, Render2D &renderer,
                             const ResolvedNode &source, std::string &error) {
  if (source.type != 7) {
    error = "source texture unavailable";
    return nullptr;
  }
  ResourceTexturePixels pixels;
  if (!texturePixelsFromSource(world, source, pixels, error))
    return nullptr;
  ResourceTreeHandle handle{};
  uint32_t flags = 0;
  float priority = 0;
  float bias = 0;
  float gain = 1;
  if (source.packNode) {
    handle = source.packNode->handle;
    flags = source.packNode->flags;
    priority = source.packNode->loadPriority;
    bias = source.packNode->visibilityBias;
    gain = source.packNode->visibilityGain;
  } else if (source.worldNode) {
    handle = source.worldNode->handle;
    flags = source.worldNode->flags;
    priority = source.worldNode->loadPriority;
    bias = source.worldNode->visibilityBias;
    gain = source.worldNode->visibilityGain;
  } else {
    error = "source texture node unavailable";
    return nullptr;
  }
  auto cache = std::make_shared<ResourceTextureCache>();
  cache->pixels = pixels;
  auto texHandle = renderer.texture(pixels.width, pixels.height, pixels.rgba.data(),
                                    pixels.rgba.size(), error);
  if (!texHandle)
    return nullptr;
  std::optional<uint64_t> slotBytes;
  if (const uint32_t maxSize = renderer.maxTextureSize())
    if (const auto shape = stockDeviceTextureShape(pixels.width, pixels.height,
                                                   hostDeviceTextureCaps(maxSize)))
      slotBytes = stockTextureSlotUpperBound(*shape);
  cache->allocation = world.textureAllocations().track(slotBytes);
  cache->renderer = &renderer;
  cache->handle = std::shared_ptr<size_t>(
      new size_t(*texHandle), [renderer = &renderer](size_t *id) {
        std::string ignored;
        renderer->releaseTexture(*id, ignored);
        delete id;
      });
  const auto parent = world.resolve(world.world().parentOwner(handle));
  WorldNode *node = nullptr;
  if (parent.worldNode)
    node = world.world().addTypedChild(*parent.worldNode, 7, "", flags | 0x200);
  else if (parent.packNode)
    node = world.world().addTypedChildUnderExternal(
        parent.packNode->handle, *parent.owner, parent.type,
        parent.packNode->sectorLink.owner, 7, "", flags | 0x200);
  else if (world.world().defaultParent())
    node = world.world().addTypedChild(*world.world().defaultParent(), 7, "",
                                       flags | 0x200);
  if (!node) {
    error = "texture clone parent or allocation unavailable";
    return nullptr;
  }
  node->loadPriority = priority;
  node->visibilityBias = bias;
  node->visibilityGain = gain;
  node->field6c = priority;
  ResolvedNode target{node, 7, node->instanceId, nullptr, node, nullptr};
  if (!world.bindSceneTexture(target, std::move(cache))) {
    world.world().destroy(*node);
    error = "texture clone binding failed";
    return nullptr;
  }
  world.world().markLoaded(*node);
  error.clear();
  return node;
}
