#include "natives/car_part.hpp"

#include "car_runtime.hpp"
#include "car_variable_refresh.hpp"
#include "dyno_model.hpp"
#include "natives/game_type.hpp"
#include "natives/lang_string.hpp"
#include "natives/resource_ref.hpp"
#include "resource_world.hpp"
#include "run_log.hpp"
#include "world_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

int32_t objectPtrHandle(const VmObject* object, VmArena* arena) {
	if (!object || !arena) return 0;
	for (const auto& field : object->fields)
		if (field.name == "ptr" && field.descriptor == "I")
			if (const auto* val = std::get_if<int32_t>(&field.value)) return *val;
	return 0;
}

LinkOwner* linkedOwnerForObject(const VmObject* object, VmArena* arena) {
	if (!object || !arena || !arena->gameState) return nullptr;
	const int32_t handle = objectPtrHandle(object, arena);
	if (handle <= 0) return nullptr;
	const auto it = arena->gameState->bindings.find(handle);
	return it == arena->gameState->bindings.end() || !it->second ? nullptr : it->second->link.owner;
}

WorldNode* worldNodeForObject(const VmObject* object, VmArena* arena) {
	if (!arena || !arena->gameState || !arena->gameState->world) return nullptr;
	auto& world = *arena->gameState->world;
	if (auto* node = world.nodeFor(linkedOwnerForObject(object, arena))) return node;
	return world.findByReceiver(object);
}

const CarPrototype* nodePrototype(const WorldNode* node, GameWorld& world) {
	if (!node || !node->carInstance) return nullptr;
	const uint32_t scriptId = node->carInstance->prototypeSource.cachedValue
	                              ? node->carInstance->prototypeSource.cachedValue
	                              : node->scriptLink.cachedValue;
	if (auto* script = world.findScript(scriptId)) return script->car.get();
	return nullptr;
}

float prototypeMass(const CarPrototype* proto) {
	if (!proto) return 0.0f;
	if (proto->config.centerMass > 0.0f) return proto->config.centerMass;
	float sum = 0.0f;
	for (const auto& body : proto->config.bodies)
		if (body.kind == CarBodyRecord::Body && body.shape.mass > 0.0f) sum += body.shape.mass;
	return sum;
}

struct PartAndCarNodes {
	WorldNode* partNode = nullptr;
	WorldNode* carNode = nullptr;
	CarRuntimeInstance* partInst = nullptr;
	CarRuntimeInstance* carInst = nullptr;
};

PartAndCarNodes resolvePartAndCar(const VmObject* receiver, VmArena* arena) {
	PartAndCarNodes out;
	if (!receiver || !arena || !arena->gameState || !arena->gameState->world) return out;
	auto& world = *arena->gameState->world;
	WorldNode* node = worldNodeForObject(receiver, arena);
	if (!node) {
		for (const auto& field : receiver->fields) {
			if (field.name == "chassis" || field.name == "car") {
				if (const auto* obj = std::get_if<const VmObject*>(&field.value); obj && *obj) {
					node = worldNodeForObject(*obj, arena);
					if (node) break;
				}
			}
		}
	}
	if (!node) return out;
	out.partNode = node;
	out.partInst = node->carInstance.get();
	if (node->nativeDevice == "car") {
		out.carNode = node;
		out.carInst = node->carInstance.get();
		if (out.carInst && out.carInst->chassisObject) {
			if (auto* chNode = worldNodeForObject(out.carInst->chassisObject, arena)) {
				out.partNode = chNode;
				out.partInst = chNode->carInstance.get();
			}
		}
	} else {
		const uint32_t carId = out.partInst ? out.partInst->carLink.cachedValue : 0;
		if (carId != 0) out.carNode = world.findInstance(carId);
		if (!out.carNode) out.carNode = world.nodeFor(node->sectorLink.owner);
		if (!out.carNode) out.carNode = node;
		out.carInst = out.carNode ? out.carNode->carInstance.get() : out.partInst;
	}
	return out;
}

const std::string* findInternedString(const VmArena* arena, const VmObject* obj) {
	if (!arena || !obj) return nullptr;
	for (const auto& entry : arena->strings)
		if (entry.second == obj) return &entry.first;
	return nullptr;
}

bool readVec3Object(const VmObject* obj, std::array<float, 3>& out) {
	if (!obj) return false;
	for (const auto& f : obj->fields) {
		const auto* val = std::get_if<float>(&f.value);
		if (!val) continue;
		if (f.name == "x") out[0] = *val;
		else if (f.name == "y") out[1] = *val;
		else if (f.name == "z") out[2] = *val;
	}
	return true;
}

bool readYprObject(const VmObject* obj, std::array<float, 3>& out) {
	if (!obj) return false;
	for (const auto& f : obj->fields) {
		const auto* val = std::get_if<float>(&f.value);
		if (!val) continue;
		if (f.name == "y") out[0] = *val;
		else if (f.name == "p") out[1] = *val;
		else if (f.name == "r") out[2] = *val;
	}
	return true;
}

const TufaClass* findClassByName(const std::vector<const TufaClass*>* classes, std::string_view name) {
	if (!classes) return nullptr;
	for (const auto* candidate : *classes)
		if (candidate && candidate->name == name) return candidate;
	return nullptr;
}

VmObject* allocateObjectWithPtr(std::string_view className, uint32_t word0, uint32_t word1, VmArena* arena,
                                const std::vector<const TufaClass*>* classes) {
	if (!arena || arena->objects.size() >= arena->limit) return nullptr;
	if (arena->ensureClass) (void)arena->ensureClass(std::string(className));
	const TufaClass* cls = findClassByName(classes, className);
	auto obj = std::make_unique<VmObject>();
	obj->runtimeClass = cls;
	arena->resourceStates.push_back({obj.get(), {word0, word1, 0, 0}});
	const auto handle = static_cast<int32_t>(arena->resourceStates.size());
	obj->fields.push_back({"ptr", handle, "I"});
	VmObject* raw = obj.get();
	arena->objects.push_back(std::move(obj));
	return raw;
}

struct ResolvedWheel {
	CarWheelRuntime* live = nullptr;
	CarWheel* wheel = nullptr;
	CarWheelExtraState* extra = nullptr;
};

ResolvedWheel resolveWheel(const VmObject* receiver, VmArena* arena) {
	ResolvedWheel out;
	if (!receiver || !arena || !arena->gameState || !arena->gameState->world) return out;
	const auto* state = nativeResourceState(receiver, arena);
	if (!state) {
		static bool noted = false;
		if (!noted) { noted = true; runlog::event(runlog::EventKind::Note, "wheel reference resolution: no owned resource state"); }
		return out;
	}
	const uint32_t instanceId = state->words[0];
	const uint32_t wheelIdx = state->words[1];
	if (wheelIdx >= 16) return out;
	WorldNode* node = arena->gameState->world->findInstance(instanceId);
	if (!node || !node->carInstance) return out;
	// Chassis.getWheel stores the chassis Part identity. The live wheel record
	// belongs to the owning Car, reached through the installed-part car link.
	if (!node->carInstance->isCar) {
		if (auto* car = arena->gameState->world->nodeFor(node->carInstance->carLink.owner);
		    car && car->carInstance && car->carInstance->isCar) node = car;
	}
	out.wheel = &node->carInstance->wheelStates[wheelIdx];
	out.extra = &node->carInstance->wheelExtras[wheelIdx];
	const auto& physics = node->carInstance->carPhysics;
	if (physics && wheelIdx < physics->wheels.size()) out.live = physics->wheels[wheelIdx].get();
	return out;
}

