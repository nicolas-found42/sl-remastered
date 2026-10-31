#include "vm.hpp"
#include "natives/lang_string.hpp"
#include "natives/registry.hpp"
#include "run_log.hpp"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

static_assert(sizeof(float) == sizeof(uint32_t) &&
              std::numeric_limits<float>::is_iec559);

static bool floatToInt(const VmValue &source, VmValue &result) {
  const auto *value = std::get_if<float>(&source);
  if (!value || !std::isfinite(*value))
    return false;
  const double truncated = std::trunc(static_cast<double>(*value));
  if (truncated < static_cast<double>(INT32_MIN) ||
      truncated > static_cast<double>(INT32_MAX))
    return false;
  result = static_cast<int32_t>(truncated);
  return true;
}

// Assumes stock's x87 rounding mode is nearest; other host modes and subnormals
// are rejected.
static bool numericFloat(const VmValue &value, float &result) {
  if (std::fegetround() != FE_TONEAREST)
    return false;
  if (const auto *f = std::get_if<float>(&value))
    result = *f;
  else if (const auto *i = std::get_if<int32_t>(&value)) {
    result = static_cast<float>(*i);
  } else
    return false;
  return std::isfinite(result) && (result == 0.0f || std::isnormal(result));
}

static bool multiply(const VmValue &left, const VmValue &right, VmValue &out) {
  if (std::holds_alternative<float>(left) ||
      std::holds_alternative<float>(right)) {
    float lhs, rhs;
    if (!numericFloat(left, lhs) || !numericFloat(right, rhs))
      return false;
    const double product = static_cast<double>(lhs) * static_cast<double>(rhs);
    const float value = static_cast<float>(product);
    if (!std::isfinite(value) || (value != 0.0f && !std::isnormal(value)) ||
        (value == 0.0f && product != 0.0))
      return false;
    out = value;
    return true;
  }
  const auto *lhs = std::get_if<int32_t>(&left);
  const auto *rhs = std::get_if<int32_t>(&right);
  if (!lhs || !rhs)
    return false;
  const uint32_t bits =
      static_cast<uint32_t>(*lhs) * static_cast<uint32_t>(*rhs);
  int32_t value;
  std::memcpy(&value, &bits, sizeof(value));
  out = value;
  return true;
}

static bool referenceType(const std::string &type) {
  size_t start = 0;
  while (start < type.size() && type[start] == '[')
    ++start;
  if (start && (type.substr(start) == "I" || type.substr(start) == "F"))
    return true;
  return type.size() >= start + 3 && type[start] == 'L' && type.back() == ';' &&
         type.find(';', start) == type.size() - 1;
}

static bool classAssignableTo(const TufaClass *cls, const std::string &name,
                              const std::vector<const TufaClass *> &classes,
                              unsigned depth = 0);

static bool supportedReference(const VmObject *object,
                               const std::string &descriptor,
                               const VmArena *arena,
                               const std::vector<const TufaClass *> *classes) {
  if (!object)
    return referenceType(descriptor);
  if (!arena || !classes || !referenceType(descriptor))
    return false;
  bool owned = false;
  for (const auto &entry : arena->objects)
    if (entry.get() == object)
      owned = true;
  if (!owned)
    return false;
  if (descriptor == "Ljava.lang.Object;" && !object->runtimeClass &&
      referenceType(object->arrayDescriptor))
    return true;
  if (descriptor.front() == '[')
    return object->arrayDescriptor == descriptor && !object->runtimeClass;
  if (descriptor.front() != 'L' || !object->runtimeClass)
    return false;
  bool loaded = false;
  for (const auto *cls : *classes)
    if (cls == object->runtimeClass)
      loaded = true;
  return loaded && classAssignableTo(
                       object->runtimeClass,
                       descriptor.substr(1, descriptor.size() - 2), *classes);
}

static bool parentClass(const TufaClass &cls,
                        const std::vector<const TufaClass *> &classes,
                        const TufaClass *&parent) {
  parent = nullptr;
  if (!cls.hasClassHeader)
    return false;
  const uint32_t index = cls.classHeader[3];
  if (index == UINT32_MAX)
    return true;
  if (index >= cls.constants.size())
    return false;
  const auto &ref = cls.constants[index];
  if (ref.kind != 4 || ref.a >= cls.constants.size() ||
      cls.constants[ref.a].kind != 0)
    return false;
  for (const auto *candidate : classes)
    if (candidate->name == cls.constants[ref.a].text)
      parent = candidate;
  return parent != nullptr;
}

static bool constructorAncestor(const TufaClass *derived,
                                const TufaClass *ancestor,
                                const std::vector<const TufaClass *> &classes) {
  std::vector<const TufaClass *> visited;
  while (derived && visited.size() < 128) {
    bool loaded = false;
    for (const auto *candidate : classes)
      if (candidate == derived)
        loaded = true;
    if (!loaded)
      return false;
    for (const auto *prior : visited)
      if (prior == derived)
        return false;
    visited.push_back(derived);
    if (derived == ancestor)
      return true;
    const TufaClass *parent;
    if (!parentClass(*derived, classes, parent))
      return false;
    derived = parent;
  }
  return false;
}

// True when an instance of `cls` can be passed where class `name` is expected:
// `name` is the class, one of its ancestors, or an interface that the class or
// an ancestor lists. Classes must be loaded.
static bool classAssignableTo(const TufaClass *cls, const std::string &name,
                              const std::vector<const TufaClass *> &classes,
                              unsigned depth) {
  if (!cls || depth > 32)
    return false;
  if (cls->name == name)
    return true;
  // Every class is an Object. The port models no java.lang.Object class, so a
  // root class must not fall through to the parent walk (the LoadingDialog
  // constructor stores `this` into an Object-typed field).
  if (name == "java.lang.Object")
    return true;
  const auto named = [&](uint32_t index) -> const TufaClass * {
    if (index >= cls->constants.size() || cls->constants[index].kind != 4 ||
        cls->constants[index].a >= cls->constants.size() ||
        cls->constants[cls->constants[index].a].kind != 0)
      return nullptr;
    for (const auto *candidate : classes)
      if (candidate->name == cls->constants[cls->constants[index].a].text)
        return candidate;
    return nullptr;
  };
  for (uint32_t index : cls->interfaces)
    if (classAssignableTo(named(index), name, classes, depth + 1))
      return true;
  if (cls->classHeader[3] == UINT32_MAX)
    return false;
  return classAssignableTo(named(cls->classHeader[3]), name, classes,
                           depth + 1);
}

static bool literalInitializer(const std::vector<TufaNode> &tree,
                               const std::string &type, VmValue &value) {
  if (type == "I" && tree.size() >= 6 && tree.size() <= 64 &&
      tree.back().op == 0x10 && !tree.back().wide) {
    std::vector<uint32_t> values;
    bool multiplied = false;
    size_t pc = 0;
    while (pc + 1 < tree.size()) {
      const auto &node = tree[pc];
      if (node.op == 0x07 && node.wide && node.operand == 28 &&
          pc + 2 < tree.size() && tree[pc + 1].op == 0x0b &&
          tree[pc + 1].wide) {
        values.push_back(tree[pc + 1].operand);
        pc += 2;
      } else if (node.op == 0x07 && node.wide && node.operand == 19 &&
                 values.size() >= 2) {
        const uint32_t right = values.back();
        values.pop_back();
        values.back() *= right;
        multiplied = true;
        ++pc;
      } else
        return false;
    }
    if (pc != tree.size() - 1 || values.size() != 1 || !multiplied)
      return false;
    int32_t result;
    std::memcpy(&result, &values[0], sizeof(result));
    value = result;
    return true;
  }
  if (type == "F" && tree.size() == 6 && tree[0].op == 0x07 && tree[0].wide &&
      tree[0].operand == 28 && tree[1].op == 0x0a && tree[1].wide &&
      tree[2].op == 0x07 && tree[2].wide && tree[2].operand == 28 &&
      tree[3].op == 0x0a && tree[3].wide && tree[4].op == 0x07 &&
      tree[4].wide && tree[4].operand == 19 && tree[5].op == 0x10 &&
      !tree[5].wide) {
    float left, right;
    std::memcpy(&left, &tree[1].operand, sizeof(left));
    std::memcpy(&right, &tree[3].operand, sizeof(right));
    if (!std::isfinite(left) || !std::isfinite(right))
      return false;
    const double product =
        static_cast<double>(left) * static_cast<double>(right);
    if (std::fabs(product) > std::numeric_limits<float>::max())
      return false;
    const float rounded = static_cast<float>(product);
    if (static_cast<double>(rounded) != product)
      return false;
    value = rounded;
    return true;
  }
  const bool negate = tree.size() == 4;
  if ((!negate && tree.size() != 3) || tree[0].op != 0x07 || !tree[0].wide ||
      tree[0].operand != 28 || !tree[1].wide || tree.back().op != 0x10 ||
      tree.back().wide ||
      (negate && ((type != "F" && type != "I") || tree[2].op != 0x07 ||
                  !tree[2].wide || tree[2].operand != 22)))
    return false;
  uint32_t bits = tree[1].operand;
  if (type == "I" && tree[1].op == 0x0b) {
    if (negate)
      bits = uint32_t{0} - bits;
    int32_t number;
    std::memcpy(&number, &bits, sizeof(number));
    value = number;
    return true;
  }
  if (type == "F" && tree[1].op == 0x0b) {
    if (negate)
      bits = uint32_t{0} - bits;
    int32_t number;
    std::memcpy(&number, &bits, sizeof(number));
    if (number < -16777216 || number > 16777216)
      return false;
    value = static_cast<float>(number);
    return true;
  }
  if (type != "F" || tree[1].op != 0x0a)
    return false;
  if (negate) {
    if ((bits & 0x7f800000U) == 0x7f800000U)
      return false;
    bits ^= 0x80000000U;
  }
  float number;
  std::memcpy(&number, &bits, sizeof(number));
  value = number;
  return true;
}

// A static location names its class and the state table, not a pointer into the
// table: initializing another class while a tree runs appends to the table and
// can move its entries.
struct FieldLocation {
  const VmObject *object;
  const std::vector<VmStaticFields> *staticTable;
  const TufaClass *staticClass;
  size_t index;
};
struct LocalLocation {
  std::vector<VmValue> *locals;
  std::vector<std::string> *types;
  size_t index;
};
struct ArrayLocation {
  const VmObject *array;
  size_t index;
};
using StackEntry =
    std::variant<VmValue, FieldLocation, LocalLocation, ArrayLocation>;

struct VmFrame {
  const TufaClass *cls;
  const std::vector<TufaNode> *tree;
  size_t pc = 0;
  unsigned depth;
  std::vector<VmValue> locals;
  std::vector<std::string> localTypes;
  std::vector<StackEntry> stack;
  bool completed = false;
  bool staticMethod = false;
  enum class ReturnKind {
    None,
    Constructor,
    Void,
    Float,
    Integer,
    Reference
  } waiting = ReturnKind::None;
  std::string returnType;

  VmFrame(const TufaClass *owner, const std::vector<TufaNode> &nodes,
          std::vector<VmValue> values, unsigned level)
      : cls(owner), tree(&nodes), depth(level), locals(std::move(values)) {
    for (const auto &value : locals) {
      if (std::holds_alternative<int32_t>(value))
        localTypes.push_back("I");
      else if (std::holds_alternative<float>(value))
        localTypes.push_back("F");
      else
        localTypes.emplace_back();
    }
  }
};

struct VmExecutionState {
  const std::vector<NativeEntry> *natives;
  const std::vector<const TufaClass *> *classes;
  const std::vector<VmStaticFields> *staticFields;
  VmArena *arena;
  std::vector<std::unique_ptr<VmFrame>> frames;
  VmResult result{true, int32_t{0}, {}};
  bool finished = false;
  bool yieldRequested = false;
  unsigned synchronousDepth = 0;
};

std::optional<VmValue> vmActiveStaticField(const VmArena &arena,
                                           const std::string &className,
                                           const std::string &fieldName) {
  const auto *execution = arena.activeExecution;
  if (!execution || !execution->staticFields)
    return std::nullopt;
  for (const auto &state : *execution->staticFields)
    if (state.owner && state.owner->name == className)
      for (const auto &field : state.fields)
        if (field.name == fieldName)
          return field.value;
  return std::nullopt;
}

static std::vector<VmField> &locationFields(const FieldLocation &location) {
  if (location.object)
    return location.object->fields;
  for (const auto &state : *location.staticTable)
    if (state.owner == location.staticClass)
      return state.fields;
  std::abort();
}

static bool concatOperandBytes(const VmValue &value, std::string &out,
                               std::string &error, VmArena *arena,
                               const std::vector<const TufaClass *> *classes) {
            if (const auto *integer = std::get_if<int32_t>(&value)) {
              out = stockFormatInt(*integer, 0, ' ');
              return true;
            }
            if (const auto *number = std::get_if<float>(&value)) {
              return stockFormatFloatConcat(*number, out, error);
            }
            const auto *object = std::get_if<const VmObject *>(&value);
            if (!object || !arena)
              return false;
            if (!*object)
              return true; // stock: a null reference contributes nothing
            if (supportedReference(*object, "Ljava.lang.String;", arena,
                                   classes)) {
              bool found = false;
              for (const auto &item : arena->strings)
                if (item.second == *object) {
                  if (found)
                    return false;
                  out = item.first;
                  found = true;
                }
              return found;
            }
            error = "concat operand is not a String after stock conversion";
            return false;
}

static const VmValue &readValue(const StackEntry &entry) {
  if (const auto *value = std::get_if<VmValue>(&entry))
    return *value;
  if (const auto *local = std::get_if<LocalLocation>(&entry))
    return (*local->locals)[local->index];
  if (const auto *array = std::get_if<ArrayLocation>(&entry))
    return array->array->arrayElements[array->index];
  const auto &location = std::get<FieldLocation>(entry);
  return locationFields(location)[location.index].value;
}

// Stock keeps one value box per slot on the stack and in every field table
// (FUN_004087e0 returns the field's box, FUN_0041ab60 writes the box payload),
// so in-place suboperations write through the box. Returns the slot a box
// names, or null when the entry holds a plain value.
static VmValue *boxedSlot(StackEntry &entry) {
  if (const auto *field = std::get_if<FieldLocation>(&entry))
    return &locationFields(*field)[field->index].value;
  if (const auto *local = std::get_if<LocalLocation>(&entry))
    return &(*local->locals)[local->index];
  if (const auto *array = std::get_if<ArrayLocation>(&entry))
    return &array->array->arrayElements[array->index];
  return nullptr;
}

// FUN_0041b2d0 increments the original box; the two dispatchers differ in
// whether they copy the old value for the expression result.
static std::optional<std::string> incrementBoxed(StackEntry &entry,
                                                 bool decrement = false) {
  VmValue *slot = boxedSlot(entry);
  VmValue *target = slot ? slot : std::get_if<VmValue>(&entry);
  if (!target)
    return "increment stack underflow";
  if (const auto *value = std::get_if<int32_t>(target)) {
    const uint32_t bits =
        static_cast<uint32_t>(*value) + (decrement ? UINT32_MAX : 1u);
    int32_t result;
    std::memcpy(&result, &bits, sizeof result);
    *target = result;
    return {};
  }
  if (const auto *value = std::get_if<float>(target)) {
    *target = *value + (decrement ? -1.0f : 1.0f);
    return {};
  }
  return {};
}

static VmResult unsupportedOperation(const TufaClass *cls, size_t pc,
                                     const TufaNode &node) {
  char op[5];
  std::snprintf(op, sizeof(op), "0x%02x", static_cast<unsigned>(node.op));
  std::string message = (cls ? cls->name : "<tree>") + std::string(": node ") +
                        std::to_string(pc) + ": unsupported operation " + op;
  if (node.wide && (node.op == 0x07 || node.op == 0x08))
    message += ", suboperation " + std::to_string(node.operand);
  return {false, int32_t{0}, message};
}

static VmResult execute(const TufaClass *cls, const std::vector<TufaNode> &tree,
                        std::vector<VmValue> locals, unsigned depth,
                        const std::vector<NativeEntry> *natives,
                        const std::vector<const TufaClass *> *classes,
                        const std::vector<VmStaticFields> *staticFields,
                        VmArena *arena);
static VmScriptCall
makeScriptCall(const std::vector<NativeEntry> *natives,
               const std::vector<const TufaClass *> *classes,
               const std::vector<VmStaticFields> *staticFields, VmArena *arena);

static const TufaClass *
findRegistered(const std::string &name,
               const std::vector<const TufaClass *> *classes) {
  const TufaClass *found = nullptr;
  if (classes)
    for (const auto *candidate : *classes)
      if (candidate && candidate->name == name)
        found = candidate;
  return found;
}

static bool hasStaticState(const TufaClass *cls,
                           const std::vector<VmStaticFields> *staticFields) {
  if (staticFields)
    for (const auto &state : *staticFields)
      if (state.owner == cls)
        return true;
  return false;
}

// Stock loads a class the first time a script names it (FUN_00410890) and
// initializes it on first use (FUN_00403e80). Returns the registered class.
// When it is missing, or has no static state yet, and the arena has an
// ensureClass hook, the hook loads and initializes it first. A hook failure
// comes back in `error`. Without a hook this is a plain lookup and the callers'
// own checks report an unregistered or uninitialized class. The hook can grow
// the registry and the static field table, so callers must not hold iterators
// or pointers into either across this call.
static const TufaClass *
resolveClass(const std::string &name,
             const std::vector<const TufaClass *> *classes,
             const std::vector<VmStaticFields> *staticFields, VmArena *arena,
             unsigned depth, std::string &error) {
  error.clear();
  const TufaClass *found = findRegistered(name, classes);
  if (found && (!staticFields || hasStaticState(found, staticFields)))
    return found;
  if (!arena || !arena->ensureClass)
    return found;
  const unsigned outerDepth = arena->nativeDepth;
  arena->nativeDepth = depth;
  const auto ensured = arena->ensureClass(name);
  arena->nativeDepth = outerDepth;
  if (!ensured.ok) {
    error = ensured.error.empty() ? "class initialization failed: " + name
                                  : ensured.error;
    return nullptr;
  }
  found = findRegistered(name, classes);
  if (!found)
    error = "class not registered: " + name;
  return found;
}

static VmResult internString(const std::string &bytes,
                             const std::vector<const TufaClass *> *classes,
                             const std::vector<VmStaticFields> *staticFields,
                             VmArena *arena) {
  if (!classes || !staticFields || !arena)
    return {false, int32_t{0}, "string literal context unavailable"};
  if (bytes.find('\0') != std::string::npos)
    return {false, int32_t{0}, "embedded NUL in string literal"};
  std::string error;
  const TufaClass *stringClass =
      resolveClass("java.lang.String", classes, staticFields, arena,
                   arena->nativeDepth, error);
  if (!stringClass)
    return {false, int32_t{0},
            error.empty() ? "string class not registered" : error};
  size_t initialized = 0;
  for (const auto &state : *staticFields)
    if (state.owner == stringClass)
      ++initialized;
  if (initialized != 1)
    return {false, int32_t{0}, "string class initialization unavailable"};
  for (const auto &entry : arena->strings)
    if (entry.first == bytes && entry.second->runtimeClass == stringClass)
      return {true, entry.second, {}};
  if (arena->objects.size() >= arena->limit)
    return {false, int32_t{0}, "object arena full"};
  auto object = std::make_unique<VmObject>();
  object->runtimeClass = stringClass;
  const auto *identity = object.get();
  arena->objects.push_back(std::move(object));
  arena->strings.emplace_back(bytes, identity);
  return {true, identity, {}};
}

static VmResult stringLiteral(const TufaClass &cls, uint32_t index,
                              const std::vector<const TufaClass *> *classes,
                              const std::vector<VmStaticFields> *staticFields,
                              VmArena *arena) {
  if (index >= cls.constants.size() || cls.constants[index].kind != 0)
    return {false, int32_t{0}, "string literal context unavailable"};
  return internString(cls.constants[index].text, classes, staticFields, arena);
}

