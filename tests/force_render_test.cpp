#include "force_render.hpp"
#include "camera_frame.hpp"
#include "pack_registry.hpp"
#include "render2d.hpp"
#include "resource_world.hpp"
#include "world_model.hpp"
#include "vm.hpp"

#include <SDL3/SDL.h>
#include <cstdio>

static int failures;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

int main() {
	CHECK(SDL_Init(SDL_INIT_VIDEO));
	auto* surface = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
	std::string error;
	auto renderer = Render2D::forSurface(surface, error);
	CHECK(renderer);
	if (renderer) {
		PackRegistry packs("ref/raw"); GameWorld world; CHECK(world.addRoot()); ResourceWorld resources(world, packs);
		const auto initial = world.frameCounter();
		CHECK(forceRenderFrames(world, resources, *renderer, false, 1000, 1000, 1000, 1000, 1, nullptr, error));
		CHECK(world.frameCounter() == initial + 7);
		uint32_t soundId = 0;
		CHECK(packs.resolveLiteral("frontend.rpk", 0x79, soundId, error));
		const auto sound = resources.lookup(soundId, 6);
		CHECK(sound.node.packNode && resources.queueSoundPrecache(*sound.node.packNode, error));
		CHECK(forceRenderFrames(world, resources, *renderer, false, 1000, 2000000000, 1000, 1000, 2, nullptr, error));
		CHECK(resources.pendingPackLoads() == 0 && sound.node.packNode->loaded() && sound.node.packNode->sound);
		const auto before = world.frameCounter();
		CHECK(!forceRenderFrames(world, resources, *renderer, true, 1000, 1000, 1000, 1000, 1, nullptr, error));
		CHECK(world.frameCounter() == before);
		int videoDraws = 0;
		CHECK(forceRenderFrames(world, resources, *renderer, true, 1000, 2000000000, 1000, 1000, 1, nullptr, error,
		                        [&](std::string&) { ++videoDraws; return true; }, true));
		CHECK(videoDraws == 7 && world.frameCounter() == before + 7);
		const uint8_t white[] = {255, 255, 255, 255};
		auto texture = renderer->texture(1, 1, white, sizeof white, error); CHECK(texture);
		CHECK(forceRenderFrames(world, resources, *renderer, false, 1000, 2000000000, 1000, 1000, 1, nullptr, error));
		if (texture) CHECK(renderer->releaseTexture(*texture, error));
		auto* viewport = world.addTypedChild(*world.defaultParent(), 18, "viewport", 0); CHECK(viewport);
		CHECK(forceRenderFrames(world, resources, *renderer, false, 1000, 2000000000, 1000, 1000, 1, nullptr, error));
		if (viewport) {
			viewport->viewport = ViewportInstance{0, 0, 0, 1, 1, ViewportList::Pending, {}};
			world.registerViewport(*viewport, 0); CHECK(world.activateViewport(*viewport));
			viewport->viewport->list = ViewportList::Active;
			const auto activeFrame = world.frameCounter();
			CHECK(!forceRenderFrames(world, resources, *renderer, false, 1000, 2000000000, 1000, 1000, 1, nullptr, error));
			CHECK(world.frameCounter() == activeFrame);
			TufaClass config; config.name = "java.util.Config";
			std::vector<VmStaticFields> states{{&config, {{"object_detail", 0.4f, "F"}, {"object_detail_amp", 2.0f, "F"}}}};
			FrameVisibilityState visibility; CHECK(visibility.applyConfig(states, error));
			CHECK(forceRenderFrames(world, resources, *renderer, false, 1000, 2000000000, 1000, 1000, 1, &visibility, error));
			CHECK(world.frameCounter() == activeFrame + 7);
			auto* camera = world.addTypedChild(*world.defaultParent(), 3, "camera", 0); CHECK(camera);
			if (camera) {
				viewport->viewport->renderAttachments = std::make_shared<ViewportRenderAttachments>();
				viewport->viewport->renderAttachments->prepare(*camera, 0);
				CHECK(viewport->viewport->renderAttachments->activate(*camera, 0));
				const auto failedFrame = world.frameCounter();
				CHECK(!forceRenderFrames(world, resources, *renderer, false, 1000, 2000000000, 1000, 1000, 1, &visibility, error));
				CHECK(error.find("camera attachment unavailable") != std::string::npos && world.frameCounter() == failedFrame);
			}
		}
	}
	renderer.reset(); if (surface) SDL_DestroySurface(surface); SDL_Quit();
	return failures ? 1 : 0;
}
