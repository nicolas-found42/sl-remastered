// Qualified field access through chained dynamic receivers (stock evalName,
// FUN_00420d20): the port pushes a location for field steps and resolves
// subsequent steps on the receiver's runtime class. Also covers most-specific
// overload selection on ties (stock FUN_00405920): a candidate whose parameter
// is a strict subclass of another candidate's parameter wins.
#include "vm_test_harness.hpp"
#include "natives/registry.hpp"

using namespace vm_test;

namespace {

NativeResult voidNative(const std::vector<VmValue>&, const VmObject*, VmArena*,
                        const std::vector<const TufaClass*>*) { return std::monostate{}; }

void testDiscardedMismatchedReturn() {
  World w;
  auto &factory = w.cls("test.VoidFactory");
  factory.method("noop", "()V", 0x49, {});
  const auto name = factory.t("noop");
  factory.method("outer", "()Ltest.VoidFactory;", 9, {literal(0), node(0x24), node(0x11, true, 1), node(0x12, true, name), node(0x07, true, 27), node(0x28), ret});
  w.start();
  const std::vector<NativeEntry> natives{{"test.VoidFactory", "noop", "()V", "test", voidNative}};
  const std::vector<TufaNode> body{literal(0), node(0x24), node(0x11, true, 2),
      node(0x19, true, w.main.cref("test.VoidFactory")),
      node(0x1a, true, w.main.ref("test.VoidFactory", "outer", "()Ltest.VoidFactory;")),
      node(0x08, true, 27), literal(42), ret};
  const auto result = vmExecute(w.main.c, body, {}, &natives, &w.runtime->classes(),
                                &w.runtime->staticFields(), &w.arena);
  CHECK(result.ok && std::get<int32_t>(result.value) == 42);
  if (!result.ok) std::printf("discarded return: %s\n", result.error.c_str());
}

NativeResult allocateFromNative(const std::vector<VmValue>&, const VmObject*, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
  auto made = vmAllocateNative(*arena, "test.Item");
  if (!made.ok) return made.error;
  const VmObject* object = asObject(made.value);
  if (!object) return std::string{"allocation did not return object"};
  auto* field = fieldOf(object, "marker");
  if (!field) return std::string{"field missing"};
  field->value = int32_t{12};
  const auto constructed = arena->callScript(object, "<init>", {int32_t{7}});
  if (!constructed.ok) return constructed.error;
  const auto handled = arena->callScript(object, "handle", {object});
  if (!handled.ok || !std::get_if<int32_t>(&handled.value) || std::get<int32_t>(handled.value) != 19)
    return std::string{"object upcall failed: "} + handled.error;
  return VmValue{object};
}

void testNativeAllocation() {
  World w;
  w.cls("test.Item").instanceField("marker", "I").markingConstructor("(I)", 42, "marker").method("handle", "(Ltest.Item;)I", 1, {literal(19), ret});
  w.cls("test.Factory").method("make", "()Ltest.Item;", 0x49, {});
  w.start();
  const std::vector<NativeEntry> natives{{"test.Factory", "make", "()Ltest.Item;", "test", allocateFromNative}};
  const std::vector<TufaNode> body{literal(0), node(0x24), node(0x11, true, 2),
      node(0x19, true, w.main.cref("test.Factory")),
      node(0x1a, true, w.main.ref("test.Factory", "make", "()Ltest.Item;")), ret};
  const auto result = vmExecute(w.main.c, body, {}, &natives, &w.runtime->classes(),
                                &w.runtime->staticFields(), &w.arena);
  const auto* object = result.ok ? asObject(result.value) : nullptr;
  CHECK(result.ok && object && fieldOf(object, "marker") &&
        std::get<int32_t>(fieldOf(object, "marker")->value) == 42);
  if (!result.ok) std::printf("native allocation: %s\n", result.error.c_str());
}

void testInheritedFieldReference() {
  World w;
  w.cls("test.Base").instanceField("marker", "I").constructor();
  auto &child = w.cls("test.Derived", "test.Base");
  child.constructor();
  const auto field = child.ref("test.Derived", "marker", "I");
  w.start();
  const auto made = w.alloc("test.Derived");
  const auto* object = made.ok ? asObject(made.value) : nullptr;
  CHECK(object && fieldOf(object, "marker"));
  if (!object) return;
  fieldOf(object, "marker")->value = int32_t{27};
  const std::vector<TufaNode> body{node(0x1b, true, field), ret};
  const auto result = vmExecute(child.c, body, {object}, nullptr,
                                &w.runtime->classes(), &w.runtime->staticFields(), &w.arena);
  CHECK(result.ok && std::get_if<int32_t>(&result.value) && std::get<int32_t>(result.value) == 27);
  const std::vector<TufaNode> bitOr{literal(32), node(0x1b, true, field), node(0x08, true, 46), retVoid};
  const auto changed = vmExecute(child.c, bitOr, {object}, nullptr,
                                 &w.runtime->classes(), &w.runtime->staticFields(), &w.arena);
  CHECK(changed.ok && std::get<int32_t>(fieldOf(object, "marker")->value) == 59);
  const std::vector<TufaNode> decrement{node(0x1b, true, field), node(0x08, true, 24), retVoid};
  const auto decremented = vmExecute(child.c, decrement, {object}, nullptr,
                                     &w.runtime->classes(), &w.runtime->staticFields(), &w.arena);
  CHECK(decremented.ok && std::get<int32_t>(fieldOf(object, "marker")->value) == 58);
  const std::vector<TufaNode> complement{node(0x1b, true, field), node(0x07, true, 20), ret};
  const auto flipped = vmExecute(child.c, complement, {object}, nullptr,
                                 &w.runtime->classes(), &w.runtime->staticFields(), &w.arena);
  CHECK(flipped.ok && std::get<int32_t>(flipped.value) == ~58 &&
        std::get<int32_t>(fieldOf(object, "marker")->value) == 58);
  const std::vector<TufaNode> logicalNot{node(0x1b, true, field), node(0x07, true, 21), ret};
  const auto negated = vmExecute(child.c, logicalNot, {object}, nullptr,
                                 &w.runtime->classes(), &w.runtime->staticFields(), &w.arena);
  CHECK(negated.ok && std::get<int32_t>(negated.value) == 0 &&
        std::get<int32_t>(fieldOf(object, "marker")->value) == 58);
  const std::vector<TufaNode> preIncrement{node(0x1b, true, field), node(0x07, true, 25), ret};
  const auto pre = vmExecute(child.c, preIncrement, {object}, nullptr,
                             &w.runtime->classes(), &w.runtime->staticFields(), &w.arena);
  CHECK(pre.ok && std::get<int32_t>(pre.value) == 59 && std::get<int32_t>(fieldOf(object, "marker")->value) == 59);
  const std::vector<TufaNode> discardPre{node(0x1b, true, field), node(0x08, true, 26), retVoid};
  const auto discarded = vmExecute(child.c, discardPre, {object}, nullptr,
                                   &w.runtime->classes(), &w.runtime->staticFields(), &w.arena);
  CHECK(discarded.ok && std::get<int32_t>(fieldOf(object, "marker")->value) == 58);
  if (!result.ok) std::printf("inherited field: %s\n", result.error.c_str());
}

void testIntegerToFloatField() {
  World w;
  auto &item = w.cls("test.FloatItem");
  item.instanceField("value", "F").constructor();
  w.start();
  const auto made = w.alloc("test.FloatItem");
  const auto* object = made.ok ? asObject(made.value) : nullptr;
  CHECK(object);
  if (!object) return;
  const std::vector<TufaNode> body{literal(500), node(0x11, true, 2), node(0x01, true, 0),
      node(0x1b, true, w.main.ref("test.FloatItem", "value", "F")),
      node(0x08, true, 35), retVoid};
  const auto result = w.run(body, {object});
  CHECK(result.ok && std::get<float>(fieldOf(object, "value")->value) == 500.0f);
  const std::vector<TufaNode> plus{literal(2), node(0x11, true, 2), node(0x01, true, 0),
      node(0x1b, true, w.main.ref("test.FloatItem", "value", "F")),
      node(0x08, true, 39), retVoid};
  CHECK(w.run(plus, {object}).ok && std::get<float>(fieldOf(object, "value")->value) == 502.0f);
  const std::vector<TufaNode> divide{literal(2), node(0x11, true, 2), node(0x01, true, 0),
      node(0x1b, true, w.main.ref("test.FloatItem", "value", "F")),
      node(0x08, true, 37), retVoid};
  CHECK(w.run(divide, {object}).ok && std::get<float>(fieldOf(object, "value")->value) == 251.0f);
  const std::vector<TufaNode> subtract{literal(1), node(0x11, true, 2), node(0x01, true, 0),
      node(0x1b, true, w.main.ref("test.FloatItem", "value", "F")),
      node(0x08, true, 40), retVoid};
  CHECK(w.run(subtract, {object}).ok && std::get<float>(fieldOf(object, "value")->value) == 250.0f);
}

void testReferenceArgumentAssignment() {
  World w;
  w.cls("test.Value").constructor();
  auto &helper = w.cls("test.Helper");
  helper.method("replace", "(Ltest.Value;)I", 9,
      {node(0x01, true, 0), node(0x01, true, 0), node(0x08, true, 35), literal(1), ret});
  w.start();
  const auto made = w.alloc("test.Value");
  const auto* object = made.ok ? asObject(made.value) : nullptr;
  CHECK(object);
  if (!object) return;
  const std::vector<TufaNode> body{node(0x01, true, 0), literal(1), node(0x24),
      node(0x11, true, 2), node(0x19, true, w.main.cref("test.Helper")),
      node(0x1a, true, w.main.ref("test.Helper", "replace", "(Ltest.Value;)I")), ret};
  const auto result = w.run(body, {object});
  CHECK(result.ok && std::get<int32_t>(result.value) == 1);
  if (!result.ok) std::printf("reference assignment: %s\n", result.error.c_str());
}

void testFloatComparison() {
  uint32_t first, second;
  const float a = 1.5f, b = 1.0f;
  std::memcpy(&first, &a, sizeof first);
  std::memcpy(&second, &b, sizeof second);
  const auto result = vmExecute({node(0x07, true, 28), node(0x0a, true, first),
                                 node(0x07, true, 28), node(0x0a, true, second),
                                 node(0x07, true, 8), ret},
                                {});
  CHECK(result.ok && std::get<int32_t>(result.value) == 1);
}

void testStaticFieldChain() {
  World w;
  w.cls("test.Controller").constructor().method("get", "()I", 1, {literal(88), ret});
  w.cls("test.Player").instanceField("value", "I").instanceField("controller", "Ltest.Controller;").constructor();
  auto &game = w.cls("test.Game");
  game.staticField("player", "Ltest.Player;", {}).constructor();
  const auto playerRef = game.ref("test.Game", "player", "Ltest.Player;");
  const auto valueRef = game.ref("test.Player", "value", "I");
  const auto controllerRef = game.ref("test.Player", "controller", "Ltest.Controller;");
  const auto getRef = game.ref("test.Controller", "get", "()I");
  w.start();
  const auto gameObject = w.alloc("test.Game");
  const auto playerObject = w.alloc("test.Player");
  const auto controllerObject = w.alloc("test.Controller");
  const auto* player = playerObject.ok ? asObject(playerObject.value) : nullptr;
  CHECK(gameObject.ok && player);
  if (!gameObject.ok || !player) return;
  fieldOf(player, "value")->value = int32_t{31};
  CHECK(controllerObject.ok);
  if (!controllerObject.ok) return;
  fieldOf(player, "controller")->value = asObject(controllerObject.value);
  for (auto& state : w.runtime->staticFields())
    if (state.owner == &game.c)
      for (auto& field : state.fields)
        if (field.name == "player") field.value = player;
  const std::vector<TufaNode> body{node(0x11, true, 2), node(0x1b, true, playerRef),
                                   node(0x1b, true, valueRef), ret};
  const auto result = vmExecute(game.c, body, {asObject(gameObject.value)}, nullptr,
                                &w.runtime->classes(), &w.runtime->staticFields(), &w.arena);
  CHECK(result.ok && std::get<int32_t>(result.value) == 31);
  const auto staticResult = vmExecute(game.c, body, {}, nullptr,
                                      &w.runtime->classes(), &w.runtime->staticFields(), &w.arena);
  CHECK(staticResult.ok && std::get<int32_t>(staticResult.value) == 31);
  if (!result.ok) std::printf("static chain: %s\n", result.error.c_str());
  const std::vector<TufaNode> qualified{node(0x11, true, 3), node(0x19, true, w.main.cref("test.Game")),
      node(0x1b, true, w.main.ref("test.Game", "player", "Ltest.Player;")),
      node(0x1b, true, w.main.ref("test.Player", "value", "I")), ret};
  const auto qualifiedResult = w.run(qualified);
  CHECK(qualifiedResult.ok && std::get<int32_t>(qualifiedResult.value) == 31);
  if (!qualifiedResult.ok) std::printf("qualified static chain: %s\n", qualifiedResult.error.c_str());
  const std::vector<TufaNode> call{literal(0), node(0x24), node(0x11, true, 3),
      node(0x1b, true, playerRef), node(0x1b, true, controllerRef),
      node(0x1a, true, getRef), ret};
  const auto called = vmExecute(game.c, call, {asObject(gameObject.value)}, nullptr,
                                &w.runtime->classes(), &w.runtime->staticFields(), &w.arena);
  CHECK(called.ok && std::get<int32_t>(called.value) == 88);
  if (!called.ok) std::printf("static chain call: %s\n", called.error.c_str());
}

void testStaticFieldThroughObject() {
  World w;
  w.cls("test.Base").staticField("count", "I", {literal(7), ret}).constructor();
  w.cls("test.Child", "test.Base").constructor();
  const auto ref = w.main.ref("test.Base", "count", "I");
  w.start();
  const auto made = w.alloc("test.Child");
  CHECK(made.ok);
  if (!made.ok) return;
  const std::vector<TufaNode> read{node(0x11, true, 2), node(0x01, true, 0),
                                   node(0x1b, true, ref), ret};
  const auto result = w.run(read, {made.value});
  CHECK(result.ok && std::get_if<int32_t>(&result.value) && std::get<int32_t>(result.value) == 7);
  const std::vector<TufaNode> write{literal(13), node(0x11, true, 2), node(0x01, true, 0),
                                    node(0x1b, true, ref), node(0x08, true, 35), node(0x2a)};
  CHECK(w.run(write, {made.value}).ok);
  const auto after = w.run(read, {made.value});
  CHECK(after.ok && std::get_if<int32_t>(&after.value) && std::get<int32_t>(after.value) == 13);
  CHECK(!w.run(read, {static_cast<const VmObject *>(nullptr)}).ok);
}

void testStaticDynamicSelf() {
  World w;
  w.cls("test.Arg").constructor();
  auto &helper = w.cls("test.DynamicHelper");
  helper.method("inner", "()I", 9, {literal(73), ret});
  const auto innerName = helper.t("inner");
  helper.method("outer", "(Ltest.Arg;)I", 9,
      {literal(0), node(0x24), node(0x11, true, 1), node(0x12, true, innerName), ret});
  w.start();
  const auto object = w.alloc("test.Arg");
  CHECK(object.ok);
  if (!object.ok) return;
  const std::vector<TufaNode> body{node(0x01, true, 0), literal(1), node(0x24),
      node(0x11, true, 2), node(0x19, true, w.main.cref("test.DynamicHelper")),
      node(0x1a, true, w.main.ref("test.DynamicHelper", "outer", "(Ltest.Arg;)I")), ret};
  const auto result = w.run(body, {object.value});
  CHECK(result.ok && std::get<int32_t>(result.value) == 73);
  if (!result.ok) std::printf("static dynamic self: %s\n", result.error.c_str());
}

void testHostUpcalls() {
  World w;
  w.cls("test.HostItem").instanceField("marker", "I").markingConstructor("(I)", 42, "marker").method("handle", "(Ltest.HostItem;)I", 1, {literal(19), ret}).method("empty", "()V", 1, {}).method("emptyValue", "()I", 1, {});
  w.start();
  auto allocated = vmHostAllocate(w.arena, "test.HostItem", nullptr,
                                  &w.runtime->classes(), &w.runtime->staticFields());
  const auto* object = allocated.ok ? asObject(allocated.value) : nullptr;
  CHECK(object);
  if (!object) return;
  auto constructed = vmHostCall(w.arena, object, "<init>", {int32_t{7}}, nullptr,
                                &w.runtime->classes(), &w.runtime->staticFields());
  CHECK(constructed.ok && std::get<int32_t>(fieldOf(object, "marker")->value) == 42);
  auto called = vmHostCall(w.arena, object, "handle", {object}, nullptr,
                           &w.runtime->classes(), &w.runtime->staticFields());
  CHECK(called.ok && std::get<int32_t>(called.value) == 19);
  auto empty = vmHostCall(w.arena, object, "empty", {}, nullptr,
                          &w.runtime->classes(), &w.runtime->staticFields());
  CHECK(empty.ok);
  auto emptyValue = vmHostCall(w.arena, object, "emptyValue", {}, nullptr,
                               &w.runtime->classes(), &w.runtime->staticFields());
  CHECK(!emptyValue.ok);
  CHECK(!w.arena.activeExecution && !w.arena.callScript);
}

void testDynamicInterfaceArgument() {
  World w;
  w.cls("test.State").interfaceKind();
  w.cls("test.Screen").implements("test.State").constructor();
  auto &helper = w.cls("test.StateHelper");
  helper.method("change", "(Ltest.State;)I", 9, {literal(61), ret});
  const auto name = helper.t("change");
  helper.method("call", "(Ltest.Screen;)I", 9,
      {node(0x01, true, 0), literal(1), node(0x24), node(0x11, true, 1),
       node(0x12, true, name), ret});
  w.start();
  const auto screen = w.alloc("test.Screen");
  CHECK(screen.ok);
  if (!screen.ok) return;
  const std::vector<TufaNode> body{node(0x01, true, 0), literal(1), node(0x24),
      node(0x11, true, 2), node(0x19, true, w.main.cref("test.StateHelper")),
      node(0x1a, true, w.main.ref("test.StateHelper", "call", "(Ltest.Screen;)I")), ret};
  const auto result = w.run(body, {screen.value});
  CHECK(result.ok && std::get<int32_t>(result.value) == 61);
  if (!result.ok) std::printf("interface argument: %s\n", result.error.c_str());
  const std::vector<TufaNode> instanceOf{node(0x01, true, 0), node(0x07, true, 32),
      node(0x06, true, w.main.t("Ltest.State;")), ret};
  const auto match = w.run(instanceOf, {screen.value});
  CHECK(match.ok && std::get<int32_t>(match.value) == 1);
  const auto nullValue = w.run(instanceOf, {static_cast<const VmObject*>(nullptr)});
  CHECK(nullValue.ok && std::get<int32_t>(nullValue.value) == 0);
}

void testDynamicFloatPromotion() {
  World w;
  auto &target = w.cls("test.FloatCall");
  target.constructor().method("take", "(F)F", 1, {node(0x01, true, 1), ret});
  w.start();
  const auto object = w.alloc("test.FloatCall");
  CHECK(object.ok);
  if (!object.ok) return;
  const std::vector<TufaNode> body{literal(5), literal(1), node(0x01, true, 0),
      node(0x25, true, w.main.t("take")), ret};
  const auto result = w.run(body, {object.value});
  CHECK(result.ok && std::get<float>(result.value) == 5.0f);
  if (!result.ok) std::printf("float promotion: %s\n", result.error.c_str());
}

void testSuperclassDynamicCall() {
  World w;
  w.cls("test.Super").constructor().method("get", "()I", 1, {literal(11), ret});
  auto &child = w.cls("test.Sub", "test.Super");
  const auto name = child.t("get");
  child.constructor().method("get", "()I", 1, {literal(22), ret}).method("baseValue", "()I", 1, {literal(0), node(0x26, true, name), ret});
  w.start();
  const auto allocated = w.alloc("test.Sub");
  CHECK(allocated.ok);
  if (!allocated.ok) return;
  const std::vector<TufaNode> body{literal(0), node(0x01, true, 0),
      node(0x25, true, w.main.t("baseValue")), ret};
  const auto result = w.run(body, {allocated.value});
  CHECK(result.ok && std::get<int32_t>(result.value) == 11);
  if (!result.ok) std::printf("super call: %s\n", result.error.c_str());
}

void testImplicitVirtualOverride() {
  World w;
  auto &base = w.cls("test.VirtualBase");
  const auto ref = base.ref("test.VirtualBase", "hook", "()I");
  base.constructor().method("hook", "()I", 1, {literal(11), ret}).method("run", "()I", 1, {literal(0), node(0x24), node(0x1a, true, ref), ret});
  w.cls("test.VirtualChild", "test.VirtualBase").constructor().method("hook", "()I", 1, {literal(22), ret});
  w.start();
  const auto object = w.alloc("test.VirtualChild");
  CHECK(object.ok);
  if (!object.ok) return;
  const auto result = w.run({literal(0), node(0x01, true, 0), node(0x25, true, w.main.t("run")), ret}, {object.value});
  CHECK(result.ok && std::get<int32_t>(result.value) == 22);
  if (!result.ok) std::printf("virtual override: %s\n", result.error.c_str());
}

void testReferenceCast() {
  World w;
  w.cls("test.CastBase").constructor();
  w.cls("test.CastChild", "test.CastBase").constructor();
  w.cls("test.CastOther").constructor();
  const auto base = w.main.cref("test.CastBase");
  w.start();
  const auto child = w.alloc("test.CastChild"), other = w.alloc("test.CastOther");
  CHECK(child.ok && other.ok);
  const std::vector<TufaNode> body{node(0x01, true, 0), node(0x07, true, 33), node(0x19, true, base), ret};
  CHECK(w.run(body, {child.value}).ok);
  CHECK(w.run(body, {static_cast<const VmObject *>(nullptr)}).ok);
  CHECK(!w.run(body, {other.value}).ok);
}

void testNullReferenceNot() {
  World w;
  auto &holder = w.cls("test.NullHolder");
  holder.instanceField("value", "Ljava.lang.Object;").constructor();
  const auto field = holder.ref("test.NullHolder", "value", "Ljava.lang.Object;");
  w.start();
  const auto made = w.alloc("test.NullHolder");
  CHECK(made.ok);
  if (!made.ok) return;
  const auto *object = asObject(made.value);
  const auto result = vmExecute(holder.c, {node(0x1b, true, field), node(0x07, true, 21), ret}, {object}, nullptr,
                                &w.runtime->classes(), &w.runtime->staticFields(), &w.arena);
  CHECK(result.ok && std::get<int32_t>(result.value) == 1);
  CHECK(std::get<const VmObject *>(fieldOf(object, "value")->value) == nullptr);
}

void testEmptyVoidMethod() {
  World w;
  auto &target = w.cls("test.Empty");
  target.constructor().method("noop", "()V", 1, {});
  w.start();
  const auto object = w.alloc("test.Empty");
  CHECK(object.ok);
  if (!object.ok) return;
  const std::vector<TufaNode> body{literal(0), node(0x01, true, 0),
      node(0x25, true, w.main.t("noop")), node(0x08, true, 27), literal(8), ret};
  const auto result = w.run(body, {object.value});
  CHECK(result.ok && std::get<int32_t>(result.value) == 8);
}

void testLocalArrayLength() {
  World w;
  w.start();
  auto array = std::make_unique<VmObject>();
  array->arrayDescriptor = "[I";
  array->arrayElements = {int32_t{1}, int32_t{2}, int32_t{3}};
  const VmObject* object = array.get();
  w.arena.objects.push_back(std::move(array));
  const std::vector<TufaNode> body{node(0x11, true, 2), node(0x01, true, 0),
      node(0x1b, true, w.main.ref("[I", "length", "I")), ret};
  const auto result = w.run(body, {object});
  CHECK(result.ok && std::get_if<int32_t>(&result.value) && std::get<int32_t>(result.value) == 3);
  CHECK(!w.run(body, {static_cast<const VmObject*>(nullptr)}).ok);
}

void testStackReceiverCall() {
  World w;
  auto &value = w.cls("test.Value");
  value.constructor().method("echo", "(I)I", 1, {node(0x01, true, 1), ret});
  w.start();
  const auto allocated = w.alloc("test.Value");
  const auto *receiver = asObject(allocated.value);
  CHECK(allocated.ok && receiver);
  if (!receiver) return;
  const std::vector<TufaNode> call{literal(37), literal(1), node(0x01, true, 0),
      node(0x25, true, w.main.t("echo")), ret};
  const auto result = w.run(call, {receiver});
  CHECK(result.ok && std::get_if<int32_t>(&result.value) && std::get<int32_t>(result.value) == 37);
  CHECK(!w.run(call, {static_cast<const VmObject *>(nullptr)}).ok);
}

void testLocalFieldMethodCall() {
  World w;
  auto &value = w.cls("test.Value");
  value.constructor().method("get", "()I", 1, {literal(42), ret}).method("constant", "()I", 9, {literal(91), ret});
  auto &child = w.cls("test.Child", "test.Value");
  child.constructor().method("get", "()I", 1, {literal(43), ret});
  auto &holder = w.cls("test.Holder");
  holder.instanceField("value", "Ltest.Value;").constructor();
  w.start();
  const auto holderResult = w.alloc("test.Holder");
  const auto valueResult = w.alloc("test.Child");
  CHECK(holderResult.ok && valueResult.ok);
  const VmObject *holderObject = asObject(holderResult.value);
  const VmObject *valueObject = asObject(valueResult.value);
  if (!holderObject || !valueObject) return;
  fieldOf(holderObject, "value")->value = valueObject;
  const std::vector<TufaNode> call{
      literal(0), node(0x24), node(0x11, true, 3), node(0x01, true, 0),
      node(0x1b, true, w.main.ref("test.Holder", "value", "Ltest.Value;")),
      node(0x1a, true, w.main.ref("test.Value", "get", "()I")), ret};
  const auto result = w.run(call, {holderObject});
  CHECK(result.ok && std::get_if<int32_t>(&result.value) &&
        std::get<int32_t>(result.value) == 43);
  const std::vector<TufaNode> staticCall{
      literal(0), node(0x24), node(0x11, true, 3), node(0x01, true, 0),
      node(0x1b, true, w.main.ref("test.Holder", "value", "Ltest.Value;")),
      node(0x1a, true, w.main.ref("test.Value", "constant", "()I")), ret};
  const auto staticResult = w.run(staticCall, {holderObject});
  CHECK(staticResult.ok && std::get<int32_t>(staticResult.value) == 91);
  fieldOf(holderObject, "value")->value = static_cast<const VmObject *>(nullptr);
  CHECK(!w.run(call, {holderObject}).ok);
}

// Qualified-call receiver chain, deepest supported operands (case 0x24,
// src/vm.cpp ~3017): for operand n >= 3 the receiver is a chain of n-1 nodes
// (a class/local/field start followed by field loads) ending at the object
// the qualified method is invoked on. operand 5 reaches through three field
// dereferences, operand 8 through six — the maximum the port accepts.
void testDeepQualifiedReceiverChain() {
  World w;
  w.cls("test.C0").instanceField("f1", "Ltest.C1;").constructor();
  w.cls("test.C1").instanceField("f2", "Ltest.C2;").constructor();
  w.cls("test.C2").instanceField("f3", "Ltest.C3;").constructor();
  auto& c3 = w.cls("test.C3");
  c3.instanceField("f4", "Ltest.C4;").constructor().method("get", "()I", 1, {literal(33), ret});
  auto& c4 = w.cls("test.C4");
  c4.instanceField("f5", "Ltest.C5;").constructor().method("get", "()I", 1, {literal(44), ret});
  w.cls("test.C5").instanceField("f6", "Ltest.C6;").constructor();
  w.cls("test.C6").constructor().method("get", "()I", 1, {literal(66), ret});
  w.start();

  const VmObject* objects[7] = {};
  bool allocated = true;
  for (int i = 0; i < 7; ++i) {
    const auto result = w.alloc("test.C" + std::to_string(i));
    allocated = allocated && result.ok;
    objects[i] = result.ok ? asObject(result.value) : nullptr;
  }
  CHECK(allocated);
  if (!allocated)
    return;
  for (int i = 0; i < 6; ++i) {
    auto* field = fieldOf(objects[i], "f" + std::to_string(i + 1));
    CHECK(field != nullptr);
    if (field)
      field->value = objects[i + 1];
  }

  // operand 5: local0.f1.f2.f3, then get() on the C3 object (33, not the
  // intermediate C4's 44 — the intermediate get must not run).
  const std::vector<TufaNode> call5{
      literal(0), node(0x24), node(0x11, true, 5), node(0x01, true, 0),
      node(0x1b, true, w.main.ref("test.C0", "f1", "Ltest.C1;")),
      node(0x1b, true, w.main.ref("test.C1", "f2", "Ltest.C2;")),
      node(0x1b, true, w.main.ref("test.C2", "f3", "Ltest.C3;")),
      node(0x1a, true, w.main.ref("test.C3", "get", "()I")), ret};
  const auto result5 = w.run(call5, {objects[0]});
  CHECK(result5.ok && std::get_if<int32_t>(&result5.value) &&
        std::get<int32_t>(result5.value) == 33);
  if (!result5.ok)
    std::printf("chain n=5: %s\n", result5.error.c_str());

  // operand 8: local0.f1...f6, then get() on the final C6 object.
  const std::vector<TufaNode> call8{
      literal(0), node(0x24), node(0x11, true, 8), node(0x01, true, 0),
      node(0x1b, true, w.main.ref("test.C0", "f1", "Ltest.C1;")),
      node(0x1b, true, w.main.ref("test.C1", "f2", "Ltest.C2;")),
      node(0x1b, true, w.main.ref("test.C2", "f3", "Ltest.C3;")),
      node(0x1b, true, w.main.ref("test.C3", "f4", "Ltest.C4;")),
      node(0x1b, true, w.main.ref("test.C4", "f5", "Ltest.C5;")),
      node(0x1b, true, w.main.ref("test.C5", "f6", "Ltest.C6;")),
      node(0x1a, true, w.main.ref("test.C6", "get", "()I")), ret};
  const auto result8 = w.run(call8, {objects[0]});
  CHECK(result8.ok && std::get_if<int32_t>(&result8.value) &&
        std::get<int32_t>(result8.value) == 66);
  if (!result8.ok)
    std::printf("chain n=8: %s\n", result8.error.c_str());

  // operand 9 is beyond the supported range.
  const std::vector<TufaNode> call9{
      literal(0), node(0x24), node(0x11, true, 9), node(0x01, true, 0),
      node(0x1b, true, w.main.ref("test.C0", "f1", "Ltest.C1;")),
      node(0x1b, true, w.main.ref("test.C1", "f2", "Ltest.C2;")),
      node(0x1b, true, w.main.ref("test.C2", "f3", "Ltest.C3;")),
      node(0x1b, true, w.main.ref("test.C3", "f4", "Ltest.C4;")),
      node(0x1b, true, w.main.ref("test.C4", "f5", "Ltest.C5;")),
      node(0x1b, true, w.main.ref("test.C5", "f6", "Ltest.C6;")),
      node(0x1b, true, w.main.ref("test.C6", "get", "()I")), ret};
  CHECK(!w.run(call9, {objects[0]}).ok);
}

// test.Osd { Object globalHandler; } and test.Me { Osd osd; } model the stock
// Dialog relationship: the constructor stores `this` into a helper object's
// field through a chained qualified access (LoadingDialog tree 1:
// `01 local0, 11(2){1b osd, 1b globalHandler}, 08(35)`).
void testChainedStore() {
  World w;
  auto &osd = w.cls("test.Osd");
  osd.instanceField("globalHandler", "Ljava.lang.Object;").constructor();
  auto &me = w.cls("test.Me");
  me.instanceField("osd", "Ltest.Osd;").constructor();
  w.start();

  VmResult allocMe = w.alloc("test.Me");
  VmResult allocOsd = w.alloc("test.Osd");
  if (!allocMe.ok)
    std::printf("  allocMe error: %s\n", allocMe.error.c_str());
  if (!allocOsd.ok)
    std::printf("  allocOsd error: %s\n", allocOsd.error.c_str());
  CHECK(allocMe.ok && allocOsd.ok);
  const VmObject *meObject = asObject(allocMe.value);
  const VmObject *osdObject = asObject(allocOsd.value);
  CHECK(meObject && osdObject);

  // The real constructor allocates its helper first; seed me.osd with it so
  // the chain has a receiver.
  fieldOf(meObject, "osd")->value = osdObject;

  std::vector<TufaNode> body{
      node(0x01, true, 0), // push this
      node(0x11, true, 2),
      TufaNode{0x1b, 0, true, w.main.ref("test.Me", "osd", "Ltest.Osd;")},
      TufaNode{0x1b, 0, true,
               w.main.ref("test.Osd", "globalHandler", "Ljava.lang.Object;")},
      node(0x08, true, 35),
      node(0x2a)};
  VmResult stored = w.run(body, {meObject, osdObject});
  CHECK(stored.ok);
  if (!stored.ok) {
    std::printf("  store error: %s\n", stored.error.c_str());
    return;
  }
  const VmField *handler = fieldOf(osdObject, "globalHandler");
  CHECK(handler);
  const VmObject *const *value = std::get_if<const VmObject *>(&handler->value);
  CHECK(value && *value == meObject);
}

// The same chain read as a value: `11(2){1b osd, 1b globalHandler}` returns
// the helper object's field value, with each step resolved on the runtime
// class of the previous result.
void testChainedValueRead() {
  World w;
  auto &osd = w.cls("test.Osd");
  osd.instanceField("globalHandler", "Ljava.lang.Object;").constructor();
  auto &me = w.cls("test.Me");
  me.instanceField("osd", "Ltest.Osd;").constructor();
  w.start();

  VmResult allocMe = w.alloc("test.Me");
  VmResult allocOsd = w.alloc("test.Osd");
  CHECK(allocMe.ok && allocOsd.ok);
  const VmObject *meObject = asObject(allocMe.value);
  const VmObject *osdObject = asObject(allocOsd.value);

  // Pre-seed the chain so the read crosses a real receiver.
  fieldOf(meObject, "osd")->value = osdObject;
  fieldOf(osdObject, "globalHandler")->value = meObject;

  std::vector<TufaNode> body{
      node(0x11, true, 2),
      TufaNode{0x1b, 0, true, w.main.ref("test.Me", "osd", "Ltest.Osd;")},
      TufaNode{0x1b, 0, true,
               w.main.ref("test.Osd", "globalHandler", "Ljava.lang.Object;")},
      ret};
  VmResult read = w.run(body, {meObject, osdObject});
  CHECK(read.ok);
  if (!read.ok) {
    std::printf("  read error: %s\n", read.error.c_str());
    return;
  }
  const VmObject **value = std::get_if<const VmObject *>(&read.value);
  CHECK(value && *value == meObject);
}

// Overload selection with a subclass argument: show(Dialog) and show(Res)
// both fit a Simple instance; the stricter parameter wins (stock picks the
// most specific overload, without a tie error).
void testMostSpecificOverload() {
  World w;
  w.cls("test.Res").constructor();
  w.cls("test.Dialog", "test.Res").constructor();
  w.cls("test.Simple", "test.Dialog").constructor();
  auto &screen = w.cls("test.Screen");
  screen.constructor().method("show", "(Ltest.Dialog;)I", 1, {literal(1), ret});
  screen.method("show", "(Ltest.Res;)I", 1, {literal(2), ret});
  w.start();

  VmResult allocScreen = w.alloc("test.Screen");
  CHECK(allocScreen.ok);
  const VmObject *screenObject = asObject(allocScreen.value);
  CHECK(screenObject);

  uint32_t simpleDesc = w.main.t("Ltest.Simple;");
  uint32_t showName = w.main.t("show");

  std::vector<TufaNode> body{node(0x07, true, 30),
                             node(0x06, true, simpleDesc),
                             literal(0),
                             node(0x21),
                             literal(1), // argument count
                             node(0x24),
                             node(0x11, true, 1),
                             node(0x12, true, showName),
                             ret};
  VmResult called = w.run(body, {screenObject});
  CHECK(called.ok);
  if (!called.ok) {
    std::printf("  call error: %s\n", called.error.c_str());
    return;
  }
  const auto *picked = std::get_if<int32_t>(&called.value);
  CHECK(picked && *picked == 1);
}

} // namespace