VmResult vmInitializeClass(const TufaClass &cls,
                           const std::vector<const TufaClass *> &classes,
                           std::vector<VmStaticFields> &staticFields,
                           const VmResourceResolver *resourceResolver,
                           VmArena *arena) {
  for (size_t i = 0; i < classes.size(); ++i) {
    if (!classes[i] || classes[i]->name.empty())
      return {false, int32_t{0}, "invalid class registry"};
    for (size_t j = 0; j < i; ++j)
      if (classes[i]->name == classes[j]->name)
        return {false, int32_t{0}, "duplicate class"};
  }
  for (size_t i = 0; i < staticFields.size(); ++i) {
    if (!staticFields[i].owner)
      return {false, int32_t{0}, "invalid static state"};
    for (size_t j = 0; j < i; ++j)
      if (staticFields[i].owner == staticFields[j].owner)
        return {false, int32_t{0}, "duplicate static state"};
  }
  std::vector<const TufaClass *> active;
  std::vector<const TufaClass *> order;
  std::string graphError;
  std::function<bool(const TufaClass *)> visit = [&](const TufaClass *current) {
    bool loaded = false;
    for (const auto *candidate : classes)
      if (candidate == current)
        loaded = true;
    if (!loaded || !current->hasClassHeader || active.size() >= 128) {
      graphError = "unsupported class initialization ancestry";
      return false;
    }
    for (const auto *prior : active)
      if (prior == current) {
        graphError = "cyclic class initialization: " + current->name;
        return false;
      }
    for (const auto *prior : order)
      if (prior == current)
        return true;
    active.push_back(current);
    auto resolve = [&](uint32_t index) -> const TufaClass * {
      if (index >= current->constants.size())
        return nullptr;
      const auto &ref = current->constants[index];
      if (ref.kind != 4 || ref.a >= current->constants.size() ||
          current->constants[ref.a].kind != 0)
        return nullptr;
      for (const auto *candidate : classes)
        if (candidate->name == current->constants[ref.a].text)
          return candidate;
      return nullptr;
    };
    const uint32_t parentIndex = current->classHeader[3];
    if (parentIndex != UINT32_MAX) {
      const auto *parent = resolve(parentIndex);
      if (!parent) {
        graphError = "parent class not registered: " + current->name;
        return false;
      }
      if (!visit(parent))
        return false;
    }
    for (uint32_t index : current->interfaces) {
      const auto *iface = resolve(index);
      if (!iface || !iface->hasClassHeader || iface->classHeader[1] != 1) {
        graphError = "invalid or missing interface: " + current->name;
        return false;
      }
      if (!visit(iface))
        return false;
    }
    active.pop_back();
    order.push_back(current);
    return true;
  };
  if (!visit(&cls))
    return {false, int32_t{0}, graphError};
  std::vector<const TufaClass *> preexisting;
  for (const auto &state : staticFields)
    preexisting.push_back(state.owner);
  const auto wasInitialized = [&](const TufaClass *c) {
    return std::find(preexisting.begin(), preexisting.end(), c) !=
           preexisting.end();
  };
  if (arena)
    for (const auto *c : order) {
      if (wasInitialized(c))
        continue;
      for (const auto &failed : arena->failedInitializations)
        if (failed.first == c)
          return {false, int32_t{0},
                  c->name +
                      ": initialization failed earlier and is not retried: " +
                      failed.second};
    }
  if (arena && arena->initDepth >= 32)
    return {false, int32_t{0},
            "class initialization nested too deeply: " + cls.name};

  // A host call that starts initialization outside vmExecute gets the same
  // script call hook and failure trace.
  const bool topLevel = arena && !arena->callScript;
  const VmResourceResolver *outerResolver =
      arena ? arena->resourceResolver : nullptr;
  if (arena) {
    if (topLevel) {
      arena->failureTrace.clear();
      arena->callScript =
          makeScriptCall(arena->natives, &classes, &staticFields, arena);
    }
    if (resourceResolver)
      arena->resourceResolver = resourceResolver;
    ++arena->initDepth;
  }
  std::vector<const TufaClass *> registered;
  const auto fieldsOf = [&](const TufaClass *owner) -> std::vector<VmField> & {
    for (auto &state : staticFields)
      if (state.owner == owner)
        return state.fields;
    std::abort();
  };
  const auto finish = [&](VmResult result, const TufaClass *failedClass,
                          bool remember) {
    if (arena) {
      --arena->initDepth;
      arena->resourceResolver = outerResolver;
      if (topLevel)
        arena->callScript = nullptr;
    }
    if (result.ok)
      return result;
    for (const auto *owner : registered)
      staticFields.erase(std::remove_if(staticFields.begin(),
                                        staticFields.end(),
                                        [&](const VmStaticFields &state) {
                                          return state.owner == owner;
                                        }),
                         staticFields.end());
    if (arena && remember && failedClass)
      arena->failedInitializations.emplace_back(failedClass, result.error);
    return result;
  };
  for (auto it = order.begin(); it != order.end(); ++it) {
    if (hasStaticState(*it, &staticFields)) {
      if (wasInitialized(*it))
        continue;
      return finish(
          {false, int32_t{0},
           (*it)->name +
               ": reentrant class initialization: an ancestor's static "
               "initializer initialized this class; stock would run its static "
               "initializers twice"},
          nullptr, false);
    }
    staticFields.push_back(VmStaticFields{*it, {}});
    registered.push_back(*it);
    for (const auto &list : (*it)->fields)
      for (const auto &field : list) {
        const auto &w = field.words;
        if (!(w[0] & 8))
          continue;
        if (w[1] >= (*it)->constants.size() ||
            w[2] >= (*it)->constants.size() ||
            (*it)->constants[w[1]].kind != 0 ||
            (*it)->constants[w[2]].kind != 0 ||
            ((*it)->constants[w[2]].text != "I" &&
             (*it)->constants[w[2]].text != "F" &&
             !referenceType((*it)->constants[w[2]].text)))
          return finish(
              {false, int32_t{0},
               (*it)->name + "." +
                   (w[1] < (*it)->constants.size() ? (*it)->constants[w[1]].text
                                                   : "<invalid>") +
                   ": unsupported static field type " +
                   (w[2] < (*it)->constants.size() ? (*it)->constants[w[2]].text
                                                   : "<invalid>")},
              *it, false);
        const auto name = (*it)->constants[w[1]].text;
        for (const auto &existing : fieldsOf(*it))
          if (existing.name == name)
            return finish({false, int32_t{0}, "duplicate static field"}, *it,
                          false);
        const auto type = (*it)->constants[w[2]].text;
        VmValue value = type == "F" ? VmValue{0.0f}
                        : type == "I"
                            ? VmValue{int32_t{0}}
                            : VmValue{static_cast<const VmObject *>(nullptr)};
        // Stock defines the field first (FUN_004038c0), then runs its
        // initializer tree, then assigns the result.
        const size_t slot = fieldsOf(*it).size();
        fieldsOf(*it).push_back({name, value, type});
        if (w[3] != UINT32_MAX) {
          if (w[3] >= (*it)->trees.size())
            return finish({false, int32_t{0}, "invalid static initializer"},
                          *it, false);
          const auto &tree = (*it)->trees[w[3]];
          if (type == "Ljava.lang.String;" && tree.size() == 3 &&
              tree[0].op == 0x07 && tree[0].wide && tree[0].operand == 28 &&
              tree[1].op == 0x09 && tree[1].wide && tree[2].op == 0x10 &&
              !tree[2].wide) {
            const auto result = stringLiteral(**it, tree[1].operand, &classes,
                                              &staticFields, arena);
            if (!result.ok)
              return finish({false, int32_t{0},
                             (*it)->name + "." + name + ": " + result.error},
                            *it, true);
            value = result.value;
          } else if (type == "I" && tree.size() == 3 && tree[0].op == 0x07 &&
                     tree[0].wide && tree[0].operand == 28 &&
                     tree[1].op == 0x0e && tree[1].wide && tree[2].op == 0x10 &&
                     !tree[2].wide) {
            if (!resourceResolver || !*resourceResolver ||
                tree[1].operand >= (*it)->constants.size())
              return finish(
                  {false, int32_t{0}, "resource resolver unavailable"}, *it,
                  true);
            const auto &resource = (*it)->constants[tree[1].operand];
            if (resource.kind != 3 || resource.a >= (*it)->constants.size() ||
                (*it)->constants[resource.a].kind != 0)
              return finish({false, int32_t{0}, "unsupported resource literal"},
                            *it, true);
            const auto resolved = (*resourceResolver)(
                (*it)->constants[resource.a].text, resource.b);
            if (!resolved)
              return finish({false, int32_t{0}, "resource resolution failed"},
                            *it, true);
            int32_t result;
            const uint32_t bits = *resolved;
            std::memcpy(&result, &bits, sizeof(result));
            value = result;
          } else if (!literalInitializer(tree, type, value)) {
            const std::string label = (*it)->name + "." + name;
            if (!arena)
              return finish({false, int32_t{0},
                             label + ": unsupported static initializer tree " +
                                 std::to_string(w[3])},
                            *it, false);
            // Stock runs the tree in a frame of the class and assigns the value
            // it leaves (FUN_00403e80).
            const auto result =
                execute(*it, tree, {}, arena->nativeDepth + 1, arena->natives,
                        &classes, &staticFields, arena);
            if (!result.ok)
              return finish({false, int32_t{0}, label + ": " + result.error},
                            *it, true);
            if (result.voidReturn)
              return finish(
                  {false, int32_t{0}, label + ": initializer left no value"},
                  *it, true);
            value = result.value;
            if (type == "I" && std::holds_alternative<float>(value) &&
                !floatToInt(value, value))
              return finish(
                  {false, int32_t{0},
                   label + ": unsupported float to integer conversion"},
                  *it, true);
            if (type == "F" && std::holds_alternative<int32_t>(value)) {
              float converted;
              if (!numericFloat(value, converted) ||
                  static_cast<double>(converted) !=
                      static_cast<double>(std::get<int32_t>(value)))
                return finish(
                    {false, int32_t{0},
                     label + ": unsupported integer to float conversion"},
                    *it, true);
              value = converted;
            }
            const auto *object = std::get_if<const VmObject *>(&value);
            const bool fits =
                type == "I"   ? std::holds_alternative<int32_t>(value)
                : type == "F" ? std::holds_alternative<float>(value)
                              : object && supportedReference(*object, type,
                                                             arena, &classes);
            if (!fits)
              return finish(
                  {false, int32_t{0},
                   label + ": initializer value does not fit " + type},
                  *it, true);
          }
        }
        fieldsOf(*it)[slot].value = value;
      }
  }
  return finish({true, int32_t{0}, {}}, nullptr, false);
}

// Stock FUN_0045d910 reads I, F, L and [ parameters in descriptor order and
// reports anything else as an unknown parameter type. Returns false for
// descriptors this port cannot represent.
static bool nativeSignature(const std::string &descriptor,
                            std::vector<std::string> &params,
                            std::string &returnType) {
  params.clear();
  if (descriptor.size() < 3 || descriptor.front() != '(')
    return false;
  size_t i = 1;
  const auto element = [&](size_t &at) {
    if (at >= descriptor.size())
      return false;
    if (descriptor[at] == 'I' || descriptor[at] == 'F') {
      ++at;
      return true;
    }
    if (descriptor[at] != 'L')
      return false;
    const auto end = descriptor.find(';', at);
    if (end == std::string::npos)
      return false;
    at = end + 1;
    return true;
  };
  while (i < descriptor.size() && descriptor[i] != ')') {
    const size_t start = i;
    while (i < descriptor.size() && descriptor[i] == '[')
      ++i;
    if (!element(i))
      return false;
    params.push_back(descriptor.substr(start, i - start));
  }
  if (i >= descriptor.size())
    return false;
  ++i;
  const size_t start = i;
  if (i < descriptor.size() && descriptor[i] == 'V')
    ++i;
  else {
    while (i < descriptor.size() && descriptor[i] == '[')
      ++i;
    if (!element(i))
      return false;
  }
  if (i != descriptor.size())
    return false;
  returnType = descriptor.substr(start);
  return true;
}

// A null reference fits any reference type. Otherwise the object must belong to
// the arena and be an array with the exact descriptor or an instance of the
// named class or a subclass of it.
static bool referenceAssignable(const VmObject *object,
                                const std::string &descriptor,
                                const VmArena *arena,
                                const std::vector<const TufaClass *> *classes) {
  if (!object)
    return referenceType(descriptor);
  if (!arena || !classes || !referenceType(descriptor))
    return false;
  bool owned = false;
  for (const auto &entry : arena->objects)
    if (entry.get() == object)
      owned = true;
  if (!owned)
    return false;
  // Every array is an Object; stock's System.arraycopy takes Object parameters
  // and the real scripts pass arrays.
  if (descriptor == "Ljava.lang.Object;" && !object->runtimeClass &&
      !object->arrayDescriptor.empty())
    return true;
  if (descriptor.front() == '[')
    return object->arrayDescriptor == descriptor && !object->runtimeClass;
  if (descriptor.front() != 'L' || descriptor.back() != ';' ||
      !object->runtimeClass)
    return false;
  const std::string name = descriptor.substr(1, descriptor.size() - 2);
  const TufaClass *wanted = nullptr;
  for (const auto *candidate : *classes)
    if (candidate && candidate->name == name)
      wanted = candidate;
  return wanted &&
         classAssignableTo(object->runtimeClass, wanted->name, *classes, 0);
}

// Finds the method in `owner` or its ancestors. An exact descriptor, or with
// paramsOnly a descriptor that starts with the given "(...)" parameter text,
// must match. `owner` ends at the declaring class.
static const TufaMethod *
findMethod(const TufaClass *&owner, const std::string &name,
           const std::string &descriptor, bool paramsOnly,
           const std::vector<const TufaClass *> *classes, std::string &error) {
  std::vector<const TufaClass *> searched;
  while (owner) {
    if (searched.size() >= 128 ||
        std::find(searched.begin(), searched.end(), owner) != searched.end()) {
      error = "cyclic method ancestry";
      return nullptr;
    }
    searched.push_back(owner);
    const TufaMethod *method = nullptr;
    for (const auto &list : owner->methods)
      for (const auto &candidate : list) {
        const auto &w = candidate.words;
        if (w[1] >= owner->constants.size() ||
            w[2] >= owner->constants.size() ||
            owner->constants[w[1]].text != name)
          continue;
        const auto &text = owner->constants[w[2]].text;
        if (paramsOnly ? text.compare(0, descriptor.size(), descriptor) != 0
                       : text != descriptor)
          continue;
        if (method) {
          error = paramsOnly ? "ambiguous dynamic method" : "ambiguous method";
          return nullptr;
        }
        method = &candidate;
      }
    if (method)
      return method;
    if (!classes)
      return nullptr;
    const TufaClass *parent = nullptr;
    if (!parentClass(*owner, *classes, parent)) {
      error = "invalid method ancestry";
      return nullptr;
    }
    owner = parent;
  }
  return nullptr;
}

// The one native call path. The stack holds the arguments in descriptor order
// with the argument count on top. Like stock's resolved-name path, the count
// value is discarded and the descriptor decides how many arguments are taken.
// Handlers are found by declaring class, name and full descriptor; the result
// is checked against the descriptor's return type and pushed (void pushes the
// monostate marker).
static VmResult callNative(std::vector<StackEntry> &stack,
                           const TufaClass &owner, const std::string &name,
                           const std::string &descriptor, bool isStatic,
                           const VmObject *receiver,
                           const std::vector<NativeEntry> *natives,
                           const std::vector<const TufaClass *> *classes,
                           VmArena *arena) {
  const std::string label = owner.name + "." + name + descriptor;
  std::vector<std::string> params;
  std::string returnType;
  if (!nativeSignature(descriptor, params, returnType))
    return {false, int32_t{0}, "unsupported native descriptor: " + label};
  if (!natives)
    return {false, int32_t{0}, "native not registered: " + label};
  const NativeEntry *entry = nullptr;
  for (const auto &candidate : *natives) {
    if (candidate.className == owner.name && candidate.method == name &&
        candidate.descriptor == descriptor) {
      if (entry)
        return {false, int32_t{0}, "ambiguous native"};
      entry = &candidate;
    }
  }
  if (!entry || !entry->handler)
    return {false, int32_t{0}, "native not registered: " + label};
  if (stack.size() < params.size() + 1)
    return {false, int32_t{0}, "missing call arguments"};
  const VmObject *nativeReceiver = nullptr;
  if (!isStatic) {
    if (!receiver || !arena || !classes)
      return {false, int32_t{0}, "missing native receiver"};
    bool owned = false;
    for (const auto &object : arena->objects)
      if (object.get() == receiver)
        owned = true;
    if (!owned || !receiver->runtimeClass ||
        !constructorAncestor(receiver->runtimeClass, &owner, *classes))
      return {false, int32_t{0}, "invalid native receiver"};
    nativeReceiver = receiver;
  }
  const size_t base = stack.size() - 1 - params.size();
  std::vector<VmValue> arguments;
  for (size_t i = 0; i < params.size(); ++i) {
    const auto &value = readValue(stack[base + i]);
    const auto &type = params[i];
    bool fits = false;
    if (type == "I") {
      if (std::holds_alternative<int32_t>(value)) {
        arguments.push_back(value);
        fits = true;
      } else {
        VmValue converted;
        if (floatToInt(value, converted)) {
          arguments.push_back(converted);
          fits = true;
        }
      }
    } else if (type == "F") {
      if (std::holds_alternative<float>(value)) {
        arguments.push_back(value);
        fits = true;
      } else if (const auto *integer = std::get_if<int32_t>(&value)) {
        const float converted = static_cast<float>(*integer);
        if (static_cast<double>(converted) == static_cast<double>(*integer)) {
          arguments.emplace_back(converted);
          fits = true;
        }
      }
    } else if (const auto *object = std::get_if<const VmObject *>(&value)) {
      if (referenceAssignable(*object, type, arena, classes)) {
        arguments.push_back(value);
        fits = true;
      }
    }
    if (!fits)
      return {false, int32_t{0}, "native argument type mismatch: " + label};
  }
  const auto describe = [&](const VmValue &value) -> std::string {
    if (const auto *n = std::get_if<int32_t>(&value))
      return std::to_string(*n);
    if (const auto *f = std::get_if<float>(&value))
      return std::to_string(*f);
    const auto *pointer = std::get_if<const VmObject *>(&value);
    if (!pointer || !*pointer)
      return "null";
    if (arena) {
      for (const auto &entry : arena->strings)
        if (entry.second == *pointer)
          return "\"" + entry.first.substr(0, 28) + "\"";
      for (const auto &state : arena->resourceStates)
        if (state.owner == *pointer) {
          char id[20];
          std::snprintf(id, sizeof id, "ref:%08x", state.words[2]);
          return id;
        }
    }
    return (*pointer)->runtimeClass ? (*pointer)->runtimeClass->name : "object";
  };
  std::string call;
  if (runlog::active()) {
    const auto dot = owner.name.find_last_of('.');
    call = owner.name.substr(dot == std::string::npos ? 0 : dot + 1) + "." +
           name + descriptor.substr(0, 24);
    if (nativeReceiver)
      call += " this=" + describe(VmValue{nativeReceiver});
    for (const auto &argument : arguments)
      call += " " + describe(argument);
  }
  const auto result = entry->handler(arguments, nativeReceiver, arena, classes);
  if (!call.empty()) {
    std::string outcome = "void";
    if (const auto *value = std::get_if<VmValue>(&result))
      outcome = describe(*value);
    else if (std::holds_alternative<std::string>(result))
      outcome = "error";
    if (call.size() > 108)
      call.resize(108);
    if (outcome.size() > 30)
      outcome.resize(30);
    call += " -> " + outcome;
    runlog::eventText(runlog::EventKind::NativeCall, call.c_str());
  }
  if (runlog::active() && owner.name.find("GroundRef") != std::string::npos) {
    const bool ok = !std::holds_alternative<std::string>(result);
    runlog::event(runlog::EventKind::Note,
                  "grlast {\"v\":1,\"m\":\"%s\",\"ok\":%u}", name.c_str(),
                  ok ? 1u : 0u);
  }
  if (const auto *error = std::get_if<std::string>(&result))
    return {false, int32_t{0}, *error};
  VmValue pushed{std::monostate{}};
  if (returnType == "V") {
    if (!std::holds_alternative<std::monostate>(result))
      return {false, int32_t{0}, "invalid native void result"};
  } else {
    const auto *value = std::get_if<VmValue>(&result);
    bool fits = false;
    if (value) {
      if (returnType == "I")
        fits = std::holds_alternative<int32_t>(*value);
      else if (returnType == "F")
        fits = std::holds_alternative<float>(*value);
      else if (const auto *object = std::get_if<const VmObject *>(value))
        fits = referenceAssignable(*object, returnType, arena, classes);
    }
    if (!fits)
      return {false, int32_t{0}, "invalid native result"};
    pushed = *value;
  }
  stack.erase(stack.end() - static_cast<std::ptrdiff_t>(params.size() + 1),
              stack.end());
  stack.emplace_back(pushed);
  return {true, int32_t{0}, {}};
}

static VmResult executeTree(VmExecutionState &execution, VmFrame &frame);

