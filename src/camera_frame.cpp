#include <map>
#include "camera_frame.hpp"
#include "run_log.hpp"

#include "resource_world.hpp"
#include "vm.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>

bool FrameVisibilityState::applyConfig(const std::vector<VmStaticFields>& states, std::string& error) {
	const VmStaticFields* config = nullptr;
	for (const auto& state : states) if (state.owner && state.owner->name == "java.util.Config") {
		if (config) { error = "frame visibility: ambiguous Config"; return false; }
		config = &state;
	}
	if (!config) { error = "frame visibility: Config unavailable"; return false; }
	std::optional<float> detail, amplitude;
	for (const auto& field : config->fields) {
		if (field.name != "object_detail" && field.name != "object_detail_amp") continue;
		auto& value = field.name == "object_detail" ? detail : amplitude;
		const auto* number = std::get_if<float>(&field.value);
		if (value || field.descriptor != "F" || !number || !std::isfinite(*number)) {
			error = "frame visibility: invalid Config field"; return false;
		}
		value = *number;
	}
	if (!detail || !amplitude) { error = "frame visibility: Config fields unavailable"; return false; }
	detail_ = detail; amplitude_ = amplitude; error.clear(); return true;
}

void FrameVisibilityState::setPriority(int32_t priority) {
	priority_ = priority != 0;
	loadFactor_ = priority_ ? -1.0f : 0.1f;
}

std::optional<FrameVisibilityControls> FrameVisibilityState::controls(std::string& error) {
	if (!detail_ || !amplitude_ || *amplitude_ == 0) {
		error = "frame visibility: Config not applied or zero amplitude"; return std::nullopt;
	}
	float detail = *detail_;
	if (!priority_ && detail == -1.0f) detail = savedDetail_;
	else if (priority_ && detail > -1.0f) savedDetail_ = detail;
	const float threshold = detail / *amplitude_;
	if (!std::isfinite(threshold)) { error = "frame visibility: invalid threshold"; return std::nullopt; }
	detail_ = detail;
	error.clear();
	return FrameVisibilityControls{*amplitude_, threshold, !priority_};
}

CameraFrame::~CameraFrame() { setLinkOwner(camera_, nullptr); }

