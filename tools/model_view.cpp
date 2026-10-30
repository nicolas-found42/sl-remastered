// Diagnostic viewer for SCX models. Loads one or more .scx files, finds their textures, and shows them
// orbiting in a window through the software rasterizer (src/raster3d.*). It is a diagnostic, not the
// game's renderer: lighting is a fixed host light, and material types that draw extra stock layers are
// shown as their base layer only (the notes are printed).

#include "platform.hpp"
#include "raster3d.hpp"
#include "render2d.hpp"
#include "world_model.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
	std::vector<fs::path> models;
	std::vector<fs::path> textures; // by SCX textureIndex
	bool autoTexture = false;
	bool strict = false, lenient = false, headless = false, skipUnsupported = false, list = false;
	bool light = true;
	uint32_t width = 640, height = 480;
	double seconds = 0;
	uint32_t bench = 0;
	uint32_t captureFrame = 0;
	std::string capture;
	float angle = 35, spin = 25, elevation = 18, fov = 50, zoom = 1;
	Raster3DCull cull = Raster3DCull::Clockwise; // SCX front faces wind counter-clockwise on screen (seen in the viewer)
	Raster3DFilter filter = Raster3DFilter::Bilinear;
};

void usage() {
	std::fputs(
	    "usage: model_view <model.scx>... [options]\n"
	    "  --texture FILE      texture (.dds DXT1/3/5 or .ptx) for SCX textureIndex 0, 1, 2 in the order given\n"
	    "  --auto-texture      find textures by material name in the model's sibling 'textures' folder or beside the model\n"
	    "  --width N --height N   framebuffer size (default 640x480)\n"
	    "  --seconds N         run for N seconds, then exit (default: until the window closes or Esc)\n"
	    "  --capture FILE.bmp  save the last rendered frame\n"
	    "  --frame N           with --capture, render N frames of the fixed-step orbit first (default 0)\n"
	    "  --headless          no window (for --capture and --bench)\n"
	    "  --bench N           render N frames headless and print the frame time\n"
	    "  --zoom X            divide the camera distance by X (default 1 fits the model)\n"
	    "  --angle DEG --spin DEG_PER_SECOND --elevation DEG --fov DEG\n"
	    "  --cull none|cw|ccw  winding to discard, as seen on screen (default cw)\n"
	    "  --filter point|bilinear|trilinear\n"
	    "  --no-light          draw unlit\n"
	    "  --strict            refuse material types that draw extra stock layers\n"
	    "  --lenient           ignore unknown material function bits instead of failing\n"
	    "  --list              print every mesh piece with its material, texture and vertex layout\n"
	    "  --skip-unsupported  skip meshes whose material the rasterizer refuses (they are listed) instead of failing\n",
	    stderr);
}