static VmResult drive(VmExecutionState &execution, size_t base, size_t budget,
                      bool &finished) {
  finished = false;
  const auto fail = [&](VmResult result) {
    if (execution.arena)
      for (size_t i = execution.frames.size(); i > base; --i) {
        const auto &failed = *execution.frames[i - 1];
        if (failed.cls) {
          size_t treeIndex = 0;
          while (treeIndex < failed.cls->trees.size() &&
                 &failed.cls->trees[treeIndex] != failed.tree)
            ++treeIndex;
          const std::string tree = treeIndex < failed.cls->trees.size()
                                       ? std::to_string(treeIndex)
                                       : "external";
          const auto line = failed.pc < failed.tree->size()
                                ? (*failed.tree)[failed.pc].source
                                : 0;
          execution.arena->failureTrace.push_back(
              failed.cls->name + " (tree " + tree + " pc " +
              std::to_string(failed.pc) + " line " + std::to_string(line) +
              ")");
        }
      }
    execution.frames.resize(base);
    finished = true;
    return result;
  };
  while (execution.frames.size() > base && budget--) {
    VmFrame &frame = *execution.frames.back();
    auto result = executeTree(execution, frame);
    if (!result.ok)
      return fail(std::move(result));
    if (!frame.completed) {
      if (base == 0 && execution.yieldRequested)
        return {true, int32_t{0}, {}};
      continue;
    }
    std::string returnedFrom;
    if (frame.cls) {
      size_t treeIndex = 0;
      while (treeIndex < frame.cls->trees.size() &&
             &frame.cls->trees[treeIndex] != frame.tree)
        ++treeIndex;
      returnedFrom = frame.cls->name + " tree " + std::to_string(treeIndex);
    }
    execution.frames.pop_back();
    if (execution.frames.size() == base) {
      finished = true;
      return result;
    }
    auto &parent = *execution.frames.back();
    switch (parent.waiting) {
    case VmFrame::ReturnKind::Constructor:
      if (!result.voidReturn)
        return fail({false, int32_t{0}, "constructor returned a value"});
      break;
    case VmFrame::ReturnKind::Void:
      parent.stack.emplace_back(result.voidReturn ? VmValue{std::monostate{}}
                                                  : result.value);
      break;
    case VmFrame::ReturnKind::Float:
      if (result.voidReturn || !std::holds_alternative<float>(result.value))
        return fail({false, int32_t{0}, "unsupported float return"});
      parent.stack.push_back(result.value);
      break;
    case VmFrame::ReturnKind::Integer:
      // Stock pushes the returned box unchanged (0x2b: FUN_00423ba0); the
      // installed Vehicle.getTotalPrice()I returns Chassis.currentPrice()F.
      // The value keeps its dynamic type for the consumer.
      if (result.voidReturn ||
          !(std::holds_alternative<int32_t>(result.value) ||
            std::holds_alternative<float>(result.value)))
        return fail({false, int32_t{0},
                     "unsupported return type from=" + returnedFrom +
                         " index=" + std::to_string(result.value.index()) +
                         " void=" + std::to_string(result.voidReturn)});
      parent.stack.push_back(result.value);
      break;
    case VmFrame::ReturnKind::Reference: {
      // Stock never type-checks a returned reference against the declared
      // return type: the installed Osd.createButton(IFFString;ResourceRef;I
      // Object;I) is declared to return Button but returns the GameRef that
      // createIcon made, and Painter stores it. Only a non-reference value is
      // refused here.
      if (result.voidReturn ||
          (!std::holds_alternative<std::monostate>(result.value) &&
           !std::holds_alternative<const VmObject *>(result.value)))
        return fail({false, int32_t{0},
                     "unsupported reference return from=" + returnedFrom +
                         " index=" + std::to_string(result.value.index()) +
                         " void=" + std::to_string(result.voidReturn)});
      parent.stack.push_back(result.value);
      break;
    }
    case VmFrame::ReturnKind::None:
      return fail({false, int32_t{0}, "missing script continuation"});
    }
    parent.waiting = VmFrame::ReturnKind::None;
  }
  return {true, int32_t{0}, {}};
}

static VmResult execute(const TufaClass *cls, const std::vector<TufaNode> &tree,
                        std::vector<VmValue> locals, unsigned depth,
                        const std::vector<NativeEntry> *natives,
                        const std::vector<const TufaClass *> *classes,
                        const std::vector<VmStaticFields> *staticFields,
                        VmArena *arena) {
  if (depth > 128)
    return {false, int32_t{0}, "call depth exceeded"};
  VmExecutionState local{natives, classes, staticFields,
                         arena,   {},      {true, int32_t{0}, {}},
                         false,   false};
  VmExecutionState &execution =
      arena && arena->activeExecution ? *arena->activeExecution : local;
  const size_t base = execution.frames.size();
  execution.frames.push_back(
      std::make_unique<VmFrame>(cls, tree, std::move(locals), depth));
  VmExecutionState *previous = arena ? arena->activeExecution : nullptr;
  if (arena)
    arena->activeExecution = &execution;
  if (base != 0)
    ++execution.synchronousDepth;
  bool finished = false;
  const auto result =
      drive(execution, base, std::numeric_limits<size_t>::max(), finished);
  if (base != 0)
    --execution.synchronousDepth;
  if (arena)
    arena->activeExecution = previous;
  return result;
}

