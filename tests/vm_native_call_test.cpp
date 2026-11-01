#include "natives/registry.hpp"
#include "vm.hpp"

#include <cstdio>
#include <cstring>
#include <variant>

static int failures = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);                      \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

namespace {

std::vector<VmValue> seenArgs;
const VmObject *seenReceiver = nullptr;
int callCount = 0;

NativeResult record(const std::vector<VmValue> &args,
                    const VmObject *receiver) {
  seenArgs = args;
  seenReceiver = receiver;
  ++callCount;
  return std::monostate{};
}

NativeResult addInts(const std::vector<VmValue> &args, const VmObject *receiver,
                     VmArena *, const std::vector<const TufaClass *> *) {
  record(args, receiver);
  return VmValue{static_cast<int32_t>(std::get<int32_t>(args[0]) +
                                      std::get<int32_t>(args[1]))};
}
NativeResult mixed(const std::vector<VmValue> &args, const VmObject *receiver,
                   VmArena *, const std::vector<const TufaClass *> *) {
  return record(args, receiver);
}
NativeResult scale(const std::vector<VmValue> &args, const VmObject *receiver,
                   VmArena *, const std::vector<const TufaClass *> *) {
  record(args, receiver);
  return VmValue{std::get<float>(args[0]) * 2.0f};
}
NativeResult returnsFloatForInt(const std::vector<VmValue> &args,
                                const VmObject *receiver, VmArena *,
                                const std::vector<const TufaClass *> *) {
  record(args, receiver);
  return VmValue{1.5f};
}
NativeResult returnsIntForVoid(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *,
                               const std::vector<const TufaClass *> *) {
  record(args, receiver);
  return VmValue{int32_t{1}};
}
NativeResult returnsNothingForInt(const std::vector<VmValue> &args,
                                  const VmObject *receiver, VmArena *,
                                  const std::vector<const TufaClass *> *) {
  record(args, receiver);
  return std::monostate{};
}
NativeResult fails(const std::vector<VmValue> &args, const VmObject *receiver,
                   VmArena *, const std::vector<const TufaClass *> *) {
  record(args, receiver);
  return std::string{"handler refused"};
}
const VmObject *handedBack = nullptr;
NativeResult returnsObject(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *,
                           const std::vector<const TufaClass *> *) {
  record(args, receiver);
  return VmValue{handedBack};
}
NativeResult overloadInt(const std::vector<VmValue> &args,
                         const VmObject *receiver, VmArena *,
                         const std::vector<const TufaClass *> *) {
  record(args, receiver);
  return VmValue{int32_t{100}};
}
NativeResult overloadFloat(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *,
                           const std::vector<const TufaClass *> *) {
  record(args, receiver);
  return VmValue{int32_t{200}};
}

const VmObject *callbackReceiver = nullptr;
std::string callbackName;
NativeResult callBack(const std::vector<VmValue> &args,
                      const VmObject *receiver, VmArena *arena,
                      const std::vector<const TufaClass *> *) {
  record(args, receiver);
  if (!arena || !arena->callScript)
    return std::string{"no script call hook"};
  const auto answer =
      arena->callScript(callbackReceiver, callbackName, {args[0]});
  if (!answer.ok)
    return answer.error;
  if (answer.voidReturn)
    return std::monostate{};
  return answer.value;
}

constexpr const char *kBox = "test.Box";

struct Fixture {
  TufaClass host;
  TufaClass box;
  std::vector<NativeEntry> registry;
  VmArena arena;
  std::vector<const TufaClass *> classes;

  uint32_t text(TufaClass &cls, const std::string &value) {
    cls.constants.push_back({0, value, 0, 0});
    return static_cast<uint32_t>(cls.constants.size() - 1);
  }
  uint32_t add(TufaClass &cls, uint32_t kind, uint32_t a, uint32_t b) {
    cls.constants.push_back({kind, {}, a, b});
    return static_cast<uint32_t>(cls.constants.size() - 1);
  }

  uint32_t classRef = 0;
  Fixture() {
    for (auto *cls : {&host, &box}) {
      cls->hasClassHeader = true;
      cls->classHeader[3] = UINT32_MAX;
    }
    host.name = "test.Host";
    box.name = kBox;
    classRef = add(host, 4, text(host, host.name), 0);
    classes = {&host, &box};
  }