// 00440ed0 and 00441270..004414d0 rebuild the live 00491f80 settings
// after writing their wheel-record field. Preserve raw lengths across calls.
void configureWheelSuspension(const ResolvedWheel& wheel, const char* cause) {
  if (!wheel.live || !wheel.extra) return;
  auto& live = *wheel.live;
  const auto& extra = *wheel.extra;
  live.radius = extra.radius;
  live.springForce = extra.springForce;
  live.damping = extra.dampingBump;
  live.reboundDamping = extra.dampingRebound;
  live.restLength = extra.restLength + extra.radius;
  live.initialLength = live.restLength;
  live.suspension.settings = physics::configureSuspension(
      live.springForce, live.damping, live.restLength,
      extra.maxLength + extra.radius, extra.minLength + extra.radius,
      live.reboundDamping, extra.radius);
  live.maximumLength = live.suspension.settings.maxLength;
  live.minimumLength = live.suspension.settings.minLength;
  runlog::event(runlog::EventKind::Note,
                "wheel suspension %s: force=%.3g damping=%.3g/%.3g rest=%.3g min=%.3g max=%.3g radius=%.3g",
                cause, live.springForce, live.damping, live.reboundDamping,
                live.restLength, live.minimumLength, live.maximumLength, live.radius);
}

CarSfxTable* resolveSfxTable(const VmObject* receiver, VmArena* arena) {
	if (!receiver || !arena || !arena->gameState || !arena->gameState->world) return nullptr;
	const auto* state = nativeResourceState(receiver, arena);
	if (!state) return nullptr;
	const uint32_t instanceId = state->words[0];
	const uint32_t tableIdx = state->words[1];
	WorldNode* node = arena->gameState->world->findInstance(instanceId);
	if (!node || !node->carInstance) return nullptr;
	if (tableIdx == 0) return &node->carInstance->sfxEngineUp;
	if (tableIdx == 1) return &node->carInstance->sfxEngineDown;
	if (tableIdx == 2) return &node->carInstance->sfxExhaust;
	return nullptr;
}

float getFloatField(const VmObject* obj, std::string_view name, float fallback = 0.0f) {
	if (!obj) return fallback;
	for (const auto& f : obj->fields) {
		if (f.name == name) {
			if (const auto* fv = std::get_if<float>(&f.value)) return *fv;
			if (const auto* iv = std::get_if<int32_t>(&f.value)) return static_cast<float>(*iv);
		}
	}
	return fallback;
}

void setFloatField(const VmObject* obj, std::string_view name, float value) {
	if (!obj) return;
	for (auto& f : obj->fields) {
		if (f.name == name) {
			if (f.descriptor == "I") f.value = static_cast<int32_t>(value);
			else f.value = value;
			return;
		}
	}
}

const VmObject* getObjectField(const VmObject* obj, std::string_view name) {
	if (!obj) return nullptr;
	for (const auto& f : obj->fields)
		if (f.name == name)
			if (const auto* ov = std::get_if<const VmObject*>(&f.value)) return *ov;
	return nullptr;
}

} // namespace

// --- java.game.parts.Part ---

// 004684a0: java.game.parts.Part.getSlots ()I
NativeResult partGetSlots(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                          const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Part.getSlots arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (!resolved.partInst) return VmValue{int32_t{0}};
	return VmValue{static_cast<int32_t>(resolved.partInst->slots.size())};
}

// 00468570: java.game.parts.Part.getSlotID (I)I
NativeResult partGetSlotId(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Part.getSlotID arguments"};
	const int32_t index = std::get<int32_t>(args[0]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (!resolved.partInst) return VmValue{int32_t{0}};
	const auto& slots = resolved.partInst->slots;
	if (index < 0) {
		for (const auto& slot : slots)
			if (slot && (slot->flags & 1u) != 0) return VmValue{slot->id};
		return VmValue{int32_t{0}};
	}
	if (static_cast<size_t>(index) < slots.size() && slots[static_cast<size_t>(index)])
		return VmValue{slots[static_cast<size_t>(index)]->id};
	return VmValue{int32_t{0}};
}

// 004686e0: java.game.parts.Part.getSlotIndex (I)I
NativeResult partGetSlotIndex(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Part.getSlotIndex arguments"};
	const int32_t slotId = std::get<int32_t>(args[0]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (!resolved.partInst) return VmValue{int32_t{0}};
	const auto& slots = resolved.partInst->slots;
	for (size_t i = 0; i < slots.size(); ++i)
		if (slots[i] && slots[i]->id == slotId) return VmValue{static_cast<int32_t>(i)};
	return VmValue{int32_t{0}};
}

// 004687f0: java.game.parts.Part.partOnSlot (I)Ljava.game.parts.Part;
NativeResult partPartOnSlot(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Part.partOnSlot arguments"};
	const int32_t slotId = std::get<int32_t>(args[0]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (!resolved.partNode || !resolved.partInst || !arena->gameState || !arena->gameState->world)
		return VmValue{static_cast<const VmObject*>(nullptr)};
	if (slotId == -1) return VmValue{resolved.partNode->scriptReceiver};
	for (const auto& slot : resolved.partInst->slots) {
		if (slot && slot->id == slotId) {
			if (auto* otherNode = arena->gameState->world->nodeFor(slot->attachedNode.owner))
				return VmValue{otherNode->scriptReceiver};
			break;
		}
	}
	return VmValue{static_cast<const VmObject*>(nullptr)};
}

// 00468990: java.game.parts.Part.slotIDOnSlot (I)I
NativeResult partSlotIdOnSlot(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Part.slotIDOnSlot arguments"};
	const int32_t slotId = std::get<int32_t>(args[0]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (!resolved.partInst) return VmValue{int32_t{0}};
	for (const auto& slot : resolved.partInst->slots)
		if (slot && slot->id == slotId) return VmValue{slot->attachedSlotId};
	return VmValue{int32_t{0}};
}

// 00468ab0: java.game.parts.Part.getSlotDamage (I)Ljava.lang.String;
NativeResult partGetSlotDamage(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>* classes) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Part.getSlotDamage arguments"};
	const int32_t index = std::get<int32_t>(args[0]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (!resolved.partInst || index < 0 || static_cast<size_t>(index) >= resolved.partInst->slots.size() ||
	    !resolved.partInst->slots[static_cast<size_t>(index)])
		return VmValue{static_cast<const VmObject*>(nullptr)};
	const auto& dmg = resolved.partInst->slots[static_cast<size_t>(index)]->damage;
	if (dmg.empty()) return VmValue{static_cast<const VmObject*>(nullptr)};
	return internStringResult(dmg, arena, classes);
}

// 00468c50: java.game.parts.Part.setSlotDamage (ILjava.lang.String;)V
NativeResult partSetSlotDamage(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (args.size() != 2 || !std::holds_alternative<int32_t>(args[0]) ||
	    !std::holds_alternative<const VmObject*>(args[1]))
		return std::string{"invalid Part.setSlotDamage arguments"};
	const int32_t index = std::get<int32_t>(args[0]);
	const auto* strObj = std::get<const VmObject*>(args[1]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst && index >= 0 && static_cast<size_t>(index) < resolved.partInst->slots.size() &&
	    resolved.partInst->slots[static_cast<size_t>(index)]) {
		const auto* text = findInternedString(arena, strObj);
		resolved.partInst->slots[static_cast<size_t>(index)]->damage = text ? *text : "";
	}
	return std::monostate{};
}

// 00468df0: java.game.parts.Part.setSlotPos (ILjava.lang.Vector3;Ljava.lang.Ypr;)V
NativeResult partSetSlotPos(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (args.size() != 3 || !std::holds_alternative<int32_t>(args[0]) ||
	    !std::holds_alternative<const VmObject*>(args[1]) || !std::holds_alternative<const VmObject*>(args[2]))
		return std::string{"invalid Part.setSlotPos arguments"};
	const int32_t slotId = std::get<int32_t>(args[0]);
	const auto* posObj = std::get<const VmObject*>(args[1]);
	const auto* yprObj = std::get<const VmObject*>(args[2]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (!resolved.partInst || !arena->gameState || !arena->gameState->world) return std::monostate{};
	const auto* proto = nodePrototype(resolved.partNode, *arena->gameState->world);
	for (size_t i = 0; i < resolved.partInst->slots.size(); ++i) {
		auto& slot = resolved.partInst->slots[i];
		if (slot && slot->id == slotId) {
			if (proto && i < proto->config.slots.size()) {
				slot->transform = proto->config.slots[i].state;
				slot->orientation = proto->config.slots[i].orientation;
			}
			std::array<float, 3> pos{0.0f, 0.0f, 0.0f};
			if (readVec3Object(posObj, pos)) {
				slot->transform.position[0] += pos[0];
				slot->transform.position[1] += pos[1];
				slot->transform.position[2] += pos[2];
			}
			std::array<float, 3> ypr{0.0f, 0.0f, 0.0f};
			if (readYprObject(yprObj, ypr)) slot->orientation = ypr;
			break;
		}
	}
	return std::monostate{};
}

// 004690a0: java.game.parts.Part.getCar ()I
NativeResult partGetCar(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                        const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Part.getCar arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (!resolved.partNode) return VmValue{int32_t{0}};
	if (resolved.partInst && resolved.partInst->carLink.cachedValue != 0)
		return VmValue{static_cast<int32_t>(resolved.partInst->carLink.cachedValue)};
	if (resolved.carNode) return VmValue{static_cast<int32_t>(resolved.carNode->instanceId)};
	return VmValue{static_cast<int32_t>(resolved.partNode->instanceId)};
}

// 00469120: java.game.parts.Part.getCarRef ()Ljava.game.parts.Part;
NativeResult partGetCarRef(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Part.getCarRef arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.carInst && resolved.carInst->chassisObject)
		return VmValue{resolved.carInst->chassisObject};
	if (resolved.carNode && resolved.carNode->scriptReceiver)
		return VmValue{resolved.carNode->scriptReceiver};
	return VmValue{static_cast<const VmObject*>(nullptr)};
}

// 004691f0: java.game.parts.Part.getWheelID ()I
NativeResult partGetWheelId(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Part.getWheelID arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (!resolved.partInst) return VmValue{int32_t{-1}};
	if (resolved.partInst->wheelId >= 0) return VmValue{resolved.partInst->wheelId};
	for (const auto& slot : resolved.partInst->slots) {
		if (slot && (slot->flags & 1u) != 0 && slot->attachedSlotId > 100 && slot->attachedSlotId < 401)
			return VmValue{static_cast<int32_t>((slot->attachedSlotId - 101) % 10)};
	}
	return VmValue{int32_t{-1}};
}

// 00469290: java.game.parts.Part.getMass ()F
NativeResult partGetMass(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Part.getMass arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partNode && arena && arena->gameState && arena->gameState->world) {
		const float m = prototypeMass(nodePrototype(resolved.partNode, *arena->gameState->world));
		if (m > 0.0f) return VmValue{m};
	}
	return VmValue{0.0f};
}

// 00469340: java.game.parts.Part.getWear ()F
NativeResult partGetWear(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Part.getWear arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	return VmValue{resolved.partInst ? resolved.partInst->wear : 1.0f};
}

// 00469420: java.game.parts.Part.setWear (F)F
NativeResult partSetWear(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0]))
		return std::string{"invalid Part.setWear arguments"};
	const float val = std::get<float>(args[0]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) resolved.partInst->wear = val;
	return VmValue{val};
}

