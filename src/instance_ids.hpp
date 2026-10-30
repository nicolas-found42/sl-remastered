#pragma once

#include <bitset>
#include <cstdint>
#include <optional>
#include <vector>

class InstanceIds {
public:
	InstanceIds() = default;
	InstanceIds(const InstanceIds&) = delete;
	InstanceIds& operator=(const InstanceIds&) = delete;
	std::optional<uint32_t> allocate();
	bool release(uint32_t id);

private:
	uint32_t next_ = 1;
	std::vector<uint16_t> free_;
	std::bitset<65536> live_;
};
