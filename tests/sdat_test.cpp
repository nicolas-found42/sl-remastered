#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "sdat.hpp"

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

static void putPack(Bytes& b, uint32_t ordinal, const char* path) {
	put32(b, ordinal);
	put32(b, static_cast<uint32_t>(std::strlen(path) + 1));
	b.insert(b.end(), path, path + std::strlen(path) + 1);
}

static Bytes header(uint8_t type) {
	return {'S', 'D', 'A', 'T', 0x00, 0x01, type, 0x00};
}

// Type 2: three payload bytes, then a resource list with kind 7 (empty), kind 7 (4 bytes) and kind 5, then two packs.
static Bytes buildType2() {
	Bytes b = header(2);
	b.insert(b.end(), {1, 2, 3});
	Bytes trailer;
	put32(trailer, 3);
	put32(trailer, 1);
	put32(trailer, 7);
	put32(trailer, 0);
	put32(trailer, 2);
	put32(trailer, 7);
	put32(trailer, 4);
	trailer.insert(trailer.end(), {9, 9, 9, 9});
	put32(trailer, 3);
	put32(trailer, 5);
	put32(trailer, 0);
	put32(trailer, 2);
	putPack(trailer, 1, "cars.rpk");
	putPack(trailer, 2, "parts\\wheels.rpk");
	b.insert(b.end(), trailer.begin(), trailer.end());
	put32(b, static_cast<uint32_t>(trailer.size()));
	return b;
}

// Type 1: payload, then the pack list, then trailer length and pack count.
static Bytes buildType1() {
	Bytes b = header(1);
	b.insert(b.end(), {7, 7});
	Bytes trailer;
	putPack(trailer, 4, "a.rpk");
	b.insert(b.end(), trailer.begin(), trailer.end());
	put32(b, static_cast<uint32_t>(trailer.size()));
	put32(b, 1);
	return b;
}

static SdatResult parseCopy(const Bytes& src, size_t len) {
	Bytes exact(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(len));
	return sdatParse(exact.data(), exact.size());
}

int main() {
	const Bytes t2 = buildType2();
	const SdatResult r = parseCopy(t2, t2.size());
	CHECK(r.ok);
	CHECK(r.file.type == 2 && r.file.flags == 0 && r.file.payloadSize == 3);
	CHECK(r.file.unsaved.size() == 3);
	if (r.file.unsaved.size() == 3) {
		CHECK(r.file.unsaved[0].ordinal == 1 && r.file.unsaved[0].kind == 7 && r.file.unsaved[0].size == 0);
		CHECK(r.file.unsaved[1].ordinal == 2 && r.file.unsaved[1].size == 4);
		CHECK(t2[r.file.unsaved[1].blobOffset] == 9);
		CHECK(r.file.unsaved[2].kind == 5 && r.file.unsaved[2].size == 0);
	}
	CHECK(r.file.packs.size() == 2);
	if (r.file.packs.size() == 2) {
		CHECK(r.file.packs[0].ordinal == 1 && r.file.packs[0].path == "cars.rpk");
		CHECK(r.file.packs[1].path == "parts\\wheels.rpk");
	}
	CHECK(r.file.zeroOrdinals == 0 && r.file.duplicateOrdinals == 0 && r.file.largeOrdinals == 0);

	const Bytes t1 = buildType1();
	const SdatResult r1 = parseCopy(t1, t1.size());
	CHECK(r1.ok && r1.file.type == 1 && r1.file.payloadSize == 2);
	CHECK(r1.file.packs.size() == 1 && r1.file.packs[0].ordinal == 4 && r1.file.packs[0].path == "a.rpk");
	CHECK(r1.file.unsaved.empty());

	Bytes dup = t2;
	// Both pack ordinals become 0, which is one zero twice and one duplicate.
	const size_t firstPack = 8 + 3 + 4 + 12 + 12 + 4 + 12 + 4;
	for (size_t i = 0; i < 4; ++i) {
		dup[firstPack + i] = 0;
	}
	const SdatResult d = parseCopy(dup, dup.size());
	CHECK(d.ok && d.file.zeroOrdinals == 1);

	Bytes badMagic = t2;
	badMagic[0] = 'X';
	CHECK(!parseCopy(badMagic, badMagic.size()).ok);
	Bytes badVersion = t2;
	badVersion[5] = 2;
	CHECK(!parseCopy(badVersion, badVersion.size()).ok);
	Bytes badType = t2;
	badType[6] = 3;
	CHECK(!parseCopy(badType, badType.size()).ok);

	// A resource of a kind with no payload that claims one would desync the stock reader.
	Bytes kind5Size = t2;
	const size_t kind5Size_at = 8 + 3 + 4 + 12 + 12 + 4 + 8;
	kind5Size[kind5Size_at] = 1;
	CHECK(!parseCopy(kind5Size, kind5Size.size()).ok);

	// An unterminated pack string.
	Bytes noNul = t2;
	noNul[noNul.size() - 4 - 1] = 'x';
	CHECK(!parseCopy(noNul, noNul.size()).ok);

	for (size_t len = 0; len < t2.size(); ++len) {
		if (parseCopy(t2, len).ok) {
			std::printf("FAIL: type 2 prefix of %zu bytes parsed\n", len);
			++failures;
		}
	}
	for (size_t len = 0; len < t1.size(); ++len) {
		if (parseCopy(t1, len).ok) {
			std::printf("FAIL: type 1 prefix of %zu bytes parsed\n", len);
			++failures;
		}
	}
	for (size_t off = 0; off + 4 <= t2.size(); ++off) {
		Bytes flip = t2;
		flip[off] = flip[off + 1] = flip[off + 2] = flip[off + 3] = 0xff;
		(void)parseCopy(flip, flip.size());
	}
	CHECK(!sdatParse(nullptr, 0).ok);

	if (failures == 0) {
		std::puts("sdat_test ok");
	}
	return failures == 0 ? 0 : 1;
}
