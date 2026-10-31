#define _DARWIN_C_SOURCE
#include "file_state.hpp"
#include "natives/registry.hpp"
#include "vm.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unistd.h>
#include <variant>

static int failures = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);                      \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

static std::vector<VmValue> capturedNativeArgs;
static const VmObject *capturedNativeReceiver = nullptr;
static NativeResult captureCreate(const std::vector<VmValue> &args,
                                  const VmObject *receiver, VmArena *,
                                  const std::vector<const TufaClass *> *) {
  capturedNativeArgs = args;
  capturedNativeReceiver = receiver;
  return std::monostate{};
}
static NativeResult testSqrt(const std::vector<VmValue> &args,
                             const VmObject *receiver, VmArena *,
                             const std::vector<const TufaClass *> *) {
  if (receiver || args.size() != 1 || !std::get_if<float>(&args[0]) ||
      std::get<float>(args[0]) != 4.0f)
    return std::string{"unexpected test input"};
  return VmValue{2.0f};
}
static NativeResult testRandom(const std::vector<VmValue> &args,
                               const VmObject *receiver, VmArena *,
                               const std::vector<const TufaClass *> *) {
  if (receiver || !args.empty())
    return std::string{"unexpected test input"};
  return VmValue{0.25f};
}

static void checkLiteralMethod(const char *path, const char *name,
                               const char *descriptor, int32_t expected) {
  const auto parsed = tufaLoadFile(path);
  CHECK(parsed.ok);
  if (!parsed.ok)
    return;
  for (const auto &cls : parsed.file.classes) {
    for (const auto &list : cls.methods) {
      for (const auto &method : list) {
        if (method.words[1] >= cls.constants.size() ||
            method.words[2] >= cls.constants.size() ||
            method.words[3] >= cls.trees.size())
          continue;
        if (cls.constants[method.words[1]].text != name ||
            cls.constants[method.words[2]].text != descriptor)
          continue;
        const auto &tree = cls.trees[method.words[3]];
        VmObject receiver, argument;
        const VmValue input =
            descriptor[1] == 'I' ? VmValue{int32_t{42}} : VmValue{&argument};
        const auto result = vmExecute(tree, {&receiver, input});
        CHECK(result.ok && std::get_if<int32_t>(&result.value) &&
              std::get<int32_t>(result.value) == expected);
        auto bad = tree;
        bad[1].op = 0x0c;
        CHECK(!vmExecute(bad, {&receiver, input}).ok);
        return;
      }
    }
  }
  CHECK(false);
}

