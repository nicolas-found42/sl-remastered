#include "scx.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

#include "reader.hpp"

namespace {

using ScxReader = Reader<ScxResult>;

constexpr uint32_t kV3Stride44 = 44;
constexpr uint32_t kV3Stride64 = 64;
constexpr uint16_t kNoTexture = 0xffff;

constexpr uint32_t kVfPosition = 0x1;
constexpr uint32_t kVfWeights4 = 0x4;
constexpr uint32_t kVfWeights8 = 0x8;
constexpr uint32_t kVfWeights12 = 0x10;
constexpr uint32_t kVfBoneIndices = 0x20;
constexpr uint32_t kVfNormal = 0x40;
constexpr uint32_t kVfEmissive = 0x80;
constexpr uint32_t kVfColor = 0x100;
constexpr uint32_t kVfUv1 = 0x200;
constexpr uint32_t kVfUv2 = 0x400;
constexpr uint32_t kVfUv3 = 0x800;
constexpr uint32_t kVfTangent = 0x40000;
constexpr uint32_t kVfKnownMask = 0x40FFF;

constexpr uint32_t kChunkMaterial = 0;
constexpr uint32_t kChunkPhysics = 1;
constexpr uint32_t kChunkBoneA = 2;
constexpr uint32_t kChunkBoneB = 3;
constexpr uint32_t kChunkVertices = 4;
constexpr uint32_t kChunkIndices = 5;

constexpr uint32_t kPropNameType = 0x08000000;
constexpr uint32_t kPropNameBytes = 32;

bool floats(ScxReader& r, std::vector<float>& v, int n, const char* what) {
	for (int i = 0; i < n; ++i) {
		float f = 0.0f;
		if (!r.f32(f, what)) {
			return false;
		}
		v.push_back(f);
	}
	return true;
}

bool positionFinite(const std::vector<float>& positions, size_t vertex) {
	for (size_t k = 0; k < 3; ++k) {
		if (!std::isfinite(positions[vertex * 3 + k])) {
			return false;
		}
	}
	return true;
}

void countNonFinite(ScxModel& m, const ScxMesh& mesh) {
	for (size_t i = 0; i < mesh.vertexCount; ++i) {
		if (!positionFinite(mesh.positions, i)) {
			++m.nonFinitePositions;
		}
	}
}

void countIndices(ScxModel& m, const ScxMesh& mesh) {
	if (mesh.indices.size() % 3 != 0) {
		++m.incompleteTriangles;
	}
	for (uint32_t idx : mesh.indices) {
		if (idx >= mesh.vertexCount) {
			++m.indicesOutOfRange;
		}
	}
}

bool parseV3(ScxReader& r, ScxModel& m) {
	r.pos = 8;
	while (r.pos != r.limit) {
		uint32_t size = 0;
		const uint64_t start = r.pos;
		if (!r.u32(size, "truncated material size")) {
			return false;
		}
		if (size == 0) {
			m.trailingBytes += r.limit - r.pos;
			return true;
		}
		if (size != 56 && size != 104 && size != 136) {
			r.pos = start;
			return r.fail("unsupported material size");
		}
		r.pos = start;
		if (!r.have(size, "truncated material")) {
			return false;
		}
		const uint8_t* b = r.data + start;

		ScxMaterial mat;
		for (size_t i = 0; i < 4; ++i) {
			uint32_t bits = readLe32(b + 4 + i * 4);
			std::memcpy(&mat.diffuse[i], &bits, sizeof bits);
		}
		mat.flags = readLe32(b + 40);
		for (uint32_t slot = 0; slot < 5; ++slot) {
			const uint16_t tex = readLe16(b + 44 + slot * 2);
			if (tex == kNoTexture) {
				continue;
			}
			ScxTextureMap map;
			map.slot = slot;
			map.textureIndex = tex;
			if (size > 56 && slot == 0) {
				map.channel = readLe16(b + 70);
			} else if (size > 56 && slot == 4) {
				map.channel = readLe16(b + 72);
			}
			mat.maps.push_back(map);
		}
		uint32_t stride = kV3Stride44;
		if (size > 56) {
			stride = readLe32(b + 60);
			if (stride != kV3Stride64) {
				r.pos = start + 60;
				return r.fail("unsupported vertex stride");
			}
		}
		if (size == 136) {
			const char* n = reinterpret_cast<const char*>(b + 104);
			mat.name.assign(n, strnlen(n, 32));
		}
		r.pos = start + size;

		ScxMesh mesh;
		mesh.materialIndex = static_cast<uint32_t>(m.materials.size());
		mesh.vertexFlags = kVfPosition | kVfNormal | kVfColor | kVfUv1 | kVfUv2;
		if (stride == kV3Stride64) {
			mesh.vertexFlags |= kVfUv3;
		}
		if (!r.u32(mesh.vertexCount, "truncated vertex count")) {
			return false;
		}
		if (mesh.vertexCount > (r.limit - r.pos) / stride) {
			return r.fail("truncated vertices");
		}
		const size_t n = mesh.vertexCount;
		mesh.positions.reserve(n * 3);
		mesh.normals.reserve(n * 3);
		mesh.uv1.reserve(n * 2);
		mesh.uv2.reserve(n * 2);
		mesh.color.reserve(n * 4);
		if (stride == kV3Stride64) {
			mesh.uv3.reserve(n * 2);
		}
		for (size_t i = 0; i < n; ++i) {
			if (!floats(r, mesh.positions, 3, "truncated vertex") || !floats(r, mesh.normals, 3, "truncated vertex") ||
				!floats(r, mesh.uv1, 2, "truncated vertex") || !floats(r, mesh.uv2, 2, "truncated vertex") ||
				!r.append(mesh.color, 4, "truncated vertex")) {
				return false;
			}
			if (stride == kV3Stride64 &&
				(!r.skip(8, "truncated vertex") || !floats(r, mesh.uv3, 2, "truncated vertex") ||
					!r.skip(4, "truncated vertex"))) {
				return false;
			}
		}

		uint32_t triangles = 0;
		if (!r.u32(triangles, "truncated triangle count")) {
			return false;
		}
		if (triangles > (r.limit - r.pos) / 12) {
			return r.fail("truncated triangles");
		}
		mesh.indices.reserve(static_cast<size_t>(triangles) * 3);
		for (uint64_t i = 0; i < static_cast<uint64_t>(triangles) * 3; ++i) {
			uint32_t idx = 0;
			if (!r.u32(idx, "truncated triangles")) {
				return false;
			}
			mesh.indices.push_back(idx);
		}
		countNonFinite(m, mesh);
		countIndices(m, mesh);
		m.materials.push_back(std::move(mat));
		m.meshes.push_back(std::move(mesh));
	}
	return true;
}

uint32_t vertexStride(uint32_t flags, uint32_t& weightBytes) {
	uint32_t s = 0;
	weightBytes = 0;
	if (flags & kVfPosition) {
		s += 12;
	}
	if (flags & kVfWeights4) {
		weightBytes = 4;
	} else if (flags & kVfWeights8) {
		weightBytes = 8;
	} else if (flags & kVfWeights12) {
		weightBytes = 12;
	}
	s += weightBytes;
	if (flags & kVfBoneIndices) {
		s += 4;
	}
	if (flags & kVfNormal) {
		s += 12;
	}
	if (flags & kVfEmissive) {
		s += 4;
	}
	if (flags & kVfColor) {
		s += 4;
	}
	if (flags & kVfUv1) {
		s += 8;
	}
	if (flags & kVfUv2) {
		s += 8;
	}
	if (flags & kVfUv3) {
		s += 8;
	}
	if (flags & kVfTangent) {
		s += 12;
	}
	return s;
}

bool readMaterialChunk(ScxReader& r, ScxMaterial& mat) {
	uint32_t count = 0;
	if (!r.u32(mat.flags, "truncated material header") || !r.u32(mat.functionWord, "truncated material header") ||
		!r.u32(count, "truncated material header")) {
		return false;
	}
	for (uint32_t i = 0; i < count; ++i) {
		const uint64_t propStart = r.pos;
		uint32_t type = 0;
		if (!r.u32(type, "truncated material property")) {
			return false;
		}
		if (type <= 2) {
			uint32_t rgba = 0;
			if (!r.u32(rgba, "truncated material color")) {
				return false;
			}
			if (type == 0) {
				for (size_t k = 0; k < 4; ++k) {
					mat.diffuse[k] = static_cast<float>((rgba >> (8 * k)) & 0xff) / 255.0f;
				}
			} else {
				mat.properties.push_back({type, rgba});
			}
		} else if ((type >= 0x01000000 && type <= 0x01000002) || type == 0x05000000 || type == 0x07000000) {
			uint32_t raw = 0;
			if (!r.u32(raw, "truncated material property value")) {
				return false;
			}
			mat.properties.push_back({type, raw});
		} else if (type >= 0x06000000 && type <= 0x06000005) {
			ScxTextureMap map;
			map.slot = type & 0xff;
			if (!r.u32(map.textureIndex, "truncated texture map") || !r.u32(map.channel, "truncated texture map") ||
				!r.u32(map.tiling, "truncated texture map") || !r.f32(map.scaleU, "truncated texture map") ||
				!r.f32(map.scaleV, "truncated texture map") || !r.f32(map.offsetU, "truncated texture map") ||
				!r.f32(map.offsetV, "truncated texture map")) {
				return false;
			}
			mat.maps.push_back(map);
		} else if (type == kPropNameType) {
			if (!r.str(mat.name, kPropNameBytes, "truncated material name")) {
				return false;
			}
		} else {
			r.pos = propStart;
			return r.fail("unknown material property");
		}
	}
	if (r.pos != r.limit) {
		return r.fail("material chunk not consumed exactly");
	}
	return true;
}

bool readVertexChunk(ScxReader& r, ScxMesh& mesh, uint64_t chunkStart, uint64_t chunkEnd) {
	if (!r.u32(mesh.vertexCount, "truncated vertex header")) {
		return false;
	}
	const uint64_t flagsAt = r.pos;
	if (!r.u32(mesh.vertexFlags, "truncated vertex header")) {
		return false;
	}
	if (!(mesh.vertexFlags & kVfPosition) || (mesh.vertexFlags & ~kVfKnownMask)) {
		r.pos = flagsAt;
		return r.fail("unsupported vertex flags");
	}
	const uint32_t flags = mesh.vertexFlags;
	const uint32_t stride = vertexStride(flags, mesh.weightBytesPerVertex);
	if (chunkEnd - chunkStart != 16 + static_cast<uint64_t>(mesh.vertexCount) * stride) {
		r.pos = chunkStart;
		return r.fail("vertex chunk size mismatch");
	}
	const size_t n = mesh.vertexCount;
	mesh.positions.reserve(n * 3);
	if (flags & kVfNormal) {
		mesh.normals.reserve(n * 3);
	}
	if (flags & kVfUv1) {
		mesh.uv1.reserve(n * 2);
	}
	if (flags & kVfUv2) {
		mesh.uv2.reserve(n * 2);
	}
	if (flags & kVfUv3) {
		mesh.uv3.reserve(n * 2);
	}
	if (flags & kVfTangent) {
		mesh.tangents.reserve(n * 3);
	}
	const char* bad = "truncated vertex";
	for (size_t i = 0; i < n; ++i) {
		if (!floats(r, mesh.positions, 3, bad) ||
			!r.append(mesh.weights, mesh.weightBytesPerVertex, bad) ||
			((flags & kVfBoneIndices) && !r.append(mesh.boneIndices, 4, bad)) ||
			((flags & kVfNormal) && !floats(r, mesh.normals, 3, bad)) ||
			((flags & kVfEmissive) && !r.append(mesh.emissive, 4, bad)) ||
			((flags & kVfColor) && !r.append(mesh.color, 4, bad)) ||
			((flags & kVfUv1) && !floats(r, mesh.uv1, 2, bad)) ||
			((flags & kVfUv2) && !floats(r, mesh.uv2, 2, bad)) ||
			((flags & kVfUv3) && !floats(r, mesh.uv3, 2, bad)) ||
			((flags & kVfTangent) && !floats(r, mesh.tangents, 3, bad))) {
			return false;
		}
	}
	return true;
}

bool parseV4(ScxReader& r, ScxModel& m) {
	r.pos = 8;
	uint32_t dirCount = 0;
	if (!r.u32(dirCount, "truncated directory count")) {
		return false;
	}
	if (dirCount > (r.limit - r.pos) / 8) {
		return r.fail("directory exceeds file");
	}
	struct DirEntry {
		uint32_t type;
		uint32_t offset;
	};
	std::vector<DirEntry> directory(dirCount);
	for (DirEntry& d : directory) {
		if (!r.u32(d.type, "truncated directory") || !r.u32(d.offset, "truncated directory")) {
			return false;
		}
	}
	const bool physicsOnly = !directory.empty() && directory[0].type == kChunkPhysics;
	const uint64_t fileEnd = r.limit;

	bool haveGroup = false;
	std::optional<uint32_t> groupMaterial;
	bool awaitingIndices = false;
	uint64_t chunkCount = 0;
	while (fileEnd - r.pos >= 8) {
		const uint64_t start = r.pos;
		uint32_t type = 0;
		uint32_t size = 0;
		if (!r.u32(type, "truncated chunk header") || !r.u32(size, "truncated chunk header")) {
			return false;
		}
		if (size < 8) {
			r.pos = start;
			return r.fail("invalid chunk size");
		}
		if (size > fileEnd - start) {
			r.pos = start;
			return r.fail("chunk past end of file");
		}
		const uint64_t end = start + size;
		if (chunkCount >= directory.size() || directory[chunkCount].type != type || directory[chunkCount].offset != start) {
			++m.directoryMismatches;
		}
		++chunkCount;
		r.limit = end;

		if (type == kChunkMaterial) {
			ScxMaterial mat;
			if (!readMaterialChunk(r, mat)) {
				return false;
			}
			groupMaterial = static_cast<uint32_t>(m.materials.size());
			m.materials.push_back(std::move(mat));
			haveGroup = true;
			awaitingIndices = false;
		} else if (type == kChunkPhysics && physicsOnly) {
			groupMaterial.reset();
			haveGroup = true;
			awaitingIndices = false;
		} else if (type == kChunkPhysics || type == kChunkBoneA || type == kChunkBoneB) {
			m.rawChunks.push_back({type, start + 8, size - 8});
		} else if (type == kChunkVertices) {
			if (!haveGroup) {
				r.pos = start;
				return r.fail("vertices before any mesh group");
			}
			ScxMesh mesh;
			mesh.materialIndex = groupMaterial;
			if (!readVertexChunk(r, mesh, start, end)) {
				return false;
			}
			countNonFinite(m, mesh);
			m.meshes.push_back(std::move(mesh));
			awaitingIndices = true;
		} else if (type == kChunkIndices) {
			if (!awaitingIndices) {
				r.pos = start;
				return r.fail("indices without vertices");
			}
			uint32_t count = 0;
			if (!r.u32(count, "truncated index header")) {
				return false;
			}
			if (end - start != 12 + 2 * static_cast<uint64_t>(count)) {
				r.pos = start;
				return r.fail("index chunk size mismatch");
			}
			ScxMesh& mesh = m.meshes.back();
			mesh.indices.reserve(count);
			for (uint32_t i = 0; i < count; ++i) {
				uint16_t idx = 0;
				if (!r.u16(idx, "truncated indices")) {
					return false;
				}
				mesh.indices.push_back(idx);
			}
			countIndices(m, mesh);
			awaitingIndices = false;
		} else {
			r.pos = start;
			return r.fail("unknown chunk type");
		}
		r.limit = fileEnd;
		r.pos = end;
	}
	m.trailingBytes += fileEnd - r.pos;
	if (chunkCount != directory.size()) {
		++m.directoryMismatches;
	}
	return true;
}

}  // namespace

ScxResult scxParse(const uint8_t* data, size_t size) {
	ScxResult out;
	ScxReader r{data, 0, size, out};
	ScxModel& m = out.model;

	if (!r.have(4, "truncated magic")) {
		return out;
	}
	if (std::memcmp(data, "INVO", 4) != 0) {
		r.fail("bad magic");
		return out;
	}
	r.pos = 4;
	if (!r.u32(m.version, "truncated version")) {
		return out;
	}
	if (m.version != 3 && m.version != 4) {
		r.pos = 4;
		r.fail("unsupported version");
		return out;
	}
	out.ok = m.version == 3 ? parseV3(r, m) : parseV4(r, m);
	return out;
}

ScxResult scxLoadFile(const std::string& path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	if (!in) {
		ScxResult out;
		out.error = "cannot open file";
		return out;
	}
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (in.bad()) {
		ScxResult out;
		out.error = "read error";
		return out;
	}
	return scxParse(bytes.data(), bytes.size());
}
