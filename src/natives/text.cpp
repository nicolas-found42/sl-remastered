#include "natives/text.hpp"
#include "natives/game_type.hpp"
#include "resource_world.hpp"
#include "scene_2d.hpp"
#include "font.hpp"
#include <cmath>

NativeResult textGetWidthPixels(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                                const std::vector<const TufaClass*>*) {
	if (!arena || args.size() != 2 || !std::holds_alternative<const VmObject*>(args[0]) ||
	    !std::holds_alternative<const VmObject*>(args[1])) return std::string{"Text.getWidthPixels: invalid call"};
	const auto* text = std::get<const VmObject*>(args[0]);
	if (!text) return VmValue{0.0f};
	if (!arena->gameState || !arena->gameState->resources || !arena->gameState->world || !arena->gameState->renderer)
		return std::string{"Text.getWidthPixels: render host unavailable"};
	const std::string* bytes = nullptr;
	for (const auto& item : arena->strings) if (item.second == text) {
		if (bytes) return std::string{"Text.getWidthPixels: ambiguous string"};
		bytes = &item.first;
	}
	if (!bytes) return std::string{"Text.getWidthPixels: unknown string"};
	const auto* font = std::get<const VmObject*>(args[1]);
	const auto* state = nativeResourceState(font, arena);
	if (!state) return std::string{"Text.getWidthPixels: invalid font link"};
	auto& game = *arena->gameState;
	const auto handle = static_cast<int32_t>(state - arena->resourceStates.data()) + 1;
	const auto binding = game.bindings.find(handle);
	if (binding == game.bindings.end() || !binding->second || !binding->second->link.owner) return VmValue{0.0f};
	const auto fontNode = game.resources->resolve(binding->second->link.owner);
	if (!fontNode || fontNode.type != 14 || !fontNode.packNode) return std::string{"Text.getWidthPixels: unsupported font resource"};
	auto material = game.resources->sceneMaterial(fontNode);
	const bool materialWasBound = static_cast<bool>(material);
	if (!material) {
		auto loaded = sceneLoadMaterial(*game.resources, fontNode.id);
		if (!loaded.material || loaded.flags != 4100) return "Text.getWidthPixels: unsupported font material: " + loaded.error;
		material = std::move(loaded.material);
	}
	if (material->flags() != 4100 || material->textureCount() != 1) return std::string{"Text.getWidthPixels: unsupported font material"};
	const auto meshNode = game.resources->lookup(material->meshId(), 5);
	if (meshNode.status != LookupStatus::Found || !meshNode.node.packNode) return std::string{"Text.getWidthPixels: unsupported font mesh"};
	auto mesh = game.resources->sceneMesh(meshNode.node);
	const bool meshWasBound = static_cast<bool>(mesh);
	std::string error;
	if (!mesh) {
		mesh = std::make_shared<ScxModel>();
		if (!sceneLoadMesh(*game.resources, meshNode.node.id, game.resources->packs().root(), *mesh, error, true))
			return "Text.getWidthPixels: " + error;
	}
	float width = 0;
	if (!fontTextWidth(*mesh, *bytes, width, error)) return "Text.getWidthPixels: " + error;
	for (const auto& node : {fontNode, meshNode.node}) {
		const auto& packed = *node.packNode;
		if (!packed.field6c && packed.stamp7c >= game.world->frameCounter())
			return std::string{"Text.getWidthPixels: unknown resource load value"};
	}
	if ((!meshWasBound && !game.resources->bindSceneMesh(meshNode.node, mesh)) ||
	    (!materialWasBound && !game.resources->bindSceneMaterial(fontNode, material)))
		return std::string{"Text.getWidthPixels: font cache failed"};
	for (const auto& node : {fontNode, meshNode.node}) {
		auto& packed = *node.packNode;
		if (packed.stamp7c < game.world->frameCounter() || (packed.field6c && *packed.field6c < 1.0f)) packed.field6c = 1.0f;
		if (!packed.loaded()) {
			packed.stateWord = (packed.stateWord & ~uint32_t{0x50}) | 0x21;
			packed.loadPriority = 1.0f;
			game.world->trackLoadState(*node.owner, node.type, true);
		}
		if (packed.stamp74 != game.world->frameCounter() && !packed.recentUseQueued)
			packed.stateWord = (packed.stateWord & ~uint32_t{0x60}) | 0x10;
		game.world->noteRecentUse(*node.owner, node.type, packed.stamp74, packed.recentUseQueued);
	}
	return VmValue{width};
}

NativeResult textSetDefaultColor(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                                 const std::vector<const TufaClass*>*) {
	if (!arena || !arena->gameState || args.size() != 1 || !std::holds_alternative<int32_t>(args[0]))
		return std::string{"Text.setDefaultColor: invalid call"};
	arena->gameState->textDefaultColor = std::get<int32_t>(args[0]);
	return std::monostate{};
}
NativeResult textGetDefaultColor(const std::vector<VmValue>& args, const VmObject*, VmArena* arena,
                                 const std::vector<const TufaClass*>*) {
	if (!arena || !arena->gameState || !args.empty()) return std::string{"Text.getDefaultColor: invalid call"};
	return VmValue{arena->gameState->textDefaultColor};
}