static VmResult executeTree(VmExecutionState &execution, VmFrame &frame) {
  const auto *cls = frame.cls;
  const auto &tree = *frame.tree;
  auto &locals = frame.locals;
  auto &localTypes = frame.localTypes;
  auto &stack = frame.stack;
  const auto depth = frame.depth;
  const auto *natives = execution.natives;
  const auto *classes = execution.classes;
  const auto *staticFields = execution.staticFields;
  auto *arena = execution.arena;
  if (depth > 128)
    return {false, int32_t{0}, "call depth exceeded"};
  for (size_t &pc = frame.pc; pc < tree.size(); ++pc) {
    const auto &node = tree[pc];
    switch (node.op) {
    case 0x14:
    case 0x15:
    case 0x16: {
      if (!node.wide)
        return {false, int32_t{0}, "invalid branch"};
      bool take = true;
      if (node.op != 0x14) {
        if (stack.empty())
          return {false, int32_t{0}, "branch stack underflow"};
        bool truth;
        const auto &condition = readValue(stack.back());
        if (const auto *value = std::get_if<int32_t>(&condition))
          truth = *value != 0;
        else if (const auto *object = std::get_if<const VmObject *>(&condition))
          truth = *object != nullptr;
        else
          return {false, int32_t{0}, "unsupported branch type"};
        stack.pop_back();
        take = node.op == 0x15 ? !truth : truth;
      }
      if (take) {
        int32_t offset;
        std::memcpy(&offset, &node.operand, sizeof(offset));
        const int64_t target = static_cast<int64_t>(pc) + offset;
        if (target < 0 || static_cast<uint64_t>(target) >= tree.size())
          return {false, int32_t{0}, "branch outside tree"};
        pc = static_cast<size_t>(target) - 1;
      }
      break;
    }
    case 0x04:
    case 0x05: {
      // Short-circuit tests (FUN_00420ff0 cases 4 and 5). The top value is
      // converted in place to its truth value (FUN_00417840: a float becomes 0
      // or 1, a non-null reference becomes 1, an int is kept) and stays on the
      // stack. Op 4 (and) skips the next node when the value is truthy, op 5
      // (or) when it is falsy; the next node is the forward jump over the right
      // operand, which the combining 07 node then joins.
      if (node.wide)
        return {false, int32_t{0}, "invalid short-circuit operation"};
      if (stack.empty())
        return {false, int32_t{0}, "short-circuit stack underflow"};
      VmValue value = readValue(stack.back());
      bool truth;
      if (const auto *integer = std::get_if<int32_t>(&value))
        truth = *integer != 0;
      else if (const auto *number = std::get_if<float>(&value)) {
        truth = !std::isnan(*number) && *number != 0.0f;
        value = VmValue{int32_t{truth ? 1 : 0}};
      } else if (const auto *object = std::get_if<const VmObject *>(&value)) {
        truth = *object != nullptr;
        value = VmValue{int32_t{truth ? 1 : 0}};
      } else
        return {false, int32_t{0}, "unsupported short-circuit operand"};
      stack.back() = value;
      if (node.op == 0x04 ? truth : !truth)
        ++pc;
      break;
    }
    case 0x27: {
      if (!node.wide)
        return {false, int32_t{0}, "invalid integer literal"};
      int32_t value;
      std::memcpy(&value, &node.operand, sizeof(value));
      stack.emplace_back(VmValue{value});
      break;
    }
    case 0x01:
      if (!node.wide || node.operand >= locals.size()) {
        return {false, int32_t{0}, "invalid local access"};
      }
      stack.emplace_back(LocalLocation{&locals, &localTypes, node.operand});
      break;
    case 0x02: {
      if (!node.wide || !cls || node.operand != locals.size() ||
          pc + 1 >= tree.size() || tree[pc + 1].op != 0x06 ||
          !tree[pc + 1].wide || tree[pc + 1].operand >= cls->constants.size())
        return {false, int32_t{0}, "unsupported local declaration"};
      const auto &type = cls->constants[tree[pc + 1].operand];
      if (type.kind != 0 ||
          (type.text != "I" && type.text != "F" && !referenceType(type.text)))
        return {false, int32_t{0}, "unsupported local type"};
      locals.push_back(type.text == "I" ? VmValue{int32_t{0}}
                       : type.text == "F"
                           ? VmValue{0.0f}
                           : VmValue{static_cast<const VmObject *>(nullptr)});
      localTypes.push_back(type.text);
      ++pc;
      if (pc + 1 < tree.size() && tree[pc + 1].op == 0x2c &&
          !tree[pc + 1].wide) {
        stack.emplace_back(LocalLocation{&locals, &localTypes, node.operand});
        ++pc;
      }
      break;
    }
    case 0x17: {
      // Stock FUN_00420ff0 case 0x1017 (switch case test): the top value is
      // the case constant, the value below it the switch subject. When they
      // are equal both are discarded and execution takes the 0x14 branch;
      // otherwise only the constant is discarded and the subject stays for
      // the next case.
      if (!node.wide)
        return {false, int32_t{0},
                "unsupported operation 0x17 without operand"};
      if (stack.size() < 2)
        return {false, int32_t{0}, "switch case stack underflow"};
      const VmValue constant = readValue(stack.back());
      const VmValue subject = readValue(stack[stack.size() - 2]);
      bool equal = false;
      const auto *constantInt = std::get_if<int32_t>(&constant);
      const auto *subjectInt = std::get_if<int32_t>(&subject);
      const auto *constantFloat = std::get_if<float>(&constant);
      const auto *subjectFloat = std::get_if<float>(&subject);
      if (constantInt && subjectInt)
        equal = *constantInt == *subjectInt;
      else if ((constantInt || constantFloat) && (subjectInt || subjectFloat))
        equal =
            (constantInt ? static_cast<float>(*constantInt) : *constantFloat) ==
            (subjectInt ? static_cast<float>(*subjectInt) : *subjectFloat);
      else {
        const auto *constantObject = std::get_if<const VmObject *>(&constant);
        const auto *subjectObject = std::get_if<const VmObject *>(&subject);
        if (!constantObject || !subjectObject)
          return {false, int32_t{0}, "unsupported switch case operand types"};
        equal = *constantObject == *subjectObject;
      }
      stack.pop_back();
      if (equal) {
        stack.pop_back();
        int32_t offset;
        std::memcpy(&offset, &node.operand, sizeof(offset));
        const int64_t target = static_cast<int64_t>(pc) + offset;
        if (target < 0 || static_cast<uint64_t>(target) >= tree.size())
          return {false, int32_t{0}, "branch outside tree"};
        pc = static_cast<size_t>(target) - 1;
      }
      break;
    }
    case 0x18: {
      // Switch fall-through: every case test (wide 0x17) failed and left the
      // subject on the stack in all six stock sites (Brake, Integer, Garage,
      // City); the no-operand 0x18 discards it before the 0x14 jump past the
      // case bodies.
      if (node.wide)
        return {false, int32_t{0}, "unsupported operation 0x18 with operand"};
      if (stack.empty())
        return {false, int32_t{0}, "switch subject stack underflow"};
      stack.pop_back();
      break;
    }
    case 0x11:
    case 0x1b: {
      const TufaClass *fieldClass = cls;
      const TufaNode *fieldNode = &node;
      if (node.op == 0x11) {
        if (node.wide && node.operand == 2 && cls && arena &&
            pc + 2 < tree.size() && tree[pc + 1].op == 0x1b &&
            tree[pc + 2].op == 0x1b && tree[pc + 2].wide &&
            tree[pc + 2].operand < cls->constants.size()) {
          const auto &lengthRef = cls->constants[tree[pc + 2].operand];
          if (lengthRef.kind == 5 && lengthRef.a < cls->constants.size() &&
              lengthRef.b < cls->constants.size()) {
            const auto &owner = cls->constants[lengthRef.a];
            const auto &pair = cls->constants[lengthRef.b];
            if (owner.kind == 4 && owner.a < cls->constants.size() &&
                !cls->constants[owner.a].text.empty() &&
                cls->constants[owner.a].text.front() == '[' && pair.kind == 7 &&
                pair.a < cls->constants.size() &&
                pair.b < cls->constants.size() &&
                cls->constants[pair.a].text == "length" &&
                cls->constants[pair.b].text == "I") {
              const std::vector<TufaNode> fieldTree{tree[pc + 1],
                                                    {0x2b, 0, false, 0}};
              const auto field = execute(cls, fieldTree, locals, depth + 1,
                                         natives, classes, staticFields, arena);
              if (!field.ok)
                return field;
              const auto *value = std::get_if<const VmObject *>(&field.value);
              if (!value || !*value || (*value)->arrayDescriptor.empty() ||
                  !supportedReference(*value, (*value)->arrayDescriptor, arena,
                                      classes) ||
                  (*value)->arrayElements.size() >
                      static_cast<size_t>(INT32_MAX))
                return {false, int32_t{0}, "invalid array length receiver"};
              stack.emplace_back(VmValue{
                  static_cast<int32_t>((*value)->arrayElements.size())});
              pc += 2;
              break;
            }
          }
        }
        // Stock FUN_00420d20 (evalName): the 0x11 operand counts the chain
        // nodes that follow. A chain starts with a local (01) or a field of
        // this (1b); a 19 start is the qualified static form handled below.
        // Each 1b step reads the named field of the previous object value and
        // pushes the field value, as stock FUN_004087e0 does.
        if (node.wide && node.operand >= 1 && node.operand <= 8 && cls &&
            pc + (size_t)node.operand < tree.size() &&
            (!locals.empty() ||
             (node.operand > 1 &&
              (tree[pc + 1].op == 0x1b || tree[pc + 1].op == 0x19))) &&
            (tree[pc + 1].op != 0x19 || node.operand > 2)) {
          VmValue current =
              locals.empty() ? VmValue{std::monostate{}} : locals[0];
          const VmObject *holder = nullptr;
          size_t fieldIndex = 0;
          const TufaClass *staticHolder = nullptr;
          bool lastWasField = false;
          size_t cursor = pc;
          bool failed = false;
          std::string error;
          for (size_t remaining = (size_t)node.operand; remaining;
               --remaining) {
            ++cursor;
            const TufaNode &step = tree[cursor];
            if (step.op == 0x19 && cursor == pc + 1 && remaining >= 2 &&
                tree[cursor + 1].op == 0x1b) {
              const std::vector<TufaNode> fieldTree{
                  {0x11, step.source, true, 2},
                  step,
                  tree[cursor + 1],
                  {0x2b, 0, false, 0}};
              const auto field = execute(cls, fieldTree, locals, depth + 1,
                                         natives, classes, staticFields, arena);
              if (!field.ok)
                return field;
              current = field.value;
              ++cursor;
              --remaining;
              lastWasField = false;
              continue;
            }
            if (step.op == 0x01) {
              if (!step.wide || step.operand >= locals.size()) {
                failed = true;
                error = "evalName local out of range";
                break;
              }
              current = locals[step.operand];
              lastWasField = false;
              continue;
            }
            if (step.op != 0x1b || !step.wide ||
                step.operand >= cls->constants.size()) {
              failed = true;
              error = "evalName step";
              break;
            }
            const auto &ref = cls->constants[step.operand];
            if (ref.kind != 5 || ref.a >= cls->constants.size() ||
                ref.b >= cls->constants.size()) {
              failed = true;
              error = "evalName field reference";
              break;
            }
            const auto &pair = cls->constants[ref.b];
            if (pair.kind != 7 || pair.a >= cls->constants.size() ||
                pair.b >= cls->constants.size()) {
              failed = true;
              error = "evalName name/type";
              break;
            }
            bool firstStatic = false;
            if (cursor == pc + 1 && remaining > 1) {
              for (const auto &list : cls->fields)
                for (const auto &candidate : list)
                  if ((candidate.words[0] & 8) &&
                      candidate.words[1] < cls->constants.size() &&
                      cls->constants[candidate.words[1]].text ==
                          cls->constants[pair.a].text)
                    firstStatic = true;
            }
            if (firstStatic) {
              const std::vector<TufaNode> firstField{step, {0x2b, 0, false, 0}};
              const auto resolved =
                  execute(cls, firstField, locals, depth + 1, natives, classes,
                          staticFields, arena);
              if (!resolved.ok)
                return resolved;
              current = resolved.value;
              lastWasField = false;
              continue;
            }
            const auto *object = std::get_if<const VmObject *>(&current);
            if (!object || !*object) {
              failed = true;
              error =
                  "evalName null receiver for " + cls->constants[pair.a].text;
              break;
            }
            const auto &fieldName = cls->constants[pair.a].text;
            const auto &owner = cls->constants[ref.a];
            if (fieldName == "length" && cls->constants[pair.b].text == "I" &&
                owner.kind == 4 && owner.a < cls->constants.size() &&
                !cls->constants[owner.a].text.empty() &&
                cls->constants[owner.a].text.front() == '[' &&
                !(*object)->arrayDescriptor.empty()) {
              if (!supportedReference(*object, (*object)->arrayDescriptor,
                                      arena, classes) ||
                  (*object)->arrayElements.size() >
                      static_cast<size_t>(INT32_MAX))
                return {false, int32_t{0}, "invalid array length receiver"};
              current = static_cast<int32_t>((*object)->arrayElements.size());
              lastWasField = false;
              continue;
            }
            const TufaClass *declaring = (*object)->runtimeClass;
            const TufaField *resolvedField = nullptr;
            std::vector<const TufaClass *> visited;
            while (declaring && !resolvedField && classes &&
                   std::find(visited.begin(), visited.end(), declaring) ==
                       visited.end()) {
              visited.push_back(declaring);
              for (const auto &list : declaring->fields)
                for (const auto &candidate : list)
                  if (candidate.words[1] < declaring->constants.size() &&
                      candidate.words[2] < declaring->constants.size() &&
                      declaring->constants[candidate.words[1]].text ==
                          fieldName &&
                      declaring->constants[candidate.words[2]].text ==
                          cls->constants[pair.b].text)
                    resolvedField = &candidate;
              if (!resolvedField) {
                const TufaClass *parent = nullptr;
                if (!parentClass(*declaring, *classes, parent))
                  break;
                declaring = parent;
              }
            }
            if (resolvedField && (resolvedField->words[0] & 8)) {
              std::string loadError;
              if (!staticFields ||
                  !resolveClass(declaring->name, classes, staticFields, arena,
                                depth, loadError))
                return {false, int32_t{0},
                        loadError.empty() ? "missing static fields"
                                          : loadError};
              const VmStaticFields *state = nullptr;
              for (const auto &candidate : *staticFields)
                if (candidate.owner == declaring)
                  state = &candidate;
              if (!state)
                return {false, int32_t{0}, "missing static fields"};
              size_t index = state->fields.size();
              for (size_t i = 0; i < state->fields.size(); ++i)
                if (state->fields[i].name == fieldName &&
                    state->fields[i].descriptor ==
                        cls->constants[pair.b].text) {
                  if (index != state->fields.size())
                    return {false, int32_t{0}, "ambiguous static field value"};
                  index = i;
                }
              if (index == state->fields.size())
                return {false, int32_t{0}, "missing static field value"};
              holder = nullptr;
              staticHolder = declaring;
              fieldIndex = index;
              lastWasField = true;
              current = state->fields[index].value;
              continue;
            }
            const VmField *field = nullptr;
            size_t matches = 0;
            size_t matchIndex = 0;
            for (size_t i = 0; i < (*object)->fields.size(); ++i)
              if ((*object)->fields[i].name == fieldName) {
                field = &(*object)->fields[i];
                matchIndex = i;
                ++matches;
              }
            if (matches > 1) {
              failed = true;
              error = "evalName ambiguous field " + fieldName;
              break;
            }
            if (!field) {
              failed = true;
              error = "evalName object has no field " + fieldName;
              break;
            }
            holder = *object;
            staticHolder = nullptr;
            fieldIndex = matchIndex;
            lastWasField = true;
            current = field->value;
          }
          if (failed)
            return {false, int32_t{0},
                    (cls ? cls->name : "<unknown>") + ": node " +
                        std::to_string(pc) + ": " + error};
          if (lastWasField)
            stack.emplace_back(
                FieldLocation{holder, staticHolder ? staticFields : nullptr,
                              staticHolder, fieldIndex});
          else
            stack.emplace_back(current);
          pc += (size_t)node.operand;
          break;
        }
        if (!node.wide || node.operand != 2 || !cls || !classes ||
            pc + 2 >= tree.size() || tree[pc + 1].op != 0x19 ||
            !tree[pc + 1].wide ||
            tree[pc + 1].operand >= cls->constants.size() ||
            tree[pc + 2].op != 0x1b)
          return unsupportedOperation(cls, pc, node);
        const auto &classRef = cls->constants[tree[pc + 1].operand];
        if (classRef.kind != 4 || classRef.a >= cls->constants.size() ||
            cls->constants[classRef.a].kind != 0)
          return {false, int32_t{0}, "invalid qualified field class"};
        std::string loadError;
        fieldClass = resolveClass(cls->constants[classRef.a].text, classes,
                                  staticFields, arena, depth, loadError);
        if (!fieldClass)
          return {false, int32_t{0},
                  !loadError.empty() &&
                          loadError.rfind("class not registered: ", 0) != 0
                      ? loadError
                      : cls->name + ": node " + std::to_string(pc) +
                            ": qualified field class not registered: " +
                            cls->constants[classRef.a].text};
        fieldNode = &tree[pc + 2];
        pc += 2;
      }
      if (!fieldNode->wide || !cls ||
          fieldNode->operand >= cls->constants.size())
        return {false, int32_t{0}, "invalid field access"};
      const auto &ref = cls->constants[fieldNode->operand];
      if (ref.kind != 5 || ref.a >= cls->constants.size() ||
          ref.b >= cls->constants.size())
        return {false, int32_t{0}, "invalid field reference"};
      const auto &owner = cls->constants[ref.a];
      const auto &pair = cls->constants[ref.b];
      if (owner.kind != 4 || owner.a >= cls->constants.size() ||
          cls->constants[owner.a].kind != 0 || pair.kind != 7 ||
          pair.a >= cls->constants.size() || pair.b >= cls->constants.size() ||
          cls->constants[pair.a].kind != 0 ||
          cls->constants[pair.b].kind != 0 ||
          (cls->constants[pair.b].text != "I" &&
           cls->constants[pair.b].text != "F" &&
           !referenceType(cls->constants[pair.b].text)))
        return {false, int32_t{0},
                (cls ? cls->name : "<unknown>") + ": node " +
                    std::to_string(pc) + ": unsupported field reference"};
      const auto &name = cls->constants[pair.a].text;
      const auto &fieldType = cls->constants[pair.b].text;
      if (cls->constants[owner.a].text != fieldClass->name) {
        if (!classes)
          return {false, int32_t{0}, "unsupported field owner"};
        const TufaClass *declaring = nullptr;
        for (const auto *candidate : *classes)
          if (candidate && candidate->name == cls->constants[owner.a].text) {
            if (declaring)
              return {false, int32_t{0}, "ambiguous field owner"};
            declaring = candidate;
          }
        if (!declaring || !constructorAncestor(fieldClass, declaring, *classes))
          return {false, int32_t{0}, "unsupported field ancestry"};
        for (const TufaClass *current = fieldClass; current != declaring;) {
          for (const auto &list : current->fields)
            for (const auto &candidate : list)
              if (candidate.words[1] < current->constants.size() &&
                  current->constants[candidate.words[1]].text == name)
                return {false, int32_t{0}, "inherited field shadowed"};
          const TufaClass *parent = nullptr;
          if (!parentClass(*current, *classes, parent) || !parent)
            return {false, int32_t{0}, "invalid field ancestry"};
          current = parent;
        }
        fieldClass = declaring;
      }
      const TufaField *field = nullptr;
      std::vector<const TufaClass *> searched;
      while (fieldClass && !field) {
        if (searched.size() >= 128 ||
            std::find(searched.begin(), searched.end(), fieldClass) !=
                searched.end())
          return {false, int32_t{0}, "cyclic field ancestry"};
        searched.push_back(fieldClass);
        for (const auto &list : fieldClass->fields)
          for (const auto &candidate : list) {
            const auto &w = candidate.words;
            if (w[1] < fieldClass->constants.size() &&
                w[2] < fieldClass->constants.size() &&
                fieldClass->constants[w[1]].text == name &&
                fieldClass->constants[w[2]].text == fieldType) {
              if (field)
                return {false, int32_t{0}, "ambiguous field"};
              field = &candidate;
            }
          }
        if (!field) {
          const TufaClass *parent = nullptr;
          if (!classes || !parentClass(*fieldClass, *classes, parent))
            return {false, int32_t{0}, "invalid field ancestry"};
          fieldClass = parent;
        }
      }
      if (!field)
        return {false, int32_t{0}, "unsupported field"};
      if (node.op == 0x11 && !(field->words[0] & 8))
        return {false, int32_t{0}, "qualified field is not static"};
      std::vector<VmField> *fields = nullptr;
      const VmObject *objectOwner = nullptr;
      if (field->words[0] & 8) {
        std::string loadError;
        if (staticFields && !hasStaticState(fieldClass, staticFields) &&
            !resolveClass(fieldClass->name, classes, staticFields, arena, depth,
                          loadError) &&
            !loadError.empty())
          return {false, int32_t{0}, loadError};
        if (staticFields)
          for (const auto &state : *staticFields)
            if (state.owner == fieldClass) {
              if (fields)
                return {false, int32_t{0}, "ambiguous static fields"};
              fields = &state.fields;
            }
      } else {
        if (locals.empty())
          return {false, int32_t{0}, "missing field receiver"};
        const auto *receiver = std::get_if<const VmObject *>(&locals[0]);
        if (!receiver || !*receiver ||
            ((*receiver)->runtimeClass != cls &&
             (!classes ||
              !constructorAncestor((*receiver)->runtimeClass, cls, *classes))))
          return {false, int32_t{0}, "missing field receiver"};
        fields = &(*receiver)->fields;
        objectOwner = *receiver;
      }
      if (!fields)
        return {false, int32_t{0}, "missing static fields"};
      size_t index = fields->size();
      for (size_t i = 0; i < fields->size(); ++i)
        if ((*fields)[i].name == name) {
          if (index != fields->size())
            return {false, int32_t{0}, "ambiguous field value"};
          index = i;
        }
      if (index == fields->size())
        return {false, int32_t{0}, "missing or invalid field value"};
      const auto &slot = (*fields)[index];
      if (fieldType == "I" ? !std::holds_alternative<int32_t>(slot.value)
          : fieldType == "F"
              ? !std::holds_alternative<float>(slot.value)
              : slot.descriptor != fieldType ||
                    !std::holds_alternative<const VmObject *>(slot.value) ||
                    !supportedReference(std::get<const VmObject *>(slot.value),
                                        fieldType, arena, classes))
        return {false, int32_t{0}, "missing or invalid field value"};
      stack.emplace_back(
          FieldLocation{objectOwner, objectOwner ? nullptr : staticFields,
                        objectOwner ? nullptr : fieldClass, index});
      break;
    }
    case 0x1c: {
      // Stock `401c` (FUN_00420ff0 case 0x1c, FUN_00408800) pops an object and
      // looks the operand's name up as one of its fields. Only instance fields
      // of an arena object are supported, by a unique name.
      if (!node.wide || !cls || !arena || stack.empty() ||
          node.operand >= cls->constants.size() ||
          cls->constants[node.operand].kind != 0)
        return {false, int32_t{0}, "invalid object field access"};
      const auto *receiver =
          std::get_if<const VmObject *>(&readValue(stack.back()));
      if (!receiver || !*receiver || !(*receiver)->runtimeClass)
        return {false, int32_t{0},
                cls->name + ": node " + std::to_string(pc) +
                    ": null or invalid field receiver"};
      bool owned = false;
      for (const auto &object : arena->objects)
        if (object.get() == *receiver)
          owned = true;
      if (!owned)
        return {false, int32_t{0}, "unowned field receiver"};
      const auto &fieldName = cls->constants[node.operand].text;
      size_t index = (*receiver)->fields.size();
      for (size_t i = 0; i < (*receiver)->fields.size(); ++i)
        if ((*receiver)->fields[i].name == fieldName) {
          if (index != (*receiver)->fields.size())
            return {false, int32_t{0}, "ambiguous object field: " + fieldName};
          index = i;
        }
      if (index == (*receiver)->fields.size())
        return {false, int32_t{0},
                cls->name + ": node " + std::to_string(pc) +
                    ": object has no field " + fieldName};
      stack.back() = FieldLocation{*receiver, nullptr, nullptr, index};
      break;
    }
    case 0x2d:
    case 0x08: {
      if (node.op == 0x2d) {
        if (node.wide || stack.size() < 2)
          return {false, int32_t{0}, "invalid declaration assignment"};
        std::swap(stack[stack.size() - 1], stack[stack.size() - 2]);
      }
      if (node.wide && node.operand == 34)
        break;
      if (node.wide && node.operand == 27) {
        if (stack.empty())
          return {false, int32_t{0}, "discard stack underflow"};
        stack.pop_back();
        break;
      }
      if (node.wide && node.operand >= 23 && node.operand <= 26) {
        if (stack.empty())
          return {false, int32_t{0}, "increment stack underflow"};
        if (const auto error = incrementBoxed(
                stack.back(), node.operand == 24 || node.operand == 26))
          return {false, int32_t{0}, *error};
        stack.pop_back();
        break;
      }
      if (node.op == 0x08 &&
          (!node.wide ||
           (node.operand != 35 && node.operand != 36 && node.operand != 37 &&
            node.operand != 39 && node.operand != 40 && node.operand != 44 &&
            node.operand != 45 && node.operand != 46)))
        return unsupportedOperation(cls, pc, node);
    assignment:
      if (stack.size() < 2)
        return {false, int32_t{0}, "assignment stack underflow"};
      const auto *target = std::get_if<FieldLocation>(&stack.back());
      const auto *local = std::get_if<LocalLocation>(&stack.back());
      const auto *array = std::get_if<ArrayLocation>(&stack.back());
      VmValue source = readValue(stack[stack.size() - 2]);
      if (!target && !local && !array)
        return {false, int32_t{0},
                (cls ? cls->name : "<unknown>") + ": node " +
                    std::to_string(pc) +
                    ": unsupported assignment target or value"};
      if (node.op == 0x08 && node.operand >= 44 && node.operand <= 46) {
        const VmValue current = readValue(stack.back());
        const auto *left = std::get_if<int32_t>(&current);
        const auto *right = std::get_if<int32_t>(&source);
        if (!left || !right)
          return {false, int32_t{0}, "unsupported compound bitwise operation"};
        source = node.operand == 44   ? (*left & *right)
                 : node.operand == 45 ? (*left ^ *right)
                                      : (*left | *right);
      }
      if (node.op == 0x08 && (node.operand == 39 || node.operand == 40)) {
        const VmValue current = readValue(stack.back());
        if (const auto *left = std::get_if<float>(&current)) {
          float right;
          if (!numericFloat(source, right))
            return {false, int32_t{0}, "unsupported compound addition"};
          source = node.operand == 39 ? *left + right : *left - right;
        } else if (const auto *left = std::get_if<int32_t>(&current)) {
          const auto *right = std::get_if<int32_t>(&source);
          if (!right)
            return {false, int32_t{0}, "unsupported compound addition"};
          const uint32_t bits =
              node.operand == 39
                  ? static_cast<uint32_t>(*left) + static_cast<uint32_t>(*right)
                  : static_cast<uint32_t>(*left) -
                        static_cast<uint32_t>(*right);
          int32_t sum;
          std::memcpy(&sum, &bits, sizeof(sum));
          source = sum;
        } else if (node.operand == 39 &&
                   std::holds_alternative<const VmObject *>(current)) {
          // Stock FUN_0041b4d0 string concatenation applies to `+=` too:
          // a null reference contributes nothing.
          std::string left, right, concatError;
          if (!concatOperandBytes(current, left, concatError, arena, classes) ||
              !concatOperandBytes(source, right, concatError, arena, classes))
            return {false, int32_t{0},
                    concatError.empty() ? "unsupported string addition"
                                        : concatError};
          const auto result =
              internString(left + right, classes, staticFields, arena);
          if (!result.ok)
            return result;
          source = result.value;
        } else
          return {false, int32_t{0}, "unsupported compound addition"};
      }
      if (node.op == 0x08 && node.operand == 37) {
        const VmValue current = readValue(stack.back());
        if (std::holds_alternative<float>(current) ||
            std::holds_alternative<float>(source)) {
          float left, right;
          if (!numericFloat(current, left) || !numericFloat(source, right) ||
              right == 0.0f)
            return {false, int32_t{0}, "unsupported compound division"};
          const double quotient =
              static_cast<double>(left) / static_cast<double>(right);
          const float rounded = static_cast<float>(quotient);
          if (!std::isfinite(rounded) ||
              (rounded != 0.0f && !std::isnormal(rounded)) ||
              (rounded == 0.0f && quotient != 0.0))
            return {false, int32_t{0}, "unsupported compound division"};
          source = rounded;
        } else {
          const auto *left = std::get_if<int32_t>(&current);
          const auto *right = std::get_if<int32_t>(&source);
          if (!left || !right || *right == 0 ||
              (*left == INT32_MIN && *right == -1))
            return {false, int32_t{0}, "unsupported compound division"};
          source = *left / *right;
        }
      }
      if (node.op == 0x08 && node.operand == 36) {
        VmValue product;
        if (!multiply(readValue(stack.back()), source, product))
          return {false, int32_t{0}, "unsupported compound multiplication"};
        source = product;
      }
      if (array) {
        if (array->array->arrayDescriptor == "[I") {
          if (std::holds_alternative<float>(source) &&
              !floatToInt(source, source))
            return {false, int32_t{0},
                    "unsupported float to integer conversion"};
          if (!std::holds_alternative<int32_t>(source))
            return {false, int32_t{0}, "unsupported array assignment"};
        } else if (array->array->arrayDescriptor == "[F") {
          if (std::holds_alternative<int32_t>(source))
            source = VmValue{static_cast<float>(std::get<int32_t>(source))};
          if (!std::holds_alternative<float>(source))
            return {false, int32_t{0}, "unsupported array assignment"};
        } else {
          const auto *object = std::get_if<const VmObject *>(&source);
          if (!object || !supportedReference(
                             *object, array->array->arrayDescriptor.substr(1),
                             arena, classes))
            return {false, int32_t{0}, "unsupported array assignment"};
        }
        array->array->arrayElements[array->index] = source;
        stack.pop_back();
        stack.pop_back();
        if (node.op == 0x07)
          stack.emplace_back(source);
        break;
      }
      if (local) {
        const auto &type = (*local->types)[local->index];
        if (type == "I" && std::holds_alternative<float>(source) &&
            !floatToInt(source, source))
          return {false, int32_t{0}, "unsupported float to integer conversion"};
        if (type == "F" && std::holds_alternative<int32_t>(source))
          source = VmValue{static_cast<float>(std::get<int32_t>(source))};
        const auto *object = std::get_if<const VmObject *>(&source);
        if (!((type == "I" && std::holds_alternative<int32_t>(source)) ||
              (type == "F" && std::holds_alternative<float>(source)) ||
              (object && supportedReference(*object, type, arena, classes))))
          return {false, int32_t{0},
                  "unsupported local assignment at node " + std::to_string(pc) +
                      " type=" + type +
                      " value=" + std::to_string(source.index())};
        (*local->locals)[local->index] = source;
        stack.pop_back();
        stack.pop_back();
        if (node.op == 0x07)
          stack.emplace_back(source);
        break;
      }
      auto &slot = locationFields(*target)[target->index];
      if (slot.descriptor == "F" && std::holds_alternative<int32_t>(source))
        source = static_cast<float>(std::get<int32_t>(source));
      if (std::holds_alternative<int32_t>(slot.value) &&
          std::holds_alternative<float>(source) && !floatToInt(source, source))
        return {false, int32_t{0}, "unsupported float to integer conversion"};
      const auto *object = std::get_if<const VmObject *>(&source);
      if (!(std::holds_alternative<int32_t>(slot.value) &&
            std::holds_alternative<int32_t>(source)) &&
          !(slot.descriptor == "F" &&
            std::holds_alternative<float>(slot.value) &&
            std::holds_alternative<float>(source)) &&
          !(object &&
            supportedReference(*object, slot.descriptor, arena, classes)))
        return {false, int32_t{0},
                (cls ? cls->name : "<unknown>") + ": node " +
                    std::to_string(pc) +
                    ": unsupported assignment target or value"};
      slot.value = source;
      stack.pop_back();
      stack.pop_back();
      if (node.op == 0x07)
        stack.emplace_back(source);
      break;
    }
    case 0x20: {
      if (node.wide || stack.size() < 2 || !arena || !classes)
        return {false, int32_t{0}, "invalid array access"};
      const auto *array =
          std::get_if<const VmObject *>(&readValue(stack.back()));
      const auto *index =
          std::get_if<int32_t>(&readValue(stack[stack.size() - 2]));
      if (!array || !*array || !index || *index < 0 ||
          (*array)->arrayDescriptor.empty() ||
          !supportedReference(*array, (*array)->arrayDescriptor, arena,
                              classes) ||
          static_cast<size_t>(*index) >= (*array)->arrayElements.size())
        return {false, int32_t{0}, "invalid array index or receiver"};
      const ArrayLocation location{*array, static_cast<size_t>(*index)};
      stack.pop_back();
      stack.pop_back();
      stack.emplace_back(location);
      break;
    }
    case 0x21:
    case 0x22:
    case 0x23: {
      if (node.wide || !cls || !classes || !arena || stack.empty())
        return {false, int32_t{0}, "invalid constructor call"};
      const auto *count = std::get_if<int32_t>(&readValue(stack.back()));
      if (!count || *count < 0 || *count > 16)
        return {false, int32_t{0}, "unsupported constructor arguments"};
      const size_t argumentCount = static_cast<size_t>(*count);
      const VmObject *receiver = nullptr;
      const TufaClass *target = nullptr;
      if (node.op == 0x21) {
        if (stack.size() < argumentCount + 2)
          return {false, int32_t{0}, "missing constructor receiver"};
        const auto *value = std::get_if<const VmObject *>(
            &readValue(stack[stack.size() - argumentCount - 2]));
        if (!value || !*value)
          return {false, int32_t{0}, "missing constructor receiver"};
        receiver = *value;
        bool owned = false;
        for (const auto &object : arena->objects)
          if (object.get() == receiver)
            owned = true;
        if (!owned)
          return {false, int32_t{0}, "unsupported constructor receiver"};
        target = receiver->runtimeClass;
      } else {
        if (locals.empty())
          return {false, int32_t{0}, "missing constructor receiver"};
        const auto *value = std::get_if<const VmObject *>(&locals[0]);
        if (!value || !*value)
          return {false, int32_t{0}, "missing constructor receiver"};
        receiver = *value;
        bool owned = false;
        for (const auto &object : arena->objects)
          if (object.get() == receiver)
            owned = true;
        if (!owned)
          return {false, int32_t{0}, "unsupported constructor receiver"};
        if (!receiver->runtimeClass ||
            !constructorAncestor(receiver->runtimeClass, cls, *classes))
          return {false, int32_t{0}, "unsupported constructor ancestry"};
        if (node.op == 0x22)
          target = cls;
        else {
          if (!parentClass(*cls, *classes, target))
            return {false, int32_t{0}, "invalid constructor parent"};
          if (!target)
            break;
        }
      }
      bool owned = false;
      for (const auto &object : arena->objects)
        if (object.get() == receiver)
          owned = true;
      if (!owned || !target)
        return {false, int32_t{0}, "unsupported constructor receiver"};
      bool loaded = false;
      for (const auto *candidate : *classes)
        if (candidate == target)
          loaded = true;
      if (!loaded || (node.op == 0x23 && target == cls))
        return {false, int32_t{0}, "unsupported constructor class"};
      if (stack.size() < argumentCount + 1)
        return {false, int32_t{0}, "missing constructor arguments"};
      const TufaMethod *constructor = nullptr;
      std::vector<std::string> constructorParameters;
      std::vector<const TufaClass *> searched;
      while (target && !constructor) {
        if (searched.size() >= 128 ||
            std::find(searched.begin(), searched.end(), target) !=
                searched.end())
          return {false, int32_t{0}, "cyclic constructor ancestry"};
        searched.push_back(target);
        unsigned bestScore = 0;
        bool tied = false;
        for (const auto &list : target->methods)
          for (const auto &method : list) {
            const auto &w = method.words;
            if (w[1] >= target->constants.size() ||
                w[2] >= target->constants.size() ||
                target->constants[w[1]].text != "<init>")
              continue;
            std::vector<std::string> parameters;
            std::string resultType;
            if (!nativeSignature(target->constants[w[2]].text + "V", parameters,
                                 resultType) ||
                parameters.size() != argumentCount)
              continue;
            bool fits = true;
            unsigned score = 0;
            for (size_t i = 0; i < argumentCount && fits; ++i) {
              const auto &value =
                  readValue(stack[stack.size() - argumentCount - 1 + i]);
              const auto &type = parameters[i];
              if (type == "I") {
                fits = std::holds_alternative<int32_t>(value);
                if (fits)
                  ++score;
              } else if (type == "F") {
                if (std::holds_alternative<float>(value))
                  ++score;
                else {
                  float converted;
                  fits = numericFloat(value, converted);
                }
              } else {
                const auto *object = std::get_if<const VmObject *>(&value);
                fits =
                    object && supportedReference(*object, type, arena, classes);
                if (fits && *object && (*object)->runtimeClass &&
                    type == "L" + (*object)->runtimeClass->name + ";")
                  ++score;
              }
            }
            if (!fits)
              continue;
            if (!constructor || score > bestScore) {
              constructor = &method;
              constructorParameters = std::move(parameters);
              bestScore = score;
              tied = false;
            } else if (score == bestScore)
              tied = true;
          }
        if (tied)
          return {false, int32_t{0}, "ambiguous constructor"};
        if (!constructor) {
          const TufaClass *parent = nullptr;
          if (!parentClass(*target, *classes, parent))
            return {false, int32_t{0}, "invalid constructor parent"};
          target = parent;
        }
      }
      if (!constructor || (constructor->words[0] & 8))
        return {false, int32_t{0}, "unsupported constructor"};
      if (constructor->words[0] & 0x40) {
        const unsigned outerDepth = arena->nativeDepth;
        arena->nativeDepth = depth;
        const auto called =
            callNative(stack, *target, "<init>",
                       target->constants[constructor->words[2]].text + "V",
                       false, receiver, natives, classes, arena);
        arena->nativeDepth = outerDepth;
        if (!called.ok)
          return called;
        stack.pop_back();
        break;
      }
      if (constructor->words[3] >= target->trees.size())
        return {false, int32_t{0}, "unsupported constructor"};
      stack.pop_back();
      std::vector<VmValue> calleeLocals{receiver};
      for (size_t i = 0; i < argumentCount; ++i) {
        const auto &argument = readValue(stack[stack.size() - 1 - i]);
        if (constructorParameters[argumentCount - 1 - i] == "F" &&
            std::holds_alternative<int32_t>(argument)) {
          float converted;
          if (!numericFloat(argument, converted))
            return {false, int32_t{0}, "unsupported constructor promotion"};
          calleeLocals.emplace_back(converted);
        } else
          calleeLocals.push_back(argument);
      }
      for (size_t i = 0; i < argumentCount; ++i)
        stack.pop_back();
      const auto &body = target->trees[constructor->words[3]];
      if (body.empty()) {
        if (target->name != "java.lang.Object" || argumentCount != 0)
          return {false, int32_t{0}, "unsupported empty constructor"};
      } else {
        frame.waiting = VmFrame::ReturnKind::Constructor;
        execution.frames.push_back(std::make_unique<VmFrame>(
            target, body, std::move(calleeLocals), depth + 1));
        auto &calleeTypes = execution.frames.back()->localTypes;
        for (size_t i = 0; i < argumentCount; ++i)
          calleeTypes[1 + i] = constructorParameters[argumentCount - 1 - i];
      }
      break;
    }
    case 0x07: {
      if (node.wide && node.operand == 35)
        goto assignment;
      if (node.wide && node.operand == 33 && cls && !stack.empty() &&
          pc + 1 < tree.size() && tree[pc + 1].op == 0x06 &&
          tree[pc + 1].wide && tree[pc + 1].operand < cls->constants.size() &&
          (cls->constants[tree[pc + 1].operand].text == "I" ||
           cls->constants[tree[pc + 1].operand].text == "F")) {
        // Primitive cast: stock 0x1021 arm of FUN_00420ff0 with a 0x1006
        // descriptor, then the assignment conversion FUN_00417da0 on a copy.
        // Same type: unchanged. I from F: __ftol truncation. F from I:
        // FILD/FSTP. Anything else (an I target from a non-float, or a
        // reference) is "cannot cast" in stock; the port refuses it.
        const bool toInt = cls->constants[tree[pc + 1].operand].text == "I";
        const VmValue value = readValue(stack.back());
        VmValue converted;
        if (toInt && std::holds_alternative<int32_t>(value))
          converted = value;
        else if (!toInt && std::holds_alternative<float>(value))
          converted = value;
        else if (toInt) {
          if (!floatToInt(value, converted))
            return {false, int32_t{0},
                    "unsupported cast to int operand or range"};
        } else {
          const auto *integer = std::get_if<int32_t>(&value);
          if (!integer || std::fegetround() != FE_TONEAREST)
            return {false, int32_t{0}, "unsupported cast to float operand"};
          converted = static_cast<float>(*integer);
        }
        stack.back() = converted;
        ++pc;
        break;
      }
      if (node.wide && node.operand == 33) {
        if (!cls || !arena || !classes || stack.empty() ||
            pc + 1 >= tree.size() || tree[pc + 1].op != 0x19 ||
            !tree[pc + 1].wide || tree[pc + 1].operand >= cls->constants.size())
          return {false, int32_t{0},
                  "unsupported cast structure in " +
                      std::string(cls ? cls->name : "<tree>") + " node " +
                      std::to_string(pc) + " (next op " +
                      (pc + 1 < tree.size() ? std::to_string(tree[pc + 1].op)
                                            : std::string("none")) +
                      ")"};
        const auto &type = cls->constants[tree[pc + 1].operand];
        if (type.kind != 4 || type.a >= cls->constants.size() ||
            cls->constants[type.a].kind != 0)
          return {false, int32_t{0}, "invalid cast class"};
        const auto &name = cls->constants[type.a].text;
        std::string error;
        if (!resolveClass(name, classes, staticFields, arena, depth, error))
          return {false, int32_t{0}, "cast class unavailable: " + error};
        const VmValue value = readValue(stack.back());
        const auto *object = std::get_if<const VmObject *>(&value);
        if (!object ||
            !referenceAssignable(*object, "L" + name + ";", arena, classes))
          return {false, int32_t{0}, "invalid reference cast"};
        stack.back() = value;
        ++pc;
        break;
      }
      if (node.wide && node.operand == 32) {
        if (!cls || !arena || !classes || stack.empty() ||
            pc + 1 >= tree.size() || tree[pc + 1].op != 0x06 ||
            !tree[pc + 1].wide || tree[pc + 1].operand >= cls->constants.size())
          return {false, int32_t{0}, "invalid instanceof operation"};
        const auto &type = cls->constants[tree[pc + 1].operand];
        const auto *object =
            std::get_if<const VmObject *>(&readValue(stack.back()));
        if (type.kind != 0 || !referenceType(type.text) || !object)
          return {false, int32_t{0}, "invalid instanceof operands"};
        bool matches = false;
        if (*object) {
          if (type.text.front() == 'L') {
            std::string error;
            const std::string name = type.text.substr(1, type.text.size() - 2);
            if (!resolveClass(name, classes, staticFields, arena, depth, error))
              return {false, int32_t{0},
                      "instanceof class unavailable: " + error};
          }
          matches = referenceAssignable(*object, type.text, arena, classes);
        }
        stack.back() = VmValue{int32_t{matches ? 1 : 0}};
        ++pc;
        break;
      }
      if (node.wide && node.operand == 31) {
        if (!cls || !classes || !staticFields || !arena || stack.empty() ||
            pc + 1 >= tree.size() || tree[pc + 1].op != 0x06 ||
            !tree[pc + 1].wide || tree[pc + 1].operand >= cls->constants.size())
          return {false, int32_t{0}, "unsupported array allocation"};
        const auto &type = cls->constants[tree[pc + 1].operand];
        const auto *length = std::get_if<int32_t>(&readValue(stack.back()));
        if (type.kind != 0 ||
            (type.text != "[I" && type.text != "[F" &&
             (type.text.size() < 4 || type.text.substr(0, 2) != "[L" ||
              !referenceType(type.text))) ||
            !length || *length < 0 ||
            static_cast<size_t>(*length) > arena->arrayElementLimit ||
            arena->objects.size() >= arena->limit)
          return {false, int32_t{0}, "unsupported array type or length"};
        if (type.text.size() > 2 && type.text[1] == 'L') {
          const std::string elementName =
              type.text.substr(2, type.text.size() - 3);
          std::string loadError;
          const TufaClass *element = resolveClass(
              elementName, classes, staticFields, arena, depth, loadError);
          if (!element)
            return {false, int32_t{0},
                    loadError.empty() ||
                            loadError.rfind("class not registered: ", 0) == 0
                        ? "array element class not registered"
                        : loadError};
          size_t initialized = 0;
          for (const auto &state : *staticFields)
            if (state.owner == element)
              ++initialized;
          if (initialized != 1)
            return {false, int32_t{0},
                    "array element class initialization unavailable"};
        }
        auto array = std::make_unique<VmObject>();
        array->arrayDescriptor = type.text;
        array->arrayElements.assign(
            static_cast<size_t>(*length),
            type.text == "[I" ? VmValue{int32_t{0}}
            : type.text == "[F"
                ? VmValue{0.0f}
                : VmValue{static_cast<const VmObject *>(nullptr)});
        const auto *value = array.get();
        arena->objects.push_back(std::move(array));
        stack.back() = VmValue{value};
        ++pc;
        break;
      }
      if (node.wide && node.operand == 27)
        break;
      if (node.wide && (node.operand == 13 || node.operand == 14)) {
        // Stock's 0x1007 switch: case 0xd (13) calls FUN_0041aff0 (>>) and
        // case 0xe (14) calls FUN_0041b0e0 (<<). Both require integral
        // operands and mask the count with 0x1f; the right shift is
        // arithmetic because stock shifts a signed int.
        if (stack.size() < 2)
          return {false, int32_t{0}, "shift stack underflow"};
        const auto *left =
            std::get_if<int32_t>(&readValue(stack[stack.size() - 2]));
        const auto *right = std::get_if<int32_t>(&readValue(stack.back()));
        if (!left || !right)
          return {false, int32_t{0}, "non-integral operands >> / <<"};
        const int32_t shift = *right & 0x1f;
        const int32_t result =
            node.operand == 13
                ? (*left >> shift)
                : static_cast<int32_t>(static_cast<uint32_t>(*left) << shift);
        stack.pop_back();
        stack.back() = VmValue{result};
        break;
      }
      if (node.wide && node.operand >= 23 && node.operand <= 26) {
        if (stack.empty())
          return {false, int32_t{0}, "increment stack underflow"};
        const VmValue previous = readValue(stack.back());
        if (const auto error = incrementBoxed(
                stack.back(), node.operand == 24 || node.operand == 26))
          return {false, int32_t{0}, *error};
        const VmValue result =
            node.operand <= 24 ? previous : readValue(stack.back());
        stack.back() = result;
        break;
      }
      if (node.wide && node.operand == 20) {
        if (stack.empty())
          return {false, int32_t{0}, "bitwise not stack underflow"};
        const auto *value = std::get_if<int32_t>(&readValue(stack.back()));
        if (!value)
          return {false, int32_t{0}, "unsupported bitwise not operand"};
        stack.back() = VmValue{static_cast<int32_t>(~*value)};
        break;
      }
      if (node.wide && node.operand == 21) {
        if (stack.empty())
          return {false, int32_t{0}, "logical not stack underflow"};
        // FUN_0041bdb0 first applies 00417840 boolean conversion.
        VmValue value = readValue(stack.back());
        if (const auto *integer = std::get_if<int32_t>(&value))
          value = int32_t{*integer == 0 ? 1 : 0};
        else if (const auto *number = std::get_if<float>(&value))
          value = int32_t{std::isnan(*number) || *number == 0.0f ? 1 : 0};
        else if (const auto *object = std::get_if<const VmObject *>(&value))
          value = int32_t{*object ? 0 : 1};
        else
          return {false, int32_t{0}, "unsupported logical not operand"};
        stack.back() = value;
        break;
      }
      if (node.wide && node.operand == 17) {
        // Stock FUN_0041bbf0: after the 00417570 operand coercion both payloads
        // must be type I ("non-integral operands" otherwise); the left box
        // receives left % right. Division by zero is logged and then executed
        // by x87 idiv, which traps, so the port refuses it, and INT_MIN % -1
        // which also traps.
        if (stack.size() < 2)
          return {false, int32_t{0}, "modulo stack underflow"};
        const auto &left = readValue(stack[stack.size() - 2]);
        const auto &right = readValue(stack.back());
        const auto *lhs = std::get_if<int32_t>(&left);
        const auto *rhs = std::get_if<int32_t>(&right);
        if (!lhs || !rhs || *rhs == 0 || (*lhs == INT32_MIN && *rhs == -1))
          return {false, int32_t{0}, "unsupported integer modulo operands"};
        const int32_t result = *lhs % *rhs;
        stack.pop_back();
        stack.back() = VmValue{result};
        break;
      }
      if (node.wide && node.operand == 18) {
        if (stack.size() < 2)
          return {false, int32_t{0}, "division stack underflow"};
        const auto &left = readValue(stack[stack.size() - 2]);
        const auto &right = readValue(stack.back());
        VmValue result;
        if (std::holds_alternative<float>(left) ||
            std::holds_alternative<float>(right)) {
          float lhs, rhs;
          if (!numericFloat(left, lhs) || !numericFloat(right, rhs) ||
              rhs == 0.0f)
            return {false, int32_t{0}, "unsupported float division operands"};
          const double quotient =
              static_cast<double>(lhs) / static_cast<double>(rhs);
          const float rounded = static_cast<float>(quotient);
          if (!std::isfinite(rounded) ||
              (rounded != 0.0f && !std::isnormal(rounded)) ||
              (rounded == 0.0f && quotient != 0.0))
            return {false, int32_t{0}, "unsupported float division result"};
          result = rounded;
        } else {
          const auto *lhs = std::get_if<int32_t>(&left);
          const auto *rhs = std::get_if<int32_t>(&right);
          if (!lhs || !rhs || *rhs == 0 || (*lhs == INT32_MIN && *rhs == -1))
            return {false, int32_t{0}, "unsupported integer division operands"};
          result = int32_t{*lhs / *rhs};
        }
        stack.pop_back();
        stack.back() = result;
        break;
      }
      if (node.wide && node.operand == 19) {
        if (stack.size() < 2)
          return {false, int32_t{0}, "multiplication stack underflow"};
        VmValue result;
        if (!multiply(readValue(stack[stack.size() - 2]),
                      readValue(stack.back()), result))
          return {false, int32_t{0},
                  "unsupported multiplication operands or result"};
        stack.pop_back();
        stack.back() = VmValue{result};
        break;
      }
      if (node.wide && node.operand == 30) {
        if (!cls || !classes || !staticFields || !arena ||
            arena->objects.size() >= arena->limit || pc + 1 >= tree.size() ||
            tree[pc + 1].op != 0x06 || !tree[pc + 1].wide ||
            tree[pc + 1].operand >= cls->constants.size())
          return {false, int32_t{0}, "unsupported object allocation"};
        const auto &type = cls->constants[tree[pc + 1].operand];
        if (type.kind != 0 || type.text.size() < 3 ||
            type.text.front() != 'L' || type.text.back() != ';')
          return {false, int32_t{0}, "unsupported allocation type"};
        const std::string name = type.text.substr(1, type.text.size() - 2);
        std::string loadError;
        const TufaClass *target =
            resolveClass(name, classes, staticFields, arena, depth, loadError);
        if (!target)
          return {false, int32_t{0},
                  !loadError.empty() &&
                          loadError.rfind("class not registered: ", 0) != 0
                      ? loadError
                      : cls->name + ": node " + std::to_string(pc) +
                            ": allocation class not registered: " + name};
        std::vector<const TufaClass *> ancestry;
        for (const TufaClass *current = target; current;) {
          if (ancestry.size() >= 128 || !current->hasClassHeader)
            return {false, int32_t{0}, "unsupported allocation ancestry"};
          for (const auto *ancestor : ancestry)
            if (ancestor == current)
              return {false, int32_t{0}, "cyclic allocation ancestry"};
          ancestry.push_back(current);
          size_t states = 0;
          for (const auto &state : *staticFields)
            if (state.owner == current)
              ++states;
          if (states != 1)
            return {false, int32_t{0}, "class initialization unavailable"};
          for (uint32_t interfaceIndex : current->interfaces) {
            if (interfaceIndex >= current->constants.size())
              return {false, int32_t{0}, "invalid allocation interface"};
            const auto &ref = current->constants[interfaceIndex];
            if (ref.kind != 4 || ref.a >= current->constants.size() ||
                current->constants[ref.a].kind != 0)
              return {false, int32_t{0}, "invalid allocation interface"};
            const TufaClass *iface = nullptr;
            for (const auto *candidate : *classes)
              if (candidate->name == current->constants[ref.a].text)
                iface = candidate;
            if (!iface || !iface->hasClassHeader || iface->classHeader[1] != 1)
              return {false, int32_t{0}, "invalid allocation interface"};
            size_t initialized = 0;
            for (const auto &state : *staticFields)
              if (state.owner == iface)
                ++initialized;
            if (initialized != 1)
              return {false, int32_t{0},
                      "interface initialization unavailable"};
          }
          const uint32_t parentIndex = current->classHeader[3];
          if (parentIndex == UINT32_MAX)
            break;
          if (parentIndex >= current->constants.size())
            return {false, int32_t{0}, "invalid parent class"};
          const auto &parent = current->constants[parentIndex];
          if (parent.kind != 4 || parent.a >= current->constants.size() ||
              current->constants[parent.a].kind != 0)
            return {false, int32_t{0}, "invalid parent class"};
          current = nullptr;
          for (const auto *candidate : *classes)
            if (candidate->name == ancestry.back()->constants[parent.a].text)
              current = candidate;
          if (!current)
            return {false, int32_t{0}, "parent class not registered"};
        }
        auto object = std::make_unique<VmObject>();
        object->runtimeClass = target;
        VmObject *const self = object.get();
        bool inArena = false;
        for (auto it = ancestry.rbegin(); it != ancestry.rend(); ++it)
          for (const auto &list : (*it)->fields)
            for (const auto &field : list) {
              const auto &w = field.words;
              if (w[0] & 8)
                continue;
              if (w[1] >= (*it)->constants.size() ||
                  w[2] >= (*it)->constants.size() ||
                  (*it)->constants[w[1]].kind != 0 ||
                  (*it)->constants[w[2]].kind != 0 ||
                  ((*it)->constants[w[2]].text != "I" &&
                   (*it)->constants[w[2]].text != "F" &&
                   !referenceType((*it)->constants[w[2]].text)))
                return {false, int32_t{0}, "unsupported instance field"};
              const auto &fieldName = (*it)->constants[w[1]].text;
              for (const auto &prior : self->fields)
                if (prior.name == fieldName)
                  return {false, int32_t{0}, "duplicate instance field"};
              const auto &fieldType = (*it)->constants[w[2]].text;
              VmValue value =
                  fieldType == "F" ? VmValue{0.0f}
                  : fieldType == "I"
                      ? VmValue{int32_t{0}}
                      : VmValue{static_cast<const VmObject *>(nullptr)};
              const size_t slot = self->fields.size();
              // Stock defines the field first, then runs its initializer tree
              // in a frame of the declaring class with the new instance as
              // receiver (`<instance_field_init>`, FUN_00407da0), then assigns
              // the result.
              self->fields.push_back({fieldName, value, fieldType});
              if (w[3] == UINT32_MAX)
                continue;
              if (w[3] >= (*it)->trees.size())
                return {false, int32_t{0}, "invalid instance initializer"};
              const auto &tree = (*it)->trees[w[3]];
              if (literalInitializer(tree, fieldType, value)) {
                self->fields[slot].value = value;
                continue;
              }
              // Code can name the object, so it must be in the arena before it
              // runs. A failure leaves it there.
              if (!inArena) {
                inArena = true;
                arena->objects.push_back(std::move(object));
              }
              const std::string label = (*it)->name + "." + fieldName;
              // A failed initializer takes the half-built object back out when
              // nothing was allocated after it.
              const auto abandon = [&](std::string message) -> VmResult {
                if (!arena->objects.empty() &&
                    arena->objects.back().get() == self)
                  arena->objects.pop_back();
                return {false, int32_t{0}, std::move(message)};
              };
              const auto ran = execute(
                  *it, tree, {VmValue{static_cast<const VmObject *>(self)}},
                  depth + 1, natives, classes, staticFields, arena);
              if (!ran.ok)
                return abandon(label + ": " + ran.error);
              if (ran.voidReturn)
                return abandon(label + ": initializer left no value");
              value = ran.value;
              if (fieldType == "I" && std::holds_alternative<float>(value) &&
                  !floatToInt(value, value))
                return abandon(label +
                               ": unsupported float to integer conversion");
              if (fieldType == "F" && std::holds_alternative<int32_t>(value)) {
                float converted;
                if (!numericFloat(value, converted) ||
                    static_cast<double>(converted) !=
                        static_cast<double>(std::get<int32_t>(value)))
                  return abandon(label +
                                 ": unsupported integer to float conversion");
                value = converted;
              }
              const auto *initialized = std::get_if<const VmObject *>(&value);
              const bool fits =
                  fieldType == "I" ? std::holds_alternative<int32_t>(value)
                  : fieldType == "F"
                      ? std::holds_alternative<float>(value)
                      : initialized &&
                            supportedReference(*initialized, fieldType, arena,
                                               classes);
              if (!fits)
                return abandon(label + ": initializer value does not fit " +
                               fieldType);
              self->fields[slot].value = value;
            }
        if (!inArena)
          arena->objects.push_back(std::move(object));
        const VmObject *value = self;
        stack.emplace_back(VmValue{value});
        ++pc;
        break;
      }
      if (node.wide &&
          (node.operand == 1 || node.operand == 2 || node.operand == 3 ||
           node.operand == 4 || node.operand == 5)) {
        if (stack.size() < 2)
          return {false, int32_t{0}, "bitwise stack underflow"};
        const auto &leftValue = readValue(stack[stack.size() - 2]);
        const auto &rightValue = readValue(stack.back());
        int32_t result = 0;
        if (node.operand == 1 || node.operand == 2) {
          const auto boolean = [](const VmValue &value) -> std::optional<bool> {
            if (const auto *v = std::get_if<int32_t>(&value))
              return *v != 0;
            if (const auto *v = std::get_if<float>(&value))
              return !std::isnan(*v) && *v != 0.0f;
            if (const auto *v = std::get_if<const VmObject *>(&value))
              return *v != nullptr;
            return std::nullopt;
          };
          const auto left = boolean(leftValue), right = boolean(rightValue);
          if (!left || !right)
            return {false, int32_t{0}, "unsupported logical operand types"};
          result = node.operand == 1 ? (*left || *right) : (*left && *right);
        } else {
          const auto *left = std::get_if<int32_t>(&leftValue);
          const auto *right = std::get_if<int32_t>(&rightValue);
          if (!left || !right)
            return {false, int32_t{0}, "unsupported bitwise operand types"};
          result = node.operand == 3   ? (*left | *right)
                   : node.operand == 4 ? (*left ^ *right)
                                       : (*left & *right);
        }
        stack.pop_back();
        stack.back() = VmValue{result};
        break;
      }
      if (node.wide && node.operand >= 8 && node.operand <= 11) {
        if (stack.size() < 2)
          return {false, int32_t{0}, "comparison stack underflow"};
        const auto &left = readValue(stack[stack.size() - 2]);
        const auto &right = readValue(stack.back());
        const auto compare = [&](auto lhs, auto rhs) {
          switch (node.operand) {
          case 8:
            return lhs >= rhs;
          case 9:
            return lhs <= rhs;
          case 10:
            return lhs > rhs;
          default:
            return lhs < rhs;
          }
        };
        bool result;
        if (const auto *lhs = std::get_if<int32_t>(&left)) {
          if (const auto *rhs = std::get_if<int32_t>(&right))
            result = compare(*lhs, *rhs);
          else {
            float l, r;
            if (!numericFloat(left, l) || !numericFloat(right, r))
              return {false, int32_t{0}, "unsupported comparison types"};
            result = compare(l, r);
          }
        } else {
          float l, r;
          if (!numericFloat(left, l) || !numericFloat(right, r))
            return {false, int32_t{0}, "unsupported comparison types"};
          result = compare(l, r);
        }
        stack.pop_back();
        stack.back() = VmValue{int32_t{result ? 1 : 0}};
        break;
      }
      if (node.wide && (node.operand == 15 || node.operand == 16)) {
        if (stack.size() < 2)
          return {false, int32_t{0}, "arithmetic stack underflow"};
        const auto &lhsValue = readValue(stack[stack.size() - 2]);
        const auto &rhsValue = readValue(stack.back());
        if (node.operand == 16 &&
            (std::holds_alternative<const VmObject *>(lhsValue) ||
             std::holds_alternative<const VmObject *>(rhsValue))) {
          // Stock FUN_0041b4d0: FUN_00417a70 first checks both operand types
          // against "Ljava.lang.String;" and converts a non-String side in
          // place via FUN_00417da0 — an int formats as "%d" (0x60ba84), a
          // float as "%.3f" (0x60ba9c) — before the L-arm concatenates bytes.
          // After that conversion both sides are String references, so the
          // L-arm only reads each side's bytes; a null reference contributes
          // nothing.
          auto bytes = [&](const VmValue &value, std::string &out,
                           std::string &error) {
            return concatOperandBytes(value, out, error, arena, classes);
          };
          std::string left, right, concatError;
          if (!bytes(lhsValue, left, concatError) ||
              !bytes(rhsValue, right, concatError))
            return {false, int32_t{0},
                    concatError.empty() ? "unsupported string addition"
                                        : concatError};
          const auto result =
              internString(left + right, classes, staticFields, arena);
          if (!result.ok)
            return result;
          stack.pop_back();
          stack.back() = result.value;
          break;
        }
        if (std::holds_alternative<float>(lhsValue) ||
            std::holds_alternative<float>(rhsValue)) {
          float lhs, rhs;
          if (!numericFloat(lhsValue, lhs) || !numericFloat(rhsValue, rhs))
            return {false, int32_t{0}, "unsupported float arithmetic operands"};
          const double sum = static_cast<double>(lhs) +
                             (node.operand == 15 ? -static_cast<double>(rhs)
                                                 : static_cast<double>(rhs));
          const float result = static_cast<float>(sum);
          if (!std::isfinite(result) ||
              (result != 0.0f && !std::isnormal(result)) ||
              (result == 0.0f && sum != 0.0))
            return {false, int32_t{0}, "unsupported float arithmetic result"};
          stack.pop_back();
          stack.back() = VmValue{result};
          break;
        }
        const auto *left =
            std::get_if<int32_t>(&readValue(stack[stack.size() - 2]));
        const auto *right = std::get_if<int32_t>(&readValue(stack.back()));
        if (!left || !right)
          return {false, int32_t{0}, "unsupported arithmetic types"};
        const uint32_t lhs = static_cast<uint32_t>(*left);
        const uint32_t rhs = static_cast<uint32_t>(*right);
        const uint32_t bits = node.operand == 15 ? lhs - rhs : lhs + rhs;
        int32_t result;
        std::memcpy(&result, &bits, sizeof(result));
        stack.pop_back();
        stack.back() = VmValue{result};
        break;
      }
      if (node.wide && node.operand == 28) {
        if (pc + 1 >= tree.size()) {
          return {false, int32_t{0}, "unsupported literal"};
        }
        const auto &literal = tree[++pc];
        if (literal.op == 0x0c && !literal.wide) {
          stack.emplace_back(VmValue{static_cast<const VmObject *>(nullptr)});
        } else if (literal.op == 0x09 && literal.wide && cls) {
          const auto value = stringLiteral(*cls, literal.operand, classes,
                                           staticFields, arena);
          if (!value.ok)
            return value;
          stack.push_back(value.value);
        } else if (literal.op == 0x0b && literal.wide) {
          int32_t value;
          std::memcpy(&value, &literal.operand, sizeof(value));
          stack.emplace_back(VmValue{value});
        } else if (literal.op == 0x0e && literal.wide && cls) {
          if (!arena || !arena->resourceResolver || !*arena->resourceResolver ||
              literal.operand >= cls->constants.size())
            return {false, int32_t{0}, "resource resolver unavailable"};
          const auto &resource = cls->constants[literal.operand];
          if (resource.kind != 3 || resource.a >= cls->constants.size() ||
              cls->constants[resource.a].kind != 0)
            return {false, int32_t{0}, "unsupported resource literal"};
          const auto resolved = (*arena->resourceResolver)(
              cls->constants[resource.a].text, resource.b);
          if (!resolved)
            return {false, int32_t{0}, "resource resolution failed"};
          int32_t value;
          const uint32_t bits = *resolved;
          std::memcpy(&value, &bits, sizeof(value));
          stack.emplace_back(VmValue{value});
        } else if (literal.op == 0x0a && literal.wide) {
          float value;
          std::memcpy(&value, &literal.operand, sizeof(value));
          stack.emplace_back(VmValue{value});
        } else
          return {false, int32_t{0}, "unsupported literal"};
        break;
      }
      if (node.wide && node.operand == 22) {
        if (stack.empty())
          return {false, int32_t{0}, "negation stack underflow"};
        if (const auto *integer =
                std::get_if<int32_t>(&readValue(stack.back()))) {
          const uint32_t bits = uint32_t{0} - static_cast<uint32_t>(*integer);
          int32_t result;
          std::memcpy(&result, &bits, sizeof(result));
          stack.back() = VmValue{result};
          break;
        }
        const auto *value = std::get_if<float>(&readValue(stack.back()));
        if (!value)
          return {false, int32_t{0}, "unsupported negation type"};
        uint32_t bits;
        std::memcpy(&bits, value, sizeof(bits));
        if ((bits & 0x7f800000U) == 0x7f800000U)
          return {false, int32_t{0}, "unsupported non-finite negation"};
        bits ^= 0x80000000U;
        float result;
        std::memcpy(&result, &bits, sizeof(result));
        stack.back() = VmValue{result};
        break;
      }
      if (!node.wide || (node.operand != 6 && node.operand != 7))
        return unsupportedOperation(cls, pc, node);
      if (stack.size() < 2)
        return {false, int32_t{0}, "comparison stack underflow"};
      const auto &left = readValue(stack[stack.size() - 2]);
      const auto &right = readValue(stack.back());
      bool equal;
      if (left.index() != right.index()) {
        if (!((std::holds_alternative<int32_t>(left) &&
               std::holds_alternative<float>(right)) ||
              (std::holds_alternative<float>(left) &&
               std::holds_alternative<int32_t>(right))))
          return {
              false, int32_t{0},
              "unsupported comparison types at node " + std::to_string(pc) +
                  " op=" + std::to_string(node.operand) + " lhs=" +
                  std::to_string(readValue(stack[stack.size() - 2]).index()) +
                  " rhs=" + std::to_string(readValue(stack.back()).index())};
        float lhs, rhs;
        if (!numericFloat(left, lhs) || !numericFloat(right, rhs))
          return {false, int32_t{0}, "unsupported promoted comparison"};
        equal = lhs == rhs;
      } else {
        if (std::holds_alternative<std::monostate>(left))
          return {
              false, int32_t{0},
              "unsupported comparison types at node " + std::to_string(pc) +
                  " op=" + std::to_string(node.operand) + " lhs=" +
                  std::to_string(readValue(stack[stack.size() - 2]).index()) +
                  " rhs=" + std::to_string(readValue(stack.back()).index())};
        equal = left == right;
      }
      const int32_t result = (node.operand == 7 ? equal : !equal) ? 1 : 0;
      stack.pop_back();
      stack.back() = VmValue{result};
      break;
    }
    case 0x24:
    case 0x25:
    case 0x26: {
      const bool stackReceiver = node.op == 0x25;
      const bool superCall = node.op == 0x26;
      if (((stackReceiver || superCall) ? !node.wide : node.wide) || !cls ||
          (!stackReceiver && !superCall && pc + 1 >= tree.size()))
        return {false, int32_t{0},
                (cls ? cls->name : "<unknown>") + ": node " +
                    std::to_string(pc) + ": unsupported call structure"};
      size_t refPc = (stackReceiver || superCall) ? pc : pc + 1;
      const TufaClass *target = cls;
      bool explicitClass = false;
      const VmObject *fieldReceiver = nullptr;
      if (superCall) {
        if (!classes || locals.empty())
          return {false, int32_t{0}, "invalid superclass call"};
        const auto *self = std::get_if<const VmObject *>(&locals[0]);
        if (!self || !*self || !(*self)->runtimeClass ||
            !parentClass(*cls, *classes, target) || !target)
          return {false, int32_t{0}, "invalid superclass receiver"};
        fieldReceiver = *self;
      } else if (stackReceiver) {
        if (!classes || !arena || stack.empty())
          return {false, int32_t{0}, "invalid stack receiver call"};
        const auto *value =
            std::get_if<const VmObject *>(&readValue(stack.back()));
        if (!value || !*value || !(*value)->runtimeClass)
          return {false, int32_t{0}, "null stack receiver"};
        fieldReceiver = *value;
        target = fieldReceiver->runtimeClass;
        bool owned = false;
        for (const auto &object : arena->objects)
          if (object.get() == fieldReceiver)
            owned = true;
        if (!owned || std::find(classes->begin(), classes->end(), target) ==
                          classes->end())
          return {false, int32_t{0}, "unsupported stack receiver"};
        stack.pop_back();
      } else if (tree[refPc].op == 0x11) {
        if (!tree[refPc].wide ||
            tree[refPc].operand < 1 || tree[refPc].operand > 8 ||
            refPc + 2 >= tree.size() || !classes || !tree[refPc + 1].wide ||
            tree[refPc + 1].operand >= cls->constants.size())
          return {false, int32_t{0}, "unsupported qualified call"};
        if (tree[refPc].operand == 1) {
          if (tree[refPc + 1].op != 0x12 ||
              (!frame.staticMethod && locals.empty()))
            return {false, int32_t{0}, "invalid dynamic self call"};
          if (!frame.staticMethod) {
            const auto *value = std::get_if<const VmObject *>(&locals[0]);
            if (!value || !*value || !(*value)->runtimeClass)
              return {false, int32_t{0}, "missing receiver"};
            fieldReceiver = *value;
            target = (*value)->runtimeClass;
          }
          --refPc;
        } else if (tree[refPc].operand >= 3) {
          // Receiver is a chain of operand-1 nodes: a first node
          // (class/local/field) followed by field loads, then the method.
          const size_t chain = tree[refPc].operand - 1;
          bool shape = refPc + chain + 1 < tree.size() &&
                       (tree[refPc + 1].op == 0x19 ||
                        tree[refPc + 1].op == 0x01 ||
                        tree[refPc + 1].op == 0x1b);
          for (size_t i = 2; shape && i <= chain; ++i)
            shape = tree[refPc + i].op == 0x1b;
          if (!shape)
            return {false, int32_t{0}, "unsupported qualified field receiver"};
          std::vector<TufaNode> fieldTree{
              {0x11, tree[refPc].source, true,
               static_cast<uint32_t>(chain)}};
          for (size_t i = 1; i <= chain; ++i)
            fieldTree.push_back(tree[refPc + i]);
          fieldTree.push_back({0x2b, 0, false, 0});
          const auto field = execute(cls, fieldTree, locals, depth + 1, natives,
                                     classes, staticFields, arena);
          if (!field.ok)
            return field;
          const auto *value = std::get_if<const VmObject *>(&field.value);
          if (!value || !*value || !(*value)->runtimeClass)
            return {false, int32_t{0},
                    "null qualified receiver: node " + std::to_string(pc) +
                        " reference " + std::to_string(refPc)};
          fieldReceiver = *value;
          target = fieldReceiver->runtimeClass;
          refPc += chain - 1;
        } else if (tree[refPc + 1].op == 0x19) {
          const auto &classRef = cls->constants[tree[refPc + 1].operand];
          if (classRef.kind != 4 || classRef.a >= cls->constants.size() ||
              cls->constants[classRef.a].kind != 0)
            return {false, int32_t{0}, "invalid class reference"};
          std::string loadError;
          target = resolveClass(cls->constants[classRef.a].text, classes,
                                staticFields, arena, depth, loadError);
          if (!target)
            return {false, int32_t{0},
                    !loadError.empty() &&
                            loadError.rfind("class not registered: ", 0) != 0
                        ? loadError
                        : cls->name + ": node " + std::to_string(pc) +
                              ": class not registered: " +
                              cls->constants[classRef.a].text};
          explicitClass = true;
        } else if (tree[refPc + 1].op == 0x01) {
          const auto index = tree[refPc + 1].operand;
          if (index >= locals.size())
            return {false, int32_t{0}, "invalid qualified local"};
          const auto *value = std::get_if<const VmObject *>(&locals[index]);
          if (!value || !*value || !(*value)->runtimeClass || !arena)
            return {false, int32_t{0}, "null or invalid qualified receiver"};
          bool owned = false;
          for (const auto &object : arena->objects)
            if (object.get() == *value)
              owned = true;
          if (!owned)
            return {false, int32_t{0}, "unowned qualified receiver"};
          fieldReceiver = *value;
          target = fieldReceiver->runtimeClass;
          bool loaded = false;
          for (const auto *candidate : *classes)
            if (candidate == target)
              loaded = true;
          if (!loaded)
            return {false, int32_t{0},
                    "qualified receiver class not registered"};
        } else if (tree[refPc + 1].op == 0x1b) {
          const std::vector<TufaNode> fieldTree{tree[refPc + 1],
                                                {0x2b, 0, false, 0}};
          const auto field = execute(cls, fieldTree, locals, depth + 1, natives,
                                     classes, staticFields, arena);
          if (!field.ok)
            return field;
          const auto *value = std::get_if<const VmObject *>(&field.value);
          if (!value || !*value)
            return {false, int32_t{0},
                    "null qualified receiver: node " + std::to_string(pc) +
                        " reference " + std::to_string(refPc)};
          fieldReceiver = *value;
          target = fieldReceiver->runtimeClass;
        } else
          return {false, int32_t{0}, "unsupported qualified call"};
        refPc += 2;
      }
      std::string name;
      std::string descriptor;
      const TufaMethod *method = nullptr;
      const TufaClass *methodOwner = target;
      std::string lookupError;
      if ((stackReceiver || superCall || tree[refPc].op == 0x12) &&
          tree[refPc].wide) {
        // Stock FUN_004208e0 marks a bare name with -2 and FUN_004207c0 then
        // builds the
        // "(types)" text from the stack values and looks the method up by name
        // and types.
        if ((!stackReceiver && !superCall && refPc == pc + 1) ||
            tree[refPc].operand >= cls->constants.size() ||
            cls->constants[tree[refPc].operand].kind != 0 || stack.empty())
          return {false, int32_t{0}, "unsupported dynamic call"};
        name = cls->constants[tree[refPc].operand].text;
        const auto *argumentCount =
            std::get_if<int32_t>(&readValue(stack.back()));
        if (!argumentCount || *argumentCount < 0 ||
            stack.size() < static_cast<size_t>(*argumentCount) + 1)
          return {false, int32_t{0}, "unsupported dynamic call arguments"};
        std::string signature = "(";
        for (size_t i = stack.size() - 1 - static_cast<size_t>(*argumentCount);
             i + 1 < stack.size(); ++i) {
          const auto &value = readValue(stack[i]);
          if (std::holds_alternative<int32_t>(value))
            signature += 'I';
          else if (std::holds_alternative<float>(value))
            signature += 'F';
          else if (const auto *object = std::get_if<const VmObject *>(&value);
                   object && *object) {
            if (!(*object)->arrayDescriptor.empty())
              signature += (*object)->arrayDescriptor;
            else if ((*object)->runtimeClass)
              signature += "L" + (*object)->runtimeClass->name + ";";
            else
              return {false, int32_t{0}, "unsupported dynamic call arguments"};
          } else if (std::holds_alternative<std::monostate>(value) ||
                     (std::get_if<const VmObject *>(&value) &&
                      !*std::get_if<const VmObject *>(&value))) {
            // A null argument has no runtime class. The direct text lookup
            // below cannot match, so the call falls through to the
            // descriptor-driven selection loop, which fits nulls against any
            // reference parameter (referenceAssignable).
            signature += '?';
          } else
            return {false, int32_t{0}, "unsupported dynamic call arguments"};
        }
        signature += ')';
        if (fieldReceiver)
          for (const auto &field : fieldReceiver->fields)
            if (field.name == name)
              return {false, int32_t{0}, "dynamic name shadows method"};
        method = findMethod(methodOwner, name, signature, true, classes,
                            lookupError);
        if (!lookupError.empty())
          return {false, int32_t{0}, lookupError};
        if (!method) {
          methodOwner = target;
          std::vector<const TufaClass *> searched;
          while (methodOwner && !method) {
            if (std::find(searched.begin(), searched.end(), methodOwner) !=
                    searched.end() ||
                searched.size() >= 128)
              return {false, int32_t{0}, "cyclic method ancestry"};
            searched.push_back(methodOwner);
            unsigned bestScore = 0;
            bool tied = false;
            for (const auto &list : methodOwner->methods)
              for (const auto &candidate : list) {
                const auto &words = candidate.words;
                if (words[1] >= methodOwner->constants.size() ||
                    words[2] >= methodOwner->constants.size() ||
                    methodOwner->constants[words[1]].text != name)
                  continue;
                std::vector<std::string> parameters;
                std::string returnType;
                if (!nativeSignature(methodOwner->constants[words[2]].text,
                                     parameters, returnType) ||
                    parameters.size() != static_cast<size_t>(*argumentCount))
                  continue;
                bool fits = true;
                unsigned score = 0;
                for (size_t i = 0; i < parameters.size() && fits; ++i) {
                  const auto &value = readValue(
                      stack[stack.size() - parameters.size() - 1 + i]);
                  const auto &type = parameters[i];
                  if (type == "I") {
                    fits = std::holds_alternative<int32_t>(value) ||
                           std::holds_alternative<float>(value);
                    if (std::holds_alternative<int32_t>(value))
                      ++score;
                  } else if (type == "F") {
                    fits = std::holds_alternative<float>(value) ||
                           std::holds_alternative<int32_t>(value);
                    if (std::holds_alternative<float>(value))
                      ++score;
                  } else {
                    const auto *object = std::get_if<const VmObject *>(&value);
                    fits = object &&
                           referenceAssignable(*object, type, arena, classes);
                    if (fits && *object && (*object)->runtimeClass &&
                        type == "L" + (*object)->runtimeClass->name + ";")
                      ++score;
                  }
                }
                if (!fits)
                  continue;
                if (!method || score > bestScore) {
                  method = &candidate;
                  bestScore = score;
                  tied = false;
                } else if (score == bestScore) {
                  // Stock FUN_00405920 resolves applicable overloads by
                  // specificity: a parameter that is a subclass of the other
                  // candidate's parameter wins. A real tie stays an error.
                  const auto specific = [&](const std::string &sub,
                                            const std::string &base) {
                    if (sub == base)
                      return 0;
                    const auto toClass =
                        [&](const std::string &text) -> const TufaClass * {
                      if (text.size() < 3 || text.front() != 'L' ||
                          text.back() != ';')
                        return nullptr;
                      const std::string wanted =
                          text.substr(1, text.size() - 2);
                      for (const auto *entry : *classes)
                        if (entry && entry->name == wanted)
                          return entry;
                      return nullptr;
                    };
                    const TufaClass *subClass = toClass(sub);
                    const TufaClass *baseClass = toClass(base);
                    if (!subClass || !baseClass)
                      return -1;
                    if (constructorAncestor(subClass, baseClass, *classes))
                      return 1;
                    if (constructorAncestor(baseClass, subClass, *classes))
                      return -1;
                    return -1;
                  };
                  std::vector<std::string> bestParams, newParams;
                  std::string bestReturn, newReturn;
                  const auto &bestText =
                      methodOwner->constants[method->words[2]].text;
                  const auto &newText =
                      methodOwner->constants[candidate.words[2]].text;
                  bool reduced = false;
                  if (nativeSignature(bestText, bestParams, bestReturn) &&
                      nativeSignature(newText, newParams, newReturn) &&
                      bestParams.size() == newParams.size()) {
                    int lean = 0;
                    bool incomparable = false;
                    for (size_t i = 0; i < bestParams.size(); ++i) {
                      if (bestParams[i] == newParams[i])
                        continue;
                      const int side = specific(newParams[i], bestParams[i]);
                      if (side == 1) {
                        if (lean == -1)
                          incomparable = true;
                        lean = 1;
                      } else if (side == -1) {
                        if (lean == 1)
                          incomparable = true;
                        lean = -1;
                      } else {
                        incomparable = true;
                      }
                    }
                    if (!incomparable && lean == 1) {
                      method = &candidate;
                      tied = false;
                      reduced = true;
                    } else if (!incomparable && lean == -1) {
                      tied = false;
                      reduced = true;
                    }
                  }
                  if (!reduced)
                    tied = true;
                } else
                  tied = true;
              }
            if (tied)
              return {false, int32_t{0},
                      (cls ? cls->name : "<unknown>") + ": node " +
                          std::to_string(pc) + ": ambiguous dynamic method " +
                          methodOwner->name + "." + name + signature};
            if (!method) {
              const TufaClass *parent = nullptr;
              if (!parentClass(*methodOwner, *classes, parent))
                return {false, int32_t{0}, "invalid method ancestry"};
              methodOwner = parent;
            }
          }
        }
        if (!method)
          return {false, int32_t{0},
                  "unsupported dynamic method: " + target->name + "." + name +
                      signature};
        descriptor = methodOwner->constants[method->words[2]].text;
      } else {
        if (tree[refPc].op != 0x1a || !tree[refPc].wide ||
            tree[refPc].operand >= cls->constants.size())
          return {false, int32_t{0},
                  cls->name + ": node " + std::to_string(pc) +
                      ": unsupported call structure"};
        const auto &ref = cls->constants[tree[refPc].operand];
        if (ref.kind != 5 || ref.a >= cls->constants.size() ||
            ref.b >= cls->constants.size())
          return {false, int32_t{0}, "invalid method reference"};
        const auto &owner = cls->constants[ref.a];
        const auto &pair = cls->constants[ref.b];
        if (owner.kind != 4 || owner.a >= cls->constants.size() ||
            cls->constants[owner.a].kind != 0 || pair.kind != 7 ||
            pair.a >= cls->constants.size() ||
            pair.b >= cls->constants.size() ||
            cls->constants[pair.a].kind != 0 ||
            cls->constants[pair.b].kind != 0)
          return {false, int32_t{0}, "unsupported method reference"};
        name = cls->constants[pair.a].text;
        descriptor = cls->constants[pair.b].text;
        if (stack.empty())
          return {false, int32_t{0},
                  "unsupported call signature: " + target->name + "." + name +
                      descriptor};
        method = findMethod(methodOwner, name, descriptor, false, classes,
                            lookupError);
        if (!lookupError.empty())
          return {false, int32_t{0}, lookupError};
        if (!method)
          return {false, int32_t{0},
                  "unsupported method lookup: " + target->name + "." + name +
                      descriptor + " at " + cls->name + " node " +
                      std::to_string(pc)};
        // FUN_004208e0 resolves an instance method again on the actual
        // receiver when it differs from the declaring class.
        if (!explicitClass && !superCall && !(method->words[0] & 8) &&
            classes) {
          const VmObject *receiver = fieldReceiver;
          if (!receiver && !locals.empty()) {
            if (const auto *self = std::get_if<const VmObject *>(&locals[0]))
              receiver = *self;
          }
          if (receiver && receiver->runtimeClass &&
              receiver->runtimeClass != methodOwner) {
            if ((method->words[0] & 0x40) &&
                !constructorAncestor(receiver->runtimeClass, methodOwner,
                                     *classes))
              return {false, int32_t{0}, "invalid native receiver"};
            methodOwner = receiver->runtimeClass;
            method = findMethod(methodOwner, name, descriptor, false, classes,
                                lookupError);
            if (!lookupError.empty())
              return {false, int32_t{0}, lookupError};
            if (!method)
              return {false, int32_t{0}, "unsupported receiver method"};
          }
        }
      }
      if (explicitClass && !(method->words[0] & 8))
        return {false, int32_t{0}, "unsupported qualified instance method"};
      if (method->words[0] & 0x40) {
        const bool isStatic = (method->words[0] & 8) != 0;
        const VmObject *receiver = fieldReceiver;
        if (!isStatic && !receiver && !locals.empty()) {
          const auto *self = std::get_if<const VmObject *>(&locals[0]);
          if (self)
            receiver = *self;
        }
        const unsigned outerDepth = arena ? arena->nativeDepth : 0;
        if (arena)
          arena->nativeDepth = depth;
        const auto called =
            callNative(stack, *methodOwner, name, descriptor, isStatic,
                       receiver, natives, classes, arena);
        if (arena)
          arena->nativeDepth = outerDepth;
        if (!called.ok)
          return called;
        pc = refPc;
        break;
      }
      std::vector<std::string> parameters;
      std::string returnType;
      if (!nativeSignature(descriptor, parameters, returnType) || stack.empty())
        return {false, int32_t{0},
                "unsupported call signature: " + target->name + "." + name +
                    descriptor};
      const size_t count = parameters.size();
      if (stack.size() < count + 1)
        return {false, int32_t{0}, "missing call arguments"};
      stack.pop_back();
      if (method->words[3] >= methodOwner->trees.size())
        return {false, int32_t{0},
                "unsupported method tree: " + methodOwner->name + "." + name +
                    descriptor};
      std::vector<VmValue> calleeLocals;
      if (!(method->words[0] & 8)) {
        if (fieldReceiver)
          calleeLocals.push_back(fieldReceiver);
        else {
          if (locals.empty() || !std::get_if<const VmObject *>(&locals[0]) ||
              !std::get<const VmObject *>(locals[0]) ||
              (std::get<const VmObject *>(locals[0])->runtimeClass != cls &&
               (!classes ||
                !constructorAncestor(
                    std::get<const VmObject *>(locals[0])->runtimeClass, cls,
                    *classes))))
            return {false, int32_t{0}, "missing receiver"};
          calleeLocals.push_back(locals[0]);
        }
      }
      for (size_t i = count; i > 0; --i) {
        VmValue argument = readValue(stack.back());
        const auto &type = parameters[i - 1];
        if (type == "I" && std::holds_alternative<float>(argument) &&
            !floatToInt(argument, argument))
          return {false, int32_t{0}, "unsupported argument type"};
        if (type == "F" && std::holds_alternative<int32_t>(argument))
          argument = static_cast<float>(std::get<int32_t>(argument));
        if (type == "Ljava.lang.String;" &&
            (std::holds_alternative<int32_t>(argument) ||
             std::holds_alternative<float>(argument))) {
          // Stock FUN_00417da0 converts a numeric value to a String ("%d" /
          // "%.3f") when it is assigned to a String-typed slot.
          std::string text, convertError;
          if (!concatOperandBytes(argument, text, convertError, arena,
                                  classes))
            return {false, int32_t{0},
                    convertError.empty() ? "unsupported argument type"
                                         : convertError};
          const auto interned = internString(text, classes, staticFields, arena);
          if (!interned.ok)
            return interned;
          argument = interned.value;
        }
        const auto *object = std::get_if<const VmObject *>(&argument);
        const bool fits =
            type == "I" ? std::holds_alternative<int32_t>(argument)
            : type == "F"
                ? std::holds_alternative<float>(argument)
                : object && referenceAssignable(*object, type, arena, classes);
        if (!fits)
          return {false, int32_t{0}, "unsupported argument type"};
        calleeLocals.push_back(argument);
        stack.pop_back();
      }
      if (methodOwner->trees[method->words[3]].empty()) {
        // Stock 0041fbc0 skips a null body but still pushes one zeroed value
        // of the declared return type for a non-void method.
        stack.emplace_back(returnType == "I"   ? VmValue{int32_t{0}}
                           : returnType == "F" ? VmValue{0.0f}
                           : returnType == "V"
                               ? VmValue{std::monostate{}}
                               : VmValue{static_cast<const VmObject *>(nullptr)});
        pc = refPc;
        break;
      }
      frame.returnType = returnType;
      frame.waiting = returnType == "V"   ? VmFrame::ReturnKind::Void
                      : returnType == "F" ? VmFrame::ReturnKind::Float
                      : returnType == "I" ? VmFrame::ReturnKind::Integer
                                          : VmFrame::ReturnKind::Reference;
      execution.frames.push_back(std::make_unique<VmFrame>(
          methodOwner, methodOwner->trees[method->words[3]],
          std::move(calleeLocals), depth + 1));
      execution.frames.back()->staticMethod = (method->words[0] & 8) != 0;
      auto &calleeTypes = execution.frames.back()->localTypes;
      const size_t receiverOffset = (method->words[0] & 8) ? 0 : 1;
      for (size_t i = 0; i < count; ++i)
        calleeTypes[receiverOffset + i] = parameters[count - 1 - i];
      pc = refPc;
      break;
    }
    case 0x28:
      if (node.wide || stack.empty()) {
        return {false, int32_t{0}, "invalid stack copy"};
      }
      stack.back() = VmValue{readValue(stack.back())};
      break;
    case 0x29:
      if (node.wide)
        return {false, int32_t{0}, "invalid void marker"};
      stack.emplace_back(VmValue{int32_t{0}});
      break;
    case 0x03:
      if (!node.wide || locals.empty()) {
        return {false, int32_t{0}, "invalid local removal"};
      }
      // Stock's local slots hold boxes kept alive by refcount, so a value can
      // outlive the slot it came from (FUN_00423e30/FUN_00423d40). Copy any
      // stack references to the dying slot before dropping it.
      {
        const size_t dying = locals.size() - 1;
        for (auto &entry : stack) {
          const auto *local = std::get_if<LocalLocation>(&entry);
          if (local && local->index == dying)
            entry = VmValue{(*local->locals)[dying]};
        }
      }
      locals.pop_back();
      localTypes.pop_back();
      break;
    case 0x2b:
      if (node.wide || stack.empty()) {
        return {false, int32_t{0}, "invalid return"};
      }
      frame.completed = true;
      return {true, readValue(stack.back()), {}};
    case 0x10:
      // Ends an initializer tree. Stock's caller pops the value it left
      // (FUN_00403e80 after FUN_00420ff0).
      if (node.wide || stack.empty())
        return {false, int32_t{0}, "initializer left no value"};
      frame.completed = true;
      return {true, readValue(stack.back()), {}};
    case 0x2a:
      if (node.wide)
        return {false, int32_t{0}, "invalid void return"};
      frame.completed = true;
      return {true, int32_t{0}, {}, true};
    default:
      return unsupportedOperation(cls, pc, node);
    }
    ++pc;
    return {true, int32_t{0}, {}};
  }
  return {false, int32_t{0},
          "tree ended without return: " + (cls ? cls->name : "<unknown>") +
              " pc=" + std::to_string(frame.pc) +
              " size=" + std::to_string(tree.size()) + " first=" +
              (tree.empty() ? std::string{"empty"}
                            : std::to_string(tree.front().op))};
}

