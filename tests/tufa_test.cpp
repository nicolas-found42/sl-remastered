#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "tufa.hpp"

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

static void putTag(Bytes& b, const char* tag) {
	b.insert(b.end(), tag, tag + 4);
}

static void putChunk(Bytes& b, const char* tag, const Bytes& payload) {
	putTag(b, tag);
	put32(b, static_cast<uint32_t>(payload.size()));
	b.insert(b.end(), payload.begin(), payload.end());
}

static void putString(Bytes& b, const char* s) {
	put32(b, 0);
	put32(b, static_cast<uint32_t>(std::strlen(s)));
	b.insert(b.end(), s, s + std::strlen(s));
	b.push_back(0);
}

struct Built {
	Bytes bytes;
	std::set<size_t> chunkEnds;
};

// One class: constants [0] "LFoo;", [1] "LBar;", [2] class ref to 0, [3] class ref to 1, [4] kind 3 pair, [5] a last
// constant of kind 2, or of the unknownKind given. The class header names constant 2, so the class is "LFoo;".
// One field in each field list, two methods in the first method list, one interface, one tree of three nodes.
static Built build(uint32_t version = 0x11ae1, uint32_t unknownKind = 0, uint8_t treeOp = 0x07) {
	Built out;
	Bytes& b = out.bytes;
	auto mark = [&] { out.chunkEnds.insert(b.size()); };
	putChunk(b, "TUFA", [&] { Bytes p; put32(p, version); return p; }());
	mark();

	Bytes cons;
	put32(cons, 6);
	putString(cons, "LFoo;");
	putString(cons, "LBar;");
	put32(cons, 4);
	put32(cons, 0);
	put32(cons, 4);
	put32(cons, 1);
	put32(cons, 3);
	put32(cons, 1);
	put32(cons, 77);
	put32(cons, unknownKind != 0 ? unknownKind : 2);
	put32(cons, 9);
	putChunk(b, "CONS", cons);
	mark();

	Bytes fild;
	put32(fild, 1);
	for (uint32_t i = 1; i <= 4; ++i) {
		put32(fild, i);
	}
	put32(fild, 1);
	for (uint32_t i = 5; i <= 8; ++i) {
		put32(fild, i);
	}
	putChunk(b, "FILD", fild);
	mark();

	Bytes mthd;
	put32(mthd, 2);
	for (uint32_t i = 0; i < 10; ++i) {
		put32(mthd, 100 + i);
	}
	put32(mthd, 0);
	putChunk(b, "MTHD", mthd);
	mark();

	Bytes clss;
	put32(clss, 0);
	put32(clss, 0);
	put32(clss, 2);
	put32(clss, 3);
	put32(clss, 1);
	put32(clss, 42);
	putChunk(b, "CLSS", clss);
	mark();

	Bytes tree;
	put32(tree, 1);
	put32(tree, 3);
	tree.push_back(treeOp);
	tree.push_back(1);
	tree.push_back(0);
	put32(tree, 0xdead);
	tree.push_back(0x28);
	tree.push_back(2);
	tree.push_back(0);
	tree.push_back(0x04);
	tree.push_back(3);
	tree.push_back(0);
	putChunk(b, "TREE", tree);
	mark();
	return out;
}

static TufaResult parseCopy(const Bytes& src, size_t len) {
	Bytes exact(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(len));
	return tufaParse(exact.data(), exact.size());
}

