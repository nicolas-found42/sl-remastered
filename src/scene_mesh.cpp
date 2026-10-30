#include "resource_world.hpp"
#include "scene_2d.hpp"
#include "scene_render_light.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

static bool sceneLoadMeshImpl(ResourceWorld &world, uint32_t id,
                              const std::filesystem::path &root, ScxModel &model,
                              std::string &error, bool allowFontFlags,
                              bool allowGroundScy) {
  const auto node = world.lookup(id, 5);
  if (node.status != LookupStatus::Found) {
    error = "linked mesh unavailable";
    return false;
  }
  if (const auto existing = world.sceneMesh(node.node)) {
    model = *existing;
    error.clear();
    return true;
  }
  if (node.node.worldNode) {
    error = "runtime mesh unavailable";
    return false;
  }
  if (!node.node.packNode) {
    error = "mesh pack unavailable";
    return false;
  }
  PackResource resource;
  if (!world.packs().lookup(id, resource, error) || !resource.packBytes)
    return false;
  if (node.node.packNode->payloadSlot != (id >> 16) ||
      node.node.packNode->payloadOffset != resource.entry.payloadOffset ||
      node.node.packNode->payloadSize != resource.entry.payloadSize) {
    error = "mesh payload identity unsupported";
    return false;
  }
  const size_t offset = resource.entry.payloadOffset,
               size = resource.entry.payloadSize;
  if (!size || size > 4096 || offset > resource.packBytes->size() ||
      size > resource.packBytes->size() - offset) {
    error = "invalid mesh payload";
    return false;
  }
  const std::string payload(resource.packBytes->begin() + offset,
                            resource.packBytes->begin() + offset + size);
  if (payload.find('\0') != std::string::npos) {
    error = "invalid mesh payload";
    return false;
  }
  std::istringstream input(payload);
  std::string line, source;
  bool fontFlagsSeen = false;
  while (std::getline(input, line)) {
    const auto begin = line.find_first_not_of(" \t\r");
    if (begin == std::string::npos)
      continue;
    const auto split = line.find_first_of(" \t", begin);
    if (split == std::string::npos) {
      error = "unsupported mesh directive";
      return false;
    }
    const auto first = line.find_first_not_of(" \t", split);
    const auto last = line.find_last_not_of(" \t\r");
    if (first == std::string::npos || first > last) {
      error = "empty mesh directive";
      return false;
    }
    const auto key = line.substr(begin, split - begin);
    const auto value = line.substr(first, last - first + 1);
    if (key == "sourcefile" && source.empty())
      source = value;
    else if (allowFontFlags && key == "flags" && !fontFlagsSeen &&
             (value == "16" || value == "16.000"))
      fontFlagsSeen = true;
    else {
      error = "unsupported mesh directive";
      return false;
    }
  }
  if (source.empty()) {
    error = "missing mesh source";
    return false;
  }
  std::replace(source.begin(), source.end(), '\\', '/');
  if (source.front() == '/' || source.find(':') != std::string::npos) {
    error = "unsafe mesh source";
    return false;
  }
  const std::filesystem::path relative(source);
  auto extension = relative.extension().string();
  std::transform(
      extension.begin(), extension.end(), extension.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (extension != ".scx" && !(allowGroundScy && extension == ".scy")) {
    error = "unsupported mesh file format";
    return false;
  }
  for (const auto &part : relative)
    if (part == "." || part == ".." || part.empty()) {
      error = "unsafe mesh source";
      return false;
    }
  std::error_code ec;
  const auto base = std::filesystem::canonical(root, ec);
  if (ec) {
    error = "invalid resource root";
    return false;
  }
  const auto target = std::filesystem::canonical(base / relative, ec);
  if (ec) {
    error = "mesh file not found";
    return false;
  }
  auto parent = base.begin(), child = target.begin();
  for (; parent != base.end() && child != target.end(); ++parent, ++child)
    if (*parent != *child) {
      error = "mesh source escapes resource root";
      return false;
    }
  if (parent != base.end() || !std::filesystem::is_regular_file(target, ec) ||
      ec) {
    error = "unsafe mesh source";
    return false;
  }
  const auto fileSize = std::filesystem::file_size(target, ec);
  if (ec || fileSize > 256u * 1024u * 1024u) {
    error = "mesh file unavailable or too large";
    return false;
  }
  auto parsed = scxLoadFile(target.string());
  if (!parsed.ok) {
    error = parsed.error;
    return false;
  }
  if (extension == ".scy" &&
      (parsed.model.version != 3 ||
       std::any_of(parsed.model.meshes.begin(), parsed.model.meshes.end(),
                   [](const ScxMesh &mesh) {
                     return mesh.vertexCount != 0 || !mesh.indices.empty();
                   }))) {
    error = "unsupported SCY mesh data";
    return false;
  }
  model = std::move(parsed.model);
  return true;
}

bool sceneLoadMesh(ResourceWorld &world, uint32_t id,
                   const std::filesystem::path &root, ScxModel &model,
                   std::string &error, bool allowFontFlags) {
  return sceneLoadMeshImpl(world, id, root, model, error, allowFontFlags, false);
}

bool sceneLoadGroundMesh(ResourceWorld &world, uint32_t id,
                         const std::filesystem::path &root, ScxModel &model,
                         std::string &error) {
  return sceneLoadMeshImpl(world, id, root, model, error, false, true);
}

bool sceneScaleMesh(ScxModel &model, float x, float y, float z) {
  for (const auto &mesh : model.meshes) {
    if (mesh.positions.size() != static_cast<size_t>(mesh.vertexCount) * 3)
      return false;
  }
  for (auto &mesh : model.meshes) {
    for (size_t i = 0; i < mesh.positions.size(); i += 3) {
      mesh.positions[i] *= x;
      mesh.positions[i + 1] *= y;
      mesh.positions[i + 2] *= z;
    }
  }
  return true;
}

WorldNode *sceneCloneMesh(ResourceWorld &world, const ResolvedNode &source,
                          std::string &error) {
  if (source.type != 5 || !source.owner) {
    error = "source mesh unavailable";
    return nullptr;
  }
  ScxModel mesh;
  if (!sceneLoadMesh(world, source.id, world.packs().root(), mesh, error))
    return nullptr;
  ResourceTreeHandle handle{};
  uint32_t flags = 0;
  float priority = 0;
  if (source.packNode) {
    handle = source.packNode->handle;
    flags = source.packNode->flags;
    priority = source.packNode->loadPriority;
  } else if (source.worldNode) {
    handle = source.worldNode->handle;
    flags = source.worldNode->flags;
    priority = source.worldNode->loadPriority;
  } else {
    error = "source mesh node unavailable";
    return nullptr;
  }
  auto clone = std::make_shared<ScxModel>(std::move(mesh));
  const auto parent = world.resolve(world.world().parentOwner(handle));
  WorldNode *node = nullptr;
  if (parent.worldNode)
    node = world.world().addTypedChild(*parent.worldNode, 5, "", flags | 0x200);
  else if (parent.packNode)
    node = world.world().addTypedChildUnderExternal(
        parent.packNode->handle, *parent.owner, parent.type,
        parent.packNode->sectorLink.owner, 5, "", flags | 0x200);
  else if (world.world().defaultParent())
    node = world.world().addTypedChild(*world.world().defaultParent(), 5, "",
                                       flags | 0x200);
  if (!node) {
    error = "mesh clone parent or allocation unavailable";
    return nullptr;
  }
  ResolvedNode target{node, 5, node->instanceId, nullptr, node, nullptr};
  if (!world.bindSceneMesh(target, std::move(clone))) {
    world.world().destroy(*node);
    error = "mesh clone binding failed";
    return nullptr;
  }
  world.world().markLoaded(*node);
  node->loadPriority = priority;
  error.clear();
  return node;
}

WorldNode *sceneCloneRenderLight(ResourceWorld &world, const ResolvedNode &source,
                                 std::string &error) {
  if (source.type != 13) {
    error = "source light unavailable";
    return nullptr;
  }
  ResourceTreeHandle handle{};
  uint32_t flags = 0;
  float priority = 0;
  if (source.packNode) {
    handle = source.packNode->handle;
    flags = source.packNode->flags;
    priority = source.packNode->loadPriority;
  } else if (source.worldNode) {
    handle = source.worldNode->handle;
    flags = source.worldNode->flags;
    priority = source.worldNode->loadPriority;
  } else {
    error = "source light node unavailable";
    return nullptr;
  }
  const auto parent = world.resolve(world.world().parentOwner(handle));
  WorldNode *node = nullptr;
  if (parent.worldNode)
    node = world.world().addTypedChild(*parent.worldNode, 13, "", flags | 0x200);
  else if (parent.packNode)
    node = world.world().addTypedChildUnderExternal(
        parent.packNode->handle, *parent.owner, parent.type,
        parent.packNode->sectorLink.owner, 13, "", flags | 0x200);
  else if (world.world().defaultParent())
    node = world.world().addTypedChild(*world.world().defaultParent(), 13, "",
                                       flags | 0x200);
  if (!node) {
    error = "light clone parent or allocation unavailable";
    return nullptr;
  }
  node->loadPriority = priority;
  node->field6c = priority;
  ResolvedNode target{node, 13, node->instanceId, nullptr, node, nullptr};
  if (!world.bindSceneLight(target, SceneRenderLight::stockDefaults())) {
    world.world().destroy(*node);
    error = "light clone binding failed";
    return nullptr;
  }
  world.world().markLoaded(*node);
  error.clear();
  return node;
}