// Resolve once for both synchronous native callbacks and resumable host tasks.
static VmResult resolveScriptCall(
    VmArena *arena, const std::vector<const TufaClass *> *classes,
    const VmObject *receiver, const std::string &name,
    const std::vector<VmValue> &arguments, const TufaClass *&resolvedOwner,
    const std::vector<TufaNode> *&resolvedTree, std::vector<VmValue> &locals) {
  if (!receiver || !receiver->runtimeClass || !classes)
    return {false, int32_t{0}, "script call needs an object receiver"};
  bool owned = false;
  for (const auto &object : arena->objects)
    if (object.get() == receiver)
      owned = true;
  if (!owned)
    return {false, int32_t{0}, "script call receiver is not an arena object"};
  std::string signature = "(";
  for (const auto &argument : arguments) {
    if (std::holds_alternative<int32_t>(argument))
      signature += 'I';
    else if (std::holds_alternative<float>(argument))
      signature += 'F';
    else if (const auto *value = std::get_if<const VmObject *>(&argument);
             value && *value && (*value)->runtimeClass)
      signature += "L" + (*value)->runtimeClass->name + ";";
    else
      signature += '?';
  }
  signature += ')';
  const TufaClass *owner = receiver->runtimeClass;
  const TufaMethod *method = nullptr;
  std::vector<const TufaClass *> visited;
  while (owner && !method) {
    if (visited.size() >= 128 ||
        std::find(visited.begin(), visited.end(), owner) != visited.end())
      return {false, int32_t{0}, "cyclic script call ancestry"};
    visited.push_back(owner);
    for (const auto &list : owner->methods)
      for (const auto &candidate : list) {
        const auto &words = candidate.words;
        if (words[1] >= owner->constants.size() ||
            words[2] >= owner->constants.size() ||
            owner->constants[words[1]].text != name)
          continue;
        std::vector<std::string> parameters;
        std::string returnType;
        std::string descriptor = owner->constants[words[2]].text;
        if (!descriptor.empty() && descriptor.back() == ')')
          descriptor += 'V';
        if (!nativeSignature(descriptor, parameters, returnType) ||
            parameters.size() != arguments.size())
          continue;
        bool fits = true;
        for (size_t i = 0; i < parameters.size(); ++i) {
          const auto &arg = arguments[i];
          const auto *object = std::get_if<const VmObject *>(&arg);
          if (parameters[i] == "I")
            fits = std::holds_alternative<int32_t>(arg);
          else if (parameters[i] == "F")
            fits = std::holds_alternative<float>(arg);
          else
            fits = object &&
                   referenceAssignable(*object, parameters[i], arena, classes);
          if (!fits)
            break;
        }
        if (!fits)
          continue;
        if (method)
          return {false, int32_t{0}, "ambiguous script call: " + name};
        method = &candidate;
      }
    if (!method) {
      const TufaClass *parent = nullptr;
      if (!parentClass(*owner, *classes, parent))
        return {false, int32_t{0}, "invalid script call ancestry"};
      owner = parent;
    }
  }
  if (!method)
    return {false, int32_t{0}, "script call: no method " + name + signature};
  if ((method->words[0] & (8 | 0x40)) ||
      method->words[3] >= owner->trees.size())
    return {false, int32_t{0},
            "script call: " + name + signature +
                " is not a script instance method"};
  if (owner->trees[method->words[3]].empty()) {
    std::vector<std::string> parameters;
    std::string returnType;
    std::string descriptor = owner->constants[method->words[2]].text;
    if (!descriptor.empty() && descriptor.back() == ')')
      descriptor += 'V';
    if (!nativeSignature(descriptor, parameters, returnType) ||
        returnType != "V")
      return {false, int32_t{0}, "empty non-void method unsupported"};
    resolvedOwner = owner;
    resolvedTree = nullptr;
    return {true, std::monostate{}, {}};
  }
  locals = {receiver};
  // 0041fbc0 pops arguments into locals in reverse order.
  locals.insert(locals.end(), arguments.rbegin(), arguments.rend());
  resolvedOwner = owner;
  resolvedTree = &owner->trees[method->words[3]];
  return {true, std::monostate{}, {}};
}

