#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class GameFile {
public:
	bool open(const std::filesystem::path& dataRoot, const std::filesystem::path& referenceRoot,
	          const std::string& name, std::string& error);
	std::optional<int32_t> readInt();
	std::optional<float> readFloat();
	// Length-prefixed string payload (FUN_00486060): int length then that many bytes.
	std::optional<std::string> readString();
	// Pack table recorded in the trailer (ordinal -> pack name).
	const std::vector<std::pair<uint32_t, std::string>>& packs() const { return packs_; }

private:
	std::optional<uint32_t> readBits();
	std::vector<uint8_t> data_;
	size_t cursor_ = 0;
	size_t payloadEnd_ = 0;
	std::vector<std::pair<uint32_t, std::string>> packs_;
};
