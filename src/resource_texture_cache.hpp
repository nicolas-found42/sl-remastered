#pragma once

#include <string>

class ResourceWorld;
class Render2D;
struct ResolvedNode;
struct WorldNode;

// Loads and retains a type-7 renderer texture. SDL upload is immediate; stock
// asynchronous readiness and subsequent LOD promotion are not represented.
bool cacheResourceTexture(ResourceWorld& resources, Render2D& renderer, const ResolvedNode& resolved, float demand, std::string& error);
WorldNode *sceneCloneTexture(ResourceWorld &world, Render2D &renderer,
                             const ResolvedNode &source, std::string &error);