// 00469500: java.game.parts.Part.setMaxWear (F)V
NativeResult partSetMaxWear(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0]))
		return std::string{"invalid Part.setMaxWear arguments"};
	const float val = std::get<float>(args[0]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) resolved.partInst->maxWear = val;
	return std::monostate{};
}

// 004695d0: java.game.parts.Part.getTear ()F
NativeResult partGetTear(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Part.getTear arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	return VmValue{resolved.partInst ? resolved.partInst->tear : 1.0f};
}

// 004696a0: java.game.parts.Part.setTear (F)F
NativeResult partSetTear(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0]))
		return std::string{"invalid Part.setTear arguments"};
	const float val = std::get<float>(args[0]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) resolved.partInst->tear = val;
	return VmValue{val};
}

// 00469770: java.game.parts.Part.disableSlot (II)V
NativeResult partDisableSlot(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>*) {
	if (args.size() != 2 || !std::holds_alternative<int32_t>(args[0]) || !std::holds_alternative<int32_t>(args[1]))
		return std::string{"invalid Part.disableSlot arguments"};
	const int32_t slotId = std::get<int32_t>(args[0]);
	const int32_t mode = std::get<int32_t>(args[1]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (!resolved.partInst || !arena->gameState || !arena->gameState->world) return std::monostate{};
	const auto applyMode = [mode](RuntimeSlotState& slot) {
		slot.flags &= ~0x50u;
		if (mode == 1) slot.flags |= 0x10u;
		else if (mode == 2) slot.flags |= 0x50u;
	};
	for (auto& slot : resolved.partInst->slots) {
		if (slot && slot->id == slotId) {
			applyMode(*slot);
			if (auto* otherNode = arena->gameState->world->nodeFor(slot->attachedNode.owner)) {
				if (otherNode->carInstance) {
					for (auto& otherSlot : otherNode->carInstance->slots)
						if (otherSlot && otherSlot->id == slot->attachedSlotId) applyMode(*otherSlot);
				}
			}
			break;
		}
	}
	return std::monostate{};
}

// 00469970: java.game.parts.Part.isSlotDisabled (I)I
NativeResult partIsSlotDisabled(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Part.isSlotDisabled arguments"};
	const int32_t slotId = std::get<int32_t>(args[0]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (!resolved.partInst) return VmValue{int32_t{0}};
	for (const auto& slot : resolved.partInst->slots) {
		if (slot && slot->id == slotId) {
			if ((slot->flags & 0x40u) != 0) return VmValue{int32_t{2}};
			if ((slot->flags & 0x10u) != 0) return VmValue{int32_t{1}};
			return VmValue{int32_t{0}};
		}
	}
	return VmValue{int32_t{0}};
}

// 00469ab0: java.game.parts.Part.getTexture ()I
NativeResult partGetTexture(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Part.getTexture arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	return VmValue{resolved.partInst ? static_cast<int32_t>(resolved.partInst->textureId) : int32_t{0}};
}

// 00469b80: java.game.parts.Part.setTexture (I)I
NativeResult partSetTexture(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Part.setTexture arguments"};
	const auto texId = static_cast<uint32_t>(std::get<int32_t>(args[0]));
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) resolved.partInst->textureId = texId;
	return VmValue{int32_t{1}};
}

// 00469cc0: java.game.parts.Part.getMesh ()I
NativeResult partGetMesh(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Part.getMesh arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	return VmValue{resolved.partInst ? static_cast<int32_t>(resolved.partInst->meshId) : int32_t{0}};
}

// 00469d90: java.game.parts.Part.setMesh (I)I
NativeResult partSetMesh(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Part.setMesh arguments"};
	const auto meshId = static_cast<uint32_t>(std::get<int32_t>(args[0]));
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) resolved.partInst->meshId = meshId;
	return VmValue{int32_t{1}};
}

// 00469ed0: java.game.parts.Part.getRenderType ()I
NativeResult partGetRenderType(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Part.getRenderType arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	return VmValue{resolved.partInst ? static_cast<int32_t>(resolved.partInst->renderType) : int32_t{0}};
}

// 00469fa0: java.game.parts.Part.setRenderType (I)I
NativeResult partSetRenderType(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Part.setRenderType arguments"};
	const auto renderId = static_cast<uint32_t>(std::get<int32_t>(args[0]));
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) resolved.partInst->renderType = renderId;
	return VmValue{int32_t{1}};
}

