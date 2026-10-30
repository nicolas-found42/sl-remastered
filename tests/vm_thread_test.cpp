#include "natives/registry.hpp"
#include "camera_frame.hpp"
#include "vm.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

static int failures = 0;
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::printf("FAIL line %d: %s\n", __LINE__, #c);                         \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

static std::vector<int> events;
static NativeResult record(const std::vector<VmValue> &args, const VmObject *,
                           VmArena *, const std::vector<const TufaClass *> *) {
  events.push_back(std::get<int32_t>(args[0]));
  return std::monostate{};
}
static const VmObject *callbackTarget = nullptr;
static NativeResult callNested(const std::vector<VmValue>&, const VmObject*, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
  const auto result = arena->callScript(callbackTarget, "run", {});
  return result.ok ? NativeResult{std::monostate{}} : NativeResult{result.error};
}
static const VmObject *waitObject = nullptr;
static NativeResult park(const std::vector<VmValue> &, const VmObject *,
                         VmArena *arena,
                         const std::vector<const TufaClass *> *) {
  const auto result = arena->scheduler->wait(waitObject);
  return result.ok ? NativeResult{std::monostate{}}
                   : NativeResult{result.error};
}
static TufaNode literal(int value) {
  return {0x27, 1, true, static_cast<uint32_t>(value)};
}
static TufaNode node(uint8_t op, uint32_t value = 0, bool wide = false) {
  return {op, 1, wide, value};
}

struct Script {
  TufaClass cls;
  uint32_t recordRef;
  uint32_t sleepRef;
  uint32_t text(const char *s) {
    cls.constants.push_back({0, s, 0, 0});
    return static_cast<uint32_t>(cls.constants.size() - 1);
  }
  uint32_t constant(uint32_t kind, uint32_t a, uint32_t b = 0) {
    cls.constants.push_back({kind, {}, a, b});
    return static_cast<uint32_t>(cls.constants.size() - 1);
  }
  Script(const char *name, int first, int second, bool sleep) {
    cls.name = name;
    cls.hasClassHeader = true;
    cls.classHeader[3] = UINT32_MAX;
    const auto owner = constant(4, text(name));
    const auto recordName = text("record"), recordType = text("(I)V");
    const auto sleepName = text("sleep"), sleepType = text("(F)V");
    const auto runName = text("run"), runType = text("()V");
    recordRef = constant(5, owner, constant(7, recordName, recordType));
    sleepRef = constant(5, owner, constant(7, sleepName, sleepType));
    cls.methods[0].push_back({{0x48, recordName, recordType, 0, 0}});
    cls.methods[0].push_back({{0x48, sleepName, sleepType, 0, 0}});
    cls.methods[0].push_back({{1, runName, runType, 0, 0}});
    auto mark = [&](int value) {
      cls.trees[0].insert(cls.trees[0].end(),
                          {literal(value), literal(1), node(0x24),
                           node(0x1a, recordRef, true), node(0x08, 27, true)});
    };
    cls.trees.emplace_back();
    mark(first);
    if (sleep) {
      float seconds = 5.0f;
      uint32_t bits;
      std::memcpy(&bits, &seconds, sizeof(bits));
      cls.trees[0].insert(cls.trees[0].end(),
                          {node(0x07, 28, true), node(0x0a, bits, true),
                           literal(1), node(0x24), node(0x1a, sleepRef, true),
                           node(0x08, 27, true)});
    }
    mark(second);
    cls.trees[0].push_back(node(0x2a));
  }
};

static VmObject *object(VmArena &arena, const TufaClass *cls) {
  auto value = std::make_unique<VmObject>();
  value->runtimeClass = cls;
  auto *result = value.get();
  arena.objects.push_back(std::move(value));
  return result;
}
static VmObject *thread(VmArena &arena, const VmObject *target) {
  auto *result = object(arena, nullptr);
  result->fields.emplace_back("ptr", int32_t{0}, "I");
  result->fields.emplace_back("target", target, "Ljava.lang.Runnable;");
  return result;
}

int main() {
  {
    // 07 suboperation 17: integer modulo, FUN_0041bbf0 (C remainder: sign follows the left operand).
    const auto remainder = vmExecute(
        {literal(-7), literal(3), node(0x07, 17, true), node(0x2b)}, {});
    CHECK(remainder.ok && std::get<int32_t>(remainder.value) == -1);
    CHECK(!vmExecute({literal(7), literal(0), node(0x07, 17, true), node(0x2b)}, {}).ok);
    CHECK(!vmExecute({literal(INT32_MIN), literal(-1), node(0x07, 17, true), node(0x2b)}, {}).ok);
    float half = 0.5f;
    uint32_t bits;
    std::memcpy(&bits, &half, sizeof(bits));
    CHECK(!vmExecute({literal(7), node(0x07, 28, true), node(0x0a, bits, true), node(0x07, 17, true),
                      node(0x2b)}, {}).ok);
  }
  {
    // 07 suboperation 33 with a 06 descriptor: primitive cast (stock 0x1021 arm, FUN_00417da0).
    TufaClass cls;
    cls.name = "test.PrimitiveCast";
    cls.constants.push_back({0, "I", 0, 0});
    cls.constants.push_back({0, "F", 0, 0});
    std::vector<const TufaClass *> classes{&cls};
    std::vector<VmStaticFields> states;
    VmArena arena;
    auto run = [&](std::vector<TufaNode> tree) {
      return vmExecute(cls, tree, {}, nullptr, &classes, &states, &arena);
    };
    float value = -2.75f;
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const auto truncated = run({node(0x07, 28, true), node(0x0a, bits, true), node(0x07, 33, true),
                                node(0x06, 0, true), node(0x2b)});
    CHECK(truncated.ok && std::holds_alternative<int32_t>(truncated.value) &&
          std::get<int32_t>(truncated.value) == -2);
    const auto widened = run({literal(7), node(0x07, 33, true), node(0x06, 1, true), node(0x2b)});
    CHECK(widened.ok && std::holds_alternative<float>(widened.value) && std::get<float>(widened.value) == 7.0f);
    const auto same = run({literal(5), node(0x07, 33, true), node(0x06, 0, true), node(0x2b)});
    CHECK(same.ok && std::get<int32_t>(same.value) == 5);
    float huge = 4.0e10f;
    std::memcpy(&bits, &huge, sizeof(bits));
    CHECK(!run({node(0x07, 28, true), node(0x0a, bits, true), node(0x07, 33, true), node(0x06, 0, true),
                node(0x2b)}).ok);
  }
  {
    const auto quotient = vmExecute(
        {literal(-7), literal(2), node(0x07, 18, true), node(0x2b)}, {});
    CHECK(quotient.ok && std::get<int32_t>(quotient.value) == -3);
    const auto zero = vmExecute(
        {literal(7), literal(0), node(0x07, 18, true), node(0x2b)}, {});
    CHECK(!zero.ok);
    float lhs = 4.0f, rhs = 3.0f;
    uint32_t a, b;
    std::memcpy(&a, &lhs, sizeof(a));
    std::memcpy(&b, &rhs, sizeof(b));
    const auto floats = vmExecute({node(0x07, 28, true), node(0x0a, a, true),
                                   node(0x07, 28, true), node(0x0a, b, true),
                                   node(0x07, 18, true), node(0x2b)},
                                  {});
    CHECK(floats.ok && std::get<float>(floats.value) == 4.0f / 3.0f);
  }
  {
    TufaClass cls;
    cls.name = "test.IntegerArray";
    cls.constants.push_back({0, "[I", 0, 0});
    std::vector<const TufaClass *> classes{&cls};
    std::vector<VmStaticFields> states;
    VmArena arena;
    const auto result = vmExecute(
        cls,
        {literal(2), node(0x07, 31, true), node(0x06, 0, true), node(0x2b)}, {},
        nullptr, &classes, &states, &arena);
    const auto *array = std::get_if<const VmObject *>(&result.value);
    CHECK(result.ok && array && *array && (*array)->arrayElements.size() == 2);
    if (result.ok && array && *array) {
      CHECK(std::get<int32_t>((*array)->arrayElements[0]) == 0);
      const auto write =
          vmExecute(cls,
                    {literal(42), literal(1), node(0x01, 0, true), node(0x20),
                     node(0x08, 35, true), literal(1), node(0x01, 0, true),
                     node(0x20), node(0x2b)},
                    {*array}, nullptr, &classes, &states, &arena);
      CHECK(write.ok && std::get<int32_t>(write.value) == 42);
      const auto compound =
          vmExecute(cls,
                    {literal(3), literal(1), node(0x01, 0, true), node(0x20),
                     node(0x08, 36, true), literal(1), node(0x01, 0, true),
                     node(0x20), node(0x2b)},
                    {*array}, nullptr, &classes, &states, &arena);
      CHECK(compound.ok && std::get<int32_t>(compound.value) == 126);
    }
  }
  {
    TufaClass cls;
    cls.name = "test.Slice";
    const std::vector<TufaNode> tree{literal(9), literal(3),
                                     node(0x07, 16, true), node(0x2b)};
    auto execution = vmStart(cls, tree, {}, nullptr, nullptr, nullptr, nullptr);
    bool finished = false;
    CHECK(vmResume(execution, 1, finished).ok && !finished);
    CHECK(vmResume(execution, 1, finished).ok && !finished);
    CHECK(vmResume(execution, 1, finished).ok && !finished);
    const auto result = vmResume(execution, 1, finished);
    CHECK(result.ok && finished && std::get<int32_t>(result.value) == 12);
  }
  {
    TufaClass cls;
    cls.name = "test.FloatSubtraction";
    const std::vector<TufaNode> tree{node(0x01, 0, true), node(0x01, 1, true),
                                     node(0x07, 15, true), node(0x2b)};
    const auto difference = vmExecute(cls, tree, {0.75f, 0.25f});
    CHECK(difference.ok && std::get<float>(difference.value) == 0.5f);
    const auto mixed = vmExecute(cls, tree, {int32_t{2}, 0.5f});
    CHECK(mixed.ok && std::get<float>(mixed.value) == 1.5f);
  }
  {
    TufaClass cls;
    cls.name = "test.Nested";
    cls.hasClassHeader = true;
    cls.classHeader[3] = UINT32_MAX;
    cls.constants = {
        {0, "test.Nested", 0, 0}, {4, {}, 0, 0}, {0, "child", 0, 0},
        {0, "()I", 0, 0},         {7, {}, 2, 3}, {5, {}, 1, 4}};
    cls.methods[0].push_back({{8, 2, 3, 0, 0}});
    cls.trees.push_back({literal(42), node(0x2b)});
    const std::vector<TufaNode> outer{literal(0), node(0x24),
                                      node(0x1a, 5, true), node(0x2b)};
    std::vector<const TufaClass *> classes{&cls};
    auto execution =
        vmStart(cls, outer, {}, nullptr, &classes, nullptr, nullptr);
    bool finished = false;
    CHECK(vmResume(execution, 2, finished).ok && !finished);
    CHECK(vmResume(execution, 1, finished).ok && !finished);
    CHECK(vmResume(execution, 1, finished).ok && !finished);
    const auto result = vmResume(execution, 1, finished);
    CHECK(result.ok && finished && std::get<int32_t>(result.value) == 42);
  }
  {
    Script first("test.First", 1, 3, false), second("test.Second", 2, 4, false);
    VmArena arena;
    std::vector<const TufaClass *> classes{&first.cls, &second.cls};
    std::vector<VmStaticFields> states;
    std::vector<NativeEntry> natives{
        {"test.First", "record", "(I)V", "test", record},
        {"test.Second", "record", "(I)V", "test", record}};
    double now = 0;
    VmScheduler scheduler(arena, &natives, &classes, &states,
                          [&] { return now; });
    auto *a = thread(arena, object(arena, &first.cls));
    auto *b = thread(arena, object(arena, &second.cls));
    CHECK(scheduler.createThread(a, "first").ok &&
          scheduler.createThread(b, "second").ok);
    events.clear();
    CHECK(scheduler.start(a).ok && scheduler.start(b).ok);
    CHECK(events.empty());
    for (int i = 0; i < 32 && scheduler.threadCount(); ++i)
      CHECK(scheduler.tick(1).ok);
    CHECK((events == std::vector<int>{1, 2, 3, 4}));
    CHECK(scheduler.threadCount() == 0);
  }
  {
    Script script("test.StaticRun", 7, 8, false);
    script.cls.methods[0][2].words[0] |= 8;
    VmArena arena;
    std::vector<const TufaClass*> classes{&script.cls};
    std::vector<VmStaticFields> states;
    std::vector<NativeEntry> natives{{"test.StaticRun", "record", "(I)V", "test", record}};
    VmScheduler scheduler(arena, &natives, &classes, &states, [] { return 0.0; });
    auto* owner = thread(arena, object(arena, &script.cls));
    events.clear();
    CHECK(scheduler.createThread(owner, "static run").ok && scheduler.start(owner).ok);
    CHECK(scheduler.tick(100).ok && (events == std::vector<int>{7, 8}));
  }
  {
    Script first("test.Sleep", 1, 3, true), second("test.Awake", 2, 4, false);
    VmArena arena;
    std::vector<const TufaClass *> classes{&first.cls, &second.cls};
    std::vector<VmStaticFields> states;
    NativeHandler sleepHandler = nullptr;
    for (const auto &entry : nativeRegistry())
      if (entry.className == "java.lang.Thread" && entry.method == "sleep" &&
          entry.descriptor == "(F)V")
        sleepHandler = entry.handler;
    CHECK(sleepHandler != nullptr);
    if (!sleepHandler)
      return 1;
    std::vector<NativeEntry> natives{
        {"test.Sleep", "record", "(I)V", "test", record},
        {"test.Sleep", "sleep", "(F)V", "test", sleepHandler},
        {"test.Awake", "record", "(I)V", "test", record}};
    double now = 0;
    VmScheduler scheduler(arena, &natives, &classes, &states,
                          [&] { return now; });
    auto *a = thread(arena, object(arena, &first.cls));
    auto *b = thread(arena, object(arena, &second.cls));
    CHECK(scheduler.createThread(a, "sleeping").ok &&
          scheduler.createThread(b, "awake").ok);
    CHECK(scheduler.start(a).ok && scheduler.start(b).ok);
    events.clear();
    CHECK(scheduler.tick(100).ok);
    CHECK((events == std::vector<int>{1, 2, 4}));
    now = 5;
    CHECK(scheduler.tick(100).ok && events.size() == 3);
    now = 5.1;
    CHECK(scheduler.tick(100).ok && (events == std::vector<int>{1, 2, 4, 3}));
    auto *stopped = thread(arena, object(arena, &first.cls));
    CHECK(scheduler.createThread(stopped, "stopped").ok &&
          scheduler.start(stopped).ok);
    CHECK(scheduler.stop(stopped).ok && scheduler.threadCount() == 0);
    CHECK(scheduler.tick(100).ok && events.size() == 4);
  }
  {
    Script script("test.HostSleep", 1, 2, true);
    VmArena arena;
    std::vector<const TufaClass*> classes{&script.cls};
    std::vector<VmStaticFields> states;
    NativeHandler sleepHandler = nullptr;
    for (const auto& entry : nativeRegistry())
      if (entry.className == "java.lang.Thread" && entry.method == "sleep" && entry.descriptor == "(F)V")
        sleepHandler = entry.handler;
    std::vector<NativeEntry> natives{{"test.HostSleep", "record", "(I)V", "test", record},
                                     {"test.HostSleep", "sleep", "(F)V", "test", sleepHandler}};
    VmScheduler scheduler(arena, &natives, &classes, &states, [] { return 0.0; });
    const auto* receiver = object(arena, &script.cls);
    const auto result = vmHostCall(arena, receiver, "run", {}, &natives, &classes, &states);
    CHECK(!result.ok && result.error.find("sleep outside a scheduled thread") != std::string::npos);
  }
  {
    Script outer("test.CallbackOuter", 1, 2, false), inner("test.CallbackInner", 3, 4, true);
    VmArena arena;
    std::vector<const TufaClass*> classes{&outer.cls, &inner.cls};
    std::vector<VmStaticFields> states;
    NativeHandler sleepHandler = nullptr;
    for (const auto& entry : nativeRegistry())
      if (entry.className == "java.lang.Thread" && entry.method == "sleep" && entry.descriptor == "(F)V")
        sleepHandler = entry.handler;
    std::vector<NativeEntry> natives{{"test.CallbackOuter", "record", "(I)V", "test", callNested},
        {"test.CallbackInner", "record", "(I)V", "test", record},
        {"test.CallbackInner", "sleep", "(F)V", "test", sleepHandler}};
    VmScheduler scheduler(arena, &natives, &classes, &states, [] { return 0.0; });
    callbackTarget = object(arena, &inner.cls);
    auto* owner = thread(arena, object(arena, &outer.cls));
    CHECK(scheduler.createThread(owner, "callback").ok && scheduler.start(owner).ok);
    const auto result = scheduler.tick(100);
    CHECK(!result.ok && result.error.find("sleep in synchronous callback") != std::string::npos);
    callbackTarget = nullptr;
  }
  {
    Script script("test.NativeTarget", 7, 8, false);
    VmArena arena;
    std::vector<const TufaClass *> classes{&script.cls};
    std::vector<VmStaticFields> states;
    std::vector<NativeEntry> natives{
        {"test.NativeTarget", "record", "(I)V", "test", record}};
    double now = 0;
    VmScheduler scheduler(arena, &natives, &classes, &states,
                          [&] { return now; });
    auto *owner = thread(arena, object(arena, &script.cls));
    auto *name = object(arena, nullptr);
    arena.strings.emplace_back("native thread", name);
    CHECK(std::holds_alternative<std::monostate>(invokeNative(
        "java.lang.Thread", "init", "(Ljava.lang.String;)V",
        {static_cast<const VmObject *>(name)}, owner, &arena, &classes)));
    CHECK(scheduler.threadCount() == 1);
    CHECK(std::holds_alternative<std::monostate>(invokeNative(
        "java.lang.Thread", "start", "()V", {}, owner, &arena, &classes)));
    const auto alive = invokeNative("java.lang.Thread", "isAlive", "()I", {},
                                    owner, &arena, &classes);
    CHECK(std::holds_alternative<VmValue>(alive) &&
          std::get<int32_t>(std::get<VmValue>(alive)) == 0);
    CHECK(std::holds_alternative<std::monostate>(invokeNative(
        "java.lang.Thread", "stop", "()V", {}, owner, &arena, &classes)));
    CHECK(scheduler.threadCount() == 0 &&
          std::get<int32_t>(owner->fields[0].value) == 0);
  }
  {
    Script a("test.WaitA", 1, 4, true), b("test.WaitB", 2, 5, true),
        c("test.WaitC", 3, 6, true);
    VmArena arena;
    std::vector<const TufaClass *> classes{&a.cls, &b.cls, &c.cls};
    std::vector<VmStaticFields> states;
    std::vector<NativeEntry> natives{
        {"test.WaitA", "record", "(I)V", "test", record},
        {"test.WaitA", "sleep", "(F)V", "test", park},
        {"test.WaitB", "record", "(I)V", "test", record},
        {"test.WaitB", "sleep", "(F)V", "test", park},
        {"test.WaitC", "record", "(I)V", "test", record},
        {"test.WaitC", "sleep", "(F)V", "test", park}};
    VmScheduler scheduler(arena, &natives, &classes, &states,
                          [] { return 0.0; });
    waitObject = object(arena, nullptr);
    auto *other = object(arena, nullptr);
    auto *ta = thread(arena, object(arena, &a.cls));
    auto *tb = thread(arena, object(arena, &b.cls));
    auto *tc = thread(arena, object(arena, &c.cls));
    CHECK(scheduler.createThread(ta, "a").ok &&
          scheduler.createThread(tb, "b").ok &&
          scheduler.createThread(tc, "c").ok);
    CHECK(scheduler.start(ta).ok && scheduler.start(tb).ok &&
          scheduler.start(tc).ok);
    events.clear();
    CHECK(scheduler.tick(100).ok && (events == std::vector<int>{1, 2, 3}));
    CHECK(scheduler.tick(100).ok && events.size() == 3);
    CHECK(std::holds_alternative<std::monostate>(invokeNative(
        "java.lang.Object", "notify", "()V", {}, other, &arena, &classes)));
    CHECK(scheduler.tick(100).ok && events.size() == 3);
    CHECK(std::holds_alternative<std::monostate>(
        invokeNative("java.lang.Object", "notify", "()V", {}, waitObject,
                     &arena, &classes)));
    CHECK(scheduler.tick(100).ok && (events == std::vector<int>{1, 2, 3, 6}));
    CHECK(std::holds_alternative<std::monostate>(
        invokeNative("java.lang.Object", "notifyAll", "()V", {}, waitObject,
                     &arena, &classes)));
    CHECK(scheduler.tick(100).ok && events.size() == 6);
    CHECK(events.size() == 6 && ((events[4] == 4 && events[5] == 5) ||
                                 (events[4] == 5 && events[5] == 4)));
    waitObject = nullptr;
  }
  {
    Script a("test.WaitA", 1, 4, true);
    VmArena arena;
    std::vector<const TufaClass *> classes{&a.cls};
    TufaClass frontend; frontend.name = "java.render.Frontend";
    std::vector<VmStaticFields> states;
    std::vector<NativeEntry> natives{{"test.WaitA", "record", "(I)V", "test", record},
                                     {"test.WaitA", "sleep", "(F)V", "test", park}};
    VmScheduler scheduler(arena, &natives, &classes, &states, [] { return 0.0; });
    waitObject = object(arena, nullptr);
    auto *ta = thread(arena, object(arena, &a.cls));
    CHECK(scheduler.createThread(ta, "a").ok && scheduler.start(ta).ok);
    events.clear();
    CHECK(scheduler.tick(100).ok && events == std::vector<int>{1});
    std::string error;
    CHECK(notifyFrontendAfterFrame(scheduler, states, error));
    CHECK(runFrameSchedulerSlice(scheduler, error) && events == std::vector<int>{1});
    states.push_back({&frontend, {{"render", waitObject, "Ljava.lang.Object;"}}});
    CHECK(notifyFrontendAfterFrame(scheduler, states, error) && error.empty());
    CHECK(runFrameSchedulerSlice(scheduler, error) && error.empty());
    CHECK((events == std::vector<int>{1, 4}));
    waitObject = nullptr;
  }
  {
    Script script("test.Priority", 1, 1, false);
    VmArena arena;
    std::vector<const TufaClass *> classes{&script.cls};
    std::vector<VmStaticFields> states;
    std::vector<NativeEntry> natives{{"test.Priority", "record", "(I)V", "test", record}};
    VmScheduler scheduler(arena, &natives, &classes, &states, [] { return 0.0; });
    auto *owner = thread(arena, object(arena, &script.cls));
    CHECK(scheduler.createThread(owner, "prio").ok);
    CHECK(scheduler.setPriority(owner, 5).ok);
    const auto native = invokeNative("java.lang.Thread", "setPriority", "(I)V",
                                     {VmValue{int32_t{3}}}, owner, &arena, nullptr);
    CHECK(std::holds_alternative<std::monostate>(native));
    CHECK(std::holds_alternative<std::monostate>(
        invokeNative("java.lang.Thread", "setPriority", "(I)V", {VmValue{int32_t{1}}},
                     object(arena, &script.cls), &arena, nullptr)));
  }
  return failures ? 1 : 0;
}
