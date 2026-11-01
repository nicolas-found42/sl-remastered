#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct ScxTextureMap {
	uint32_t slot = 0;
	uint32_t textureIndex = 0;
	uint32_t channel = 0;
	uint32_t tiling = 0;
	float scaleU = 1.0f;
	float scaleV = 1.0f;
	float offsetU = 0.0f;
	float offsetV = 0.0f;
};

// raw is the u32 value, the f32 bit pattern, or the RGBA bytes packed little-endian, depending on type.
struct ScxProperty {
	uint32_t type = 0;
	uint32_t raw = 0;
};

// Original version-3 material fields used by the stock material converter.
// Indices and channels are signed 16-bit values; -1 denotes no texture.
struct ScyV3Material {
	uint32_t recordSize = 0;
	std::array<float, 3> specular{};
	float specularStrength = 0.0f;
	float shininess = 0.0f;
	std::array<int32_t, 7> textureIndices{{-1, -1, -1, -1, -1, -1, -1}};
	std::optional<int32_t> diffuseChannel;
	std::optional<int32_t> layer2Channel;
};

struct ScxMaterial {
	std::string name;
	uint32_t recordSize = 0;
	std::array<float, 4> diffuse{};
	uint32_t flags = 0;
	uint32_t functionWord = 0;
	std::vector<ScxTextureMap> maps;
	std::optional<ScyV3Material> scyV3;
	std::vector<ScxProperty> properties;
};

struct ScxMesh {
	std::optional<uint32_t> materialIndex;
	uint32_t vertexFlags = 0;
	uint32_t vertexCount = 0;
	std::vector<float> positions;
	std::vector<float> normals;
	std::vector<float> uv1;
	std::vector<float> uv2;
	std::vector<float> uv3;
	std::vector<float> tangents;
	std::vector<uint8_t> color;
	std::vector<uint8_t> emissive;
	// Chunk 2/3 maps mesh-local blend indices to skeleton palette entries.
	std::vector<uint32_t> bonePalette;
	std::vector<uint8_t> boneIndices;
	std::vector<uint8_t> weights;
	uint32_t weightBytesPerVertex = 0;
	std::vector<uint32_t> indices;
};

struct ScxRawChunk {
	uint32_t type = 0;
	uint64_t payloadOffset = 0;
	uint64_t payloadSize = 0;
};

struct ScxModel {
	uint32_t version = 0;
	std::vector<ScxMaterial> materials;
	std::vector<ScxMesh> meshes;
	std::vector<ScxRawChunk> rawChunks;
	uint32_t indicesOutOfRange = 0;
	uint32_t incompleteTriangles = 0;
	uint32_t nonFinitePositions = 0;
	uint64_t trailingBytes = 0;
	uint32_t directoryMismatches = 0;
};

struct ScxResult {
	bool ok = false;
	ScxModel model;
	std::string error;
	uint64_t errorOffset = 0;
};

ScxResult scxParse(const uint8_t* data, size_t size);
ScxResult scxLoadFile(const std::string& path);