// 0046a0e0: java.game.parts.Part.getLogo ()I
NativeResult partGetLogo(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                         const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Part.getLogo arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partNode && arena && arena->gameState && arena->gameState->world) {
		if (const auto* proto = nodePrototype(resolved.partNode, *arena->gameState->world)) {
			for (const auto& res : proto->resources)
				if (res && res->request.directive == "logo") return VmValue{static_cast<int32_t>(res->id)};
		}
	}
	return VmValue{int32_t{0}};
}

// 0046a1b0: java.game.parts.Part.setSfxLoopParams (FF)I
NativeResult partSetSfxLoopParams(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                  const std::vector<const TufaClass*>*) {
	if (args.size() != 2 || !std::holds_alternative<float>(args[0]) || !std::holds_alternative<float>(args[1]))
		return std::string{"invalid Part.setSfxLoopParams arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) {
		resolved.partInst->sfxLoopParamA = std::get<float>(args[0]);
		resolved.partInst->sfxLoopParamB = std::get<float>(args[1]);
	}
	return VmValue{int32_t{1}};
}

// 0046a5a0: java.game.parts.Part.flap (I)I
NativeResult partFlap(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                      const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Part.flap arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (!resolved.partInst) return VmValue{int32_t{-1}};
	return VmValue{int32_t{0}};
}

// 0046a680: java.game.parts.Part.install_OK (Ljava.util.resource.GameRef;ILjava.util.resource.GameRef;ILjava.lang.Vector3;)[I
NativeResult partInstallOk(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>*) {
	if (args.size() != 5 || !std::holds_alternative<const VmObject*>(args[0]) ||
	    !std::holds_alternative<int32_t>(args[1]) || !std::holds_alternative<const VmObject*>(args[2]) ||
	    !std::holds_alternative<int32_t>(args[3]) || !std::holds_alternative<const VmObject*>(args[4]))
		return std::string{"invalid Part.install_OK arguments"};
	const auto* targetParentObj = std::get<const VmObject*>(args[0]);
	const auto* targetOutObj = std::get<const VmObject*>(args[2]);
	int32_t targetSlotId = std::get<int32_t>(args[3]);
	if (!targetParentObj || !arena || !arena->gameState || !arena->gameState->world)
		return VmValue{static_cast<const VmObject*>(nullptr)};
	const auto resolved = resolvePartAndCar(receiver, arena);
	WorldNode* parentNode = worldNodeForObject(targetParentObj, arena);
	if (!resolved.partNode || !parentNode) return VmValue{static_cast<const VmObject*>(nullptr)};
	WorldNode* actualTarget = parentNode;
	// 0046a680 reads an optional Vector3 through FUN_0042a560 and passes it to
	// 00473b10, which keeps the nearest candidate slot instead of the first.
	std::array<float, 3> preferred{};
	bool hasPreferred = false;
	if (const auto* vector = std::get<const VmObject*>(args[4])) {
		const char* const xyz[] = {"x", "y", "z"};
		int found = 0;
		for (size_t i = 0; i < 3; ++i)
			for (const auto& field : vector->fields)
				if (field.name == xyz[i] && field.descriptor == "F")
					if (const auto* value = std::get_if<float>(&field.value)) {
						preferred[i] = *value;
						++found;
					}
		hasPreferred = found == 3;
	}
	const int32_t foundPartSlot = carPartFindCompatibleSlot(
	    *resolved.partNode, actualTarget, targetSlotId, *arena->gameState->world,
	    hasPreferred ? &preferred : nullptr);
	if (foundPartSlot == 0 || !actualTarget) return VmValue{static_cast<const VmObject*>(nullptr)};
	if (targetOutObj) {
		if (auto* dstState = nativeResourceState(targetOutObj, arena)) {
			const auto dstHandle = static_cast<int32_t>(dstState - arena->resourceStates.data()) + 1;
			auto& slot = arena->gameState->bindings[dstHandle];
			if (!slot) slot = std::make_unique<ReceiverBinding>();
			setLinkOwner(slot->link, actualTarget);
			dstState->words[2] = slot->link.cachedValue;
		}
	}
	if (arena->objects.size() >= arena->limit) return VmValue{static_cast<const VmObject*>(nullptr)};
	auto arr = std::make_unique<VmObject>();
	arr->arrayDescriptor = "[I";
	arr->arrayElements = {VmValue{foundPartSlot}, VmValue{targetSlotId}};
	const VmObject* raw = arr.get();
	arena->objects.push_back(std::move(arr));
	return VmValue{raw};
}

// --- java.game.parts.DynoData ---

// Newer 004cba80 owns the same 0x1ec native model as older 0046aa20.
NativeResult dynoDataNewNative(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
  if (!args.empty() || !receiver || !arena)
    return std::string{"invalid DynoData.newNative"};
  if (auto* state = nativeResourceState(receiver, arena)) {
    if (!state->dyno)
      return std::string{"DynoData.newNative: handle names another native resource"};
    return std::monostate{};
  }
  for (auto& field : receiver->fields) {
    if (field.name != "ptr" || field.descriptor != "I")
      continue;
    const auto* handle = std::get_if<int32_t>(&field.value);
    if (!handle || *handle != 0 || arena->resourceStates.size() >= static_cast<size_t>(std::numeric_limits<int32_t>::max()))
      return std::string{"DynoData.newNative: invalid or exhausted handle"};
    arena->resourceStates.push_back({receiver, {}});
    arena->resourceStates.back().dyno = std::make_shared<DynoModel>();
    field.value = static_cast<int32_t>(arena->resourceStates.size());
    return std::monostate{};
  }
  return std::string{"DynoData.newNative: missing native handle field"};
}

NativeResult dynoDataDeleteNative(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                  const std::vector<const TufaClass*>*) {
  if (!args.empty() || !receiver || !arena)
    return std::string{"invalid DynoData.deleteNative"};
  if (auto* state = nativeResourceState(receiver, arena))
    state->dyno.reset();
  for (auto& field : receiver->fields)
    if (field.name == "ptr" && field.descriptor == "I")
      field.value = int32_t{0};
  return std::monostate{};
}