bool parseArgs(int argc, char** argv, Options& o) {
	const auto number = [&](int& i, double& out) {
		if (i + 1 >= argc) return false;
		char* end = nullptr;
		out = std::strtod(argv[++i], &end);
		return end != argv[i] && !*end && std::isfinite(out);
	};
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		double v = 0;
		if (a == "--texture" && i + 1 < argc) o.textures.push_back(argv[++i]);
		else if (a == "--auto-texture") o.autoTexture = true;
		else if (a == "--width" && number(i, v) && v >= 16 && v <= 4096) o.width = static_cast<uint32_t>(v);
		else if (a == "--height" && number(i, v) && v >= 16 && v <= 4096) o.height = static_cast<uint32_t>(v);
		else if (a == "--seconds" && number(i, v) && v >= 0) o.seconds = v;
		else if (a == "--capture" && i + 1 < argc) o.capture = argv[++i];
		else if (a == "--frame" && number(i, v) && v >= 0 && v < 1e6) o.captureFrame = static_cast<uint32_t>(v);
		else if (a == "--bench" && number(i, v) && v >= 1 && v < 1e6) { o.bench = static_cast<uint32_t>(v); o.headless = true; }
		else if (a == "--headless") o.headless = true;
		else if (a == "--zoom" && number(i, v) && v > 0.05 && v < 100) o.zoom = static_cast<float>(v);
		else if (a == "--angle" && number(i, v)) o.angle = static_cast<float>(v);
		else if (a == "--spin" && number(i, v)) o.spin = static_cast<float>(v);
		else if (a == "--elevation" && number(i, v)) o.elevation = static_cast<float>(v);
		else if (a == "--fov" && number(i, v) && v > 5 && v < 150) o.fov = static_cast<float>(v);
		else if (a == "--cull" && i + 1 < argc) {
			const std::string c = argv[++i];
			if (c == "none") o.cull = Raster3DCull::None;
			else if (c == "cw") o.cull = Raster3DCull::Clockwise;
			else if (c == "ccw") o.cull = Raster3DCull::CounterClockwise;
			else return false;
		} else if (a == "--filter" && i + 1 < argc) {
			const std::string c = argv[++i];
			if (c == "point") o.filter = Raster3DFilter::Point;
			else if (c == "bilinear") o.filter = Raster3DFilter::Bilinear;
			else if (c == "trilinear") o.filter = Raster3DFilter::Trilinear;
			else return false;
		} else if (a == "--no-light") o.light = false;
		else if (a == "--strict") o.strict = true;
		else if (a == "--lenient") o.lenient = true;
		else if (a == "--skip-unsupported") o.skipUnsupported = true;
		else if (a == "--list") o.list = true;
		else if (!a.empty() && a[0] != '-') o.models.push_back(a);
		else return false;
	}
	return !o.models.empty();
}

std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

// Finds a texture for a material name or mesh name: a file in a sibling "textures" folder whose stem
// equals the name or ends with "_<name>" (cars name theirs "<Car>_<part>.dds").
struct TextureFinder {
	std::map<fs::path, std::vector<fs::path>> listings;
	const std::vector<fs::path>& list(const fs::path& dir) {
		auto it = listings.find(dir);
		if (it != listings.end()) return it->second;
		std::vector<fs::path> files;
		std::error_code ec;
		for (fs::directory_iterator d(dir, ec), end; !ec && d != end; d.increment(ec)) {
			const auto ext = lower(d->path().extension().string());
			if (d->is_regular_file(ec) && (ext == ".dds" || ext == ".ptx")) files.push_back(d->path());
		}
		return listings.emplace(dir, std::move(files)).first->second;
	}
	std::optional<fs::path> find(const fs::path& meshPath, std::string name) {
		std::replace(name.begin(), name.end(), ' ', '_');
		name = lower(name);
		if (name.empty()) return std::nullopt;
		const fs::path meshDir = meshPath.parent_path();
		for (const fs::path& dir : {meshDir.parent_path() / "textures", meshDir / "textures", meshDir.parent_path().parent_path() / "textures", meshDir, meshDir.parent_path()}) {
			std::optional<fs::path> best;
			for (const auto& file : list(dir)) {
				const std::string stem = lower(file.stem().string());
				const bool match = stem == name || (stem.size() > name.size() && stem.compare(stem.size() - name.size() - 1, name.size() + 1, "_" + name) == 0);
				if (match && (!best || file.stem().string().size() < best->stem().string().size())) best = file;
			}
			if (best) return best;
		}
		return std::nullopt;
	}
};

struct Piece {
	size_t part = 0;
	size_t mesh = 0;
	Raster3DMaterial material;
	std::array<float, 3> center{}; // in view-independent world units, before the part offset
};

struct Part {
	fs::path path;
	ScxModel model;
	float offsetX = 0;
};

