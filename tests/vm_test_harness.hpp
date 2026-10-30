// Shared synthetic-class harness for the VM tests. One copy: new tests include
// this header instead of copying Cb and World, so a fixed helper fixes every
// test.
//
// Pool rule: a tree runs against ONE executing class (World::run passes
// w.main), and every operand indexes that class's constant pool. Build
// references with w.main.ref() / w.main.t() / w.main.cref() — never another
// Cb's pool. Cb::make and World::alloc already follow this.
#pragma once

#include "class_environment.hpp"
#include "class_runtime.hpp"
#include "vm.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <variant>

inline int failures = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);                      \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

namespace vm_test {

inline TufaNode node(uint8_t op, bool wide = false, uint32_t operand = 0) {
  return {op, 0, wide, operand};
}
inline TufaNode literal(int32_t value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return node(0x27, true, bits);
}
const TufaNode ret = node(0x2b);
const TufaNode retVoid = node(0x2a);
const TufaNode endTree = node(0x10);

// Builds one synthetic class with a constant pool, static fields, methods and
// trees.
struct Cb {
  TufaClass c;
  std::map<std::string, uint32_t> texts;

  explicit Cb(const std::string &name, const std::string &parent = {}) {
    c.name = name;
    c.hasClassHeader = true;
    t(name);
    c.classHeader[3] = parent.empty() ? UINT32_MAX : cref(parent);
  }
  uint32_t t(const std::string &s) {
    auto found = texts.find(s);
    if (found != texts.end())
      return found->second;
    c.constants.push_back({0, s, 0, 0});
    return texts[s] = static_cast<uint32_t>(c.constants.size() - 1);
  }
  uint32_t add(uint32_t kind, uint32_t a, uint32_t b) {
    c.constants.push_back({kind, {}, a, b});
    return static_cast<uint32_t>(c.constants.size() - 1);
  }
  uint32_t cref(const std::string &name) { return add(4, t(name), 0); }
  uint32_t ref(const std::string &owner, const std::string &name,
               const std::string &type) {
    return add(5, cref(owner), add(7, t(name), t(type)));
  }
  uint32_t tree(std::vector<TufaNode> nodes) {
    c.trees.push_back(std::move(nodes));
    return static_cast<uint32_t>(c.trees.size() - 1);
  }
  Cb &staticField(const std::string &name, const std::string &type,
                  std::vector<TufaNode> init) {
    c.fields[0].push_back(
        {{8, t(name), t(type),
          init.empty() ? UINT32_MAX : tree(std::move(init))}});
    return *this;
  }
  Cb &instanceField(const std::string &name, const std::string &type) {
    c.fields[0].push_back({{0, t(name), t(type), UINT32_MAX}});
    return *this;
  }
  Cb &instanceFieldInit(const std::string &name, const std::string &type,
                        std::vector<TufaNode> init) {
    c.fields[0].push_back({{0, t(name), t(type), tree(std::move(init))}});
    return *this;
  }
  Cb &implements(const std::string &name) {
    c.interfaces.push_back(cref(name));
    return *this;
  }
  Cb &interfaceKind() {
    c.classHeader[1] = 1;
    return *this;
  }
  // <init>(parameters) that stores `marker` into the int field `field` of
  // `this`, so a test can tell which overload ran.
  Cb &markingConstructor(const std::string &parameters, int32_t marker,
                         const std::string &field) {
    return method("<init>", parameters, 1,
                  {literal(marker), node(0x01, true, 0),
                   node(0x1c, true, t(field)), node(0x08, true, 35), retVoid});
  }
  Cb &method(const std::string &name, const std::string &descriptor,
             uint32_t flags, std::vector<TufaNode> body) {
    c.methods[0].push_back(
        {{flags, t(name), t(descriptor), tree(std::move(body)), 0}});
    return *this;
  }
  Cb &constructor() { return method("<init>", "()", 1, {retVoid}); }
  // new <name>() leaving the object on the stack. The 0x06 operand indexes a
  // kind-0 text constant holding the descriptor directly (checked by the
  // allocation path in src/vm.cpp).
  std::vector<TufaNode> make(const std::string &name) {
    return {node(0x07, true, 30), node(0x06, true, t("L" + name + ";")),
            literal(0), node(0x21)};
  }
  // The value of Owner.name, read through a qualified field access.
  std::vector<TufaNode> read(const std::string &owner,
                             const std::string &name) {
    return {node(0x11, true, 2), node(0x19, true, cref(owner)),
            node(0x1b, true, ref(owner, name, "I"))};
  }
};

inline std::vector<TufaNode> concat(std::vector<TufaNode> a,
                                    const std::vector<TufaNode> &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

inline const VmObject *asObject(const VmValue &value) {
  const VmObject *const *p = std::get_if<const VmObject *>(&value);
  return p ? *p : nullptr;
}

inline VmField *fieldOf(const VmObject *object, const std::string &name) {
  if (!object)
    return nullptr;
  for (auto &f : object->fields)
    if (f.name == name)
      return &f;
  return nullptr;
}

inline int32_t intField(const VmStaticFields *s, const std::string &name) {
  if (s)
    for (const auto &f : s->fields)
      if (f.name == name) {
        if (const auto *v = std::get_if<int32_t>(&f.value))
          return *v;
      }
  return -12345;
}

inline const VmObject *objectField(const VmStaticFields *s,
                                   const std::string &name) {
  if (s)
    for (const auto &f : s->fields)
      if (f.name == name) {
        if (const auto *v = std::get_if<const VmObject *>(&f.value))
          return *v;
      }
  return nullptr;
}

inline bool contains(const std::string &text, const std::string &part) {
  return text.find(part) != std::string::npos;
}

struct World {
  std::map<std::string, const TufaClass *> table;
  std::vector<std::unique_ptr<Cb>> owned;
  std::vector<std::string> loadLog;
  VmArena arena;
  std::unique_ptr<ClassRuntime> runtime;
  Cb main{"test.Main"};

  Cb &cls(const std::string &name, const std::string &parent = {}) {
    owned.push_back(std::make_unique<Cb>(name, parent));
    table[name] = &owned.back()->c;
    return *owned.back();
  }
  void start() {
    runtime = std::make_unique<ClassRuntime>(
        [this](const std::string &name,
               std::string &error) -> const TufaClass * {
          loadLog.push_back(name);
          const auto found = table.find(name);
          if (found == table.end()) {
            error = "no such class in this test";
            return nullptr;
          }
          return found->second;
        },
        arena);
  }
  VmResult run(std::vector<TufaNode> body, std::vector<VmValue> locals = {}) {
    return vmExecute(main.c, body, std::move(locals), nullptr,
                     &runtime->classes(), &runtime->staticFields(), &arena);
  }
  // Allocates a zero-argument instance of `name`, leaving the object as the
  // tree result.
  VmResult alloc(const std::string &name) {
    return run(concat(main.make(name), {ret}));
  }
  const VmStaticFields *state(const std::string &name) {
    for (const auto &s : runtime->staticFields())
      if (s.owner->name == name)
        return &s;
    return nullptr;
  }
  bool registered(const std::string &name) {
    for (const auto *c : runtime->classes())
      if (c->name == name)
        return true;
    return false;
  }
  int loads(const std::string &name) {
    int n = 0;
    for (const auto &l : loadLog)
      if (l == name)
        ++n;
    return n;
  }
};

} // namespace vm_test
