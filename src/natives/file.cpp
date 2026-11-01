#include "natives/file.hpp"
#include "file_state.hpp"
#include "natives/lang_string.hpp"
#include "natives/game_type.hpp"
#include "run_log.hpp"
#include "vm.hpp"
#include "resource_world.hpp"
#include <cstring>

namespace {
VmField* field(const VmObject* receiver, const char* name,
               const char* descriptor) {
  VmField* result = nullptr;
  for (auto& candidate : receiver->fields)
    if (candidate.name == name) {
      if (result || candidate.descriptor != descriptor)
        return nullptr;
      result = &candidate;
    }
  return result;
}

VmField* checkedPtr(const VmObject* receiver, VmArena* arena,
                    const std::vector<const TufaClass*>* classes) {
  if (!receiver || !arena || !arena->fileState || !classes)
    return nullptr;
  bool owned = false, loaded = false;
  for (const auto& object : arena->objects)
    if (object.get() == receiver)
      owned = true;
  for (const auto* cls : *classes)
    if (cls == receiver->runtimeClass)
      loaded = true;
  if (!owned || !loaded || !receiver->runtimeClass ||
      receiver->runtimeClass->name != "java.io.File")
    return nullptr;
  VmField* ptr = field(receiver, "ptr", "I");
  if (!ptr || !std::holds_alternative<int32_t>(ptr->value))
    return nullptr;
  const auto handle = arena->fileState->handle(receiver);
  if (std::get<int32_t>(ptr->value) != handle.value_or(0))
    return nullptr;
  return ptr;
}
} // namespace

NativeResult fileExists(const std::vector<VmValue>& args, const VmObject*,
                        VmArena* arena,
                        const std::vector<const TufaClass*>*) {
  if (!arena || !arena->fileState || args.size() != 1)
    return std::string{"invalid File.exists call"};
  const auto* object = std::get_if<const VmObject*>(&args[0]);
  if (!object || !*object)
    return std::string{"unsupported File.exists name"};
  for (const auto& entry : arena->strings)
    if (entry.second == *object) {
      bool exists = false;
      std::string error;
      if (!arena->fileState->exists(entry.first, exists, error))
        return error;
      return VmValue{int32_t{exists ? 1 : 0}};
    }
  return std::string{"unsupported File.exists name"};
}

NativeResult fileOpenRead(const std::vector<VmValue>& args,
                          const VmObject* receiver, VmArena* arena,
                          const std::vector<const TufaClass*>* classes) {
  VmField* ptr = checkedPtr(receiver, arena, classes);
  if (!ptr || args.size() != 1 || !std::holds_alternative<int32_t>(args[0]) ||
      (std::get<int32_t>(args[0]) != 0 && std::get<int32_t>(args[0]) != 1))
    return std::string{"unsupported File.open call"};
  const bool writing = std::get<int32_t>(args[0]) == 1;
  const VmField* name = field(receiver, "name", "Ljava.lang.String;");
  if (!name || !std::holds_alternative<const VmObject*>(name->value))
    return std::string{"invalid File name"};
  const VmObject* string = std::get<const VmObject*>(name->value);
  const std::string* path = nullptr;
  for (const auto& entry : arena->strings)
    if (entry.second == string) {
      if (path)
        return std::string{"ambiguous File name"};
      path = &entry.first;
    }
  if (!path)
    return std::string{"unsupported File name"};
  std::string error;
  const auto result = writing
                          ? arena->fileState->openWrite(receiver, *path, error)
                          : arena->fileState->openRead(receiver, *path, error);
  ptr->value = arena->fileState->handle(receiver).value_or(0);
  if (result == FileOpenResult::error)
    return error;
  return VmValue{int32_t{result == FileOpenResult::opened ? 1 : 0}};
}

NativeResult fileReadInt(const std::vector<VmValue>& args,
                         const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>* classes) {
  if (!checkedPtr(receiver, arena, classes) || !args.empty())
    return std::string{"invalid File.readInt call"};
  int32_t value;
  std::string error;
  if (!arena->fileState->readInt(receiver, value, error))
    return error;
  return VmValue{value};
}

