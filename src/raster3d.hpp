#pragma once

#include "scx.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// A software 3D rasterizer for the host. It draws into an RGBA8 framebuffer owned by Raster3D. Render2D
// then uploads that framebuffer as one texture and presents it (Raster3D::present). Nothing here changes
// Render2D. See docs/render3d-evidence.md for what is stock-derived and what is a host choice.
//
// Matrix convention: row vectors, as everywhere else in the host (point * model * view * projection),
// with the translation in elements 12..14. The projection is the stock one from cameraViewState
// (right-handed, D3D clip volume: -w <= x <= w, -w <= y <= w, 0 <= z <= w).

class Render2D;

// RGBA8 texture. File loaders preserve stored mip levels; build generates a
// box-filtered chain for callers that supply only raw RGBA pixels.
struct Raster3DTexture {
	struct Level { uint32_t width = 0, height = 0; std::vector<uint8_t> rgba; };
	std::vector<Level> levels; // levels[0] is the full-size image
	bool hasAlpha = false;     // some texel alpha is below 255
	static bool build(uint32_t width, uint32_t height, const uint8_t* rgba, size_t size,
	                  Raster3DTexture& out, std::string& error);
};

// Decodes a .dds (DXT1, DXT3, DXT5; first level only) or .ptx file into RGBA8 pixels.
struct Raster3DPixels { uint32_t width = 0, height = 0; std::vector<uint8_t> rgba; };
bool raster3dDecodeDxt(const uint8_t* data, size_t size, Raster3DPixels& out, std::string& error);
bool raster3dDecodeDxtTexture(const uint8_t* data, size_t size, Raster3DTexture& out, std::string& error);
bool raster3dLoadTextureFile(const std::filesystem::path& path, Raster3DTexture& out, std::string& error);

enum class Raster3DBlend : uint8_t { Opaque = 0, AlphaTest = 1, AlphaBlend = 2 };
// Which on-screen winding is discarded. Clockwise and counter-clockwise are as seen in the final image.
enum class Raster3DCull : uint8_t { None = 0, Clockwise = 1, CounterClockwise = 2 };
enum class Raster3DFilter : uint8_t { Point = 0, Bilinear = 1, Trilinear = 2 };
enum class Raster3DAddress : uint8_t { Wrap = 0, Clamp = 1 };

struct Raster3DMaterial {
	Raster3DBlend blend = Raster3DBlend::Opaque;
	uint8_t alphaRef = 0x38;            // AlphaTest passes when alpha*255 >= alphaRef
	Raster3DCull cull = Raster3DCull::None;
	bool depthTest = true;              // z <= stored z
	bool depthWrite = true;
	const Raster3DTexture* texture = nullptr; // null: flat diffuse colour
	Raster3DAddress addressU = Raster3DAddress::Wrap, addressV = Raster3DAddress::Wrap;
	Raster3DFilter filter = Raster3DFilter::Bilinear;
	std::array<float, 4> diffuse{{1, 1, 1, 1}}; // RGBA multiplier
	bool useVertexColor = false;        // multiply by the draw's vertex colours when they are present
	bool perspectiveCorrect = true;     // false is a diagnostic: affine interpolation, to show the difference
};

struct Raster3DViewport { float left = 0, top = 0, width = 1, height = 1; }; // fractions of the output

struct Raster3DLight {
	bool enabled = false;
	std::array<float, 3> toLight{{0.4f, 0.8f, 0.5f}}; // world-space direction towards the light
	float ambient = 0.45f, diffuse = 0.65f;
};