static VmScriptCall
makeScriptCall(const std::vector<NativeEntry> *natives,
               const std::vector<const TufaClass *> *classes,
               const std::vector<VmStaticFields> *staticFields,
               VmArena *arena) {
  return [=](const VmObject *receiver, const std::string &name,
             std::vector<VmValue> arguments) -> VmResult {
    const TufaClass *owner = nullptr;
    const std::vector<TufaNode> *tree = nullptr;
    std::vector<VmValue> locals;
    auto result = resolveScriptCall(arena, classes, receiver, name, arguments,
                                    owner, tree, locals);
    if (!result.ok || !tree)
      return result;
    return execute(owner, *tree, std::move(locals), arena->nativeDepth + 1,
                   natives, classes, staticFields, arena);
  };
}

VmResult vmAllocateNative(VmArena &arena, const std::string &className) {
  const auto *state = arena.activeExecution;
  if (!state || !state->classes || !state->staticFields || className.empty())
    return {false, int32_t{0}, "native allocation needs active VM state"};
  TufaClass caller;
  caller.name = "<native allocation>";
  caller.constants.push_back({0, "L" + className + ";", 0, 0});
  const std::vector<TufaNode> tree{
      {0x07, 0, true, 30}, {0x06, 0, true, 0}, {0x2b, 0, false, 0}};
  return execute(&caller, tree, {}, arena.nativeDepth + 1, state->natives,
                 state->classes, state->staticFields, &arena);
}