NativeResult fileReadFloat(const std::vector<VmValue>& args,
                           const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes) {
  if (!checkedPtr(receiver, arena, classes) || !args.empty())
    return std::string{"invalid File.readFloat call"};
  float value;
  std::string error;
  if (!arena->fileState->readFloat(receiver, value, error))
    return error;
  return VmValue{value};
}

NativeResult fileClose(const std::vector<VmValue>& args,
                       const VmObject* receiver, VmArena* arena,
                       const std::vector<const TufaClass*>* classes) {
  VmField* ptr = checkedPtr(receiver, arena, classes);
  if (!ptr || !args.empty())
    return std::string{"invalid File.close call"};
  const auto failure = arena->fileState->close(receiver);
  ptr->value = int32_t{0};
  // Stock FUN_00485870 reports a short write only through its own log line
  // ("Invalid datafile saved: %s") and returns void, so the script keeps
  // running. Mirror that control flow and put the diagnostic in the run log
  // instead of raising a host error that aborts the calling script.
  if (!failure.empty()) {
    runlog::event(runlog::EventKind::Error, "Invalid datafile saved: %s",
                  failure.c_str());
  }
  return std::monostate{};
}

namespace {
bool findFields(const VmObject* receiver, VmArena* arena, const std::vector<const TufaClass*>* classes, VmField*& handle, VmField*& flags) {
  if (!receiver || !arena || !arena->fileState || !classes || !receiver->runtimeClass || receiver->runtimeClass->name != "java.io.FindFile") return false;
  bool owned = false, loaded = false;
  for (const auto& object : arena->objects)
    if (object.get() == receiver) owned = true;
  for (const auto* cls : *classes)
    if (cls == receiver->runtimeClass) loaded = true;
  handle = field(receiver, "handle", "I");
  flags = field(receiver, "flags", "I");
  return owned && loaded && handle && flags && std::holds_alternative<int32_t>(handle->value) && std::holds_alternative<int32_t>(flags->value);
}
NativeResult findResult(const std::optional<std::string>& name, VmArena* arena, const std::vector<const TufaClass*>* classes) {
  return name ? internStringResult(*name, arena, classes) : NativeResult{VmValue{static_cast<const VmObject*>(nullptr)}};
}
} // namespace
NativeResult findFileFirst(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena, const std::vector<const TufaClass*>* classes) {
  VmField *handle = nullptr, *flags = nullptr;
  if (!findFields(receiver, arena, classes, handle, flags) || args.size() != 2 || !std::holds_alternative<const VmObject*>(args[0]) || !std::holds_alternative<int32_t>(args[1])) return std::string{"invalid FindFile.first call"};
  const auto* object = std::get<const VmObject*>(args[0]);
  for (const auto& entry : arena->strings)
    if (entry.second == object) {
      int32_t h;
      std::optional<std::string> result;
      std::string error;
      const int32_t f = std::get<int32_t>(args[1]);
      if (!arena->fileState->findFirst(entry.first, f, h, result, error)) return error;
      handle->value = h;
      flags->value = f;
      return findResult(result, arena, classes);
    }
  return std::string{"unsupported FindFile pattern string"};
}
NativeResult findFileNext(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena, const std::vector<const TufaClass*>* classes) {
  VmField *handle = nullptr, *flags = nullptr;
  if (!findFields(receiver, arena, classes, handle, flags) || !args.empty()) return std::string{"invalid FindFile.next call"};
  return findResult(arena->fileState->findNext(std::get<int32_t>(handle->value), std::get<int32_t>(flags->value)), arena, classes);
}
NativeResult findFileClose(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena, const std::vector<const TufaClass*>* classes) {
  VmField *handle = nullptr, *flags = nullptr;
  if (!findFields(receiver, arena, classes, handle, flags) || !args.empty()) return std::string{"invalid FindFile.close call"};
  arena->fileState->findClose(std::get<int32_t>(handle->value));
  handle->value = int32_t{-1};
  return std::monostate{};
}

