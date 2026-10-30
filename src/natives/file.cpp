#include "natives/file.hpp"
#include "file_state.hpp"
#include "natives/lang_string.hpp"

namespace {
VmField *field(const VmObject *receiver, const char *name,
               const char *descriptor) {
  VmField *result = nullptr;
  for (auto &candidate : receiver->fields)
    if (candidate.name == name) {
      if (result || candidate.descriptor != descriptor)
        return nullptr;
      result = &candidate;
    }
  return result;
}

VmField *checkedPtr(const VmObject *receiver, VmArena *arena,
                    const std::vector<const TufaClass *> *classes) {
  if (!receiver || !arena || !arena->fileState || !classes)
    return nullptr;
  bool owned = false, loaded = false;
  for (const auto &object : arena->objects)
    if (object.get() == receiver)
      owned = true;
  for (const auto *cls : *classes)
    if (cls == receiver->runtimeClass)
      loaded = true;
  if (!owned || !loaded || !receiver->runtimeClass ||
      receiver->runtimeClass->name != "java.io.File")
    return nullptr;
  VmField *ptr = field(receiver, "ptr", "I");
  if (!ptr || !std::holds_alternative<int32_t>(ptr->value))
    return nullptr;
  const auto handle = arena->fileState->handle(receiver);
  if (std::get<int32_t>(ptr->value) != handle.value_or(0))
    return nullptr;
  return ptr;
}
} // namespace

NativeResult fileExists(const std::vector<VmValue> &args, const VmObject *,
                        VmArena *arena,
                        const std::vector<const TufaClass *> *) {
  if (!arena || !arena->fileState || args.size() != 1)
    return std::string{"invalid File.exists call"};
  const auto *object = std::get_if<const VmObject *>(&args[0]);
  if (!object || !*object)
    return std::string{"unsupported File.exists name"};
  for (const auto &entry : arena->strings)
    if (entry.second == *object) {
      bool exists = false;
      std::string error;
      if (!arena->fileState->exists(entry.first, exists, error))
        return error;
      return VmValue{int32_t{exists ? 1 : 0}};
    }
  return std::string{"unsupported File.exists name"};
}

NativeResult fileOpenRead(const std::vector<VmValue> &args,
                          const VmObject *receiver, VmArena *arena,
                          const std::vector<const TufaClass *> *classes) {
  VmField *ptr = checkedPtr(receiver, arena, classes);
  if (!ptr || args.size() != 1 || !std::holds_alternative<int32_t>(args[0]) ||
      std::get<int32_t>(args[0]) != 0)
    return std::string{"unsupported File.open call"};
  const VmField *name = field(receiver, "name", "Ljava.lang.String;");
  if (!name || !std::holds_alternative<const VmObject *>(name->value))
    return std::string{"invalid File name"};
  const VmObject *string = std::get<const VmObject *>(name->value);
  const std::string *path = nullptr;
  for (const auto &entry : arena->strings)
    if (entry.second == string) {
      if (path)
        return std::string{"ambiguous File name"};
      path = &entry.first;
    }
  if (!path)
    return std::string{"unsupported File name"};
  std::string error;
  const auto result = arena->fileState->openRead(receiver, *path, error);
  ptr->value = arena->fileState->handle(receiver).value_or(0);
  if (result == FileOpenResult::error)
    return error;
  return VmValue{int32_t{result == FileOpenResult::opened ? 1 : 0}};
}

NativeResult fileReadInt(const std::vector<VmValue> &args,
                         const VmObject *receiver, VmArena *arena,
                         const std::vector<const TufaClass *> *classes) {
  if (!checkedPtr(receiver, arena, classes) || !args.empty())
    return std::string{"invalid File.readInt call"};
  int32_t value;
  std::string error;
  if (!arena->fileState->readInt(receiver, value, error))
    return error;
  return VmValue{value};
}

NativeResult fileReadFloat(const std::vector<VmValue> &args,
                           const VmObject *receiver, VmArena *arena,
                           const std::vector<const TufaClass *> *classes) {
  if (!checkedPtr(receiver, arena, classes) || !args.empty())
    return std::string{"invalid File.readFloat call"};
  float value;
  std::string error;
  if (!arena->fileState->readFloat(receiver, value, error))
    return error;
  return VmValue{value};
}

NativeResult fileClose(const std::vector<VmValue> &args,
                       const VmObject *receiver, VmArena *arena,
                       const std::vector<const TufaClass *> *classes) {
  VmField *ptr = checkedPtr(receiver, arena, classes);
  if (!ptr || !args.empty())
    return std::string{"invalid File.close call"};
  arena->fileState->close(receiver);
  ptr->value = int32_t{0};
  return std::monostate{};
}

namespace {
bool findFields(const VmObject *receiver, VmArena *arena, const std::vector<const TufaClass *> *classes, VmField *&handle, VmField *&flags) {
  if (!receiver || !arena || !arena->fileState || !classes || !receiver->runtimeClass || receiver->runtimeClass->name != "java.io.FindFile") return false;
  bool owned = false, loaded = false;
  for (const auto &object : arena->objects) if (object.get() == receiver) owned = true;
  for (const auto *cls : *classes) if (cls == receiver->runtimeClass) loaded = true;
  handle = field(receiver, "handle", "I"); flags = field(receiver, "flags", "I");
  return owned && loaded && handle && flags && std::holds_alternative<int32_t>(handle->value) && std::holds_alternative<int32_t>(flags->value);
}
NativeResult findResult(const std::optional<std::string> &name, VmArena *arena, const std::vector<const TufaClass *> *classes) {
  return name ? internStringResult(*name, arena, classes) : NativeResult{VmValue{static_cast<const VmObject *>(nullptr)}};
}
}
NativeResult findFileFirst(const std::vector<VmValue> &args, const VmObject *receiver, VmArena *arena, const std::vector<const TufaClass *> *classes) {
  VmField *handle = nullptr, *flags = nullptr;
  if (!findFields(receiver, arena, classes, handle, flags) || args.size() != 2 || !std::holds_alternative<const VmObject *>(args[0]) || !std::holds_alternative<int32_t>(args[1])) return std::string{"invalid FindFile.first call"};
  const auto *object = std::get<const VmObject *>(args[0]);
  for (const auto &entry : arena->strings) if (entry.second == object) {
    int32_t h; std::optional<std::string> result; std::string error; const int32_t f = std::get<int32_t>(args[1]);
    if (!arena->fileState->findFirst(entry.first, f, h, result, error)) return error;
    handle->value = h; flags->value = f; return findResult(result, arena, classes);
  }
  return std::string{"unsupported FindFile pattern string"};
}
NativeResult findFileNext(const std::vector<VmValue> &args, const VmObject *receiver, VmArena *arena, const std::vector<const TufaClass *> *classes) {
  VmField *handle = nullptr, *flags = nullptr;
  if (!findFields(receiver, arena, classes, handle, flags) || !args.empty()) return std::string{"invalid FindFile.next call"};
  return findResult(arena->fileState->findNext(std::get<int32_t>(handle->value), std::get<int32_t>(flags->value)), arena, classes);
}
NativeResult findFileClose(const std::vector<VmValue> &args, const VmObject *receiver, VmArena *arena, const std::vector<const TufaClass *> *classes) {
  VmField *handle = nullptr, *flags = nullptr;
  if (!findFields(receiver, arena, classes, handle, flags) || !args.empty()) return std::string{"invalid FindFile.close call"};
  arena->fileState->findClose(std::get<int32_t>(handle->value)); handle->value = int32_t{-1}; return std::monostate{};
}