std::array<float, 16> lookAt(const std::array<float, 3>& eye, const std::array<float, 3>& target) {
	using V3 = std::array<float, 3>;
	const auto sub = [](V3 a, V3 b) { return std::array<float, 3>{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
	const auto cross = [](V3 a, V3 b) {
		return std::array<float, 3>{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
	};
	const auto norm = [](std::array<float, 3> a) {
		const float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
		return std::array<float, 3>{a[0] / l, a[1] / l, a[2] / l};
	};
	const auto z = norm(sub(eye, target)); // the camera looks down its own -Z
	const auto x = norm(cross(V3{0, 1, 0}, z));
	const auto y = cross(z, x);
	return {{x[0], x[1], x[2], 0, y[0], y[1], y[2], 0, z[0], z[1], z[2], 0, eye[0], eye[1], eye[2], 1}};
}

} // namespace

int main(int argc, char** argv) {
	Options options;
	if (!parseArgs(argc, argv, options)) { usage(); return 2; }

	// ---- load models
	std::vector<Part> parts;
	float cursor = 0;
	for (const auto& path : options.models) {
		auto result = scxLoadFile(path.string());
		if (!result.ok) { std::fprintf(stderr, "%s: %s (offset %llu)\n", path.string().c_str(), result.error.c_str(),
		                               static_cast<unsigned long long>(result.errorOffset)); return 1; }
		Part part;
		part.path = path;
		part.model = std::move(result.model);
		parts.push_back(std::move(part));
	}

	constexpr float meshScale = 0.1f; // stock mesh scale, as in sceneDrawCameraMesh
	// Bounds per part, then lay parts out in a row along X.
	struct Bounds { float lo[3], hi[3]; };
	std::vector<Bounds> bounds(parts.size());
	for (size_t p = 0; p < parts.size(); ++p) {
		Bounds b{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
		for (const auto& mesh : parts[p].model.meshes)
			for (size_t i = 0; i < mesh.vertexCount; ++i)
				for (int k = 0; k < 3; ++k) {
					const float v = mesh.positions[i * 3 + static_cast<size_t>(k)] * meshScale;
					b.lo[k] = std::min(b.lo[k], v);
					b.hi[k] = std::max(b.hi[k], v);
				}
		if (b.lo[0] > b.hi[0]) { std::fprintf(stderr, "%s: no geometry\n", parts[p].path.string().c_str()); return 1; }
		bounds[p] = b;
		const float width = b.hi[0] - b.lo[0];
		parts[p].offsetX = cursor + width * 0.5f - (b.lo[0] + b.hi[0]) * 0.5f;
		cursor += width * 1.1f;
	}
	float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
	for (size_t p = 0; p < parts.size(); ++p)
		for (int k = 0; k < 3; ++k) {
			const float off = k == 0 ? parts[p].offsetX : 0.0f;
			lo[k] = std::min(lo[k], bounds[p].lo[k] + off);
			hi[k] = std::max(hi[k], bounds[p].hi[k] + off);
		}
	const std::array<float, 3> centre{(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
	const float radius = 0.5f * std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
	                                     (hi[2] - lo[2]) * (hi[2] - lo[2]));

	// ---- materials and textures
	std::map<fs::path, std::unique_ptr<Raster3DTexture>> textureCache;
	std::vector<std::unique_ptr<Raster3DTexture>> explicitTextures;
	std::string error;
	for (const auto& path : options.textures) {
		auto texture = std::make_unique<Raster3DTexture>();
		if (!raster3dLoadTextureFile(path, *texture, error)) { std::fprintf(stderr, "texture: %s\n", error.c_str()); return 1; }
		explicitTextures.push_back(std::move(texture));
	}
	TextureFinder finder;
	Raster3DScxOptions scxOptions;
	scxOptions.allowApproximations = !options.strict;
	scxOptions.ignoreUnknownFunctionBits = options.lenient;
	std::vector<Piece> pieces;
	std::set<std::string> notes;
	size_t texturedPieces = 0;
	for (size_t p = 0; p < parts.size(); ++p) {
		const auto& model = parts[p].model;
		for (size_t m = 0; m < model.meshes.size(); ++m) {
			const auto& mesh = model.meshes[m];
			if (mesh.indices.empty()) continue;
			if (!mesh.materialIndex || *mesh.materialIndex >= model.materials.size()) {
				std::fprintf(stderr, "%s: mesh %zu has no material\n", parts[p].path.string().c_str(), m);
				return 1;
			}
			if (mesh.uv1.size() != static_cast<size_t>(mesh.vertexCount) * 2 && !mesh.uv1.empty()) {
				std::fprintf(stderr, "%s: mesh %zu has an unexpected UV layout\n", parts[p].path.string().c_str(), m);
				return 1;
			}
			const auto& scxMaterial = model.materials[*mesh.materialIndex];
			Raster3DScxMaterial mapped;
			if (!raster3dScxMaterial(scxMaterial, scxOptions, mapped, error)) {
				if (options.skipUnsupported) {
					std::printf("skipped mesh %zu of %s: %s\n", m, parts[p].path.filename().string().c_str(), error.c_str());
					continue;
				}
				std::fprintf(stderr, "%s: mesh %zu: %s\n", parts[p].path.string().c_str(), m, error.c_str());
				return 1;
			}
			for (const auto& note : mapped.notes) notes.insert(note);
			Piece piece;
			piece.part = p;
			piece.mesh = m;
			piece.material = mapped.state;
			piece.material.cull = options.cull;
			piece.material.filter = options.filter;
			if (mapped.baseTextureIndex && !mesh.uv1.empty()) {
				const Raster3DTexture* texture = nullptr;
				if (*mapped.baseTextureIndex < explicitTextures.size()) {
					texture = explicitTextures[*mapped.baseTextureIndex].get();
				} else if (options.autoTexture) {
					for (const std::string& name : {scxMaterial.name, parts[p].path.stem().string()}) {
						const auto found = finder.find(parts[p].path, name);
						if (!found) continue;
						auto it = textureCache.find(*found);
						if (it == textureCache.end()) {
							auto loaded = std::make_unique<Raster3DTexture>();
							if (!raster3dLoadTextureFile(*found, *loaded, error)) {
								std::fprintf(stderr, "texture: %s\n", error.c_str());
								return 1;
							}
							std::printf("texture: material '%s' -> %s (%ux%u)\n", scxMaterial.name.c_str(), found->filename().string().c_str(),
							            loaded->levels[0].width, loaded->levels[0].height);
							it = textureCache.emplace(*found, std::move(loaded)).first;
						}
						texture = it->second.get();
						break;
					}
					if (!texture) std::printf("texture: material '%s' has no matching file; drawn as flat colour\n", scxMaterial.name.c_str());
				}
				if (texture) {
					piece.material.texture = texture;
					++texturedPieces;
					if (texture->hasAlpha && piece.material.blend == Raster3DBlend::Opaque)
						notes.insert("a texture has alpha but the material is opaque; alpha ignored, as the stock does for opaque");
				} else if (!mapped.baseTextureIndex || options.autoTexture || options.textures.empty()) {
					// untextured: flat colour from the diffuse; use a neutral grey when the diffuse is black
				}
			}
			if (!piece.material.texture) {
				// No texture was found: draw the material's diffuse colour (a neutral grey if it is black).
				auto& d = piece.material.diffuse;
				d = {scxMaterial.diffuse[0], scxMaterial.diffuse[1], scxMaterial.diffuse[2], d[3]};
				if (d[0] + d[1] + d[2] < 0.05f && piece.material.blend != Raster3DBlend::AlphaBlend) d = {0.6f, 0.6f, 0.6f, d[3]};
			}
			for (int k = 0; k < 3; ++k) {
				float sum = 0;
				for (size_t i = 0; i < mesh.vertexCount; ++i) sum += mesh.positions[i * 3 + static_cast<size_t>(k)] * meshScale;
				piece.center[static_cast<size_t>(k)] = sum / static_cast<float>(mesh.vertexCount) + (k == 0 ? parts[p].offsetX : 0.0f);
			}
			if (options.list) {
				std::printf("piece %zu.%zu '%s' type 0x%x function 0x%08x vertexFlags 0x%x triangles %zu blend %d texture %s vertexColour %s\n",
				            p, m, scxMaterial.name.c_str(), scxMaterial.flags, scxMaterial.functionWord, mesh.vertexFlags,
				            mesh.indices.size() / 3, static_cast<int>(piece.material.blend), piece.material.texture ? "yes" : "no",
				            piece.material.useVertexColor && !mesh.color.empty() ? "yes" : "no");
			}
			pieces.push_back(piece);
		}
	}
	if (pieces.empty()) { std::fprintf(stderr, "nothing to draw\n"); return 1; }
	size_t triangles = 0;
	for (const auto& piece : pieces) triangles += parts[piece.part].model.meshes[piece.mesh].indices.size() / 3;
	std::printf("model_view: %zu part(s), %zu mesh piece(s), %zu triangles, %zu textured\n", parts.size(), pieces.size(), triangles, texturedPieces);
	for (const auto& note : notes) std::printf("note: %s\n", note.c_str());

	// ---- output
	std::unique_ptr<Platform> platform;
	std::unique_ptr<Render2D> renderer;
	if (!options.headless) {
		platform = Platform::create("SCX model viewer", static_cast<int>(options.width), static_cast<int>(options.height), error);
		if (!platform) { std::fprintf(stderr, "window: %s\n", error.c_str()); return 1; }
		renderer = Render2D::forWindow(platform->window(), error);
		if (!renderer) { std::fprintf(stderr, "renderer: %s\n", error.c_str()); return 1; }
	}
	Raster3D raster;
	if (!raster.resize(options.width, options.height, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }

	// Camera: stock objects, near/far in metres (makeCameraResource converts to world units).
	const float aspect = static_cast<float>(options.width) / static_cast<float>(options.height);
	// makeCameraResource multiplies near and far by 10 (metres to decimetre world units).
	const CameraResource camera = makeCameraResource(0, options.fov, radius * 0.02f / 10.0f, radius * 40.0f / 10.0f, 0, 1, 0, 0);

	const auto renderFrame = [&](double time) -> bool {
		const float azimuth = (options.angle + options.spin * static_cast<float>(time)) * 3.14159265f / 180.0f;
		const float elevation = options.elevation * 3.14159265f / 180.0f;
		const float distance = radius / std::sin(options.fov * 0.5f * 3.14159265f / 180.0f) * 0.85f / options.zoom;
		const std::array<float, 3> eye{centre[0] + distance * std::cos(elevation) * std::sin(azimuth),
		                               centre[1] + distance * std::sin(elevation),
		                               centre[2] + distance * std::cos(elevation) * std::cos(azimuth)};
		const auto world = lookAt(eye, centre);
		const auto state = cameraViewState(camera, world, aspect);
		if (!state) { std::fprintf(stderr, "camera matrices unavailable\n"); return false; }
		raster.clear(38, 46, 62);
		raster.resetStats();
		// Opaque and alpha-tested first in file order, then blended pieces far to near, as the stock's
		// sorted transparent pass does (00525930 orders by camera depth of the object).
		std::vector<size_t> order;
		std::vector<std::pair<float, size_t>> blended;
		for (size_t i = 0; i < pieces.size(); ++i) {
			if (pieces[i].material.blend != Raster3DBlend::AlphaBlend) { order.push_back(i); continue; }
			const auto& c = pieces[i].center;
			const float dx = c[0] - eye[0], dy = c[1] - eye[1], dz = c[2] - eye[2];
			blended.push_back({dx * dx + dy * dy + dz * dz, i});
		}
		std::sort(blended.begin(), blended.end(), [](auto& a, auto& b) { return a.first > b.first; });
		for (const auto& entry : blended) order.push_back(entry.second);
		for (size_t index : order) {
			const Piece& piece = pieces[index];
			const Part& part = parts[piece.part];
			const ScxMesh& mesh = part.model.meshes[piece.mesh];
			Raster3DDraw d;
			d.positions = mesh.positions.data();
			d.vertexCount = mesh.vertexCount;
			d.uvs = mesh.uv1.empty() ? nullptr : mesh.uv1.data();
			d.normals = mesh.normals.empty() ? nullptr : mesh.normals.data();
			d.colors = mesh.color.empty() ? nullptr : mesh.color.data();
			d.indices = mesh.indices.data();
			d.indexCount = mesh.indices.size();
			d.positionScale = meshScale;
			d.model[12] = part.offsetX;
			d.view = state->view;
			d.projection = state->projection;
			d.material = piece.material;
			if (!d.uvs) d.material.texture = nullptr;
			d.light.enabled = options.light;
			if (!raster.draw(d, error)) { std::fprintf(stderr, "draw: %s\n", error.c_str()); return false; }
		}
		return true;
	};

	using Clock = std::chrono::steady_clock;
	const auto seconds = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); };

	if (options.headless) {
		const uint32_t frames = options.bench ? options.bench : options.captureFrame + 1;
		const auto start = Clock::now();
		for (uint32_t f = 0; f < frames; ++f)
			if (!renderFrame(f / 60.0)) return 1;
		const double elapsed = seconds(start, Clock::now());
		const auto& s = raster.stats();
		std::printf("rendered %u frame(s) in %.3f s: %.2f ms per frame, %.1f frames per second\n", frames, elapsed,
		            1000.0 * elapsed / frames, frames / elapsed);
		std::printf("last frame: %llu triangles submitted, %llu clipped, %llu outside, %llu rasterized, %llu pixels written, %llu depth-rejected\n",
		            static_cast<unsigned long long>(s.trianglesSubmitted), static_cast<unsigned long long>(s.trianglesClipped),
		            static_cast<unsigned long long>(s.trianglesRejectedOutside), static_cast<unsigned long long>(s.trianglesRasterized),
		            static_cast<unsigned long long>(s.pixelsWritten), static_cast<unsigned long long>(s.pixelsDepthRejected));
	} else {
		const auto start = Clock::now();
		uint64_t frames = 0;
		double rasterTime = 0;
		bool running = true;
		const bool fixedStep = !options.capture.empty() && options.seconds == 0;
		while (running) {
			for (const auto& event : platform->poll()) {
				if (std::holds_alternative<PlatformQuit>(event)) running = false;
				if (const auto* key = std::get_if<PlatformKey>(&event))
					if (key->down && key->scancode == SDL_SCANCODE_ESCAPE) running = false;
			}
			if (!running) break;
			const double now = seconds(start, Clock::now());
			const auto t0 = Clock::now();
			if (!renderFrame(fixedStep ? frames / 60.0 : now)) return 1;
			rasterTime += seconds(t0, Clock::now());
			if (!renderer->clear(0, 0, 0, 255, error) || !raster.blit(*renderer, error) || !renderer->present(error)) {
				std::fprintf(stderr, "present: %s\n", error.c_str());
				return 1;
			}
			++frames;
			if (options.seconds > 0 && now >= options.seconds) break;
			if (fixedStep && frames > options.captureFrame) break;
		}
		const double elapsed = seconds(start, Clock::now());
		if (frames) std::printf("window: %llu frames in %.2f s (%.1f frames per second); software raster %.2f ms per frame\n",
		                        static_cast<unsigned long long>(frames), elapsed, frames / elapsed, 1000.0 * rasterTime / frames);
	}
	if (!options.capture.empty()) {
		if (!raster.saveBmp(options.capture, error)) { std::fprintf(stderr, "capture: %s\n", error.c_str()); return 1; }
		std::printf("saved %s\n", options.capture.c_str());
	}
	return 0;
}
