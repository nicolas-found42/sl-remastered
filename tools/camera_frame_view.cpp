#include "camera_frame.hpp"
#include "pack_registry.hpp"
#include "platform.hpp"
#include "resource_world.hpp"

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

int main(int argc, char** argv) {
	if (argc != 2 && argc != 4) {
		std::fputs("usage: camera_frame_view <game-root> [--timeout-ms <milliseconds>]\n", stderr); return 2;
	}
	unsigned long timeout = 0;
	if (argc == 4) {
		char* end = nullptr;
		timeout = std::strtoul(argv[3], &end, 10);
		if (std::string(argv[2]) != "--timeout-ms" || argv[3][0] == '-' || end == argv[3] || *end || !timeout || timeout > 60000) {
			std::fputs("invalid timeout\n", stderr); return 2;
		}
	}
	std::string error;
	auto platform = Platform::create("Camera frame diagnostic", 800, 700, error);
	if (!platform) { std::fprintf(stderr, "window: %s\n", error.c_str()); return 1; }
	auto renderer = Render2D::forWindow(platform->window(), error);
	if (!renderer) { std::fprintf(stderr, "renderer: %s\n", error.c_str()); return 1; }
	PackRegistry packs(argv[1]);
	GameWorld world;
	auto* root = world.addRoot();
	if (!root) { std::fputs("world root unavailable\n", stderr); return 1; }
	ResourceWorld resources(world, packs);
	uint32_t materialId = 0, loadingId = 0;
	if (!packs.resolveLiteral("frontend.rpk", 0xd7, materialId, error) ||
	    !packs.resolveLiteral("frontend.rpk", 0xa8, loadingId, error)) {
		std::fprintf(stderr, "resource: %s\n", error.c_str()); return 1;
	}
	const auto materialNode = resources.lookup(materialId, 14);
	const auto loadingNode = resources.lookup(loadingId, 7);
	auto material = sceneLoadMaterial(resources, materialId);
	if (materialNode.status != LookupStatus::Found || loadingNode.status != LookupStatus::Found || !material.material ||
	    !resources.bindSceneMaterial(materialNode.node, std::shared_ptr<SceneMaterialResources>(std::move(material.material)))) {
		std::fprintf(stderr, "material: %s\n", material.error.c_str()); return 1;
	}
	auto* cloneNode = sceneCloneMaterial(resources, materialNode.node, error);
	if (!cloneNode) { std::fprintf(stderr, "material clone: %s\n", error.c_str()); return 1; }
	auto clone = resources.sceneMaterial(resources.resolve(cloneNode));
	if (!clone || !clone->replace(clone->textureId(0), loadingNode.node.owner, [] {})) {
		std::fputs("material texture replacement failed\n", stderr); return 1;
	}
	auto* sceneRoot = world.addChild(*root, "diagnostic-scene", 0);
	auto* cameraType = world.addTypedChild(*root, 12, "diagnostic-camera-type", 0);
	if (!sceneRoot || !cameraType) { std::fputs("scene allocation failed\n", stderr); return 1; }
	auto* rectangle = world.addTypedChild(*sceneRoot, 3, "diagnostic-rectangle", 0);
	auto* camera = world.addTypedChild(*sceneRoot, 3, "diagnostic-camera", 0);
	if (!rectangle || !camera) { std::fputs("instance allocation failed\n", stderr); return 1; }
	world.markLoaded(*sceneRoot); world.markLoaded(*cameraType); world.markLoaded(*camera);
	cameraType->camera = makeCameraResource(1, 60, 0.5f, 100, 0, 1, 0, 0);
	auto renderInstance = std::make_shared<SceneRenderInstance>(cloneNode);
	renderInstance->setBoneTranslation(0, 0, -40);
	auto cameraInstance = std::make_shared<SceneRenderInstance>(cameraType);
	if (!resources.bindSceneRenderInstance(resources.resolve(rectangle), renderInstance) ||
	    !resources.bindSceneRenderInstance(resources.resolve(camera), cameraInstance)) {
		std::fputs("instance binding failed\n", stderr); return 1;
	}
	auto attachments = std::make_shared<ViewportRenderAttachments>();
	attachments->prepare(*camera, 0);
	if (!attachments->activate(*camera, -1)) { std::fputs("camera attachment failed\n", stderr); return 1; }
	ViewportInstance viewport{0, 0, 0, 1, 1, ViewportList::Active, attachments};
	CameraFrame frame(resources, *renderer);
	const Uint64 start = SDL_GetTicks();
	bool running = true;
	bool verified = false;
	while (running && (!timeout || SDL_GetTicks() - start < timeout)) {
		for (const auto& event : platform->poll()) {
			if (std::holds_alternative<PlatformQuit>(event)) running = false;
			if (const auto* key = std::get_if<PlatformKey>(&event))
				if (key->down && key->scancode == SDL_SCANCODE_ESCAPE) running = false;
		}
		if (!running) break;
		if (!renderer->clear(0, 0, 0, 255, error) ||
		    !frame.draw(*camera, viewport, 1, 0, false, error)) {
			std::fprintf(stderr, "camera frame: %s\n", error.c_str()); return 1;
		}
		if (!verified) {
			SDL_Surface* pixels = SDL_RenderReadPixels(SDL_GetRenderer(platform->window()), nullptr);
			if (!pixels) { std::fprintf(stderr, "readback: %s\n", SDL_GetError()); return 1; }
			Uint8 r = 0, g = 0, b = 0, a = 0;
			const bool read = SDL_ReadSurfacePixel(pixels, pixels->w / 2, pixels->h / 2, &r, &g, &b, &a);
			SDL_DestroySurface(pixels);
			if (!read || (r == 0 && g == 0 && b == 0)) { std::fputs("readback: center pixel is black\n", stderr); return 1; }
			std::printf("camera frame center: %u %u %u %u\n", r, g, b, a);
			std::fflush(stdout);
			verified = true;
		}
		if (!renderer->present(error)) { std::fprintf(stderr, "present: %s\n", error.c_str()); return 1; }
		SDL_Delay(16);
	}
	return 0;
}