namespace {
const VmObject* textFieldObject(const VmObject* object, const char* name) {
	if (!object) return nullptr;
	const VmObject* result = nullptr;
	bool found = false;
	for (const auto& field : object->fields) if (field.name == name) {
		if (found || !std::holds_alternative<const VmObject*>(field.value)) return nullptr;
		found = true;
		result = std::get<const VmObject*>(field.value);
	}
	return result;
}
OwnerLink* textResourceLink(VmGameState& game, VmArena& arena, const VmObject* object) {
	const auto* state = nativeResourceState(object, &arena);
	if (!state) return nullptr;
	const auto handle = static_cast<int32_t>(state - arena.resourceStates.data()) + 1;
	const auto it = game.bindings.find(handle);
	return it == game.bindings.end() || !it->second ? nullptr : &it->second->link;
}
}

NativeResult textCreate(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                        const std::vector<const TufaClass*>*) {
	if (!arena || !arena->gameState || !receiver || args.size() != 4 ||
	    !std::holds_alternative<const VmObject*>(args[0]) || !std::holds_alternative<const VmObject*>(args[1]) ||
	    !std::holds_alternative<float>(args[2]) || !std::holds_alternative<float>(args[3]))
		return std::string{"Text.create: invalid call"};
	auto& game = *arena->gameState;
	if (!std::isfinite(std::get<float>(args[2])) || !std::isfinite(std::get<float>(args[3])))
		return std::string{"Text.create: nonfinite position"};
	if (!game.world || !game.resources || !game.packs || !game.renderer)
		return std::string{"Text.create: unsupported render host"};
	const auto* renderObject = textFieldObject(receiver, "renderinst");
	auto* self = nativeResourceState(renderObject, arena);
	const auto* parentLink = textResourceLink(game, *arena, std::get<const VmObject*>(args[0]));
	const auto* fontLink = textResourceLink(game, *arena, std::get<const VmObject*>(args[1]));
	if (!self || !parentLink || !parentLink->owner || !fontLink || !fontLink->owner)
		return std::string{"Text.create: invalid resource links"};
	const auto parent = game.resources->resolve(parentLink->owner);
	const auto font = game.resources->resolve(fontLink->owner);
	if (!parent || parent.type != 1 || (!parent.worldNode && !parent.packNode) ||
	    (parent.worldNode && !parent.worldNode->loaded) || (parent.packNode && !parent.packNode->loaded()) ||
	    !font || font.type != 14 || !font.packNode)
		return std::string{"Text.create: unsupported parent or font"};
	auto material = game.resources->sceneMaterial(font);
	if (!material) {
		auto loaded = sceneLoadMaterial(*game.resources, font.id);
		if (!loaded.material || loaded.flags != 4100) return std::string{"Text.create: unsupported font material"};
		material = std::move(loaded.material);
	}
	SceneMaterialCache cache; std::string error;
	if (material->flags() != 4100 || !sceneRefreshMaterial(*game.resources, *material, game.packs->root(), cache, error))
		return "Text.create: " + error;
	float width = 0;
	if (!fontTextWidth(cache.mesh, "", width, error)) return "Text.create: " + error;
	if (!game.resources->sceneMaterial(font) && !game.resources->bindSceneMaterial(font, material))
		return std::string{"Text.create: material binding failed"};
	WorldNode* node = parent.worldNode ? game.world->addTypedChild(*parent.worldNode, 3, "r_text", 0x200)
	    : game.world->addTypedChildUnderExternal(parent.packNode->handle, *parent.packNode, parent.type,
	          parent.packNode->sectorLink.owner, 3, "r_text", 0x200);
	if (!node) return std::string{"Text.create: allocation failed"};
	node->bounds = ResourceBounds{};
	node->field6c = parent.worldNode ? parent.worldNode->field6c : parent.packNode->field6c;
	node->field70 = parent.worldNode ? parent.worldNode->field70 : parent.packNode->field70;
	game.world->markLoaded(*node); node->loadPriority = 1.0f;
	auto instance = std::make_shared<SceneRenderInstance>(font.owner);
	instance->setBoneTranslation(std::get<float>(args[2]), std::get<float>(args[3]), 0);
	instance->setTextState({"", 2, static_cast<uint32_t>(game.textDefaultColor), 1.0f});
	if (!game.resources->bindSceneRenderInstance(game.resources->resolve(node), instance)) {
		game.world->destroy(*node); return std::string{"Text.create: instance binding failed"};
	}
	const auto handle = static_cast<int32_t>(self - arena->resourceStates.data()) + 1;
	auto& binding = game.bindings[handle];
	if (!binding) binding = std::make_unique<ReceiverBinding>();
	setLinkOwner(binding->link, node); self->words[2] = binding->link.cachedValue;
	return std::monostate{};
}