// Newer FUN_004cba80; older FUN_0046aa20. The temporary-model branch copies
// legacy Java arrays, whereas Native-backed objects retain their own tables.
NativeResult dynoDataCalcDyno(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
  if (args.size() != 1 || !std::holds_alternative<float>(args[0]) || !receiver)
    return std::string{"invalid DynoData.calcDyno arguments"};
  DynoParameters p;
  p.cylinders = getFloatField(receiver, "cylinders");
  p.bore = getFloatField(receiver, "bore");
  p.stroke = getFloatField(receiver, "stroke");
  p.minimumVolume = getFloatField(receiver, "Vmin");
  p.intakeMinimum = getFloatField(receiver, "in_min");
  p.intakeMaximum = getFloatField(receiver, "in_max");
  p.exhaustMinimum = getFloatField(receiver, "out_min");
  p.exhaustMaximum = getFloatField(receiver, "out_max");
  p.intakeOpen = getFloatField(receiver, "time_in_open");
  p.intakeClose = getFloatField(receiver, "time_in_close");
  p.exhaustOpen = getFloatField(receiver, "time_out_open");
  p.exhaustClose = getFloatField(receiver, "time_out_close");
  p.sparkMinimum = getFloatField(receiver, "time_spark_min");
  p.sparkIncrement = getFloatField(receiver, "time_spark_inc");
  p.sparkRpm0 = getFloatField(receiver, "time_spark_RPM0");
  p.sparkRpm1 = getFloatField(receiver, "time_spark_RPM1");
  p.burnTime = getFloatField(receiver, "time_burn");
  p.rpmLimit = getFloatField(receiver, "RPM_limit");
  p.maximumRpm = getFloatField(receiver, "maxRPM");
  p.turboMultiplier = getFloatField(receiver, "rpm_turbo_mul");
  p.turboOptimum = getFloatField(receiver, "rpm_turbo_opt");
  p.turboRange = getFloatField(receiver, "rpm_turbo_range");
  p.turboPressure = getFloatField(receiver, "P_turbo_max");
  p.wastePressure = getFloatField(receiver, "P_turbo_waste", p.turboPressure);
  p.intercooling = getFloatField(receiver, "intercooling", 1);
  p.mixtureHeat = getFloatField(receiver, "mixture_H");
  p.mixtureRatio = getFloatField(receiver, "mixture_ratio");
  p.maximumFuelConsumption = getFloatField(receiver, "max_fuel_consumption");
  p.maximumAirConsumption = getFloatField(receiver, "max_air_consumption");
  p.nitroHeat = getFloatField(receiver, "nitro_H");
  p.nitroCooling = getFloatField(receiver, "nitro_cooling");
  auto* state = nativeResourceState(receiver, arena);
  const auto persistent = state ? state->dyno : nullptr;
  DynoModel temporary;
  auto calculated = (persistent ? *persistent : temporary).calculate(p, std::get<float>(args[0]));
  if (const auto* error = std::get_if<std::string>(&calculated))
    return *error;
  const auto& result = std::get<DynoResult>(calculated);
  if (p.maximumRpm <= 0)
    setFloatField(receiver, "maxRPM", result.maximumRpm);
  if (!persistent) {
    const auto copy = [&](const char* name, const std::vector<float>& curve) {
      const auto* array = getObjectField(receiver, name);
      if (!array)
        return true;
      if (array->arrayElements.size() > curve.size())
        return false;
      for (size_t i = 0; i < array->arrayElements.size(); ++i)
        array->arrayElements[i] = VmValue{curve[i] <= 0 ? 0.0f : curve[i]};
      return true;
    };
    if (!copy("torquetable", result.primary.torque) || !copy("HPtable", result.primary.power) ||
        (!result.nitro.torque.empty() && !copy("torquetable2", result.nitro.torque)))
      return std::string{"DynoData.calcDyno: legacy array exceeds native table"};
    setFloatField(receiver, "table_stepsize", result.requestedStepRpm);
    setFloatField(receiver, "torque", 1);
    if (!result.nitro.torque.empty())
      setFloatField(receiver, "torque2", 1);
  }
  setFloatField(receiver, "Displacement", result.displacement);
  setFloatField(receiver, "Compression", result.compression);
  setFloatField(receiver, "maxTorque", result.maximumTorque);
  setFloatField(receiver, "RPM_maxTorque", result.maximumTorqueRpm);
  setFloatField(receiver, "maxHP", result.maximumHorsepower);
  setFloatField(receiver, "RPM_maxHP", result.maximumHorsepowerRpm);
  runlog::event(runlog::EventKind::Note,
                "DynoData.calcDyno: native=%d rows=%zu step=%d displacement=%g compression=%g peakNm=%g peakHP=%g",
                persistent ? 1 : 0, result.primary.torque.size(), result.stepRpm,
                result.displacement, result.compression, result.maximumTorque, result.maximumHorsepower);
  return VmValue{0.0f};
}

namespace {
// Newer 004cc2b0/004cc1b0 deliberately ignore the nitrous argument. The
// last grid point is excluded, and getHP returns the stored watt sample.
NativeResult sampleDyno(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena, bool power) {
  if (args.size() != 2 || !std::holds_alternative<float>(args[0]) || !std::holds_alternative<float>(args[1]))
    return std::string{"invalid DynoData sample arguments"};
  const auto* state = nativeResourceState(receiver, arena);
  const auto* result = state && state->dyno ? state->dyno->result() : nullptr;
  const float speed = std::get<float>(args[0]);
  if (!result || speed < 0 || !std::isfinite(speed))
    return VmValue{0.0f};
  const auto& curve = power ? result->primary.power : result->primary.torque;
  for (size_t i = 1; i < curve.size(); ++i) {
    const float upper = static_cast<float>(i * static_cast<size_t>(result->stepRpm));
    if (speed < upper) {
      const float lower = static_cast<float>((i - 1) * static_cast<size_t>(result->stepRpm));
      return VmValue{((curve[i] - curve[i - 1]) * (speed - lower)) / static_cast<float>(result->stepRpm) + curve[i - 1]};
    }
  }
  return VmValue{0.0f};
}
} // namespace

NativeResult dynoDataGetTorque(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
  return sampleDyno(args, receiver, arena, false);
}

NativeResult dynoDataGetHp(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>*) {
  return sampleDyno(args, receiver, arena, true);
}

// --- java.game.parts.bodypart.Chassis ---

// 0043c7f0: java.game.parts.bodypart.Chassis.getTorque (FF)F
NativeResult chassisGetTorque(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>* classes) {
  if (args.size() != 2 || !std::holds_alternative<float>(args[0]) || !std::holds_alternative<float>(args[1]))
    return std::string{"invalid Chassis.getTorque arguments"};
  if (const auto* dyno = getObjectField(receiver, "dyno"))
    return dynoDataGetTorque(args, dyno, arena, classes);
  const auto resolved = resolvePartAndCar(receiver, arena);
  return VmValue{resolved.partInst ? resolved.partInst->driveTorque : 200.0f};
}

// 0043c930: java.game.parts.bodypart.Chassis.setTorque (F)V
NativeResult chassisSetTorque(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
  if (args.size() != 1 || !std::holds_alternative<float>(args[0]))
    return std::string{"invalid Chassis.setTorque arguments"};
  const float val = std::get<float>(args[0]);
  const auto resolved = resolvePartAndCar(receiver, arena);
  if (resolved.partInst) resolved.partInst->driveTorque = val;
  if (resolved.carInst) resolved.carInst->driveTorque = val;
  return std::monostate{};
}

// 0043caa0: java.game.parts.bodypart.Chassis.getMass ()F
NativeResult chassisGetMass(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
  if (!args.empty()) return std::string{"invalid Chassis.getMass arguments"};
  const auto resolved = resolvePartAndCar(receiver, arena);
  if (resolved.carInst && resolved.carInst->totalMass > 0.0f) return VmValue{resolved.carInst->totalMass};
  if (resolved.partInst && resolved.partInst->totalMass > 0.0f) return VmValue{resolved.partInst->totalMass};
  if (resolved.partNode && arena && arena->gameState && arena->gameState->world) {
    const float m = prototypeMass(nodePrototype(resolved.partNode, *arena->gameState->world));
    if (m > 0.0f) return VmValue{m};
  }
  return VmValue{1200.0f};
}

// 0043cb80: java.game.parts.bodypart.Chassis.getCM ()Ljava.lang.Vector3;
NativeResult chassisGetCm(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                          const std::vector<const TufaClass*>* classes) {
  if (!args.empty()) return std::string{"invalid Chassis.getCM arguments"};
  return allocateVector3(arena, classes, 0.0f, 0.0f, 0.0f);
}

// 0043d600: java.game.parts.bodypart.Chassis.getMin ()Ljava.lang.Vector3;
NativeResult chassisGetMin(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                           const std::vector<const TufaClass*>* classes) {
  if (!args.empty()) return std::string{"invalid Chassis.getMin arguments"};
  return allocateVector3(arena, classes, -0.95f, -0.45f, -2.2f);
}

// 0043d8b0: java.game.parts.bodypart.Chassis.getMax ()Ljava.lang.Vector3;
NativeResult chassisGetMax(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                           const std::vector<const TufaClass*>* classes) {
  if (!args.empty()) return std::string{"invalid Chassis.getMax arguments"};
	return allocateVector3(arena, classes, 0.95f, 0.85f, 2.2f);
}

