#include "natives/navigator.hpp"
#include "natives/game_type.hpp"
#include "natives/registry.hpp"
#include "render2d.hpp"
#include "resource_world.hpp"
#include "run_log.hpp"
#include "scene_2d.hpp"
#include "vm.hpp"
#include "world_model.hpp"

#include <array>
#include <string_view>

namespace {
// Script instance field readers over the VM object (stock FUN_0042a010 and
// FUN_0042a560).
const VmValue* scriptField(const VmObject& object, std::string_view name) {
  for (const auto& field : object.fields)
    if (field.name == name)
      return &field.value;
  return nullptr;
}

const VmObject* objectField(const VmObject& object, std::string_view name) {
  const auto* value = scriptField(object, name);
  return value && std::holds_alternative<const VmObject*>(*value)
             ? std::get<const VmObject*>(*value)
             : nullptr;
}

bool floatField(const VmObject& object, std::string_view name, float& out) {
  const auto* value = scriptField(object, name);
  if (!value || !std::holds_alternative<float>(*value))
    return false;
  out = std::get<float>(*value);
  return true;
}

// FUN_0042a040: write a float instance field.
void setFloatField(const VmObject& object, std::string_view name, float value) {
  for (auto& field : object.fields)
    if (field.name == name) {
      field.value = value;
      return;
    }
}

WorldNode* boundNode(const VmObject& object, VmArena& arena) {
  if (!arena.gameState || !arena.gameState->world)
    return nullptr;
  const auto* state = nativeResourceState(&object, &arena);
  if (!state)
    return nullptr;
  const auto handle =
      static_cast<int32_t>(state - arena.resourceStates.data()) + 1;
  const auto it = arena.gameState->bindings.find(handle);
  if (it == arena.gameState->bindings.end() || !it->second ||
      !it->second->link.owner)
    return nullptr;
  return arena.gameState->world->nodeFor(it->second->link.owner);
}

std::shared_ptr<SceneRenderInstance> nodeInstance(const VmObject& object,
                                                  VmArena& arena) {
  if (!arena.gameState || !arena.gameState->resources)
    return nullptr;
  auto* node = boundNode(object, arena);
  if (!node)
    return nullptr;
  const auto resolved = arena.gameState->resources->resolve(node);
  return arena.gameState->resources->sceneRenderInstance(resolved);
}

// FUN_0048b280: the followed object's or a marker mover's position. Stock
// asks the native data slot 2 first (FUN_00426470) and falls back to the
// render instance words at +0x84; the port's equivalents are the car
// runtime position and the physics box position (gameRefGetPos pattern).
bool nativePosition(const VmObject& object, VmArena& arena, float out[3]) {
  auto* node = boundNode(object, arena);
  if (!node)
    return false;
  if (node->carInstance) {
    out[0] = node->carInstance->position[0];
    out[1] = node->carInstance->position[1];
    out[2] = node->carInstance->position[2];
    return true;
  }
  if (node->physicsBox) {
    out[0] = node->physicsBox->position[0][0];
    out[1] = node->physicsBox->position[0][1];
    out[2] = node->physicsBox->position[0][2];
    return true;
  }
  return false;
}

// FUN_00426470(mover, 4, 0): integer heading query for dynamic markers.
// Stock reads native data slot 4; the port's supported analog is the car
// yaw in whole degrees. [INFERENCE] the stock slot returns degrees, since
// updateNavigator multiplies it by DAT_005f13bc = 0.0174533.
int nativeHeading(const VmObject& object, VmArena& arena) {
  auto* node = boundNode(object, arena);
  if (node && node->carInstance)
    return static_cast<int>(node->carInstance->ypr[0] * (180.0f / 3.14159265f));
  return 0;
}
} // namespace

