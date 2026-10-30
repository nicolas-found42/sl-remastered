#include "renderer_buffer_inventory.hpp"

#include <limits>

RendererBufferInventory::RendererBufferInventory(uint32_t vertexCapacity,
    uint32_t primitiveCapacity, uint32_t vertexStride)
    : vertexCapacity_(vertexCapacity), primitiveCapacity_(primitiveCapacity),
      vertexStride_(vertexStride) {}

bool RendererBufferInventory::submit(uint64_t id, MeshSize size) {
  if (!vertexCapacity_ || !primitiveCapacity_ || !vertexStride_ ||
      size.vertices >= vertexCapacity_ || size.primitives >= primitiveCapacity_ ||
      locations_.count(id))
    return false;
  // 00500e90 first ensures a buffer exists, then chooses the buffer with
  // greatest free vertex capacity. Equality is insufficient for a fit.
  if (buffers_.empty()) buffers_.push_back({vertexCapacity_, primitiveCapacity_});
  size_t chosen = 0;
  for (size_t i = 1; i < buffers_.size(); ++i)
    if (buffers_[i].vertices > buffers_[chosen].vertices) chosen = i;
  if (buffers_[chosen].vertices <= size.vertices ||
      buffers_[chosen].primitives <= size.primitives) {
    chosen = buffers_.size();
    buffers_.push_back({vertexCapacity_, primitiveCapacity_});
  }
  buffers_[chosen].vertices -= size.vertices;
  buffers_[chosen].primitives -= size.primitives;
  locations_.emplace(id, chosen);
  sizes_.emplace(id, size);
  return true;
}

void RendererBufferInventory::remove(uint64_t id) {
  const auto it = locations_.find(id);
  if (it == locations_.end()) return;
  const auto size = sizes_.at(id);
  auto &buffer = buffers_[it->second];
  buffer.vertices += size.vertices;
  buffer.primitives += size.primitives;
  sizes_.erase(id);
  locations_.erase(it);
}

std::optional<uint64_t> RendererBufferInventory::allocatedVertexBytes() const {
  const uint64_t oneBuffer = uint64_t{vertexCapacity_} * vertexStride_;
  if (oneBuffer > std::numeric_limits<uint64_t>::max() / 2) return std::nullopt;
  const uint64_t pairBytes = oneBuffer * 2;
  if (pairBytes && buffers_.size() > std::numeric_limits<uint64_t>::max() / pairBytes)
    return std::nullopt;
  return pairBytes * buffers_.size();
}