// 0043cd70: java.game.parts.bodypart.Chassis.getWheels ()I
NativeResult chassisGetWheels(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Chassis.getWheels arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (arena && arena->gameState && arena->gameState->world) {
		if (const auto* proto = nodePrototype(resolved.partNode ? resolved.partNode : resolved.carNode,
		                                      *arena->gameState->world)) {
			if (!proto->config.wheels.empty()) return VmValue{static_cast<int32_t>(proto->config.wheels.size())};
		}
	}
	return VmValue{int32_t{4}};
}

// 0043ce30: java.game.parts.bodypart.Chassis.getWheelPos (I)Ljava.lang.Vector3;
NativeResult chassisGetWheelPos(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Chassis.getWheelPos arguments"};
	const int32_t idx = std::get<int32_t>(args[0]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	CarRuntimeInstance* inst = resolved.partInst ? resolved.partInst : resolved.carInst;
	std::array<float, 3> pos{0.0f, 0.0f, 0.0f};
	if (inst && idx >= 0 && static_cast<size_t>(idx) < inst->wheelStates.size())
		pos = inst->wheelStates[static_cast<size_t>(idx)].position;
	return allocateVector3(arena, classes, pos[0], pos[1], pos[2]);
}

// 0043d080: java.game.parts.bodypart.Chassis.getWheelDamage (I)Ljava.lang.String;
NativeResult chassisGetWheelDamage(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                   const std::vector<const TufaClass*>* classes) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Chassis.getWheelDamage arguments"};
	const int32_t idx = std::get<int32_t>(args[0]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	CarRuntimeInstance* inst = resolved.partInst ? resolved.partInst : resolved.carInst;
	if (!inst || idx < 0 || static_cast<size_t>(idx) >= inst->wheelExtras.size())
		return VmValue{static_cast<const VmObject*>(nullptr)};
	const auto& dmg = inst->wheelExtras[static_cast<size_t>(idx)].damage;
	if (dmg.empty()) return VmValue{static_cast<const VmObject*>(nullptr)};
	return internStringResult(dmg, arena, classes);
}

// 0043d280: java.game.parts.bodypart.Chassis.setWheelDamage (ILjava.lang.String;)V
NativeResult chassisSetWheelDamage(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                   const std::vector<const TufaClass*>*) {
	if (args.size() != 2 || !std::holds_alternative<int32_t>(args[0]) ||
	    !std::holds_alternative<const VmObject*>(args[1]))
		return std::string{"invalid Chassis.setWheelDamage arguments"};
	const int32_t idx = std::get<int32_t>(args[0]);
	const auto* strObj = std::get<const VmObject*>(args[1]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	CarRuntimeInstance* inst = resolved.partInst ? resolved.partInst : resolved.carInst;
	if (inst && idx >= 0 && static_cast<size_t>(idx) < inst->wheelExtras.size()) {
		const auto* text = findInternedString(arena, strObj);
		inst->wheelExtras[static_cast<size_t>(idx)].damage = text ? *text : "";
	}
	return std::monostate{};
}

// 00440c80: java.game.parts.bodypart.Chassis.getWheel (I)Ljava.game.parts.WheelRef;
NativeResult chassisGetWheel(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>* classes) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Chassis.getWheel arguments"};
	const int32_t idx = std::get<int32_t>(args[0]);
	if (idx < 0 || idx >= 16) return VmValue{static_cast<const VmObject*>(nullptr)};
	const auto resolved = resolvePartAndCar(receiver, arena);
	WorldNode* targetNode = resolved.partNode ? resolved.partNode : resolved.carNode;
	if (!targetNode || !targetNode->carInstance) return VmValue{static_cast<const VmObject*>(nullptr)};
	runlog::event(runlog::EventKind::Note, "wheel reference created: chassis=0x%08x car=0x%08x index=%d",
	              targetNode->instanceId, resolved.carNode ? resolved.carNode->instanceId : 0u, idx);
	auto* wheelObj = allocateObjectWithPtr("java.game.parts.WheelRef", targetNode->instanceId,
	                                       static_cast<uint32_t>(idx), arena, classes);
	return VmValue{wheelObj};
}

// 00442210: java.game.parts.bodypart.Chassis.getSfxTable (I)Ljava.game.parts.SfxTable;
NativeResult chassisGetSfxTable(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>* classes) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"invalid Chassis.getSfxTable arguments"};
	const int32_t idx = std::get<int32_t>(args[0]);
	if (idx < 0 || idx >= 3) return VmValue{static_cast<const VmObject*>(nullptr)};
	const auto resolved = resolvePartAndCar(receiver, arena);
	WorldNode* targetNode = resolved.partNode ? resolved.partNode : resolved.carNode;
	if (!targetNode || !targetNode->carInstance) return VmValue{static_cast<const VmObject*>(nullptr)};
	auto* sfxObj = allocateObjectWithPtr("java.game.parts.SfxTable", targetNode->instanceId,
	                                     static_cast<uint32_t>(idx), arena, classes);
	return VmValue{sfxObj};
}

// 0043deb0: java.game.parts.bodypart.Chassis.setCooling (FFF)V
NativeResult chassisSetCooling(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (args.size() != 3 || !std::holds_alternative<float>(args[0]) ||
	    !std::holds_alternative<float>(args[1]) || !std::holds_alternative<float>(args[2]))
		return std::string{"invalid Chassis.setCooling arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst)
		resolved.partInst->cooling = {std::get<float>(args[0]), std::get<float>(args[1]), std::get<float>(args[2])};
	return std::monostate{};
}

// 0043dc00: java.game.parts.bodypart.Chassis.setHornSFX (Ljava.util.resource.ResourceRef;FI)V
NativeResult chassisSetHornSfx(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (args.size() != 3 || !std::holds_alternative<const VmObject*>(args[0]) ||
	    !std::holds_alternative<float>(args[1]) || !std::holds_alternative<int32_t>(args[2]))
		return std::string{"invalid Chassis.setHornSFX arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) {
		resolved.partInst->hornPitch = std::get<float>(args[1]);
		resolved.partInst->hornIndex = std::get<int32_t>(args[2]);
	}
	return std::monostate{};
}

// 0043dd70: java.game.parts.bodypart.Chassis.setNitroSFX (Ljava.util.resource.ResourceRef;F)V
NativeResult chassisSetNitroSfx(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
	if (args.size() != 2 || !std::holds_alternative<const VmObject*>(args[0]) ||
	    !std::holds_alternative<float>(args[1]))
		return std::string{"invalid Chassis.setNitroSFX arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) resolved.partInst->nitroPitch = std::get<float>(args[1]);
	return std::monostate{};
}

// 0043dff0: java.game.parts.bodypart.Chassis.getMileage ()F
NativeResult chassisGetMileage(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Chassis.getMileage arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) return VmValue{resolved.partInst->mileage};
	if (resolved.carInst) return VmValue{resolved.carInst->mileage};
	return VmValue{0.0f};
}

// 0043e080: java.game.parts.bodypart.Chassis.setMileage (F)V
NativeResult chassisSetMileage(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0]))
		return std::string{"invalid Chassis.setMileage arguments"};
	const float val = std::get<float>(args[0]);
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) resolved.partInst->mileage = val;
	if (resolved.carInst) resolved.carInst->mileage = val;
	return std::monostate{};
}

// 0043e110: java.game.parts.bodypart.Chassis.setBuck (IIFFFF)I
NativeResult chassisSetBuck(const std::vector<VmValue>& args, const VmObject*, VmArena*,
                            const std::vector<const TufaClass*>*) {
	if (args.size() != 6) return std::string{"invalid Chassis.setBuck arguments"};
	return VmValue{int32_t{0}};
}

