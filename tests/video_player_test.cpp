#include "video_player.hpp"
#include "natives/registry.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
int main() {
	CHECK(SDL_Init(0));
	auto* surface = SDL_CreateSurface(320, 240, SDL_PIXELFORMAT_RGBA32);
	CHECK(surface);
	std::string error;
	auto renderer = Render2D::forSurface(surface, error);
	CHECK(renderer);
	{
		VideoPlayer player(*renderer, "ref/raw");
		VmGameState game; game.video = &player;
		VmArena arena; arena.gameState = &game; arena.performanceMilliseconds = [] { return 10000.0; };
		VmObject path; arena.strings.emplace_back("data\\fmv\\prime.avi", &path);
		const auto opened = invokeNative("java.render.GfxEngine", "openVideo", "(Ljava.lang.String;II)I",
		                                 {&path, int32_t{1}, int32_t{1}}, nullptr, &arena);
		CHECK(std::holds_alternative<VmValue>(opened) && std::get<int32_t>(std::get<VmValue>(opened)) == 0);
		const auto active = invokeNative("java.render.GfxEngine", "isPlayingVideo", "()I", {}, nullptr, &arena);
		CHECK(std::holds_alternative<VmValue>(active) && std::get<int32_t>(std::get<VmValue>(active)) == 1);
		CHECK(player.playing() && renderer->clear(0, 0, 0, 255, error));
		CHECK(player.draw(10.0, error) && renderer->present(error));
		Uint8 r, g, b, a;
		CHECK(SDL_ReadSurfacePixel(surface, 160, 120, &r, &g, &b, &a));
		CHECK(r == 17 && g == 0 && b == 0 && a == 255);
		CHECK(player.draw(15.0, error) && renderer->present(error));
		CHECK(SDL_ReadSurfacePixel(surface, 160, 120, &r, &g, &b, &a));
		CHECK(r == 117 && g == 104 && b == 99 && a == 255);
		CHECK(player.draw(10.0 + 34.75, error));
		CHECK(player.playing());
		CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.GfxEngine", "closeVideo", "()V", {}, nullptr, &arena)));
		CHECK(!player.playing());
		CHECK(!player.open("../unsafe.avi", false, 0.0, error));
		std::filesystem::path stock = ".scratch/2005-extract/Program Files/Street Legal Racing - Redline";
		if (!std::filesystem::exists(stock / "sl/Scripts/game/MainMenu.class")) stock = "ref/raw";
		const auto menuFile = tufaLoadFile((stock / "sl/Scripts/game/MainMenu.class").string());
		const auto gfxFile = tufaLoadFile((stock / "system/Scripts/render/GfxEngine.class").string());
		const auto stringFile = tufaLoadFile((stock / "system/Scripts/lang/String.class").string());
		CHECK(menuFile.ok && gfxFile.ok && stringFile.ok);
		const TufaClass* menu = nullptr;
		for (const auto& cls : menuFile.file.classes) if (cls.name == "java.game.MainMenuDialog") menu = &cls;
		CHECK(menu);
		const std::vector<TufaNode>* show = nullptr;
		for (const auto& list : menu->methods) for (const auto& method : list)
			if (menu->constants[method.words[1]].text == "show" && menu->constants[method.words[2]].text == "()V" && method.words[3] < menu->trees.size()) show = &menu->trees[method.words[3]];
		CHECK(show);
		size_t call = show->size();
		for (size_t i = 10; i < show->size(); ++i) {
			const auto& node = (*show)[i];
			if (node.op != 0x1a || node.operand >= menu->constants.size()) continue;
			const auto& ref = menu->constants[node.operand];
			if (ref.b >= menu->constants.size()) continue;
			const auto& pair = menu->constants[ref.b];
			if (pair.a < menu->constants.size() && menu->constants[pair.a].text == "openVideo") { call = i; break; }
		}
		CHECK(call < show->size());
		std::vector<TufaNode> slice(show->begin() + call - 10, show->begin() + call + 1);
		slice.push_back({0x2a, 0, false, 0});
		std::vector<const TufaClass*> classes{menu, &gfxFile.file.classes[0], &stringFile.file.classes[0]};
		std::vector<VmStaticFields> states{{&gfxFile.file.classes[0], {}}, {&stringFile.file.classes[0], {}}};
		const auto execution = vmExecute(*menu, slice, {}, &nativeRegistry(), &classes, &states, &arena);
		if (!execution.ok) std::fprintf(stderr, "stock menu video slice: %s\n", execution.error.c_str());
		CHECK(execution.ok && player.playing());
		player.close();
	}
	renderer.reset(); SDL_DestroySurface(surface); SDL_Quit();
}