int main() {
  {
    World w;
    w.start();
    // Stock Vector.removeElement returns true/false using literal0x0d.
    for (uint32_t value : {0u, 1u}) {
      const std::vector<TufaNode> body{node(0x07, true, 28), node(0x0d, true, value), ret};
      const auto result = w.run(body);
      CHECK(result.ok && std::holds_alternative<int32_t>(result.value) && std::get<int32_t>(result.value) == static_cast<int32_t>(value));
    }
  }

  testNullReferenceNot();
  testReferenceCast();
  testImplicitVirtualOverride();
  testEmptyVoidMethod();
  testSuperclassDynamicCall();
  testDynamicFloatPromotion();
  testDynamicInterfaceArgument();
  testHostUpcalls();
  testDiscardedMismatchedReturn();
  testStaticDynamicSelf();
  testStaticFieldChain();
  testStaticFieldThroughObject();
  testFloatComparison();
  testReferenceArgumentAssignment();
  testIntegerToFloatField();
  testInheritedFieldReference();
  testNativeAllocation();
  testLocalArrayLength();
  testStackReceiverCall();
  testLocalFieldMethodCall();
  testDeepQualifiedReceiverChain();
  testChainedStore();
  testChainedValueRead();
  testMostSpecificOverload();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("vm_eval_name_test: all checks passed\n");
  return 0;
}
