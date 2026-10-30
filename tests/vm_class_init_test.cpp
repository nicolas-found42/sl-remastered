// On-demand class loading and initialization (stock FUN_00410890, FUN_00403e80)
// and static initializer trees that run code. Synthetic classes cover each
// rule. The real stock java.render.Frontend covers the path the startup probe
// takes.
#include "vm_test_harness.hpp"

#include "natives/game_type.hpp"
#include "natives/registry.hpp"
#include "pack_registry.hpp"
#include "resource_world.hpp"

using namespace vm_test;

static NativeResult nativeConstructor(const std::vector<VmValue> &args,
                                      const VmObject *receiver, VmArena *,
                                      const std::vector<const TufaClass *> *) {
  auto *field = fieldOf(receiver, "value");
  if (!field || args.size() != 2 || !std::get_if<int32_t>(&args[0]) ||
      !std::get_if<int32_t>(&args[1]))
    return std::string{"bad constructor arguments"};
  field->value = std::get<int32_t>(args[0]) * 10 + std::get<int32_t>(args[1]);
  return std::monostate{};
}

static NativeResult failingConstructor(const std::vector<VmValue> &,
                                      const VmObject *, VmArena *,
                                      const std::vector<const TufaClass *> *) {
  return std::string{"constructor handler failed"};
}