bool CameraFrame::draw(WorldNode& cameraInstance, const ViewportInstance& viewport,
	                       float worldScale, float threshold, bool frustumChecks, std::string& error,
	                       bool requireCached, VmArena* arena,
	                       const std::vector<VmStaticFields>* staticFields) {
	if (cameraInstance.type != 3 || !cameraInstance.loaded || viewport.list != ViewportList::Active || !viewport.renderAttachments) {
		error = "camera frame: active camera attachment unavailable"; return false;
	}
	const auto active = viewport.renderAttachments->active();
	if (std::find(active.begin(), active.end(), &cameraInstance) == active.end()) {
		error = "camera frame: camera is not attached to viewport"; return false;
	}
	if (bound_ && (camera_.owner != &cameraInstance || attachments_.lock() != viewport.renderAttachments)) {
		error = "camera frame: camera or viewport changed"; return false;
	}
	const auto node = resources_.resolve(&cameraInstance);
	const auto instance = node ? resources_.sceneRenderInstance(node) : nullptr;
	if (!instance) { error = "camera frame: camera instance unavailable"; return false; }
	const auto type = resources_.lookup(instance->typeId(), 12);
	if (type.status != LookupStatus::Found || !type.node.worldNode || !type.node.worldNode->loaded || !type.node.worldNode->camera) {
		error = "camera frame: camera resource unavailable"; return false;
	}
	int width, height;
	if (!renderer_.outputSize(width, height, error)) return false;
	const float pixelWidth = viewport.width * width, pixelHeight = viewport.height * height;
	if (!std::isfinite(pixelWidth) || !std::isfinite(pixelHeight) || pixelWidth <= 0 || pixelHeight <= 0) {
		error = "camera frame: invalid viewport"; return false;
	}
	if (instance->hasPhysicsBoneAttachment() && !instance->refreshPhysicsBone()) {
		error = "camera frame: physics bone unavailable"; return false;
	}
	instance->processRootBoneUpdate();
	const auto& cameraWorld = instance->derivedRootBoneMatrix();
	{
		static std::map<uint32_t, double> lastCamLogs;
		const double nowCam = runlog::monotonicSeconds();
		double& lastCamLog = lastCamLogs.try_emplace(cameraInstance.instanceId, -1e9).first->second;
		if (nowCam - lastCamLog >= 5.0) {
			lastCamLog = nowCam;
			runlog::event(runlog::EventKind::Note,
			              "camera world id=0x%x t=(%.1f,%.1f,%.1f) scale=(%.3f,%.3f,%.3f)",
			              cameraInstance.instanceId, cameraWorld[12],
			              cameraWorld[13], cameraWorld[14],
			              std::sqrt(cameraWorld[0]*cameraWorld[0] + cameraWorld[1]*cameraWorld[1] + cameraWorld[2]*cameraWorld[2]),
			              std::sqrt(cameraWorld[4]*cameraWorld[4] + cameraWorld[5]*cameraWorld[5] + cameraWorld[6]*cameraWorld[6]),
			              std::sqrt(cameraWorld[8]*cameraWorld[8] + cameraWorld[9]*cameraWorld[9] + cameraWorld[10]*cameraWorld[10]));
		}
	}
	const auto& camera = *type.node.worldNode->camera;
	// FUN_00510fc0: a non-zero plane-test mode resets the camera instance
	// node's bounds block to {mode 0, center 0, extents 100} every frame.
	if (camera.planeTest != 0) {
		cameraInstance.bounds.mode = 0;
		cameraInstance.bounds.values = {0.0f, 0.0f, 0.0f, 100.0f, 100.0f, 100.0f};
	}
	const auto state = cameraViewState(camera, cameraWorld, pixelWidth / pixelHeight);
	if (!state) { error = "camera frame: camera matrices unavailable"; return false; }
	const auto params = cameraTraversalParams(camera, *state, cameraWorld, worldScale, threshold, frustumChecks);
	if (!params) { error = "camera frame: visibility inputs unavailable"; return false; }
	auto* root = cameraTraversalRoot(cameraInstance);
	if (!root) { error = "camera frame: traversal root unavailable"; return false; }
	if (!bound_) {
		setLinkOwner(camera_, &cameraInstance);
		attachments_ = viewport.renderAttachments;
		bound_ = true;
	}
	if (!traverseCameraCandidates(resources_.world(), *root, materials_.candidates(), *params, error,
	                              &type1Records_, &resources_, arena, staticFields)) {
		materials_.candidates().discardPending();
		return false;
	}
	return materials_.draw(state->view, state->projection, viewport, error, requireCached,
	                       camera.fog ? &*camera.fog : nullptr);
}

bool renderActiveViewport(GameWorld& world, WorldNode& viewportNode, float worldScale,
	                          float threshold, bool frustumChecks, std::string& error,
	                          bool requireCached, VmArena* arena,
	                          const std::vector<VmStaticFields>* staticFields) {
	if (viewportNode.type != 18 || !viewportNode.viewport ||
	    viewportNode.viewport->list != ViewportList::Active) {
		error = "frame host: viewport is not active"; return false;
	}
	const auto& viewport = *viewportNode.viewport;
	if (!viewport.renderAttachments) { error.clear(); return true; }
	for (auto* owner : viewport.renderAttachments->active()) {
		auto* camera = world.nodeFor(owner);
		if (!camera || camera->cameraViewport.owner != &viewportNode || !camera->cameraFrame) {
			error = "frame host: camera attachment unavailable"; return false;
		}
		if (!camera->cameraFrame->draw(*camera, viewport, worldScale, threshold, frustumChecks,
		                              error, requireCached, arena, staticFields)) return false;
	}
	error.clear(); return true;
}

bool notifyFrontendAfterFrame(VmScheduler& scheduler, const std::vector<VmStaticFields>& states,
                              std::string& error) {
	for (const auto& state : states) {
		if (!state.owner || state.owner->name != "java.render.Frontend") continue;
		for (const auto& field : state.fields) if (field.name == "render") {
			const auto* object = std::get_if<const VmObject*>(&field.value);
			if (!object) { error = "frame host: invalid Frontend.render"; return false; }
			if (*object) {
				const auto result = scheduler.notify(*object, true);
				if (!result.ok) { error = result.error; return false; }
			}
			error.clear(); return true;
		}
		break;
	}
	error.clear(); return true;
}


bool runFrameSchedulerSlice(VmScheduler& scheduler, std::string& error) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
	do {
		const auto before = scheduler.threadProgress();
		const auto result = scheduler.tick(64);
		if (!result.ok) { error = result.error; return false; }
		if (scheduler.threadProgress() == before) break;
	} while (std::chrono::steady_clock::now() < deadline);
	error.clear(); return true;
}
