#pragma once

#include "object_config.hpp"
#include "object_physics.hpp"
#include "object_sound.hpp"
#include "resource_world.hpp"
#include "world_model.hpp"

#include <memory>
#include <string>
#include <vector>

struct VmArena;

namespace object {

// Runtime of a mounted type-1 ICFG object instance, owned by ResourceNode.
// Loading and rendering mutate this only; the host loader does not touch the
// prototype/config once construction has returned.
class ObjectRuntime {
public:
  explicit ObjectRuntime(object_config::ObjectConfig config,
                         std::shared_ptr<ObjectPhysics> physics)
      : config(std::move(config)), physics(std::move(physics)),
        damageRemaining(this->config.damage.root) {}
  ~ObjectRuntime();
  ObjectRuntime(const ObjectRuntime &) = delete;
  ObjectRuntime &operator=(const ObjectRuntime &) = delete;

  object_config::ObjectConfig config;
  std::shared_ptr<ObjectPhysics> physics;
  // Stock instance +0x58, initialized from class damage.root by 00465bd0.
  float damageRemaining;
  struct SlotResources {
    OwnerLink primary;
    std::vector<std::unique_ptr<OwnerLink>> variants;
    ~SlotResources();
  };
  struct MaterialResources {
    OwnerLink collisionSound;
    OwnerLink resource;
    OwnerLink impactSound;
    float threshold = 0;
    ~MaterialResources();
  };
  struct Render {
    size_t slot = 0;
    OwnerLink node;
    ::physics::Vec3 localPosition{};
    ::physics::Mat34 localRotation{};
    ~Render();
  };
  std::vector<std::unique_ptr<SlotResources>> resources;
  std::vector<std::unique_ptr<SlotResources>> soundResources;
  std::unique_ptr<MaterialResources> material;
  std::unique_ptr<ObjectSoundRuntime> sound;
  std::vector<std::unique_ptr<Render>> renders;
  ::physics::Body *root = nullptr; // Borrowed from GameWorld's physics handle.
  ::physics::Vec3 initialRootPosition{};
  ::physics::Mat34 initialRootRotation{};
  int lod = -1;
};

// Constructs a mounted type-1 ICFG object instance. Replaces the generic
// object unsupported error once all reachable semantics are implemented. Any
// directive whose stock semantics are not reachable fails before mutating
// the resource, and the returned error names the unsupported directive.
std::optional<std::string>
loadObjectGameResource(VmArena &arena, ResourceNode &node,
                       const PackResource &typeResource,
                       GameResourcePayload &payload);

// Links the three stock 00465150 material resources without loading sounds.
// A lookup failure leaves the runtime's previous material unchanged.
bool bindObjectMaterial(ResourceWorld &resources,
                        const object_config::MaterialDirective &directive,
                        ObjectRuntime &runtime, std::string &error);

// Reconciles 004661b0 render children. Conditional slots require the live
// game clock; existing slots are removed by LOD, not by a clock change alone.
bool updateObjectLod(ResourceWorld &resources, ResourceNode &node,
                     float visibility, std::string &error,
                     std::optional<int32_t> gameTime = std::nullopt);

} // namespace object