int main() {
	const Built good = build();
	const TufaResult r = parseCopy(good.bytes, good.bytes.size());
	CHECK(r.ok);
	CHECK(r.file.classes.size() == 1);
	if (r.file.classes.size() == 1) {
		const TufaClass& c = r.file.classes[0];
		CHECK(c.version == 0x11ae1);
		CHECK(c.chunkOrder == "TCFMCT");
		CHECK(c.name == "LFoo;");
		CHECK(c.constants.size() == 6);
		CHECK(c.fields[0].size() == 1 && c.fields[1].size() == 1);
		if (c.fields[0].size() == 1 && c.fields[1].size() == 1) {
			CHECK(c.fields[0][0].words[0] == 1 && c.fields[0][0].words[3] == 4);
			CHECK(c.fields[1][0].words[0] == 5 && c.fields[1][0].words[3] == 8);
		}
		CHECK(c.methods[0].size() == 2 && c.methods[1].empty());
		if (c.methods[0].size() == 2) {
			CHECK(c.methods[0][0].words[0] == 100 && c.methods[0][0].words[4] == 104);
			CHECK(c.methods[0][1].words[0] == 105 && c.methods[0][1].words[4] == 109);
		}
		CHECK(c.hasClassHeader && c.classHeader[2] == 2 && c.classHeader[3] == 3 && c.classHeader[4] == 1);
		CHECK(c.interfaces.size() == 1 && c.interfaces[0] == 42);
		CHECK(c.trees.size() == 1 && c.trees[0].size() == 3);
		if (c.trees.size() == 1 && c.trees[0].size() == 3) {
			const std::vector<TufaNode>& t = c.trees[0];
			CHECK(t[0].op == 0x07 && t[0].wide && t[0].source == 1 && t[0].operand == 0xdead);
			CHECK(t[1].op == 0x28 && !t[1].wide && t[1].source == 2);
			CHECK(t[2].op == 0x04 && !t[2].wide && t[2].source == 3);
		}
	}

	// A file holds several classes, one after another.
	Bytes twice = good.bytes;
	twice.insert(twice.end(), good.bytes.begin(), good.bytes.end());
	const TufaResult two = parseCopy(twice, twice.size());
	CHECK(two.ok && two.file.classes.size() == 2);

	// The stock constant reader rejects a kind it does not know. The failure says so and keeps the version.
	const Built oldFormat = build(0x108f5, 10);
	const TufaResult bad = parseCopy(oldFormat.bytes, oldFormat.bytes.size());
	CHECK(!bad.ok && bad.unknownConstantKind && bad.hasFirstVersion && bad.firstVersion == 0x108f5);

	// Kind 6 is also rejected by the stock reader.
	const Built kind6 = build(0x11ae1, 6);
	const TufaResult k6 = parseCopy(kind6.bytes, kind6.bytes.size());
	CHECK(!k6.ok && k6.unknownConstantKind);

	// Ops past the stock width table are refused.
	const Built wideOp = build(0x11ae1, 0, 0x2f);
	CHECK(!parseCopy(wideOp.bytes, wideOp.bytes.size()).ok);

	// A chunk before the constant pool is refused.
	Bytes noCons;
	putChunk(noCons, "TUFA", [] { Bytes p; put32(p, 1); return p; }());
	Bytes tree;
	put32(tree, 0);
	putChunk(noCons, "TREE", tree);
	CHECK(!parseCopy(noCons, noCons.size()).ok);

	// An unknown tag is refused, as the stock loader reports an error for it.
	Bytes unknownTag = good.bytes;
	putChunk(unknownTag, "WHAT", Bytes{1, 2, 3, 4});
	CHECK(!parseCopy(unknownTag, unknownTag.size()).ok);

	// Cutting the file anywhere except on a chunk boundary must fail, never read past the end.
	for (size_t len = 0; len < good.bytes.size(); ++len) {
		const bool boundary = len == 0 || good.chunkEnds.count(len) != 0;
		const TufaResult t = parseCopy(good.bytes, len);
		if (!boundary && t.ok) {
			std::printf("FAIL: prefix of %zu bytes parsed\n", len);
			++failures;
		}
	}

	// A chunk size larger than the file, and counts larger than their chunk.
	Bytes hugeChunk = good.bytes;
	hugeChunk[16] = hugeChunk[17] = hugeChunk[18] = hugeChunk[19] = 0xff;
	CHECK(!parseCopy(hugeChunk, hugeChunk.size()).ok);
	for (size_t off = 0; off + 4 <= good.bytes.size(); ++off) {
		Bytes flip = good.bytes;
		flip[off] = 0xff;
		flip[off + 1] = 0xff;
		flip[off + 2] = 0xff;
		flip[off + 3] = 0xff;
		(void)parseCopy(flip, flip.size());
	}

	CHECK(!tufaParse(nullptr, 0).ok);

	if (failures == 0) {
		std::puts("tufa_test ok");
	}
	return failures == 0 ? 0 : 1;
}