NativeResult textUpdate(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                        const std::vector<const TufaClass*>*) {
	if (!arena || !arena->gameState || !arena->gameState->resources || !receiver || !args.empty())
		return std::string{"Text.update: invalid call"};
	auto& game = *arena->gameState;
	const auto* link = textResourceLink(game, *arena, textFieldObject(receiver, "renderinst"));
	if (!link || !link->owner) return std::monostate{};
	const auto node = game.resources->resolve(link->owner);
	const auto instance = node ? game.resources->sceneRenderInstance(node) : nullptr;
	if (!instance || !instance->textState() || !node.worldNode || !node.worldNode->loaded) return std::string{"Text.update: unsupported instance"};
	const auto* text = textFieldObject(receiver, "text");
	bool hasTextField = false;
	for (const auto& field : receiver->fields) if (field.name == "text") {
		if (hasTextField || !std::holds_alternative<const VmObject*>(field.value))
			return std::string{"Text.update: invalid text field"};
		hasTextField = true;
	}
	if (!hasTextField) return std::string{"Text.update: missing text field"};
	std::string value;
	if (text) {
		const std::string* found = nullptr;
		for (const auto& item : arena->strings) if (item.second == text) {
			if (found) return std::string{"Text.update: ambiguous string"};
			found = &item.first;
		}
		if (!found) return std::string{"Text.update: unknown string"};
		value = *found;
	}
	for (size_t i = 0; i < value.size();) {
		if (value[i] == '\0') return std::string{"Text.update: embedded zero unsupported"};
		if (value[i] != '\x1b') { ++i; continue; }
		if (i + 1 >= value.size()) return std::string{"Text.update: truncated escape"};
		const size_t length = value[i + 1] == 'C' ? 10 : 2;
		if (length > value.size() - i || value.substr(i, length).find('\0') != std::string::npos)
			return std::string{"Text.update: truncated escape"};
		i += length;
	}
	auto state = *instance->textState(); state.text = std::move(value); instance->setTextState(std::move(state));
	return std::monostate{};
}

NativeResult textChangeAlign(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>*) {
	if (!arena || !arena->gameState || !arena->gameState->resources || !receiver || args.size() != 1 ||
	    !std::holds_alternative<int32_t>(args[0])) return std::string{"Text.changeAlign: invalid call"};
	const int32_t alignment = std::get<int32_t>(args[0]);
	if (alignment < 0 || alignment > 2) return std::string{"Text.changeAlign: unsupported alignment"};
	auto& game = *arena->gameState;
	const auto* link = textResourceLink(game, *arena, textFieldObject(receiver, "renderinst"));
	if (!link || !link->owner) return std::monostate{};
	const auto node = game.resources->resolve(link->owner);
	const auto instance = node ? game.resources->sceneRenderInstance(node) : nullptr;
	if (!instance || !instance->textState() || !node.worldNode || !node.worldNode->loaded) return std::string{"Text.changeAlign: unsupported instance"};
	auto state = *instance->textState(); state.alignment = alignment; instance->setTextState(std::move(state));
	return std::monostate{};
}

NativeResult textChangeColor(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                             const std::vector<const TufaClass*>*) {
	if (!arena || !arena->gameState || !arena->gameState->resources || !receiver || args.size() != 1 ||
	    !std::holds_alternative<int32_t>(args[0])) return std::string{"Text.changeColor: invalid call"};
	auto& game = *arena->gameState;
	const auto* link = textResourceLink(game, *arena, textFieldObject(receiver, "renderinst"));
	if (!link || !link->owner) return std::monostate{};
	const auto node = game.resources->resolve(link->owner);
	const auto instance = node ? game.resources->sceneRenderInstance(node) : nullptr;
	if (!instance || !instance->textState() || !node.worldNode || !node.worldNode->loaded) return std::string{"Text.changeColor: unsupported instance"};
	auto state = *instance->textState(); state.color = static_cast<uint32_t>(std::get<int32_t>(args[0]));
	instance->setTextState(std::move(state));
	return std::monostate{};
}

NativeResult textSetPos(const std::vector<VmValue>& args, const VmObject* receiver, VmArena* arena,
                        const std::vector<const TufaClass*>*) {
	if (!arena || !arena->gameState || !arena->gameState->resources || !receiver || args.size() != 2 ||
	    !std::holds_alternative<float>(args[0]) || !std::holds_alternative<float>(args[1]))
		return std::string{"Text.setPos: invalid call"};
	const float x = std::get<float>(args[0]), y = std::get<float>(args[1]);
	if (!std::isfinite(x) || !std::isfinite(y)) return std::string{"Text.setPos: nonfinite position"};
	auto& game = *arena->gameState;
	const auto* link = textResourceLink(game, *arena, textFieldObject(receiver, "renderinst"));
	if (!link || !link->owner) return std::monostate{};
	const auto node = game.resources->resolve(link->owner);
	const auto instance = node ? game.resources->sceneRenderInstance(node) : nullptr;
	if (!instance || !instance->textState() || !node.worldNode || !node.worldNode->loaded) return std::string{"Text.setPos: unsupported instance"};
	instance->setBoneTranslation(x, y, 0);
	return std::monostate{};
}
