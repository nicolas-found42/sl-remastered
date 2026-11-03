#include <cstddef>
#include <cstdint>
#include <cstdio>

#ifdef USE_PTR32
#include "ptr32.hpp"
#endif
#include "mid.hpp"

int main() {
	HostMidPayload m;
	bool tree = static_cast<void*>(m.bone_tree) == static_cast<void*>(m.bone_id_sentinel);
	bool head = static_cast<void*>(m.bone_own_head) == static_cast<void*>(m.bone_own_sentinel);
#ifdef USE_PTR32
	uint32_t slot;
	std::memcpy(&slot, m.bone_own_sentinel + 0x0C, 4);
	bool back = HandleTable::get(slot) == static_cast<void*>(m.bone_own_sentinel);
#else
	void* slot;
	std::memcpy(&slot, m.bone_own_sentinel + 0x0C, sizeof slot);
	bool back = slot == static_cast<void*>(m.bone_own_sentinel);
#endif
	std::printf("sizeof=0x%zx bone_tree_ok=%d own_head_ok=%d own_sentinel_backlink_ok=%d\n",
		sizeof(HostMidPayload), tree, head, back);
	return tree && head && back ? 0 : 1;
}
