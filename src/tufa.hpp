#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// kind 0 holds text. Kinds 1, 2 and 4 hold one word in a. Kinds 3, 5 and 7 hold two words in a and b.
struct TufaConstant {
	uint32_t kind = 0;
	std::string text;
	uint32_t a = 0;
	uint32_t b = 0;
};

struct TufaField {
	std::array<uint32_t, 4> words{};
};

// words are flags, name constant, descriptor constant, tree index, local count.
struct TufaMethod {
	std::array<uint32_t, 5> words{};
};

struct TufaNode {
	uint8_t op = 0;
	uint16_t source = 0;
	bool wide = false;
	uint32_t operand = 0;
};

struct TufaClass {
	uint32_t version = 0;
	std::string chunkOrder;
	std::vector<TufaConstant> constants;
	// The stock loader reads two lists of each record kind, one after the other.
	std::array<std::vector<TufaField>, 2> fields;
	std::array<std::vector<TufaMethod>, 2> methods;
	std::array<uint32_t, 5> classHeader{};
	std::vector<uint32_t> interfaces;
	std::vector<std::vector<TufaNode>> trees;
	bool hasClassHeader = false;
	std::string name;
};

struct TufaFile {
	std::vector<TufaClass> classes;
};

struct TufaResult {
	bool ok = false;
	TufaFile file;
	std::string error;
	uint64_t errorOffset = 0;
	// Version of the first TUFA chunk, set even when the parse fails after it.
	bool hasFirstVersion = false;
	uint32_t firstVersion = 0;
	// True when the failure is a constant of a kind the stock constant pool reader rejects.
	bool unknownConstantKind = false;
};

TufaResult tufaParse(const uint8_t* data, size_t size);
TufaResult tufaLoadFile(const std::string& path);
