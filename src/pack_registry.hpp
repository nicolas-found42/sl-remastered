#pragma once

#include "rpk.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct PackResource {
	uint32_t id = 0;
	RpkEntry entry;
	const std::vector<uint8_t>* packBytes = nullptr;
};

// What FUN_00544170 builds from a pack file when it mounts a slot: the dependency table with each
// dependency already mapped to a global slot, and the entries in file order.
struct PackView {
	uint16_t slot = 0;
	std::string name;
	const RpkPack* pack = nullptr;
	const std::vector<uint8_t>* bytes = nullptr;
	std::vector<uint16_t> dependencies;
};
enum class PackViewStatus { Ok, FileMissing, Invalid };

class PackRegistry {
public:
	explicit PackRegistry(std::filesystem::path root);
	const std::filesystem::path& root() const { return root_; }
	// Stock FUN_00538380: find or add a pack name and return its slot. Does not open the file.
	bool registerPackName(const std::string& packName, uint16_t& slot, std::string& error);
	// Stock world+0xffe20: how many pack slots exist.
	size_t slotCount() const { return slots_.size(); }
	// Opens and parses the slot's file (registering its dependency names, as the stock mount does).
	PackViewStatus view(uint16_t slot, PackView& result, std::string& error);
	bool resolveLiteral(const std::string& packName, uint32_t serializedId, uint32_t& result, std::string& error);
	bool lookup(uint32_t runtimeId, PackResource& result, std::string& error);

private:
	struct Slot {
		std::string name;
		std::vector<uint8_t> bytes;
		RpkPack pack;
		std::vector<uint16_t> dependencies;
		bool loaded = false;
	};
	bool registerName(const std::string& name, uint16_t& index, std::string& error);
	enum class LoadStatus { Ok, Missing, Failed };
	LoadStatus loadSlot(uint16_t index, std::string& error);
	bool load(uint16_t index, std::string& error) { return loadSlot(index, error) == LoadStatus::Ok; }
	std::filesystem::path root_;
	std::vector<std::unique_ptr<Slot>> slots_;
};
