#pragma once

#include "camera_traversal.hpp"
#include "render2d.hpp"

struct VmStaticFields;
struct FrameVisibilityControls { float worldScale; float threshold; bool frustumChecks; };
class FrameVisibilityState {
public:
	bool applyConfig(const std::vector<VmStaticFields>& states, std::string& error);
	void setPriority(int32_t priority);
	std::optional<FrameVisibilityControls> controls(std::string& error);
	float loadFactor() const { return loadFactor_; }
private:
	std::optional<float> detail_;
	std::optional<float> amplitude_;
	float savedDetail_ = -1;
	float loadFactor_ = 0.1f;
	bool priority_ = false;
};

// Bounded frame adapter. ResourceWorld and Render2D outlive this object;
// the caller supplies live global visibility inputs and owns clear/present.
class CameraFrame {
public:
	CameraFrame(ResourceWorld& resources, Render2D& renderer)
		: resources_(resources), renderer_(renderer), materials_(resources, renderer) {}
	~CameraFrame();
	CameraFrame(const CameraFrame&) = delete;
	CameraFrame& operator=(const CameraFrame&) = delete;
	bool draw(WorldNode& cameraInstance, const ViewportInstance& viewport,
	          float worldScale, float threshold, bool frustumChecks, std::string& error, bool requireCached = false);
private:
	ResourceWorld& resources_;
	Render2D& renderer_;
	SceneCameraRenderer materials_;
	CameraType1Records type1Records_;
	OwnerLink camera_;
	std::weak_ptr<ViewportRenderAttachments> attachments_;
	bool bound_ = false;
};

// Draws the ordered camera attachments of one active runtime viewport. The
// caller owns clear/present and supplies live stock visibility controls.
bool renderActiveViewport(GameWorld& world, WorldNode& viewportNode, float worldScale,
	                          float threshold, bool frustumChecks, std::string& error, bool requireCached = false);

// Stock 00428960 notifies Frontend.render after a completed engine frame.
class VmScheduler;
bool notifyFrontendAfterFrame(VmScheduler& scheduler, const std::vector<VmStaticFields>& states,
                              std::string& error);

// Host approximation of the stock 10 ms pre-render script slice.
bool runFrameSchedulerSlice(VmScheduler& scheduler, std::string& error);
