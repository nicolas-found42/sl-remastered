#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class GameFile {
public:
	bool open(const std::filesystem::path& dataRoot, const std::filesystem::path& referenceRoot,
	          const std::string& name, std::string& error);
	std::optional<int32_t> readInt();
	std::optional<float> readFloat();

private:
	std::optional<uint32_t> readBits();
	std::vector<uint8_t> data_;
	size_t cursor_ = 0;
	size_t payloadEnd_ = 0;
};