  // Declares a method and returns the method reference constant the call node
  // points at.
  uint32_t declare(const std::string &name, const std::string &descriptor,
                   uint32_t flags) {
    const uint32_t nameIndex = text(host, name);
    const uint32_t descriptorIndex = text(host, descriptor);
    host.methods[0].push_back({{flags, nameIndex, descriptorIndex, 0, 0}});
    return add(host, 5, classRef, add(host, 7, nameIndex, descriptorIndex));
  }
  uint32_t nameConstant(const std::string &name) { return text(host, name); }

  VmObject *object(const TufaClass *cls,
                   const std::string &arrayDescriptor = {}) {
    auto made = std::make_unique<VmObject>();
    made->runtimeClass = cls;
    made->arrayDescriptor = arrayDescriptor;
    arena.objects.push_back(std::move(made));
    return arena.objects.back().get();
  }

  VmResult run(const std::vector<TufaNode> &tree,
               std::vector<VmValue> locals = {}, bool withRegistry = true) {
    seenArgs.clear();
    seenReceiver = nullptr;
    callCount = 0;
    return vmExecute(host, tree, std::move(locals),
                     withRegistry ? &registry : nullptr, &classes, nullptr,
                     &arena);
  }
};

TufaNode literal(int32_t value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return {0x27, 0, true, bits};
}
TufaNode local(uint32_t index) { return {0x01, 0, true, index}; }
TufaNode count(uint32_t n) { return {0x27, 0, true, n}; }
TufaNode call() { return {0x24, 0, false, 0}; }
TufaNode ref(uint32_t index) { return {0x1a, 0, true, index}; }
TufaNode ret() { return {0x2b, 0, false, 0}; }
TufaNode retVoid() { return {0x2a, 0, false, 0}; }
std::vector<TufaNode> floatLiteral(float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return {{0x07, 0, true, 28}, {0x0a, 0, true, bits}};
}
std::vector<TufaNode> join(std::initializer_list<std::vector<TufaNode>> parts) {
  std::vector<TufaNode> all;
  for (const auto &part : parts)
    all.insert(all.end(), part.begin(), part.end());
  return all;
}

constexpr uint32_t kStatic = 0x48;
constexpr uint32_t kInstance = 0x40;

} // namespace

