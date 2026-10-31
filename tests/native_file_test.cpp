#include "file_state.hpp"
#include "natives/registry.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unistd.h>

static int failures;
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::printf("FAIL %d: %s\n", __LINE__, #c);                              \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

int main() {
  char directory[] = "/tmp/sl-native-file-XXXXXX";
  if (!mkdtemp(directory))
    return 1;
  const std::filesystem::path root(directory);
  std::filesystem::create_directories(root / "save/game");
  std::filesystem::copy_file("ref/raw/save/game/options",
                             root / "save/game/options");
  FileState files(root, "ref", root);
  VmArena arena;
  arena.fileState = &files;
  TufaClass fileClass;
  fileClass.name = "java.io.File";
  std::vector<const TufaClass *> classes{&fileClass};
  auto name = std::make_unique<VmObject>();
  const VmObject *namePtr = name.get();
  arena.objects.push_back(std::move(name));
  arena.strings.push_back({"save/game/options", namePtr});
  auto object = std::make_unique<VmObject>();
  object->runtimeClass = &fileClass;
  object->fields.push_back({"ptr", int32_t{0}, "I"});
  object->fields.push_back({"name", namePtr, "Ljava.lang.String;"});
  const VmObject *receiver = object.get();
  arena.objects.push_back(std::move(object));
  const auto call = [&](const char *method, const char *descriptor,
                        const std::vector<VmValue> &args) {
    return invokeNative("java.io.File", method, descriptor, args, receiver,
                        &arena, &classes);
  };
  auto exists = call("exists", "(Ljava.lang.String;)I", {namePtr});
  CHECK(std::holds_alternative<VmValue>(exists));
  if (const auto *value = std::get_if<VmValue>(&exists))
    CHECK(std::get<int32_t>(*value) == 1);
  arena.strings[0].first = "save\\game\\missing";
  exists = call("exists", "(Ljava.lang.String;)I", {namePtr});
  if (const auto *value = std::get_if<VmValue>(&exists))
    CHECK(std::get<int32_t>(*value) == 0);
  else
    CHECK(false);
  arena.strings[0].first = "save/../save/game/options";
  CHECK(std::holds_alternative<std::string>(
      call("exists", "(Ljava.lang.String;)I", {namePtr})));
  arena.strings[0].first = "save/game/options";
  auto opened = call("open", "(I)I", {int32_t{0}});
  CHECK(std::holds_alternative<VmValue>(opened));
  if (const auto *value = std::get_if<VmValue>(&opened))
    CHECK(std::get<int32_t>(*value) == 1);
  CHECK(std::get<int32_t>(receiver->fields[0].value) > 0);
  auto first = call("readInt", "()I", {});
  CHECK(std::holds_alternative<VmValue>(first));
  if (const auto *value = std::get_if<VmValue>(&first)) {
    const auto integer = std::get<int32_t>(*value);
    uint32_t bits;
    std::memcpy(&bits, &integer, 4);
    CHECK(bits == 0xfedcba98u);
  }
  auto second = call("readInt", "()I", {});
  if (const auto *value = std::get_if<VmValue>(&second))
    CHECK(std::get<int32_t>(*value) == 12);
  else
    CHECK(false);
  for (int i = 0; i < 4; ++i)
    CHECK(std::holds_alternative<VmValue>(call("readInt", "()I", {})));
  auto floating = call("readFloat", "()F", {});
  if (const auto *value = std::get_if<VmValue>(&floating)) {
    const auto number = std::get<float>(*value);
    uint32_t bits;
    std::memcpy(&bits, &number, 4);
    CHECK(bits == 0x3f806166u);
  } else
    CHECK(false);
  CHECK(std::holds_alternative<std::monostate>(call("close", "()V", {})));
  // Stock 00485440 mode 1 opens for writing; close (00485870) flushes the
  // packed values with the type-2 trailer, and mode 0 reads them back.
  arena.strings[0].first = "save/game/roundtrip";
  CHECK(std::holds_alternative<VmValue>(call("open", "(I)I", {int32_t{1}})));
  CHECK(std::holds_alternative<VmValue>(call("write", "(I)I", {int32_t{-123456}})));
  CHECK(std::holds_alternative<VmValue>(call("write", "(F)I", {1.5f})));
  CHECK(std::holds_alternative<std::monostate>(call("close", "()V", {})));
  CHECK(std::filesystem::exists(root / "save/game/roundtrip"));
  CHECK(std::holds_alternative<VmValue>(call("open", "(I)I", {int32_t{0}})));
  {
    const auto integer = call("readInt", "()I", {});
    CHECK(std::holds_alternative<VmValue>(integer) &&
          std::get<int32_t>(std::get<VmValue>(integer)) == -123456);
    const auto number = call("readFloat", "()F", {});
    CHECK(std::holds_alternative<VmValue>(number) &&
          std::get<float>(std::get<VmValue>(number)) == 1.5f);
  }
  CHECK(std::holds_alternative<std::monostate>(call("close", "()V", {})));
  CHECK(std::holds_alternative<VmValue>(
      call("delete", "(Ljava.lang.String;)I", {namePtr})));
  CHECK(!std::filesystem::exists(root / "save/game/roundtrip"));
  arena.strings[0].first = "save/game/options";
  CHECK(std::holds_alternative<VmValue>(call("open", "(I)I", {int32_t{0}})));
  CHECK(std::holds_alternative<std::monostate>(call("close", "()V", {})));
  CHECK(std::get<int32_t>(receiver->fields[0].value) == 0);
  CHECK(std::holds_alternative<std::string>(call("readInt", "()I", {})));
  arena.strings[0].first = "save/game/missing";
  opened = call("open", "(I)I", {int32_t{0}});
  if (const auto *value = std::get_if<VmValue>(&opened))
    CHECK(std::get<int32_t>(*value) == 0);
  else
    CHECK(false);
  receiver->fields[0].value = int32_t{77};
  CHECK(std::holds_alternative<std::string>(call("close", "()V", {})));
  TufaClass findClass, stringClass;
  findClass.name = "java.io.FindFile"; stringClass.name = "java.lang.String";
  classes.push_back(&findClass); classes.push_back(&stringClass);
  auto finder = std::make_unique<VmObject>(); finder->runtimeClass = &findClass;
  finder->fields.push_back({"handle", int32_t{-1}, "I"});
  finder->fields.push_back({"flags", int32_t{0}, "I"});
  const VmObject *finderPtr = finder.get(); arena.objects.push_back(std::move(finder));
  arena.strings[0].first = "save/game/*";
  auto find = invokeNative("java.io.FindFile", "first", "(Ljava.lang.String;I)Ljava.lang.String;", {namePtr, int32_t{1}}, finderPtr, &arena, &classes);
  CHECK(std::holds_alternative<VmValue>(find));
  if (const auto *value = std::get_if<VmValue>(&find)) {
    const auto *identity = std::get<const VmObject *>(*value);
    bool matched = false; for (const auto &entry : arena.strings) if (entry.second == identity && entry.first == "options") matched = true;
    CHECK(matched);
  }
  CHECK(std::get<int32_t>(finderPtr->fields[0].value) == 0);
  auto next = invokeNative("java.io.FindFile", "next", "()Ljava.lang.String;", {}, finderPtr, &arena, &classes);
  CHECK(std::holds_alternative<VmValue>(next));
  if (const auto *value = std::get_if<VmValue>(&next)) CHECK(std::get<const VmObject *>(*value) == nullptr);
  CHECK(std::holds_alternative<std::monostate>(invokeNative("java.io.FindFile", "close", "()V", {}, finderPtr, &arena, &classes)));
  CHECK(std::get<int32_t>(finderPtr->fields[0].value) == -1);
  std::filesystem::remove_all(root);
  return failures ? 1 : 0;
}
