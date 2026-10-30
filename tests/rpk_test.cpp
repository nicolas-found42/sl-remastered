#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "rpk.hpp"

static int failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
			++failures; \
		} \
	} while (0)

using Bytes = std::vector<uint8_t>;

static void put32(Bytes& b, uint32_t v) {
	for (int i = 0; i < 4; ++i) {
		b.push_back(static_cast<uint8_t>(v >> (8 * i)));
	}
}

static void putF(Bytes& b, float f) {
	uint32_t bits = 0;
	std::memcpy(&bits, &f, sizeof bits);
	put32(b, bits);
}

static void putRecord(Bytes& b, uint32_t superId, uint32_t typeId, uint8_t flags, uint32_t off, uint32_t size,
	const char* alias) {
	put32(b, superId);
	put32(b, typeId);
	b.push_back(7);
	b.push_back(flags);
	putF(b, 1.5f);
	put32(b, off);
	put32(b, size);
	const size_t n = std::strlen(alias) + 1;
	b.push_back(static_cast<uint8_t>(n));
	for (size_t i = 0; i < n; ++i) {
		b.push_back(static_cast<uint8_t>(alias[i]));
	}
	if (flags & 1) {
		for (int i = 0; i < 12; ++i) {
			putF(b, static_cast<float>(i));
		}
	}
}

struct Built {
	Bytes bytes;
	size_t tableStart = 0;
	size_t tableEnd = 0;
};

// Records: A in range, B overlaps the table and has a transform, C out of range.
static Built build(uint32_t tableSizeDelta = 0, uint32_t recordCount = 3) {
	Built out;
	Bytes& b = out.bytes;
	b = {'R', 'P', 'A', 'K'};
	put32(b, 0x200);
	put32(b, 1);
	put32(b, 0);
	put32(b, 0x00020001);
	char path[56] = {};
	std::strcpy(path, "cars.rpk");
	b.insert(b.end(), path, path + 56);
	put32(b, 99);

	Bytes table;
	const size_t headerBytes = b.size() + 16;
	const size_t fixedTable = 3 * 23 + 2 + 4 + 5 + 48;
	const uint32_t payloadAt = static_cast<uint32_t>(headerBytes + fixedTable);
	putRecord(table, 1, 2, 0, payloadAt, 8, "A");
	putRecord(table, 3, 4, 1, static_cast<uint32_t>(headerBytes + 2), 4, "BBB");
	putRecord(table, 5, 6, 0, payloadAt + 4, 100, "CCCC");

	put32(b, static_cast<uint32_t>(table.size()) + tableSizeDelta);
	put32(b, recordCount);
	put32(b, 0xAAAA);
	put32(b, 0xBBBB);
	out.tableStart = b.size();
	b.insert(b.end(), table.begin(), table.end());
	out.tableEnd = b.size();
	for (int i = 0; i < 8; ++i) {
		b.push_back(static_cast<uint8_t>(i));
	}
	return out;
}

// Each parse gets an exact-size heap copy so the sanitizer sees any read past the end.
static RpkResult parseCopy(const Bytes& src, size_t len) {
	Bytes exact(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(len));
	return rpkParse(exact.data(), exact.size());
}

int main() {
	const Built good = build();
	const RpkResult r = parseCopy(good.bytes, good.bytes.size());
	CHECK(r.ok);
	CHECK(r.pack.version == 0x200);
	CHECK(r.pack.serviceA == 0xAAAA && r.pack.serviceB == 0xBBBB);
	CHECK(r.pack.dependencies.size() == 1);
	if (r.pack.dependencies.size() == 1) {
		CHECK(r.pack.dependencies[0].id == 0x00020001);
		CHECK(r.pack.dependencies[0].path == "cars.rpk");
		CHECK(r.pack.dependencies[0].trailer == 99);
	}
	CHECK(r.pack.entries.size() == 3);
	if (r.pack.entries.size() == 3) {
		const RpkEntry& a = r.pack.entries[0];
		CHECK(a.superId == 1 && a.typeId == 2 && a.resType == 7 && a.flags == 0 && a.compat == 1.5f);
		CHECK(a.payloadOffset == good.tableEnd && a.payloadSize == 8 && a.alias == "A" && !a.transform);
		const RpkEntry& b = r.pack.entries[1];
		CHECK(b.superId == 3 && b.typeId == 4 && b.flags == 1 && b.alias == "BBB");
		CHECK(b.payloadOffset == 16 + 64 + 16 + 2 && b.payloadSize == 4);
		CHECK(b.transform.has_value());
		if (b.transform) {
			for (int i = 0; i < 12; ++i) {
				CHECK((*b.transform)[static_cast<size_t>(i)] == static_cast<float>(i));
			}
		}
		const RpkEntry& c = r.pack.entries[2];
		CHECK(c.superId == 5 && c.payloadSize == 100 && c.alias == "CCCC" && !c.transform);
	}
	CHECK(r.pack.payloadsOutOfRange == 1);
	CHECK(r.pack.payloadsOverlappingTable == 1);

	Bytes badMagic = good.bytes;
	badMagic[0] = 'X';
	CHECK(!parseCopy(badMagic, badMagic.size()).ok);

	Bytes badVersion = good.bytes;
	badVersion[4] = 0x01;
	CHECK(!parseCopy(badVersion, badVersion.size()).ok);

	for (size_t len = 0; len < good.tableEnd; ++len) {
		const RpkResult t = parseCopy(good.bytes, len);
		if (t.ok) {
			std::printf("FAIL: prefix of %zu bytes parsed\n", len);
			++failures;
		}
	}

	const Built big = build(1);
	CHECK(!parseCopy(big.bytes, big.bytes.size()).ok);
	const Built small = build(static_cast<uint32_t>(-1));
	CHECK(!parseCopy(small.bytes, small.bytes.size()).ok);
	const Built hugeCount = build(0, 0xFFFFFFFFu);
	CHECK(!parseCopy(hugeCount.bytes, hugeCount.bytes.size()).ok);

	Bytes hugeDeps = good.bytes;
	hugeDeps[8] = hugeDeps[9] = hugeDeps[10] = hugeDeps[11] = 0xFF;
	hugeDeps[12] = hugeDeps[13] = hugeDeps[14] = hugeDeps[15] = 0xFF;
	CHECK(!parseCopy(hugeDeps, hugeDeps.size()).ok);

	CHECK(!rpkParse(nullptr, 0).ok);

	if (failures == 0) {
		std::puts("rpk_test ok");
	}
	return failures == 0 ? 0 : 1;
}