int main() {
  {
    Fixture f;
    const auto add = f.declare("add", "(II)I", kStatic);
    f.registry = {{"test.Host", "add", "(II)I", "test", addInts}};
    const auto result =
        f.run({literal(7), literal(35), count(2), call(), ref(add), ret()});
    CHECK(result.ok && !result.voidReturn &&
          std::get_if<int32_t>(&result.value) &&
          std::get<int32_t>(result.value) == 42);
    CHECK(callCount == 1 && seenArgs.size() == 2 &&
          std::get<int32_t>(seenArgs[0]) == 7 &&
          std::get<int32_t>(seenArgs[1]) == 35 && seenReceiver == nullptr);
    const auto swapped =
        f.run({literal(35), literal(-5), count(2), call(), ref(add), ret()});
    CHECK(swapped.ok && std::get<int32_t>(swapped.value) == 30 &&
          std::get<int32_t>(seenArgs[0]) == 35);
  }
  {
    Fixture f;
    const auto mix = f.declare("mix", "(IFLtest.Box;[Ltest.Box;)V", kStatic);
    f.registry = {
        {"test.Host", "mix", "(IFLtest.Box;[Ltest.Box;)V", "test", mixed}};
    const auto *box = f.object(&f.box);
    const auto *array = f.object(nullptr, "[Ltest.Box;");
    const auto tree =
        join({{literal(9)},
              floatLiteral(2.5f),
              {local(0), local(1), count(4), call(), ref(mix), retVoid()}});
    const auto result = f.run(tree, {box, array});
    CHECK(result.ok && result.voidReturn);
    CHECK(seenArgs.size() == 4 && callCount == 1);
    if (seenArgs.size() == 4) {
      CHECK(std::get<int32_t>(seenArgs[0]) == 9 &&
            std::get<float>(seenArgs[1]) == 2.5f &&
            std::get<const VmObject *>(seenArgs[2]) == box &&
            std::get<const VmObject *>(seenArgs[3]) == array);
    }
    const auto nulls = f.run(join({{literal(9)},
                                   floatLiteral(2.5f),
                                   {{0x07, 0, true, 28},
                                    {0x0c, 0, false, 0},
                                    local(1),
                                    count(4),
                                    call(),
                                    ref(mix),
                                    retVoid()}}),
                             {box, array});
    CHECK(nulls.ok && std::get<const VmObject *>(seenArgs[2]) == nullptr);
    const auto swapped = f.run(
        join({{literal(9)},
              floatLiteral(2.5f),
              {local(1), local(0), count(4), call(), ref(mix), retVoid()}}),
        {box, array});
    CHECK(!swapped.ok &&
          swapped.error.find("native argument type mismatch") == 0 &&
          callCount == 0);
    const auto stranger = f.object(&f.host);
    const auto wrongClass = f.run(
        join({{literal(9)},
              floatLiteral(2.5f),
              {local(0), local(1), count(4), call(), ref(mix), retVoid()}}),
        {stranger, array});
    CHECK(!wrongClass.ok &&
          wrongClass.error.find("native argument type mismatch") == 0 &&
          callCount == 0);
    VmObject loose{&f.box, {}};
    const auto unowned = f.run(
        join({{literal(9)},
              floatLiteral(2.5f),
              {local(0), local(1), count(4), call(), ref(mix), retVoid()}}),
        {&loose, array});
    CHECK(!unowned.ok && callCount == 0);
  }
  {
    Fixture f;
    const auto scaleRef = f.declare("scale", "(F)F", kInstance);
    f.registry = {{"test.Host", "scale", "(F)F", "test", scale}};
    const auto *self = f.object(&f.host);
    const auto result = f.run(
        join({floatLiteral(1.25f), {count(1), call(), ref(scaleRef), ret()}}),
        {self});
    CHECK(result.ok && std::get_if<float>(&result.value) &&
          std::get<float>(result.value) == 2.5f);
    CHECK(seenReceiver == self && seenArgs.size() == 1);
    const auto noReceiver = f.run(
        join({floatLiteral(1.25f), {count(1), call(), ref(scaleRef), ret()}}),
        {static_cast<const VmObject *>(nullptr)});
    CHECK(!noReceiver.ok && noReceiver.error == "missing native receiver" &&
          callCount == 0);
    const auto noLocals = f.run(
        join({floatLiteral(1.25f), {count(1), call(), ref(scaleRef), ret()}}));
    CHECK(!noLocals.ok && noLocals.error == "missing native receiver");
    const auto wrongReceiver = f.run(
        join({floatLiteral(1.25f), {count(1), call(), ref(scaleRef), ret()}}),
        {f.object(&f.box)});
    CHECK(!wrongReceiver.ok &&
          wrongReceiver.error == "invalid native receiver" && callCount == 0);
    const auto exactInteger =
        f.run({literal(3), count(1), call(), ref(scaleRef), ret()}, {self});
    CHECK(exactInteger.ok && std::get<float>(exactInteger.value) == 6.0f);
    const auto inexactInteger = f.run(
        {literal(16777217), count(1), call(), ref(scaleRef), ret()}, {self});
    CHECK(!inexactInteger.ok &&
          inexactInteger.error.find("native argument type mismatch") == 0);
  }
  {
    Fixture f;
    const auto getObject = f.declare("getObject", "()Ltest.Box;", kStatic);
    const auto getArray = f.declare("getArray", "()[Ltest.Box;", kStatic);
    const auto getInt = f.declare("getInt", "()I", kStatic);
    const auto getVoid = f.declare("getVoid", "()V", kStatic);
    f.registry = {
        {"test.Host", "getObject", "()Ltest.Box;", "test", returnsObject},
        {"test.Host", "getArray", "()[Ltest.Box;", "test", returnsObject},
        {"test.Host", "getInt", "()I", "test", returnsFloatForInt},
        {"test.Host", "getVoid", "()V", "test", returnsIntForVoid}};
    const auto *box = f.object(&f.box);
    const auto *array = f.object(nullptr, "[Ltest.Box;");
    handedBack = box;
    const auto object = f.run({count(0), call(), ref(getObject), ret()});
    CHECK(object.ok && std::get_if<const VmObject *>(&object.value) &&
          std::get<const VmObject *>(object.value) == box);
    handedBack = nullptr;
    const auto nothing = f.run({count(0), call(), ref(getObject), ret()});
    CHECK(nothing.ok && std::get<const VmObject *>(nothing.value) == nullptr);
    handedBack = array;
    const auto arrayResult = f.run({count(0), call(), ref(getArray), ret()});
    CHECK(arrayResult.ok &&
          std::get<const VmObject *>(arrayResult.value) == array);
    handedBack = array;
    const auto arrayAsObject = f.run({count(0), call(), ref(getObject), ret()});
    CHECK(!arrayAsObject.ok && arrayAsObject.error == "invalid native result");
    handedBack = box;
    const auto objectAsArray = f.run({count(0), call(), ref(getArray), ret()});
    CHECK(!objectAsArray.ok && objectAsArray.error == "invalid native result");
    VmObject loose{&f.box, {}};
    handedBack = &loose;
    const auto unowned = f.run({count(0), call(), ref(getObject), ret()});
    CHECK(!unowned.ok && unowned.error == "invalid native result");
    const auto floatForInt = f.run({count(0), call(), ref(getInt), ret()});
    CHECK(!floatForInt.ok && floatForInt.error == "invalid native result");
    const auto intForVoid = f.run({count(0), call(), ref(getVoid), retVoid()});
    CHECK(!intForVoid.ok && intForVoid.error == "invalid native void result");
  }
  {
    Fixture f;
    const auto getInt = f.declare("getInt", "()I", kStatic);
    const auto getFloat = f.declare("getFloat", "()F", kStatic);
    const auto getVoid = f.declare("getVoid", "()V", kStatic);
    f.registry = {{"test.Host", "getInt", "()I", "test", returnsNothingForInt},
                  {"test.Host", "getFloat", "()F", "test", returnsIntForVoid},
                  {"test.Host", "getVoid", "()V", "test", mixed}};
    CHECK(!f.run({count(0), call(), ref(getInt), ret()}).ok);
    const auto intForFloat = f.run({count(0), call(), ref(getFloat), ret()});
    CHECK(!intForFloat.ok && intForFloat.error == "invalid native result");
    const auto voidOk = f.run({count(0), call(), ref(getVoid), retVoid()});
    CHECK(voidOk.ok && voidOk.voidReturn);
  }
  {
    Fixture f;
    const auto add = f.declare("add", "(II)I", kStatic);
    const auto missing = f.declare("missing", "()I", kStatic);
    const auto wide = f.declare("wide", "(J)V", kStatic);
    const auto refuse = f.declare("refuse", "()V", kStatic);
    f.registry = {{"test.Host", "add", "(II)I", "test", addInts},
                  {"test.Host", "refuse", "()V", "test", fails},
                  {"test.Host", "wide", "(J)V", "test", mixed}};
    const auto notRegistered = f.run({count(0), call(), ref(missing), ret()});
    CHECK(!notRegistered.ok &&
          notRegistered.error == "native not registered: test.Host.missing()I");
    CHECK(!f.run({literal(1), literal(2), count(2), call(), ref(add), ret()},
                 {}, false)
               .ok);
    const auto noRegistry = f.run(
        {literal(1), literal(2), count(2), call(), ref(add), ret()}, {}, false);
    CHECK(noRegistry.error == "native not registered: test.Host.add(II)I");
    const auto tooFew = f.run({literal(1), count(2), call(), ref(add), ret()});
    CHECK(!tooFew.ok && tooFew.error == "missing call arguments" &&
          callCount == 0);
    const auto onlyCount = f.run({count(2), call(), ref(add), ret()});
    CHECK(!onlyCount.ok && onlyCount.error == "missing call arguments" &&
          callCount == 0);
    const auto floatForInt = f.run(join({{literal(1)},
                                         floatLiteral(-2.75f),
                                         {count(2), call(), ref(add), ret()}}));
    CHECK(floatForInt.ok && std::get<int32_t>(floatForInt.value) == -1 &&
          callCount == 1 && std::get<int32_t>(seenArgs[1]) == -2);
    const auto unsupported =
        f.run({literal(1), count(1), call(), ref(wide), retVoid()});
    CHECK(!unsupported.ok &&
          unsupported.error.find("unsupported native descriptor") == 0 &&
          callCount == 0);
    const auto refused = f.run({count(0), call(), ref(refuse), retVoid()});
    CHECK(!refused.ok && refused.error == "handler refused");
    std::vector<NativeEntry> duplicated = f.registry;
    duplicated.push_back(f.registry[0]);
    f.registry = duplicated;
    const auto twice =
        f.run({literal(1), literal(2), count(2), call(), ref(add), ret()});
    CHECK(!twice.ok && twice.error == "ambiguous native");
  }
  {
    Fixture f;
    const auto fake = f.declare("add", "(II)I", kStatic);
    f.registry = {{"test.Other", "add", "(II)I", "test", addInts},
                  {"test.Host", "add", "(II)F", "test", addInts}};
    const auto wrongOwner =
        f.run({literal(1), literal(2), count(2), call(), ref(fake), ret()});
    CHECK(!wrongOwner.ok &&
          wrongOwner.error == "native not registered: test.Host.add(II)I");
  }
  {
    Fixture f;
    const auto scripted = f.declare("scripted", "(II)I", 0x08);
    f.registry = {{"test.Host", "scripted", "(II)I", "test", addInts}};
    const auto result =
        f.run({literal(1), literal(2), count(2), call(), ref(scripted), ret()});
    CHECK(!result.ok && callCount == 0);
  }
  {
    Fixture f;
    const auto setThing = f.declare("setThing", "(IF)V", kStatic);
    (void)setThing;
    const auto over = f.declare("over", "(I)I", kStatic);
    const auto overFloat = f.declare("over", "(F)I", kStatic);
    (void)over;
    (void)overFloat;
    const auto open = f.declare("open", "(I)I", kInstance);
    (void)open;
    f.registry = {{"test.Host", "setThing", "(IF)V", "test", mixed},
                  {"test.Host", "over", "(I)I", "test", overloadInt},
                  {"test.Host", "over", "(F)I", "test", overloadFloat},
                  {"test.Host", "open", "(I)I", "test", overloadInt}};
    const auto setName = f.nameConstant("setThing");
    const auto overName = f.nameConstant("over");
    const auto openName = f.nameConstant("open");
    const auto absentName = f.nameConstant("absent");
    const auto byClass = [&](uint32_t name) {
      return std::vector<TufaNode>{call(),
                                   {0x11, 0, true, 2},
                                   {0x19, 0, true, f.classRef},
                                   {0x12, 0, true, name}};
    };
    const auto dynamicVoid = f.run(join({{literal(4)},
                                         floatLiteral(0.5f),
                                         {count(2)},
                                         byClass(setName),
                                         {retVoid()}}));
    CHECK(dynamicVoid.ok && callCount == 1 &&
          std::get<int32_t>(seenArgs[0]) == 4 &&
          std::get<float>(seenArgs[1]) == 0.5f);
    const auto pickedInt =
        f.run(join({{literal(4), count(1)}, byClass(overName), {ret()}}));
    CHECK(pickedInt.ok && std::get<int32_t>(pickedInt.value) == 100);
    const auto pickedFloat = f.run(
        join({floatLiteral(4.0f), {count(1)}, byClass(overName), {ret()}}));
    CHECK(pickedFloat.ok && std::get<int32_t>(pickedFloat.value) == 200);
    const auto absent =
        f.run(join({{literal(4), count(1)}, byClass(absentName), {ret()}}));
    CHECK(!absent.ok &&
          absent.error == "unsupported dynamic method: test.Host.absent(I)" &&
          callCount == 0);
    const auto wrongTypes = f.run(join(
        {{literal(4), literal(5), count(2)}, byClass(setName), {retVoid()}}));
    CHECK(wrongTypes.ok && callCount == 1 && std::get<int32_t>(seenArgs[0]) == 4 &&
          std::get<float>(seenArgs[1]) == 5.0f);
    const auto narrowed = f.run(join({floatLiteral(-3.75f), floatLiteral(0.5f),
                                      {count(2)}, byClass(setName), {retVoid()}}));
    CHECK(narrowed.ok && callCount == 1 &&
          std::get<int32_t>(seenArgs[0]) == -3 &&
          std::get<float>(seenArgs[1]) == 0.5f);
    const auto nullArgument =
        f.run(join({{{0x07, 0, true, 28}, {0x0c, 0, false, 0}, count(1)},
                    byClass(overName),
                    {ret()}}));
    // A null argument reaches descriptor-driven selection, which fits nulls
    // only against reference parameters; an int parameter fits nothing.
    CHECK(!nullArgument.ok &&
          nullArgument.error ==
              "unsupported dynamic method: test.Host.over(?)");
    const auto *self = f.object(&f.host);
    const auto byLocal = std::vector<TufaNode>{
        call(), {0x11, 0, true, 2}, local(0), {0x12, 0, true, openName}};
    const auto instance =
        f.run(join({{literal(4), count(1)}, byLocal, {ret()}}), {self});
    CHECK(instance.ok && std::get<int32_t>(instance.value) == 100 &&
          seenReceiver == self);
    const auto noSelf = f.run(join({{literal(4), count(1)}, byLocal, {ret()}}),
                              {static_cast<const VmObject *>(nullptr)});
    // Stock policy: null receiver skips the body; the native handler is not
    // called and the declared return default (0 for I) is pushed.
    CHECK(noSelf.ok && callCount == 0 &&
          std::get<int32_t>(noSelf.value) == 0);
    const auto instanceViaClass =
        f.run(join({{literal(4), count(1)}, byClass(openName), {ret()}}));
    CHECK(!instanceViaClass.ok &&
          instanceViaClass.error == "unsupported qualified instance method");
    self->fields.emplace_back("open", VmValue{int32_t{0}}, "I");
    const auto shadowed =
        f.run(join({{literal(4), count(1)}, byLocal, {ret()}}), {self});
    CHECK(!shadowed.ok && shadowed.error == "dynamic name shadows method");
    const auto unqualified =
        f.run({literal(4), count(1), call(), {0x12, 0, true, overName}, ret()});
    CHECK(!unqualified.ok && unqualified.error == "unsupported dynamic call");
  }
  {
    Fixture f;
    f.box.classHeader[3] = f.add(f.box, 4, f.text(f.box, f.host.name), 0);
    f.declare("accept", "(Ltest.Host;)V", kStatic);
    f.registry = {{"test.Host", "accept", "(Ltest.Host;)V", "test", mixed}};
    const auto name = f.nameConstant("accept");
    const auto *box = f.object(&f.box);
    const auto result = f.run({local(0),
                               count(1),
                               call(),
                               {0x11, 0, true, 2},
                               {0x19, 0, true, f.classRef},
                               {0x12, 0, true, name},
                               retVoid()},
                              {box});
    CHECK(result.ok && callCount == 1 &&
          std::get<const VmObject *>(seenArgs[0]) == box);
  }
  {
    Fixture f;
    f.host.trees.push_back({local(0), ret()});
    const auto method = f.declare("echoInt", "(I)I", 0x08);
    const auto name = f.nameConstant("echoInt");
    const auto result = f.run(join({floatLiteral(-3.75f), {count(1), call()},
        {{0x11, 0, true, 2}, {0x19, 0, true, f.classRef},
         {0x12, 0, true, name}, ret()}}));
    CHECK(result.ok && std::get<int32_t>(result.value) == -3);
    (void)method;
  }
  {
    // A native calls back into a script method on an arena object (the facility
    // GameType.createNativeInstance uses for getInfo).
    Fixture f;
    const auto callbackRef = f.declare("callback", "(I)I", kStatic);
    f.registry = {{"test.Host", "callback", "(I)I", "test", callBack}};
    const auto addMethod = [&](const std::string &name,
                               const std::string &descriptor, uint32_t flags,
                               std::vector<TufaNode> tree) {
      f.host.trees.push_back(std::move(tree));
      f.host.methods[0].push_back(
          {{flags, f.nameConstant(name), f.nameConstant(descriptor),
            static_cast<uint32_t>(f.host.trees.size() - 1), 2}});
    };
    addMethod("echo", "(I)I", 0x01, {local(1), ret()});
    addMethod("nativeOnly", "(I)I", 0x41, {});
    addMethod("staticOne", "(I)I", 0x09, {local(0), ret()});
    addMethod("broken", "(I)I", 0x01, {{0x3f, 0, false, 0}});
    const auto *self = f.object(&f.host);
    const auto run = [&](const std::string &name, const VmObject *receiver,
                         int32_t value) {
      callbackReceiver = receiver;
      callbackName = name;
      return f.run({literal(value), count(1), call(), ref(callbackRef), ret()});
    };
    CHECK(!f.arena.callScript);
    const auto echoed = run("echo", self, 41);
    CHECK(echoed.ok && std::get_if<int32_t>(&echoed.value) &&
          std::get<int32_t>(echoed.value) == 41);
    CHECK(!f.arena.callScript && f.arena.nativeDepth == 0);
    const auto missing = run("absent", self, 1);
    CHECK(!missing.ok && missing.error == "script call: no method absent(I)");
    const auto native = run("nativeOnly", self, 1);
    CHECK(!native.ok &&
          native.error ==
              "script call: nativeOnly(I) is not a script instance method");
    const auto stat = run("staticOne", self, 1);
    CHECK(!stat.ok &&
          stat.error ==
              "script call: staticOne(I) is not a script instance method");
    const auto noReceiver = run("echo", nullptr, 1);
    CHECK(!noReceiver.ok &&
          noReceiver.error == "script call needs an object receiver");
    VmObject loose{&f.host, {}};
    const auto unowned = run("echo", &loose, 1);
    CHECK(!unowned.ok &&
          unowned.error == "script call receiver is not an arena object");
    const auto broken = run("broken", self, 1);
    CHECK(!broken.ok &&
          broken.error.find("unsupported operation") != std::string::npos);
    CHECK(!f.arena.failureTrace.empty() &&
          f.arena.failureTrace.front().find("test.Host") == 0);
    f.arena.failureTrace.clear();
    const auto afterwards = run("echo", self, 7);
    CHECK(afterwards.ok && std::get<int32_t>(afterwards.value) == 7 &&
          f.arena.failureTrace.empty());
  }
  {
    // The installed Sound.init()V ends with `29 2b`: a value return from a ()V
    // script method is accepted.
    Fixture f;
    f.host.trees.push_back({{0x29, 0, false, 0}, ret()});
    f.host.trees.push_back({retVoid()});
    const uint32_t valueNameIndex = f.nameConstant("ends"),
                   voidNameIndex = f.nameConstant("plain"),
                   descriptorIndex = f.nameConstant("()V");
    f.host.methods[0].push_back(
        {{0x09, valueNameIndex, descriptorIndex, 0, 0}});
    f.host.methods[0].push_back({{0x09, voidNameIndex, descriptorIndex, 1, 0}});
    const auto valueRef =
        f.add(f.host, 5, f.classRef,
              f.add(f.host, 7, valueNameIndex, descriptorIndex));
    const auto voidRef =
        f.add(f.host, 5, f.classRef,
              f.add(f.host, 7, voidNameIndex, descriptorIndex));
    const auto viaValue = f.run({count(0), call(), ref(valueRef), retVoid()});
    CHECK(viaValue.ok && viaValue.voidReturn);
    const auto viaVoid = f.run({count(0), call(), ref(voidRef), retVoid()});
    CHECK(viaVoid.ok && viaVoid.voidReturn);
    const auto valueKept = f.run({count(0), call(), ref(valueRef), ret()});
    CHECK(valueKept.ok && !valueKept.voidReturn &&
          std::get_if<int32_t>(&valueKept.value) &&
          std::get<int32_t>(valueKept.value) == 0);
  }
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::puts("vm native call tests ok");
  return 0;
}
