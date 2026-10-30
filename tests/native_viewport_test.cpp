#include "natives/game_type.hpp"
#include "natives/registry.hpp"

#include <cstdio>
#include <memory>
#include <string>

static int failures = 0;
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #condition);                 \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

int main() {
  TufaClass cls;
  cls.name = "java.util.resource.ResourceRef";
  cls.hasClassHeader = true;
  std::vector<const TufaClass *> classes{&cls};
  GameWorld world;
  CHECK(world.addRoot());
  VmGameState game;
  game.world = &world;
  VmArena arena;
  arena.gameState = &game;
  VmDisplaySize display{800, 600};
  arena.displaySize = &display;
  auto object = std::make_unique<VmObject>();
  object->runtimeClass = &cls;
  object->fields.push_back({"ptr", int32_t{0}, "I"});
  const auto *receiver = object.get();
  arena.objects.push_back(std::move(object));
  const auto call = [&](const char *name, const char *descriptor,
                        std::vector<VmValue> args = {}) {
    return invokeNative("java.render.Viewport", name, descriptor, args,
                        receiver, &arena, &classes);
  };
  const auto ok = [](const NativeResult &value) {
    return std::holds_alternative<std::monostate>(value);
  };
  const auto get = [&](const char *name) {
    const auto value = call(name, "()F");
    const auto *result = std::get_if<VmValue>(&value);
    CHECK(result && std::holds_alternative<float>(*result));
    return result && std::holds_alternative<float>(*result)
               ? std::get<float>(*result)
               : 0.0f;
  };
  CHECK(get("getAspect") == 1.0f);
  CHECK(std::holds_alternative<std::string>(
      call("create", "(IFFFF)V", {int32_t{3}, 0.1f, 0.2f, 0.5f, 0.8f})));
  CHECK(ok(invokeNative("java.util.resource.ResourceRef", "newNative", "()V",
                        {}, receiver, &arena, &classes)));
  CHECK(ok(call("create", "(IFFFF)V", {int32_t{3}, 0.1f, 0.2f, 0.5f, 0.8f})));
  auto &binding = *game.bindings.at(1);
  auto *node = world.nodeFor(binding.link.owner);
  CHECK(node && node->type == 0x12 && node->viewport &&
        node->viewport->flags == 3);
  CHECK(node && node->viewport->list == ViewportList::Pending);
  CHECK(get("getLeft") == 0.1f && get("getTop") == 0.2f);
  CHECK(get("getWidth") == 0.5f && get("getHeight") == 0.8f);
  CHECK(get("getAspect") == static_cast<float>(4.0 / 3.0));
  CHECK(ok(call("resize", "(FFFF)V", {0.3f, 0.4f, 0.6f, 0.7f})));
  CHECK(get("getLeft") == 0.3f && get("getTop") == 0.4f);
  CHECK(get("getWidth") == 0.6f && get("getHeight") == 0.7f);
  CHECK(ok(call("activate", "(I)V", {int32_t{42}})));
  CHECK(node && node->viewport->list == ViewportList::Active);
  CHECK(world.activeViewports() == std::vector<WorldNode *>({node}));
  CHECK(ok(call("activate", "(I)V", {int32_t{0}})));
  CHECK(ok(call("deactivate", "()V")));
  CHECK(node && node->viewport->list == ViewportList::Removed);
  CHECK(world.activeViewports().empty());
  CHECK(ok(call("activate", "(I)V", {int32_t{0}})));
  CHECK(node && node->viewport->list == ViewportList::Removed);
  CHECK(ok(call("create", "(IFFFF)V", {int32_t{1}, 1.0f, 2.0f, 3.0f, 4.0f})));
  auto *replacement = world.nodeFor(binding.link.owner);
  // Type 18 has no sector hook; deactivation and rebinding remove both links.
  CHECK(replacement && replacement != node && node->head == nullptr);
  CHECK(get("getWidth") == 3.0f);
  CHECK(ok(call("destroy", "()V")) && binding.link.owner == nullptr);
  CHECK(arena.resourceStates[0].words[2] == 0);
  CHECK(std::holds_alternative<std::string>(call("getWidth", "()F")));
  CHECK(ok(call("resize", "(FFFF)V", {1.0f, 2.0f, 3.0f, 4.0f})));
  CHECK(std::holds_alternative<std::string>(call("resize", "(FFFF)V", {1.0f})));
  GameWorld ordering;
  auto *orderRoot = ordering.addRoot();
  auto *later = ordering.addTypedChild(*orderRoot, 18, "later", 0);
  auto *earlier = ordering.addTypedChild(*orderRoot, 18, "earlier", 0);
  if (!later || !earlier) return 1;
  later->viewport = ViewportInstance{5, 0, 0, 1, 1, ViewportList::Pending, {}};
  earlier->viewport = ViewportInstance{2, 0, 0, 1, 1, ViewportList::Pending, {}};
  ordering.registerViewport(*later, 5);
  ordering.registerViewport(*earlier, 2);
  CHECK(ordering.activateViewport(*later)); later->viewport->list = ViewportList::Active;
  CHECK(ordering.activateViewport(*earlier)); earlier->viewport->list = ViewportList::Active;
  CHECK(ordering.activeViewports() == std::vector<WorldNode *>({earlier, later}));
  ordering.deactivateViewport(*earlier); earlier->viewport->list = ViewportList::Removed;
  CHECK(ordering.activeViewports() == std::vector<WorldNode *>({later}));
  if (failures)
    return 1;
  std::puts("Viewport native tests passed");
  return 0;
}
