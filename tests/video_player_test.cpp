#include "video_player.hpp"
#include "audio.hpp"
#include "natives/registry.hpp"
#include "run_log.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#define CHECK(x)                                           \
  do {                                                     \
    if (!(x)) {                                            \
      std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); \
      std::abort();                                        \
    }                                                      \
  } while (0)
int main() {
  runlog::Hub hub;
  uint64_t written = 0;
  size_t closed = 0;
  runlog::g_hub.store(&hub);
  CHECK(SDL_Init(0));
  CHECK(SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1) == 0);
  auto* surface = SDL_CreateSurface(320, 240, SDL_PIXELFORMAT_RGBA32);
  CHECK(surface);
  std::string error;
  auto renderer = Render2D::forSurface(surface, error);
  CHECK(renderer);
  {
    VideoPlayer player(*renderer, "ref/raw");
    VmGameState game;
    game.video = &player;
    VmArena arena;
    arena.gameState = &game;
    arena.performanceMilliseconds = [] { return 10000.0; };
    VmObject path;
    arena.strings.emplace_back("data\\fmv\\prime.avi", &path);
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
    size_t notes = 0;
    for (const auto& event : hub.snapshot(runlog::EventRingT<4096>::kCapacity, written))
      if (event.kind == static_cast<uint8_t>(runlog::EventKind::Note) &&
          (std::string_view{event.text}.rfind("video ", 0) == 0))
        ++notes;
    CHECK(notes >= 2); // open + loop wrap
    CHECK(std::holds_alternative<std::monostate>(invokeNative("java.render.GfxEngine", "closeVideo", "()V", {}, nullptr, &arena)));
    CHECK(!player.playing());
    for (const auto& event : hub.snapshot(runlog::EventRingT<4096>::kCapacity, written)) {
      if (event.kind != static_cast<uint8_t>(runlog::EventKind::Note)) continue;
      const std::string_view text{event.text};
      if (text.rfind("video open ", 0) == 0)
        CHECK(text.find("data\\fmv\\prime.avi loop=1") != std::string_view::npos);
      if (text.rfind("video close", 0) == 0) ++closed;
    }
    CHECK(closed == 1);
    CHECK(!player.open("../unsafe.avi", false, 0.0, error));
    std::filesystem::path stock = ".scratch/2005-extract/Program Files/Street Legal Racing - Redline";
    if (!std::filesystem::exists(stock / "sl/Scripts/game/MainMenu.class")) stock = "ref/raw";
    const auto menuFile = tufaLoadFile((stock / "sl/Scripts/game/MainMenu.class").string());
    const auto gfxFile = tufaLoadFile((stock / "system/Scripts/render/GfxEngine.class").string());
    const auto stringFile = tufaLoadFile((stock / "system/Scripts/lang/String.class").string());
    CHECK(menuFile.ok && gfxFile.ok && stringFile.ok);
    const TufaClass* menu = nullptr;
    for (const auto& cls : menuFile.file.classes)
      if (cls.name == "java.game.MainMenuDialog") menu = &cls;
    CHECK(menu);
    const std::vector<TufaNode>* show = nullptr;
    for (const auto& list : menu->methods)
      for (const auto& method : list)
        if (menu->constants[method.words[1]].text == "show" && menu->constants[method.words[2]].text == "()V" && method.words[3] < menu->trees.size()) show = &menu->trees[method.words[3]];
    CHECK(show);
    size_t call = show->size();
    for (size_t i = 10; i < show->size(); ++i) {
      const auto& node = (*show)[i];
      if (node.op != 0x1a || node.operand >= menu->constants.size()) continue;
      const auto& ref = menu->constants[node.operand];
      if (ref.b >= menu->constants.size()) continue;
      const auto& pair = menu->constants[ref.b];
      if (pair.a < menu->constants.size() && menu->constants[pair.a].text == "openVideo") {
        call = i;
        break;
      }
    }
    CHECK(call < show->size());
    CHECK(call >= 10);
    const auto callOffset = static_cast<std::vector<TufaNode>::difference_type>(call);
    std::vector<TufaNode> slice(show->begin() + callOffset - 10, show->begin() + callOffset + 1);
    slice.push_back({0x2a, 0, false, 0});
    std::vector<const TufaClass*> classes{menu, &gfxFile.file.classes[0], &stringFile.file.classes[0]};
    std::vector<VmStaticFields> states{{&gfxFile.file.classes[0], {}}, {&stringFile.file.classes[0], {}}};
    const auto execution = vmExecute(*menu, slice, {}, &nativeRegistry(), &classes, &states, &arena);
    if (!execution.ok) std::fprintf(stderr, "stock menu video slice: %s\n", execution.error.c_str());
    CHECK(execution.ok && player.playing());
    player.close();
  }
  {
    auto audio = AudioOutput::create(error);
    CHECK(audio);
    VideoPlayer player2(*renderer, "ref/raw", audio.get());
    CHECK(player2.open("data\\fmv\\Activision.avi", false, 0.0, error));
    CHECK(player2.draw(1.0, error));
    size_t queued = 0;
    CHECK(audio->queuedSamples(queued, error) && queued > 48000);
    // Natural non-loop end: stock's graph end-of-stream covers the audio
    // renderer, so the queued tail must survive close() and drain on device.
    CHECK(player2.draw(20.0, error));
    CHECK(!player2.playing());
    player2.close();
    CHECK(audio->queuedSamples(queued, error) && queued > 0);
    size_t tailNotes = 0;
    for (const auto& event : hub.snapshot(runlog::EventRingT<4096>::kCapacity, written))
      if (event.kind == static_cast<uint8_t>(runlog::EventKind::Note) &&
          (std::string_view{event.text}.rfind("video audio tail queued", 0) == 0))
        ++tailNotes;
    CHECK(tailNotes == 1);
    size_t audioNotes = 0;
    for (const auto& event : hub.snapshot(runlog::EventRingT<4096>::kCapacity, written))
      if (event.kind == static_cast<uint8_t>(runlog::EventKind::Note) &&
          std::string_view{event.text}.rfind("video audio open ", 0) == 0)
        ++audioNotes;
    CHECK(audioNotes == 1);
    // A new open() clears a stale tail.
    CHECK(player2.open("data\\fmv\\Activision.avi", false, 0.0, error));
    CHECK(audio->queuedSamples(queued, error) && queued == 0);
    // Backpressure: fill the device queue to capacity, then draw — a full
    // queue must skip audio, never fail the video path.
    CHECK(audio->queuedSamples(queued, error));
    if (queued < AudioOutput::maxQueuedSamples) {
      std::vector<float> filler(AudioOutput::maxQueuedSamples - queued, 0.0f);
      CHECK(audio->queue(filler.data(), filler.size(), error));
    }
    CHECK(audio->queuedSamples(queued, error) && queued == AudioOutput::maxQueuedSamples);
    CHECK(player2.draw(1.0, error));
    CHECK(player2.playing());
    // An explicit close mid-play still clears the device queue.
    player2.close();
    CHECK(audio->queuedSamples(queued, error) && queued == 0);
  }
  renderer.reset();
  SDL_DestroySurface(surface);
  SDL_Quit();
}
