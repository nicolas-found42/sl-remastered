#include "ground_parameters.hpp"
#include "resource_world.hpp"
#include <cassert>

int main() {
  PackRegistry registry("ref/raw");
  GameWorld world;
  assert(world.addRoot());
  ResourceWorld resources(world, registry);
  uint16_t slot = 0;
  std::string error;
  assert(registry.registerPackName("maps\\city.rpk", slot, error));
  auto found = resources.lookup((uint32_t(slot) << 16) | 1, 1);
  assert(found.node.packNode);
  GameResourcePayload payload;
  assert(resources.inspectGamePayload(*found.node.packNode, payload, error));
  assert(payload.parameters);
  GroundParameters result;
  assert(parseGroundParameters(*payload.parameters, result, error));
  assert(result.resourceId == 0x10);
  assert((result.position == std::array<float, 3>{0, 0, 0}));
  assert((result.extraScalars == std::array<float, 3>{0, 0, 0}));
  assert(result.materialPath == "maps\\city\\meshes\\city.scm");
  assert(result.roadPath == "maps\\city\\ai\\city.trc");
  assert(!parseGroundParameters("0x10,0,0,0,0,0,0," + std::string(256, 'a') + ",road", result, error));
  uint16_t city2Slot = 0;
  assert(registry.registerPackName("maps\\city2.rpk", city2Slot, error));
  auto city2 = resources.lookup((uint32_t(city2Slot) << 16) | 1, 1);
  assert(city2.node.packNode);
  GameResourcePayload city2Payload;
  assert(resources.inspectGamePayload(*city2.node.packNode, city2Payload, error));
  assert(city2Payload.parameters);
  assert(parseGroundParameters(*city2Payload.parameters, result, error));
  assert(result.materialPath == "maps\\city2\\meshes\\city2.scm");
  assert(result.roadPath.empty());
  // Truncation at a field boundary: stock sscanf leaves the zeroed floats.
  assert(parseGroundParameters("0x00000002", result, error));
  assert(result.resourceId == 2 && result.position[2] == 0 &&
         result.materialPath.empty());
  assert(parseGroundParameters("0x10,1,2", result, error));
  assert(result.position[1] == 2 && result.position[2] == 0);
  assert(!parseGroundParameters("0x10,1,x", result, error));
  assert(parseGroundParameters("0x00000005,0,0,0,0,0,0", result, error));
  assert(result.resourceId == 5 && result.materialPath.empty() &&
         result.roadPath.empty());
  assert(!parseGroundParameters("0x10,0,0,0,0,0,0,material,road extra", result, error));
  assert(result.resourceId == 5);
  assert(parseGroundParameters("0xff,-1.5,2,3,4,5,6,,", result, error));
  assert(result.resourceId == 255 && result.position[0] == -1.5f);
  assert(!parseGroundParameters("10,0,0,0,0,0,0,,", result, error));
  assert(!parseGroundParameters("0x10,0.1,0,0,0,0,0,,", result, error));
  assert(!parseGroundParameters("0x10,8388608,0,0,0,0,0,,", result, error));
  assert(result.materialPath.empty() && result.roadPath.empty());
}
