// ResourceRef.set(I)V (handler 0047cf80) through the native registry, against
// the real installed packs. The receiver is a synthetic ResourceRef object so
// each argument and state is exact; the VM path with the real Frontend class is
// in vm_class_init_test.cpp.
#include "natives/game_type.hpp"
#include "natives/registry.hpp"
#include "pack_registry.hpp"
#include "render2d.hpp"
#include "resource_sound.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"
#include "sfx.hpp"
#include "world_model.hpp"
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

static int failures = 0;
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::printf("FAIL line %d: %s\n", __LINE__, #c);                         \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

namespace {
TufaClass refClass() {
  TufaClass cls;
  cls.name = "java.util.resource.ResourceRef";
  cls.hasClassHeader = true;
  return cls;
}
struct Host {
  TufaClass cls = refClass();
  std::vector<const TufaClass *> classes{&cls};
  VmArena arena;
  std::string error;

  const VmObject *make(bool withPtr = true) {
    auto object = std::make_unique<VmObject>();
    object->runtimeClass = &cls;
    if (withPtr)
      object->fields.push_back({"ptr", int32_t{0}, "I"});
    arena.objects.push_back(std::move(object));
    return arena.objects.back().get();
  }
  NativeResult newNative(const VmObject *receiver) {
    return invokeNative("java.util.resource.ResourceRef", "newNative", "()V",
                        {}, receiver, &arena, &classes);
  }
  NativeResult set(const VmObject *receiver, uint32_t id) {
    int32_t raw;
    std::memcpy(&raw, &id, sizeof raw);
    return invokeNative("java.util.resource.ResourceRef", "set", "(I)V",
                        {VmValue{raw}}, receiver, &arena, &classes);
  }
  NativeResult id(const VmObject *receiver) {
    return invokeNative("java.util.resource.ResourceRef", "id", "()I", {},
                        receiver, &arena, &classes);
  }
};
bool isId(const NativeResult &result, uint32_t id) {
  const auto *value = std::get_if<VmValue>(&result);
  if (!value)
    return false;
  const auto *raw = std::get_if<int32_t>(value);
  if (!raw)
    return false;
  uint32_t bits;
  std::memcpy(&bits, raw, sizeof bits);
  return bits == id;
}
std::string errorOf(const NativeResult &result) {
  const auto *text = std::get_if<std::string>(&result);
  return text ? *text : std::string();
}
bool ok(const NativeResult &result) {
  return std::holds_alternative<std::monostate>(result);
}
bool contains(const std::string &text, const char *part) {
  return text.find(part) != std::string::npos;
}
} // namespace

int main() {
  PackRegistry packs("ref/raw");
  std::string error;
  uint32_t systemDummy = 0, systemMissing = 0, loading = 0, systemGame = 0,
           rectangle = 0, sound = 0, emptyScript = 0, emptySound = 0;
  CHECK(packs.resolveLiteral("system.rpk", 0x1b, systemDummy, error));
  CHECK(packs.resolveLiteral("sound.rpk", 0x1d, sound, error));
  CHECK(packs.resolveLiteral("humans.rpk", 0x6b, emptyScript, error));
  CHECK(packs.resolveLiteral("humans.rpk", 0x4, emptySound, error));
  CHECK(packs.resolveLiteral("system.rpk", 0x4, systemGame, error));
  CHECK(packs.resolveLiteral("system.rpk", 0x7e, systemMissing, error));
  CHECK(packs.resolveLiteral("frontend.rpk", 0xa8, loading, error));
  CHECK(packs.resolveLiteral("frontend.rpk", 0xd7, rectangle, error));

  {
    GameWorld world;
    CHECK(world.addRoot());
    ResourceWorld resources(world, packs);
    VmGameState game{&packs, &world, {}, {}, &resources};
    Host host;
    host.arena.gameState = &game;
    uint16_t slot = 0;
    CHECK(packs.registerPackName("maps\\city.rpk", slot, error));
    const uint32_t city = (uint32_t(slot) << 16) | 1;
    const VmObject *ref = host.make();
    CHECK(ok(host.newNative(ref)));
    CHECK(ok(host.set(ref, systemGame)));
    auto empty = resources.lookup(systemGame, 1);
    CHECK(empty.node.packNode && !empty.node.packNode->loaded());
    CHECK(ok(invokeNative("java.util.resource.ResourceRef", "load", "()V",
                          {}, ref, &host.arena, &host.classes)));
    CHECK(empty.node.packNode && empty.node.packNode->loaded());
    CHECK(ok(host.set(ref, city)));
    auto node = resources.lookup(city, 1);
    CHECK(node.node.packNode && !node.node.packNode->loaded());
    CHECK(node.node.packNode && node.node.packNode->parent == empty.node.packNode &&
          empty.node.packNode->loaded());
    GameResourcePayload payload;
    CHECK(node.node.packNode &&
          resources.inspectGamePayload(*node.node.packNode, payload, error));
    CHECK(!payload.children.empty() && payload.gameType.has_value());
    auto result = invokeNative("java.util.resource.ResourceRef", "load", "()V",
                               {}, ref, &host.arena, &host.classes);
    CHECK(!ok(result) && !errorOf(result).empty());
    CHECK(node.node.packNode && !node.node.packNode->loaded());
  }

  // Exercise child wrappers using the real class hierarchy and mounted tree.
  {
    auto base =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/ResourceRef.class");
    auto game =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/GameRef.class");
    auto render =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/RenderRef.class");
    auto native = tufaLoadFile("ref/raw/system/Scripts/lang/Native.class");
    auto object = tufaLoadFile("ref/raw/system/Scripts/lang/Object.class");
    CHECK(base.ok && game.ok && render.ok && native.ok && object.ok);
    if (base.ok && game.ok && render.ok && native.ok && object.ok) {
      std::vector<const TufaClass *> classes{
          &base.file.classes[0], &game.file.classes[0], &render.file.classes[0],
          &native.file.classes[0], &object.file.classes[0]};
      GameWorld world;
      CHECK(world.addRoot());
      ResourceWorld resources(world, packs);
      VmGameState gameState{&packs, &world, {}, {}, &resources};
      VmArena arena;
      arena.gameState = &gameState;
      auto parent = std::make_unique<VmObject>();
      parent->runtimeClass = classes[0];
      parent->fields.push_back({"ptr", int32_t{0}, "I"});
      const auto *ref = parent.get();
      arena.objects.push_back(std::move(parent));
      CHECK(ok(invokeNative("java.util.resource.ResourceRef", "newNative",
                            "()V", {}, ref, &arena, &classes)));
      CHECK(ok(invokeNative("java.util.resource.ResourceRef", "set", "(I)V",
                            {static_cast<int32_t>(systemGame)}, ref, &arena,
                            &classes)));
      auto mounted = resources.lookup(systemGame);
      CHECK(mounted.node.packNode != nullptr);
      CHECK(resources.addGameChild(*mounted.node.packNode, "test child", 0) !=
            nullptr);
      CHECK(resources.addGameChild(*mounted.node.packNode, "second child", 0) !=
            nullptr);
      auto result = invokeNative(
          "java.util.resource.ResourceRef", "getFirstChild",
          "()Ljava.util.resource.ResourceRef;", {}, ref, &arena, &classes);
      CHECK(std::holds_alternative<VmValue>(result));
      if (const auto *value = std::get_if<VmValue>(&result)) {
        const auto *child = std::get<const VmObject *>(*value);
        CHECK(child != nullptr);
        if (child) {
          const auto resolved = resources.lookup(systemGame);
          auto children = world.childOwners(resolved.node.packNode->handle);
          CHECK(children && !children->empty());
          if (children && !children->empty())
            CHECK(isId(invokeNative("java.util.resource.ResourceRef", "id",
                                    "()I", {}, child, &arena, &classes),
                       children->front()->value));
          auto next =
              invokeNative("java.util.resource.ResourceRef", "getNextChild",
                           "()Ljava.util.resource.ResourceRef;", {}, child,
                           &arena, &classes);
          CHECK(std::holds_alternative<VmValue>(next));
          if (const auto *nextValue = std::get_if<VmValue>(&next)) {
            const auto *sibling = std::get<const VmObject *>(*nextValue);
            CHECK(sibling != nullptr);
            if (sibling && children && children->size() >= 2)
              CHECK(isId(invokeNative("java.util.resource.ResourceRef", "id",
                                      "()I", {}, sibling, &arena, &classes),
                         (*children)[1]->value));
          }
          if (children && !children->empty()) {
            auto *first = world.nodeFor(children->front());
            CHECK(first != nullptr);
            if (first) {
              world.markLoaded(*first);
              first->loadPriority = 1.0f;
              first->scriptReceiver = ref;
              auto name = std::make_unique<VmObject>();
              const auto *namePtr = name.get();
              arena.objects.push_back(std::move(name));
              arena.strings.emplace_back("java.util.resource.ResourceRef",
                                         namePtr);
              CHECK(isId(gameRefIsScripted({namePtr}, child, &arena, &classes),
                         1));
              CHECK(world.recentlyUsed(1).size() == 1 &&
                    first->stamp74 == world.frameCounter());
              arena.strings.back().first = "java.util.resource.GameRef";
              CHECK(isId(gameRefIsScripted({namePtr}, child, &arena, &classes),
                         0));
              CHECK(world.recentlyUsed(1).size() == 1);
              const uint32_t oldFrame = world.frameCounter();
              world.setFrameCounter(oldFrame + 8);
              CHECK(resources.ageRecentUses(error));
              CHECK(world.recentlyUsed(1).size() == 1);
              world.setFrameCounter(oldFrame + 9);
              CHECK(resources.ageRecentUses(error));
              CHECK(world.recentlyUsed(1).empty());
              CHECK(!first->recentUseQueued && first->field6c == 0.0f &&
                    first->stamp78 == oldFrame + 9);
              const auto loaded = world.loadList(1, true);
              CHECK(std::find(loaded.begin(), loaded.end(), first) !=
                    loaded.end());
              world.setFrameCounter(oldFrame);
              CHECK(world.destroy(*first));
              CHECK(world.recentlyUsed(1).empty());
              CHECK(isId(invokeNative("java.util.resource.ResourceRef", "id",
                                      "()I", {}, child, &arena, &classes),
                         0));
            }
          }
        }
      }
      world.setClassResolver([](const std::string &) { return true; });
      arena.loadScriptPath = [](const std::string &,
                                std::string &) -> std::optional<std::string> {
        return std::nullopt;
      };
      CHECK(ok(invokeNative("java.util.resource.ResourceRef", "set", "(I)V",
                            {static_cast<int32_t>(systemDummy)}, ref, &arena,
                            &classes)));
      auto script = resources.lookup(systemDummy);
      CHECK(script.node.packNode && script.node.script &&
            !script.node.packNode->loaded());
      CHECK(isId(gameRefIsScripted({static_cast<const VmObject *>(nullptr)},
                                   ref, &arena, &classes),
                 1));
      CHECK(script.node.packNode->loaded() && script.node.script->loaded);
      CHECK((script.node.packNode->stateWord & 0x70) == 0x10);
      CHECK(script.node.script->className == "java.util.resource.Dummy");
      CHECK(world.recentlyUsed(8).size() == 1);
      const uint32_t frame = world.frameCounter();
      world.setFrameCounter(frame + 8);
      CHECK(resources.ageRecentUses(error));
      CHECK(world.recentlyUsed(8).size() == 1);
      world.setFrameCounter(frame + 9);
      CHECK(resources.ageRecentUses(error));
      CHECK(world.recentlyUsed(8).empty());
      CHECK(script.node.packNode->field6c == 0.0f &&
            (script.node.packNode->stateWord & 0x70) == 0x20);
      world.setFrameCounter(frame);
    }
  }
  std::weak_ptr<SoundSample> expiredSound;
  {
    GameWorld world;
    CHECK(world.addRoot());
    ResourceWorld resources(world, packs);
    VmGameState game{&packs, &world, {}, {}, &resources};
    Host host;
    host.arena.gameState = &game;
    std::unique_ptr<Render2D> dupRenderer;
    SDL_Surface *dupSurface = nullptr;

    // A receiver whose newNative has not run: stock reads a zero link record
    // and does nothing at all.
    const VmObject *fresh = host.make();
    CHECK(isId(host.id(fresh), 0));
    CHECK(ok(host.set(fresh, systemDummy)));
    CHECK(game.bindings.empty() && resources.nodeCount() == 0 &&
          resources.mountState(0) == MountState::Unmounted);
    CHECK(host.arena.resourceStates.empty());

    // With a link: the script node of system.rpk 0x1b, and the id word follows
    // the link.
    const VmObject *ref = host.make();
    CHECK(ok(host.newNative(ref)));
    CHECK(isId(host.id(ref), 0));
    const VmResourceState *state = nativeResourceState(ref, &host.arena);
    CHECK(state && state->words[2] == 0);
    CHECK(ok(host.set(ref, systemDummy)));
    CHECK(isId(host.id(ref), systemDummy));
    CHECK(state && state->words[2] == systemDummy && game.bindings.size() == 1);
    ReceiverBinding &binding = *game.bindings.begin()->second;
    const ResolvedNode dummyNode = resources.resolve(binding.link.owner);
    CHECK(dummyNode && dummyNode.type == 8 && dummyNode.id == systemDummy &&
          dummyNode.script && dummyNode.packNode);
    CHECK(resources.mountState(0) == MountState::Mounted &&
          resources.nodeCount() == 23);

    // The same id again changes nothing. Another pack's id mounts that pack on
    // the way.
    CHECK(ok(host.set(ref, systemDummy)) &&
          binding.link.owner == dummyNode.owner);
    CHECK(ok(host.set(ref, loading)));
    CHECK(state && state->words[2] == loading &&
          binding.link.cachedValue == loading);
    const ResolvedNode loadingNode = resources.resolve(binding.link.owner);
    CHECK(loadingNode.packNode && loadingNode.type == 7 &&
          loadingNode.id == loading);
    CHECK(dummyNode.script && dummyNode.script->head == nullptr);
    CHECK(resources.nodeCount() == 23 + 279);

    CHECK(!ok(invokeNative("java.util.resource.ResourceRef", "cache", "()V", {},
                           ref, &host.arena, &host.classes)));
    CHECK(ok(host.set(ref, sound)));
    auto soundNode = resources.resolve(binding.link.owner);
    CHECK(soundNode.packNode && !soundNode.packNode->sound &&
          !soundNode.packNode->loaded());
    CHECK(ok(invokeNative("java.util.resource.ResourceRef", "precache", "()V",
                          {}, ref, &host.arena, &host.classes)));
    CHECK(resources.pendingPackLoads() == 1 &&
          (soundNode.packNode->flags & 0x2000000u));
    CHECK(ok(invokeNative("java.util.resource.ResourceRef", "precache", "()V",
                          {}, ref, &host.arena, &host.classes)));
    CHECK(resources.pendingPackLoads() == 1);
    CHECK(!soundNode.packNode->sound && !soundNode.packNode->loaded());
    CHECK(contains(
        errorOf(invokeNative("java.util.resource.ResourceRef", "cache", "()V",
                             {}, ref, &host.arena, &host.classes)),
        "pending resource unsupported"));
    CHECK(resources.pendingPackLoads() == 1 && !soundNode.packNode->loaded());
    CHECK(resources.completePendingPackLoads(16, error));
    CHECK(resources.pendingPackLoads() == 0 &&
          !(soundNode.packNode->flags & 0x2000000u));
    CHECK(soundNode.packNode->loaded() && soundNode.packNode->sound &&
          !soundNode.packNode->sound->pcm.empty());
    CHECK(soundNode.packNode->soundBufferBytes &&
          *soundNode.packNode->soundBufferBytes ==
              soundNode.packNode->sound->stockBufferBytes);
    CHECK(ok(invokeNative("java.util.resource.ResourceRef", "cache", "()V", {},
                          ref, &host.arena, &host.classes)));
    CHECK(soundNode.packNode->loaded() && soundNode.packNode->sound &&
          !soundNode.packNode->sound->pcm.empty());
    CHECK(ok(invokeNative("java.util.resource.ResourceRef", "precache", "()V",
                          {}, ref, &host.arena, &host.classes)));
    expiredSound = soundNode.packNode->sound;
    auto *buffer = soundNode.packNode->sound.get();
    CHECK(ok(invokeNative("java.util.resource.ResourceRef", "cache", "()V", {},
                          ref, &host.arena, &host.classes)));
    CHECK(soundNode.packNode->sound.get() == buffer);
    std::vector<ResourceNode *> menuSounds;
    for (uint32_t localId : {0x79u, 0x7au, 0x7eu}) {
      uint32_t menuSoundId = 0;
      CHECK(packs.resolveLiteral("frontend.rpk", localId, menuSoundId, error));
      CHECK(ok(host.set(ref, menuSoundId)));
      auto menuSound = resources.resolve(binding.link.owner);
      CHECK(menuSound.packNode && menuSound.type == 6);
      CHECK(ok(invokeNative("java.util.resource.ResourceRef", "precache", "()V",
                            {}, ref, &host.arena, &host.classes)));
      menuSounds.push_back(menuSound.packNode);
    }
    const auto queuedMenuSounds = resources.pendingPackLoads();
    CHECK(resources.completePendingPackLoads(1, error));
    CHECK(resources.pendingPackLoads() == queuedMenuSounds);
    CHECK(resources.completePendingPackLoads(2, error));
    CHECK(resources.pendingPackLoads() ==
          (queuedMenuSounds ? queuedMenuSounds - 1 : 0));
    CHECK(resources.completePendingPackLoads(16, error));
    for (auto *menuSound : menuSounds)
      CHECK(menuSound->loaded() &&
            (menuSound->payloadSize == 0 || menuSound->sound));
    CHECK(ok(host.set(ref, sound)));

    CHECK(SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1) == 0);
    auto effects = SoundEffects::create(error);
    CHECK(effects);
    VmSoundState soundState;
    soundState.effects = effects.get();
    host.arena.soundState = &soundState;
    const std::vector<VmValue> playArgs{static_cast<const VmObject *>(nullptr),
                                        0.0f,
                                        1.0f,
                                        1.0f,
                                        int32_t{0},
                                        int32_t{7}};
    const auto played = invokeNative("java.util.resource.SfxRef", "nplay",
                                     "(Ljava.lang.Vector3;FFFII)I", playArgs,
                                     ref, &host.arena, &host.classes);
    CHECK(isId(played, 7) && effects->voiceCount() == 1);
    CHECK(ok(invokeNative("java.util.resource.SfxRef", "stop", "(I)V",
                          {int32_t{7}}, ref, &host.arena, &host.classes)));
    CHECK(effects->voiceCount() == 0);
    host.arena.soundState = nullptr;
    CHECK(ok(host.set(ref, emptyScript)));
    auto scriptNode = resources.resolve(binding.link.owner);
    CHECK(scriptNode.type == 8 && scriptNode.packNode && scriptNode.script &&
          scriptNode.packNode->payloadSize == 0);
    CHECK(!scriptNode.packNode->loaded() && !scriptNode.script->loaded);
    CHECK(ok(invokeNative("java.util.resource.ResourceRef", "cache", "()V", {},
                          ref, &host.arena, &host.classes)));
    CHECK(scriptNode.packNode->loaded() && scriptNode.script->loaded &&
          scriptNode.script->className.empty());
    CHECK(scriptNode.packNode->loadPriority == 1.0f &&
          scriptNode.packNode->field6c &&
          *scriptNode.packNode->field6c == 1.0f);
    CHECK(isId(invokeNative("java.util.resource.GameRef", "isScripted",
                            "(Ljava.lang.String;)I",
                            {static_cast<const VmObject *>(nullptr)}, ref,
                            &host.arena, &host.classes),
               0));
    CHECK(ok(host.set(ref, emptySound)));
    auto emptySoundNode = resources.resolve(binding.link.owner);
    CHECK(emptySoundNode.type == 6 && emptySoundNode.packNode &&
          emptySoundNode.packNode->payloadSize == 0);
    CHECK(ok(invokeNative("java.util.resource.ResourceRef", "cache", "()V", {},
                          ref, &host.arena, &host.classes)));
    CHECK(emptySoundNode.packNode->loaded() &&
          !emptySoundNode.packNode->sound &&
          emptySoundNode.packNode->loadPriority == 1.0f);
    const uint32_t soundFrame = world.frameCounter();
    const auto soundsQueued = world.recentlyUsed(6).size();
    CHECK(soundsQueued >= 2);
    world.setFrameCounter(soundFrame + 8);
    CHECK(resources.ageRecentUses(error));
    CHECK(world.recentlyUsed(6).size() == soundsQueued);
    world.setFrameCounter(soundFrame + 9);
    CHECK(resources.ageRecentUses(error));
    CHECK(world.recentlyUsed(6).empty());
    CHECK(emptySoundNode.packNode->field6c == 0.0f &&
          (emptySoundNode.packNode->stateWord & 0x70) == 0x20);
    world.setFrameCounter(soundFrame);

    CHECK(ok(invokeNative("java.util.resource.ResourceRef", "cache", "()V", {},
                          ref, &host.arena, &host.classes)));

    // Zero clears the link and the id word.
    CHECK(ok(host.set(ref, 0)) && !binding.link.owner && state &&
          state->words[2] == 0);
    CHECK(isId(host.id(ref), 0));
    auto *dynamic =
        world.addTypedChild(*world.defaultParent(), 3, "id-fixture", 0);
    CHECK(dynamic && ok(host.set(ref, dynamic->instanceId)) &&
          isId(host.id(ref), dynamic->instanceId));
    CHECK(world.destroy(*dynamic) && isId(host.id(ref), 0));

    // An id nothing resolves to is a normal return: not an error, link and id
    // word empty, id remembered as missing.
    CHECK(ok(host.set(ref, systemGame)) && state &&
          state->words[2] == systemGame);
    CHECK(ok(host.set(ref, systemMissing)));
    CHECK(state && state->words[2] == 0 && !binding.link.owner);
    CHECK(resources.missingIds().size() == 1 &&
          resources.missingIds()[0] == systemMissing);
    // Stock compares the new id with the record's id word first, so a repeat
    // after a miss is a lookup again.
    CHECK(ok(host.set(ref, systemMissing)) &&
          resources.missingIds().size() == 2);

    // A run-time node is found by its 0xffffxxxx instance id; the id word keeps
    // the full 32 bits.
    WorldNode *thing = world.addChild(*world.defaultParent(), "thing", 0x21);
    CHECK(thing && (thing->instanceId & 0x80000000u));
    if (thing) {
      CHECK(ok(host.set(ref, thing->instanceId)));
      CHECK(state && state->words[2] == thing->instanceId &&
            binding.link.owner == thing);
      CHECK(resources.resolve(binding.link.owner).worldNode == thing);
    }

    // A second receiver gets its own binding and shares the node's referrer
    // list.
    const VmObject *other = host.make();
    CHECK(ok(host.newNative(other)) && ok(host.set(other, systemDummy)));
    CHECK(game.bindings.size() == 2 && dummyNode.script &&
          dummyNode.script->head != nullptr);

    // The supported type-14 material gets a distinct runtime node and material
    // links.
    const VmObject *material = host.make();
    const VmObject *duplicate = host.make();
    CHECK(ok(host.newNative(material)) && ok(host.newNative(duplicate)));
    CHECK(ok(host.set(material, rectangle)) &&
          ok(host.set(duplicate, systemDummy)));
    const auto cloneResult =
        invokeNative("java.util.resource.ResourceRef", "duplicate",
                     "(Ljava.util.resource.ResourceRef;)V", {VmValue{material}},
                     duplicate, &host.arena, &host.classes);
    CHECK(ok(cloneResult));
    const auto *duplicateState = nativeResourceState(duplicate, &host.arena);
    const uint32_t clonedId = duplicateState->words[2];
    CHECK((clonedId >> 16) == 0xffff && clonedId != rectangle);
    const auto cloned = resources.lookup(clonedId, 14);
    const auto original = resources.lookup(rectangle, 14);
    CHECK(cloned.status == LookupStatus::Found &&
          original.status == LookupStatus::Found);
    CHECK(cloned.node.owner != original.node.owner && cloned.node.worldNode &&
          cloned.node.worldNode->loaded);
    const auto clonedMaterial = resources.sceneMaterial(cloned.node);
    const auto originalMaterial = resources.sceneMaterial(original.node);
    CHECK(clonedMaterial && originalMaterial &&
          clonedMaterial != originalMaterial);
    if (clonedMaterial && originalMaterial) {
      CHECK(clonedMaterial->flags() == 4096 &&
            clonedMaterial->meshId() == originalMaterial->meshId());
      CHECK(clonedMaterial->defaults().bones == 0 &&
            clonedMaterial->defaults().lodBias == 0 &&
            clonedMaterial->defaults().lodAmplitude == 1);
      CHECK(clonedMaterial->defaults().shadowWords ==
                originalMaterial->defaults().shadowWords &&
            clonedMaterial->defaults().shadowWords[11] == 0x41200000 &&
            clonedMaterial->defaults().shadowWords[16] == 0x41200000);
      const uint32_t oldTexture = originalMaterial->textureId(0);
      const VmObject *oldRef = host.make();
      const VmObject *newRef = host.make();
      CHECK(ok(host.newNative(oldRef)) && ok(host.newNative(newRef)));
      CHECK(ok(host.set(oldRef, oldTexture)) && ok(host.set(newRef, loading)));
      int refreshes = 0;
      CHECK(
          resources.setSceneMaterialRefresh(cloned.node, [&] { ++refreshes; }));
      const auto change = [&] {
        return invokeNative("java.util.resource.RenderRef", "changeResource",
                            "(Ljava.util.resource.ResourceRef;Ljava.util."
                            "resource.ResourceRef;)V",
                            {VmValue{oldRef}, VmValue{newRef}}, duplicate,
                            &host.arena, &host.classes);
      };
      CHECK(ok(change()));
      CHECK(clonedMaterial->textureId(0) == loading &&
            originalMaterial->textureId(0) == oldTexture && refreshes == 1);
      CHECK(ok(change()) && refreshes == 1);
      CHECK(ok(host.set(oldRef, loading)) && ok(change()) && refreshes == 2);
      CHECK(resources.setSceneMaterialRefresh(cloned.node, [&] {
        ++refreshes;
        resources.unbindSceneMaterial(cloned.node);
      }));
      CHECK(ok(change()) && refreshes == 3 &&
            !resources.sceneMaterial(cloned.node));
      CHECK(resources.bindSceneMaterial(cloned.node, clonedMaterial));
    }
    const VmObject *secondClone = host.make();
    CHECK(ok(host.newNative(secondClone)));
    const auto cloneAgain = invokeNative(
        "java.util.resource.ResourceRef", "duplicate",
        "(Ljava.util.resource.ResourceRef;)V", {VmValue{duplicate}},
        secondClone, &host.arena, &host.classes);
    CHECK(ok(cloneAgain));
    const auto secondId =
        nativeResourceState(secondClone, &host.arena)->words[2];
    const auto secondNode = resources.lookup(secondId, 14);
    CHECK(secondId != clonedId && secondNode.status == LookupStatus::Found);
    const auto secondMaterial = resources.sceneMaterial(secondNode.node);
    CHECK(secondMaterial && secondMaterial != clonedMaterial &&
          secondMaterial->textureId(0) == loading);
    const VmObject *empty = host.make();
    const VmObject *textureRef = host.make();
    CHECK(ok(host.newNative(empty)) && ok(host.newNative(textureRef)) &&
          ok(host.set(textureRef, loading)));
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0 || SDL_WasInit(SDL_INIT_VIDEO));
    dupSurface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
    CHECK(dupSurface);
    dupRenderer = Render2D::forSurface(dupSurface, error);
    CHECK(dupRenderer);
    game.renderer = dupRenderer.get();
    const auto textureDup = invokeNative(
        "java.util.resource.ResourceRef", "duplicate",
        "(Ljava.util.resource.ResourceRef;)V", {VmValue{textureRef}}, duplicate,
        &host.arena, &host.classes);
    CHECK(ok(textureDup));
    const auto textureClonedId =
        nativeResourceState(duplicate, &host.arena)->words[2];
    CHECK(textureClonedId != clonedId && textureClonedId != loading);
    const auto textureNode = resources.lookup(textureClonedId, 7);
    CHECK(textureNode.status == LookupStatus::Found &&
          textureNode.node.worldNode && textureNode.node.worldNode->loaded);
    CHECK(resources.sceneTexture(textureNode.node));
    CHECK(textureNode.node.worldNode &&
          world.destroy(*textureNode.node.worldNode));
    game.renderer = nullptr;
    dupRenderer.reset();
    SDL_DestroySurface(dupSurface);
    dupSurface = nullptr;
    const auto savedClonedId = clonedId;
    CHECK(ok(host.set(duplicate, savedClonedId)));
    CHECK(nativeResourceState(duplicate, &host.arena)->words[2] == savedClonedId);
    const auto clearResult =
        invokeNative("java.util.resource.ResourceRef", "duplicate",
                     "(Ljava.util.resource.ResourceRef;)V", {VmValue{empty}},
                     duplicate, &host.arena, &host.classes);
    CHECK(ok(clearResult));
    CHECK(nativeResourceState(duplicate, &host.arena)->words[2] == 0);
    const auto shareResult =
        invokeNative("java.util.resource.ResourceRef", "duplicate",
                     "(Ljava.util.resource.ResourceRef;)V", {VmValue{other}},
                     duplicate, &host.arena, &host.classes);
    CHECK(ok(shareResult));
    CHECK(nativeResourceState(duplicate, &host.arena)->words[2] == systemDummy);
    duplicateState = nativeResourceState(duplicate, &host.arena);
    const auto duplicateHandle =
        static_cast<int32_t>(duplicateState -
                             host.arena.resourceStates.data()) +
        1;
    CHECK(game.bindings.at(duplicateHandle)->link.owner == dummyNode.owner);
    const auto sameResult = invokeNative(
        "java.util.resource.ResourceRef", "duplicate",
        "(Ljava.util.resource.ResourceRef;)V", {VmValue{duplicate}}, duplicate,
        &host.arena, &host.classes);
    CHECK(ok(sameResult) &&
          game.bindings.at(duplicateHandle)->link.owner == dummyNode.owner);
    const auto nullResult =
        invokeNative("java.util.resource.ResourceRef", "duplicate",
                     "(Ljava.util.resource.ResourceRef;)V",
                     {VmValue{static_cast<const VmObject *>(nullptr)}},
                     duplicate, &host.arena, &host.classes);
    CHECK(ok(nullResult) &&
          nativeResourceState(duplicate, &host.arena)->words[2] == systemDummy);

    // Stock FUN_00545230 clones any bound type-14 material: no flags or
    // texture-count gate. A material with flags 0x1234 and two textures
    // (the Navigator tile shape) must clone and keep its flags.
    {
      auto *custom =
          world.addTypedChild(*world.defaultParent(), 14, "custom-flags", 0);
      CHECK(custom);

      LinkOwner customMesh;
      customMesh.value = static_cast<int32_t>(originalMaterial->meshId());
      LinkOwner customTex;
      customTex.value = static_cast<int32_t>(loading);
      auto customMaterial = std::make_shared<SceneMaterialResources>(
          &customMesh, std::vector<LinkOwner *>{&customTex, &customTex},
          0x1234);
      const auto resolvedCustom = resources.resolve(custom);
      CHECK(resources.bindSceneMaterial(resolvedCustom, customMaterial));
      const VmObject *customRef = host.make();
      const VmObject *customClone = host.make();
      CHECK(ok(host.newNative(customRef)) && ok(host.newNative(customClone)));
      CHECK(ok(host.set(customRef, custom->instanceId)));
      const auto customDup = invokeNative(
          "java.util.resource.ResourceRef", "duplicate",
          "(Ljava.util.resource.ResourceRef;)V", {VmValue{customRef}},
          customClone, &host.arena, &host.classes);
      if (!ok(customDup))
        std::printf("custom dup error: %s\n", errorOf(customDup).c_str());
      CHECK(ok(customDup));
      const auto customClonedId =
          nativeResourceState(customClone, &host.arena)->words[2];
      CHECK((customClonedId >> 16) == 0xffff &&
            customClonedId != custom->instanceId);
      const auto customCloned = resources.lookup(customClonedId, 14);
      CHECK(customCloned.status == LookupStatus::Found &&
            customCloned.node.worldNode && customCloned.node.worldNode->loaded);
      const auto customClonedMaterial =
          resources.sceneMaterial(customCloned.node);
      CHECK(customClonedMaterial && customClonedMaterial != customMaterial &&
            customClonedMaterial->flags() == 0x1234 &&
            customClonedMaterial->textureCount() == 2 &&
            customClonedMaterial->meshId() == originalMaterial->meshId());
    }

    // Argument and receiver checks are loud.
    CHECK(contains(
        errorOf(invokeNative("java.util.resource.ResourceRef", "set", "(I)V",
                             {}, ref, &host.arena, &host.classes)),
        "invalid ResourceRef.set call"));
    CHECK(contains(
        errorOf(invokeNative("java.util.resource.ResourceRef", "set", "(I)V",
                             {VmValue{1.0f}}, ref, &host.arena, &host.classes)),
        "invalid ResourceRef.set call"));
    CHECK(contains(errorOf(invokeNative("java.util.resource.ResourceRef", "set",
                                        "(I)V", {VmValue{int32_t{1}}}, nullptr,
                                        &host.arena, &host.classes)),
                   "invalid ResourceRef.set receiver"));
    CHECK(contains(errorOf(host.set(host.make(false), 1)), "no ptr field"));
    auto forged = std::make_unique<VmObject>();
    forged->runtimeClass = &host.cls;
    forged->fields.push_back({"ptr", int32_t{99}, "I"});
    host.arena.objects.push_back(std::move(forged));
    CHECK(contains(errorOf(host.set(host.arena.objects.back().get(), 1)),
                   "not a link handle"));
  }

  {
    // No host world state, or no resource world in it.
    Host host;
    const VmObject *ref = host.make();
    CHECK(ok(host.newNative(ref)));
    CHECK(contains(errorOf(host.set(ref, systemDummy)),
                   "no resource world state"));
    GameWorld world;
    VmGameState game{&packs, &world, {}, {}, nullptr};
    host.arena.gameState = &game;
    CHECK(contains(errorOf(host.set(ref, systemDummy)),
                   "no resource world state"));
  }

  {
    // Stock dereferences null when a pack entry's parent is the default parent
    // and no world root exists.
    GameWorld world;
    ResourceWorld resources(world, packs);
    VmGameState game{&packs, &world, {}, {}, &resources};
    Host host;
    host.arena.gameState = &game;
    const VmObject *ref = host.make();
    CHECK(ok(host.newNative(ref)));
    const std::string crash = errorOf(host.set(ref, systemDummy));
    CHECK(contains(crash, "ResourceRef.set: stock crash: ") &&
          contains(crash, "no default parent"));
    const VmResourceState *state = nativeResourceState(ref, &host.arena);
    CHECK(state && state->words[2] == 0);
  }

  {
    GameWorld world;
    CHECK(world.addRoot());
    OwnerLink external;
    {
      ResourceWorld resources(world, packs);
      auto soundNode = resources.lookup(sound, 6);
      CHECK(soundNode.status == LookupStatus::Found && soundNode.node.packNode);
      setLinkOwner(external, soundNode.node.owner);
      CHECK(resources.queueSoundPrecache(*soundNode.node.packNode, error));
      CHECK(resources.pendingPackLoads() == 1 && external.owner);
    }
    CHECK(!external.owner && external.cachedValue == 0);
  }
  CHECK(expiredSound.expired());
  {
    GameWorld world;
    auto *root = world.addRoot();
    CHECK(root);
    ResourceWorld resources(world, packs);
    CHECK(resources.stockSceneBatchBuffersAbsent());
    auto *node = world.addTypedChild(*root, 14, "batch-test", 0);
    CHECK(node && !resources.stockSceneBatchBuffersAbsent());
    if (node) {
      auto ordinary = std::make_shared<SceneMaterialResources>(
          nullptr, std::vector<LinkOwner *>{}, 0x1000);
      CHECK(resources.bindSceneMaterial(resources.resolve(node), ordinary));
      CHECK(resources.stockSceneBatchBuffersAbsent());
      auto batched = std::make_shared<SceneMaterialResources>(
          nullptr, std::vector<LinkOwner *>{}, 0x8000);
      CHECK(resources.bindSceneMaterial(resources.resolve(node), batched));
      CHECK(!resources.stockSceneBatchBuffersAbsent());
      world.destroy(*node);
      CHECK(!resources.stockSceneBatchBuffersAbsent());
    }
  }
  {
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    auto *surface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
    CHECK(surface);
    auto renderer = Render2D::forSurface(surface, error);
    CHECK(renderer);
    std::weak_ptr<ResourceTextureCache> expiredTexture;
    size_t textureId = 0;
    TextureAllocationInventory textureInventory;
    if (renderer) {
      {
        GameWorld world;
        CHECK(world.addRoot());
        ResourceWorld resources(world, packs);
        textureInventory = resources.textureAllocations();
        VmGameState gameState{&packs, &world, {}, {}, &resources};
        gameState.renderer = renderer.get();
        Host host;
        host.arena.gameState = &gameState;
        const auto *ref = host.make();
        CHECK(ok(host.newNative(ref)));
        CHECK(ok(host.set(ref, loading)));
        CHECK(ok(invokeNative("java.util.resource.ResourceRef", "cache", "()V",
                              {}, ref, &host.arena, &host.classes)));
        const auto node = resources.lookup(loading, 7);
        CHECK(node.node.packNode && node.node.packNode->loaded() &&
              node.node.packNode->texture);
        if (node.node.packNode && node.node.packNode->texture) {
          auto cache = node.node.packNode->texture;
          expiredTexture = cache;
          textureId = *cache->handle;
          CHECK(cache->pixels.width == 1024 && cache->pixels.height == 1024);
          CHECK(textureInventory.count() == 1 &&
                textureInventory.upperBound() == 5592404u);
          // One device slot at the presented caps, far below the family
          // allowance used before.
          CHECK(renderer->maxTextureSize() >= 1024);
          CHECK(textureInventory.upperBound() <
                stockTextureFamilyUpperBound(1024, 1024));
          CHECK(resources.cachedTextureIds() == std::vector<uint32_t>{loading});
          CHECK(renderer->draw(textureId, {0, 0, 32, 32}, error));
          CHECK(ok(invokeNative("java.util.resource.ResourceRef", "cache",
                                "()V", {}, ref, &host.arena, &host.classes)));
          CHECK(node.node.packNode->texture == cache);
        }
        // Font and sound requests share one FIFO and one per-frame cap.
        world.setFrameCounter(1);
        uint32_t pendingFontId = 0;
        CHECK(
            packs.resolveLiteral("frontend.rpk", 0x139, pendingFontId, error));
        const auto pendingFont = resources.lookup(pendingFontId, 14);
        const auto pendingSound = resources.lookup(sound, 6);
        CHECK(pendingFont.status == LookupStatus::Found &&
              pendingSound.status == LookupStatus::Found);
        CHECK(resources.queueFontLoad(pendingFont.node, error));
        CHECK(resources.queueFontLoad(pendingFont.node, error));
        CHECK(resources.queueSoundPrecache(*pendingSound.node.packNode, error));
        CHECK(resources.pendingPackLoads() == 2 &&
              !pendingFont.node.packNode->loaded());
        CHECK(resources.completePendingPackLoads(2, error));
        CHECK(resources.pendingPackLoads() == 1 &&
              pendingFont.node.packNode->loaded());
        CHECK(resources.sceneMaterial(pendingFont.node) &&
              !pendingSound.node.packNode->loaded());
        CHECK(resources.completePendingPackLoads(2, error));
        CHECK(resources.pendingPackLoads() == 0 &&
              pendingSound.node.packNode->loaded());
        uint32_t fontId = 0;
        CHECK(packs.resolveLiteral("frontend.rpk", 0x25, fontId, error));
        CHECK(ok(host.set(ref, fontId)));
        CHECK(ok(invokeNative("java.util.resource.ResourceRef", "cache", "()V",
                              {}, ref, &host.arena, &host.classes)));
        const auto font = resources.lookup(fontId, 14);
        auto material = resources.sceneMaterial(font.node);
        CHECK(material && font.node.packNode && font.node.packNode->loaded());
        CHECK(resources.stockSceneMeshUpperBound() &&
              *resources.stockSceneMeshUpperBound() > 0);
        if (material) {
          const auto mesh = resources.lookup(material->meshId(), 5);
          const auto atlas = resources.lookup(material->textureId(0), 7);
          CHECK(mesh.node.packNode && mesh.node.packNode->loaded() &&
                resources.sceneMesh(mesh.node));
          CHECK(atlas.node.packNode && atlas.node.packNode->loaded() &&
                atlas.node.packNode->texture);
          if (atlas.node.packNode && atlas.node.packNode->texture)
            CHECK(renderer->draw(*atlas.node.packNode->texture->handle,
                                 {0, 0, 16, 16}, error));
          CHECK(ok(invokeNative("java.util.resource.ResourceRef", "cache",
                                "()V", {}, ref, &host.arena, &host.classes)));
          CHECK(resources.sceneMaterial(font.node) == material);
        }
      }
      CHECK(expiredTexture.expired());
      CHECK(textureInventory.count() == 0 &&
            textureInventory.upperBound() == 0);
      CHECK(!renderer->draw(textureId, {0, 0, 32, 32}, error));
    }
    renderer.reset();
    if (surface)
      SDL_DestroySurface(surface);
    SDL_Quit();
  }
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::puts("ResourceRef.set native tests passed");
  return 0;
}
