#include "natives/physics.hpp"
#include "natives/game_type.hpp"
#include "part_config.hpp"
#include "resource_world.hpp"
#include <algorithm>
#include <cmath>

NativeResult physicsCreateBox(const std::vector<VmValue> &args,
                              const VmObject *receiver, VmArena *arena,
                              const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 5 ||
      !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[4]))
    return std::string{"PhysicsRef.createBox: invalid arguments"};
  std::array<float, 3> extents{};
  for (size_t i = 0; i < 3; ++i) {
    const auto *value = std::get_if<float>(&args[i + 1]);
    if (!value || !std::isfinite(*value) || *value < 0)
      return std::string{"PhysicsRef.createBox: invalid box dimensions"};
    extents[i] = *value * 0.5f;
  }
  if (!arena->gameState || !arena->gameState->world ||
      !arena->gameState->resources)
    return std::string{"PhysicsRef.createBox: world unavailable"};
  auto &game = *arena->gameState;
  auto *self = nativeResourceState(receiver, arena);
  auto *parentState =
      nativeResourceState(std::get<const VmObject *>(args[0]), arena);
  if (!self || !parentState)
    return std::string{"PhysicsRef.createBox: native link unavailable"};
  const auto parentHandle =
      static_cast<int32_t>(parentState - arena->resourceStates.data()) + 1;
  const auto parentBinding = game.bindings.find(parentHandle);
  if (parentBinding == game.bindings.end() || !parentBinding->second)
    return std::string{"PhysicsRef.createBox: parent binding unavailable"};
  const auto parent =
      game.resources->resolve(parentBinding->second->link.owner);
  if (!parent || (!parent.worldNode && !parent.packNode) || parent.type != 1)
    return std::string{"PhysicsRef.createBox: unsupported parent"};
  std::string name = "_physinst";
  const auto *nameObject = std::get<const VmObject *>(args[4]);
  if (nameObject) {
    bool found = false;
    for (const auto &entry : arena->strings)
      if (entry.second == nameObject) {
        name =
            entry.first.substr(0, std::min<size_t>(31, entry.first.find('\0')));
        found = true;
        break;
      }
    if (!found)
      return std::string{"PhysicsRef.createBox: name unavailable"};
  }
  auto *node = parent.worldNode
                   ? game.world->addTypedChild(*parent.worldNode, 2, name, 1)
                   : game.world->addTypedChildUnderExternal(
                         parent.packNode->handle, *parent.packNode, parent.type,
                         parent.packNode->sectorLink.owner, 2, name, 1);
  if (!node)
    return std::string{"PhysicsRef.createBox: allocation failed"};
  if (!game.world->registerStaticBox(*node, extents)) {
    game.world->destroy(*node);
    return std::string{"PhysicsRef.createBox: box registration failed"};
  }
  node->flags |= 0x200;
  node->bounds.mode = 0;
  node->bounds.values = {0,
                         0,
                         0,
                         node->physicsBox->radius,
                         node->physicsBox->radius,
                         node->physicsBox->radius};
  game.world->markLoaded(*node);
  node->loadPriority = 1.0f;
  const auto handle =
      static_cast<int32_t>(self - arena->resourceStates.data()) + 1;
  auto &binding = game.bindings[handle];
  if (!binding)
    binding = std::make_unique<ReceiverBinding>();
  setLinkOwner(binding->link, node);
  self->words[2] = binding->link.cachedValue;
  return std::monostate{};
}