namespace {
const std::string* stringOf(const VmValue& value, VmArena* arena) {
  const auto* object = std::get_if<const VmObject*>(&value);
  if (!object || !*object)
    return nullptr;
  for (const auto& entry : arena->strings)
    if (entry.second == *object)
      return &entry.first;
  return nullptr;
}
NativeResult writeWord(uint32_t word, const VmObject* receiver, VmArena* arena,
                       const std::vector<const TufaClass*>* classes) {
  if (!checkedPtr(receiver, arena, classes))
    return std::string{"invalid File.write call"};
  if (!arena->fileState->isWriter(receiver))
    return VmValue{int32_t{0}}; // stock: no handle, nothing written
  std::string error;
  if (!arena->fileState->writeBytes(receiver, &word, 4, error))
    return error;
  return VmValue{int32_t{4}};
}
} // namespace

NativeResult fileWriteInt(const std::vector<VmValue>& args,
                          const VmObject* receiver, VmArena* arena,
                          const std::vector<const TufaClass*>* classes) {
  if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
    return std::string{"invalid File.write(I) call"};
  uint32_t word;
  const int32_t value = std::get<int32_t>(args[0]);
  std::memcpy(&word, &value, 4);
  return writeWord(word, receiver, arena, classes);
}

NativeResult fileWriteFloat(const std::vector<VmValue>& args,
                            const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes) {
  if (args.size() != 1 || !std::holds_alternative<float>(args[0]))
    return std::string{"invalid File.write(F) call"};
  uint32_t word;
  const float value = std::get<float>(args[0]);
  std::memcpy(&word, &value, 4);
  return writeWord(word, receiver, arena, classes);
}

NativeResult fileWriteString(const std::vector<VmValue>& args,
                             const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>* classes) {
  if (!checkedPtr(receiver, arena, classes) || args.size() != 1)
    return std::string{"invalid File.write(String) call"};
  const auto* object = std::get_if<const VmObject*>(&args[0]);
  if (!object)
    return std::string{"invalid File.write(String) argument"};
  const std::string* text = nullptr;
  if (*object) {
    text = stringOf(args[0], arena);
    if (!text)
      return std::string{"unsupported File.write string object"};
  }
  if (!arena->fileState->isWriter(receiver))
    return VmValue{int32_t{0}};
  // FUN_00485fd0: length counts the terminator; a null string writes length 0.
  const uint32_t length = text ? static_cast<uint32_t>(text->size() + 1) : 0;
  std::string error;
  if (!arena->fileState->writeBytes(receiver, &length, 4, error))
    return error;
  if (length == 0)
    return VmValue{int32_t{4}};
  std::string bytes = *text;
  bytes.push_back('\0');
  if (!arena->fileState->writeBytes(receiver, bytes.data(), bytes.size(), error))
    return error;
  return VmValue{static_cast<int32_t>(4 + bytes.size())};
}

NativeResult fileReadString(const std::vector<VmValue>& args,
                            const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes) {
  if (!checkedPtr(receiver, arena, classes) || !args.empty())
    return std::string{"invalid File.readString call"};
  std::string value, error;
  if (!arena->fileState->readString(receiver, value, error))
    return error;
  if (value.empty())
    return VmValue{static_cast<const VmObject*>(nullptr)}; // length 0 -> null
  return internStringResult(value, arena, classes);
}

