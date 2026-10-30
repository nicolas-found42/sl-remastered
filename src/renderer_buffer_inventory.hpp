#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

// Inventory for the paired dynamic vertex buffers allocated by 00500ad0.
// A pool corresponds to one stock batching object, not to a whole frame.
class RendererBufferInventory {
public:
  struct MeshSize { uint32_t vertices; uint32_t primitives; };
  RendererBufferInventory(uint32_t vertexCapacity, uint32_t primitiveCapacity,
                          uint32_t vertexStride);

  // Add a previously unregistered stock batch member. Oversized meshes are unsupported:
  // the stock allocation path does not establish a safe bounded fit for them.
  bool submit(uint64_t id, MeshSize size);
  void remove(uint64_t id);
  std::optional<uint64_t> allocatedVertexBytes() const;
  size_t bufferPairs() const { return buffers_.size(); }

private:
  struct Buffer { uint32_t vertices; uint32_t primitives; };
  uint32_t vertexCapacity_, primitiveCapacity_, vertexStride_;
  std::vector<Buffer> buffers_;
  std::unordered_map<uint64_t, size_t> locations_;
  std::unordered_map<uint64_t, MeshSize> sizes_;
};
