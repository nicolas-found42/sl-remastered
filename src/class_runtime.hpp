#pragma once

#include "tufa.hpp"
#include "vm.hpp"

#include <functional>
#include <set>
#include <string>
#include <vector>

// Host side of stock's lazy class use. Stock loads a class when a script first names it (FUN_00410890) and
// initializes it on first use (FUN_00403e80). This owns the class registry and the static field table the VM runs
// against, loads a class with its parent and interfaces through a loader, and installs the VM's ensureClass hook.
// Loaded classes come from the loader and must outlive this object. Header only on purpose: the loader can be a
// ClassEnvironment or a test's table of synthetic classes.
class ClassRuntime {
public:
	using Loader = std::function<const TufaClass*(const std::string& name, std::string& error)>;

	ClassRuntime(Loader loader, VmArena& arena, const VmResourceResolver* resolver = nullptr)
		: loader_(std::move(loader)), arena_(arena), resolver_(resolver) {
		if (resolver_ && !arena_.resourceResolver) arena_.resourceResolver = resolver_;
		arena_.ensureClass = [this](const std::string& name) { return ensure(name); };
	}
	~ClassRuntime() { arena_.ensureClass = nullptr; }
	ClassRuntime(const ClassRuntime&) = delete;
	ClassRuntime& operator=(const ClassRuntime&) = delete;

	// The registry and the static state grow while scripts run. Pass these to vmExecute.
	const std::vector<const TufaClass*>& classes() const { return classes_; }
	std::vector<VmStaticFields>& staticFields() { return states_; }

	// Loads the class, its ancestors and its interfaces without initializing them.
	const TufaClass* load(const std::string& name, std::string& error) {
		std::set<std::string> active;
		return loadGraph(name, active, error);
	}

	// Loads the class if needed and initializes it unless it already is, or is initializing.
	VmResult ensure(const std::string& name) {
		std::string error;
		const TufaClass* cls = load(name, error);
		if (!cls) return {false, int32_t{0}, "class " + name + " could not be loaded: " + error};
		for (const auto& state : states_) if (state.owner == cls) return {true, int32_t{0}, {}};
		return vmInitializeClass(*cls, classes_, states_, resolver_, &arena_);
	}

private:
	const TufaClass* loadGraph(const std::string& name, std::set<std::string>& active, std::string& error) {
		if (active.count(name)) { error = "cyclic class graph: " + name; return nullptr; }
		const TufaClass* cls = loader_(name, error);
		if (!cls) { error = name + ": " + error; return nullptr; }
		if (!cls->hasClassHeader) { error = name + ": missing class header"; return nullptr; }
		for (const auto* known : classes_) if (known == cls) return cls;
		active.insert(name);
		const auto reference = [&](uint32_t index) {
			if (index >= cls->constants.size() || cls->constants[index].kind != 4) {
				error = name + ": invalid class reference"; return false;
			}
			const auto textIndex = cls->constants[index].a;
			if (textIndex >= cls->constants.size() || cls->constants[textIndex].kind != 0) {
				error = name + ": invalid class name reference"; return false;
			}
			return loadGraph(cls->constants[textIndex].text, active, error) != nullptr;
		};
		if (cls->classHeader[3] != UINT32_MAX && !reference(cls->classHeader[3])) { active.erase(name); return nullptr; }
		for (uint32_t index : cls->interfaces) if (!reference(index)) { active.erase(name); return nullptr; }
		active.erase(name);
		classes_.push_back(cls);
		return cls;
	}

	Loader loader_;
	VmArena& arena_;
	const VmResourceResolver* resolver_;
	std::vector<const TufaClass*> classes_;
	std::vector<VmStaticFields> states_;
};
