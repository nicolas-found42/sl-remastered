#include "instance_ids.hpp"

#include <bitset>
#include <cstdio>
#include <cstdint>
#include <vector>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

int main() {
	InstanceIds ids;
	CHECK(ids.allocate() == 0xffff0080u);
	CHECK(ids.allocate() == 0xffff007fu);
	CHECK(ids.release(0xffff0080u));
	CHECK(!ids.release(0xffff0080u));
	CHECK(!ids.release(0x00000080u));
	CHECK(!ids.release(0xffff0000u));
	CHECK(!ids.release(0xffff0081u));
	CHECK(ids.allocate() == 0xffff0080u);
	for (uint32_t slot = 0x7e; slot > 0; --slot)
		CHECK(ids.allocate() == (0xffff0000u | slot));
	CHECK(ids.allocate() == 0xffff0100u);
	CHECK(ids.release(0xffff0001u));
	CHECK(ids.release(0xffff0002u));
	CHECK(ids.allocate() == 0xffff0002u);
	CHECK(ids.allocate() == 0xffff0001u);

	InstanceIds exhausted;
	std::vector<uint32_t> allocated;
	std::bitset<65536> seen;
	for (uint32_t i = 0; i < 65535; ++i) {
		const auto id = exhausted.allocate();
		CHECK(id.has_value());
		if (!id) break;
		allocated.push_back(*id);
		const auto slot = static_cast<uint16_t>(*id);
		CHECK(!seen.test(slot));
		seen.set(slot);
		CHECK((*id & 0xffff0000u) == 0xffff0000u && (*id & 0xffffu) != 0);
	}
	CHECK(allocated.size() == 65535);
	CHECK(!exhausted.allocate());
	CHECK(exhausted.release(0xffffffffu));
	CHECK(exhausted.allocate() == 0xffffffffu);
	CHECK(!exhausted.allocate());
	return failures ? 1 : 0;
}
