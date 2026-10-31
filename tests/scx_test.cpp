#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "scx.hpp"

static int failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
			++failures; \
		} \
	} while (0)

using Bytes = std::vector<uint8_t>;

static void put16(Bytes& b, uint16_t v) {
	b.push_back(static_cast<uint8_t>(v));
	b.push_back(static_cast<uint8_t>(v >> 8));
}

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

static void putName(Bytes& b, const char* name) {
	char buf[32] = {};
	std::strncpy(buf, name, sizeof buf);
	b.insert(b.end(), buf, buf + 32);
}

static void set32(Bytes& b, size_t at, uint32_t v) {
	for (int i = 0; i < 4; ++i) {
		b[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
	}
}

static void set16(Bytes& b, size_t at, uint16_t v) {
	b[at] = static_cast<uint8_t>(v);
	b[at + 1] = static_cast<uint8_t>(v >> 8);
}

// Each parse gets an exact-size heap copy so the sanitizer sees any read past the end.
static ScxResult parseCopy(const Bytes& src, size_t len) {
	Bytes exact(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(len));
	return scxParse(exact.data(), exact.size());
}

static ScxResult parseAll(const Bytes& src) {
	return parseCopy(src, src.size());
}

static Bytes header(uint32_t version) {
	Bytes b = {'I', 'N', 'V', 'O'};
	put32(b, version);
	return b;
}

struct V3Mat {
	uint32_t size = 56;
	uint32_t stride = 44;
	uint32_t flags = 0;
	uint16_t tex[5] = {0xffff, 0xffff, 0xffff, 0xffff, 0xffff};
	uint16_t diffuseChannel = 0;
	uint16_t layer2Channel = 0;
	const char* name = "";
	uint32_t vertexCount = 3;
	uint32_t triangleCount = 1;
};

// Returns the offset where the next material block starts.
static size_t putV3Material(Bytes& b, const V3Mat& m) {
	const size_t start = b.size();
	b.resize(start + m.size, 0);
	set32(b, start, m.size);
	const float diffuse[4] = {0.25f, 0.5f, 0.75f, 1.0f};
	for (size_t i = 0; i < 4; ++i) {
		uint32_t bits = 0;
		std::memcpy(&bits, &diffuse[i], sizeof bits);
		set32(b, start + 4 + i * 4, bits);
	}
	set32(b, start + 40, m.flags);
	for (size_t i = 0; i < 5; ++i) {
		set16(b, start + 44 + i * 2, m.tex[i]);
	}
	if (m.size > 56) {
		set32(b, start + 60, m.stride);
		set16(b, start + 70, m.diffuseChannel);
		set16(b, start + 72, m.layer2Channel);
	}
	if (m.size == 136) {
		std::strncpy(reinterpret_cast<char*>(b.data() + start + 104), m.name, 32);
	}
	put32(b, m.vertexCount);
	for (uint32_t v = 0; v < m.vertexCount; ++v) {
		const float f = static_cast<float>(v);
		putF(b, f);
		putF(b, f + 0.1f);
		putF(b, f + 0.2f);
		putF(b, 0.0f);
		putF(b, 1.0f);
		putF(b, 0.0f);
		putF(b, f + 0.3f);
		putF(b, f + 0.4f);
		putF(b, f + 0.5f);
		putF(b, f + 0.6f);
		for (uint8_t c = 1; c <= 4; ++c) {
			b.push_back(static_cast<uint8_t>(v * 16 + c));
		}
		if (m.stride == 64 && m.size > 56) {
			for (int i = 0; i < 8; ++i) {
				b.push_back(0xEE);
			}
			putF(b, f + 0.7f);
			putF(b, f + 0.8f);
			for (int i = 0; i < 4; ++i) {
				b.push_back(0xEE);
			}
		}
	}
	put32(b, m.triangleCount);
	for (uint32_t t = 0; t < m.triangleCount; ++t) {
		put32(b, 0);
		put32(b, 1);
		put32(b, 2);
	}
	return b.size();
}

struct Chunk {
	uint32_t type;
	Bytes payload;
};

static Bytes materialPayload() {
	Bytes p;
	put32(p, 0xA1);
	put32(p, 0xB2);
	put32(p, 5);
	put32(p, 0);
	p.insert(p.end(), {255, 0, 51, 255});
	put32(p, 0x06000001);
	put32(p, 7);
	put32(p, 1);
	put32(p, 2);
	putF(p, 2.0f);
	putF(p, 3.0f);
	putF(p, 0.5f);
	putF(p, 0.25f);
	put32(p, 0x01000001);
	putF(p, 1.5f);
	put32(p, 0x05000000);
	put32(p, 99);
	put32(p, 0x08000000);
	putName(p, "paint");
	return p;
}

static Bytes vertexPayload(uint32_t count, uint32_t flags, const Bytes& records) {
	Bytes p;
	put32(p, count);
	put32(p, flags);
	p.insert(p.end(), records.begin(), records.end());
	return p;
}

constexpr uint32_t kFullFlags = 0x1 | 0x4 | 0x20 | 0x40 | 0x200 | 0x400 | 0x800 | 0x40000;

static Bytes fullVertexRecords() {
	Bytes r;
	for (uint32_t v = 0; v < 2; ++v) {
		const float f = static_cast<float>(v) * 100.0f;
		putF(r, f + 1);
		putF(r, f + 2);
		putF(r, f + 3);
		r.insert(r.end(), {static_cast<uint8_t>(v + 10), static_cast<uint8_t>(v + 11), static_cast<uint8_t>(v + 12),
			static_cast<uint8_t>(v + 13)});
		r.insert(r.end(), {static_cast<uint8_t>(v + 20), static_cast<uint8_t>(v + 21), static_cast<uint8_t>(v + 22),
			static_cast<uint8_t>(v + 23)});
		putF(r, f + 4);
		putF(r, f + 5);
		putF(r, f + 6);
		putF(r, f + 7);
		putF(r, f + 8);
		putF(r, f + 9);
		putF(r, f + 10);
		putF(r, f + 11);
		putF(r, f + 12);
		putF(r, f + 13);
		putF(r, f + 14);
		putF(r, f + 15);
	}
	return r;
}

static Bytes indexPayload(const std::vector<uint16_t>& idx) {
	Bytes p;
	put32(p, static_cast<uint32_t>(idx.size()));
	for (uint16_t i : idx) {
		put16(p, i);
	}
	return p;
}

static Bytes simpleVertexPayload(float x = 0.0f) {
	Bytes rec;
	for (int v = 0; v < 3; ++v) {
		putF(rec, v == 0 ? x : 1.0f);
		putF(rec, 2.0f);
		putF(rec, 3.0f);
	}
	return vertexPayload(3, 0x1, rec);
}

static Bytes v4File(const std::vector<Chunk>& chunks, int offsetSkew = 0, bool dropDirectoryEntry = false) {
	Bytes b = header(4);
	uint32_t dirCount = static_cast<uint32_t>(chunks.size()) - (dropDirectoryEntry ? 1 : 0);
	put32(b, dirCount);
	uint32_t off = static_cast<uint32_t>(12 + 8 * dirCount);
	std::vector<uint32_t> starts;
	for (const Chunk& c : chunks) {
		starts.push_back(off);
		off += 8 + static_cast<uint32_t>(c.payload.size());
	}
	for (uint32_t i = 0; i < dirCount; ++i) {
		put32(b, chunks[i].type);
		put32(b, starts[i] + (i == 0 ? static_cast<uint32_t>(offsetSkew) : 0));
	}
	for (const Chunk& c : chunks) {
		put32(b, c.type);
		put32(b, 8 + static_cast<uint32_t>(c.payload.size()));
		b.insert(b.end(), c.payload.begin(), c.payload.end());
	}
	return b;
}

static std::vector<Chunk> goodChunks() {
	return {{0, materialPayload()},
		{2, {1, 2, 3, 4, 5}},
		{4, vertexPayload(2, kFullFlags, fullVertexRecords())},
		{5, indexPayload({0, 1, 0})}};
}

static void testV3() {
	Bytes minimal = header(3);
	put32(minimal, 0);
	ScxResult r = parseAll(minimal);
	CHECK(r.ok && r.model.version == 3 && r.model.materials.empty() && r.model.meshes.empty());
	CHECK(r.model.trailingBytes == 0);
	r = parseAll(header(3));
	CHECK(r.ok && r.model.meshes.empty());

	Bytes small = header(3);
	V3Mat sm;
	sm.flags = 0x1234;
	sm.tex[0] = 4;
	sm.tex[2] = 9;
	const size_t smallEnd = putV3Material(small, sm);
	Bytes big = small;
	V3Mat bm;
	bm.size = 136;
	bm.stride = 64;
	bm.flags = 0x77;
	bm.tex[0] = 1;
	bm.tex[4] = 3;
	bm.diffuseChannel = 2;
	bm.layer2Channel = 5;
	bm.name = "body";
	bm.vertexCount = 2;
	bm.triangleCount = 2;
	const size_t bigEnd = putV3Material(big, bm);
	put32(big, 0);

	r = parseAll(big);
	CHECK(r.ok);
	CHECK(r.model.materials.size() == 2 && r.model.meshes.size() == 2);
	if (r.model.materials.size() == 2 && r.model.meshes.size() == 2) {
		const ScxMaterial& a = r.model.materials[0];
		CHECK(a.name.empty() && a.flags == 0x1234);
		CHECK(a.diffuse[0] == 0.25f && a.diffuse[1] == 0.5f && a.diffuse[2] == 0.75f && a.diffuse[3] == 1.0f);
		CHECK(a.maps.size() == 2);
		if (a.maps.size() == 2) {
			CHECK(a.maps[0].slot == 0 && a.maps[0].textureIndex == 4 && a.maps[0].channel == 0);
			CHECK(a.maps[0].tiling == 0 && a.maps[0].scaleU == 1.0f && a.maps[0].offsetV == 0.0f);
			CHECK(a.maps[1].slot == 2 && a.maps[1].textureIndex == 9);
		}
		const ScxMesh& m = r.model.meshes[0];
		CHECK(m.materialIndex && *m.materialIndex == 0);
		CHECK(m.vertexFlags == (0x1 | 0x40 | 0x100 | 0x200 | 0x400));
		CHECK(m.vertexCount == 3 && m.positions.size() == 9 && m.normals.size() == 9);
		CHECK(m.uv1.size() == 6 && m.uv2.size() == 6 && m.uv3.empty() && m.color.size() == 12);
		CHECK(m.positions[3] == 1.0f && m.positions[4] == 1.1f && m.positions[5] == 1.2f);
		CHECK(m.normals[4] == 1.0f);
		CHECK(m.uv1[2] == 1.3f && m.uv1[3] == 1.4f && m.uv2[2] == 1.5f && m.uv2[3] == 1.6f);
		CHECK(m.color[4] == 17 && m.color[7] == 20);
		CHECK((m.indices == std::vector<uint32_t>{0, 1, 2}));

		const ScxMaterial& b = r.model.materials[1];
		CHECK(b.name == "body" && b.flags == 0x77);
		CHECK(b.maps.size() == 2);
		if (b.maps.size() == 2) {
			CHECK(b.maps[0].slot == 0 && b.maps[0].textureIndex == 1 && b.maps[0].channel == 2);
			CHECK(b.maps[1].slot == 4 && b.maps[1].textureIndex == 3 && b.maps[1].channel == 5);
		}
		const ScxMesh& n = r.model.meshes[1];
		CHECK(n.materialIndex && *n.materialIndex == 1);
		CHECK(n.vertexFlags == (0x1 | 0x40 | 0x100 | 0x200 | 0x400 | 0x800));
		CHECK(n.vertexCount == 2 && n.uv3.size() == 4);
		CHECK(n.uv3[0] == 0.7f && n.uv3[1] == 0.8f && n.uv3[2] == 1.7f && n.uv3[3] == 1.8f);
		CHECK(n.color.size() == 8 && n.color[4] == 17);
		CHECK(n.indices.size() == 6);
	}
	CHECK(r.model.trailingBytes == 0 && r.model.incompleteTriangles == 0 && r.model.nonFinitePositions == 0);
	CHECK(r.model.indicesOutOfRange == 2);

	for (size_t len = 0; len < big.size(); ++len) {
		const ScxResult t = parseCopy(big, len);
		const bool boundary = len == 8 || len == smallEnd || len == bigEnd;
		if (t.ok != boundary) {
			std::printf("FAIL: v3 prefix of %zu bytes ok=%d\n", len, t.ok ? 1 : 0);
			++failures;
		}
	}

	Bytes size60 = header(3);
	V3Mat bad60;
	bad60.size = 56;
	putV3Material(size60, bad60);
	set32(size60, 8, 60);
	r = parseAll(size60);
	CHECK(!r.ok && r.errorOffset == 8);

	Bytes stride48 = header(3);
	V3Mat bad48;
	bad48.size = 104;
	bad48.stride = 48;
	putV3Material(stride48, bad48);
	r = parseAll(stride48);
	CHECK(!r.ok && r.errorOffset == 8 + 60);

	Bytes hugeVertices = header(3);
	V3Mat hv;
	putV3Material(hugeVertices, hv);
	set32(hugeVertices, 8 + 56, 0xFFFFFFFFu);
	r = parseAll(hugeVertices);
	CHECK(!r.ok);

	Bytes hugeTriangles = header(3);
	V3Mat ht;
	const size_t htEnd = putV3Material(hugeTriangles, ht);
	set32(hugeTriangles, htEnd - 4 - 12, 0xFFFFFFFFu);
	r = parseAll(hugeTriangles);
	CHECK(!r.ok);

	Bytes trailing = header(3);
	V3Mat tm;
	putV3Material(trailing, tm);
	put32(trailing, 0);
	trailing.insert(trailing.end(), {9, 9, 9, 9, 9});
	r = parseAll(trailing);
	CHECK(r.ok && r.model.trailingBytes == 5);

	Bytes outOfRange = header(3);
	V3Mat om;
	const size_t omEnd = putV3Material(outOfRange, om);
	set32(outOfRange, omEnd - 4, 3);
	r = parseAll(outOfRange);
	CHECK(r.ok && r.model.indicesOutOfRange == 1 && r.model.incompleteTriangles == 0);

	Bytes nan = header(3);
	V3Mat nm;
	putV3Material(nan, nm);
	set32(nan, 8 + 56 + 4 + 44 + 4, 0x7FC00000u);
	r = parseAll(nan);
	CHECK(r.ok && r.model.nonFinitePositions == 1);
}

static void testV3MaterialFields() {
	for (uint32_t size : {56u, 104u, 136u}) {
		Bytes data = header(3);
		V3Mat fixture;
		fixture.size = size;
		fixture.stride = size == 56 ? 44 : 64;
		putV3Material(data, fixture);
		const float values[] = {0.125f, 0.25f, 0.5f, 0.75f, 32.0f};
		for (size_t i = 0; i < 5; ++i) {
			uint32_t bits;
			std::memcpy(&bits, &values[i], sizeof bits);
			set32(data, 8 + 20 + 4 * i, bits);
		}
		for (size_t i = 0; i < (size == 56 ? 6u : 7u); ++i) {
			set16(data, 8 + 44 + 2 * i, static_cast<uint16_t>(i + 10));
		}
		set16(data, 8 + 48, 0xfffe);
		if (size > 56) {
			set16(data, 8 + 70, 0xffff);
			set16(data, 8 + 72, 0xfffe);
		}
		const ScxResult result = parseAll(data);
		CHECK(result.ok && result.model.materials.size() == 1);
		if (!result.ok || result.model.materials.size() != 1) continue;
		const ScxMaterial& material = result.model.materials[0];
		CHECK(material.scyV3.has_value());
		if (!material.scyV3) continue;
		const ScyV3Material& legacy = *material.scyV3;
		CHECK(legacy.recordSize == size);
		CHECK((legacy.specular == std::array<float, 3>{0.125f, 0.25f, 0.5f}));
		CHECK(legacy.specularStrength == 0.75f && legacy.shininess == 32.0f);
		for (size_t i = 0; i < (size == 56 ? 6u : 7u); ++i) {
			CHECK(legacy.textureIndices[i] == (i == 2 ? -2 : static_cast<int32_t>(i + 10)));
		}
		if (size == 56) {
			CHECK(legacy.textureIndices[6] == -1);
			CHECK(!legacy.diffuseChannel && !legacy.layer2Channel);
		} else {
			CHECK(legacy.diffuseChannel && *legacy.diffuseChannel == -1);
			CHECK(legacy.layer2Channel && *legacy.layer2Channel == -2);
		}
	}
}

static void testV4Good() {
	const Bytes good = v4File(goodChunks());
	ScxResult r = parseAll(good);
	CHECK(r.ok);
	CHECK(r.model.version == 4 && r.model.directoryMismatches == 0 && r.model.trailingBytes == 0);
	CHECK(r.model.materials.size() == 1 && r.model.meshes.size() == 1 && r.model.rawChunks.size() == 1);
	if (!r.ok || r.model.materials.size() != 1 || r.model.meshes.size() != 1 || r.model.rawChunks.size() != 1) {
		return;
	}
	const ScxMaterial& mat = r.model.materials[0];
	CHECK(!mat.scyV3);
	CHECK(mat.name == "paint" && mat.flags == 0xA1 && mat.functionWord == 0xB2);
	CHECK(mat.diffuse[0] == 1.0f && mat.diffuse[1] == 0.0f && mat.diffuse[2] == 51.0f / 255.0f && mat.diffuse[3] == 1.0f);
	CHECK(mat.maps.size() == 1);
	if (mat.maps.size() == 1) {
		const ScxTextureMap& m = mat.maps[0];
		CHECK(m.slot == 1 && m.textureIndex == 7 && m.channel == 1 && m.tiling == 2);
		CHECK(m.scaleU == 2.0f && m.scaleV == 3.0f && m.offsetU == 0.5f && m.offsetV == 0.25f);
	}
	CHECK(mat.properties.size() == 2);
	if (mat.properties.size() == 2) {
		CHECK(mat.properties[0].type == 0x01000001 && mat.properties[0].raw == 0x3FC00000u);
		CHECK(mat.properties[1].type == 0x05000000 && mat.properties[1].raw == 99);
	}

	const ScxMesh& m = r.model.meshes[0];
	CHECK(m.materialIndex && *m.materialIndex == 0);
	CHECK(m.vertexFlags == kFullFlags && m.vertexCount == 2 && m.weightBytesPerVertex == 4);
	CHECK((m.positions == std::vector<float>{1, 2, 3, 101, 102, 103}));
	CHECK((m.weights == Bytes{10, 11, 12, 13, 11, 12, 13, 14}));
	CHECK((m.boneIndices == Bytes{20, 21, 22, 23, 21, 22, 23, 24}));
	CHECK((m.normals == std::vector<float>{4, 5, 6, 104, 105, 106}));
	CHECK((m.uv1 == std::vector<float>{7, 8, 107, 108}));
	CHECK((m.uv2 == std::vector<float>{9, 10, 109, 110}));
	CHECK((m.uv3 == std::vector<float>{11, 12, 111, 112}));
	CHECK((m.tangents == std::vector<float>{13, 14, 15, 113, 114, 115}));
	CHECK(m.color.empty() && m.emissive.empty());
	CHECK((m.indices == std::vector<uint32_t>{0, 1, 0}));
	CHECK(r.model.rawChunks[0].type == 2 && r.model.rawChunks[0].payloadSize == 5);
	CHECK(r.model.rawChunks[0].payloadOffset == 12 + 8 * 4 + goodChunks()[0].payload.size() + 8 + 8);

	for (size_t len = 0; len < good.size(); ++len) {
		const ScxResult t = parseCopy(good, len);
		if (t.ok && t.model.directoryMismatches == 0) {
			std::printf("FAIL: v4 prefix of %zu bytes parsed without a mismatch\n", len);
			++failures;
		}
	}

	CHECK(parseAll(v4File({{0, materialPayload()}, {4, simpleVertexPayload()}, {5, indexPayload({0, 1, 2})},
		{4, simpleVertexPayload(5.0f)}, {5, indexPayload({2, 1, 0})}})).model.meshes.size() == 2);

	r = parseAll(v4File({{0, materialPayload()}, {4, simpleVertexPayload()}, {4, simpleVertexPayload(5.0f)}}));
	CHECK(r.ok && r.model.meshes.size() == 2 && r.model.meshes[0].indices.empty());

	r = parseAll(v4File({{0, materialPayload()}, {1, {1, 2, 3}}, {4, simpleVertexPayload()}, {5, indexPayload({0, 1, 2})}}));
	CHECK(r.ok && r.model.rawChunks.size() == 1 && r.model.rawChunks[0].type == 1 && r.model.rawChunks[0].payloadSize == 3);
	CHECK(r.ok && r.model.meshes.size() == 1 && r.model.meshes[0].materialIndex == 0u);

	r = parseAll(v4File({{1, {1, 2, 3, 4}}, {4, simpleVertexPayload()}, {5, indexPayload({0, 1, 2})}}));
	CHECK(r.ok && r.model.materials.empty() && r.model.meshes.size() == 1 && r.model.rawChunks.empty());
	CHECK(r.ok && r.model.meshes.size() == 1 && !r.model.meshes[0].materialIndex);
}

static void testV4Failures() {
	const Bytes good = v4File(goodChunks());

	Bytes badMagic = good;
	badMagic[0] = 'X';
	CHECK(!parseAll(badMagic).ok);
	CHECK(!parseAll(Bytes{}).ok);

	Bytes v5 = good;
	v5[4] = 5;
	ScxResult r = parseAll(v5);
	CHECK(!r.ok && r.errorOffset == 4);

	const size_t firstChunk = 12 + 8 * 4;
	Bytes size7 = good;
	set32(size7, firstChunk + 4, 7);
	CHECK(!parseAll(size7).ok);

	Bytes pastEnd = good;
	set32(pastEnd, firstChunk + 4, static_cast<uint32_t>(good.size()));
	CHECK(!parseAll(pastEnd).ok);

	for (int delta : {-1, 1}) {
		auto chunks = goodChunks();
		Bytes& vp = chunks[2].payload;
		if (delta > 0) {
			vp.push_back(0);
		} else {
			vp.pop_back();
		}
		CHECK(!parseAll(v4File(chunks)).ok);

		chunks = goodChunks();
		Bytes& ip = chunks[3].payload;
		if (delta > 0) {
			ip.push_back(0);
		} else {
			ip.pop_back();
		}
		CHECK(!parseAll(v4File(chunks)).ok);
	}

	auto flagged = goodChunks();
	set32(flagged[2].payload, 4, kFullFlags | 0x1000);
	CHECK(!parseAll(v4File(flagged)).ok);

	flagged = goodChunks();
	set32(flagged[2].payload, 4, kFullFlags & ~0x1u);
	CHECK(!parseAll(v4File(flagged)).ok);

	auto unknownType = goodChunks();
	unknownType.push_back({9, {0, 0, 0, 0}});
	CHECK(!parseAll(v4File(unknownType)).ok);

	auto unknownProp = goodChunks();
	set32(unknownProp[0].payload, 12, 3);
	CHECK(!parseAll(v4File(unknownProp)).ok);

	auto extra = goodChunks();
	extra[0].payload.push_back(0);
	CHECK(!parseAll(v4File(extra)).ok);

	auto missing = goodChunks();
	missing[0].payload.pop_back();
	CHECK(!parseAll(v4File(missing)).ok);

	CHECK(!parseAll(v4File({{4, simpleVertexPayload()}})).ok);
	CHECK(!parseAll(v4File({{5, indexPayload({0})}})).ok);
	CHECK(!parseAll(v4File({{0, materialPayload()}, {5, indexPayload({0})}})).ok);
	CHECK(!parseAll(v4File({{0, materialPayload()}, {4, simpleVertexPayload()}, {5, indexPayload({0})},
		{5, indexPayload({0})}})).ok);

	auto hugeCount = goodChunks();
	set32(hugeCount[2].payload, 0, 0xFFFFFFFFu);
	CHECK(!parseAll(v4File(hugeCount)).ok);

	auto hugeIndex = goodChunks();
	set32(hugeIndex[3].payload, 0, 0xFFFFFFFFu);
	CHECK(!parseAll(v4File(hugeIndex)).ok);

	auto hugeProps = goodChunks();
	set32(hugeProps[0].payload, 8, 0xFFFFFFFFu);
	CHECK(!parseAll(v4File(hugeProps)).ok);

	Bytes hugeDir = good;
	set32(hugeDir, 8, 0xFFFFFFFFu);
	CHECK(!parseAll(hugeDir).ok);
	CHECK(!parseAll(header(4)).ok);
}

static void testCounters() {
	ScxResult r = parseAll(v4File({{0, materialPayload()}, {4, simpleVertexPayload()}, {5, indexPayload({0, 1, 3})}}));
	CHECK(r.ok && r.model.indicesOutOfRange == 1 && r.model.incompleteTriangles == 0);

	r = parseAll(v4File({{0, materialPayload()}, {4, simpleVertexPayload()}, {5, indexPayload({0, 1, 2, 0})}}));
	CHECK(r.ok && r.model.indicesOutOfRange == 0 && r.model.incompleteTriangles == 1);

	r = parseAll(v4File({{0, materialPayload()}, {4, simpleVertexPayload(std::numeric_limits<float>::quiet_NaN())},
		{5, indexPayload({0, 1, 2})}}));
	CHECK(r.ok && r.model.nonFinitePositions == 1);
	r = parseAll(v4File({{0, materialPayload()}, {4, simpleVertexPayload(std::numeric_limits<float>::infinity())},
		{5, indexPayload({0, 1, 2})}}));
	CHECK(r.ok && r.model.nonFinitePositions == 1);

	Bytes trailing = v4File(goodChunks());
	trailing.insert(trailing.end(), {1, 2, 3});
	r = parseAll(trailing);
	CHECK(r.ok && r.model.trailingBytes == 3 && r.model.directoryMismatches == 0);

	r = parseAll(v4File(goodChunks(), 4));
	CHECK(r.ok && r.model.directoryMismatches == 1);

	r = parseAll(v4File(goodChunks(), 0, true));
	CHECK(r.ok && r.model.directoryMismatches == 2);
}

int main() {
	testV3();
	testV3MaterialFields();
	testV4Good();
	testV4Failures();
	testCounters();

	CHECK(!scxParse(nullptr, 0).ok);
	CHECK(!scxLoadFile("/nonexistent/path.scx").ok);

	if (failures == 0) {
		std::puts("scx_test ok");
	}
	return failures == 0 ? 0 : 1;
}