VmResult vmHostAllocate(VmArena &arena, const std::string &className,
                        const std::vector<NativeEntry> *natives,
                        const std::vector<const TufaClass *> *classes,
                        const std::vector<VmStaticFields> *staticFields) {
  if (arena.activeExecution || className.empty())
    return {false, int32_t{0}, "host allocation requires inactive VM"};
  TufaClass caller;
  caller.name = "<host allocation>";
  caller.constants.push_back({0, "L" + className + ";", 0, 0});
  const std::vector<TufaNode> tree{
      {0x07, 0, true, 30}, {0x06, 0, true, 0}, {0x2b, 0, false, 0}};
  return vmExecute(caller, tree, {}, natives, classes, staticFields, &arena);
}

VmResult vmHostCall(VmArena &arena, const VmObject *receiver,
                    const std::string &name, std::vector<VmValue> arguments,
                    const std::vector<NativeEntry> *natives,
                    const std::vector<const TufaClass *> *classes,
                    const std::vector<VmStaticFields> *staticFields) {
  if (arena.activeExecution)
    return {false, int32_t{0}, "host call requires inactive VM"};
  auto previous = std::move(arena.callScript);
  const auto *previousNatives = arena.natives;
  const unsigned previousDepth = arena.nativeDepth;
  arena.natives = natives;
  arena.nativeDepth = 0;
  arena.callScript = makeScriptCall(natives, classes, staticFields, &arena);
  auto result = arena.callScript(receiver, name, std::move(arguments));
  arena.callScript = std::move(previous);
  arena.natives = previousNatives;
  arena.nativeDepth = previousDepth;
  return result;
}