struct Raster3DDraw {
	const float* positions = nullptr; // vertexCount * 3
	size_t vertexCount = 0;
	const float* uvs = nullptr;       // vertexCount * 2, optional
	const float* normals = nullptr;   // vertexCount * 3, optional (used only when the light is enabled)
	const uint8_t* colors = nullptr;  // vertexCount * 4 bytes, D3DCOLOR order (B, G, R, A), optional
	const uint32_t* indices = nullptr;
	size_t indexCount = 0;            // multiple of 3
	float positionScale = 1.0f;       // stock mesh scale is 0.1 (scene_2d.cpp, sceneDrawCameraMesh)
	std::array<float, 16> model{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
	std::array<float, 16> view{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
	std::array<float, 16> projection{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
	Raster3DViewport viewport;
	Raster3DMaterial material;
	Raster3DLight light;
};

struct Raster3DStats {
	uint64_t trianglesSubmitted = 0, trianglesRejectedOutside = 0, trianglesClipped = 0;
	uint64_t trianglesCulled = 0, trianglesDegenerate = 0, trianglesRasterized = 0;
	uint64_t pixelsTested = 0, pixelsDepthRejected = 0, pixelsAlphaRejected = 0, pixelsWritten = 0;
};

class Raster3D {
public:
	// Creates a framebuffer of the given size; returns false with an error for an invalid size.
	bool resize(uint32_t width, uint32_t height, std::string& error);
	uint32_t width() const { return width_; }
	uint32_t height() const { return height_; }
	void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255, float depth = 1.0f);
	// Draws indexed triangles. Fails (and draws nothing) on invalid input or unsupported states.
	bool draw(const Raster3DDraw& draw, std::string& error);
	const std::vector<uint8_t>& pixels() const { return color_; } // RGBA, row 0 at the top
	const std::vector<float>& depth() const { return depth_; }
	const Raster3DStats& stats() const { return stats_; }
	void resetStats() { stats_ = {}; }
	// Uploads the framebuffer through Render2D and draws it over the whole output. The caller owns
	// clear() and present() of the Render2D. The texture is created on first use and re-created if the
	// framebuffer size or the Render2D changes.
	bool blit(Render2D& renderer, std::string& error);
	// Writes the framebuffer as a 24-bit BMP.
	bool saveBmp(const std::filesystem::path& path, std::string& error) const;

private:
	uint32_t width_ = 0, height_ = 0;
	std::vector<uint8_t> color_;
	std::vector<float> depth_;
	Raster3DStats stats_;
	Render2D* uploadRenderer_ = nullptr;
	size_t uploadTexture_ = 0;
	uint32_t uploadWidth_ = 0, uploadHeight_ = 0;
};

// Maps a point already divided by w (NDC) to output pixels with the stock viewport mapping
// (cameraViewportPoint in world_model.cpp), without its inside-the-volume restriction.
std::array<float, 2> raster3dViewportPoint(float ndcX, float ndcY, const Raster3DViewport& viewport,
                                           uint32_t outputWidth, uint32_t outputHeight);

// Homogeneous clip against the six D3D planes. Positions are clip-space (x, y, z, w). attr carries
// per-vertex attributes (up to 7 floats, linearly interpolated). Returns the polygon (0 to 9 vertices).
struct Raster3DClipVertex { std::array<float, 4> position{}; std::array<float, 7> attr{}; };
std::vector<Raster3DClipVertex> raster3dClipTriangle(const std::array<Raster3DClipVertex, 3>& triangle);

// How an SCX material becomes rasterizer states. The SCX "flags" word is the stock material type
// (004befc0 switch) and "functionWord" its option bits; see docs/render3d-evidence.md.
struct Raster3DScxOptions {
	bool allowApproximations = false;   // accept types whose extra layers (specular, env, mix) are not drawn
	bool ignoreUnknownFunctionBits = false;
};
struct Raster3DScxMaterial {
	Raster3DMaterial state;                     // texture left null
	std::optional<uint32_t> baseTextureIndex;   // SCX textureIndex of the map that supplies the base colour
	std::optional<uint32_t> baseSlot;
	std::vector<std::string> notes;             // what was approximated or ignored
};
bool raster3dScxMaterial(const ScxMaterial& material, const Raster3DScxOptions& options,
                         Raster3DScxMaterial& out, std::string& error);