int main() {
  {
    const auto parsed = tufaLoadFile("ref/raw/parts/scripts/Part.class");
    CHECK(parsed.ok && !parsed.file.classes.empty());
    if (parsed.ok && !parsed.file.classes.empty()) {
      auto cls = parsed.file.classes[0];
      uint32_t name = UINT32_MAX, signature = UINT32_MAX;
      for (const auto &list : cls.methods)
        for (const auto &method : list)
          if (cls.constants[method.words[1]].text == "kmToMaxWear" &&
              cls.constants[method.words[2]].text == "(F)F") {
            name = method.words[1];
            signature = method.words[2];
          }
      CHECK(name != UINT32_MAX && signature != UINT32_MAX);
      if (name != UINT32_MAX && signature != UINT32_MAX) {
        const auto ownerName = static_cast<uint32_t>(cls.constants.size());
        cls.constants.push_back({0, cls.name, 0, 0});
        const auto owner = static_cast<uint32_t>(cls.constants.size());
        cls.constants.push_back({4, {}, ownerName, 0});
        const auto pair = static_cast<uint32_t>(cls.constants.size());
        cls.constants.push_back({7, {}, name, signature});
        const auto ref = static_cast<uint32_t>(cls.constants.size());
        cls.constants.push_back({5, {}, owner, pair});
        const std::vector<TufaNode> call{{0x01, 0, true, 1},
                                         {0x27, 0, true, 1},
                                         {0x24, 0, false, 0},
                                         {0x1a, 0, true, ref},
                                         {0x2b, 0, false, 0}};
        VmObject receiver{&cls, {}};
        const auto result = vmExecute(cls, call, {&receiver, 2.0f});
        CHECK(result.ok && std::get_if<float>(&result.value) &&
              std::get<float>(result.value) == 2000.0f);
        const auto promoted = vmExecute(cls, call, {&receiver, int32_t{2}});
        CHECK(promoted.ok && std::get_if<float>(&promoted.value) &&
              std::get<float>(promoted.value) == 2000.0f);
      }
    }
  }
  {
    const auto dummy =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/Dummy.class");
    const auto gameType =
        tufaLoadFile("ref/raw/system/Scripts/lang/GameType.class");
    const auto gameRef =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/GameRef.class");
    const auto string =
        tufaLoadFile("ref/raw/system/Scripts/lang/String.class");
    CHECK(dummy.ok && gameType.ok && gameRef.ok && string.ok);
    if (dummy.ok && gameType.ok && gameRef.ok && string.ok) {
      const auto &cls = dummy.file.classes[0];
      const auto &type = gameType.file.classes[0];
      const auto &ref = gameRef.file.classes[0];
      const auto &str = string.file.classes[0];
      std::vector<const TufaClass *> classes{&cls, &type, &ref, &str};
      std::vector<VmStaticFields> states{
          {&ref, {{"RID_DUMMY", int32_t{1234}, "I"}}}, {&str, {}}};
      VmArena arena;
      auto object = std::make_unique<VmObject>();
      object->runtimeClass = &cls;
      const auto *receiver = object.get();
      arena.objects.push_back(std::move(object));
      const std::vector<TufaNode> *tree = nullptr;
      for (const auto &list : cls.methods)
        for (const auto &method : list)
          if (cls.constants[method.words[1]].text == "<init>" &&
              cls.constants[method.words[2]].text ==
                  "(Ljava.util.resource.GameRef;)" &&
              method.words[3] < cls.trees.size())
            tree = &cls.trees[method.words[3]];
      CHECK(tree && tree->size() > 13);
      if (tree && tree->size() > 13) {
        std::vector<TufaNode> slice(tree->begin() + 3, tree->begin() + 14);
        const std::vector<NativeEntry> registry{
            {"java.lang.GameType", "createNativeInstance",
             "(Ljava.util.resource.GameRef;ILjava.lang.String;Ljava.lang."
             "String;)V",
             "test", captureCreate}};
        capturedNativeArgs.clear();
        capturedNativeReceiver = nullptr;
        const auto result = vmExecute(
            cls, slice, {receiver, static_cast<const VmObject *>(nullptr)},
            &registry, &classes, &states, &arena);
        CHECK(result.ok && result.voidReturn &&
              capturedNativeReceiver == receiver &&
              capturedNativeArgs.size() == 4);
        if (capturedNativeArgs.size() == 4) {
          CHECK(std::get<const VmObject *>(capturedNativeArgs[0]) == nullptr &&
                std::get<int32_t>(capturedNativeArgs[1]) == 1234 &&
                std::get<const VmObject *>(capturedNativeArgs[2]) == nullptr);
          CHECK(arena.strings.size() == 1 &&
                arena.strings[0].first == "dummy" &&
                std::get<const VmObject *>(capturedNativeArgs[3]) ==
                    arena.strings[0].second);
        }
        CHECK(!vmExecute(cls, slice, {receiver, int32_t{1}}, &registry,
                         &classes, &states, &arena)
                   .ok);
        CHECK(!vmExecute(cls, slice,
                         {receiver, static_cast<const VmObject *>(nullptr)},
                         nullptr, &classes, &states, &arena)
                   .ok);
      }
    }
  }
  {
    const auto menu = tufaLoadFile("ref/raw/sl/Scripts/game/MainMenu.class");
    CHECK(menu.ok);
    if (menu.ok)
      for (const auto &cls : menu.file.classes)
        if (cls.name == "java.game.MainMenuDialog") {
          const std::vector<TufaNode> *show = nullptr;
          for (const auto &list : cls.methods)
            for (const auto &method : list)
              if (cls.constants[method.words[1]].text == "show" &&
                  method.words[3] < cls.trees.size())
                show = &cls.trees[method.words[3]];
          CHECK(show && show->size() > 29);
          if (show && show->size() > 29) {
            const auto configFile =
                tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
            const auto stringFile =
                tufaLoadFile("ref/raw/system/Scripts/lang/String.class");
            const auto systemFile =
                tufaLoadFile("ref/raw/system/Scripts/lang/System.class");
            CHECK(configFile.ok && stringFile.ok && systemFile.ok &&
                  show->size() > 160);
            if (configFile.ok && stringFile.ok && systemFile.ok &&
                show->size() > 160) {
              auto config = configFile.file.classes[0];
              config.classHeader[3] = UINT32_MAX;
              for (auto &list : config.fields) {
                std::vector<TufaField> fields;
                for (const auto &field : list)
                  if ((field.words[0] & 8) &&
                      config.constants[field.words[1]].text == "version")
                    fields.push_back(field);
                list = std::move(fields);
              }
              const auto &stringClass = stringFile.file.classes[0];
              const auto &systemClass = systemFile.file.classes[0];
              std::vector<const TufaClass *> classes{
                  &cls, &config, &stringClass, &systemClass};
              std::vector<VmStaticFields> states{{&stringClass, {}}};
              VmArena arena;
              CHECK(vmInitializeClass(config, classes, states, nullptr, &arena)
                        .ok);
              std::vector<TufaNode> version(show->begin() + 145,
                                            show->begin() + 161);
              version.push_back({0x2b, 0, false, 0});
              const auto text = vmExecute(cls, version, {}, &nativeRegistry(),
                                          &classes, &states, &arena);
              CHECK(text.ok && std::get_if<const VmObject *>(&text.value));
              bool found = false;
              if (text.ok && std::get_if<const VmObject *>(&text.value))
                for (const auto &item : arena.strings)
                  if (item.second == std::get<const VmObject *>(text.value) &&
                      item.first == "v2.2.1 (build 606)")
                    found = true;
              CHECK(found);
              std::vector<TufaNode> bad{{0x07, 0, true, 28},
                                        {0x0c, 0, false, 0},
                                        {0x27, 0, true, 1},
                                        {0x07, 0, true, 16},
                                        {0x2b, 0, false, 0}};
              // Stock converts the null-reference-plus-int concatenation:
              // FUN_00417a70 formats the int side as "%d" and the null
              // reference contributes nothing, so the result interns as "1".
              const auto nullPlusInt =
                  vmExecute(cls, bad, {}, nullptr, &classes, &states, &arena);
              CHECK(nullPlusInt.ok &&
                    std::get_if<const VmObject *>(&nullPlusInt.value));
              bool internedOne = false;
              if (nullPlusInt.ok &&
                  std::get_if<const VmObject *>(&nullPlusInt.value))
                for (const auto &item : arena.strings)
                  if (item.second ==
                          std::get<const VmObject *>(nullPlusInt.value) &&
                      item.first == "1")
                    internedOne = true;
              CHECK(internedOne);
            }
            const auto track =
                tufaLoadFile("ref/raw/sl/Scripts/game/TrackSelect.class");
            CHECK(track.ok && !track.file.classes.empty());
            if (track.ok && !track.file.classes.empty()) {
              const auto &target = track.file.classes[0];
              std::vector<const TufaClass *> classes{&cls, &target};
              std::vector<TufaNode> check(show->begin() + 13,
                                          show->begin() + 19);
              check.push_back({0x2b, 0, false, 0});
              VmObject receiver{&cls, {}};
              const auto result =
                  vmExecute(cls, check, {&receiver, 4.0f}, nullptr, &classes);
              CHECK(!result.ok &&
                    result.error.find("java.game.TrackSelect: node 2: class "
                                      "not registered: java.lang.Math") !=
                        std::string::npos);
              const auto math =
                  tufaLoadFile("ref/raw/system/Scripts/lang/Math.class");
              CHECK(math.ok && !math.file.classes.empty());
              if (math.ok && !math.file.classes.empty()) {
                classes.push_back(&math.file.classes[0]);
                const std::vector<NativeEntry> registry{
                    {"java.lang.Math", "sqrt", "(F)F", "test", testSqrt}};
                const auto completed = vmExecute(cls, check, {&receiver, 4.0f},
                                                 &registry, &classes);
                CHECK(completed.ok && std::get_if<float>(&completed.value) &&
                      std::get<float>(completed.value) == 5.75f);
                const auto actual = vmExecute(cls, check, {&receiver, 4.0f},
                                              &nativeRegistry(), &classes);
                CHECK(actual.ok && std::get_if<float>(&actual.value) &&
                      std::get<float>(actual.value) == 5.75f);
                CHECK(!vmExecute(cls, check, {&receiver, 9.0f}, &registry,
                                 &classes)
                           .ok);
                CHECK(
                    !vmExecute(cls, check, {&receiver, 4.0f}, nullptr, &classes)
                         .ok);
                std::vector<TufaNode> random(show->begin() + 3,
                                             show->begin() + 12);
                random.push_back({0x2b, 0, false, 0});
                const std::vector<NativeEntry> randomRegistry{
                    {"java.lang.Math", "random", "()F", "test", testRandom}};
                const auto randomResult = vmExecute(cls, random, {&receiver},
                                                    &randomRegistry, &classes);
                CHECK(randomResult.ok &&
                      std::get_if<float>(&randomResult.value) &&
                      std::get<float>(randomResult.value) == 25.0f);
                VmRandomState randomState;
                VmArena randomArena;
                randomArena.randomState = &randomState;
                const auto seeded =
                    vmExecute(cls, random, {&receiver}, &nativeRegistry(),
                              &classes, nullptr, &randomArena);
                CHECK(seeded.ok && std::get_if<float>(&seeded.value) &&
                      randomState.state == 0x0029e2c0u);
                if (seeded.ok && std::get_if<float>(&seeded.value)) {
                  const float value = std::get<float>(seeded.value);
                  uint32_t bits;
                  std::memcpy(&bits, &value, sizeof(bits));
                  CHECK(bits == 0x3e002100u);
                }
                randomState.state = 1;
                std::vector<TufaNode> firstCheck(show->begin(),
                                                 show->begin() + 31);
                firstCheck.push_back({0x2b, 0, false, 0});
                const auto checked =
                    vmExecute(cls, firstCheck, {&receiver}, &nativeRegistry(),
                              &classes, nullptr, &randomArena);
                CHECK(checked.ok && std::get_if<int32_t>(&checked.value) &&
                      std::get<int32_t>(checked.value) == 0 &&
                      randomState.state == 0x0029e2c0u);
                randomState.state = 1;
                std::vector<TufaNode> branch(show->begin(), show->begin() + 37);
                branch.insert(branch.end(),
                              {{0x27, 0, true, 37}, {0x2b, 0, false, 0}});
                const auto skipped =
                    vmExecute(cls, branch, {&receiver}, &nativeRegistry(),
                              &classes, nullptr, &randomArena);
                CHECK(skipped.ok && std::get<int32_t>(skipped.value) == 37);
                const auto garage =
                    tufaLoadFile("ref/raw/sl/Scripts/game/Garage.class");
                const auto trackClass =
                    tufaLoadFile("ref/raw/sl/Scripts/game/Track.class");
                const auto logic =
                    tufaLoadFile("ref/raw/sl/Scripts/game/GameLogic.class");
                const auto sound =
                    tufaLoadFile("ref/raw/system/Scripts/sound/Sound.class");
                CHECK(garage.ok && trackClass.ok && logic.ok && sound.ok);
                if (garage.ok && trackClass.ok && logic.ok && sound.ok &&
                    show->size() > 124) {
                  const auto &logicClass = logic.file.classes[0];
                  const auto &soundClass = sound.file.classes[0];
                  classes.insert(classes.end(), {&garage.file.classes[0],
                                                 &trackClass.file.classes[0],
                                                 &logicClass, &soundClass});
                  std::vector<VmStaticFields> state{
                      {&logicClass, {{"klampiPatch", int32_t{1}, "I"}}},
                      {&soundClass, {{"MUSIC_SET_MENU", int32_t{3}, "I"}}}};
                  randomState.state = 1;
                  std::vector<TufaNode> prefix(show->begin(),
                                               show->begin() + 125);
                  const auto untilNative =
                      vmExecute(cls, prefix, {&receiver}, &nativeRegistry(),
                                &classes, &state, &randomArena);
                  CHECK(!untilNative.ok &&
                        untilNative.error ==
                            "invalid Sound.changeMusicSet call" &&
                        std::get<int32_t>(state[0].fields[0].value) == 0 &&
                        randomState.state == 0x18be873au);
                  struct RecordingMusic final : VmMusicControl {
                    int32_t set = -9;
                    float volume = -1;
                    void changeMusicSet(int32_t s, float v) override {
                      set = s;
                      volume = v;
                    }
                    void nextTrack() override {}
                    void prevTrack() override {}
                    int32_t setMode(int32_t m) override { return m; }
                    int32_t getMode() const override { return -1; }
                    void setMusicVolume(float) override {}
                  } recording;
                  VmSoundState menuSound;
                  menuSound.volumes[1] = 0.5f;
                  menuSound.music = &recording;
                  randomArena.soundState = &menuSound;
                  randomState.state = 1;
                  prefix.push_back({0x2a, 0, false, 0});
                  const auto reached =
                      vmExecute(cls, prefix, {&receiver}, &nativeRegistry(),
                                &classes, &state, &randomArena);
                  CHECK(reached.ok && recording.set == 3 &&
                        recording.volume == 0.5f);
                }
                CHECK(!vmExecute(cls, random, {&receiver}, &registry, &classes)
                           .ok);
              }
            }
            const auto vector =
                tufaLoadFile("ref/raw/system/Scripts/lang/Vector3.class");
            const auto object =
                tufaLoadFile("ref/raw/system/Scripts/lang/Object.class");
            CHECK(vector.ok && object.ok && !vector.file.classes.empty() &&
                  !object.file.classes.empty() && show->size() > 355);
            if (vector.ok && object.ok && !vector.file.classes.empty() &&
                !object.file.classes.empty() && show->size() > 355) {
              const auto &vectorClass = vector.file.classes[0];
              const auto &objectClass = object.file.classes[0];
              std::vector<const TufaClass *> classes{&cls, &vectorClass,
                                                     &objectClass};
              std::vector<VmStaticFields> states{{&vectorClass, {}},
                                                 {&objectClass, {}}};
              VmArena arena;
              std::vector<TufaNode> construct(show->begin() + 344,
                                              show->begin() + 356);
              construct.push_back({0x2b, 0, false, 0});
              const auto result = vmExecute(cls, construct, {}, nullptr,
                                            &classes, &states, &arena);
              CHECK(result.ok && std::get_if<const VmObject *>(&result.value));
              if (result.ok && std::get_if<const VmObject *>(&result.value)) {
                const auto *value = std::get<const VmObject *>(result.value);
                CHECK(value && value->fields.size() == 3);
                if (value && value->fields.size() == 3) {
                  for (const auto &field : value->fields) {
                    uint32_t bits = 0;
                    const auto *number = std::get_if<float>(&field.value);
                    CHECK(number);
                    if (number)
                      std::memcpy(&bits, number, sizeof(bits));
                    if (field.name == "x")
                      CHECK(bits == (1066074637u ^ 0x80000000u));
                    if (field.name == "y")
                      CHECK(bits == 1056394183u);
                    if (field.name == "z")
                      CHECK(bits == (1074102534u ^ 0x80000000u));
                  }
                }
              }
              std::vector<TufaNode> integers(show->begin() + 310,
                                             show->begin() + 320);
              integers.push_back({0x2b, 0, false, 0});
              const auto promoted = vmExecute(cls, integers, {}, nullptr,
                                              &classes, &states, &arena);
              CHECK(promoted.ok &&
                    std::get_if<const VmObject *>(&promoted.value));
              if (promoted.ok &&
                  std::get_if<const VmObject *>(&promoted.value)) {
                const auto *value = std::get<const VmObject *>(promoted.value);
                CHECK(value && value->fields.size() == 3);
                if (value)
                  for (const auto &field : value->fields)
                    CHECK(std::get_if<float>(&field.value) &&
                          std::get<float>(field.value) == 0.0f);
              }
            }
            const auto pori =
                tufaLoadFile("ref/raw/system/Scripts/lang/Pori.class");
            CHECK(pori.ok && !pori.file.classes.empty() && show->size() > 341);
            if (pori.ok && !pori.file.classes.empty() && show->size() > 341) {
              const auto &element = pori.file.classes[0];
              std::vector<const TufaClass *> classes{&cls, &element};
              std::vector<VmStaticFields> states{{&element, {}}};
              VmArena arena;
              VmObject receiver{
                  &cls,
                  {{"bgCamPath", static_cast<const VmObject *>(nullptr),
                    "[Ljava.lang.Pori;"}}};
              std::vector<TufaNode> array(show->begin() + 336,
                                          show->begin() + 342);
              array.insert(
                  array.end(),
                  {(*show)[340], {0x28, 0, false, 0}, {0x2b, 0, false, 0}});
              const auto result = vmExecute(cls, array, {&receiver}, nullptr,
                                            &classes, &states, &arena);
              CHECK(result.ok && std::get_if<const VmObject *>(&result.value));
              if (result.ok && std::get_if<const VmObject *>(&result.value)) {
                const auto *value = std::get<const VmObject *>(result.value);
                CHECK(value && value->arrayDescriptor == "[Ljava.lang.Pori;" &&
                      value->arrayElements.size() == 28 &&
                      std::get<const VmObject *>(receiver.fields[0].value) ==
                          value);
                for (const auto &entry : value->arrayElements)
                  CHECK(std::get<const VmObject *>(entry) == nullptr);
              }
              auto item = std::make_unique<VmObject>();
              item->runtimeClass = &element;
              const auto *itemPtr = item.get();
              arena.objects.push_back(std::move(item));
              std::vector<TufaNode> index{{0x01, 0, true, 1}};
              index.insert(index.end(), show->begin() + 370,
                           show->begin() + 375);
              index.insert(index.end(), show->begin() + 370,
                           show->begin() + 374);
              index.insert(index.end(),
                           {{0x28, 0, false, 0}, {0x2b, 0, false, 0}});
              const auto written =
                  vmExecute(cls, index, {&receiver, itemPtr}, nullptr, &classes,
                            &states, &arena);
              CHECK(written.ok &&
                    std::get_if<const VmObject *>(&written.value) &&
                    std::get<const VmObject *>(written.value) == itemPtr);
              index[2].operand = 28;
              CHECK(!vmExecute(cls, index, {&receiver, itemPtr}, nullptr,
                               &classes, &states, &arena)
                         .ok);
              index[2].operand = UINT32_MAX;
              CHECK(!vmExecute(cls, index, {&receiver, itemPtr}, nullptr,
                               &classes, &states, &arena)
                         .ok);
              index[2].operand = 0;
              CHECK(!vmExecute(cls, index, {&receiver, &receiver}, nullptr,
                               &classes, &states, &arena)
                         .ok);
              const auto vector =
                  tufaLoadFile("ref/raw/system/Scripts/lang/Vector3.class");
              const auto ypr =
                  tufaLoadFile("ref/raw/system/Scripts/lang/Ypr.class");
              const auto base =
                  tufaLoadFile("ref/raw/system/Scripts/lang/Object.class");
              CHECK(vector.ok && ypr.ok && base.ok);
              if (vector.ok && ypr.ok && base.ok) {
                const auto &vectorClass = vector.file.classes[0];
                const auto &yprClass = ypr.file.classes[0];
                const auto &baseClass = base.file.classes[0];
                classes.insert(classes.end(),
                               {&vectorClass, &yprClass, &baseClass});
                states.push_back({&vectorClass, {}});
                states.push_back({&yprClass, {}});
                states.push_back({&baseClass, {}});
                std::vector<TufaNode> point(show->begin() + 342,
                                            show->begin() + 375);
                point.insert(point.end(), show->begin() + 370,
                             show->begin() + 374);
                point.insert(point.end(),
                             {{0x28, 0, false, 0}, {0x2b, 0, false, 0}});
                const auto created = vmExecute(cls, point, {&receiver}, nullptr,
                                               &classes, &states, &arena);
                CHECK(created.ok &&
                      std::get_if<const VmObject *>(&created.value));
                if (created.ok &&
                    std::get_if<const VmObject *>(&created.value)) {
                  const auto *p = std::get<const VmObject *>(created.value);
                  CHECK(p && p->runtimeClass == &element &&
                        p->fields.size() == 2);
                  if (p && p->fields.size() == 2)
                    for (const auto &field : p->fields) {
                      const auto *v =
                          std::get_if<const VmObject *>(&field.value);
                      CHECK(v && *v);
                      if (v && *v) {
                        if (field.name == "pos")
                          CHECK((*v)->runtimeClass == &vectorClass);
                        if (field.name == "ori")
                          CHECK((*v)->runtimeClass == &yprClass);
                      }
                    }
                  if (p && p->fields.size() == 2) {
                    const VmObject *pos = nullptr;
                    const VmObject *ori = nullptr;
                    for (const auto &field : p->fields) {
                      if (field.name == "pos")
                        pos = std::get<const VmObject *>(field.value);
                      if (field.name == "ori")
                        ori = std::get<const VmObject *>(field.value);
                    }
                    const std::vector<TufaNode> wrong{
                        {0x01, 0, true, 0},  {0x01, 0, true, 1},
                        {0x01, 0, true, 2},  {0x27, 0, true, 2},
                        {0x21, 0, false, 0}, {0x2b, 0, false, 0}};
                    CHECK(!vmExecute(cls, wrong, {p, ori, pos}, nullptr,
                                     &classes, &states, &arena)
                               .ok);
                  }
                }
                classes.resize(2);
                states.resize(1);
              }
              const auto before = arena.objects.size();
              array[1].operand = UINT32_MAX;
              CHECK(!vmExecute(cls, array, {&receiver}, nullptr, &classes,
                               &states, &arena)
                         .ok &&
                    arena.objects.size() == before);
              array[1].operand = 28;
              classes.pop_back();
              CHECK(!vmExecute(cls, array, {&receiver}, nullptr, &classes,
                               &states, &arena)
                         .ok &&
                    arena.objects.size() == before);
            }
            CHECK(show->size() > 231);
            if (show->size() > 231) {
              std::vector<TufaNode> resource(show->begin() + 230,
                                             show->begin() + 232);
              resource.push_back({0x2b, 0, false, 0});
              VmArena arena;
              std::string pack;
              uint32_t id = 0;
              VmResourceResolver resolve =
                  [&](std::string_view name,
                      uint32_t local) -> std::optional<uint32_t> {
                pack = name;
                id = local;
                return 0x12345678u;
              };
              arena.resourceResolver = &resolve;
              const auto result = vmExecute(cls, resource, {}, nullptr, nullptr,
                                            nullptr, &arena);
              CHECK(result.ok && std::get_if<int32_t>(&result.value) &&
                    std::get<int32_t>(result.value) == 0x12345678);
              CHECK(pack == "maps\\skydome.rpk" && id == 36);
              arena.resourceResolver = nullptr;
              CHECK(!vmExecute(cls, resource, {}, nullptr, nullptr, nullptr,
                               &arena)
                         .ok);
              VmResourceResolver reject =
                  [](std::string_view, uint32_t) -> std::optional<uint32_t> {
                return std::nullopt;
              };
              arena.resourceResolver = &reject;
              CHECK(!vmExecute(cls, resource, {}, nullptr, nullptr, nullptr,
                               &arena)
                         .ok);
              resource[1].operand = 712;
              arena.resourceResolver = &resolve;
              CHECK(!vmExecute(cls, resource, {}, nullptr, nullptr, nullptr,
                               &arena)
                         .ok);
            }
            CHECK(show->size() > 200);
            if (show->size() > 200) {
              std::vector<VmStaticFields> states{
                  {&cls, {{"bgVideoActive", int32_t{9}, "I"}}}};
              std::vector<TufaNode> assignment{{0x27, 0, true, 0}};
              assignment.insert(assignment.end(), show->begin() + 196,
                                show->begin() + 201);
              assignment.push_back({0x2b, 0, false, 0});
              const auto equal =
                  vmExecute(cls, assignment, {}, nullptr, nullptr, &states);
              CHECK(equal.ok && std::get<int32_t>(equal.value) == 1 &&
                    std::get<int32_t>(states[0].fields[0].value) == 1);
              assignment[0].operand = 1;
              const auto unequal =
                  vmExecute(cls, assignment, {}, nullptr, nullptr, &states);
              CHECK(unequal.ok && std::get<int32_t>(unequal.value) == 0 &&
                    std::get<int32_t>(states[0].fields[0].value) == 0);
              states[0].fields.clear();
              CHECK(!vmExecute(cls, assignment, {}, nullptr, nullptr, &states)
                         .ok);
            }
            std::vector<TufaNode> product{{0x07, 0, true, 28},
                                          {0x0a, 0, true, 0x3f000000}};
            product.insert(product.end(), show->begin() + 9,
                           show->begin() + 12);
            product.push_back({0x2b, 0, false, 0});
            const auto multiplied = vmExecute(cls, product, {});
            CHECK(multiplied.ok && std::get_if<float>(&multiplied.value) &&
                  std::get<float>(multiplied.value) == 50.0f);
            product[1].operand = 0x7fc00000;
            CHECK(!vmExecute(cls, product, {}).ok);
            product[1].operand = 0x3dcccccd;
            const auto roundedProduct = vmExecute(cls, product, {});
            CHECK(roundedProduct.ok &&
                  std::get<float>(roundedProduct.value) == 10.0f);
            std::vector<TufaNode> sum{{0x07, 0, true, 28},
                                      {0x0a, 0, true, 0x40000000}};
            sum.insert(sum.end(), show->begin() + 27, show->begin() + 30);
            sum.push_back({0x2b, 0, false, 0});
            const auto added = vmExecute(cls, sum, {});
            CHECK(added.ok && std::get_if<float>(&added.value) &&
                  std::get<float>(added.value) == 5.75f);
            std::vector<TufaNode> comparison{{0x27, 0, true, 4},
                                             {0x07, 0, true, 28},
                                             {0x0a, 0, true, 0x3e800000}};
            comparison.insert(comparison.end(), show->begin() + 27,
                              show->begin() + 31);
            comparison.push_back({0x2b, 0, false, 0});
            const auto same = vmExecute(cls, comparison, {});
            CHECK(same.ok && std::get<int32_t>(same.value) == 0);
            comparison[0].operand = 3;
            const auto different = vmExecute(cls, comparison, {});
            CHECK(different.ok && std::get<int32_t>(different.value) == 1);
            comparison[0] = {0x01, 0, true, 0};
            CHECK(!vmExecute(cls, comparison,
                             {static_cast<const VmObject *>(nullptr)})
                       .ok);
            sum[1].operand = 0x3dcccccd;
            const auto roundedSum = vmExecute(cls, sum, {});
            CHECK(roundedSum.ok && std::get_if<float>(&roundedSum.value));
            if (roundedSum.ok && std::get_if<float>(&roundedSum.value)) {
              const float value = std::get<float>(roundedSum.value);
              uint32_t bits;
              std::memcpy(&bits, &value, sizeof bits);
              CHECK(bits == 0x40766666u);
            }
            sum[1].operand = 1;
            CHECK(!vmExecute(cls, sum, {}).ok);
          }
        }
  }
  {
    const auto parsed =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/Dummy.class");
    CHECK(parsed.ok && !parsed.file.classes.empty());
    if (parsed.ok && !parsed.file.classes.empty()) {
      auto cls = parsed.file.classes[0];
      const std::vector<TufaNode> *constructor = nullptr;
      TufaMethod twoArgument{};
      for (const auto &list : cls.methods)
        for (const auto &method : list) {
          if (cls.constants[method.words[1]].text != "<init>")
            continue;
          const auto &signature = cls.constants[method.words[2]].text;
          if (signature == "(I)" && method.words[3] < cls.trees.size())
            constructor = &cls.trees[method.words[3]];
          if (signature == "(Ljava.util.resource.GameRef;I)")
            twoArgument = method;
        }
      CHECK(constructor && twoArgument.words[3] < cls.trees.size());
      if (constructor && twoArgument.words[3] < cls.trees.size()) {
        VmArena arena;
        auto instance = std::make_unique<VmObject>();
        instance->runtimeClass = &cls;
        const auto *receiver = instance.get();
        arena.objects.push_back(std::move(instance));
        std::vector<const TufaClass *> classes{&cls};
        const auto cycle = vmExecute(cls, *constructor, {receiver, int32_t{16}},
                                     nullptr, &classes, nullptr, &arena);
        CHECK(!cycle.ok && cycle.error == "invalid constructor parent");
        const auto stringSignature =
            static_cast<uint32_t>(cls.constants.size());
        cls.constants.push_back({0, "(Ljava.lang.String;I)", 0, 0});
        twoArgument.words[2] = stringSignature;
        cls.methods[0].push_back(twoArgument);
        const auto ambiguous =
            vmExecute(cls, *constructor, {receiver, int32_t{16}}, nullptr,
                      &classes, nullptr, &arena);
        CHECK(!ambiguous.ok && ambiguous.error == "ambiguous constructor");
      }
    }
  }
  {
    const auto sound = tufaLoadFile("ref/raw/system/Scripts/sound/Sound.class");
    const auto dummy =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/Dummy.class");
    const auto gameType =
        tufaLoadFile("ref/raw/system/Scripts/lang/GameType.class");
    const auto gameRef =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/GameRef.class");
    CHECK(sound.ok && dummy.ok && gameType.ok && gameRef.ok);
    if (sound.ok && dummy.ok && gameType.ok && gameRef.ok) {
      const auto &caller = sound.file.classes[0];
      auto child = dummy.file.classes[0];
      const auto &middle = gameType.file.classes[0];
      const auto &owner = gameRef.file.classes[0];
      std::vector<const TufaClass *> classes{&caller, &child, &middle, &owner};
      std::vector<VmStaticFields> states{
          {&owner, {{"WORLDTREEROOT", int32_t{16}, "I"}}}};
      states[0].fields.push_back({"RID_DUMMY", int32_t{1234}, "I"});
      const std::vector<TufaNode> *dummyCtor = nullptr;
      for (const auto &list : child.methods)
        for (const auto &method : list)
          if (child.constants[method.words[1]].text == "<init>" &&
              child.constants[method.words[2]].text ==
                  "(Ljava.util.resource.GameRef;)" &&
              method.words[3] < child.trees.size())
            dummyCtor = &child.trees[method.words[3]];
      CHECK(dummyCtor && dummyCtor->size() > 4);
      if (dummyCtor && dummyCtor->size() > 4) {
        const std::vector<TufaNode> reference{
            (*dummyCtor)[4], {0x28, 0, false, 0}, {0x2b, 0, false, 0}};
        const auto resolved =
            vmExecute(child, reference, {}, nullptr, &classes, &states);
        CHECK(resolved.ok && std::get<int32_t>(resolved.value) == 1234);
      }
      const std::vector<TufaNode> *tree = nullptr;
      for (const auto &list : caller.methods)
        for (const auto &method : list)
          if (caller.constants[method.words[1]].text == "init" &&
              method.words[3] < caller.trees.size())
            tree = &caller.trees[method.words[3]];
      CHECK(tree && tree->size() > 7);
      if (tree && tree->size() > 7) {
        std::vector<TufaNode> slice(tree->begin() + 5, tree->begin() + 8);
        slice.insert(slice.end(), {{0x28, 0, false, 0}, {0x2b, 0, false, 0}});
        const auto value =
            vmExecute(caller, slice, {}, nullptr, &classes, &states);
        CHECK(value.ok && std::get<int32_t>(value.value) == 16);
        classes.erase(classes.begin() + 2);
        CHECK(!vmExecute(caller, slice, {}, nullptr, &classes, &states).ok);
      }
    }
  }
  {
    const auto init = tufaLoadFile("ref/raw/sl/Scripts/game/Init.class");
    const auto sound = tufaLoadFile("ref/raw/system/Scripts/sound/Sound.class");
    CHECK(init.ok && sound.ok && !init.file.classes.empty() &&
          !sound.file.classes.empty());
    if (init.ok && sound.ok && !init.file.classes.empty() &&
        !sound.file.classes.empty()) {
      const auto &caller = init.file.classes[0];
      auto target = sound.file.classes[0];
      const std::vector<TufaNode> *constructor = nullptr;
      for (const auto &list : caller.methods)
        for (const auto &method : list)
          if (caller.constants[method.words[1]].text == "<init>" &&
              caller.constants[method.words[2]].text == "(I)" &&
              method.words[3] < caller.trees.size())
            constructor = &caller.trees[method.words[3]];
      CHECK(constructor && constructor->size() > 8);
      if (constructor && constructor->size() > 8) {
        std::vector<TufaNode> slice(constructor->begin() + 3,
                                    constructor->begin() + 9);
        slice.insert(slice.end(), {{0x27, 0, true, 1}, {0x2b, 0, false, 0}});
        std::vector<const TufaClass *> classes{&caller, &target};
        std::vector<VmStaticFields> states;
        VmArena arena;
        const auto entered = vmExecute(caller, slice, {}, &nativeRegistry(),
                                       &classes, &states, &arena);
        CHECK(!entered.ok &&
              entered.error.find("java.sound.Sound: node 3: allocation class "
                                 "not registered: java.util.resource.Dummy") !=
                  std::string::npos);
        for (const auto &list : target.methods)
          for (const auto &method : list)
            if (target.constants[method.words[1]].text == "init" &&
                target.constants[method.words[2]].text == "()V")
              target.trees[method.words[3]] = {{0x2a, 0, false, 0}};
        const auto completed = vmExecute(caller, slice, {}, &nativeRegistry(),
                                         &classes, &states, &arena);
        CHECK(completed.ok && std::get<int32_t>(completed.value) == 1);
        classes.pop_back();
        CHECK(!vmExecute(caller, slice, {}, &nativeRegistry(), &classes,
                         &states, &arena)
                   .ok);
      }
    }
  }
  {
    const auto config =
        tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
    CHECK(config.ok && !config.file.classes.empty());
    if (config.ok && !config.file.classes.empty()) {
      const auto &cls = config.file.classes[0];
      const std::vector<TufaNode> *tree = nullptr;
      for (const auto &list : cls.methods)
        for (const auto &method : list)
          if (cls.constants[method.words[1]].text == "<init>" &&
              cls.constants[method.words[2]].text == "(I)" &&
              method.words[3] < cls.trees.size())
            tree = &cls.trees[method.words[3]];
      CHECK(tree && tree->size() > 9);
      if (tree && tree->size() > 9) {
        std::vector<TufaNode> expression{
            {0x27, 0, true, 0}, (*tree)[9], {0x2b, 0, false, 0}};
        const auto zero = vmExecute(cls, expression, {});
        CHECK(zero.ok && std::get<int32_t>(zero.value) == 1);
        expression[0].operand = 42;
        const auto nonzero = vmExecute(cls, expression, {});
        CHECK(nonzero.ok && std::get<int32_t>(nonzero.value) == 0);
        // Stock converts float and reference boxes to boolean before not.
        std::vector<TufaNode> nonIntegral{{0x07, 0, true, 28},
                                          {0x0a, 0, true, 0},
                                          (*tree)[9],
                                          {0x2b, 0, false, 0}};
        const auto converted = vmExecute(cls, nonIntegral, {});
        CHECK(converted.ok && std::get<int32_t>(converted.value) == 1);
      }
    }
  }
  {
    const auto config =
        tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
    const auto system =
        tufaLoadFile("ref/raw/system/Scripts/lang/System.class");
    CHECK(config.ok && system.ok && !config.file.classes.empty() &&
          !system.file.classes.empty());
    if (config.ok && system.ok && !config.file.classes.empty() &&
        !system.file.classes.empty()) {
      const auto &cls = config.file.classes[0];
      std::vector<const TufaClass *> classes{&cls, &system.file.classes[0]};
      const std::vector<TufaNode> *tree = nullptr;
      for (const auto &list : cls.methods)
        for (const auto &method : list)
          if (cls.constants[method.words[1]].text == "loadConfig" &&
              method.words[3] < cls.trees.size())
            tree = &cls.trees[method.words[3]];
      CHECK(tree && tree->size() > 443);
      if (tree && tree->size() > 443) {
        std::vector<TufaNode> slice(tree->begin() + 436, tree->begin() + 444);
        slice.insert(slice.end(), {{0x27, 0, true, 1}, {0x2b, 0, false, 0}});
        VmArena arena;
        VmMeasurementState measurement;
        arena.measurementState = &measurement;
        const auto result = vmExecute(cls, slice, {}, &nativeRegistry(),
                                      &classes, nullptr, &arena);
        CHECK(result.ok && measurement.value == 1000.0f &&
              measurement.product != 0.0f && measurement.quotient != 0.0f);
        arena.measurementState = nullptr;
        CHECK(!vmExecute(cls, slice, {}, &nativeRegistry(), &classes, nullptr,
                         &arena)
                   .ok);
        arena.measurementState = &measurement;
        slice[1].operand = 16777217;
        CHECK(!vmExecute(cls, slice, {}, &nativeRegistry(), &classes, nullptr,
                         &arena)
                   .ok);
      }
    }
  }
  {
    const auto sound = tufaLoadFile("ref/raw/system/Scripts/sound/Sound.class");
    const auto object =
        tufaLoadFile("ref/raw/system/Scripts/lang/Object.class");
    CHECK(sound.ok && object.ok && !sound.file.classes.empty() &&
          !object.file.classes.empty());
    if (sound.ok && object.ok && !sound.file.classes.empty() &&
        !object.file.classes.empty()) {
      auto cls = sound.file.classes[0];
      const auto &base = object.file.classes[0];
      std::vector<const TufaClass *> classes{&base, &cls};
      std::vector<VmStaticFields> states;
      CHECK(vmInitializeClass(cls, classes, states).ok && states.size() == 2);
      if (states.size() == 2) {
        for (const char *name : {"debugDisplay", "vp", "camera"}) {
          const VmField *found = nullptr;
          for (const auto &field : states.back().fields)
            if (field.name == name)
              found = &field;
          CHECK(found && std::get_if<const VmObject *>(&found->value) &&
                std::get<const VmObject *>(found->value) == nullptr);
        }
      }
      for (auto &list : cls.fields)
        for (auto &field : list)
          if (cls.constants[field.words[1]].text == "debugDisplay")
            field.words[3] = 1;
      std::vector<VmStaticFields> rejected;
      CHECK(!vmInitializeClass(cls, classes, rejected).ok && rejected.empty());
    }
  }
  {
    const auto cfg = tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
    const auto file = tufaLoadFile("ref/raw/system/Scripts/io/File.class");
    const auto native =
        tufaLoadFile("ref/raw/system/Scripts/lang/Native.class");
    const auto object =
        tufaLoadFile("ref/raw/system/Scripts/lang/Object.class");
    const auto string =
        tufaLoadFile("ref/raw/system/Scripts/lang/String.class");
    const auto sound = tufaLoadFile("ref/raw/system/Scripts/sound/Sound.class");
    CHECK(cfg.ok && file.ok && native.ok && object.ok && string.ok && sound.ok);
    if (cfg.ok && file.ok && native.ok && object.ok && string.ok && sound.ok) {
      auto c = cfg.file.classes[0];
      c.constants[267].text = "save/sl-vm-test-missing";
      const auto &f = file.file.classes[0];
      const auto &s = string.file.classes[0];
      std::vector<const TufaClass *> classes{&c,
                                             &f,
                                             &native.file.classes[0],
                                             &object.file.classes[0],
                                             &s,
                                             &sound.file.classes[0]};
      std::vector<VmStaticFields> states;
      VmArena arena;
      CHECK(vmInitializeClass(s, classes, states, nullptr, &arena).ok);
      CHECK(vmInitializeClass(f, classes, states, nullptr, &arena).ok);
      CHECK(vmInitializeClass(sound.file.classes[0], classes, states, nullptr,
                              &arena)
                .ok);
      std::vector<TufaNode> literal(c.trees[5].begin(), c.trees[5].end() - 1);
      literal.push_back({0x2b, 0, false, 0});
      const auto arg =
          vmExecute(c, literal, {}, nullptr, &classes, &states, &arena);
      CHECK(arg.ok);
      const std::vector<TufaNode> *tree = nullptr;
      for (const auto &list : c.methods)
        for (const auto &method : list)
          if (c.constants[method.words[1]].text == "loadConfig" &&
              method.words[3] < c.trees.size())
            tree = &c.trees[method.words[3]];
      CHECK(tree && tree->size() > 10);
      if (arg.ok && tree && tree->size() > 10) {
        std::vector<TufaNode> slice(tree->begin() + 5, tree->begin() + 10);
        slice.push_back({0x2b, 0, false, 0});
        VmObject receiver;
        const auto result = vmExecute(c, slice, {&receiver, arg.value}, nullptr,
                                      &classes, &states, &arena);
        CHECK(result.ok &&
              std::get<const VmObject *>(result.value)->runtimeClass == &f);
        CHECK(!vmExecute(c, slice, {&receiver, int32_t{7}}, nullptr, &classes,
                         &states, &arena)
                   .ok);
        CHECK(vmExecute(c, slice,
                        {&receiver, static_cast<const VmObject *>(nullptr)},
                        nullptr, &classes, &states, &arena)
                  .ok);
        std::vector<TufaNode> declaration(tree->begin(), tree->begin() + 11);
        declaration.insert(
            declaration.end(),
            {{0x01, 0, true, 3}, {0x28, 0, false, 0}, {0x2b, 0, false, 0}});
        const auto assigned = vmExecute(c, declaration, {&receiver, arg.value},
                                        nullptr, &classes, &states, &arena);
        CHECK(assigned.ok && std::get_if<const VmObject *>(&assigned.value) &&
              std::get<const VmObject *>(assigned.value)->runtimeClass == &f);
        auto badTarget = declaration;
        badTarget[4].op = 0x28;
        CHECK(!vmExecute(c, badTarget, {&receiver, arg.value}, nullptr,
                         &classes, &states, &arena)
                   .ok);
        std::vector<TufaNode> qualified(tree->begin() + 13, tree->begin() + 16);
        qualified.push_back({0x28, 0, false, 0});
        qualified.push_back({0x2b, 0, false, 0});
        const auto mode =
            vmExecute(c, qualified, {}, nullptr, &classes, &states, &arena);
        const VmValue *expected = nullptr;
        for (const auto &state : states)
          if (state.owner == &f)
            for (const auto &field : state.fields)
              if (field.name == "MODE_READ")
                expected = &field.value;
        CHECK(mode.ok && expected && mode.value == *expected);
        auto missing = classes;
        missing.erase(missing.begin() + 1);
        CHECK(!vmExecute(c, qualified, {}, nullptr, &missing, &states, &arena)
                   .ok);
        char tempName[] = "/tmp/sl-vm-call-XXXXXX";
        const char *temp = mkdtemp(tempName);
        CHECK(temp);
        if (!temp)
          return 1;
        std::filesystem::create_directories(std::filesystem::path(temp) /
                                            "save");
        FileState fileState(temp, "ref");
        arena.fileState = &fileState;
        std::vector<TufaNode> call(tree->begin(), tree->begin() + 11);
        call.insert(call.end(), tree->begin() + 13, tree->begin() + 21);
        call.push_back({0x2b, 0, false, 0});
        const auto opened =
            vmExecute(c, call, {&receiver, arg.value}, &nativeRegistry(),
                      &classes, &states, &arena);
        CHECK(opened.ok && std::get_if<int32_t>(&opened.value) &&
              std::get<int32_t>(opened.value) == 0);
        CHECK(!vmExecute(c, call, {&receiver, arg.value}, nullptr, &classes,
                         &states, &arena)
                   .ok);
        auto wrongCount = call;
        wrongCount[14].operand = 0;
        CHECK(!vmExecute(c, wrongCount, {&receiver, arg.value},
                         &nativeRegistry(), &classes, &states, &arena)
                   .ok);
        std::filesystem::create_directories(std::filesystem::path(temp) /
                                            "save/game");
        std::filesystem::copy_file("ref/raw/save/game/options",
                                   std::filesystem::path(temp) /
                                       "save/game/options");
        c.constants[267].text = "save/game/options";
        const auto path =
            vmExecute(c, literal, {}, nullptr, &classes, &states, &arena);
        CHECK(path.ok);
        std::vector<TufaNode> read(call.begin(), call.end() - 1);
        read.insert(read.end(), tree->begin() + 23, tree->begin() + 28);
        read.push_back({0x2b, 0, false, 0});
        if (path.ok) {
          VmSoundState soundState;
          arena.soundState = &soundState;
          std::vector<TufaNode> volume(call.begin(), call.end() - 1);
          volume.insert(volume.end(), tree->begin() + 295, tree->begin() + 310);
          volume.insert(volume.end(),
                        {{0x27, 0, true, 1}, {0x2b, 0, false, 0}});
          const auto volumeResult =
              vmExecute(c, volume, {&receiver, path.value}, &nativeRegistry(),
                        &classes, &states, &arena);
          CHECK(volumeResult.ok);
          uint32_t volumeBits;
          std::memcpy(&volumeBits, &soundState.volumes[0], sizeof volumeBits);
          CHECK(volumeBits == 0xfedcba98u);
          arena.soundState = nullptr;
          CHECK(!vmExecute(c, volume, {&receiver, path.value},
                           &nativeRegistry(), &classes, &states, &arena)
                     .ok);
          arena.soundState = &soundState;
          const auto first =
              vmExecute(c, read, {&receiver, path.value}, &nativeRegistry(),
                        &classes, &states, &arena);
          CHECK(first.ok && std::get_if<int32_t>(&first.value) &&
                static_cast<uint32_t>(std::get<int32_t>(first.value)) ==
                    0xfedcba98u);
          std::vector<TufaNode> readFloat(call.begin(), call.end() - 1);
          for (int i = 0; i < 6; ++i)
            readFloat.insert(readFloat.end(), tree->begin() + 23,
                             tree->begin() + 28);
          readFloat.insert(readFloat.end(), tree->begin() + 73,
                           tree->begin() + 78);
          readFloat.push_back({0x2b, 0, false, 0});
          const auto floating =
              vmExecute(c, readFloat, {&receiver, path.value},
                        &nativeRegistry(), &classes, &states, &arena);
          CHECK(floating.ok && std::get_if<float>(&floating.value));
          if (floating.ok && std::get_if<float>(&floating.value)) {
            uint32_t bits;
            const float value = std::get<float>(floating.value);
            std::memcpy(&bits, &value, sizeof bits);
            CHECK(bits == 0x3f806166u);
          }
          states.push_back({&c, {{"video_gamma", 0.0f, "F"}}});
          readFloat.pop_back();
          readFloat.insert(readFloat.end(), tree->begin() + 78,
                           tree->begin() + 81);
          readFloat.push_back((*tree)[79]);
          readFloat.push_back({0x28, 0, false, 0});
          readFloat.push_back({0x2b, 0, false, 0});
          const auto assignedFloat =
              vmExecute(c, readFloat, {&receiver, path.value},
                        &nativeRegistry(), &classes, &states, &arena);
          CHECK(assignedFloat.ok && std::get_if<float>(&assignedFloat.value) &&
                std::get<float>(assignedFloat.value) ==
                    std::get<float>(states.back().fields[0].value));
          if (assignedFloat.ok && std::get_if<float>(&assignedFloat.value)) {
            uint32_t bits;
            const float value = std::get<float>(assignedFloat.value);
            std::memcpy(&bits, &value, sizeof bits);
            CHECK(bits == 0x3f806166u);
          }
          states.back().fields[0].value = int32_t{0};
          CHECK(!vmExecute(c, readFloat, {&receiver, path.value},
                           &nativeRegistry(), &classes, &states, &arena)
                     .ok);
          states.back().fields.push_back(
              {c.constants[36].text, int32_t{9}, "I"});
          std::vector<TufaNode> convert(call.begin(), call.end() - 1);
          for (int i = 0; i < 16; ++i) {
            convert.insert(convert.end(), tree->begin() + 23,
                           tree->begin() + 28);
            convert.push_back({0x08, 0, true, 27});
          }
          convert.insert(convert.end(), tree->begin() + 159,
                         tree->begin() + 167);
          convert.push_back((*tree)[165]);
          convert.push_back({0x28, 0, false, 0});
          convert.push_back({0x2b, 0, false, 0});
          const auto converted =
              vmExecute(c, convert, {&receiver, path.value}, &nativeRegistry(),
                        &classes, &states, &arena);
          CHECK(converted.ok && std::get_if<int32_t>(&converted.value) &&
                std::get<int32_t>(converted.value) == 0);
          for (const auto bits : {uint32_t{0x3ff33333}, uint32_t{0xbff33333},
                                  uint32_t{0x7fc00000}, uint32_t{0x4f000000}}) {
            const std::vector<TufaNode> conversion{
                {0x07, 0, true, 28}, {0x0a, 0, true, bits}, (*tree)[165],
                (*tree)[166],        (*tree)[165],          {0x28, 0, false, 0},
                {0x2b, 0, false, 0}};
            const auto value = vmExecute(c, conversion, {}, nullptr, &classes,
                                         &states, &arena);
            if (bits == 0x3ff33333)
              CHECK(value.ok && std::get<int32_t>(value.value) == 1);
            else if (bits == 0xbff33333)
              CHECK(value.ok && std::get<int32_t>(value.value) == -1);
            else
              CHECK(!value.ok);
          }
        }
        std::filesystem::remove_all(temp);
        missing.push_back(&f);
        missing.push_back(&f);
        CHECK(!vmExecute(c, qualified, {}, nullptr, &missing, &states, &arena)
                   .ok);
      }
    }
  }
  {
    const auto parsed =
        tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
    CHECK(parsed.ok && !parsed.file.classes.empty());
    if (parsed.ok && !parsed.file.classes.empty()) {
      const auto &cls = parsed.file.classes[0];
      const std::vector<TufaNode> *tree = nullptr;
      for (const auto &list : cls.methods)
        for (const auto &method : list)
          if (cls.constants[method.words[1]].text == "loadConfig" &&
              method.words[3] < cls.trees.size())
            tree = &cls.trees[method.words[3]];
      CHECK(tree && tree->size() >= 5);
      if (tree && tree->size() >= 5) {
        std::vector<TufaNode> slice(tree->begin(), tree->begin() + 5);
        slice.push_back({0x28, 0, false, 0});
        slice.push_back({0x2b, 0, false, 0});
        VmObject receiver;
        const auto result = vmExecute(
            cls, slice, {&receiver, static_cast<const VmObject *>(nullptr)});
        CHECK(result.ok && std::get_if<const VmObject *>(&result.value) &&
              std::get<const VmObject *>(result.value) == nullptr);
        slice[0].operand = 3;
        CHECK(!vmExecute(cls, slice,
                         {&receiver, static_cast<const VmObject *>(nullptr)})
                   .ok);
        std::vector<TufaNode> integer(tree->begin(), tree->begin() + 2);
        integer.insert(
            integer.end(),
            {{0x01, 0, true, 2}, {0x28, 0, false, 0}, {0x2b, 0, false, 0}});
        const auto zero = vmExecute(
            cls, integer, {&receiver, static_cast<const VmObject *>(nullptr)});
        CHECK(zero.ok && std::get_if<int32_t>(&zero.value) &&
              std::get<int32_t>(zero.value) == 0);
      }
    }
  }
  {
    TufaClass diagnostic;
    diagnostic.name = "test.Class";
    const std::vector<TufaNode> unknown{{0x07, 0, true, 99}};
    const auto result = vmExecute(diagnostic, unknown, {});
    CHECK(!result.ok &&
          result.error.find("test.Class: node 0: unsupported operation 0x07, "
                            "suboperation 99") != std::string::npos);
  }
  {
    const auto dialog =
        tufaLoadFile("ref/raw/system/Scripts/render/osd/dialog/Dialog.class");
    const auto strings =
        tufaLoadFile("ref/raw/system/Scripts/lang/String.class");
    CHECK(dialog.ok && strings.ok && !dialog.file.classes.empty() &&
          !strings.file.classes.empty());
    if (dialog.ok && strings.ok && !dialog.file.classes.empty() &&
        !strings.file.classes.empty()) {
      TufaClass cls;
      for (const auto &candidate : dialog.file.classes)
        if (candidate.name == "java.render.osd.dialog.FileRequesterDialog")
          cls = candidate;
      CHECK(!cls.name.empty());
      const auto &stringClass = strings.file.classes[0];
      uint32_t methodName = 0, descriptor = 0;
      for (const auto &list : cls.methods)
        for (const auto &method : list)
          if (cls.constants[method.words[1]].text == "validator" &&
              cls.constants[method.words[2]].text == "(Ljava.lang.String;)I") {
            methodName = method.words[1];
            descriptor = method.words[2];
          }
      CHECK(methodName && descriptor);
      const uint32_t ownerText = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({0, cls.name, 0, 0});
      const uint32_t owner = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({4, {}, ownerText, 0});
      const uint32_t pair = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({7, {}, methodName, descriptor});
      const uint32_t ref = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({5, {}, owner, pair});
      const uint32_t textIndex = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({0, "argument", 0, 0});
      const std::vector<TufaNode> caller{
          {0x07, 0, true, 28}, {0x09, 0, true, textIndex}, {0x27, 0, true, 1},
          {0x24, 0, false, 0}, {0x1a, 0, true, ref},       {0x2b, 0, false, 0}};
      std::vector<const TufaClass *> classes{&cls, &stringClass};
      std::vector<VmStaticFields> states{{&stringClass, {}}};
      VmArena arena;
      VmObject receiver{&cls, {}};
      const auto result = vmExecute(cls, caller, {&receiver}, nullptr, &classes,
                                    &states, &arena);
      CHECK(result.ok && std::get<int32_t>(result.value) == 1 &&
            arena.strings.size() == 1);
      const std::vector<TufaNode> external{{0x01, 0, true, 1},
                                           {0x27, 0, true, 1},
                                           {0x24, 0, false, 0},
                                           {0x1a, 0, true, ref},
                                           {0x2b, 0, false, 0}};
      VmObject unowned{&stringClass, {}};
      CHECK(!vmExecute(cls, external, {&receiver, &unowned}, nullptr, &classes,
                       &states, &arena)
                 .ok);
      // Stock FUN_00417da0 converts a number to "%d" text when it is
      // passed to a String parameter.
      const size_t stringsBefore = arena.strings.size();
      CHECK(vmExecute(cls, external, {&receiver, int32_t{3}}, nullptr,
                      &classes, &states, &arena)
                .ok);
      CHECK(arena.strings.size() == stringsBefore + 1 &&
            arena.strings.back().first == "3");
      CHECK(vmExecute(cls, external,
                      {&receiver, static_cast<const VmObject *>(nullptr)},
                      nullptr, &classes, &states, &arena)
                .ok);
    }
  }
  {
    const auto config =
        tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
    const auto gameType =
        tufaLoadFile("ref/raw/system/Scripts/lang/GameType.class");
    const auto gameRef =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/GameRef.class");
    const auto resource =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/ResourceRef.class");
    const auto native =
        tufaLoadFile("ref/raw/system/Scripts/lang/Native.class");
    const auto object =
        tufaLoadFile("ref/raw/system/Scripts/lang/Object.class");
    CHECK(config.ok && gameType.ok && gameRef.ok && resource.ok && native.ok &&
          object.ok);
    if (config.ok && gameType.ok && gameRef.ok && resource.ok && native.ok &&
        object.ok && !config.file.classes.empty() &&
        !gameType.file.classes.empty() && !gameRef.file.classes.empty() &&
        !resource.file.classes.empty() && !native.file.classes.empty() &&
        !object.file.classes.empty()) {
      const auto &cfg = config.file.classes[0];
      std::vector<const TufaClass *> classes{&cfg,
                                             &gameType.file.classes[0],
                                             &gameRef.file.classes[0],
                                             &resource.file.classes[0],
                                             &native.file.classes[0],
                                             &object.file.classes[0]};
      const std::vector<TufaNode> *constructor = nullptr;
      for (const auto &list : cfg.methods)
        for (const auto &method : list)
          if (cfg.constants[method.words[1]].text == "<init>" &&
              cfg.constants[method.words[2]].text == "(I)" &&
              method.words[3] < cfg.trees.size())
            constructor = &cfg.trees[method.words[3]];
      CHECK(constructor);
      if (constructor) {
        VmArena arena;
        auto instance = std::make_unique<VmObject>();
        instance->runtimeClass = &cfg;
        instance->fields.push_back({"ptr", int32_t{0}, "I"});
        const auto *receiver = instance.get();
        arena.objects.push_back(std::move(instance));
        const auto result =
            vmExecute(cfg, *constructor, {receiver, int32_t{0}},
                      &nativeRegistry(), &classes, nullptr, &arena);
        CHECK(!result.ok &&
              result.error == "string literal context unavailable");
        CHECK(arena.resourceStates.size() == 1 &&
              std::get<int32_t>(receiver->fields[0].value) == 1);
      }
    }
  }
  {
    const auto sound = tufaLoadFile("ref/raw/system/Scripts/sound/Sound.class");
    const auto gameRef =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/GameRef.class");
    const auto resourceRef =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/ResourceRef.class");
    CHECK(sound.ok && gameRef.ok && resourceRef.ok &&
          !sound.file.classes.empty() && !gameRef.file.classes.empty() &&
          !resourceRef.file.classes.empty());
    if (sound.ok && gameRef.ok && resourceRef.ok &&
        !sound.file.classes.empty() && !gameRef.file.classes.empty() &&
        !resourceRef.file.classes.empty()) {
      const auto &cls = sound.file.classes[0];
      const auto &game = gameRef.file.classes[0];
      const auto &resource = resourceRef.file.classes[0];
      const std::vector<TufaNode> *init = nullptr;
      for (const auto &list : cls.methods)
        for (const auto &method : list) {
          const auto &name = cls.constants[method.words[1]].text;
          if (name == "init" && method.words[3] < cls.trees.size())
            init = &cls.trees[method.words[3]];
          if (name == "enableDebugDump")
            CHECK(method.words[0] == 0x48);
        }
      CHECK(init && init->size() > 17);
      if (init && init->size() > 17) {
        std::vector<TufaNode> caller(init->begin() + 11, init->begin() + 18);
        caller.push_back({0x2b, 0, false, 0});
        std::vector<const TufaClass *> classes{&cls, &game, &resource};
        VmArena arena;
        auto ref = std::make_unique<VmObject>();
        ref->runtimeClass = &game;
        const auto *value = ref.get();
        arena.objects.push_back(std::move(ref));
        const auto result = vmExecute(cls, caller, {value}, &nativeRegistry(),
                                      &classes, nullptr, &arena);
        CHECK(result.ok && std::get<int32_t>(result.value) == 0);
        CHECK(vmExecute(cls, caller, {static_cast<const VmObject *>(nullptr)},
                        &nativeRegistry(), &classes, nullptr, &arena)
                  .ok);
        CHECK(
            !vmExecute(cls, caller, {value}, nullptr, &classes, nullptr, &arena)
                 .ok);
        CHECK(!vmExecute(cls, caller, {int32_t{1}}, &nativeRegistry(), &classes,
                         nullptr, &arena)
                   .ok);
        auto wrong = std::make_unique<VmObject>();
        wrong->runtimeClass = &cls;
        const auto *wrongPtr = wrong.get();
        arena.objects.push_back(std::move(wrong));
        CHECK(!vmExecute(cls, caller, {wrongPtr}, &nativeRegistry(), &classes,
                         nullptr, &arena)
                   .ok);
        VmObject unowned{&game, {}};
        CHECK(!vmExecute(cls, caller, {&unowned}, &nativeRegistry(), &classes,
                         nullptr, &arena)
                   .ok);
      }
    }
  }
  {
    const auto config =
        tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
    CHECK(config.ok && !config.file.classes.empty());
    if (config.ok && !config.file.classes.empty()) {
      auto cls = config.file.classes[0];
      cls.classHeader[3] = UINT32_MAX;
      for (auto &list : cls.fields) {
        std::vector<TufaField> selected;
        for (const auto &field : list)
          if ((field.words[0] & 8) &&
              cls.constants[field.words[1]].text == "mem_vertex_max")
            selected.push_back(field);
        list = std::move(selected);
      }
      std::vector<const TufaClass *> classes{&cls};
      std::vector<VmStaticFields> states;
      CHECK(vmInitializeClass(cls, classes, states).ok);
      CHECK(states.size() == 1 && states[0].fields.size() == 1 &&
            std::get<int32_t>(states[0].fields[0].value) == 83886080);
    }
    const std::vector<TufaNode> product{{0x01, 0, true, 0},
                                        {0x01, 0, true, 1},
                                        {0x07, 0, true, 19},
                                        {0x2b, 0, false, 0}};
    const auto wrap = vmExecute(product, {int32_t{1073741824}, int32_t{4}});
    CHECK(wrap.ok && std::get<int32_t>(wrap.value) == 0);
    const auto promoted = vmExecute(product, {1.0f, int32_t{4}});
    CHECK(promoted.ok && std::get<float>(promoted.value) == 4.0f);
    // 07 23 copies the previous value before incrementing; 08 23 increments
    // the slot and discards it.
    const std::vector<TufaNode> post{
        {0x01, 0, true, 0}, {0x07, 0, true, 23}, {0x2b, 0, false, 0}};
    const auto previous = vmExecute(post, {int32_t{7}});
    CHECK(previous.ok && std::get<int32_t>(previous.value) == 7);
    const std::vector<TufaNode> incremented{{0x01, 0, true, 0},
                                            {0x08, 0, true, 23},
                                            {0x01, 0, true, 0},
                                            {0x2b, 0, false, 0}};
    const auto next = vmExecute(incremented, {int32_t{7}});
    CHECK(next.ok && std::get<int32_t>(next.value) == 8);
  }
  {
    const auto config =
        tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
    CHECK(config.ok && !config.file.classes.empty());
    if (config.ok && !config.file.classes.empty()) {
      auto cls = config.file.classes[0];
      cls.classHeader[3] = UINT32_MAX;
      for (auto &list : cls.fields) {
        std::vector<TufaField> selected;
        for (const auto &field : list)
          if ((field.words[0] & 8) &&
              cls.constants[field.words[1]].text == "camera_FOV")
            selected.push_back(field);
        list = std::move(selected);
      }
      std::vector<const TufaClass *> classes{&cls};
      std::vector<VmStaticFields> states;
      CHECK(vmInitializeClass(cls, classes, states).ok);
      CHECK(states.size() == 1 && states[0].fields.size() == 1 &&
            std::get<float>(states[0].fields[0].value) == 0.625f);
      cls.trees[52][1].operand = 0x3f8ccccd;
      cls.trees[52][3].operand = 0x3f8ccccd;
      std::vector<VmStaticFields> rejected;
      CHECK(!vmInitializeClass(cls, classes, rejected).ok && rejected.empty());
    }
  }
  {
    const auto config =
        tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
    CHECK(config.ok && !config.file.classes.empty());
    if (config.ok && !config.file.classes.empty()) {
      auto cls = config.file.classes[0];
      cls.classHeader[3] = UINT32_MAX;
      for (auto &list : cls.fields) {
        std::vector<TufaField> selected;
        for (const auto &field : list)
          if ((field.words[0] & 8) &&
              cls.constants[field.words[1]].text == "camera_mirror_viewrange")
            selected.push_back(field);
        list = std::move(selected);
      }
      std::vector<const TufaClass *> classes{&cls};
      std::vector<VmStaticFields> states;
      CHECK(vmInitializeClass(cls, classes, states).ok);
      CHECK(states.size() == 1 && states[0].fields.size() == 1 &&
            std::get<float>(states[0].fields[0].value) == 50.0f);
      cls.trees[39][1].operand = 16777217;
      std::vector<VmStaticFields> rejected;
      CHECK(!vmInitializeClass(cls, classes, rejected).ok && rejected.empty());
    }
  }
  {
    const auto config =
        tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
    const auto strings =
        tufaLoadFile("ref/raw/system/Scripts/lang/String.class");
    CHECK(config.ok && strings.ok && !config.file.classes.empty() &&
          !strings.file.classes.empty());
    if (config.ok && strings.ok && !config.file.classes.empty() &&
        !strings.file.classes.empty()) {
      auto cls = config.file.classes[0];
      const auto &stringClass = strings.file.classes[0];
      cls.classHeader[3] = UINT32_MAX;
      for (auto &list : cls.fields) {
        std::vector<TufaField> selected;
        for (const auto &field : list)
          if ((field.words[0] & 8) &&
              cls.constants[field.words[1]].text == "version")
            selected.push_back(field);
        list = std::move(selected);
      }
      std::vector<const TufaClass *> classes{&cls, &stringClass};
      std::vector<VmStaticFields> states{{&stringClass, {}}};
      VmArena arena;
      CHECK(vmInitializeClass(cls, classes, states, nullptr, &arena).ok);
      CHECK(states.size() == 2 && states[1].fields.size() == 1 &&
            arena.strings.size() == 1);
      if (states.size() == 2 && states[1].fields.size() == 1 &&
          arena.strings.size() == 1) {
        const auto *value =
            std::get<const VmObject *>(states[1].fields[0].value);
        CHECK(value && value->runtimeClass == &stringClass &&
              arena.strings[0].first == "v2.2.1" &&
              arena.strings[0].second == value);
        std::vector<TufaNode> expression(cls.trees[5].begin(),
                                         cls.trees[5].end() - 1);
        expression.push_back({0x2b, 0, false, 0});
        const auto again =
            vmExecute(cls, expression, {}, nullptr, &classes, &states, &arena);
        CHECK(again.ok && std::get<const VmObject *>(again.value) == value &&
              arena.strings.size() == 1);
        cls.constants[267].text = "other";
        const auto different =
            vmExecute(cls, expression, {}, nullptr, &classes, &states, &arena);
        CHECK(different.ok &&
              std::get<const VmObject *>(different.value) != value &&
              arena.strings.size() == 2);
        cls.constants[267].text = std::string("\xff", 1);
        const auto byteString =
            vmExecute(cls, expression, {}, nullptr, &classes, &states, &arena);
        CHECK(byteString.ok &&
              arena.strings.back().first == std::string("\xff", 1));
        cls.constants[267].text = std::string("a\0b", 3);
        CHECK(
            !vmExecute(cls, expression, {}, nullptr, &classes, &states, &arena)
                 .ok);
      }
      cls.constants[267].text = "v2.2.1";
      std::vector<VmStaticFields> missing;
      VmArena otherArena;
      CHECK(
          !vmInitializeClass(cls, classes, missing, nullptr, &otherArena).ok &&
          missing.empty());
      std::vector<const TufaClass *> missingClass{&cls};
      CHECK(!vmInitializeClass(cls, missingClass, missing, nullptr, &otherArena)
                 .ok &&
            missing.empty());
    }
  }
  {
    const auto object =
        tufaLoadFile("ref/raw/system/Scripts/lang/Object.class");
    const auto native =
        tufaLoadFile("ref/raw/system/Scripts/lang/Native.class");
    const auto resource =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/ResourceRef.class");
    const auto gameRef =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/GameRef.class");
    const auto runnable =
        tufaLoadFile("ref/raw/system/Scripts/lang/Runnable.class");
    const auto gameType =
        tufaLoadFile("ref/raw/system/Scripts/lang/GameType.class");
    const auto config =
        tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
    CHECK(object.ok && native.ok && resource.ok && gameRef.ok && runnable.ok &&
          gameType.ok && config.ok);
    if (object.ok && native.ok && resource.ok && gameRef.ok && runnable.ok &&
        gameType.ok && config.ok && !object.file.classes.empty() &&
        !native.file.classes.empty() && !resource.file.classes.empty() &&
        !gameRef.file.classes.empty() && !runnable.file.classes.empty() &&
        !gameType.file.classes.empty() && !config.file.classes.empty()) {
      const auto &root = object.file.classes[0];
      const auto &iface = runnable.file.classes[0];
      const auto &game = gameType.file.classes[0];
      const auto &cfg = config.file.classes[0];
      std::vector<const TufaClass *> classes{&root,
                                             &native.file.classes[0],
                                             &resource.file.classes[0],
                                             &gameRef.file.classes[0],
                                             &iface,
                                             &game,
                                             &cfg};
      VmResourceResolver resolver = [](std::string_view,
                                       uint32_t) -> std::optional<uint32_t> {
        return 123U;
      };
      std::vector<VmStaticFields> states;
      CHECK(vmInitializeClass(game, classes, states, &resolver).ok);
      CHECK(states.size() == 6 && states[0].owner == &root &&
            states[4].owner == &iface && states[5].owner == &game);
      TufaClass expression;
      expression.constants.push_back({0, "Ljava.lang.GameType;", 0, 0});
      const std::vector<TufaNode> allocate{
          {0x07, 0, true, 30}, {0x06, 0, true, 0}, {0x2b, 0, false, 0}};
      VmArena arena;
      const auto allocated = vmExecute(expression, allocate, {}, nullptr,
                                       &classes, &states, &arena);
      CHECK(allocated.ok &&
            std::get<const VmObject *>(allocated.value)->runtimeClass == &game);
      auto missingInterface = states;
      missingInterface.erase(missingInterface.begin() + 4);
      const auto countBefore = arena.objects.size();
      CHECK(!vmExecute(expression, allocate, {}, nullptr, &classes,
                       &missingInterface, &arena)
                 .ok &&
            arena.objects.size() == countBefore);
      std::vector<VmStaticFields> untouched;
      const auto failed = vmInitializeClass(cfg, classes, untouched, &resolver);
      CHECK(!failed.ok &&
            failed.error.find("java.util.Config.version") !=
                std::string::npos &&
            untouched.empty());
      auto cyclicInterface = iface;
      cyclicInterface.interfaces.push_back(cyclicInterface.classHeader[2]);
      auto cyclicClasses = classes;
      cyclicClasses[4] = &cyclicInterface;
      std::vector<VmStaticFields> rejected;
      CHECK(!vmInitializeClass(game, cyclicClasses, rejected, &resolver).ok &&
            rejected.empty());
      cyclicClasses[4] = &iface;
      cyclicClasses.push_back(&iface);
      CHECK(!vmInitializeClass(game, cyclicClasses, rejected, &resolver).ok &&
            rejected.empty());
      auto isolated = cfg;
      isolated.classHeader[3] = UINT32_MAX;
      for (auto &list : isolated.fields) {
        std::vector<TufaField> selected;
        for (const auto &field : list)
          if (isolated.constants[field.words[1]].text == "OFF")
            selected.push_back(field);
        list = std::move(selected);
      }
      std::vector<const TufaClass *> only{&isolated};
      std::vector<VmStaticFields> off;
      CHECK(vmInitializeClass(isolated, only, off).ok && off.size() == 1 &&
            off[0].fields.size() == 1 &&
            std::get<int32_t>(off[0].fields[0].value) == -1);
    }
  }
  {
    const auto parsed =
        tufaLoadFile("ref/raw/system/Scripts/util/Config.class");
    CHECK(parsed.ok && !parsed.file.classes.empty());
    if (parsed.ok && !parsed.file.classes.empty()) {
      auto cls = parsed.file.classes[0];
      cls.classHeader[3] = UINT32_MAX;
      for (auto &list : cls.fields) {
        std::vector<TufaField> selected;
        for (const auto &field : list)
          if ((field.words[0] & 8) &&
              cls.constants[field.words[1]].text == "init")
            selected.push_back(field);
        list = std::move(selected);
      }
      std::vector<const TufaClass *> classes{&cls};
      std::vector<VmStaticFields> states;
      std::string requestedPack;
      uint32_t requestedId = 0;
      VmResourceResolver resolver =
          [&](std::string_view pack, uint32_t id) -> std::optional<uint32_t> {
        requestedPack = pack;
        requestedId = id;
        return uint32_t{0x12345678};
      };
      CHECK(vmInitializeClass(cls, classes, states, &resolver).ok);
      CHECK(requestedPack == "sl.rpk" && requestedId == 64);
      CHECK(states.size() == 1 && states[0].fields.size() == 1 &&
            states[0].fields[0].name == "init" &&
            std::get<int32_t>(states[0].fields[0].value) ==
                int32_t{0x12345678});
      std::vector<VmStaticFields> failed;
      VmResourceResolver reject = [](std::string_view,
                                     uint32_t) -> std::optional<uint32_t> {
        return std::nullopt;
      };
      CHECK(!vmInitializeClass(cls, classes, failed, &reject).ok &&
            failed.empty());
      CHECK(!vmInitializeClass(cls, classes, failed).ok && failed.empty());
    }
  }
  {
    const auto inventory =
        tufaLoadFile("ref/raw/sl/Scripts/game/Inventory.class");
    const auto vector =
        tufaLoadFile("ref/raw/system/Scripts/util/Vector.class");
    CHECK(inventory.ok && vector.ok && !inventory.file.classes.empty() &&
          !vector.file.classes.empty());
    if (inventory.ok && vector.ok && !inventory.file.classes.empty() &&
        !vector.file.classes.empty()) {
      const auto &cls = inventory.file.classes[0];
      const auto &vectorClass = vector.file.classes[0];
      const std::vector<TufaNode> *tree = nullptr;
      for (const auto &list : cls.methods)
        for (const auto &method : list)
          if (cls.constants[method.words[1]].text == "size" &&
              cls.constants[method.words[2]].text == "()I" &&
              method.words[3] < cls.trees.size())
            tree = &cls.trees[method.words[3]];
      CHECK(tree);
      if (tree) {
        std::vector<const TufaClass *> classes{&cls, &vectorClass};
        VmArena arena;
        auto items = std::make_unique<VmObject>();
        items->runtimeClass = &vectorClass;
        items->fields.push_back({"elementCount", int32_t{7}});
        const auto *itemsPtr = items.get();
        arena.objects.push_back(std::move(items));
        VmObject receiver{&cls, {{"items", itemsPtr, "Ljava.util.Vector;"}}};
        const auto result = vmExecute(cls, *tree, {&receiver}, nullptr,
                                      &classes, nullptr, &arena);
        CHECK(result.ok && std::get<int32_t>(result.value) == 7);
        receiver.fields[0].value = static_cast<const VmObject *>(nullptr);
        CHECK(!vmExecute(cls, *tree, {&receiver}, nullptr, &classes, nullptr,
                         &arena)
                   .ok);
        receiver.fields[0].value = &receiver;
        CHECK(!vmExecute(cls, *tree, {&receiver}, nullptr, &classes, nullptr,
                         &arena)
                   .ok);
      }
    }
  }
  {
    const auto options =
        tufaLoadFile("ref/raw/sl/Scripts/game/OptionsDialog.class");
    const auto gameRef =
        tufaLoadFile("ref/raw/system/Scripts/util/resource/GameRef.class");
    CHECK(options.ok && gameRef.ok && !gameRef.file.classes.empty());
    if (options.ok && gameRef.ok && !gameRef.file.classes.empty()) {
      const TufaClass *cls = nullptr;
      for (const auto &candidate : options.file.classes)
        if (candidate.name == "java.game.OptionsDialog")
          cls = &candidate;
      CHECK(cls);
      if (cls) {
        const std::vector<TufaNode> *setter = nullptr;
        for (const auto &list : cls->methods)
          for (const auto &method : list)
            if (cls->constants[method.words[1]].text == "setActiveCamera" &&
                method.words[3] < cls->trees.size())
              setter = &cls->trees[method.words[3]];
        CHECK(setter);
        if (setter) {
          const auto &refClass = gameRef.file.classes[0];
          std::vector<const TufaClass *> classes{cls, &refClass};
          VmArena arena;
          auto holder = std::make_unique<VmObject>();
          holder->runtimeClass = cls;
          holder->fields.push_back({"activeCamera",
                                    static_cast<const VmObject *>(nullptr),
                                    "Ljava.util.resource.GameRef;"});
          const auto *holderPtr = holder.get();
          arena.objects.push_back(std::move(holder));
          auto camera = std::make_unique<VmObject>();
          camera->runtimeClass = &refClass;
          const auto *cameraPtr = camera.get();
          arena.objects.push_back(std::move(camera));
          CHECK(vmExecute(*cls, *setter, {holderPtr, cameraPtr}, nullptr,
                          &classes, nullptr, &arena)
                    .ok);
          CHECK(std::get<const VmObject *>(holderPtr->fields[0].value) ==
                cameraPtr);
          std::vector<TufaNode> read{{0x1b, 0, true, (*setter)[1].operand},
                                     {0x2b, 0, false, 0}};
          const auto result = vmExecute(*cls, read, {holderPtr}, nullptr,
                                        &classes, nullptr, &arena);
          CHECK(result.ok &&
                std::get<const VmObject *>(result.value) == cameraPtr);
          CHECK(!vmExecute(*cls, *setter, {holderPtr, holderPtr}, nullptr,
                           &classes, nullptr, &arena)
                     .ok);
          VmObject unowned{&refClass, {}};
          CHECK(!vmExecute(*cls, *setter, {holderPtr, &unowned}, nullptr,
                           &classes, nullptr, &arena)
                     .ok);
          CHECK(std::get<const VmObject *>(holderPtr->fields[0].value) ==
                cameraPtr);
          CHECK(vmExecute(*cls, *setter,
                          {holderPtr, static_cast<const VmObject *>(nullptr)},
                          nullptr, &classes, nullptr, &arena)
                    .ok);
          CHECK(std::get<const VmObject *>(holderPtr->fields[0].value) ==
                nullptr);
        }
      }
    }
  }
  {
    const auto parsed = tufaLoadFile("ref/raw/parts/scripts/Part.class");
    CHECK(parsed.ok && !parsed.file.classes.empty());
    if (parsed.ok && !parsed.file.classes.empty()) {
      const auto &cls = parsed.file.classes[0];
      const std::vector<TufaNode> *tree = nullptr;
      for (const auto &list : cls.methods)
        for (const auto &method : list)
          if (cls.constants[method.words[1]].text == "isDriveable" &&
              cls.constants[method.words[2]].text == "()Ljava.lang.String;" &&
              method.words[3] < cls.trees.size())
            tree = &cls.trees[method.words[3]];
      CHECK(tree);
      if (tree) {
        VmObject object;
        const auto result = vmExecute(*tree, {&object});
        CHECK(result.ok &&
              std::holds_alternative<const VmObject *>(result.value) &&
              std::get<const VmObject *>(result.value) == nullptr);
      }
    }
    const auto objectClass =
        tufaLoadFile("ref/raw/system/Scripts/lang/Object.class");
    const auto labelClass =
        tufaLoadFile("ref/raw/system/Scripts/render/osd/Label.class");
    CHECK(objectClass.ok && labelClass.ok &&
          !objectClass.file.classes.empty() &&
          !labelClass.file.classes.empty());
    if (objectClass.ok && labelClass.ok && !objectClass.file.classes.empty() &&
        !labelClass.file.classes.empty()) {
      const auto &base = objectClass.file.classes[0];
      auto cls = labelClass.file.classes[0];
      const uint32_t type = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({0, "Ljava.render.osd.Label;", 0, 0});
      std::vector<const TufaClass *> classes{&base, &cls};
      std::vector<VmStaticFields> states;
      CHECK(vmInitializeClass(cls, classes, states).ok);
      VmArena arena;
      const std::vector<TufaNode> tree{
          {0x07, 0, true, 30}, {0x06, 0, true, type}, {0x2b, 0, false, 0}};
      const auto result =
          vmExecute(cls, tree, {}, nullptr, &classes, &states, &arena);
      CHECK(result.ok &&
            std::holds_alternative<const VmObject *>(result.value));
      if (result.ok) {
        const auto *label = std::get<const VmObject *>(result.value);
        CHECK(
            label && label->fields.size() == 1 &&
            label->fields[0].name == "txt" &&
            label->fields[0].descriptor == "Ljava.render.Text;" &&
            std::holds_alternative<const VmObject *>(label->fields[0].value) &&
            std::get<const VmObject *>(label->fields[0].value) == nullptr);
      }
    }
  }
  {
    const auto menu = tufaLoadFile("ref/raw/sl/Scripts/game/MainMenu.class");
    CHECK(menu.ok);
    if (menu.ok)
      for (const auto &original : menu.file.classes) {
        if (original.name != "java.game.MainMenuDialog")
          continue;
        auto cls = original;
        cls.classHeader[3] = UINT32_MAX;
        for (auto &list : cls.fields) {
          std::vector<TufaField> selected;
          for (const auto &field : list)
            if ((field.words[0] & 8) &&
                (field.words[3] == 50 || field.words[3] == 54))
              selected.push_back(field);
          list = std::move(selected);
        }
        std::vector<const TufaClass *> classes{&cls};
        std::vector<VmStaticFields> states;
        CHECK(vmInitializeClass(cls, classes, states).ok);
        CHECK(states.size() == 1 && states[0].fields.size() == 2);
        if (states.size() == 1 && states[0].fields.size() == 2) {
          CHECK(std::holds_alternative<float>(states[0].fields[0].value));
          CHECK(std::holds_alternative<float>(states[0].fields[1].value));
          uint32_t first, second;
          const float a = std::get<float>(states[0].fields[0].value);
          const float b = std::get<float>(states[0].fields[1].value);
          std::memcpy(&first, &a, sizeof(first));
          std::memcpy(&second, &b, sizeof(second));
          CHECK(first == original.trees[50][1].operand);
          CHECK(second == (original.trees[54][1].operand ^ 0x80000000U));
        }
        std::vector<TufaNode> expression(original.trees[54].begin(),
                                         original.trees[54].end() - 1);
        expression.push_back({0x2b, 0, false, 0});
        const auto result = vmExecute(expression, {});
        CHECK(result.ok && std::holds_alternative<float>(result.value) &&
              std::get<float>(result.value) < 0.0f);
        std::vector<TufaNode> zero(original.trees[55].begin(),
                                   original.trees[55].end() - 1);
        zero.push_back({0x2b, 0, false, 0});
        const auto spacing = vmExecute(zero, {});
        CHECK(spacing.ok && std::holds_alternative<float>(spacing.value) &&
              std::get<float>(spacing.value) == 0.0f);
      }
    const auto parsed =
        tufaLoadFile("ref/raw/parts/scripts/enginepart/Battery.class");
    CHECK(parsed.ok && !parsed.file.classes.empty());
    if (parsed.ok && !parsed.file.classes.empty()) {
      auto cls = parsed.file.classes[0];
      cls.classHeader[3] = UINT32_MAX;
      const uint32_t type = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back(
          {0, "Ljava.game.parts.enginepart.Battery;", 0, 0});
      const std::vector<TufaNode> tree{
          {0x07, 0, true, 30}, {0x06, 0, true, type}, {0x2b, 0, false, 0}};
      std::vector<const TufaClass *> classes{&cls};
      std::vector<VmStaticFields> states{{&cls, {}}};
      VmArena arena;
      const auto result =
          vmExecute(cls, tree, {}, nullptr, &classes, &states, &arena);
      CHECK(result.ok &&
            std::holds_alternative<const VmObject *>(result.value));
      if (result.ok) {
        const auto *object = std::get<const VmObject *>(result.value);
        CHECK(object && object->fields.size() == 1 &&
              object->fields[0].name == "power" &&
              std::get<float>(object->fields[0].value) == 1.0f);
      }
    }
  }
  {
    const auto objects =
        tufaLoadFile("ref/raw/system/Scripts/lang/Object.class");
    const auto integers =
        tufaLoadFile("ref/raw/system/Scripts/lang/Integer.class");
    CHECK(objects.ok && integers.ok && !objects.file.classes.empty() &&
          !integers.file.classes.empty());
    if (objects.ok && integers.ok && !objects.file.classes.empty() &&
        !integers.file.classes.empty()) {
      const auto &base = objects.file.classes[0];
      auto integer = integers.file.classes[0];
      const uint32_t type = static_cast<uint32_t>(integer.constants.size());
      integer.constants.push_back({0, "Ljava.lang.Integer;", 0, 0});
      const std::vector<TufaNode> tree{
          {0x07, 0, true, 30}, {0x06, 0, true, type}, {0x2b, 0, false, 0}};
      std::vector<const TufaClass *> classes{&base, &integer};
      std::vector<VmStaticFields> states;
      CHECK(vmInitializeClass(integer, classes, states).ok);
      CHECK(states.size() == 2 && states[0].owner == &base &&
            states[1].owner == &integer);
      if (states.size() == 2) {
        CHECK(states[1].fields.size() == 2);
        if (states[1].fields.size() == 2) {
          CHECK(states[1].fields[0].name == "MIN_VALUE" &&
                std::get<int32_t>(states[1].fields[0].value) == INT32_MIN);
          CHECK(states[1].fields[1].name == "MAX_VALUE" &&
                std::get<int32_t>(states[1].fields[1].value) == INT32_MAX);
        }
      }
      CHECK(vmInitializeClass(integer, classes, states).ok &&
            states.size() == 2);
      auto unsupported = integer;
      unsupported.trees[0][0].operand = 29;
      std::vector<const TufaClass *> badClasses{&base, &unsupported};
      std::vector<VmStaticFields> badStates;
      CHECK(!vmInitializeClass(unsupported, badClasses, badStates).ok &&
            badStates.empty());
      unsupported = integer;
      unsupported.classHeader[3] = unsupported.classHeader[2];
      CHECK(!vmInitializeClass(unsupported, badClasses, badStates).ok &&
            badStates.empty());
      unsupported = integer;
      unsupported.fields[0].push_back(unsupported.fields[0][0]);
      CHECK(!vmInitializeClass(unsupported, badClasses, badStates).ok &&
            badStates.empty());
      VmArena arena;
      const auto first =
          vmExecute(integer, tree, {}, nullptr, &classes, &states, &arena);
      CHECK(first.ok && std::holds_alternative<const VmObject *>(first.value));
      const auto *firstObject =
          first.ok ? std::get<const VmObject *>(first.value) : nullptr;
      CHECK(firstObject && firstObject->runtimeClass == &integer &&
            firstObject->fields.size() == 1 &&
            firstObject->fields[0].name == "number" &&
            std::get<int32_t>(firstObject->fields[0].value) == 0);
      const std::vector<TufaNode> construct{
          {0x07, 0, true, 30}, {0x06, 0, true, type}, {0x27, 0, true, 42},
          {0x27, 0, true, 1},  {0x21, 0, false, 0},   {0x2b, 0, false, 0}};
      const auto constructed =
          vmExecute(integer, construct, {}, nullptr, &classes, &states, &arena);
      CHECK(constructed.ok &&
            std::holds_alternative<const VmObject *>(constructed.value));
      if (constructed.ok) {
        const auto *value = std::get<const VmObject *>(constructed.value);
        CHECK(value && std::get<int32_t>(value->fields[0].value) == 42);
      }
      auto badCount = construct;
      badCount[3].operand = 2;
      CHECK(
          !vmExecute(integer, badCount, {}, nullptr, &classes, &states, &arena)
               .ok);
      std::vector<TufaNode> recursive{{0x27, 0, true, 0}, {0x23, 0, false, 0}};
      auto fake = integer;
      fake.classHeader[3] = fake.classHeader[2];
      VmObject impostor{&fake, {}};
      CHECK(!vmExecute(fake, recursive, {&impostor}, nullptr, &classes, &states,
                       &arena)
                 .ok);
      for (int i = 0; i < 100; ++i)
        CHECK(vmExecute(integer, tree, {}, nullptr, &classes, &states, &arena)
                  .ok);
      CHECK(arena.objects[0].get() == firstObject &&
            arena.objects[1].get() != firstObject);
      const auto before = arena.objects.size();
      classes.erase(classes.begin());
      CHECK(
          !vmExecute(integer, tree, {}, nullptr, &classes, &states, &arena).ok);
      CHECK(arena.objects.size() == before);
      classes.insert(classes.begin(), &base);
      for (auto &list : integer.fields)
        for (auto &field : list)
          if (!(field.words[0] & 8))
            field.words[3] = 2;
      CHECK(
          !vmExecute(integer, tree, {}, nullptr, &classes, &states, &arena).ok);
      CHECK(arena.objects.size() == before);
      for (auto &list : integer.fields)
        for (auto &field : list)
          if (!(field.words[0] & 8))
            field.words[3] = UINT32_MAX;
      const auto parent = integer.classHeader[3];
      integer.classHeader[3] = integer.classHeader[2];
      CHECK(
          !vmExecute(integer, tree, {}, nullptr, &classes, &states, &arena).ok);
      integer.classHeader[3] = parent;
      arena.limit = before;
      CHECK(
          !vmExecute(integer, tree, {}, nullptr, &classes, &states, &arena).ok);
      CHECK(arena.objects.size() == before);
    }
  }
  {
    const auto parsed =
        tufaLoadFile("ref/raw/system/Scripts/lang/Integer.class");
    CHECK(parsed.ok && !parsed.file.classes.empty());
    if (parsed.ok && !parsed.file.classes.empty()) {
      const auto &cls = parsed.file.classes[0];
      const std::vector<TufaNode> *tree = nullptr;
      for (const auto &list : cls.methods)
        for (const auto &method : list)
          if (cls.constants[method.words[1]].text == "setValue" &&
              cls.constants[method.words[2]].text == "(I)V" &&
              method.words[3] < cls.trees.size())
            tree = &cls.trees[method.words[3]];
      CHECK(tree);
      if (tree) {
        VmObject object{&cls, {{"number", int32_t{3}}}};
        VmObject other{&cls, {{"number", int32_t{9}}}};
        const auto result = vmExecute(cls, *tree, {&object, int32_t{42}});
        CHECK(result.ok && std::get<int32_t>(object.fields[0].value) == 42);
        CHECK(std::get<int32_t>(other.fields[0].value) == 9);
        object.fields.clear();
        CHECK(!vmExecute(cls, *tree, {&object, int32_t{42}}).ok);
        object.fields.push_back({"number", &other});
        CHECK(!vmExecute(cls, *tree, {&object, int32_t{42}}).ok);
        object.fields[0].value = int32_t{3};
        CHECK(!vmExecute(cls, *tree, {&object, &other}).ok);
      }
    }
  }
  {
    const auto parsed = tufaLoadFile(
        "ref/raw/system/Scripts/render/osd/dialog/MenuDialog.class");
    CHECK(parsed.ok && !parsed.file.classes.empty());
    if (parsed.ok && !parsed.file.classes.empty()) {
      const auto &cls = parsed.file.classes[0];
      const TufaNode *compare = nullptr;
      for (const auto &list : cls.methods)
        for (const auto &method : list) {
          if (cls.constants[method.words[1]].text != "osdCommand" ||
              method.words[3] >= cls.trees.size())
            continue;
          for (const auto &node : cls.trees[method.words[3]])
            if (node.op == 0x07 && node.wide && node.operand == 11)
              compare = &node;
        }
      CHECK(compare);
      if (compare) {
        std::vector<TufaNode> tree{{0x01, 0, true, 0},
                                   {0x01, 0, true, 1},
                                   *compare,
                                   {0x2b, 0, false, 0}};
        for (uint32_t op = 8; op <= 11; ++op) {
          tree[2].operand = op;
          const auto less = vmExecute(tree, {int32_t{-1}, int32_t{0}});
          const auto equal = vmExecute(tree, {int32_t{0}, int32_t{0}});
          const auto greater = vmExecute(tree, {int32_t{1}, int32_t{0}});
          CHECK(less.ok &&
                std::get<int32_t>(less.value) == (op == 9 || op == 11));
          CHECK(equal.ok &&
                std::get<int32_t>(equal.value) == (op == 8 || op == 9));
          CHECK(greater.ok &&
                std::get<int32_t>(greater.value) == (op == 8 || op == 10));
        }
        VmObject object;
        CHECK(!vmExecute(tree, {&object, int32_t{0}}).ok);
      }
    }
  }
  {
    // 0x1007 case 0xd (`>>`, stock FUN_0041aff0) and case 0xe (`<<`, stock
    // FUN_0041b0e0): integral operands only, count masked with 0x1f, and the
    // right shift is arithmetic. Negative values and a count above 31 are
    // covered because the mask is the only clamp stock applies.
    std::vector<TufaNode> tree{{0x01, 0, true, 0},
                               {0x01, 0, true, 1},
                               {0x07, 13, true, 0},
                               {0x2b, 0, false, 0}};
    const auto shiftRight = [&](int32_t value, int32_t count) {
      tree[2].operand = 13;
      return vmExecute(tree, {int32_t{value}, int32_t{count}});
    };
    const auto shiftLeft = [&](int32_t value, int32_t count) {
      tree[2].operand = 14;
      return vmExecute(tree, {int32_t{value}, int32_t{count}});
    };
    // Arithmetic right shift keeps the sign.
    CHECK(shiftRight(-16, 2).ok &&
          std::get<int32_t>(shiftRight(-16, 2).value) == -4);
    CHECK(shiftRight(16, 2).ok &&
          std::get<int32_t>(shiftRight(16, 2).value) == 4);
    // The count is masked with 0x1f: 33 behaves as 1.
    CHECK(shiftRight(-8, 33).ok &&
          std::get<int32_t>(shiftRight(-8, 33).value) == -4);
    CHECK(shiftLeft(1, 4).ok && std::get<int32_t>(shiftLeft(1, 4).value) == 16);
    CHECK(shiftLeft(1, 33).ok &&
          std::get<int32_t>(shiftLeft(1, 33).value) == 2);
    // Left shift wraps like stock's 32-bit shift.
    CHECK(shiftLeft(1, 31).ok &&
          std::get<int32_t>(shiftLeft(1, 31).value) == INT32_MIN);
    // Non-integral operands are refused, as stock's "non integral operands"
    // diagnostic does.
    tree[2].operand = 13;
    CHECK(!vmExecute(tree, {1.5f, int32_t{1}}).ok);
    CHECK(!vmExecute(tree, {int32_t{1}, 1.5f}).ok);
  }
  {
    const auto parsed = tufaLoadFile("ref/raw/sl/Scripts/game/Roc.class");
    CHECK(parsed.ok && !parsed.file.classes.empty());
    if (parsed.ok && !parsed.file.classes.empty()) {
      const auto &cls = parsed.file.classes[0];
      const std::vector<TufaNode> *tree = nullptr;
      for (const auto &list : cls.methods)
        for (const auto &method : list)
          if (cls.constants[method.words[1]].text == "isFinalRound" &&
              cls.constants[method.words[2]].text == "()I" &&
              method.words[3] < cls.trees.size())
            tree = &cls.trees[method.words[3]];
      CHECK(tree);
      if (tree) {
        VmObject roc{&cls, {{"actRound", int32_t{3}}}};
        std::vector<VmStaticFields> state{{&cls, {{"ROUNDS", int32_t{4}}}}};
        const auto final =
            vmExecute(cls, *tree, {&roc}, nullptr, nullptr, &state);
        CHECK(final.ok && std::get<int32_t>(final.value) == 1);
        roc.fields[0].value = int32_t{2};
        const auto earlier =
            vmExecute(cls, *tree, {&roc}, nullptr, nullptr, &state);
        CHECK(earlier.ok && std::get<int32_t>(earlier.value) == 0);
        CHECK(!vmExecute(cls, *tree, {&roc}).ok);
        state[0].fields[0].value = &roc;
        CHECK(!vmExecute(cls, *tree, {&roc}, nullptr, nullptr, &state).ok);
        state[0].fields[0].value = int32_t{4};
        state.push_back(state[0]);
        CHECK(!vmExecute(cls, *tree, {&roc}, nullptr, nullptr, &state).ok);
      }
    }
  }
  {
    std::vector<TufaNode> arithmetic = {{0x01, 0, true, 0},
                                        {0x01, 0, true, 1},
                                        {0x07, 0, true, 15},
                                        {0x2b, 0, false, 0}};
    const auto difference = vmExecute(arithmetic, {int32_t{8}, int32_t{3}});
    CHECK(difference.ok && std::get<int32_t>(difference.value) == 5);
    arithmetic[2].operand = 16;
    const auto sum = vmExecute(arithmetic, {int32_t{8}, int32_t{3}});
    CHECK(sum.ok && std::get<int32_t>(sum.value) == 11);
    const auto wrap = vmExecute(arithmetic, {int32_t{2147483647}, int32_t{1}});
    CHECK(wrap.ok && std::get<int32_t>(wrap.value) == INT32_MIN);
    VmObject object;
    CHECK(!vmExecute(arithmetic, {&object, int32_t{1}}).ok);
    arithmetic.erase(arithmetic.begin());
    CHECK(!vmExecute(arithmetic, {int32_t{1}, int32_t{2}}).ok);
  }
  {
    const auto mainMenu =
        tufaLoadFile("ref/raw/sl/Scripts/game/MainMenu.class");
    const auto system =
        tufaLoadFile("ref/raw/system/Scripts/lang/System.class");
    CHECK(mainMenu.ok && system.ok && !system.file.classes.empty());
    if (mainMenu.ok && system.ok && !system.file.classes.empty()) {
      const TufaClass *menu = nullptr;
      for (const auto &cls : mainMenu.file.classes)
        if (cls.name == "java.game.MainMenuDialog")
          menu = &cls;
      CHECK(menu);
      if (menu) {
        const std::vector<TufaNode> *show = nullptr;
        for (const auto &list : menu->methods)
          for (const auto &method : list)
            if (menu->constants[method.words[1]].text == "show" &&
                menu->constants[method.words[2]].text == "()V" &&
                method.words[3] < menu->trees.size())
              show = &menu->trees[method.words[3]];
        CHECK(show && show->size() > 155);
        if (show && show->size() > 155) {
          std::vector<TufaNode> caller(show->begin() + 151,
                                       show->begin() + 156);
          caller.push_back({0x2b, 0, false, 0});
          std::vector<const TufaClass *> classes{&system.file.classes[0]};
          const auto result =
              vmExecute(*menu, caller, {}, &nativeRegistry(), &classes);
          CHECK(result.ok && std::get<int32_t>(result.value) == 606);
          CHECK(!vmExecute(*menu, caller, {}, &nativeRegistry()).ok);
          std::vector<const TufaClass *> missing;
          CHECK(!vmExecute(*menu, caller, {}, &nativeRegistry(), &missing).ok);
          classes.push_back(classes[0]);
          CHECK(!vmExecute(*menu, caller, {}, &nativeRegistry(), &classes).ok);
          classes.pop_back();
          caller[4].operand = caller[3].operand;
          CHECK(!vmExecute(*menu, caller, {}, &nativeRegistry(), &classes).ok);
          caller[4] = (*show)[155];
          auto mismatched = *menu;
          mismatched.constants[caller[4].operand].a = mismatched.classHeader[2];
          const auto rebound =
              vmExecute(mismatched, caller, {}, &nativeRegistry(), &classes);
          CHECK(rebound.ok && std::get<int32_t>(rebound.value) == 606);
          std::vector<NativeEntry> wrong = {
              {"java.lang.System", "wrong", "()I", "", nullptr}};
          CHECK(!vmExecute(*menu, caller, {}, &wrong, &classes).ok);
        }
      }
    }
  }
  {
    auto parsedVector =
        tufaLoadFile("ref/raw/system/Scripts/util/Vector.class");
    CHECK(parsedVector.ok && !parsedVector.file.classes.empty());
    if (parsedVector.ok && !parsedVector.file.classes.empty()) {
      const auto &cls = parsedVector.file.classes[0];
      const std::vector<TufaNode> *tree = nullptr;
      for (const auto &list : cls.methods)
        for (const auto &method : list) {
          if (cls.constants[method.words[1]].text == "isEmpty" &&
              cls.constants[method.words[2]].text == "()I" &&
              method.words[3] < cls.trees.size())
            tree = &cls.trees[method.words[3]];
        }
      CHECK(tree);
      if (tree) {
        VmObject object{&cls, {{"elementCount", int32_t{0}}}};
        const auto empty = vmExecute(cls, *tree, {&object});
        CHECK(empty.ok && std::get<int32_t>(empty.value) == 1);
        object.fields[0].value = int32_t{3};
        const auto nonempty = vmExecute(cls, *tree, {&object});
        CHECK(nonempty.ok && std::get<int32_t>(nonempty.value) == 0);
        object.fields.clear();
        CHECK(!vmExecute(cls, *tree, {&object}).ok);
        object.fields.push_back({"elementCount", &object});
        CHECK(!vmExecute(cls, *tree, {&object}).ok);
        object.runtimeClass = nullptr;
        CHECK(!vmExecute(cls, *tree, {&object}).ok);
      }
    }
  }
  {
    auto parsedSystem =
        tufaLoadFile("ref/raw/system/Scripts/lang/System.class");
    CHECK(parsedSystem.ok && !parsedSystem.file.classes.empty());
    if (parsedSystem.ok && !parsedSystem.file.classes.empty()) {
      auto cls = parsedSystem.file.classes[0];
      uint32_t name = 0, signature = 0;
      for (const auto &list : cls.methods)
        for (const auto &method : list) {
          if (cls.constants[method.words[1]].text == "buildNumber") {
            CHECK(method.words[0] == 0x49);
            name = method.words[1];
            signature = method.words[2];
          }
        }
      CHECK(name && signature && cls.constants[signature].text == "()I");
      const uint32_t ownerText = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({0, cls.name, 0, 0});
      const uint32_t owner = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({4, {}, ownerText, 0});
      const uint32_t pair = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({7, {}, name, signature});
      const uint32_t ref = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({5, {}, owner, pair});
      std::vector<TufaNode> caller = {{0x27, 0, true, 0},
                                      {0x24, 0, false, 0},
                                      {0x1a, 0, true, ref},
                                      {0x2b, 0, false, 0}};
      const auto result = vmExecute(cls, caller, {}, &nativeRegistry());
      CHECK(result.ok && std::get<int32_t>(result.value) == 606);
      CHECK(!vmExecute(cls, caller, {}).ok);
      caller[0].operand = 1;
      CHECK(vmExecute(cls, caller, {}, &nativeRegistry()).ok);
      std::vector<NativeEntry> wrong = {
          {"java.lang.System", "wrong", "()I", "", nullptr}};
      CHECK(!vmExecute(cls, caller, {}, &wrong).ok);
      for (auto &list : cls.methods)
        for (auto &method : list) {
          if (method.words[1] == name)
            method.words[0] &= ~uint32_t{8};
        }
      CHECK(!vmExecute(cls, caller, {}, &nativeRegistry()).ok);
    }
  }
  {
    auto parsedClass =
        tufaLoadFile("ref/raw/system/Scripts/lang/GameType.class");
    CHECK(parsedClass.ok && !parsedClass.file.classes.empty());
    if (parsedClass.ok && !parsedClass.file.classes.empty()) {
      auto cls = parsedClass.file.classes[0];
      uint32_t name = 0, signature = 0;
      for (const auto &list : cls.methods)
        for (const auto &method : list) {
          if (cls.constants[method.words[1]].text == "getInfo" &&
              cls.constants[method.words[2]].text == "(I)I") {
            name = method.words[1];
            signature = method.words[2];
          }
        }
      CHECK(name && signature);
      const uint32_t ownerText = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({0, cls.name, 0, 0});
      const uint32_t owner = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({4, {}, ownerText, 0});
      const uint32_t pair = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({7, {}, name, signature});
      const uint32_t ref = static_cast<uint32_t>(cls.constants.size());
      cls.constants.push_back({5, {}, owner, pair});
      std::vector<TufaNode> caller = {{0x27, 0, true, 42}, {0x27, 0, true, 1},
                                      {0x24, 0, false, 0}, {0x1a, 0, true, ref},
                                      {0x07, 0, true, 27}, {0x2b, 0, false, 0}};
      VmObject receiver{&cls, {}};
      const auto result = vmExecute(cls, caller, {&receiver});
      CHECK(result.ok && std::get<int32_t>(result.value) == 0);
      caller[1].operand = 0;
      CHECK(vmExecute(cls, caller, {&receiver}).ok);
      caller[1].operand = 1;
      CHECK(!vmExecute(cls, caller, {}).ok);
      VmObject unknownClass;
      CHECK(!vmExecute(cls, caller, {&unknownClass}).ok);
    }
  }
  const auto parsed = tufaLoadFile("ref/raw/system/Scripts/lang/Object.class");
  CHECK(parsed.ok);
  if (!parsed.ok)
    return 1;
  const std::vector<TufaNode> *tree = nullptr;
  for (const auto &cls : parsed.file.classes) {
    for (const auto &list : cls.methods) {
      for (const auto &method : list) {
        const auto name = method.words[1];
        const auto descriptor = method.words[2];
        const auto index = method.words[3];
        if (name < cls.constants.size() && descriptor < cls.constants.size() &&
            cls.constants[name].text == "equals" &&
            cls.constants[descriptor].text == "(Ljava.lang.Object;)I" &&
            index < cls.trees.size()) {
          tree = &cls.trees[index];
        }
      }
    }
  }
  CHECK(tree != nullptr);
  if (!tree)
    return 1;
  VmObject a, b;
  const auto same = vmExecute(*tree, {&a, &a});
  const auto different = vmExecute(*tree, {&a, &b});
  CHECK(same.ok && std::get_if<int32_t>(&same.value) &&
        std::get<int32_t>(same.value) == 1);
  CHECK(different.ok && std::get_if<int32_t>(&different.value) &&
        std::get<int32_t>(different.value) == 0);
  auto unsupported = *tree;
  unsupported[0].op = 0xff;
  CHECK(!vmExecute(unsupported, {&a, &b}).ok);
  CHECK(std::get<int32_t>(vmExecute(*tree, {int32_t{1}, int32_t{1}}).value) ==
        1);
  CHECK(std::get<int32_t>(vmExecute(*tree, {int32_t{1}, int32_t{2}}).value) ==
        0);
  CHECK(!vmExecute(*tree, {int32_t{1}, &a}).ok);
  auto notEqual = *tree;
  notEqual[2].operand = 6;
  CHECK(std::get<int32_t>(
            vmExecute(notEqual, {int32_t{1}, int32_t{2}}).value) == 1);
  CHECK(std::get<int32_t>(
            vmExecute(notEqual, {int32_t{1}, int32_t{1}}).value) == 0);
  CHECK(std::get<int32_t>(vmExecute(notEqual, {&a, &b}).value) == 1);
  CHECK(std::get<int32_t>(vmExecute(notEqual, {&a, &a}).value) == 0);
  const std::vector<TufaNode> immediate{{0x27, 0, true, 0xffffffff},
                                        {0x2b, 0, false, 0}};
  const auto immediateResult = vmExecute(immediate, {});
  CHECK(immediateResult.ok && std::get_if<int32_t>(&immediateResult.value) &&
        std::get<int32_t>(immediateResult.value) == -1);
  const std::vector<TufaNode> conditional{
      {0x01, 0, true, 0},  {0x15, 0, true, 3}, {0x27, 0, true, 7},
      {0x2b, 0, false, 0}, {0x27, 0, true, 9}, {0x2b, 0, false, 0}};
  const auto zero = vmExecute(conditional, {int32_t{0}});
  const auto nonzero = vmExecute(conditional, {int32_t{1}});
  CHECK(zero.ok && std::get<int32_t>(zero.value) == 9);
  CHECK(nonzero.ok && std::get<int32_t>(nonzero.value) == 7);
  auto branchTrue = conditional;
  branchTrue[1].op = 0x16;
  CHECK(std::get<int32_t>(vmExecute(branchTrue, {int32_t{1}}).value) == 9);
  CHECK(std::get<int32_t>(vmExecute(branchTrue, {int32_t{0}}).value) == 7);
  const VmObject *nullObject = nullptr;
  CHECK(std::get<int32_t>(vmExecute(conditional, {nullObject}).value) == 9);
  CHECK(std::get<int32_t>(vmExecute(conditional, {&a}).value) == 7);
  auto badBranch = conditional;
  badBranch[1].operand = 0xffffffff;
  badBranch[1].op = 0x14;
  badBranch[0].op = 0x14;
  badBranch[0].operand = 0xffffffff;
  CHECK(!vmExecute(badBranch, {}).ok);
  const std::vector<TufaNode> backward{{0x14, 0, true, 3},
                                       {0x27, 0, true, 12},
                                       {0x2b, 0, false, 0},
                                       {0x14, 0, true, 0xfffffffe}};
  const auto backwardResult = vmExecute(backward, {});
  CHECK(backwardResult.ok && std::get<int32_t>(backwardResult.value) == 12);
  // Short-circuit operations 04 (and) and 05 (or), in the shape the stock
  // Frontend.setFonts uses:
  // `(l0 >= 1024) || (l1 >= 768)`. The left value stays on the stack as a truth
  // value; when it decides the result the forward jump skips the right operand
  // and the combining 07 node, otherwise 07 (1 = or, 5 = and) joins both.
  const auto shortCircuit = [](uint8_t test, uint32_t combine) {
    return std::vector<TufaNode>{
        {0x01, 0, true, 0}, {0x07, 0, true, 28},      {0x0b, 0, true, 1024},
        {0x07, 0, true, 8}, {test, 0, false, 0},      {0x14, 0, true, 6},
        {0x01, 0, true, 1}, {0x07, 0, true, 28},      {0x0b, 0, true, 768},
        {0x07, 0, true, 8}, {0x07, 0, true, combine}, {0x2b, 0, false, 0}};
  };
  const auto truth = [](const VmResult &result) {
    return result.ok && std::get_if<int32_t>(&result.value)
               ? std::get<int32_t>(result.value)
               : -1;
  };
  const auto orTree = shortCircuit(0x05, 1);
  CHECK(truth(vmExecute(orTree, {int32_t{2000}, int32_t{0}})) == 1);
  CHECK(truth(vmExecute(orTree, {int32_t{0}, int32_t{1000}})) == 1);
  CHECK(truth(vmExecute(orTree, {int32_t{0}, int32_t{0}})) == 0);
  const auto andTree = shortCircuit(0x04, 5);
  CHECK(truth(vmExecute(andTree, {int32_t{2000}, int32_t{1000}})) == 1);
  CHECK(truth(vmExecute(andTree, {int32_t{2000}, int32_t{0}})) == 0);
  CHECK(truth(vmExecute(andTree, {int32_t{0}, int32_t{1000}})) == 0);
  // The right operand is not evaluated when the left decides: local 1 is
  // missing here, and that would be an error.
  CHECK(truth(vmExecute(orTree, {int32_t{2000}})) == 1);
  CHECK(truth(vmExecute(andTree, {int32_t{0}})) == 0);
  CHECK(!vmExecute(orTree, {int32_t{0}}).ok);
  // The test converts a float or a non-null reference in place to 0 or 1, and
  // 04/05 on an empty stack are errors.
  const std::vector<TufaNode> floatOr{{0x01, 0, true, 0},
                                      {0x05, 0, false, 0},
                                      {0x14, 0, true, 2},
                                      {0x27, 0, true, 7},
                                      {0x2b, 0, false, 0}};
  CHECK(truth(vmExecute(floatOr, {0.5f})) == 1 &&
        truth(vmExecute(floatOr, {&a})) == 1);
  CHECK(truth(vmExecute(floatOr, {0.0f})) == 7 &&
        truth(vmExecute(floatOr, {nullObject})) == 7);
  CHECK(!vmExecute({{0x04, 0, false, 0}, {0x2b, 0, false, 0}}, {}).ok);
  CHECK(!vmExecute({{0x04, 0, true, 0}, {0x2b, 0, false, 0}}, {}).ok);
  // Suboperation 1 is logical or; 3 and 5 are bitwise or and and.
  const std::vector<TufaNode> bitOr{{0x27, 0, true, 6},
                                    {0x27, 0, true, 3},
                                    {0x07, 0, true, 3},
                                    {0x2b, 0, false, 0}};
  const std::vector<TufaNode> bitAnd{{0x27, 0, true, 6},
                                     {0x27, 0, true, 3},
                                     {0x07, 0, true, 5},
                                     {0x2b, 0, false, 0}};
  CHECK(truth(vmExecute(bitOr, {})) == 7 && truth(vmExecute(bitAnd, {})) == 2);
  auto bitXor = bitOr;
  bitXor[2].operand = 4;
  CHECK(truth(vmExecute(bitXor, {})) == 5);
  const std::vector<TufaNode> logicalOr{{0x27, 0, true, 6},
                                        {0x27, 0, true, 3},
                                        {0x07, 0, true, 1},
                                        {0x2b, 0, false, 0}};
  CHECK(truth(vmExecute(logicalOr, {})) == 1);
  const std::vector<TufaNode> bitFloat{{0x27, 0, true, 6},
                                       {0x07, 0, true, 28},
                                       {0x0a, 0, true, 0x3f800000},
                                       {0x07, 0, true, 1},
                                       {0x2b, 0, false, 0}};
  CHECK(truth(vmExecute(bitFloat, {})) == 1);
  const std::vector<TufaNode> logicalReference{{0x01, 0, true, 0},
                                               {0x27, 0, true, 0},
                                               {0x07, 0, true, 1},
                                               {0x2b, 0, false, 0}};
  CHECK(truth(vmExecute(logicalReference, {nullObject})) == 0);
  CHECK(truth(vmExecute(logicalReference, {&a})) == 1);
  // Wide 07 operand 16 string concatenation follows stock FUN_0041b4d0:
  // FUN_00417a70 converts a non-String side in place via FUN_00417da0 — an
  // int formats as "%d" (0x60ba84), a float as "%.3f" (0x60ba9c) — then the
  // L-arm concatenates the two String operands' bytes. The shape mirrors
  // DiscBrake.calcStuffs line 109, which adds the float field `torque` to a
  // String.
  {
    auto stringFile = tufaLoadFile("ref/raw/system/Scripts/lang/String.class");
    const auto nativeFile =
        tufaLoadFile("ref/raw/system/Scripts/lang/Native.class");
    const auto objectFile =
        tufaLoadFile("ref/raw/system/Scripts/lang/Object.class");
    CHECK(stringFile.ok && !stringFile.file.classes.empty());
    CHECK(nativeFile.ok && !nativeFile.file.classes.empty());
    CHECK(objectFile.ok && !objectFile.file.classes.empty());
    if (stringFile.ok && !stringFile.file.classes.empty() && nativeFile.ok &&
        !nativeFile.file.classes.empty() && objectFile.ok &&
        !objectFile.file.classes.empty()) {
      auto &stringClass = stringFile.file.classes[0];
      const auto &nativeClass = nativeFile.file.classes[0];
      const auto &objectClass = objectFile.file.classes[0];
      std::vector<const TufaClass *> concatClasses{&objectClass, &nativeClass,
                                                   &stringClass};
      std::vector<VmStaticFields> concatStates{{&stringClass, {}}};
      VmArena concatArena;
      CHECK(vmInitializeClass(stringClass, concatClasses, concatStates, nullptr,
                              &concatArena)
                .ok);
      auto intern = [&](const std::string &text) {
        const auto index = static_cast<uint32_t>(stringClass.constants.size());
        stringClass.constants.push_back(TufaConstant{0, text, 0, 0});
        const auto value = vmExecute(
            stringClass,
            {{0x07, 0, true, 28}, {0x09, 0, true, index}, {0x2b, 0, false, 0}},
            {}, nullptr, &concatClasses, &concatStates, &concatArena);
        CHECK(value.ok);
        const auto *object = std::get_if<const VmObject *>(&value.value);
        return object ? *object : nullptr;
      };
      const VmObject *const prefix = intern("torque: ");
      CHECK(prefix != nullptr);
      std::vector<VmValue> concatLocals{prefix,
                                        static_cast<const VmObject *>(nullptr)};
      auto concat = [&](std::vector<TufaNode> body) {
        body.push_back({0x2b, 0, false, 0});
        return vmExecute(stringClass, body, concatLocals, nullptr,
                         &concatClasses, &concatStates, &concatArena);
      };
      const auto textOf = [&](const VmResult &result) {
        if (!result.ok)
          return std::string{"<error: " + result.error + ">"};
        const auto *object = std::get_if<const VmObject *>(&result.value);
        if (!object || !*object)
          return std::string{"<null>"};
        for (const auto &item : concatArena.strings)
          if (item.second == *object)
            return item.first;
        return std::string{"<unknown>"};
      };
      const auto intLit = [](int32_t value) {
        return TufaNode{0x27, 0, true, static_cast<uint32_t>(value)};
      };
      const auto floatLit = [](float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return std::vector<TufaNode>{{0x07, 0, true, 28},
                                     {0x0a, 0, true, bits}};
      };
      const auto pushPrefix = [&]() {
        return std::vector<TufaNode>{{0x01, 0, true, 0}};
      };
      const auto pushNull = [&]() {
        return std::vector<TufaNode>{{0x01, 0, true, 1}};
      };
      auto append = [](std::vector<TufaNode> a,
                       const std::vector<TufaNode> &b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
      };
      // String + float: "torque: " + 12.5f -> "torque: 12.500" (%.3f).
      CHECK(textOf(concat(append(append(pushPrefix(), floatLit(12.5f)),
                                 {{0x07, 0, true, 16}}))) == "torque: 12.500");
      // Float + String keeps the stock operand order.
      CHECK(textOf(concat(append(append(floatLit(-0.25f), pushPrefix()),
                                 {{0x07, 0, true, 16}}))) == "-0.250torque: ");
      // String + int formats the int as %d.
      CHECK(textOf(concat(append(append(pushPrefix(), {intLit(-42)}),
                                 {{0x07, 0, true, 16}}))) == "torque: -42");
      // A null reference operand contributes nothing, as stock FUN_0041b4d0
      // skips a zero pointer.
      CHECK(textOf(concat(append(append(pushPrefix(), pushNull()),
                                 {{0x07, 0, true, 16}}))) == "torque: ");
      // A non-String, non-null object operand is rejected: stock converts it
      // via FUN_00417da0, which only handles I/F/L/[ targets, so an arbitrary
      // object type has no String conversion and the concat fails.
      {
        auto &other = *new TufaClass();
        other.name = "test.Other";
        other.hasClassHeader = true;
        other.constants.push_back(TufaConstant{0, "test.Other", 0, 0});
        other.classHeader[3] = UINT32_MAX;
        concatClasses.push_back(&other);
        concatStates.push_back({&other, {}});
        CHECK(vmInitializeClass(other, concatClasses, concatStates, nullptr,
                                &concatArena)
                  .ok);
        auto object = std::make_unique<VmObject>();
        object->runtimeClass = &other;
        const VmObject *const otherObject = object.get();
        concatArena.objects.push_back(std::move(object));
        concatLocals[1] = otherObject;
        const auto result = concat(
            {{0x01, 0, true, 0}, {0x01, 0, true, 1}, {0x07, 0, true, 16}});
        CHECK(!result.ok);
        concatLocals[1] = static_cast<const VmObject *>(nullptr);
      }
      // Mixed int + float still promotes numerically, never concatenating.
      {
        const auto result = concat(
            append(append({intLit(1)}, floatLit(0.5f)), {{0x07, 0, true, 16}}));
        CHECK(result.ok && std::get_if<float>(&result.value) &&
              std::get<float>(result.value) == 1.5f);
      }
    }
  }
  checkLiteralMethod("ref/raw/system/Scripts/lang/GameType.class", "getInfo",
                     "(I)I", 0);
  checkLiteralMethod("ref/raw/system/Scripts/render/osd/dialog/Dialog.class",
                     "validator", "(Ljava.lang.String;)I", 1);
  if (failures)
    return 1;
  std::puts("[{\"class\":\"java.lang.System\",\"method\":\"buildNumber\","
            "\"descriptor\":\"()I\",\"handler\":\"0047c0b0\",\"coverage\":\"vm-"
            "dispatch\"}]");
  return 0;
}