int main() {
  {
    // A class named by `new` is loaded and initialized on first use, and a
    // static initializer that runs code works.
    World w;
    w.cls("test.Lazy")
        .staticField("count", "I", {literal(7), endTree})
        .constructor();
    w.start();
    CHECK(!w.registered("test.Lazy") && w.state("test.Lazy") == nullptr);
    const auto first = w.run(concat(w.main.make("test.Lazy"), {ret}));
    CHECK(first.ok);
    const auto *object = std::get_if<const VmObject *>(&first.value);
    CHECK(object && *object && (*object)->runtimeClass->name == "test.Lazy");
    CHECK(w.registered("test.Lazy") && w.loads("test.Lazy") == 1);
    CHECK(intField(w.state("test.Lazy"), "count") == 7);
    for (auto &s : w.runtime->staticFields())
      s.fields[0].value = int32_t{99};
    CHECK(w.run(concat(w.main.make("test.Lazy"), {ret})).ok);
    CHECK(intField(w.state("test.Lazy"), "count") == 99 &&
          w.loads("test.Lazy") == 1);
  }
  {
    // An initializer that allocates another class initializes that class first,
    // and the field gets the object.
    World w;
    w.cls("test.Inner")
        .staticField("v", "I", {literal(4), endTree})
        .constructor();
    auto &outer = w.cls("test.Outer");
    outer.staticField("a", "I", {literal(1), endTree});
    outer.staticField("inner", "Ltest.Inner;",
                      concat(outer.make("test.Inner"), {endTree}));
    outer.staticField("b", "I", {literal(2), endTree});
    outer.constructor();
    w.start();
    CHECK(w.run(concat(w.main.make("test.Outer"), {ret})).ok);
    const auto *s = w.state("test.Outer");
    CHECK(intField(s, "a") == 1 && intField(s, "b") == 2);
    const auto *inner = objectField(s, "inner");
    CHECK(inner && inner->runtimeClass->name == "test.Inner");
    CHECK(intField(w.state("test.Inner"), "v") == 4);
  }
  {
    // Reading another class's static field initializes that class.
    World w;
    w.cls("test.Src")
        .staticField("value", "I",
                     {node(0x07, true, 28), node(0x0b, true, 5), endTree});
    auto &user = w.cls("test.User");
    user.staticField("copy", "I",
                     concat(user.read("test.Src", "value"), {endTree}));
    w.start();
    const auto ensured = w.runtime->ensure("test.User");
    CHECK(ensured.ok);
    CHECK(intField(w.state("test.User"), "copy") == 5 &&
          intField(w.state("test.Src"), "value") == 5);
    CHECK(w.loads("test.Src") == 1);
  }
  {
    // An explicit-class static call initializes the class and runs the method
    // against its state.
    World w;
    auto &callee = w.cls("test.Callee");
    callee.staticField("n", "I", {literal(42), endTree});
    callee.method("get", "()I", 0x08,
                  {node(0x1b, true, callee.ref("test.Callee", "n", "I")), ret});
    w.start();
    const auto call =
        w.run({literal(0), node(0x24), node(0x11, true, 2),
               node(0x19, true, w.main.cref("test.Callee")),
               node(0x1a, true, w.main.ref("test.Callee", "get", "()I")), ret});
    CHECK(call.ok && std::get_if<int32_t>(&call.value) &&
          std::get<int32_t>(call.value) == 42);
    CHECK(w.registered("test.Callee") &&
          intField(w.state("test.Callee"), "n") == 42);
  }
  {
    // A class that is still initializing exposes the fields it has already
    // defined, as in stock FUN_00403e80, which marks the class initialized
    // before it runs the static initializers.
    World w;
    auto &a = w.cls("test.A");
    a.staticField("x", "I", {literal(3), endTree});
    a.staticField("b", "Ltest.B;", concat(a.make("test.B"), {endTree}));
    a.staticField("later", "I", {literal(9), endTree});
    a.constructor();
    auto &b = w.cls("test.B");
    b.staticField("seen", "I", concat(b.read("test.A", "x"), {endTree}));
    b.constructor();
    w.start();
    CHECK(w.runtime->ensure("test.A").ok);
    CHECK(intField(w.state("test.B"), "seen") == 3);
    CHECK(intField(w.state("test.A"), "later") == 9 &&
          objectField(w.state("test.A"), "b") != nullptr);
  }
  {
    // A field that is not defined yet is not readable, so the initialization
    // fails loudly and leaves no state.
    World w;
    auto &a = w.cls("test.A2");
    a.staticField("b", "Ltest.B2;", concat(a.make("test.B2"), {endTree}));
    a.staticField("later", "I", {literal(9), endTree});
    a.constructor();
    auto &b = w.cls("test.B2");
    b.staticField("seen", "I", concat(b.read("test.A2", "later"), {endTree}));
    b.constructor();
    w.start();
    const auto failed = w.runtime->ensure("test.A2");
    CHECK(!failed.ok && contains(failed.error, "test.B2.seen"));
    CHECK(w.runtime->staticFields().empty());
    CHECK(w.arena.failedInitializations.size() == 2);
    const auto again = w.runtime->ensure("test.A2");
    CHECK(!again.ok && contains(again.error, "failed earlier"));
    CHECK(w.arena.failedInitializations.size() == 2 &&
          w.runtime->staticFields().empty());
    const auto other = w.runtime->ensure("test.B2");
    CHECK(!other.ok && contains(other.error, "failed earlier"));
  }
  {
    // A parent whose initializer initializes its own subclass would run the
    // subclass initializers twice in stock. Here it is an error, and the failed
    // call removes the state it registered.
    World w;
    auto &base = w.cls("test.Base");
    base.staticField("made", "Ltest.Child;",
                     concat(base.make("test.Child"), {endTree}));
    base.constructor();
    w.cls("test.Child", "test.Base").constructor();
    w.start();
    const auto result = w.runtime->ensure("test.Child");
    CHECK(!result.ok &&
          contains(result.error, "reentrant class initialization"));
    CHECK(w.state("test.Base") == nullptr);
  }
  {
    // Loader failure: the error names the class and the loader's reason, and
    // nothing is registered.
    World w;
    w.start();
    const auto result = w.run(concat(w.main.make("test.Missing"), {ret}));
    CHECK(!result.ok && contains(result.error, "test.Missing") &&
          contains(result.error, "no such class in this test"));
    CHECK(w.runtime->classes().empty() && w.runtime->staticFields().empty());
  }
  {
    // A failing initializer names the class and field, leaves no state, records
    // the failure once and is never retried. The failure trace shows the class.
    World w;
    w.cls("test.Bad")
        .staticField("ok", "I", {literal(1), endTree})
        .staticField("broken", "I", {node(0x3f), endTree})
        .constructor();
    w.start();
    const auto first = w.run(concat(w.main.make("test.Bad"), {ret}));
    CHECK(!first.ok && contains(first.error, "test.Bad.broken"));
    CHECK(w.state("test.Bad") == nullptr &&
          w.arena.failedInitializations.size() == 1);
    bool traced = false;
    for (const auto &line : w.arena.failureTrace)
      if (contains(line, "test.Bad"))
        traced = true;
    CHECK(traced);
    const auto second = w.run(concat(w.main.make("test.Bad"), {ret}));
    CHECK(!second.ok && contains(second.error, "failed earlier"));
    CHECK(w.state("test.Bad") == nullptr &&
          w.arena.failedInitializations.size() == 1);
  }
  {
    // An initializer whose value does not fit the field type fails instead of
    // storing it.
    World w;
    auto &wrong = w.cls("test.Wrong");
    wrong.staticField("n", "I", concat(wrong.make("test.Wrong"), {endTree}));
    wrong.constructor();
    w.start();
    const auto result = w.runtime->ensure("test.Wrong");
    CHECK(!result.ok && contains(result.error, "does not fit"));
    CHECK(w.state("test.Wrong") == nullptr);
  }
  {
    // A chain of initializers that each initialize the next class stops at a
    // fixed depth.
    World w;
    for (int i = 0; i < 40; ++i) {
      const std::string name = "test.C" + std::to_string(i);
      auto &c = w.cls(name);
      if (i + 1 < 40) {
        const std::string next = "test.C" + std::to_string(i + 1);
        c.staticField("next", "L" + next + ";",
                      concat(c.make(next), {endTree}));
      }
      c.constructor();
    }
    w.start();
    const auto result = w.runtime->ensure("test.C0");
    CHECK(!result.ok && contains(result.error, "nested too deeply"));
  }
  {
    // `object.field` (stock 401c) stores through an object's field, found by
    // name; a missing field is an error.
    World w;
    w.cls("test.Holder").instanceField("n", "I").constructor();
    w.start();
    const auto stored =
        w.run(concat(concat({literal(5)}, w.main.make("test.Holder")),
                     {node(0x1c, true, w.main.t("n")), node(0x08, true, 35),
                      node(0x29), ret}));
    CHECK(stored.ok);
    const VmObject *holder = nullptr;
    for (const auto &object : w.arena.objects)
      if (object->runtimeClass && object->runtimeClass->name == "test.Holder")
        holder = object.get();
    CHECK(holder && holder->fields.size() == 1 &&
          std::get<int32_t>(holder->fields[0].value) == 5);
    const auto missing =
        w.run(concat(concat({literal(5)}, w.main.make("test.Holder")),
                     {node(0x1c, true, w.main.t("absent")),
                      node(0x08, true, 35), node(0x29), ret}));
    CHECK(!missing.ok && contains(missing.error, "object has no field absent"));
    const auto nothing = w.run({node(0x07, true, 28), node(0x0c),
                                node(0x1c, true, w.main.t("n")), ret});
    CHECK(!nothing.ok &&
          contains(nothing.error, "null or invalid field receiver"));
  }
  {
    // Instance field initializers that run code (stock `<instance_field_init>`,
    // FUN_00407da0): the field is defined first, the tree runs in a frame of
    // the declaring class with the new object as receiver, and the value it
    // leaves is assigned. The initializer can name the object, and an
    // ancestor's initializers run before the subclass's.
    World w;
    w.cls("test.Inner")
        .instanceField("marker", "I")
        .markingConstructor("()", 7, "marker");
    auto &base = w.cls("test.Base");
    base.instanceFieldInit("baseInner", "Ltest.Inner;",
                           concat(base.make("test.Inner"), {endTree}))
        .constructor();
    auto &holder = w.cls("test.Holder2", "test.Base");
    holder
        .instanceFieldInit("inner", "Ltest.Inner;",
                           concat(holder.make("test.Inner"), {endTree}))
        .instanceFieldInit("me", "Ltest.Holder2;",
                           {node(0x01, true, 0), endTree})
        .instanceFieldInit("count", "I", {literal(3), endTree})
        .constructor();
    w.start();
    const auto made = w.run(concat(w.main.make("test.Holder2"), {ret}));
    if (!made.ok)
      std::printf("holder: %s\n", made.error.c_str());
    CHECK(made.ok);
    const auto *object =
        made.ok ? std::get_if<const VmObject *>(&made.value) : nullptr;
    CHECK(object && *object && (*object)->fields.size() == 4);
    if (object && *object && (*object)->fields.size() == 4) {
      const auto &fields = (*object)->fields;
      CHECK(fields[0].name == "baseInner" && fields[1].name == "inner" &&
            fields[2].name == "me" && fields[3].name == "count");
      const auto *baseInner = std::get_if<const VmObject *>(&fields[0].value);
      const auto *inner = std::get_if<const VmObject *>(&fields[1].value);
      CHECK(baseInner && *baseInner && inner && *inner && *baseInner != *inner);
      CHECK(inner && *inner && (*inner)->runtimeClass->name == "test.Inner" &&
            std::get<int32_t>((*inner)->fields[0].value) == 7);
      CHECK(std::get<const VmObject *>(fields[2].value) == *object &&
            std::get<int32_t>(fields[3].value) == 3);
    }
    // A failing initializer names the class and field, and a value of the wrong
    // type is refused.
    w.cls("test.Bad2")
        .instanceFieldInit("broken", "I", {node(0x3f), endTree})
        .constructor();
    auto &wrongClass = w.cls("test.Wrong2");
    wrongClass
        .instanceFieldInit("n", "I",
                           concat(wrongClass.make("test.Inner"), {endTree}))
        .constructor();
    const auto bad = w.run(concat(w.main.make("test.Bad2"), {ret}));
    CHECK(!bad.ok && contains(bad.error, "test.Bad2.broken"));
    const auto wrong = w.run(concat(w.main.make("test.Wrong2"), {ret}));
    CHECK(!wrong.ok && contains(wrong.error, "test.Wrong2.n") &&
          contains(wrong.error, "does not fit I"));
  }
  {
    // Constructor choice by assignability (the stock signature is built from
    // the arguments' classes and ranked by FUN_00405920): a parameter accepts
    // the object's class, an ancestor or a listed interface. The exact class
    // wins, and two equally good candidates are ambiguous.
    World w;
    w.cls("test.Run").interfaceKind();
    w.cls("test.Other").interfaceKind();
    w.cls("test.Impl")
        .implements("test.Run")
        .implements("test.Other")
        .constructor();
    w.cls("test.SubImpl", "test.Impl").constructor();
    w.cls("test.Unrelated").constructor();
    w.cls("test.OneArg")
        .instanceField("which", "I")
        .markingConstructor("(Ltest.Run;)", 1, "which");
    w.cls("test.Exact")
        .instanceField("which", "I")
        .markingConstructor("(Ltest.Run;)", 1, "which")
        .markingConstructor("(Ltest.Impl;)", 2, "which");
    w.cls("test.Tie")
        .instanceField("which", "I")
        .markingConstructor("(Ltest.Run;)", 1, "which")
        .markingConstructor("(Ltest.Other;)", 2, "which");
    w.cls("test.TwoArgs")
        .instanceField("which", "I")
        .markingConstructor("(Ltest.Run;Ltest.Other;)", 5, "which");
    w.start();
    const auto build = [&](const std::string &target,
                           const std::vector<std::string> &arguments) {
      std::vector<TufaNode> body{
          node(0x07, true, 30), node(0x06, true, w.main.t("L" + target + ";"))};
      for (const auto &argument : arguments)
        body = concat(body, {node(0x07, true, 30),
                             node(0x06, true, w.main.t("L" + argument + ";")),
                             literal(0), node(0x21)});
      body.push_back(literal(static_cast<int32_t>(arguments.size())));
      body.push_back(node(0x21));
      body.push_back(ret);
      return w.run(body);
    };
    const auto which = [](const VmResult &result) {
      const auto *object =
          result.ok ? std::get_if<const VmObject *>(&result.value) : nullptr;
      return object && *object ? std::get<int32_t>((*object)->fields[0].value)
                               : -1;
    };
    CHECK(which(build("test.OneArg", {"test.Impl"})) == 1);
    CHECK(which(build("test.OneArg", {"test.SubImpl"})) == 1);
    CHECK(which(build("test.Exact", {"test.Impl"})) == 2);
    CHECK(which(build("test.Exact", {"test.SubImpl"})) == -1 &&
          contains(build("test.Exact", {"test.SubImpl"}).error,
                   "ambiguous constructor"));
    CHECK(contains(build("test.Tie", {"test.Impl"}).error,
                   "ambiguous constructor"));
    CHECK(!build("test.OneArg", {"test.Unrelated"}).ok);
    CHECK(which(build("test.TwoArgs", {"test.Impl", "test.SubImpl"})) == 5);
    CHECK(!build("test.TwoArgs", {"test.Impl", "test.Unrelated"}).ok);
  }
  {
    // Without a host hook the old rules hold: an unregistered class is an
    // error.
    VmArena arena;
    Cb main("test.Main");
    Cb lazy("test.Lazy");
    lazy.constructor();
    std::vector<const TufaClass *> classes;
    std::vector<VmStaticFields> states;
    const auto result = vmExecute(main.c, concat(main.make("test.Lazy"), {ret}),
                                  {}, nullptr, &classes, &states, &arena);
    CHECK(!result.ok &&
          contains(result.error, "allocation class not registered: test.Lazy"));
  }
  {
    // Without an arena a static initializer that needs code is still rejected.
    Cb cls("test.NoArena");
    cls.staticField("n", "I", {literal(7), endTree});
    std::vector<const TufaClass *> classes{&cls.c};
    std::vector<VmStaticFields> states;
    const auto result = vmInitializeClass(cls.c, classes, states);
    CHECK(!result.ok &&
          contains(result.error, "unsupported static initializer tree 0") &&
          states.empty());
  }
  {
    // The real stock Frontend: loaded on demand, its code initializers run
    // (`new ResourceRef(<resource literal>)` and `new Vector()`).
    // ResourceRef.set(I)V is the registered native (0047cf80). Frontend is not
    // preloaded.
    ClassEnvironment environment("ref/raw");
    const auto load = [&](const std::string &name, std::string &error) {
      return environment.findOrLoad(name, error);
    };

    // Without a resource world the native refuses loudly instead of inventing a
    // node.
    {
      VmArena bare;
      std::vector<NativeEntry> natives = nativeRegistry();
      bare.natives = &natives;
      VmResourceResolver resolver =
          [&](std::string_view, uint32_t id) -> std::optional<uint32_t> {
        return 0x00040000u + id;
      };
      ClassRuntime runtime(load, bare, &resolver);
      const auto blocked = runtime.ensure("java.render.Frontend");
      CHECK(!blocked.ok);
      CHECK(
          contains(blocked.error, "java.render.Frontend.defLoadingPic") &&
          contains(blocked.error, "ResourceRef.set: no resource world state"));
      bool inFrontend = false, inResourceRef = false;
      for (const auto &line : bare.failureTrace) {
        inFrontend = inFrontend || contains(line, "java.render.Frontend ");
        inResourceRef =
            inResourceRef || contains(line, "java.util.resource.ResourceRef ");
      }
      CHECK(inFrontend && inResourceRef);
      bool frontendState = false;
      for (const auto &s : runtime.staticFields())
        if (s.owner->name == "java.render.Frontend")
          frontendState = true;
      CHECK(!frontendState && bare.failedInitializations.size() == 1);
      CHECK(!runtime.ensure("java.render.Frontend").ok);
    }

    // With the host world: the id resolves through the real PackRegistry,
    // ResourceRef.set mounts frontend.rpk (and system.rpk under it) and links
    // the receiver to the real `loading` texture node, frontend.rpk 0xa8.
    PackRegistry packs("ref/raw");
    GameWorld world;
    CHECK(world.addRoot() != nullptr);
    ResourceWorld resources(world, packs);
    VmGameState game{&packs, &world, {}, {}, &resources};
    VmArena arena2;
    arena2.gameState = &game;
    std::vector<NativeEntry> natives2 = nativeRegistry();
    arena2.natives = &natives2;
    std::string resolveError;
    int resolved = 0;
    VmResourceResolver resolver = [&](std::string_view name,
                                      uint32_t id) -> std::optional<uint32_t> {
      ++resolved;
      uint32_t result = 0;
      if (!packs.resolveLiteral(std::string(name), id, result, resolveError))
        return std::nullopt;
      return result;
    };
    ClassRuntime runtime2(load, arena2, &resolver);
    const auto initialized = runtime2.ensure("java.render.Frontend");
    if (!initialized.ok)
      std::printf("Frontend: %s\n", initialized.error.c_str());
    CHECK(initialized.ok);
    const VmStaticFields *frontend = nullptr;
    for (const auto &s : runtime2.staticFields())
      if (s.owner->name == "java.render.Frontend")
        frontend = &s;
    CHECK(frontend != nullptr);
    const auto *pic = objectField(frontend, "defLoadingPic");
    const auto *queue = objectField(frontend, "inputQueue");
    CHECK(pic && pic->runtimeClass->name == "java.util.resource.ResourceRef");
    CHECK(queue && queue->runtimeClass->name == "java.util.Vector");
    CHECK(resolved > 0);
    CHECK(objectField(frontend, "largeFont") == nullptr &&
          objectField(frontend, "render") == nullptr);

    uint32_t loadingId = 0, moneyId = 0;
    CHECK(packs.resolveLiteral("frontend.rpk", 0xa8, loadingId, resolveError));
    CHECK(packs.resolveLiteral("frontend.rpk", 0x9b, moneyId, resolveError));
    CHECK(intField(frontend, "SFX_MONEY") == static_cast<int32_t>(moneyId));
    const VmResourceState *picState =
        pic ? nativeResourceState(pic, &arena2) : nullptr;
    CHECK(picState && picState->words[2] == loadingId);
    const auto binding =
        picState
            ? game.bindings.find(static_cast<int32_t>(
                                     picState - arena2.resourceStates.data()) +
                                 1)
            : game.bindings.end();
    CHECK(binding != game.bindings.end());
    if (binding != game.bindings.end()) {
      const OwnerLink &link = binding->second->link;
      CHECK(link.cachedValue == loadingId);
      const ResolvedNode node = resources.resolve(link.owner);
      CHECK(node.packNode && node.type == 7 && node.id == loadingId &&
            node.packNode->parentId != 0);
      // Under `backgrounds` (0xd2), under `OSD` (3), as the evidence file
      // records.
      uint32_t backgroundsId = 0, osdId = 0;
      CHECK(packs.resolveLiteral("frontend.rpk", 0xd2, backgroundsId,
                                 resolveError));
      CHECK(packs.resolveLiteral("frontend.rpk", 3, osdId, resolveError));
      CHECK(node.packNode && node.packNode->parent &&
            node.packNode->parent->value == backgroundsId);
      CHECK(node.packNode && node.packNode->parent &&
            node.packNode->parent->parent &&
            node.packNode->parent->parent->value == osdId);
    }
    CHECK(resources.mountState(0) == MountState::Mounted &&
          resources.missingIds().empty());

    // The real Vector() constructor chains Vector(I) and Vector(II) and
    // allocates its element array through `07/31` and `1c` field stores.
    Cb main("test.Main");
    std::vector<TufaNode> body = main.make("java.util.Vector");
    body.push_back(ret);
    const auto made =
        vmExecute(main.c, body, {}, &natives2, &runtime2.classes(),
                  &runtime2.staticFields(), &arena2);
    CHECK(made.ok);
    const auto *vector =
        made.ok ? std::get_if<const VmObject *>(&made.value) : nullptr;
    CHECK(vector && *vector &&
          (*vector)->runtimeClass->name == "java.util.Vector");
    const VmField *elements = nullptr;
    const VmField *increment = nullptr;
    if (vector && *vector)
      for (const auto &f : (*vector)->fields) {
        if (f.name == "elementData")
          elements = &f;
        if (f.name == "capacityIncrement")
          increment = &f;
      }
    CHECK(elements && std::get<const VmObject *>(elements->value) &&
          std::get<const VmObject *>(elements->value)->arrayElements.size() ==
              10);
    CHECK(increment && std::get<int32_t>(increment->value) == 0);
  }
  {
    World w;
    w.cls("test.Arg").constructor();
    auto &mixed = w.cls("test.Mixed");
    mixed.instanceField("chosen", "I")
        .markingConstructor("(ILtest.Arg;)", 1, "chosen")
        .markingConstructor("(Ltest.Arg;I)", 2, "chosen")
        .markingConstructor("(Ltest.Arg;ILtest.Arg;Ltest.Arg;)", 3, "chosen");
    w.start();
    const auto makeArg = w.main.make("test.Arg");
    const auto run = [&](std::vector<TufaNode> args, int count) {
      std::vector<TufaNode> body{node(0x07, true, 30),
                                 node(0x06, true, w.main.t("Ltest.Mixed;"))};
      body = concat(body, args);
      body.push_back(literal(count));
      body.push_back(node(0x21));
      body.push_back(ret);
      return w.run(body);
    };
    const auto chosen = [&](const VmResult &result) {
      if (!result.ok)
        return -1;
      const auto *obj = std::get_if<const VmObject *>(&result.value);
      if (!obj || !*obj)
        return -1;
      for (const auto &field : (*obj)->fields)
        if (field.name == "chosen")
          return std::get<int32_t>(field.value);
      return -1;
    };
    CHECK(chosen(run(concat({literal(9)}, makeArg), 2)) == 1);
    CHECK(chosen(run(concat(makeArg, {literal(9)}), 2)) == 2);
    const std::vector<TufaNode> nullValue{node(0x07, true, 28), node(0x0c)};
    auto args = concat(makeArg, {literal(9)});
    args = concat(concat(args, nullValue), nullValue);
    CHECK(chosen(run(args, 4)) == 3);
  }
  {
    World w;
    auto &native = w.cls("test.Native").instanceField("value", "I")
        .method("<init>", "(II)", 0x41, {});
    auto &child = w.cls("test.Child", "test.Native").constructor();
    w.start();
    const std::vector<NativeEntry> natives{
        {"test.Native", "<init>", "(II)V", "test", nativeConstructor}};
    std::vector<TufaNode> body{node(0x07, true, 30),
                               node(0x06, true, w.main.t("Ltest.Native;")),
                               literal(3), literal(7), literal(2), node(0x21), ret};
    const auto result = vmExecute(w.main.c, body, {}, &natives,
                                  &w.runtime->classes(), &w.runtime->staticFields(), &w.arena);
    const auto *made = result.ok ? asObject(result.value) : nullptr;
    auto *value = fieldOf(made, "value");
    CHECK(value && std::get<int32_t>(value->value) == 37);

    const auto chain = [&](const TufaClass &executing, const VmObject *receiver,
                           uint8_t opcode, const std::vector<NativeEntry> *entries) {
      return vmExecute(executing,
                       {literal(91), literal(4), literal(2), literal(2),
                        node(opcode), ret},
                       {receiver}, entries, &w.runtime->classes(),
                       &w.runtime->staticFields(), &w.arena);
    };
    const auto sameClass = chain(native.c, made, 0x22, &natives);
    CHECK(sameClass.ok && std::get_if<int32_t>(&sameClass.value) &&
          std::get<int32_t>(sameClass.value) == 91);
    CHECK(value && std::get<int32_t>(value->value) == 42);

    const auto childResult = w.alloc("test.Child");
    const auto *childObject = childResult.ok ? asObject(childResult.value) : nullptr;
    const auto parent = chain(child.c, childObject, 0x23, &natives);
    CHECK(parent.ok && std::get_if<int32_t>(&parent.value) &&
          std::get<int32_t>(parent.value) == 91);
    auto *childValue = fieldOf(childObject, "value");
    CHECK(childValue && std::get<int32_t>(childValue->value) == 42);

    const auto missing = chain(native.c, made, 0x22, nullptr);
    CHECK(!missing.ok && contains(missing.error, "native not registered"));
    const std::vector<NativeEntry> failing{
        {"test.Native", "<init>", "(II)V", "test", failingConstructor}};
    const auto failed = chain(child.c, childObject, 0x23, &failing);
    CHECK(!failed.ok && failed.error == "constructor handler failed");
  }
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::puts("class initialization tests passed");
  return 0;
}
