#include "native_lang_support.hpp"
#include "natives/lang_system.hpp"
#include "timers.hpp"
#include "natives/game_type.hpp"
#include "resource_world.hpp"
#include "natives/random.hpp"
#include "natives/video.hpp"
#include <bit>
#include <cstdlib>

int main() {
	const auto stringClass = loadStockClass("String");
	if (!stringClass) return 1;
	VmArena arena;
	const VmObject* word = addString(arena, *stringClass, "x");
	const auto array = [&](std::vector<VmValue> elements, const char* descriptor = "[I") {
		auto object = std::make_unique<VmObject>();
		object->arrayDescriptor = descriptor;
		object->arrayElements = std::move(elements);
		const VmObject* result = object.get();
		arena.objects.push_back(std::move(object));
		return result;
	};
	const auto ints = [](const VmObject* a) {
		std::vector<int32_t> out;
		for (const auto& v : a->arrayElements) out.push_back(std::get<int32_t>(v));
		return out;
	};
	const auto copy = [&](const VmObject* s, int32_t sp, const VmObject* d, int32_t dp, int32_t n) {
		return systemArraycopy({s, sp, d, dp, n}, nullptr, &arena, nullptr);
	};
	const auto src = array({int32_t{1}, int32_t{2}, int32_t{3}, int32_t{4}});
	const auto dst = array({int32_t{9}, int32_t{9}, int32_t{9}, int32_t{9}});
	CHECK(std::holds_alternative<std::monostate>(copy(src, 1, dst, 0, 3)));
	CHECK((ints(dst) == std::vector<int32_t>{2, 3, 4, 9}));
	CHECK(std::holds_alternative<std::monostate>(copy(src, 0, dst, 0, 0)));
	CHECK((ints(dst) == std::vector<int32_t>{2, 3, 4, 9}));
	const auto same = array({int32_t{1}, int32_t{2}, int32_t{3}, int32_t{4}, int32_t{5}});
	CHECK(std::holds_alternative<std::monostate>(copy(same, 0, same, 1, 4)));
	CHECK((ints(same) == std::vector<int32_t>{1, 1, 2, 3, 4}));
	CHECK(std::holds_alternative<std::monostate>(copy(same, 1, same, 0, 4)));
	CHECK((ints(same) == std::vector<int32_t>{1, 2, 3, 4, 4}));
	CHECK(isError(copy(src, 2, dst, 0, 3)));
	CHECK(isError(copy(src, 0, dst, 2, 3)));
	CHECK(isError(copy(src, -1, dst, 0, 1)));
	CHECK(isError(copy(src, 0, dst, -1, 1)));
	CHECK(isError(copy(src, 0, dst, 0, -1)));
	CHECK(isError(copy(src, 2147483647, dst, 0, 2)));
	CHECK(isError(copy(nullptr, 0, dst, 0, 1)));
	CHECK(isError(copy(src, 0, nullptr, 0, 1)));
	CHECK(isError(copy(word, 0, dst, 0, 1)));
	CHECK(isError(copy(src, 0, array({float{1.0f}}, "[F"), 0, 1)));
	CHECK(isError(copy(array({}), 0, dst, 0, 0)));
	CHECK(isError(systemArraycopy({src, int32_t{0}, dst, int32_t{0}}, nullptr, &arena, nullptr)));
	const auto refs = array({word, static_cast<const VmObject*>(nullptr)}, "[Ljava.lang.String;");
	const auto refsOut = array({static_cast<const VmObject*>(nullptr), static_cast<const VmObject*>(nullptr)}, "[Ljava.lang.String;");
	CHECK(std::holds_alternative<std::monostate>(copy(refs, 0, refsOut, 0, 2)));
	CHECK(std::get<const VmObject*>(refsOut->arrayElements[0]) == word);
	CHECK(std::get<const VmObject*>(refsOut->arrayElements[1]) == nullptr);

	CHECK(!arena.exitRequested);
	CHECK(isError(systemExitNoArgs({int32_t{1}}, nullptr, &arena, nullptr)));
	CHECK(!arena.exitRequested);
	CHECK(isError(systemExitNoArgs({}, nullptr, nullptr, nullptr)));
	CHECK(std::holds_alternative<std::monostate>(systemExitNoArgs({}, nullptr, &arena, nullptr)));
	CHECK(arena.exitRequested);
	CHECK(std::holds_alternative<std::monostate>(systemExitNoArgs({}, nullptr, &arena, nullptr)));
	VmArena independentArena;
	CHECK(!independentArena.exitRequested);
	CHECK(std::holds_alternative<std::monostate>(systemExitString({word}, nullptr, &independentArena, nullptr)));
	CHECK(!independentArena.exitRequested);
	CHECK(std::holds_alternative<std::monostate>(systemExitString({word}, nullptr, &arena, nullptr)));
	CHECK(std::holds_alternative<std::monostate>(systemExitString({static_cast<const VmObject*>(nullptr)}, nullptr, &arena, nullptr)));
	CHECK(isError(systemExitString({}, nullptr, &arena, nullptr)));
	CHECK(isError(systemExitString({int32_t{1}}, nullptr, &arena, nullptr)));
	CHECK(std::holds_alternative<std::monostate>(systemLog({word}, nullptr, &arena, nullptr)));
	CHECK(std::holds_alternative<std::monostate>(systemLog({static_cast<const VmObject*>(nullptr)}, nullptr, &arena, nullptr)));
	CHECK(isError(systemLog({}, nullptr, &arena, nullptr)));
	CHECK(isError(systemLog({1.0f}, nullptr, &arena, nullptr)));
	{
		PackRegistry packs("ref/raw");
		GameWorld world;
		CHECK(world.addRoot());
		ResourceWorld resources(world, packs);
		VmGameState game{&packs, &world, {}, {}, &resources};
		arena.gameState = &game;
		const auto missing = addString(arena, *stringClass, "missing-openlib-test.rpk");
		const auto open = [&](const VmObject* name) {
			return systemOpenLib({name}, nullptr, &arena, nullptr);
		};
		const auto first = open(missing);
		const auto second = open(missing);
		CHECK(std::get<int32_t>(std::get<VmValue>(first)) == -1);
		CHECK(std::get<int32_t>(std::get<VmValue>(second)) == -1);
		CHECK(packs.slotCount() == 1);
		const auto system = addString(arena, *stringClass, "system.rpk");
		const auto opened = open(system);
		CHECK(std::holds_alternative<VmValue>(opened));
		if (const auto* value = std::get_if<VmValue>(&opened))
			CHECK(std::get<int32_t>(*value) == 1);
		const auto reopened = open(system);
		CHECK(std::holds_alternative<VmValue>(reopened));
		if (const auto* value = std::get_if<VmValue>(&reopened))
			CHECK(std::get<int32_t>(*value) == 1);
		CHECK(isError(open(nullptr)));
	}
	VmArena clockArena;
  clockArena.performanceMilliseconds = [] { return 1250.0; };
  VmGameState clockGame; clockArena.gameState = &clockGame;
  CHECK(std::holds_alternative<std::monostate>(systemSyncGameTime({86400.0f + 3600.0f}, nullptr, &clockArena, nullptr)));
  CHECK(clockGame.gameTimeOfDay == 3600 && !clockGame.gameNight);
  CHECK(std::holds_alternative<std::monostate>(systemSyncGameTime({18000.0f}, nullptr, &clockArena, nullptr)) && clockGame.gameNight);
  CHECK(std::holds_alternative<std::monostate>(systemSyncGameTime({25200.0f}, nullptr, &clockArena, nullptr)) && !clockGame.gameNight);
  CHECK(std::holds_alternative<std::monostate>(systemSyncGameTime({72000.0f}, nullptr, &clockArena, nullptr)) && clockGame.gameNight);
  auto currentTime = systemCurrentTime({}, nullptr, &clockArena, nullptr);
  CHECK(std::holds_alternative<VmValue>(currentTime));
  if (const auto* value = std::get_if<VmValue>(&currentTime)) CHECK(std::abs(std::get<float>(*value) - 1.25f) < 0.00001f);
  clockArena.performanceMilliseconds = {};
  CHECK(std::holds_alternative<std::string>(systemCurrentTime({}, nullptr, &clockArena, nullptr)));
  {
    VmArena storedClock;
    unsigned liveReads = 0;
    storedClock.performanceMilliseconds = [&] { ++liveReads; return 1250.0; };
    storedClock.storedCurrentTimeBits = []() -> VmCurrentTimeBitsResult { return uint32_t{0x80000000}; };
    const auto stored = systemCurrentTime({}, nullptr, &storedClock, nullptr);
    const auto* value = std::get_if<VmValue>(&stored);
    CHECK(value && std::holds_alternative<float>(*value) &&
          std::bit_cast<uint32_t>(std::get<float>(*value)) == 0x80000000);
    CHECK(liveReads == 0);
    for (const uint32_t bits : {0x00000000u, 0x00000001u, 0x007fffffu, 0x3f800000u,
                                0x7f7fffffu, 0xbf800000u, 0xff7fffffu}) {
      storedClock.storedCurrentTimeBits = [bits]() -> VmCurrentTimeBitsResult { return bits; };
      const auto exact = systemCurrentTime({}, nullptr, &storedClock, nullptr);
      const auto* exactValue = std::get_if<VmValue>(&exact);
      CHECK(exactValue && std::holds_alternative<float>(*exactValue) &&
            std::bit_cast<uint32_t>(std::get<float>(*exactValue)) == bits);
    }
    for (const uint32_t bits : {0x7f800000u, 0xff800000u, 0x7fc00000u, 0x7f800001u}) {
      storedClock.storedCurrentTimeBits = [bits]() -> VmCurrentTimeBitsResult { return bits; };
      CHECK(isError(systemCurrentTime({}, nullptr, &storedClock, nullptr)));
    }
    storedClock.storedCurrentTimeBits = []() -> VmCurrentTimeBitsResult { return std::string{"fixture error"}; };
    const auto providerError = systemCurrentTime({}, nullptr, &storedClock, nullptr);
    CHECK(isError(providerError) && std::get<std::string>(providerError).find("fixture error") != std::string::npos);
    storedClock.storedCurrentTimeBits = []() -> VmCurrentTimeBitsResult { return std::monostate{}; };
    const auto exhausted = systemCurrentTime({}, nullptr, &storedClock, nullptr);
    CHECK(isError(exhausted));
    CHECK(liveReads == 0);

    // Installed stored Native input must not become a shared performance-clock
    // operand for randomize, timeWarp or video, even after its exhaustion.
    storedClock.performanceMilliseconds = [&] { ++liveReads; return 2000.0; };
    VmRandomState random;
    storedClock.randomState = &random;
    unsetenv("SLRR_FORCE_SEED");
    CHECK(std::holds_alternative<std::monostate>(mathRandomize({}, nullptr, &storedClock, nullptr)));
    CHECK(random.state == 2000u);
    TimerState separateSimulation;
    storedClock.timers = &separateSimulation;
    CHECK(std::holds_alternative<VmValue>(systemTimeWarp({0.5f}, nullptr, &storedClock, nullptr)));
    CHECK(separateSimulation.timeOrigin() == 2.0f && separateSimulation.timeScale() == 0.5f);
    struct VideoClock final : VmVideoControl {
      double started = -1;
      bool open(const std::string&, bool, double now, std::string&) override { started = now; return true; }
      void close() override {}
      bool playing() const override { return true; }
    } video;
    VmGameState videoGame;
    videoGame.video = &video;
    storedClock.gameState = &videoGame;
    const auto videoPath = addString(storedClock, *stringClass, "clock.avi");
    const auto openedVideo = gfxOpenVideo({videoPath, int32_t{1}, int32_t{0}}, nullptr, &storedClock, nullptr);
    CHECK(std::holds_alternative<VmValue>(openedVideo) && std::get<int32_t>(std::get<VmValue>(openedVideo)) == 0);
    CHECK(video.started == 2.0);
    CHECK(liveReads == 3);
    storedClock.performanceMilliseconds = {};
    storedClock.storedCurrentTimeBits = []() -> VmCurrentTimeBitsResult { return uint32_t{0x00000001}; };
    const auto withoutLive = systemCurrentTime({}, nullptr, &storedClock, nullptr);
    const auto* onlyStored = std::get_if<VmValue>(&withoutLive);
    CHECK(onlyStored && std::holds_alternative<float>(*onlyStored) &&
          std::bit_cast<uint32_t>(std::get<float>(*onlyStored)) == 0x00000001);
  }
  TimerState simulation; clockArena.timers = &simulation;
  auto oldScale = systemTimeWarp({-1.0f}, nullptr, &clockArena, nullptr);
  CHECK(std::holds_alternative<VmValue>(oldScale) && std::get<float>(std::get<VmValue>(oldScale)) == 1.0f);
  clockArena.performanceMilliseconds = [] { return 2000.0; };
  oldScale = systemTimeWarp({0.5f}, nullptr, &clockArena, nullptr);
  CHECK(std::holds_alternative<VmValue>(oldScale) && std::get<float>(std::get<VmValue>(oldScale)) == 1.0f);
  CHECK(simulation.timeScale() == 0.5f && std::abs(simulation.timeOrigin() - 2.0f) < 0.00001f);
  std::string timerError;
  CHECK(simulation.advance(0.5f, [](LinkOwner &, int32_t, uint32_t, int32_t, std::string &) { return true; }, timerError));
  oldScale = systemTimeWarp({2.0f}, nullptr, &clockArena, nullptr);
  CHECK(std::holds_alternative<VmValue>(oldScale) && std::get<float>(std::get<VmValue>(oldScale)) == 0.5f);
  CHECK(simulation.timeScale() == 2.0f && std::abs(simulation.timeOrigin() - 1.75f) < 0.00001f);
  return failures ? 1 : 0;
}