VmExecution vmStart(const TufaClass &cls, const std::vector<TufaNode> &tree,
                    std::vector<VmValue> locals,
                    const std::vector<NativeEntry> *natives,
                    const std::vector<const TufaClass *> *classes,
                    const std::vector<VmStaticFields> *staticFields,
                    VmArena *arena) {
  auto state = std::make_shared<VmExecutionState>();
  state->natives = natives;
  state->classes = classes;
  state->staticFields = staticFields;
  state->arena = arena;
  state->frames.push_back(
      std::make_unique<VmFrame>(&cls, tree, std::move(locals), 0));
  return {std::move(state)};
}

VmResult vmResume(VmExecution &execution, size_t nodeBudget, bool &finished) {
  if (!execution.state) {
    finished = true;
    return {false, int32_t{0}, "missing execution state"};
  }
  auto &state = *execution.state;
  if (state.finished) {
    finished = true;
    return state.result;
  }
  VmArena *arena = state.arena;
  VmExecutionState *previous = arena ? arena->activeExecution : nullptr;
  VmScriptCall oldCall;
  const std::vector<NativeEntry> *oldNatives = nullptr;
  unsigned oldDepth = 0;
  if (arena) {
    arena->failureTrace.clear();
    oldCall = std::move(arena->callScript);
    oldNatives = arena->natives;
    oldDepth = arena->nativeDepth;
    arena->activeExecution = &state;
    arena->natives = state.natives;
    arena->callScript =
        makeScriptCall(state.natives, state.classes, state.staticFields, arena);
  }
  state.yieldRequested = false;
  state.result = drive(state, 0, nodeBudget, finished);
  state.finished = finished;
  if (arena) {
    arena->activeExecution = previous;
    arena->callScript = std::move(oldCall);
    arena->natives = oldNatives;
    arena->nativeDepth = oldDepth;
  }
  return state.result;
}

struct VmScheduler::Impl {
  struct Thread {
    const VmObject *owner;
    std::string name;
    int priority = 2;
    uint32_t flags = 2;
    double wake = 0;
    const VmObject *waitingOn = nullptr;
    uint64_t waitOrder = 0;
    VmExecution execution;
    VmRandomState random;
    size_t slices = 0;
    uint32_t lastSliceFrame = 0;
    std::shared_ptr<VmHostTask> hostTask;
    std::function<bool()> alive;
  };
  VmArena &arena;
  const std::vector<NativeEntry> *natives;
  const std::vector<const TufaClass *> *classes;
  const std::vector<VmStaticFields> *staticFields;
  Clock clock;
  std::vector<std::unique_ptr<Thread>> threads;
  Thread *current = nullptr;
  size_t cursor = 0;
  uint64_t nextWaitOrder = 0;

  VmField *pointer(const VmObject *owner) const {
    if (!owner)
      return nullptr;
    for (auto &field : owner->fields)
      if (field.name == "ptr")
        return &field;
    return nullptr;
  }
  Thread *find(const VmObject *owner) const {
    auto *ptr = pointer(owner);
    const auto *id = ptr ? std::get_if<int32_t>(&ptr->value) : nullptr;
    if (!id || *id <= 0 || static_cast<size_t>(*id) > threads.size())
      return nullptr;
    auto *thread = threads[static_cast<size_t>(*id) - 1].get();
    return thread && thread->owner == owner ? thread : nullptr;
  }
};

VmScheduler::VmScheduler(VmArena &arena,
                         const std::vector<NativeEntry> *natives,
                         const std::vector<const TufaClass *> *classes,
                         const std::vector<VmStaticFields> *staticFields,
                         Clock clock)
    : impl_(std::make_unique<Impl>(Impl{arena,
                                        natives,
                                        classes,
                                        staticFields,
                                        std::move(clock),
                                        {},
                                        nullptr,
                                        0})) {
  arena.scheduler = this;
}
VmScheduler::~VmScheduler() {
  for (const auto &thread : impl_->threads) {
    if (thread && thread->hostTask && !thread->hostTask->finished) {
      thread->hostTask->result = {false, int32_t{0}, "scheduler destroyed"};
      thread->hostTask->finished = true;
    }
  }
  if (impl_->arena.scheduler == this)
    impl_->arena.scheduler = nullptr;
}

VmResult VmScheduler::createThread(const VmObject *owner, std::string name) {
  auto *ptr = impl_->pointer(owner);
  if (!ptr || !std::holds_alternative<int32_t>(ptr->value))
    return {false, int32_t{0}, "thread has no ptr field"};
  if (impl_->find(owner))
    return {false, int32_t{0}, "thread already initialized"};
  if (impl_->threads.size() >= static_cast<size_t>(INT32_MAX))
    return {false, int32_t{0}, "thread handles exhausted"};
  auto thread = std::make_unique<Impl::Thread>();
  thread->owner = owner;
  thread->name = std::move(name);
  // Bounded diagnostic: the loading dialog never dismisses; note every
  // thread creation and start until that flow is traced.
  runlog::event(runlog::EventKind::Note, "vmthread create \"%s\"",
                thread->name.c_str());
  impl_->threads.push_back(std::move(thread));
  ptr->value = static_cast<int32_t>(impl_->threads.size());
  return {true, int32_t{0}, {}};
}

VmResult VmScheduler::setPriority(const VmObject *owner, int32_t priority) {
  auto *thread = impl_->find(owner);
  if (!thread)
    return {true, int32_t{0}, {}};
  thread->priority = priority;
  return {true, int32_t{0}, {}};
}

VmResult VmScheduler::start(const VmObject *owner) {
  auto *thread = impl_->find(owner);
  if (!thread)
    return {true, int32_t{0}, {}};
  const VmObject *target = nullptr;
  for (const auto &field : owner->fields)
    if (field.name == "target") {
      if (const auto *value = std::get_if<const VmObject *>(&field.value))
        target = *value;
    }
  if (!target || !target->runtimeClass || !impl_->classes)
    return {false, int32_t{0}, "thread target is unavailable"};
  const TufaClass *methodOwner = target->runtimeClass;
  std::string error;
  const auto *method =
      findMethod(methodOwner, "run", "()V", false, impl_->classes, error);
  if (!error.empty())
    return {false, int32_t{0}, error};
  if (!method || (method->words[0] & 0x40) ||
      method->words[3] >= methodOwner->trees.size())
    return {false, int32_t{0},
            "thread target has no script run()V: " +
                target->runtimeClass->name};
  const bool isStatic = (method->words[0] & 8) != 0;
  // Bounded diagnostic: see which script threads actually start.
  runlog::event(runlog::EventKind::Note, "vmthread start \"%s\" target %s",
                thread->name.c_str(), target->runtimeClass->name.c_str());
  thread->execution = vmStart(
      *methodOwner, methodOwner->trees[method->words[3]],
      isStatic ? std::vector<VmValue>{} : std::vector<VmValue>{target},
      impl_->natives, impl_->classes, impl_->staticFields, &impl_->arena);
  thread->execution.state->frames.back()->staticMethod = isStatic;
  thread->flags &= ~uint32_t{2};
  return {true, int32_t{0}, {}};
}

VmResult VmScheduler::stop(const VmObject *owner) {
  auto *thread = impl_->find(owner);
  if (!thread)
    return {true, int32_t{0}, {}};
  if (impl_->current == thread && impl_->arena.activeExecution &&
      impl_->arena.activeExecution->synchronousDepth)
    return {false, int32_t{0},
            "stop in synchronous callback or initializer is unsupported"};
  thread->flags |= 0x80;
  if (auto *ptr = impl_->pointer(owner))
    ptr->value = int32_t{0};
  if (impl_->current == thread && impl_->arena.activeExecution)
    impl_->arena.activeExecution->yieldRequested = true;
  return {true, int32_t{0}, {}};
}

// Thread.sleep(F) takes milliseconds: stock 0047c650 -> 0041f630 stores
// FUN_005516c0() + argument as the wake time, and FUN_005516c0 is the
// performance counter in seconds times 1000.0 (float at 005f0910, read in the
// stock exe), the same clock FUN_00416940 compares the wake time against. The
// scheduler clock here is in seconds.
VmResult VmScheduler::sleep(float milliseconds) {
  if (!impl_->current || !impl_->arena.activeExecution)
    return {false, int32_t{0}, "sleep outside a scheduled thread"};
  if (impl_->arena.activeExecution->synchronousDepth)
    return {false, int32_t{0},
            "sleep in synchronous callback or initializer is unsupported"};
  impl_->current->wake =
      impl_->clock() + static_cast<double>(milliseconds) * 0.001;
  impl_->current->flags |= 8;
  impl_->arena.activeExecution->yieldRequested = true;
  return {true, int32_t{0}, {}};
}

VmResult VmScheduler::wait(const VmObject *object) {
  if (!object)
    return {false, int32_t{0}, "wait without a receiver"};
  if (!impl_->current || !impl_->arena.activeExecution)
    return {false, int32_t{0}, "wait outside a scheduled thread"};
  if (impl_->arena.activeExecution->synchronousDepth)
    return {false, int32_t{0},
            "wait in synchronous callback or initializer is unsupported"};
  auto *thread = impl_->current;
  if (thread->waitingOn)
    return {false, int32_t{0}, "thread already waiting"};
  if (impl_->nextWaitOrder == UINT64_MAX)
    return {false, int32_t{0}, "wait order exhausted"};
  thread->waitingOn = object;
  thread->waitOrder = ++impl_->nextWaitOrder;
  thread->flags |= 0x10;
  // Bounded diagnostic: loading dialog stall; note monitor waits.
  runlog::event(runlog::EventKind::Note, "monitor wait \"%s\" on %s",
                thread->name.c_str(),
                object->runtimeClass ? object->runtimeClass->name.c_str()
                                     : "?");
  impl_->arena.activeExecution->yieldRequested = true;
  return {true, int32_t{0}, {}};
}

VmResult VmScheduler::notify(const VmObject *object, bool all) {
  if (!object)
    return {false, int32_t{0}, "notify without a receiver"};
  const char *objectClass =
      object->runtimeClass ? object->runtimeClass->name.c_str() : "?";
  const char *notifier = impl_->current ? impl_->current->name.c_str() : "host";
  size_t woken = 0;
  do {
    Impl::Thread *newest = nullptr;
    for (const auto &thread : impl_->threads)
      if (thread && !(thread->flags & 0x80) && thread->waitingOn == object &&
          (!newest || thread->waitOrder > newest->waitOrder))
        newest = thread.get();
    if (!newest)
      break;
    newest->waitingOn = nullptr;
    newest->flags &= ~uint32_t{0x10};
    ++woken;
    runlog::event(runlog::EventKind::Note,
                  "monitor wake \"%s\" on %s by \"%s\"", newest->name.c_str(),
                  objectClass, notifier);
  } while (all);
  // A notify nobody waits for is lost, as in stock. The per-frame engine
  // notify is expected to be lost most frames; any other lost notify is the
  // usual sign of a handshake that never completes.
  if (!woken && std::strcmp(objectClass, "java.render.GfxEngine") != 0)
    runlog::event(runlog::EventKind::Note,
                  "monitor notify%s lost on %s by \"%s\" (no waiter)",
                  all ? "All" : "", objectClass, notifier);
  return {true, int32_t{0}, {}};
}
VmResult VmScheduler::startHostTask(const VmObject *receiver,
                                    const std::string &name,
                                    std::vector<VmValue> arguments,
                                    std::function<bool()> alive,
                                    std::shared_ptr<VmHostTask> &task) {
  task.reset();
  if (alive && !alive())
    return {false, int32_t{0}, "host task owner destroyed"};
  if (impl_->arena.activeExecution)
    return {false, int32_t{0}, "host task requires inactive VM"};
  const TufaClass *owner = nullptr;
  const std::vector<TufaNode> *tree = nullptr;
  std::vector<VmValue> locals;
  auto result = resolveScriptCall(&impl_->arena, impl_->classes, receiver, name,
                                  arguments, owner, tree, locals);
  if (!result.ok)
    return result;
  task = std::make_shared<VmHostTask>();
  if (!tree) {
    task->finished = true;
    task->result = result;
    return result;
  }
  auto thread = std::make_unique<Impl::Thread>();
  thread->owner = nullptr;
  thread->name = "host:" + name;
  thread->flags = 0;
  thread->hostTask = task;
  thread->alive = std::move(alive);
  thread->execution =
      vmStart(*owner, *tree, std::move(locals), impl_->natives, impl_->classes,
              impl_->staticFields, &impl_->arena);
  for (auto &slot : impl_->threads) {
    if (!slot) {
      slot = std::move(thread);
      return {true, std::monostate{}, {}};
    }
  }
  impl_->threads.push_back(std::move(thread));
  return {true, std::monostate{}, {}};
}

VmResult VmScheduler::tick(size_t nodeBudget, int minPriority) {
  const size_t count = impl_->threads.size();
  if (!count)
    return {true, int32_t{0}, {}};
  const size_t begin = impl_->cursor % count;
  impl_->cursor = (begin + 1) % count;
  for (size_t i = 0; i < count; ++i) {
    const size_t index = (begin + i) % count;
    auto *thread = impl_->threads[index].get();
    if (!thread)
      continue;
    if (thread->hostTask && thread->alive && !thread->alive()) {
      thread->hostTask->result = {false, int32_t{0},
                                  "host task owner destroyed"};
      thread->hostTask->finished = true;
      impl_->threads[index].reset();
      continue;
    }
    if ((thread->flags & 0x80) && !(thread->flags & 4)) {
      impl_->threads[index].reset();
      continue;
    }
    if ((thread->flags & 0x92) || (thread->flags & 0x44) ||
        thread->priority < minPriority)
      continue;
    if ((thread->flags & 8) &&
        static_cast<double>(thread->wake) < impl_->clock())
      thread->flags &= ~uint32_t{8};
    if (thread->flags & 8)
      continue;
    impl_->current = thread;
    auto *previousRandom = impl_->arena.randomState;
    impl_->arena.randomState = &thread->random;
    bool finished = false;
    ++thread->slices;
    thread->lastSliceFrame = runlog::g_frame.load(std::memory_order_relaxed);
    runlog::event(runlog::EventKind::Thread, "slice %s #%zu",
                  thread->name.c_str(), thread->slices);
    auto result = vmResume(thread->execution, nodeBudget, finished);
    impl_->arena.randomState = previousRandom;
    impl_->current = nullptr;
    if (!result.ok) {
      if (thread->hostTask) {
        thread->hostTask->result = result;
        thread->hostTask->finished = true;
        thread->flags |= 0x80;
      }
      result.error = "thread " + thread->name + ": " + result.error;
      return result;
    }
    if (finished) {
      if (thread->hostTask) {
        thread->hostTask->result = result;
        thread->hostTask->finished = true;
      }
      thread->flags |= 0x40;
      if (!(thread->flags & 1))
        thread->flags |= 0x80;
      if (auto *ptr = impl_->pointer(thread->owner))
        ptr->value = int32_t{0};
    }
  }
  return {true, int32_t{0}, {}};
}

const std::vector<const TufaClass *> *VmScheduler::classes() const {
  return impl_->classes;
}
const std::vector<VmStaticFields> *VmScheduler::staticFields() const {
  return impl_->staticFields;
}

size_t VmScheduler::threadCount() const {
  size_t count = 0;
  for (const auto &thread : impl_->threads)
    if (thread && !(thread->flags & 0x80))
      ++count;
  return count;
}
std::vector<std::string> VmScheduler::threadNames() const {
  std::vector<std::string> names;
  for (const auto &thread : impl_->threads)
    if (thread && !(thread->flags & 0x80))
      names.push_back(thread->name);
  return names;
}

std::vector<std::pair<std::string, size_t>>
VmScheduler::threadProgress() const {
  std::vector<std::pair<std::string, size_t>> result;
  for (const auto &thread : impl_->threads)
    if (thread)
      result.emplace_back(thread->name, thread->slices);
  return result;
}

std::vector<std::pair<std::string, const VmObject *>>
VmScheduler::threadWaits() const {
  std::vector<std::pair<std::string, const VmObject *>> result;
  for (const auto &thread : impl_->threads)
    if (thread && !(thread->flags & 0x80) && thread->waitingOn)
      result.emplace_back(thread->name, thread->waitingOn);
  return result;
}

static std::string describeFrame(const VmFrame &frame) {
  std::string label = frame.cls ? frame.cls->name : std::string{"?"};
  const char *method = "?";
  const char *desc = "";
  if (frame.cls && frame.tree) {
    const auto &cls = *frame.cls;
    for (const auto &side : cls.methods)
      for (const auto &m : side) {
        if (m.words[3] >= cls.trees.size() ||
            &cls.trees[m.words[3]] != frame.tree)
          continue;
        if (m.words[1] < cls.constants.size() &&
            cls.constants[m.words[1]].kind == 0)
          method = cls.constants[m.words[1]].text.c_str();
        if (m.words[2] < cls.constants.size() &&
            cls.constants[m.words[2]].kind == 0)
          desc = cls.constants[m.words[2]].text.c_str();
      }
  }
  label += '.';
  label += method;
  label += desc;
  label += " @" + std::to_string(frame.pc) + '/' +
           std::to_string(frame.tree ? frame.tree->size() : 0);
  if (frame.tree && frame.pc < frame.tree->size()) {
    char op[16];
    std::snprintf(op, sizeof op, " op=%02x", (*frame.tree)[frame.pc].op);
    label += op;
  }
  return label;
}

std::vector<VmThreadSnapshot> VmScheduler::snapshot(size_t maxFrames) const {
  std::vector<VmThreadSnapshot> result;
  const double now = impl_->clock();
  for (const auto &thread : impl_->threads) {
    if (!thread)
      continue;
    VmThreadSnapshot s;
    s.name = thread->name;
    s.flags = thread->flags;
    s.slices = thread->slices;
    s.lastSliceFrame = thread->lastSliceFrame;
    s.hostTask = thread->hostTask != nullptr;
    if (thread->flags & 0x80)
      s.state = "dead";
    else if (thread->waitingOn) {
      s.state = "waiting";
      s.waitingOn = thread->waitingOn->runtimeClass
                        ? thread->waitingOn->runtimeClass->name
                        : "?";
    } else if (thread->flags & 8) {
      s.state = "sleeping";
      s.sleepRemaining = static_cast<double>(thread->wake) - now;
    } else if (thread->flags & 2)
      s.state = "not-started";
    else if (thread->flags & 0x44)
      s.state = "stopped";
    else
      s.state = "runnable";
    const auto *state = thread->execution.state.get();
    if (state)
      for (size_t i = state->frames.size();
           i-- > 0 && s.stack.size() < maxFrames;)
        s.stack.push_back(describeFrame(*state->frames[i]));
    result.push_back(std::move(s));
  }
  return result;
}

VmResult vmExecute(const std::vector<TufaNode> &tree,
                   std::vector<VmValue> locals) {
  return execute(nullptr, tree, std::move(locals), 0, nullptr, nullptr, nullptr,
                 nullptr);
}

VmResult vmExecute(const TufaClass &cls, const std::vector<TufaNode> &tree,
                   std::vector<VmValue> locals,
                   const std::vector<NativeEntry> *natives,
                   const std::vector<const TufaClass *> *classes,
                   const std::vector<VmStaticFields> *staticFields,
                   VmArena *arena) {
  if (classes)
    for (size_t i = 0; i < classes->size(); ++i) {
      if (!(*classes)[i] || (*classes)[i]->name.empty())
        return {false, int32_t{0}, "invalid class registry"};
      for (size_t j = 0; j < i; ++j)
        if ((*classes)[i]->name == (*classes)[j]->name)
          return {false, int32_t{0}, "duplicate class"};
    }
  if (!arena)
    return execute(&cls, tree, std::move(locals), 0, natives, classes,
                   staticFields, nullptr);
  arena->failureTrace.clear();
  VmScriptCall outer = std::move(arena->callScript);
  const auto *outerNatives = arena->natives;
  if (natives)
    arena->natives = natives;
  arena->callScript = makeScriptCall(natives, classes, staticFields, arena);
  const unsigned outerDepth = arena->nativeDepth;
  arena->nativeDepth = 0;
  auto result = execute(&cls, tree, std::move(locals), 0, natives, classes,
                        staticFields, arena);
  arena->nativeDepth = outerDepth;
  arena->callScript = std::move(outer);
  arena->natives = outerNatives;
  return result;
}
