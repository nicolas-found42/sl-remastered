#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class GameWorld;
class Render2D;
class ResourceWorld;
class FrameVisibilityState;
struct VmArena;
struct VmStaticFields;

// Bounded SDL adapter for seven passes, using the supported active camera path.
// Unknown resource pressure and video rendering remain unsupported.
bool forceRenderFrames(GameWorld& world, ResourceWorld& resources, Render2D& renderer,
                      bool videoPlaying, std::optional<int32_t> instanceMax,
                      std::optional<int32_t> soundMax, std::optional<int32_t> textureMax,
                      std::optional<int32_t> meshMax, std::optional<int32_t> loadRate,
                      FrameVisibilityState* visibility, std::string& error,
                      const std::function<bool(std::string&)>& drawVideo = {},
                      bool videoPressureAccounted = false, VmArena* arena = nullptr,
                      const std::vector<VmStaticFields>* staticFields = nullptr);