NativeResult fileWriteResource(const std::vector<VmValue>& args,
                               const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes) {
  if (!checkedPtr(receiver, arena, classes) || args.size() != 1)
    return std::string{"invalid File.write(ResourceRef) call"};
  const auto* object = std::get_if<const VmObject*>(&args[0]);
  if (!object)
    return std::string{"invalid File.write(ResourceRef) argument"};
  uint32_t id = 0;
  if (*object) {
    const VmField* ptr = field(*object, "ptr", "I");
    if (!ptr || !std::holds_alternative<int32_t>(ptr->value))
      return std::string{"File.write(ResourceRef): argument has no ptr"};
    if (std::get<int32_t>(ptr->value) != 0) {
      const auto* state = nativeResourceState(*object, arena);
      if (!state)
        return std::string{"File.write(ResourceRef): invalid native link"};
      id = state->words[2];
      const int32_t handle =
          static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
      if (arena->gameState) {
        const auto binding = arena->gameState->bindings.find(handle);
        if (binding != arena->gameState->bindings.end() && binding->second)
          id = binding->second->link.cachedValue;
      }
    }
  }
  if (!arena->fileState->isWriter(receiver))
    return VmValue{int32_t{0}};
  if (id != 0) {
    if ((id >> 16) == 0xffff)
      return std::string{"File.write(ResourceRef): unsaved dynamic resource "
                         "(FUN_00484fa0 embedded kinds) is an unported gap"};
    if (!arena->gameState || !arena->gameState->resources)
      return std::string{"File.write(ResourceRef): no resource world"};
    PackView view;
    std::string error;
    if (arena->gameState->resources->packs().view(
            static_cast<uint16_t>(id >> 16), view, error) !=
        PackViewStatus::Ok)
      return "File.write(ResourceRef): pack view failed: " + error;
    id = (arena->fileState->packOrdinal(receiver, view.name) << 16) |
         (id & 0xffff);
  }
  std::string error;
  if (!arena->fileState->writeBytes(receiver, &id, 4, error))
    return error;
  return VmValue{int32_t{4}};
}

NativeResult fileReadResId(const std::vector<VmValue>& args,
                           const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>* classes) {
  if (!checkedPtr(receiver, arena, classes) || !args.empty())
    return std::string{"invalid File.readResID call"};
  int32_t raw;
  std::string error;
  if (!arena->fileState->readInt(receiver, raw, error))
    return error;
  const uint32_t id = static_cast<uint32_t>(raw);
  if (id == 0)
    return VmValue{int32_t{0}};
  const uint32_t high = id >> 16;
  if (high == 0 || high == 0xffff)
    return std::string{"File.readResID: unsaved embedded resource ids are an "
                       "unported gap (FUN_00485ea0)"};
  const auto* packs = arena->fileState->readPacks(receiver);
  if (!packs || !arena->gameState || !arena->gameState->resources)
    return std::string{"File.readResID: no pack table"};
  for (const auto& pack : *packs)
    if (pack.first == high) {
      uint16_t slot;
      if (!arena->gameState->resources->packs().registerPackName(pack.second, slot, error))
        return "File.readResID: " + error;
      const uint32_t mapped = (static_cast<uint32_t>(slot) << 16) | (id & 0xffff);
      int32_t signedId;
      std::memcpy(&signedId, &mapped, 4);
      return VmValue{signedId};
    }
  return VmValue{int32_t{0}};
}

NativeResult fileDelete(const std::vector<VmValue>& args, const VmObject*,
                        VmArena* arena, const std::vector<const TufaClass*>*) {
  if (!arena || !arena->fileState || args.size() != 1)
    return std::string{"invalid File.delete call"};
  const auto* name = stringOf(args[0], arena);
  if (!name)
    return std::string{"unsupported File.delete name"};
  int32_t result;
  std::string error;
  if (!arena->fileState->deletePath(*name, result, error))
    return error;
  return VmValue{result};
}

namespace {
NativeResult twoPath(const std::vector<VmValue>& args, VmArena* arena,
                     bool move) {
  if (!arena || !arena->fileState || args.size() != 2)
    return std::string{"invalid File.copy/move call"};
  const auto* from = stringOf(args[0], arena);
  const auto* to = stringOf(args[1], arena);
  if (!from || !to)
    return std::string{"unsupported File.copy/move name"};
  int32_t result;
  std::string error;
  if (!(move ? arena->fileState->movePath(*from, *to, result, error)
             : arena->fileState->copyPath(*from, *to, result, error)))
    return error;
  return VmValue{result};
}
} // namespace

NativeResult fileCopy(const std::vector<VmValue>& args, const VmObject*,
                      VmArena* arena, const std::vector<const TufaClass*>*) {
  return twoPath(args, arena, false);
}

NativeResult fileMove(const std::vector<VmValue>& args, const VmObject*,
                      VmArena* arena, const std::vector<const TufaClass*>*) {
  return twoPath(args, arena, true);
}