NativeResult physicsSetMatrix(const std::vector<VmValue> &args,
                              const VmObject *receiver, VmArena *arena,
                              const std::vector<const TufaClass *> *) {
  if (!arena || !receiver || args.size() != 2 ||
      !std::holds_alternative<const VmObject *>(args[0]) ||
      !std::holds_alternative<const VmObject *>(args[1]))
    return std::string{"PhysicsRef.setMatrix: invalid arguments"};
  if (!arena->gameState || !arena->gameState->world)
    return std::string{"PhysicsRef.setMatrix: world unavailable"};
  const auto *state = nativeResourceState(receiver, arena);
  if (!state)
    return std::string{"PhysicsRef.setMatrix: native link unavailable"};
  auto &game = *arena->gameState;
  const auto handle =
      static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
  const auto binding = game.bindings.find(handle);
  if (binding == game.bindings.end() || !binding->second->link.owner)
    return std::monostate{};
  auto *node = game.world->nodeFor(binding->second->link.owner);
  if (!node)
    return std::string{"PhysicsRef.setMatrix: body unavailable"};
  const auto read = [](const VmObject *object, const char *const(&names)[3],
                       std::array<float, 3> &out) {
    if (!object)
      return true;
    for (size_t i = 0; i < 3; ++i) {
      const VmField *field = nullptr;
      for (const auto &value : object->fields)
        if (value.name == names[i]) {
          if (field)
            return false;
          field = &value;
        }
      if (!field || field->descriptor != "F" ||
          !std::holds_alternative<float>(field->value))
        return false;
      out[i] = std::get<float>(field->value);
      if (!std::isfinite(out[i]))
        return false;
    }
    return true;
  };
  const char *const xyz[] = {"x", "y", "z"};
  const char *const ypr[] = {"y", "p", "r"};
  std::array<float, 3> position{}, orientation{};
  if (!read(std::get<const VmObject *>(args[0]), xyz, position) ||
      !read(std::get<const VmObject *>(args[1]), ypr, orientation))
    return std::string{"PhysicsRef.setMatrix: invalid vector or orientation"};
  if (node->physicsBox) {
    if (orientation != std::array<float, 3>{})
      return std::string{"PhysicsRef.setMatrix: rotation unsupported"};
    node->physicsBox->position[0] = position;
    node->physicsBox->position[1] = position;
    node->bounds.mode = 1;
    node->bounds.values = {position[0],
                           position[1],
                           position[2],
                           node->physicsBox->radius,
                           node->physicsBox->radius,
                           node->physicsBox->radius};
    return std::monostate{};
  }
  auto body = game.world->dynamicBody(*node).lock();
  if (!body || !body->proto || body->current > 1 ||
      !std::isfinite(body->proto->shape.boundRadius) ||
      body->proto->shape.boundRadius < 0)
    return std::string{"PhysicsRef.setMatrix: body unavailable"};
  // 004986f0 -> 00537790 -> type-2 0053fce0. A nonempty
  // physics-instance attachment list requires the unmodeled update queue.
  if ((node->flags & 0x100u) && !(node->flags & 0x4000000u) &&
      node->physicsBoneAttachments)
    return std::string{
        "PhysicsRef.setMatrix: physics attachment update queue unsupported"};
  PartSlotDirective directive{};
  for (size_t i = 0; i < 3; ++i)
    directive.orientation[i] = orientation[i];
  PartSlotState transform;
  if (!makePartSlotState(directive, transform))
    return std::string{"PhysicsRef.setMatrix: invalid orientation"};
  const auto &r = transform.rotation;
  const physics::Mat34 matrix{r[0], r[1], r[2], 0,    r[3], r[4],
                              r[5], 0,    r[6], r[7], r[8], 0};
  const auto quaternion = physics::quatFromMatrix(matrix);
  for (float value : quaternion)
    if (!std::isfinite(value))
      return std::string{"PhysicsRef.setMatrix: invalid orientation"};
  auto &bodyState = body->state[body->current];
  bodyState.pos = position;
  body->state[body->current ^ 1].pos = position;
  bodyState.rot = matrix;
  bodyState.quat = quaternion;
  const float radius = body->proto->shape.boundRadius;
  node->bounds.mode = 1;
  node->bounds.values = {position[0], position[1], position[2],
                         radius,      radius,      radius};
  return std::monostate{};
}
