#pragma once

#include "natives/registry.hpp"
#include "tufa.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

// Loads one stock script class so objects carry the real field names and types.
inline std::unique_ptr<TufaClass> loadStockClass(const std::string& name) {
	std::ifstream file("ref/raw/system/Scripts/lang/" + name + ".class", std::ios::binary);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	const auto parsed = tufaParse(bytes.data(), bytes.size());
	if (!parsed.ok || parsed.file.classes.empty()) { std::printf("cannot load %s.class\n", name.c_str()); return nullptr; }
	return std::make_unique<TufaClass>(parsed.file.classes[0]);
}

inline const VmObject* instantiate(VmArena& arena, const TufaClass& cls) {
	auto object = std::make_unique<VmObject>();
	object->runtimeClass = &cls;
	for (const auto& list : cls.fields) for (const auto& field : list) {
		if (field.words[0] & 8) continue;
		const auto& type = cls.constants[field.words[2]].text;
		VmValue value = type == "F" ? VmValue{0.0f} : type == "I" ? VmValue{int32_t{0}}
		                                                         : VmValue{static_cast<const VmObject*>(nullptr)};
		object->fields.push_back({cls.constants[field.words[1]].text, value, type});
	}
	const VmObject* result = object.get();
	arena.objects.push_back(std::move(object));
	return result;
}

inline const VmObject* addString(VmArena& arena, const TufaClass& stringClass, const std::string& text) {
	auto object = std::make_unique<VmObject>();
	object->runtimeClass = &stringClass;
	const VmObject* result = object.get();
	arena.objects.push_back(std::move(object));
	arena.strings.emplace_back(text, result);
	return result;
}

inline const std::string* textOf(const VmArena& arena, const NativeResult& result) {
	const auto* value = std::get_if<VmValue>(&result);
	if (!value) return nullptr;
	const auto* object = std::get_if<const VmObject*>(value);
	if (!object || !*object) return nullptr;
	for (const auto& entry : arena.strings) if (entry.second == *object) return &entry.first;
	return nullptr;
}

inline bool isError(const NativeResult& result) { return std::holds_alternative<std::string>(result); }
inline bool isNull(const NativeResult& result) {
	const auto* value = std::get_if<VmValue>(&result);
	const auto* object = value ? std::get_if<const VmObject*>(value) : nullptr;
	return object && !*object;
}
