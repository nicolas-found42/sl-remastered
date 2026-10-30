#include "instance_ids.hpp"

#include <algorithm>

std::optional<uint32_t> InstanceIds::allocate() {
	if (free_.empty()) {
		if (next_ > 0xffff) return std::nullopt;
		const uint32_t end = std::min(next_ + 128, uint32_t{0x10000});
		for (; next_ < end; ++next_) free_.push_back(static_cast<uint16_t>(next_));
	}
	const uint16_t slot = free_.back();
	free_.pop_back();
	live_.set(slot);
	return 0xffff0000u | slot;
}

bool InstanceIds::release(uint32_t id) {
	if ((id & 0xffff0000u) != 0xffff0000u) return false;
	const auto slot = static_cast<uint16_t>(id);
	if (!slot || !live_.test(slot)) return false;
	free_.push_back(slot);
	live_.reset(slot);
	return true;
}
