#include "font.hpp"
#include "car_runtime.hpp"
#include "video.hpp"
#include "natives/game_type.hpp"
#include "natives/registry.hpp"
#include "pack_registry.hpp"
#include "resource_world.hpp"
#include "render2d.hpp"
#include "scene_2d.hpp"
#include <SDL3/SDL.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
int main() {
	auto parsed = scxLoadFile("ref/raw/frontend/meshes/Fonts/slii11.scx");
	CHECK(parsed.ok);
	CHECK(stockMeshVertexBytes(parsed.model) == 1024u * 56u);
	std::string error; float width = 0;
	CHECK(fontTextWidth(parsed.model, "AB A", width, error) && width == 34.0f);
	CHECK(fontTextWidth(parsed.model, "A\x1b" "C12345678B", width, error) && width == 19.0f);
	CHECK(!fontTextWidth(parsed.model, "A\x1b" "Cshort", width, error));
	CHECK(SDL_Init(0));
	auto* surface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
	CHECK(surface);
	auto renderer = Render2D::forSurface(surface, error); CHECK(renderer);
	{
		PackRegistry packs("ref/raw"); GameWorld world; CHECK(world.addRoot());
		ResourceWorld resources(world, packs);
		VmGameState game{&packs, &world, {}, {}, &resources, renderer.get()};
		VmArena arena; arena.gameState = &game;
		const auto color = invokeNative("java.render.Text", "getDefaultColor", "()I", {}, nullptr, &arena);
		CHECK(std::holds_alternative<VmValue>(color) && std::get<int32_t>(std::get<VmValue>(color)) == -1);
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.Text", "setDefaultColor", "(I)V", {int32_t{0x12345678}}, nullptr, &arena)));
		const auto changed = invokeNative("java.render.Text", "getDefaultColor", "()I", {}, nullptr, &arena);
		CHECK(std::holds_alternative<VmValue>(changed) && std::get<int32_t>(std::get<VmValue>(changed)) == 0x12345678);
		TufaClass refClass; refClass.name = "java.util.resource.ResourceRef"; refClass.hasClassHeader = true;
		std::vector<const TufaClass*> classes{&refClass};
		auto object = std::make_unique<VmObject>(); object->runtimeClass = &refClass;
		object->fields.push_back({"ptr", int32_t{0}, "I"});
		auto* font = object.get(); arena.objects.push_back(std::move(object));
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.util.resource.ResourceRef", "newNative", "()V", {}, font, &arena, &classes)));
		uint32_t id = 0; CHECK(packs.resolveLiteral("frontend.rpk", 0x138, id, error));
		for (uint32_t localId : {0x25u, 0x139u, 0x23u}) {
			uint32_t otherId = 0; CHECK(packs.resolveLiteral("frontend.rpk", localId, otherId, error));
			auto other = sceneLoadMaterial(resources, otherId);
			CHECK(other.material && other.flags == 4100);
			SceneMaterialCache otherCache;
			CHECK(sceneRefreshMaterial(resources, *other.material, packs.root(), otherCache, error));
			float otherWidth = 0; CHECK(fontTextWidth(otherCache.mesh, "AB", otherWidth, error) && otherWidth > 0);
			CHECK(otherCache.texture.width > 0 && otherCache.texture.height > 0);
		}
    // Native HUD r_text instances select glyph rendering independently of
    // material flags (FUN_0048c670); cars.rpk fonts carry flags 4.
    uint32_t hudFontId = 0;
    CHECK(packs.resolveLiteral("cars.rpk", 0x58, hudFontId, error));
    auto hudFont = sceneLoadMaterial(resources, hudFontId);
    CHECK(hudFont.material && hudFont.flags == 4);
    SceneMaterialCache hudCache;
    CHECK(sceneRefreshMaterial(resources, *hudFont.material, packs.root(),
                               hudCache, error, false, true));
    std::vector<RenderVertex> glyphVertices;
    std::vector<uint32_t> glyphIndices;
    CHECK(fontGlyphGeometry(hudCache.mesh, "abcdef", 0, 0, 2, 1,
                            glyphVertices, glyphIndices, error));
    CHECK(glyphVertices.size() == 24 && glyphIndices.size() == 36);
    const auto hudFontNode = resources.lookup(hudFontId, 14);
    CHECK(resources.bindSceneMaterial(hudFontNode.node,
                                      std::shared_ptr<SceneMaterialResources>(std::move(hudFont.material))));
    auto* hudNode = world.addTypedChild(*world.defaultParent(), 3, "r_text", 0x200);
    CHECK(hudNode);
    world.markLoaded(*hudNode);
    auto hudInstance = std::make_shared<SceneRenderInstance>(hudFontNode.node.owner);
    hudInstance->setBoneTranslation(-0.8f, 0, 0);
    hudInstance->setTextState({"abcdef", 2, 0xff4fe015, 1});
    CHECK(resources.bindSceneRenderInstance(resources.resolve(hudNode), hudInstance));
    auto* hudSurface = SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_RGBA32);
    CHECK(hudSurface);
    {
      auto hudOutput = Render2D::forSurface(hudSurface, error);
      CHECK(hudOutput);
      auto hudDraw = SceneMaterialRenderer::create(resources, *hudOutput, *hudNode, error);
      CHECK(hudDraw);
      const std::array<float, 16> unit{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
      const ViewportInstance hudViewport{0, 0, 0, 1, 1, ViewportList::Active, {}};
      CHECK(hudOutput->clear(0, 0, 0, 255, error));
      CHECK(hudDraw->draw(unit, unit, hudViewport, error));
      CHECK(hudOutput->present(error));
      Uint8 red, green, blue, alpha;
      CHECK(SDL_ReadSurfacePixel(hudSurface, 65, 246, &red, &green, &blue, &alpha));
      CHECK(red == 79 && green == 224 && blue == 21 && alpha == 255);
      hudInstance->setInstanceColor(0xff808080);
      CHECK(hudOutput->clear(0, 0, 0, 255, error));
      CHECK(hudDraw->draw(unit, unit, hudViewport, error));
      CHECK(hudOutput->present(error));
      CHECK(SDL_ReadSurfacePixel(hudSurface, 65, 246, &red, &green, &blue, &alpha));
      CHECK(red == 40 && green == 112 && blue == 11 && alpha == 255);
      // Production SCX/atlas geometry, including non-quad cluster and needle,
      // must take the same planar material path as their seat OSD camera.
      CarSeat seat;
      auto* hudParent = world.addTypedChild(*world.defaultParent(), 1, "hud parent", 0);
      CHECK(hudParent);
      world.markLoaded(*hudParent);
      setLinkOwner(seat.osd, hudParent);
      constexpr size_t slots[]{26, 28, 34, 35, 36, 37};
      auto projection = unit;
      projection[0] = projection[5] = projection[10] = 0.02f;
      projection[14] = 1.0f; // source HUD plane at z=-35.9 maps into D3D [0,1].
      for (size_t model = 0; model < 6; ++model) {
        const uint32_t material = (hudFontId & 0xffff0000u) |
            (model == 0 ? 0x55u : model == 1 ? 0x56u : 0x5fu + uint32_t(model));
        const std::array<float, 3> position = model == 1 ?
            std::array<float, 3>{1.43f, -4.36f, -3.58f} :
            std::array<float, 3>{3.24f, -2.4f, -3.59f};
        const bool created = carHudModel(resources, world, seat, model, material, position,
                                          model == 1 ? 0.48f : 0, true, error);
        if (!created) std::fprintf(stderr, "HUD model %zu: %s\n", model, error.c_str());
        CHECK(created);
        auto* node = world.nodeFor(seat.links[slots[model]].owner);
        CHECK(node);
        seat.hudModels[model]->setCameraPlane(false);
        CHECK(!SceneMaterialRenderer::create(resources, *hudOutput, *node, error));
        error.clear();
        seat.hudModels[model]->setCameraPlane(true);
        auto draw = SceneMaterialRenderer::create(resources, *hudOutput, *node, error);
        CHECK(draw);
        CHECK(draw->transparentDepth(unit, error));
        CHECK(hudOutput->clear(0, 0, 0, 255, error));
        CHECK(draw->draw(unit, projection, hudViewport, error));
        CHECK(hudOutput->present(error));
        bool colored = false;
        for (int y = 0; y < hudSurface->h && !colored; ++y)
          for (int x = 0; x < hudSurface->w && !colored; ++x) {
            CHECK(SDL_ReadSurfacePixel(hudSurface, x, y, &red, &green, &blue, &alpha));
            colored = red || green || blue;
          }
        CHECK(colored);
      }
    }
    SDL_DestroySurface(hudSurface);
    int32_t raw;
    std::memcpy(&raw, &id, sizeof raw);
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.util.resource.ResourceRef", "set", "(I)V", {raw}, font, &arena, &classes)));
		TufaClass stringClass; stringClass.name = "java.lang.String"; stringClass.hasClassHeader = true;
		classes.push_back(&stringClass);
		auto textObject = std::make_unique<VmObject>(); textObject->runtimeClass = &stringClass;
		auto* text = textObject.get(); arena.objects.push_back(std::move(textObject)); arena.strings.emplace_back("AB A", text);
		const auto result = invokeNative("java.render.Text", "getWidthPixels", "(Ljava.lang.String;Ljava.util.resource.ResourceRef;)F", {text, font}, nullptr, &arena, &classes);
		if (const auto* failure = std::get_if<std::string>(&result)) std::fprintf(stderr, "%s\n", failure->c_str());
		CHECK(std::holds_alternative<VmValue>(result));
		CHECK(std::get<float>(std::get<VmValue>(result)) == 34.0f);
		const auto fontNode = resources.lookup(id, 14);
		CHECK(fontNode.node.packNode && fontNode.node.packNode->loaded());
		const auto beforeState = fontNode.node.packNode->stateWord;
		const auto repeated = invokeNative("java.render.Text", "getWidthPixels", "(Ljava.lang.String;Ljava.util.resource.ResourceRef;)F", {text, font}, nullptr, &arena, &classes);
		CHECK(std::holds_alternative<VmValue>(repeated) && std::get<float>(std::get<VmValue>(repeated)) == 34.0f);
		CHECK(fontNode.node.packNode->stateWord == beforeState);
		const auto textFile = tufaLoadFile("ref/raw/system/Scripts/render/Text.class"); CHECK(textFile.ok);
		const TufaClass* textClass = nullptr;
		for (const auto& cls : textFile.file.classes) if (cls.name == "java.render.Text") textClass = &cls;
		CHECK(textClass);
		const std::vector<TufaNode>* getWidth = nullptr;
		for (const auto& list : textClass->methods) for (const auto& method : list)
			if (textClass->constants[method.words[1]].text == "getWidth" && method.words[3] < textClass->trees.size()) getWidth = &textClass->trees[method.words[3]];
		CHECK(getWidth && getWidth->size() > 6 && (*getWidth)[6].op == 0x1a);
		std::vector<TufaNode> slice(getWidth->begin(), getWidth->begin() + 8);
		slice.push_back({0x2b, 0, false, 0});
		classes.push_back(textClass);
		std::vector<VmStaticFields> states{{textClass, {}}};
		const auto resultFromScript = vmExecute(*textClass, slice, {static_cast<const VmObject*>(nullptr), font, text}, &nativeRegistry(), &classes, &states, &arena);
		if (!resultFromScript.ok) std::fprintf(stderr, "font script: %s\n", resultFromScript.error.c_str());
		CHECK(resultFromScript.ok && std::get<float>(resultFromScript.value) == 34.0f);
		CHECK(!world.recentlyUsed(14).empty() && !world.recentlyUsed(5).empty());
		const uint32_t fontFrame = world.frameCounter();
		world.setFrameCounter(fontFrame + 8);
		CHECK(resources.ageRecentUses(error));
		CHECK(!world.recentlyUsed(14).empty() && !world.recentlyUsed(5).empty());
		world.setFrameCounter(fontFrame + 9);
		CHECK(resources.ageRecentUses(error));
		CHECK(world.recentlyUsed(14).empty() && world.recentlyUsed(5).empty());
		CHECK(!world.loadList(14, true).empty() && !world.loadList(5, true).empty());
		world.setFrameCounter(fontFrame);
		auto loadedFont = sceneLoadMaterial(resources, id);
		CHECK(loadedFont.material && loadedFont.flags == 4100);
		auto material = std::shared_ptr<SceneMaterialResources>(std::move(loadedFont.material));
		CHECK(resources.bindSceneMaterial(fontNode.node, material));
		auto parentRef = std::make_unique<VmObject>(); parentRef->runtimeClass = &refClass;
		parentRef->fields.push_back({"ptr", int32_t{0}, "I"});
		auto* parent = parentRef.get(); arena.objects.push_back(std::move(parentRef));
		auto renderRef = std::make_unique<VmObject>(); renderRef->runtimeClass = &refClass;
		renderRef->fields.push_back({"ptr", int32_t{0}, "I"});
		auto* render = renderRef.get(); arena.objects.push_back(std::move(renderRef));
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.util.resource.ResourceRef", "newNative", "()V", {}, parent, &arena, &classes)));
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.util.resource.ResourceRef", "newNative", "()V", {}, render, &arena, &classes)));
		auto* parentNode = world.addChild(*world.defaultParent(), "text-parent", 0); CHECK(parentNode);
		world.markLoaded(*parentNode);
		int32_t parentId; const auto parentRaw = parentNode->value;
		std::memcpy(&parentId, &parentRaw, sizeof parentId);
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.util.resource.ResourceRef", "set", "(I)V", {parentId}, parent, &arena, &classes)));
		auto textReceiver = std::make_unique<VmObject>(); textReceiver->runtimeClass = textClass;
		textReceiver->fields.push_back({"renderinst", render, "Ljava.util.resource.RenderRef;"});
		textReceiver->fields.push_back({"text", text, "Ljava.lang.String;"});
		auto* textSelf = textReceiver.get(); arena.objects.push_back(std::move(textReceiver));
		game.textDefaultColor = -65536;
		const auto madeText = invokeNative("java.render.Text", "create", "(Ljava.util.resource.ResourceRef;Ljava.util.resource.ResourceRef;FF)V", {parent, font, -0.6875f, -0.6875f}, textSelf, &arena, &classes);
		if (const auto* failure = std::get_if<std::string>(&madeText)) std::fprintf(stderr, "%s\n", failure->c_str());
		CHECK(std::holds_alternative<std::monostate>(madeText));
		const auto resolvedText = resources.lookup(nativeResourceState(render, &arena)->words[2], 3);
		CHECK(resolvedText.status == LookupStatus::Found && resolvedText.node.worldNode);
		auto* node = resolvedText.node.worldNode;
		auto instance = resources.sceneRenderInstance(resolvedText.node); CHECK(instance);
		CHECK(instance->textState() && instance->textState()->color == 0xffff0000u);
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.Text", "changeColor", "(I)V", {int32_t{-1}}, textSelf, &arena, &classes)));
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.Text", "update", "()V", {}, textSelf, &arena, &classes)));
		auto textRenderer = SceneMaterialRenderer::create(resources, *renderer, *node, error);
		if (!textRenderer) std::fprintf(stderr, "text renderer: %s\n", error.c_str());
		CHECK(textRenderer);
		const std::array<float, 16> identity{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
		const ViewportInstance viewport{0, 0, 0, 1, 1, ViewportList::Active, {}};
		CHECK(renderer->clear(0, 0, 0, 255, error));
		CHECK(textRenderer->draw(identity, identity, viewport, error) && renderer->present(error));
		Uint8 tr, tg, tb, ta; CHECK(SDL_ReadSurfacePixel(surface, 13, 11, &tr, &tg, &tb, &ta));
		CHECK(tr > 0 && tg == tr && tb == tr && ta == 255);
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.Text", "changeAlign", "(I)V", {int32_t{0}}, textSelf, &arena, &classes)));
		CHECK(renderer->clear(0, 0, 0, 255, error));
		CHECK(textRenderer->draw(identity, identity, viewport, error) && renderer->present(error));
		CHECK(SDL_ReadSurfacePixel(surface, 13, 11, &tr, &tg, &tb, &ta) && tr == 0 && tg == 0 && tb == 0);
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.Text", "changeAlign", "(I)V", {int32_t{2}}, textSelf, &arena, &classes)));
		textSelf->fields[1].value = static_cast<const VmObject*>(nullptr);
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.Text", "update", "()V", {}, textSelf, &arena, &classes)));
		CHECK(renderer->clear(0, 0, 0, 255, error));
		CHECK(textRenderer->draw(identity, identity, viewport, error) && renderer->present(error));
		CHECK(SDL_ReadSurfacePixel(surface, 13, 11, &tr, &tg, &tb, &ta) && tr == 0 && tg == 0 && tb == 0);
		textSelf->fields[1].value = text;
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.Text", "update", "()V", {}, textSelf, &arena, &classes)));
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.Text", "changeColor", "(I)V", {int32_t{-65536}}, textSelf, &arena, &classes)));
		CHECK(renderer->clear(0, 0, 0, 255, error));
		CHECK(textRenderer->draw(identity, identity, viewport, error) && renderer->present(error));
		CHECK(SDL_ReadSurfacePixel(surface, 13, 11, &tr, &tg, &tb, &ta));
		CHECK(tr > 0 && tg == 0 && tb == 0 && ta == 255);
		const Uint8 opaqueRed = tr;
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.Text", "changeColor", "(I)V", {int32_t{-2130771968}}, textSelf, &arena, &classes)));
		CHECK(renderer->clear(0, 0, 0, 255, error));
		CHECK(textRenderer->draw(identity, identity, viewport, error) && renderer->present(error));
		CHECK(SDL_ReadSurfacePixel(surface, 13, 11, &tr, &tg, &tb, &ta));
		CHECK(tr > 0 && tr < opaqueRed && tg == 0 && tb == 0 && ta == 255);
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.Text", "changeColor", "(I)V", {int32_t{-65536}}, textSelf, &arena, &classes)));
		auto coloredText = std::make_unique<VmObject>(); coloredText->runtimeClass = &stringClass;
		auto* colored = coloredText.get(); arena.objects.push_back(std::move(coloredText));
		arena.strings.emplace_back("A\x1b" "Cff00ff00B", colored);
		const std::vector<TufaNode>* changeText = nullptr;
		for (const auto& list : textClass->methods) for (const auto& method : list)
			if (textClass->constants[method.words[1]].text == "changeText" && method.words[3] < textClass->trees.size()) changeText = &textClass->trees[method.words[3]];
		CHECK(changeText);
		const auto changeResult = vmExecute(*textClass, *changeText, {textSelf, colored}, &nativeRegistry(), &classes, &states, &arena);
		if (!changeResult.ok) std::fprintf(stderr, "text script: %s\n", changeResult.error.c_str());
		CHECK(changeResult.ok);
		CHECK(renderer->clear(0, 0, 0, 255, error));
		CHECK(textRenderer->draw(identity, identity, viewport, error) && renderer->present(error));
		CHECK(SDL_ReadSurfacePixel(surface, 13, 11, &tr, &tg, &tb, &ta) && tr > 0 && tg == 0 && tb == 0);
		bool foundGreen = false;
		for (int y = 5; y < 25; ++y) for (int x = 18; x < 28; ++x) {
			CHECK(SDL_ReadSurfacePixel(surface, x, y, &tr, &tg, &tb, &ta));
			foundGreen |= tg > 0 && tr == 0 && tb == 0;
		}
		CHECK(foundGreen);
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.Text", "setPos", "(FF)V", {0.0f, -0.6875f}, textSelf, &arena, &classes)));
		CHECK(renderer->clear(0, 0, 0, 255, error));
		CHECK(textRenderer->draw(identity, identity, viewport, error) && renderer->present(error));
		CHECK(SDL_ReadSurfacePixel(surface, 13, 11, &tr, &tg, &tb, &ta) && tr == 0 && tg == 0 && tb == 0);
		CHECK(SDL_ReadSurfacePixel(surface, 35, 11, &tr, &tg, &tb, &ta) && tr > 0 && tg == 0 && tb == 0);
		std::vector<RenderVertex> vertices;
		std::vector<uint32_t> indices;
		CHECK(fontGlyphGeometry(parsed.model, "AB", 10.0f, 10.0f, 2, 1.0f, vertices, indices, error));
		CHECK(vertices.size() == 8 && indices.size() == 12 && vertices[0].x == 7.75f && vertices[4].x == 17.75f);
		auto atlas = VideoDecoder::open("ref/raw/frontend/textures/Fonts/SLII11.png", error); CHECK(atlas);
		VideoFrame image; CHECK(atlas->next(image, error) == VideoRead::Frame && image.width == 256 && image.height == 128);
		auto texture = renderer->texture(image.width, image.height, image.rgba.data(), image.rgba.size(), error); CHECK(texture);
		CHECK(renderer->clear(0, 0, 0, 255, error));
		CHECK(renderer->drawMesh(*texture, vertices, indices, error) && renderer->present(error));
		Uint8 r, g, b, a; CHECK(SDL_ReadSurfacePixel(surface, 13, 11, &r, &g, &b, &a));
		CHECK(r > 0 && g == r && b == r && a == 255);
		CHECK(renderer->releaseTexture(*texture, error));
		CHECK(fontGlyphGeometry(parsed.model, "A\x1b" "Cff00ff00B\x1b" "cA", 10, 10, 2, 1, vertices, indices, error, 0xffff0000u));
		CHECK(vertices.size() == 12 && vertices[0].color == 0xffff0000u && vertices[4].color == 0xff00ff00u && vertices[8].color == 0xffff0000u);
		CHECK(!fontGlyphGeometry(parsed.model, "A\x1b" "Cshort", 10, 10, 2, 1, vertices, indices, error));
	}
	renderer.reset(); SDL_DestroySurface(surface); SDL_Quit();
}
