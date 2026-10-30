#include "force_render.hpp"

#include "camera_frame.hpp"
#include "render2d.hpp"
#include "resource_world.hpp"
#include "world_model.hpp"

bool forceRenderFrames(GameWorld& world, ResourceWorld& resources, Render2D& renderer,
                      bool videoPlaying, std::optional<int32_t> instanceMax,
                      std::optional<int32_t> soundMax, std::optional<int32_t> textureMax,
                      std::optional<int32_t> meshMax, std::optional<int32_t> loadRate,
                      FrameVisibilityState* visibility, std::string& error,
                      const std::function<bool(std::string&)>& drawVideo, bool videoPressureAccounted) {
	for (int pass = 0; pass < 7; ++pass) {
		if (resources.pendingPackLoads()) {
			if (!loadRate || *loadRate < 0) { error = "resource load rate unavailable"; return false; }
			if (!resources.completePendingPackLoads(static_cast<uint32_t>(*loadRate), error)) return false;
		}
		if (!resources.forceRenderPreflight(error, instanceMax, soundMax, textureMax, meshMax)) return false;
		if (resources.pendingPackLoads()) { error = "pending load queue unsupported"; return false; }
		if (videoPlaying && !drawVideo) { error = "video renderer unavailable"; return false; }
		if (videoPlaying && !videoPressureAccounted) { error = "video texture pressure unavailable"; return false; }
		const auto viewports = world.activeViewports();
		std::optional<FrameVisibilityControls> controls;
		if (!viewports.empty()) {
			if (!visibility || !(controls = visibility->controls(error))) {
				if (!visibility) error = "visibility controls unavailable";
				return false;
			}
		}
		if (!resources.ageRecentUses(error)) return false;
		// Stock presents the preceding frame before beginning the next scene.
		// SDL's empty scene uses a black clear; device/gamma behavior differs.
		if (!renderer.present(error) || !renderer.clear(0, 0, 0, 255, error)) return false;
		if (videoPlaying && !drawVideo(error)) return false;
		for (auto* viewport : viewports)
			if (!renderActiveViewport(world, *viewport, controls->worldScale, controls->threshold,
			                          controls->frustumChecks, error, true)) return false;
		world.setFrameCounter(world.frameCounter() + 1);
	}
	error.clear(); return true;
}
