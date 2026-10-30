#include "renderer_buffer_inventory.hpp"
#include <cassert>

int main() {
  RendererBufferInventory pool(1024, 1024, 56);
  assert(pool.allocatedVertexBytes() == 0);
  assert(pool.submit(1, {600, 300}));
  assert(pool.allocatedVertexBytes() == 114688);
  assert(pool.submit(2, {424, 100})); // exact fit allocates a second pair
  assert(pool.bufferPairs() == 2);
  assert(pool.submit(3, {100, 100}));
  assert(!pool.submit(3, {1, 1}));
  pool.remove(2);
  assert(pool.submit(4, {500, 200})); // freed capacity reuses the pair
  assert(pool.bufferPairs() == 2);
  assert(!pool.submit(5, {1024, 1}));
  assert(pool.allocatedVertexBytes() == 229376);
  RendererBufferInventory fragmented(100, 100, 24);
  assert(fragmented.submit(1, {60, 10}));
  assert(fragmented.submit(2, {40, 70}));
  assert(fragmented.submit(3, {20, 30})); // largest vertex remainder wins
  assert(fragmented.bufferPairs() == 3); // its primitive space is an exact fit
  fragmented.remove(999);
  assert(fragmented.allocatedVertexBytes() == 14400);
  RendererBufferInventory invalid(0, 0, 0);
  assert(!invalid.submit(1, {1, 1}));
}
