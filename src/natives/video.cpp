#include "natives/video.hpp"
#include "natives/game_type.hpp"
#include <cmath>

NativeResult gfxOpenVideo(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                          const std::vector<const TufaClass*>*) {
	if (!arena || args.size() != 3 || !std::holds_alternative<const VmObject*>(args[0]) ||
	    !std::holds_alternative<int32_t>(args[1]) || !std::holds_alternative<int32_t>(args[2]))
		return std::string{"GfxEngine.openVideo: invalid call"};
	const auto* object = std::get<const VmObject*>(args[0]);
	if (!object) return VmValue{int32_t{-1}};
	if (std::get<int32_t>(args[1]) != 1) return std::string{"GfxEngine.openVideo: only texture mode is supported"};
	const int32_t loop = std::get<int32_t>(args[2]);
	if (loop != 0 && loop != 1) return std::string{"GfxEngine.openVideo: unsupported loop value"};
	if (!arena->gameState || !arena->gameState->video || !arena->performanceMilliseconds)
		return std::string{"GfxEngine.openVideo: video host unavailable"};
	const std::string* path = nullptr;
	for (const auto& entry : arena->strings) if (entry.second == object) {
		if (path) return std::string{"GfxEngine.openVideo: ambiguous string"};
		path = &entry.first;
	}
	if (!path) return std::string{"GfxEngine.openVideo: unknown string"};
	const double now = arena->performanceMilliseconds() / 1000.0;
	std::string error;
	if (!std::isfinite(now) || !arena->gameState->video->open(*path, loop != 0, now, error))
		return "GfxEngine.openVideo: " + error;
	return VmValue{int32_t{0}};
}
NativeResult gfxCloseVideo(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                           const std::vector<const TufaClass*>*) {
	if (!args.empty() || !arena || !arena->gameState || !arena->gameState->video)
		return std::string{"GfxEngine.closeVideo: video host unavailable"};
	arena->gameState->video->close(); return std::monostate{};
}
NativeResult gfxIsPlayingVideo(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (!args.empty() || !arena || !arena->gameState || !arena->gameState->video)
		return std::string{"GfxEngine.isPlayingVideo: video host unavailable"};
	return VmValue{int32_t{arena->gameState->video->playing() ? 1 : 0}};
}