// 0043e320: java.game.parts.bodypart.Chassis.forceUpdate ()V
NativeResult chassisForceUpdate(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Chassis.forceUpdate arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	runlog::event(runlog::EventKind::Note, "chassis force update: part=0x%08x car=0x%08x script=%s",
	              resolved.partNode ? resolved.partNode->instanceId : 0u,
	              resolved.carNode ? resolved.carNode->instanceId : 0u,
	              receiver && receiver->runtimeClass ? receiver->runtimeClass->name.c_str() : "none");
  // 0043e320 enters the same native routine as the frame request.
  if (resolved.carNode && resolved.carInst && resolved.carInst->carPhysics) {
    if (!arena)
      return std::string{"Chassis.forceUpdate: script arena unavailable"};
    std::string error;
    // Copy the function: vmExecute may temporarily replace arena.callScript.
    const auto callback = arena->callScript;
    if (!refreshCarVariables(*resolved.carNode, *arena, callback, error))
      return "Chassis.forceUpdate: " + error;
  }
	if (arena && arena->gameState && arena->gameState->world) {
		if (resolved.carNode) carPartUpdateMass(*resolved.carNode, *arena->gameState->world);
		else if (resolved.partNode) carPartUpdateMass(*resolved.partNode, *arena->gameState->world);
	}
	return std::monostate{};
}

// 004409d0: java.game.parts.bodypart.Chassis.setSfxExhaustMinVol (F)V
NativeResult chassisSetSfxExhaustMinVol(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                        const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0]))
		return std::string{"invalid Chassis.setSfxExhaustMinVol arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) resolved.partInst->sfxExhaustMinVol = std::get<float>(args[0]);
	return std::monostate{};
}

// 00440a50: java.game.parts.bodypart.Chassis.setSteerWheel (FF)V
NativeResult chassisSetSteerWheel(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                  const std::vector<const TufaClass*>*) {
	if (args.size() != 2 || !std::holds_alternative<float>(args[0]) || !std::holds_alternative<float>(args[1]))
		return std::string{"invalid Chassis.setSteerWheel arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) {
		resolved.partInst->steerWheelRadius = std::get<float>(args[0]);
		resolved.partInst->steerWheelLock = std::get<float>(args[1]);
	}
	return std::monostate{};
}

// 00440af0: java.game.parts.bodypart.Chassis.setSteerWheelRadius (F)V
NativeResult chassisSetSteerWheelRadius(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                        const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0]))
		return std::string{"invalid Chassis.setSteerWheelRadius arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) resolved.partInst->steerWheelRadius = std::get<float>(args[0]);
	return std::monostate{};
}

// 00440b70: java.game.parts.bodypart.Chassis.setAckermann (F)V
NativeResult chassisSetAckermann(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0]))
		return std::string{"invalid Chassis.setAckermann arguments"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.partInst) resolved.partInst->ackermann = std::get<float>(args[0]);
	return std::monostate{};
}

// --- java.game.parts.WheelRef ---

NativeResult wheelRefGetDrive(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid WheelRef.getDrive"};
	const auto w = resolveWheel(receiver, arena);
	return VmValue{w.extra ? w.extra->drive : 0.0f};
}

NativeResult wheelRefSetDrive(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setDrive"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->drive = std::get<float>(args[0]);
	return std::monostate{};
}

NativeResult wheelRefGetSteer(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid WheelRef.getSteer"};
	const auto w = resolveWheel(receiver, arena);
	return VmValue{w.extra ? w.extra->steer : 0.0f};
}

NativeResult wheelRefSetSteer(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setSteer"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->steer = std::get<float>(args[0]);
	return std::monostate{};
}

NativeResult wheelRefGetRadius(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid WheelRef.getRadius"};
	const auto w = resolveWheel(receiver, arena);
	return VmValue{w.extra ? w.extra->radius : 0.3f};
}

NativeResult wheelRefSetRadius(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setRadius"};
	if (auto w = resolveWheel(receiver, arena); w.extra) {
		w.extra->radius = std::get<float>(args[0]);
		configureWheelSuspension(w, "radius");
	}
	return std::monostate{};
}

NativeResult wheelRefSetFriction(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setFriction"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->tire.setFriction(std::get<float>(args[0]));
	return std::monostate{};
}

NativeResult wheelRefSetFrictnX(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setFrictn_x"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->tire.setFrictn_x(std::get<float>(args[0]));
	return std::monostate{};
}

NativeResult wheelRefSetSliction(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setSliction"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->tire.setSliction(std::get<float>(args[0]));
	return std::monostate{};
}

NativeResult wheelRefSetStiffness(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                  const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setStiffness"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->tire.setStiffness(std::get<float>(args[0]));
	return std::monostate{};
}

NativeResult wheelRefSetRollRes(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setRollRes"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->rollResistance = std::get<float>(args[0]);
	return std::monostate{};
}

NativeResult wheelRefSetBearing(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setBearing"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->bearing = std::get<float>(args[0]);
	return std::monostate{};
}

NativeResult wheelRefSetMaxLoad(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setMaxLoad"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->tire.setMaxLoad(std::get<float>(args[0]));
	return std::monostate{};
}

NativeResult wheelRefSetLoadSmooth(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                   const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setLoadSmooth"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->tire.setLoadSmooth(std::get<float>(args[0]));
	return std::monostate{};
}

NativeResult wheelRefSetPacejka(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
	if (args.size() != 2 || !std::holds_alternative<int32_t>(args[0]) || !std::holds_alternative<float>(args[1]))
		return std::string{"invalid WheelRef.setPacejka"};
	const int32_t idx = std::get<int32_t>(args[0]);
	if (auto w = resolveWheel(receiver, arena); w.extra)
		w.extra->tire.setPacejka(idx, std::get<float>(args[1]));
	return std::monostate{};
}

NativeResult wheelRefSetForce(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setForce"};
	if (auto w = resolveWheel(receiver, arena); w.extra) {
		w.extra->springForce = std::get<float>(args[0]);
		configureWheelSuspension(w, "force");
	}
	return std::monostate{};
}

NativeResult wheelRefSetDamping1(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setDamping(F)"};
	if (auto w = resolveWheel(receiver, arena); w.extra) {
		w.extra->dampingBump = std::get<float>(args[0]);
		w.extra->dampingRebound = std::get<float>(args[0]) * 0.7f;
		configureWheelSuspension(w, "damping");
	}
	return std::monostate{};
}

NativeResult wheelRefSetDamping2(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>*) {
	if (args.size() != 2 || !std::holds_alternative<float>(args[0]) || !std::holds_alternative<float>(args[1]))
		return std::string{"invalid WheelRef.setDamping(FF)"};
	if (auto w = resolveWheel(receiver, arena); w.extra) {
		w.extra->dampingBump = std::get<float>(args[0]);
		w.extra->dampingRebound = std::get<float>(args[1]);
		configureWheelSuspension(w, "damping pair");
	}
	return std::monostate{};
}

NativeResult wheelRefSetRestLen(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setRestLen"};
	if (auto w = resolveWheel(receiver, arena); w.extra) {
		w.extra->restLength = std::get<float>(args[0]);
		configureWheelSuspension(w, "rest length");
	}
	return std::monostate{};
}

NativeResult wheelRefSetMinLen(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setMinLen"};
	if (auto w = resolveWheel(receiver, arena); w.extra) {
		w.extra->minLength = std::get<float>(args[0]);
		configureWheelSuspension(w, "min length");
	}
	return std::monostate{};
}

NativeResult wheelRefSetMaxLen(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setMaxLen"};
	if (auto w = resolveWheel(receiver, arena); w.extra) {
		w.extra->maxLength = std::get<float>(args[0]);
		configureWheelSuspension(w, "max length");
	}
	return std::monostate{};
}

NativeResult wheelRefGetBrake(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid WheelRef.getBrake"};
	const auto w = resolveWheel(receiver, arena);
	return VmValue{w.extra ? w.extra->brake : 0.0f};
}

