// Scene.time2Config(F)I: the stock script maps actual game time of day to a
// sky/light config index. Stock bands (mirrored in
// ref/slrr-reverse-main/engine/Runtime/Resources/GameRef_part6.inc,
// scene_time2config): hour <3 or >=22 -> 0; [3,4) -> 1|2; [4,8) -> 3|4|5;
// [8,10) -> 6; [10,19) -> 7..10; [19,20.5) -> 11; [20.5,22) -> 12..14.
// This test executes the stock Scene.time2Config tree itself (tree 5) through
// the VM with a pinned Math.random state, asserting fixed bands exactly and
// band membership for the random ones.
#include "natives/random.hpp"
#include "tufa.hpp"
#include "vm.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(c)                                       \
  do {                                                 \
    if (!(c)) {                                        \
      std::printf("FAIL line %d: %s\n", __LINE__, #c); \
      ++failures;                                      \
    }                                                  \
  } while (0)

int main() {
  const auto scene = tufaLoadFile("ref/raw/sl/Scripts/game/Scene.class");
  const auto math = tufaLoadFile("ref/raw/system/Scripts/lang/Math.class");
  const auto gameType =
      tufaLoadFile("ref/raw/system/Scripts/lang/GameType.class");
  CHECK(scene.ok && !scene.file.classes.empty());
  CHECK(math.ok && !math.file.classes.empty());
  CHECK(gameType.ok && !gameType.file.classes.empty());
  if (!scene.ok || scene.file.classes.empty() || !math.ok ||
      math.file.classes.empty() || !gameType.ok || gameType.file.classes.empty())
    return 1;
  const TufaClass& sceneClass = scene.file.classes[0];
  const TufaClass& mathClass = math.file.classes[0];

  // time2Config is method tree 5 in Scene.class (verified with tufa_dump).
  bool found = false;
  size_t treeIndex = 0;
  for (const auto& list : sceneClass.methods)
    for (const auto& m : list)
      if (m.words[1] < sceneClass.constants.size() &&
          sceneClass.constants[m.words[1]].text == "time2Config" &&
          m.words[2] < sceneClass.constants.size() &&
          sceneClass.constants[m.words[2]].text == "(F)I") {
        found = true;
        treeIndex = m.words[3];
      }
  CHECK(found);
  if (!found)
    return 1;

  TufaClass objectClass;
  objectClass.name = "java.lang.Object";
  objectClass.hasClassHeader = true;
  objectClass.classHeader[3] = UINT32_MAX;
  std::vector<const TufaClass*> classes{&objectClass, &sceneClass, &mathClass};
  for (const auto& c : gameType.file.classes)
    classes.push_back(&c);
  std::vector<VmStaticFields> states;
  std::vector<NativeEntry> natives{
      {"java.lang.Math", "random", "()F", "test", mathRandom}};
  VmRandomState randomState;
  randomState.state = 12345u;
  VmArena arena;
  arena.randomState = &randomState;

  VmObject receiver{&sceneClass, {}};
  const auto configAt = [&](float seconds) -> int32_t {
    const auto result =
        vmExecute(sceneClass, sceneClass.trees[treeIndex],
                  {&receiver, seconds}, &natives, &classes, &states, &arena);
    if (!result.ok || !std::holds_alternative<int32_t>(result.value)) {
      if (!result.ok)
        std::printf("vm error: %s\n", result.error.c_str());
      return -1;
    }
    return std::get<int32_t>(result.value);
  };
  for (const auto& list : sceneClass.fields)
    for (const auto& f : list) {
      const auto& name = sceneClass.constants[f.words[1]].text;
      const auto& desc = sceneClass.constants[f.words[2]].text;
      VmValue v = std::monostate{};
      if (desc == "I" || desc == "Z" || desc == "B" || desc == "S" || desc == "C")
        v = int32_t{0};
      else if (desc == "F" || desc == "D")
        v = 0.0f;
      receiver.fields.emplace_back(name, v, desc);
    }

  const float hour = 3600.0f;
  // Fixed bands (no random draw consumed for the pick).
  CHECK(configAt(0.5f * hour) == 0);   // night, before 03:00
  CHECK(configAt(22.0f * hour) == 0);  // night, from 22:00
  CHECK(configAt(9.0f * hour) == 6);   // [08,10) -> 6
  CHECK(configAt(19.5f * hour) == 11); // [19,20.5) -> 11
  // Random bands: membership only.
  const int32_t early = configAt(3.5f * hour); // [3,4) -> 1|2
  CHECK(early == 1 || early == 2);
  const int32_t morning = configAt(5.0f * hour); // [4,8) -> 3|4|5
  CHECK(morning >= 3 && morning <= 5);
  const int32_t day = configAt(12.0f * hour); // [10,19) -> 7..10
  CHECK(day >= 7 && day <= 10);
  const int32_t evening = configAt(21.0f * hour); // [20.5,22) -> 12..14
  CHECK(evening >= 12 && evening <= 14);

  if (failures == 0)
    std::printf("scene_time: ok\n");
  return failures == 0 ? 0 : 1;
}