NavigatorClamp navigatorClampTarget(float targetX, float targetZ,
                                    float offsetX, float offsetZ, float camY,
                                    float top, float left, float size) {
  // FUN_00482d30 mode 0 map rectangle. Stock constants: DAT_005f09c8 = 100,
  // DAT_005f0da0 = 8, DAT_005f0cdc = 5, DAT_005f0c78 = 1.3333334. The z
  // bounds move with camY * 5, the x bounds with camY * 5 * 4/3.
  const float half = size * 100.0f * 8.0f;
  const float camY5 = camY * 5.0f;
  const float zMin = top * 100.0f + camY5;
  const float zMax = top * 100.0f + half - camY5;
  const float xMin = left * 100.0f + camY5 * 1.3333334f;
  const float xMax = left * 100.0f + half - camY5 * 1.3333334f;
  NavigatorClamp out{targetX, targetZ, offsetX, offsetZ};
  if (out.targetZ < zMin) {
    out.offsetZ += zMin - out.targetZ;
    out.targetZ = zMin;
  } else if (out.targetZ > zMax) {
    out.offsetZ += zMax - out.targetZ;
    out.targetZ = zMax;
  }
  if (out.targetX < xMin) {
    out.offsetX += xMin - out.targetX;
    out.targetX = xMin;
  } else if (out.targetX > xMax) {
    out.offsetX += xMax - out.targetX;
    out.targetX = xMax;
  }
  return out;
}

bool navigatorClampMarkerPoint(std::array<float, 3>& point,
                               const std::array<float, 16>& view,
                               const std::array<float, 16>& projection,
                               const std::array<float, 16>& cameraWorld) {
  const auto normalized = projectCameraPoint(point, view, projection);
  if (!normalized)
    return false;
  float x = (*normalized)[0];
  float y = (*normalized)[1];
  if (x < -0.9f) {
    const float scale = -0.9f / x;
    x *= scale;
    y *= scale;
  }
  if (x > 0.9f) {
    const float scale = 0.9f / x;
    x *= scale;
    y *= scale;
  }
  if (y < -0.85f) {
    const float scale = -0.85f / y;
    x *= scale;
    y *= scale;
  }
  if (y > 0.85f) {
    const float scale = 0.85f / y;
    x *= scale;
    y *= scale;
  }
  const auto world =
      unprojectCameraPoint({x, y, (*normalized)[2]}, cameraWorld, projection);
  if (!world)
    return false;
  point = *world;
  return true;
}

namespace {
// Marker placement shared by the dynamarker and marker loops: the symbol
// lives in the 1:10 map-model space, is clamped to the map viewport through
// the map camera, then its bone00 matrix is rebuilt from the yaw and the
// clamped point (FUN_00551c70 + 0054ecd0 + 0054f4c0 + 0048c270; the port's
// setBoneMatrix is that same pair). Stock also zeroes the symbol's native
// instance words +0x84..+0x98 and sets +0x80 = 2; the port's bone matrix is
// the only position source, so there is nothing to zero.
void placeSymbol(SceneRenderInstance& symbol, float x, float z, float height,
                 float yawRadians, const CameraViewState& view,
                 const std::array<float, 16>& cameraWorld) {
  std::array<float, 3> point{x * 0.1f, height, z * 0.1f};
  navigatorClampMarkerPoint(point, view.view, view.projection, cameraWorld);
  const float position[3]{point[0] * 0.1f, point[1] * 0.1f, point[2] * 0.1f};
  const float ypr[3]{yawRadians, 0.0f, 0.0f};
  symbol.setBoneMatrix(position, ypr);
}

// java.util.Vector traversal: elementData array bounded by elementCount
// (stock FUN_0042a690 + FUN_0042a430 + FUN_0042b000).
template <typename Callback>
bool forEachVectorEntry(const VmObject& object, std::string_view vectorField,
                        Callback callback) {
  const auto* vector = objectField(object, vectorField);
  if (!vector)
    return false;
  const auto* elementData = objectField(*vector, "elementData");
  const auto* count = scriptField(*vector, "elementCount");
  if (!elementData || !count || !std::holds_alternative<int32_t>(*count))
    return false;
  const auto total = std::get<int32_t>(*count);
  for (int32_t index = 0; index < total; ++index) {
    if (index >= static_cast<int32_t>(elementData->arrayElements.size()))
      break;
    const auto* entry =
        elementData->arrayElements[index].valueless_by_exception()
            ? nullptr
            : std::get_if<const VmObject*>(&elementData->arrayElements[index]);
    if (!entry || !*entry)
      continue;
    callback(**entry);
  }
  return true;
}
} // namespace