NativeResult wheelRefSetBrake(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setBrake"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->brake = std::get<float>(args[0]);
	return std::monostate{};
}

NativeResult wheelRefGetHBrake(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid WheelRef.getHBrake"};
	const auto w = resolveWheel(receiver, arena);
	return VmValue{w.extra ? w.extra->handBrake : 0.0f};
}

NativeResult wheelRefSetHBrake(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setHBrake"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->handBrake = std::get<float>(args[0]);
	return std::monostate{};
}

NativeResult wheelRefSetWidth(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<float>(args[0])) return std::string{"invalid WheelRef.setWidth"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->width = std::get<float>(args[0]);
	return std::monostate{};
}

NativeResult wheelRefSetCPatch(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                               const std::vector<const TufaClass*>*) {
	if (args.size() != 3 || !std::holds_alternative<float>(args[0]) ||
	    !std::holds_alternative<float>(args[1]) || !std::holds_alternative<float>(args[2]))
		return std::string{"invalid WheelRef.setCPatch"};
	if (auto w = resolveWheel(receiver, arena); w.extra)
		w.extra->contactPatch = {std::get<float>(args[0]), std::get<float>(args[1]), std::get<float>(args[2])};
	return std::monostate{};
}

NativeResult wheelRefSetArm(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (args.size() != 7) return std::string{"invalid WheelRef.setArm"};
	if (auto w = resolveWheel(receiver, arena); w.extra) {
		for (size_t i = 0; i < 7; ++i)
			if (const auto* fv = std::get_if<float>(&args[i])) w.extra->arm[i] = *fv;
		runlog::event(runlog::EventKind::Note, "wheel arm parameters (pose application gap): %.3g %.3g %.3g %.3g %.3g %.3g %.3g",
		              w.extra->arm[0],w.extra->arm[1],w.extra->arm[2],w.extra->arm[3],w.extra->arm[4],w.extra->arm[5],w.extra->arm[6]);
	}
	return std::monostate{};
}

NativeResult wheelRefSetHub(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (args.size() != 10) return std::string{"invalid WheelRef.setHub"};
	if (auto w = resolveWheel(receiver, arena); w.extra) {
		for (size_t i = 0; i < 10; ++i)
			if (const auto* fv = std::get_if<float>(&args[i])) w.extra->hub[i] = *fv;
		runlog::event(runlog::EventKind::Note, "wheel hub parameters (pose application gap): %.3g %.3g %.3g %.3g %.3g %.3g %.3g %.3g %.3g %.3g",
		              w.extra->hub[0],w.extra->hub[1],w.extra->hub[2],w.extra->hub[3],w.extra->hub[4],w.extra->hub[5],w.extra->hub[6],w.extra->hub[7],w.extra->hub[8],w.extra->hub[9]);
	}
	return std::monostate{};
}

NativeResult wheelRefSetOppWheel(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                 const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<int32_t>(args[0])) return std::string{"invalid WheelRef.setOppWheel"};
	if (auto w = resolveWheel(receiver, arena); w.extra) w.extra->oppositeWheel = std::get<int32_t>(args[0]);
	return std::monostate{};
}

NativeResult wheelRefSetInstantCenter(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                      const std::vector<const TufaClass*>*) {
	if (args.size() != 6) return std::string{"invalid WheelRef.setInstantCenter"};
	if (auto w = resolveWheel(receiver, arena); w.extra) {
		for (size_t i = 0; i < 6; ++i)
			if (const auto* fv = std::get_if<float>(&args[i])) w.extra->instantCenter[i] = *fv;
	}
	return std::monostate{};
}

NativeResult wheelRefGetPos(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes) {
	if (!args.empty()) return std::string{"invalid WheelRef.getPos"};
	const auto w = resolveWheel(receiver, arena);
	const std::array<float, 3> p = w.wheel ? w.wheel->position : std::array<float, 3>{0.0f, 0.0f, 0.0f};
	return allocateVector3(arena, classes, p[0], p[1], p[2]);
}

NativeResult wheelRefSetPos(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<const VmObject*>(args[0]))
		return std::string{"invalid WheelRef.setPos"};
	if (auto w = resolveWheel(receiver, arena); w.wheel)
		readVec3Object(std::get<const VmObject*>(args[0]), w.wheel->position);
	return std::monostate{};
}

NativeResult wheelRefGetYpr(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>* classes) {
	if (!args.empty()) return std::string{"invalid WheelRef.getYpr"};
	const auto w = resolveWheel(receiver, arena);
	const std::array<float, 3> o = w.wheel ? w.wheel->orientation : std::array<float, 3>{0.0f, 0.0f, 0.0f};
	return allocateYpr(arena, classes, o[0], o[1], o[2]);
}

NativeResult wheelRefSetYpr(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (args.size() != 1 || !std::holds_alternative<const VmObject*>(args[0]))
		return std::string{"invalid WheelRef.setYpr"};
	if (auto w = resolveWheel(receiver, arena); w.wheel)
		readYprObject(std::get<const VmObject*>(args[0]), w.wheel->orientation);
	return std::monostate{};
}

// --- java.game.parts.SfxTable ---

// 00442310: java.game.parts.SfxTable.getItems ()I
NativeResult sfxTableGetItems(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                              const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid SfxTable.getItems"};
	auto* table = resolveSfxTable(receiver, arena);
	return VmValue{table ? static_cast<int32_t>(table->size()) : int32_t{0}};
}

// 00442350: java.game.parts.SfxTable.clear ()V
NativeResult sfxTableClear(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                           const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid SfxTable.clear"};
	if (auto* table = resolveSfxTable(receiver, arena)) table->clear();
	return std::monostate{};
}

// 004423e0: java.game.parts.SfxTable.addItem (Ljava.util.resource.ResourceRef;FFFFF)V
NativeResult sfxTableAddItem(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>*) {
	if (args.size() != 6 || !std::holds_alternative<const VmObject*>(args[0]) ||
	    !std::holds_alternative<float>(args[1]) || !std::holds_alternative<float>(args[2]) ||
	    !std::holds_alternative<float>(args[3]) || !std::holds_alternative<float>(args[4]) ||
	    !std::holds_alternative<float>(args[5]))
		return std::string{"invalid SfxTable.addItem"};
	if (auto* table = resolveSfxTable(receiver, arena)) {
		LinkOwner* soundOwner = nullptr;
		if (const auto* refObj = std::get<const VmObject*>(args[0]))
			soundOwner = linkedOwnerForObject(refObj, arena);
		table->addItem(soundOwner, std::get<float>(args[1]), std::get<float>(args[2]), std::get<float>(args[3]),
		               std::get<float>(args[4]), std::get<float>(args[5]));
	}
	return std::monostate{};
}

// --- java.game.Vehicle ---

// 0043db60: java.game.Vehicle.getHorn ()I
NativeResult vehicleGetHorn(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                            const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Vehicle.getHorn"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.carInst && resolved.carInst->hornOn) return VmValue{int32_t{1}};
	return VmValue{int32_t{0}};
}

// 00480500: java.game.Vehicle.getSpeedSquare ()F
NativeResult vehicleGetSpeedSquare(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                                   const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Vehicle.getSpeedSquare"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	if (resolved.carInst) {
		const auto& v = resolved.carInst->linearVelocity;
		return VmValue{v[0] * v[0] + v[1] * v[1] + v[2] * v[2]};
	}
	return VmValue{0.0f};
}

// 00440bf0: java.game.Vehicle.hasCrime ()F
NativeResult vehicleHasCrime(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>*) {
	if (!args.empty()) return std::string{"invalid Vehicle.hasCrime"};
	const auto resolved = resolvePartAndCar(receiver, arena);
	return VmValue{resolved.carInst ? resolved.carInst->crimeLevel : 0.0f};
}