NativeResult navigatorUpdate(const std::vector<VmValue>& args, const VmObject* receiver,
                             VmArena* arena, const std::vector<const TufaClass*>*) {
  if (!arena || !receiver || args.size() != 2 ||
      !std::holds_alternative<const VmObject*>(args[0]) ||
      !std::holds_alternative<int32_t>(args[1]))
    return std::string{"Navigator.updateNavigator: invalid call"};
  if (!arena->gameState || !arena->gameState->world ||
      !arena->gameState->resources || !arena->gameState->renderer)
    return std::string{"Navigator.updateNavigator: game world unavailable"};
  const auto* followed = std::get<const VmObject*>(args[0]);
  const auto mode = std::get<int32_t>(args[1]);
  // FUN_00482d30 no-ops unless the cam field holds a live Camera, the vp
  // field resolves and the followed GameRef is live.
  if (!followed)
    return std::monostate{};
  const auto* camObject = objectField(*receiver, "cam");
  if (!camObject)
    return std::monostate{};
  const auto cameraInstance = nodeInstance(*camObject, *arena);
  if (!cameraInstance)
    return std::monostate{};
  if ((cameraInstance->typeId() & 0xffff0000u) != 0xffff0000u)
    return std::monostate{};
  const auto cameraType =
      arena->gameState->resources->lookup(cameraInstance->typeId(), 12);
  if (!cameraType.node.worldNode || !cameraType.node.worldNode->camera)
    return std::monostate{};
  const auto& camera = *cameraType.node.worldNode->camera;

  const auto* viewportObject = objectField(*receiver, "vp");
  const ViewportInstance* viewport = nullptr;
  WorldNode* viewportNode = nullptr;
  if (viewportObject) {
    viewportNode = boundNode(*viewportObject, *arena);
    if (viewportNode && viewportNode->viewport)
      viewport = &*viewportNode->viewport;
  }

  float position[3] = {0.0f, 0.0f, 0.0f};
  if (!nativePosition(*followed, *arena, position)) {
    static bool notedPosition = false;
    if (!notedPosition) {
      notedPosition = true;
      runlog::event(runlog::EventKind::Note,
                    "Navigator.updateNavigator 00482d30: followed object has "
                    "no supported position source");
    }
    return std::monostate{};
  }

  float offsetX = 0.0f;
  float offsetZ = 0.0f;
  floatField(*receiver, "offsetX", offsetX);
  floatField(*receiver, "offsetZ", offsetZ);
  auto targetX = position[0] + offsetX;
  auto targetZ = position[2] + offsetZ;
  // FUN_00482d30 reads the stored camera bone height directly.
  const float camY = cameraInstance->boneMatrix()[13];

  if (mode == 0) {
    float top = 0.0f;
    float left = 0.0f;
    float size = 0.0f;
    if (floatField(*receiver, "top", top) &&
        floatField(*receiver, "left", left) &&
        floatField(*receiver, "size", size)) {
      const auto clamped = navigatorClampTarget(targetX, targetZ, offsetX,
                                                offsetZ, camY, top, left, size);
      setFloatField(*receiver, "offsetX", clamped.offsetX);
      setFloatField(*receiver, "offsetZ", clamped.offsetZ);
      targetX = clamped.targetX;
      targetZ = clamped.targetZ;
    } else {
      static bool notedFields = false;
      if (!notedFields) {
        notedFields = true;
        runlog::event(runlog::EventKind::Note,
                      "Navigator.updateNavigator 00482d30: top/left/size "
                      "fields missing, map rectangle clamp skipped");
      }
    }
  }

  if (mode == 0) {
    // FUN_00482d30 mode 0 patches the bone translation in place and keeps
    // the rotation: bone[12] = targetX * 0.1, bone[14] = targetZ * 0.1.
    // The map model and marker bones use this same reduced coordinate space.
    cameraInstance->setBoneTranslationKeepRotation(
        targetX, cameraInstance->boneMatrix()[13], targetZ);
  } else if (mode == 1) {
    // FUN_0048b300 supplies the followed Euler angles; stock replaces pitch
    // and roll but preserves yaw before rebuilding the camera basis.
    const auto* followedNode = boundNode(*followed, *arena);
    if (!followedNode || !followedNode->carInstance)
      return std::string{"Navigator.updateNavigator: mode 1 rotation unavailable"};
    cameraInstance->setNavigatorCameraBone(
        targetX, targetZ, followedNode->carInstance->ypr[0]);
  }

  // FUN_004fd220 + FUN_004fdc80: the marker loops only run while the map
  // viewport has a render frame. The port's equivalent is an active viewport
  // whose render attachments contain the map camera.
  if (!viewport || viewport->list != ViewportList::Active ||
      !viewport->renderAttachments || !viewportNode)
    return std::monostate{};
  const auto* cameraRenderNode = boundNode(*camObject, *arena);
  bool cameraActive = false;
  if (cameraRenderNode)
    for (const auto* active : viewport->renderAttachments->active())
      if (active == static_cast<const LinkOwner*>(cameraRenderNode))
        cameraActive = true;
  if (!cameraActive)
    return std::monostate{};

  int width = 0;
  int height = 0;
  std::string error;
  if (!arena->gameState->renderer->outputSize(width, height, error))
    return std::monostate{};
  const float pixelWidth = viewport->width * static_cast<float>(width);
  const float pixelHeight = viewport->height * static_cast<float>(height);
  if (!(pixelWidth > 0.0f) || !(pixelHeight > 0.0f))
    return std::monostate{};
  const auto view = cameraViewState(camera, cameraInstance->boneMatrix(),
                                    pixelWidth / pixelHeight);
  if (!view)
    return std::monostate{};

  // Dynamarker loop: position from the mover, yaw from the heading query,
  // height 0.031 above the map model.
  forEachVectorEntry(*receiver, "dynamarker", [&](const VmObject& entry) {
    const auto* symbol = objectField(entry, "symbol");
    const auto* mover = objectField(entry, "mover");
    if (!symbol || !mover)
      return;
    const auto symbolInstance = nodeInstance(*symbol, *arena);
    float moverPosition[3] = {0.0f, 0.0f, 0.0f};
    if (!symbolInstance || !nativePosition(*mover, *arena, moverPosition))
      return;
    const float yawRadians =
        static_cast<float>(nativeHeading(*mover, *arena)) * 0.0174533f;
    placeSymbol(*symbolInstance, moverPosition[0], moverPosition[2], 0.031f,
                yawRadians, *view, cameraInstance->boneMatrix());
  });

  // Marker loop: position from the script Vector3, identity rotation,
  // height 0.01 above the map model.
  forEachVectorEntry(*receiver, "marker", [&](const VmObject& entry) {
    const auto* symbol = objectField(entry, "symbol");
    const auto* pos = objectField(entry, "pos");
    if (!symbol || !pos)
      return;
    const auto symbolInstance = nodeInstance(*symbol, *arena);
    float x = 0.0f;
    float z = 0.0f;
    if (!symbolInstance || !floatField(*pos, "x", x) ||
        !floatField(*pos, "z", z))
      return;
    placeSymbol(*symbolInstance, x, z, 0.01f, 0.0f, *view,
                cameraInstance->boneMatrix());
  });
  return std::monostate{};
}
