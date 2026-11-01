#include "audio.hpp"
#include "camera_device_factory.hpp"
#include "camera_frame.hpp"
#include "car_world_step.hpp"
#include "class_environment.hpp"
#include "class_runtime.hpp"
#include "file_state.hpp"
#include "force_render.hpp"
#include "hotkeys.hpp"
#include "instance_ids.hpp"
#include "music.hpp"
#include "music_control.hpp"
#include "natives/game_type.hpp"
#include "natives/registry.hpp"
#include "object_sound.hpp"
#include "pack_registry.hpp"
#include "platform.hpp"
#include "probe_script.hpp"
#include "render2d.hpp"
#include "resource_world.hpp"
#include "run_log.hpp"
#include "sfx.hpp"
#include "timers.hpp"
#include "video_player.hpp"
#include "vm.hpp"
#include "world_model.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static int probeMain(int argc, char** argv, const std::string& runDir) {
  const auto processClockOrigin = std::chrono::steady_clock::now();
  std::string root = "ref/raw", dataRoot, referenceRoot = "ref";
  std::string captureFrame, scriptPath;
  // Test hook: --crash-on-key NAME[:segv|abort|error] fails when that key goes
  // down (see docs/run-log.md).
  SDL_Scancode crashKey = SDL_SCANCODE_UNKNOWN;
  std::string crashMode = "segv", crashKeyName;
  bool constructor = false, initProbe = false, haveRoot = false,
       noInput = false, useRenderer = false;
  bool renderFrame = false, visibleWindow = false, runLoop = false,
       holdFrame = false, captureOnError = false;
  std::vector<std::string> extraNames;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--init" && !initProbe) {
      initProbe = true;
      constructor = true;
    } else if (arg == "--no-input" && !noInput)
      noInput = true;
    else if (arg == "--renderer" && !useRenderer)
      useRenderer = true;
    else if (arg == "--frame" && !renderFrame)
      renderFrame = true;
    else if (arg == "--loop" && !runLoop)
      runLoop = true;
    else if (arg == "--visible" && !visibleWindow)
      visibleWindow = true;
    else if (arg == "--hold-frame" && !holdFrame)
      holdFrame = true;
    else if (arg == "--capture-frame" && captureFrame.empty() && i + 1 < argc)
      captureFrame = argv[++i];
    else if (arg == "--capture-on-error" && !captureOnError)
      captureOnError = true;
    else if (arg == "--no-run-log")
      continue; // main() reads it before the recorder starts
    else if (arg == "--script" && scriptPath.empty() && i + 1 < argc)
      scriptPath = argv[++i];
    else if (arg == "--crash-on-key" && crashKey == SDL_SCANCODE_UNKNOWN &&
             i + 1 < argc) {
      crashKeyName = argv[++i];
      const size_t colon = crashKeyName.rfind(':');
      if (colon != std::string::npos) {
        const std::string mode = crashKeyName.substr(colon + 1);
        if (mode == "segv" || mode == "abort" || mode == "error") {
          crashMode = mode;
          crashKeyName.resize(colon);
        }
      }
      crashKey = SDL_GetScancodeFromName(crashKeyName.c_str());
      if (crashKey == SDL_SCANCODE_UNKNOWN) {
        std::fprintf(stderr, "startup probe: --crash-on-key: unknown key %s\n",
                     crashKeyName.c_str());
        return 2;
      }
    } else if (arg == "--constructor" && !constructor)
      constructor = true;
    else if (arg == "--class" && i + 1 < argc)
      extraNames.push_back(argv[++i]);
    else if (arg == "--reference-root" && i + 1 < argc)
      referenceRoot = argv[++i];
    else if (arg == "--data-root" && dataRoot.empty() && i + 1 < argc)
      dataRoot = argv[++i];
    else if (!haveRoot && !arg.empty() && arg[0] != '-') {
      root = arg;
      haveRoot = true;
    } else {
      std::fprintf(stderr,
                   "usage: startup_probe [--constructor|--init] [--no-input] "
                   "[--renderer] [--frame] [--loop] [--visible] [--script "
                   "file] [--crash-on-key NAME[:mode]] [--no-run-log] [--class "
                   "name]... [--data-root directory] [--reference-root directory] [game-root]\n");
      return 2;
    }
  }
  PackRegistry packs(root);
  ClassEnvironment classes(root);
  std::string error;
  uint32_t configId = 0;
  if (!packs.resolveLiteral("system.rpk", 0x47, configId, error)) {
    std::fprintf(stderr, "startup probe: system.rpk Config resource: %s\n",
                 error.c_str());
    return 1;
  }
  PackResource configResource;
  if (!packs.lookup(configId, configResource, error) ||
      configResource.entry.resType != 8 || !configResource.packBytes) {
    std::fprintf(stderr, "startup probe: Config resource lookup: %s\n",
                 error.c_str());
    return 1;
  }
  const auto& bytes = *configResource.packBytes;
  const auto& entry = configResource.entry;
  const std::string payload(bytes.begin() + entry.payloadOffset,
                            bytes.begin() + entry.payloadOffset +
                                entry.payloadSize);
  if (payload != "script java.util.Config\r\n") {
    std::fprintf(stderr,
                 "startup probe: unsupported Config resource payload\n");
    return 1;
  }
  VmRandomState mainRandom;
  HotkeyState hotkeys;
  TimerState timers;
  VmArena arena;
  arena.randomState = &mainRandom;
  arena.loadScriptPath =
      [&](const std::string& path,
          std::string& detail) -> std::optional<std::string> {
    const auto* cls = classes.loadScriptPath(path, detail);
    return cls ? std::optional<std::string>(cls->name) : std::nullopt;
  };
  arena.performanceMilliseconds = [&] {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - processClockOrigin)
        .count();
  };
  arena.natives = &nativeRegistry();
  VmResourceResolver resolve = [&](std::string_view name,
                                   uint32_t id) -> std::optional<uint32_t> {
    uint32_t result = 0;
    if (!packs.resolveLiteral(std::string(name), id, result, error)) {
      std::fprintf(stderr, "startup probe: resource literal %.*s 0x%08x: %s\n",
                   static_cast<int>(name.size()), name.data(), id,
                   error.c_str());
      return std::nullopt;
    }
    return result;
  };
  // Stock loads a class when a script first names it and initializes it on
  // first use. The runtime does the same for the VM, so only the classes this
  // probe reads directly are named here.
  ClassRuntime runtime(
      [&](const std::string& name, std::string& loadError) {
        return classes.findOrLoad(name, loadError);
      },
      arena, &resolve);
  const auto loadGraph = [&](const std::string& name) {
    return runtime.load(name, error);
  };
  const TufaClass* config = loadGraph("java.util.Config");
  if (!config) {
    std::fprintf(stderr, "startup probe: class graph: %s\n", error.c_str());
    return 1;
  }
  const TufaClass* stringClass = loadGraph("java.lang.String");
  if (!stringClass) {
    std::fprintf(stderr, "startup probe: String graph: %s\n", error.c_str());
    return 1;
  }
  const TufaClass* fileClass = loadGraph("java.io.File");
  if (!fileClass) {
    std::fprintf(stderr, "startup probe: File graph: %s\n", error.c_str());
    return 1;
  }
  const TufaClass* soundClass =
      constructor ? loadGraph("java.sound.Sound") : nullptr;
  if (constructor && !soundClass) {
    std::fprintf(stderr, "startup probe: Sound graph: %s\n", error.c_str());
    return 1;
  }
  const TufaClass* systemClass =
      constructor ? loadGraph("java.lang.System") : nullptr;
  if (constructor && !systemClass) {
    std::fprintf(stderr, "startup probe: System graph: %s\n", error.c_str());
    return 1;
  }
  const TufaClass* gameInit = initProbe ? loadGraph("java.game.Init") : nullptr;
  if (initProbe && !gameInit) {
    std::fprintf(stderr, "startup probe: Init graph: %s\n", error.c_str());
    return 1;
  }
  const TufaClass* dummyClass =
      initProbe ? loadGraph("java.util.resource.Dummy") : nullptr;
  if (initProbe && !dummyClass) {
    std::fprintf(stderr, "startup probe: Dummy graph: %s\n", error.c_str());
    return 1;
  }
  const TufaClass* gameRef =
      initProbe ? loadGraph("java.util.resource.GameRef") : nullptr;
  if (initProbe && !gameRef) {
    std::fprintf(stderr, "startup probe: GameRef graph: %s\n", error.c_str());
    return 1;
  }
  std::vector<const TufaClass*> extraClasses;
  for (const auto& name : extraNames) {
    const TufaClass* extra = loadGraph(name);
    if (!extra) {
      std::fprintf(stderr, "startup probe: %s graph: %s\n", name.c_str(),
                   error.c_str());
      return 1;
    }
    extraClasses.push_back(extra);
  }
  std::vector<VmStaticFields>& states = runtime.staticFields();
  arena.hotkeys = &hotkeys;
  arena.timers = &timers;
  VmMeasurementState measurement{};
  std::unique_ptr<SoundEffects> soundEffects;
  std::unique_ptr<AudioOutput> musicOutput;
  std::unique_ptr<MusicBackend> musicBackend;
  std::unique_ptr<MusicPlayer> musicPlayer;
  std::unique_ptr<MusicPlayerControl> musicControl;
  VmSoundState sound{};
  VmDisplaySize displaySize{};
  arena.displaySize = &displaySize;
  arena.measurementState = &measurement;
  arena.soundState = &sound;
  // The world root is the type-0 `nulltype` node that stock's world constructor
  // (FUN_00535e70) makes. The host world and the resource world are declared in
  // the order stock needs them destroyed in (reverse of this).
  std::unique_ptr<PlatformInput> platformInput;
  std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> diagnosticWindow(
      nullptr, SDL_DestroyWindow);
  std::unique_ptr<Render2D> diagnosticRenderer;
  std::unique_ptr<VideoPlayer> videoPlayer;
  GameWorld world;
  world.setClassResolver([&](const std::string& name) {
    std::string ignored;
    return classes.findOrLoad(name, ignored) != nullptr;
  });
  ResourceWorld resources(world, packs);
  FrameVisibilityState frameVisibility;
  VmGameState gameState{&packs, &world, {}, {}, &resources, nullptr, &frameVisibility};
  struct ObjectSoundBinding {
    VmArena& arena;
    std::unique_ptr<ObjectSoundState> state;
    ~ObjectSoundBinding() { arena.objectSound = nullptr; }
  } objectSound{arena, {}};
  std::optional<int32_t> resourceLoadRate;
  std::optional<int32_t> instanceMax, soundMax, textureMax, meshMax;
  arena.gameState = &gameState;
  if (initProbe && !world.addRoot()) {
    std::fprintf(stderr, "startup probe: world root\n");
    return 1;
  }
  if (initProbe && !noInput) {
    platformInput = PlatformInput::create(error);
    if (!platformInput) {
      std::fprintf(stderr, "startup probe: SDL input enumeration: %s\n",
                   error.c_str());
      return 1;
    }
    platformInput->beginFrame();
    hotkeys.lastKey = [&]() { return platformInput->nextKey(); };
    hotkeys.physicalKey = [&](int32_t key) {
      return platformInput->sample(0, key);
    };
    hotkeys.virtualAxis = [&](const VmObject* controller,
                              int32_t axis) -> std::optional<float> {
      const auto* link = nativeResourceState(controller, &arena);
      if (!link)
        return std::nullopt;
      const auto handle =
          static_cast<int32_t>(link - arena.resourceStates.data()) + 1;
      const auto binding = gameState.bindings.find(handle);
      if (binding == gameState.bindings.end() || !binding->second)
        return std::nullopt;
      auto* player = world.nodeFor(binding->second->link.owner);
      if (!player || !player->playerInput)
        return std::nullopt;
      return player->playerInput->sampleAxis(axis);
    };
    for (const auto& device : platformInput->names())
      std::printf("input device: %s\n", device.c_str());
    world.setInputDevices(platformInput->names());
    world.setInputReaders(
        [&](int32_t device, int32_t axis) {
          return platformInput->sample(device, axis);
        },
        [&](int32_t device, int32_t axis) {
          return platformInput->type(device, axis);
        });
    world.setCursorSelector([&](int32_t index, bool visible,
                                std::string& cursorError) {
      return platformInput->selectStockCursor(
          (std::filesystem::path(referenceRoot) / "raw/StreetLegal_Redline.bak.exe").string(),
          index, visible, cursorError);
    });
  }
  if (useRenderer) {
    soundEffects = SoundEffects::create(error);
    if (!soundEffects) {
      std::fprintf(stderr, "startup probe: sound effects: %s\n", error.c_str());
      return 1;
    }
    sound.effects = soundEffects.get();
    ObjectSoundState::Config objectSoundConfig;
    objectSoundConfig.windowClock = [&] {
      return static_cast<float>(gameState.gameTimeOfDay);
    };
    objectSoundConfig.effects = soundEffects.get();
    objectSoundConfig.masterVolume = [&] { return sound.volumes[0]; };
    objectSoundConfig.randomState = &mainRandom.state;
    objectSoundConfig.listener = [&](SoundEffects::Listener& listener,
                                     std::string& detail) {
      if (!world.audioListener(listener.position, listener.forward,
                               listener.up)) {
        detail = "object sound listener unavailable";
        return false;
      }
      detail.clear();
      return true;
    };
    objectSound.state = std::make_unique<ObjectSoundState>(
        timers, std::move(objectSoundConfig));
    arena.objectSound = objectSound.state.get();
    musicOutput = AudioOutput::create(error);
    if (!musicOutput) {
      std::fprintf(stderr, "startup probe: music output: %s\n", error.c_str());
      return 1;
    }
    musicBackend = createAudioToolboxMusicBackend(*musicOutput);
    musicPlayer = MusicPlayer::create(root, *musicBackend,
                                      MusicPlayer::steadyClockTicks(), error);
    if (!musicPlayer) {
      std::fprintf(stderr, "startup probe: music playlists: %s\n",
                   error.c_str());
      return 1;
    }
    musicControl = std::make_unique<MusicPlayerControl>(*musicPlayer);
    sound.music = musicControl.get();
    if (!initProbe || !platformInput) {
      std::fprintf(stderr,
                   "startup probe: --renderer requires --init and SDL input\n");
      return 1;
    }
    diagnosticWindow.reset(SDL_CreateWindow(
        "Startup diagnostic", 640, 480, visibleWindow ? 0 : SDL_WINDOW_HIDDEN));
    if (!diagnosticWindow || !(diagnosticRenderer = Render2D::forWindow(
                                   diagnosticWindow.get(), error))) {
      std::fprintf(stderr, "startup probe: diagnostic renderer: %s\n",
                   diagnosticWindow ? error.c_str() : SDL_GetError());
      return 1;
    }
    gameState.displayModes = platformDisplayModes(error);
    if (!gameState.displayModes) {
      std::fprintf(stderr, "startup probe: display modes: %s\n", error.c_str());
      return 1;
    }
    gameState.currentDisplayMode = [&]() -> std::optional<PlatformDisplayMode> {
      int width = 0, height = 0;
      if (!SDL_GetWindowSizeInPixels(diagnosticWindow.get(), &width, &height))
        return std::nullopt;
      auto format = SDL_GetWindowPixelFormat(diagnosticWindow.get());
      if (format == SDL_PIXELFORMAT_UNKNOWN)
        return std::nullopt;
      return PlatformDisplayMode{
          width, height, static_cast<int32_t>(SDL_BITSPERPIXEL(format))};
    };
    gameState.renderer = diagnosticRenderer.get();
    videoPlayer = std::make_unique<VideoPlayer>(*diagnosticRenderer, root);
    gameState.video = videoPlayer.get();
    std::printf("renderer: %s 640x480 diagnostic window\n",
                visibleWindow ? "visible" : "hidden");
  }
  if ((renderFrame || visibleWindow || runLoop) && !diagnosticRenderer) {
    std::fprintf(
        stderr,
        "startup probe: --frame, --loop and --visible require --renderer\n");
    return 1;
  }
  std::optional<FileState> fileState;
  if (!dataRoot.empty()) {
    fileState.emplace(dataRoot, referenceRoot, root);
    arena.fileState = &*fileState;
  }
  const auto& loaded = runtime.classes();
  const auto clockStart = std::chrono::steady_clock::now();
  VmScheduler scheduler(arena, &nativeRegistry(), &loaded, &states, [&] {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         clockStart)
        .count();
  });
  const auto stringInit =
      vmInitializeClass(*stringClass, loaded, states, &resolve, &arena);
  if (!stringInit.ok) {
    std::fprintf(stderr, "startup probe: String initialization: %s\n",
                 stringInit.error.c_str());
    return 1;
  }
  const auto fileInit =
      vmInitializeClass(*fileClass, loaded, states, &resolve, &arena);
  if (!fileInit.ok) {
    std::fprintf(stderr, "startup probe: File initialization: %s\n",
                 fileInit.error.c_str());
    return 1;
  }
  if (soundClass) {
    const auto soundInit =
        vmInitializeClass(*soundClass, loaded, states, &resolve, &arena);
    if (!soundInit.ok) {
      std::fprintf(stderr, "startup probe: Sound initialization: %s\n",
                   soundInit.error.c_str());
      return 1;
    }
  }
  if (systemClass) {
    const auto systemInit =
        vmInitializeClass(*systemClass, loaded, states, &resolve, &arena);
    if (!systemInit.ok) {
      std::fprintf(stderr, "startup probe: System initialization: %s\n",
                   systemInit.error.c_str());
      return 1;
    }
  }
  if (gameRef) {
    const auto gameRefInit =
        vmInitializeClass(*gameRef, loaded, states, &resolve, &arena);
    if (!gameRefInit.ok) {
      std::fprintf(stderr, "startup probe: GameRef initialization: %s\n",
                   gameRefInit.error.c_str());
      return 1;
    }
  }
  if (dummyClass) {
    const auto dummyInit =
        vmInitializeClass(*dummyClass, loaded, states, &resolve, &arena);
    if (!dummyInit.ok) {
      std::fprintf(stderr, "startup probe: Dummy initialization: %s\n",
                   dummyInit.error.c_str());
      return 1;
    }
  }
  if (gameInit) {
    const auto gameInitResult =
        vmInitializeClass(*gameInit, loaded, states, &resolve, &arena);
    if (!gameInitResult.ok) {
      std::fprintf(stderr, "startup probe: Init initialization: %s\n",
                   gameInitResult.error.c_str());
      return 1;
    }
  }
  for (const auto* extra : extraClasses) {
    const auto extraInit =
        vmInitializeClass(*extra, loaded, states, &resolve, &arena);
    if (!extraInit.ok) {
      std::fprintf(stderr, "startup probe: %s initialization: %s\n",
                   extra->name.c_str(), extraInit.error.c_str());
      return 1;
    }
  }
  const auto init =
      vmInitializeClass(*config, loaded, states, &resolve, &arena);
  if (!init.ok) {
    std::fprintf(stderr, "startup probe: Config initialization: %s",
                 init.error.c_str());
    if (!error.empty())
      std::fprintf(stderr, " (%s)", error.c_str());
    std::fputc('\n', stderr);
    return 1;
  }
  for (const auto& state : states)
    if (state.owner == config)
      for (const auto& field : state.fields) {
        if (field.name != "init")
          continue;
        const auto* value = std::get_if<int32_t>(&field.value);
        if (!value) {
          std::fprintf(stderr,
                       "startup probe: Config.init is not an integer\n");
          return 1;
        }
        uint32_t id;
        std::memcpy(&id, value, sizeof id);
        PackResource target;
        if (!packs.lookup(id, target, error) || target.entry.resType != 8 ||
            !target.packBytes) {
          std::fprintf(stderr, "startup probe: Config.init lookup: %s\n",
                       error.c_str());
          return 1;
        }
        const auto& targetBytes = *target.packBytes;
        const std::string script(
            targetBytes.begin() + target.entry.payloadOffset,
            targetBytes.begin() + target.entry.payloadOffset +
                target.entry.payloadSize);
        std::printf("Config.init = 0x%08x: %s", id, script.c_str());
        if (!constructor)
          return 0;
        // This host-made tree requests allocation of the unmodified loaded
        // Config class.
        TufaClass expression;
        expression.constants.push_back({0, "Ljava.util.Config;", 0, 0});
        const std::vector<TufaNode> tree{
            {0x07, 0, true, 30}, {0x06, 0, true, 0}, {0x2b, 0, false, 0}};
        const auto allocation =
            vmExecute(expression, tree, {}, nullptr, &loaded, &states, &arena);
        if (!allocation.ok) {
          std::fprintf(stderr, "startup probe: Config allocation: %s\n",
                       allocation.error.c_str());
          return 1;
        }
        const auto* object = std::get_if<const VmObject*>(&allocation.value);
        if (!object || !*object || (*object)->runtimeClass != config) {
          std::fprintf(
              stderr,
              "startup probe: Config allocation returned invalid object\n");
          return 1;
        }
        const TufaMethod* constructorMethod = nullptr;
        for (const auto& list : config->methods)
          for (const auto& method : list) {
            const auto& words = method.words;
            if (words[1] < config->constants.size() &&
                words[2] < config->constants.size() &&
                config->constants[words[1]].text == "<init>" &&
                config->constants[words[2]].text == "(I)") {
              if (constructorMethod) {
                std::fprintf(stderr,
                             "startup probe: ambiguous Config constructor\n");
                return 1;
              }
              constructorMethod = &method;
            }
          }
        if (!constructorMethod ||
            constructorMethod->words[3] >= config->trees.size() ||
            (constructorMethod->words[0] & (8 | 0x40))) {
          std::fprintf(stderr,
                       "startup probe: Config constructor unavailable\n");
          return 1;
        }
        InstanceIds diagnosticIds;
        std::optional<uint32_t> instanceId;
        WorldNode* configNode = nullptr;
        if (initProbe) {
          uint32_t parentId = 0;
          if (!packs.resolveLiteral("system.rpk", 4, parentId, error)) {
            std::fprintf(stderr, "startup probe: Config parent: %s\n",
                         error.c_str());
            return 1;
          }
          const auto parent = resources.lookup(parentId, 1);
          if (!parent.node.packNode ||
              !resources.loadEmptyGameParent(*parent.node.packNode, error)) {
            std::fprintf(stderr, "startup probe: Config parent load: %s\n",
                         error.c_str());
            return 1;
          }
          parent.node.packNode->flags |= 0x10;
          setLinkOwner(parent.node.packNode->sectorLink, parent.node.owner);
          if (!world.attachGameParent(*parent.node.owner)) {
            std::fprintf(stderr,
                         "startup probe: global game parent link failed\n");
            return 1;
          }
          ScriptNode* configScript = world.findScript(configId);
          if (!configScript) {
            std::fprintf(stderr,
                         "startup probe: Config script resource unavailable\n");
            return 1;
          }
          configScript->className = config->name;
          configScript->loaded = true;
          resources.noteScriptLoaded(configId);
          configNode =
              resources.addGameChild(*parent.node.packNode, "Config", 0x21);
          if (!configNode) {
            std::fprintf(stderr,
                         "startup probe: Config node allocation failed\n");
            return 1;
          }
          configNode->flags |= 0x200;
          world.markLoaded(*configNode);
          configNode->loadPriority = 1.0f;
          configNode->eventSubscriptionsKnownEmpty = true;
          configNode->hasReceiver = true;
          configNode->scriptReceiver = *object;
          setLinkOwner(configNode->scriptLink, configScript);
          const auto made =
              invokeNative("java.util.resource.ResourceRef", "newNative", "()V",
                           {}, *object, &arena, &loaded);
          if (!std::holds_alternative<std::monostate>(made)) {
            std::fprintf(
                stderr,
                "startup probe: Config native link allocation failed\n");
            return 1;
          }
          auto* resourceState = nativeResourceState(*object, &arena);
          if (!resourceState) {
            std::fprintf(stderr,
                         "startup probe: Config native link unavailable\n");
            return 1;
          }
          const int32_t handle =
              static_cast<int32_t>(resourceState -
                                   arena.resourceStates.data()) +
              1;
          auto& binding = gameState.bindings[handle];
          binding = std::make_unique<ReceiverBinding>();
          setLinkOwner(binding->link, configNode);
          resourceState->words[2] = configNode->instanceId;
          instanceId = configNode->instanceId;
          world.noteInstanceCreated(*configNode);
        } else {
          instanceId = diagnosticIds.allocate();
        }
        if (!instanceId) {
          std::fprintf(stderr,
                       "startup probe: diagnostic instance IDs exhausted\n");
          return 1;
        }
        int32_t argument;
        const uint32_t bits = *instanceId;
        std::memcpy(&argument, &bits, sizeof argument);
        if (initProbe)
          std::printf("Config constructor world ID 0x%08x\n", bits);
        else
          std::printf(
              "Config constructor diagnostic ID 0x%08x (fresh allocator; "
              "stock startup order and native game object not modeled)\n",
              bits);
        std::fflush(stdout);
        const auto executed = vmExecute(
            *config, config->trees[constructorMethod->words[3]],
            {*object, argument}, &nativeRegistry(), &loaded, &states, &arena);
        if (!executed.ok) {
          std::fprintf(stderr, "startup probe: Config constructor: %s\n",
                       executed.error.c_str());
          return 1;
        }
        if (initProbe) {
          for (const auto& state : states) {
            if (!state.owner || state.owner->name != "java.util.Config")
              continue;
            for (const auto& field : state.fields) {
              if (field.name == "MouseHelp") {
                if (const auto* value = std::get_if<int32_t>(&field.value))
                  gameState.mouseHelp = *value;
              } else if (field.name == "resource_loadrate") {
                if (const auto* value = std::get_if<int32_t>(&field.value))
                  resourceLoadRate = *value;
              } else if (field.name == "mem_instance_max") {
                if (const auto* value = std::get_if<int32_t>(&field.value))
                  instanceMax = *value;
              } else if (field.name == "mem_sound_max") {
                if (const auto* value = std::get_if<int32_t>(&field.value))
                  soundMax = *value;
              } else if (field.name == "mem_texture_max") {
                if (const auto* value = std::get_if<int32_t>(&field.value))
                  textureMax = *value;
              } else if (field.name == "mem_vertex_max") {
                if (const auto* value = std::get_if<int32_t>(&field.value))
                  meshMax = *value;
              }
            }
          }
        }
        if (initProbe && !frameVisibility.applyConfig(states, error)) {
          std::fprintf(stderr, "startup probe: frame visibility: %s\n",
                       error.c_str());
          return 1;
        }
        if (diagnosticRenderer) {
          gameState.forceRendering = [&](std::string& renderError) {
            const auto initialFrame = world.frameCounter();
            const auto viewports = world.activeViewports();
            size_t cameras = 0;
            for (const auto* viewport : viewports)
              if (viewport->viewport && viewport->viewport->renderAttachments)
                cameras +=
                    viewport->viewport->renderAttachments->active().size();
            if (forceRenderFrames(
                    world, resources, *diagnosticRenderer,
                    videoPlayer && videoPlayer->playing(), instanceMax,
                    soundMax, textureMax, meshMax, resourceLoadRate,
                    &frameVisibility, renderError,
                    [&](std::string& videoError) {
                      return videoPlayer->draw(
                          arena.performanceMilliseconds() / 1000.0, videoError);
                    },
                    true, &arena, &states))
              return true;
            renderError += " (at entry: " + std::to_string(viewports.size()) +
                           " active viewports, " + std::to_string(cameras) +
                           " active cameras; " +
                           std::to_string(world.frameCounter() - initialFrame) +
                           " passes completed)";
            if (renderError.find("loaded type 5") != std::string::npos) {
              std::string boundReason;
              const auto bound =
                  resources.stockSceneMeshUpperBound(&boundReason);
              renderError +=
                  " (mesh limit " +
                  (meshMax ? std::to_string(*meshMax)
                           : std::string("unknown")) +
                  ", bound " +
                  (bound ? std::to_string(*bound) : std::string("unknown")) +
                  ", batches absent " +
                  (resources.stockSceneBatchBuffersAbsent() ? "yes" : "no") +
                  ")";
              if (!bound && !boundReason.empty())
                renderError += " " + boundReason;
            }
            return false;
          };
          gameState.flush = [&](std::string& flushError) {
            if (!gameState.forceRendering(flushError))
              return false;
            // Stock 0047c2d0 appends 00537ed0(1): the aging sweep with the
            // forced threshold 0. 004b7b90 restores deferred D3D device
            // resources; the bounded renderer creates them eagerly.
            return resources.ageRecentUses(flushError, 0);
          };
        }
        if (configNode) {
          // Stock registers getInfo hooks after the script constructor.
          const TufaClass* gameType = nullptr;
          for (const auto* candidate : loaded)
            if (candidate && candidate->name == "java.lang.GameType")
              gameType = candidate;
          const TufaMethod* getInfo = nullptr;
          if (gameType)
            for (const auto& list : gameType->methods)
              for (const auto& method : list)
                if (gameType->constants[method.words[1]].text == "getInfo" &&
                    gameType->constants[method.words[2]].text == "(I)I")
                  getInfo = &method;
          if (!gameType || !getInfo ||
              getInfo->words[3] >= gameType->trees.size()) {
            std::fprintf(stderr, "startup probe: Config getInfo unavailable\n");
            return 1;
          }
          for (int32_t kind : {8, 9, 0x1c}) {
            const auto answer = vmExecute(
                *gameType, gameType->trees[getInfo->words[3]], {*object, kind},
                &nativeRegistry(), &loaded, &states, &arena);
            const auto* value = std::get_if<int32_t>(&answer.value);
            if (!answer.ok || !value || *value != 0) {
              std::fprintf(stderr,
                           "startup probe: Config getInfo hook unsupported\n");
              return 1;
            }
          }
          if (!world.attachConfig(*configNode)) {
            std::fprintf(stderr, "startup probe: Config global link failed\n");
            return 1;
          }
          std::printf(
              "Config attached to system.rpk 0x00000004; global link 0x%08x\n",
              world.configNode()->instanceId);
        }
        for (const auto& state : states)
          if (state.owner == config)
            for (const auto& setting : state.fields) {
              if (setting.name == "video_x" || setting.name == "video_y") {
                const auto* dimension = std::get_if<int32_t>(&setting.value);
                if (dimension) {
                  if (setting.name == "video_x")
                    displaySize.width = *dimension;
                  else
                    displaySize.height = *dimension;
                }
              }
              if (setting.name != "video_gamma")
                continue;
              const auto* gamma = std::get_if<float>(&setting.value);
              if (!gamma) {
                std::fprintf(stderr, "startup probe: invalid gamma field\n");
                return 1;
              }
              uint32_t gammaBits;
              std::memcpy(&gammaBits, gamma, sizeof gammaBits);
              std::printf("Config.video_gamma bits = 0x%08x\n", gammaBits);
            }
        std::printf("measurement = %.0f; sound = %.6g, %.6g, %.6g\n",
                    measurement.value, sound.volumes[0], sound.volumes[1],
                    sound.volumes[2]);
        std::puts("Config constructor tree completed in bounded VM; full "
                  "startup not modeled");
        if (!initProbe)
          return 0;
        TufaClass initExpression;
        initExpression.constants.push_back({0, "Ljava.game.Init;", 0, 0});
        const auto allocatedInit = vmExecute(initExpression, tree, {}, nullptr,
                                             &loaded, &states, &arena);
        if (!allocatedInit.ok) {
          std::fprintf(stderr, "startup probe: Init allocation: %s\n",
                       allocatedInit.error.c_str());
          return 1;
        }
        const auto* initObject =
            std::get_if<const VmObject*>(&allocatedInit.value);
        if (!initObject || !*initObject ||
            (*initObject)->runtimeClass != gameInit) {
          std::fprintf(stderr, "startup probe: invalid Init object\n");
          return 1;
        }
        const TufaMethod* initConstructor = nullptr;
        for (const auto& list : gameInit->methods)
          for (const auto& method : list) {
            const auto& words = method.words;
            if (words[1] < gameInit->constants.size() &&
                words[2] < gameInit->constants.size() &&
                gameInit->constants[words[1]].text == "<init>" &&
                gameInit->constants[words[2]].text == "(I)") {
              if (initConstructor) {
                std::fprintf(stderr,
                             "startup probe: ambiguous Init constructor\n");
                return 1;
              }
              initConstructor = &method;
            }
          }
        if (!initConstructor ||
            initConstructor->words[3] >= gameInit->trees.size() ||
            (initConstructor->words[0] & (8 | 0x40))) {
          std::fprintf(stderr, "startup probe: Init constructor unavailable\n");
          return 1;
        }
        const auto initScriptLookup = resources.lookup(id, 8);
        uint32_t initParentId = 0;
        if (!packs.resolveLiteral("system.rpk", 4, initParentId, error)) {
          std::fprintf(stderr, "startup probe: Init parent: %s\n",
                       error.c_str());
          return 1;
        }
        const auto initParent = resources.lookup(initParentId, 1);
        if (!initScriptLookup.node.script || !initParent.node.packNode ||
            !initParent.node.packNode->loaded()) {
          std::fprintf(stderr,
                       "startup probe: Init resource or parent unavailable\n");
          return 1;
        }
        ScriptNode* initScript = initScriptLookup.node.script;
        initScript->className = gameInit->name;
        initScript->loaded = true;
        resources.noteScriptLoaded(id);
        WorldNode* initNode =
            resources.addGameChild(*initParent.node.packNode, "GameInit", 0x21);
        if (!initNode) {
          std::fprintf(stderr, "startup probe: Init world node unavailable\n");
          return 1;
        }
        initNode->flags |= 0x200;
        world.markLoaded(*initNode);
        initNode->loadPriority = 1.0f;
        initNode->eventSubscriptionsKnownEmpty = true;
        initNode->hasReceiver = true;
        initNode->scriptReceiver = *initObject;
        setLinkOwner(initNode->scriptLink, initScript);
        const auto initNative =
            invokeNative("java.util.resource.ResourceRef", "newNative", "()V",
                         {}, *initObject, &arena, &loaded);
        if (!std::holds_alternative<std::monostate>(initNative)) {
          std::fprintf(stderr,
                       "startup probe: Init native link allocation failed\n");
          return 1;
        }
        auto* initResource = nativeResourceState(*initObject, &arena);
        if (!initResource) {
          std::fprintf(stderr, "startup probe: Init native link unavailable\n");
          return 1;
        }
        const int32_t initHandle =
            static_cast<int32_t>(initResource - arena.resourceStates.data()) +
            1;
        auto& initBinding = gameState.bindings[initHandle];
        initBinding = std::make_unique<ReceiverBinding>();
        setLinkOwner(initBinding->link, initNode);
        initResource->words[2] = initNode->instanceId;
        world.noteInstanceCreated(*initNode);
        int32_t initArgument;
        std::memcpy(&initArgument, &initNode->instanceId, sizeof initArgument);
        std::printf("Init constructor world ID 0x%08x\n", initNode->instanceId);
        const auto gameResult =
            vmExecute(*gameInit, gameInit->trees[initConstructor->words[3]],
                      {*initObject, initArgument}, &nativeRegistry(), &loaded,
                      &states, &arena);
        // These lines show what the scripts asked the host world to create.
        // Pack-mounted nodes are in the resource world.
        std::function<void(const WorldNode&, int)> printNode =
            [&](const WorldNode& node, int depth) {
              std::printf(
                  "world node%*s'%s' type %u instance 0x%08x flags 0x%x%s\n",
                  depth * 2 + 1, "", node.name.c_str(), node.type,
                  node.instanceId, node.flags,
                  node.sectorLink.owner == &node ? " (own sector root)" : "");
              if (node.cursor)
                std::printf("cursor native: font links 0x%08x, 0x%08x; event "
                            "mask 0x%x\n",
                            node.cursor->fontA.cachedValue,
                            node.cursor->fontB.cachedValue, node.eventMask);
              std::function<void(ResourceTreeHandle)> printChildren =
                  [&](ResourceTreeHandle parent) {
                    const auto kids = world.childOwners(parent);
                    if (!kids)
                      return;
                    for (const auto* owner : *kids) {
                      const auto resolved = resources.resolve(owner);
                      if (resolved.worldNode)
                        printNode(*resolved.worldNode, depth + 1);
                      else if (resolved.packNode)
                        printChildren(resolved.packNode->handle);
                    }
                  };
              printChildren(node.handle);
            };
        if (const WorldNode* root = world.defaultParent())
          printNode(*root, 0);
        std::printf("world: %zu nodes, %u game instances created; resource "
                    "world: %zu pack nodes, %zu missing ids\n",
                    world.nodeCount(), world.createdInstances(),
                    resources.nodeCount(), resources.missingIds().size());
        // The ResourceRef statics that Frontend's initializers and setFonts
        // made, with the node each one links to.
        for (const auto& state : states) {
          if (state.owner->name != "java.render.Frontend")
            continue;
          for (const auto& field : state.fields) {
            const auto* object = std::get_if<const VmObject*>(&field.value);
            if (!object || !*object ||
                (*object)->runtimeClass->name !=
                    "java.util.resource.ResourceRef")
              continue;
            const VmResourceState* ref = nativeResourceState(*object, &arena);
            const auto binding =
                ref ? gameState.bindings.find(
                          static_cast<int32_t>(ref -
                                               arena.resourceStates.data()) +
                          1)
                    : gameState.bindings.end();
            const ResolvedNode node =
                binding != gameState.bindings.end()
                    ? resources.resolve(binding->second->link.owner)
                    : ResolvedNode{};
            if (node)
              std::printf("Frontend.%s = resource 0x%08x, node type %u\n",
                          field.name.c_str(), node.id, node.type);
            else
              std::printf("Frontend.%s = no node\n", field.name.c_str());
          }
        }
        for (const auto& name : scheduler.threadNames())
          std::printf("script thread: %s\n", name.c_str());
        if (!gameResult.ok) {
          std::fprintf(stderr, "startup probe: Init constructor: %s\n",
                       gameResult.error.c_str());
          for (const auto& frame : arena.failureTrace)
            std::fprintf(stderr, "  in %s\n", frame.c_str());
        }
        if (gameResult.ok)
          std::puts("Init constructor tree completed in bounded VM");
        else if (!runLoop)
          std::puts("Running queued script threads after the constructor "
                    "failure for diagnostics only");
        if (runLoop) {
          if (!gameResult.ok) {
            std::fprintf(stderr,
                         "startup probe: frame loop requires completed Init\n");
            return 1;
          }
          if (!visibleWindow) {
            std::fprintf(stderr,
                         "startup probe: frame loop requires --visible\n");
            return 1;
          }
          if (!timers.setTimeWarp(timers.timeScale(),
                                  arena.performanceMilliseconds(), error)) {
            std::fprintf(stderr, "startup probe: simulation clock: %s\n",
                         error.c_str());
            return 1;
          }
          float previousFrameSeconds = 0.0f;
          bool noCameraFrameLogged = false;
          std::optional<std::array<float, 3>> cursorInput =
              std::array<float, 3>{};
          world.setCursorInput([&]() { return cursorInput; });
          const auto updateCursorPosition = [&](float x, float y) {
            int width = 0, height = 0;
            if (!SDL_GetWindowSize(diagnosticWindow.get(), &width, &height)) {
              cursorInput.reset();
              return;
            }
            const auto point = normalizeCursorPosition(x, y, width, height);
            if (point)
              cursorInput =
                  std::array<float, 3>{(*point)[0], (*point)[1], 0.0f};
            else
              cursorInput.reset();
          };
          ProbeScript script;
          if (!scriptPath.empty()) {
            std::ifstream in(scriptPath);
            if (!in || !script.read(in, error)) {
              std::fprintf(stderr, "startup probe: script %s: %s\n",
                           scriptPath.c_str(),
                           in ? error.c_str() : "cannot open file");
              return 2;
            }
          }
          std::string pendingShot;
          const auto loopStarted = std::chrono::steady_clock::now();
          const auto sinceLoopStart = [&] {
            return std::chrono::duration<double>(
                       std::chrono::steady_clock::now() - loopStarted)
                .count();
          };
          // Camera instances attached to an active viewport in the last
          // presented frame.
          std::vector<LinkOwner*> lastFrameCameras;
          const auto fieldObject = [](const VmValue* value) -> const VmObject* {
            const auto object =
                value ? std::get_if<const VmObject*>(value) : nullptr;
            return object ? *object : nullptr;
          };
          const auto objectField = [](const VmObject* object,
                                      std::string_view name) -> const VmValue* {
            if (object)
              for (const auto& field : object->fields)
                if (field.name == name)
                  return &field.value;
            return nullptr;
          };
          const auto staticField = [&](std::string_view owner,
                                       std::string_view name) -> const VmValue* {
            for (const auto& state : states)
              if (state.owner->name == owner)
                for (const auto& field : state.fields)
                  if (field.name == name)
                    return &field.value;
            return nullptr;
          };
          const auto shown = [&](const VmObject* dialog) {
            const auto value = objectField(dialog, "shown");
            const auto flag = value ? std::get_if<int32_t>(value) : nullptr;
            return flag && *flag != 0;
          };
          // nullptr when ready; otherwise the first condition still blocking.
          const auto scriptReady =
              [&](std::string_view condition) -> const char* {
            const auto* section =
                fieldObject(staticField("java.game.GameLogic", "actualState"));
            const auto* loading = fieldObject(
                staticField("java.render.Frontend", "loadingScreen"));
            if (!section || !section->runtimeClass)
              return "no GameLogic.actualState";
            if (!loading)
              return "no Frontend.loadingScreen";
            if (resources.isLoading())
              return "resource load pending";
            if (shown(fieldObject(objectField(loading, "loadingDialog"))))
              return "loading dialog shown";
            if (condition == "menu") {
              if (section->runtimeClass->name != "java.game.MainMenu")
                return "active state is not java.game.MainMenu";
              return shown(fieldObject(objectField(section, "mmd")))
                         ? nullptr
                         : "main menu dialog not shown";
            }
            // A background/menu car is not a driving scene. Observe the real
            // stock Track ancestry and player's chassis without changing either.
            const TufaClass* cls = section->runtimeClass;
            bool track = false;
            for (size_t depth = 0; cls && depth < loaded.size(); ++depth) {
              if (cls->name == "java.game.Track") {
                track = true;
                break;
              }
              const auto parent = cls->classHeader[3];
              if (parent >= cls->constants.size() ||
                  cls->constants[parent].kind != 4 ||
                  cls->constants[parent].a >= cls->constants.size())
                break;
              const auto& name = cls->constants[cls->constants[parent].a].text;
              const auto found = std::find_if(
                  loaded.begin(), loaded.end(),
                  [&](const TufaClass* candidate) {
                    return candidate->name == name;
                  });
              cls = found == loaded.end() ? nullptr : *found;
            }
            if (!track)
              return "active state is not a java.game.Track";
            const auto* player =
                fieldObject(staticField("java.game.GameLogic", "player"));
            const auto* vehicle = fieldObject(objectField(player, "car"));
            const auto* chassis = fieldObject(objectField(vehicle, "chassis"));
            const auto* node = chassis ? world.findByReceiver(chassis) : nullptr;
            if (!node || !node->carInstance || !node->carInstance->isCar ||
                !node->carInstance->carPhysics)
              return "player car chassis has no physical car";
            // The native player device must control that car and view it
            // through a camera that was attached in the last presented frame.
            for (const auto* input : world.playerInputs()) {
              if (!input->player ||
                  world.nodeFor(input->player->controllable.owner) != node)
                continue;
              // Player.camera is the camera device; its render instance is
              // what the viewport attaches and the frame draws.
              const auto* device =
                  world.nodeFor(input->player->camera.owner);
              const auto* render = device && device->cameraDevice
                                       ? device->cameraDevice->renderNode()
                                       : nullptr;
              if (!render)
                return "player has no camera render instance";
              for (auto* attached : lastFrameCameras)
                if (world.nodeFor(attached) == render)
                  return nullptr;
              return "player camera not in last presented frame";
            }
            return "no player device controls the player car";
          };
          const auto waitComplete = [&](std::string_view condition, double real,
                                        double logical, double waited) {
            std::printf("startup probe: wait %.*s ready (loop=%.3fs "
                        "script=%.3fs waited=%.3fs frame=%u)\n",
                        static_cast<int>(condition.size()), condition.data(),
                        real, logical, waited,
                        static_cast<unsigned>(world.frameCounter()));
            runlog::event(runlog::EventKind::Note,
                          "script wait %.*s ready real=%.3f script=%.3f "
                          "waited=%.3f frame=%u",
                          static_cast<int>(condition.size()), condition.data(),
                          real, logical, waited,
                          static_cast<unsigned>(world.frameCounter()));
          };
          // Fires for a real key-down and for a replayed one, so a replay can
          // reproduce an input-triggered crash.
          const auto crashIfKey = [&](SDL_Scancode code) -> int {
            if (crashKey == SDL_SCANCODE_UNKNOWN || code != crashKey)
              return -1;
            runlog::event(runlog::EventKind::Error, "crash-on-key %s (%s)",
                          crashKeyName.c_str(), crashMode.c_str());
            if (crashMode == "error") {
              std::fprintf(
                  stderr, "startup probe: injected failure (crash-on-key %s)\n",
                  crashKeyName.c_str());
              return 1;
            }
            if (crashMode == "abort")
              std::abort();
            *static_cast<volatile int*>(nullptr) = 1;
            return 1;
          };
          bool menuMilestone = false, drivingMilestone = false;
          double lastWorldSummary = -5.0;
          struct CarSample {
            float x = 0.f;
            float y = 0.f;
            float z = 0.f;
            bool valid = false;
          };
          static CarSample lastCar;
          const auto recordCarDelta = [&](const PlayerCarNote& cars) {
            if (cars.count != 1)
              return;
            if (lastCar.valid) {
              runlog::event(
                  runlog::EventKind::Note,
                  "card1 {\"v\":1,\"dx\":%.5g,\"dy\":%.5g,\"dz\":%.5g}",
                  cars.x - lastCar.x, cars.y - lastCar.y, cars.z - lastCar.z);
            }
            lastCar = {cars.x, cars.y, cars.z, true};
          };
          // Thread dump: one block per scheduler thread with its state, what it
          // waits on and its script frames. It feeds the `threads` snapshot
          // (threads.txt, and crash.txt on a signal) and, on a stall or
          // failure, run.log.
          const auto formatThreadDump = [&] {
            std::string text;
            char head[160];
            std::snprintf(
                head, sizeof head, "t=%.1fs frame=%u system_isLoading=%d\n",
                sinceLoopStart(), static_cast<unsigned>(world.frameCounter()),
                resources.isLoading() ? 1 : 0);
            text += head;
            for (const auto& t : scheduler.snapshot(10)) {
              char line[400];
              std::snprintf(
                  line, sizeof line,
                  "%-40s %-11s on=%s%s slices=%zu last_slice=f%u flags=0x%x\n",
                  t.name.c_str(), t.state.c_str(),
                  t.waitingOn.empty() ? "-" : t.waitingOn.c_str(),
                  t.hostTask ? " [host task]" : "", t.slices, t.lastSliceFrame,
                  t.flags);
              text += line;
              if (t.state == "sleeping") {
                std::snprintf(line, sizeof line, "    wakes in %.2fs\n",
                              t.sleepRemaining);
                text += line;
              }
              for (const auto& frame : t.stack)
                text += "    at " + frame + "\n";
            }
            return text;
          };
          const auto logThreadDump = [&](const char* why) {
            runlog::event(runlog::EventKind::Note, "world threads dump (%s)",
                          why);
            for (const auto& t : scheduler.snapshot(4)) {
              std::string stack;
              for (const auto& frame : t.stack)
                stack += (stack.empty() ? "" : " < ") + frame;
              runlog::event(runlog::EventKind::Note,
                            "world thread \"%s\" %s on=%s slices=%zu f%u %s",
                            t.name.c_str(), t.state.c_str(),
                            t.waitingOn.empty() ? "-" : t.waitingOn.c_str(),
                            t.slices, t.lastSliceFrame, stack.c_str());
            }
            runlog::setSnapshot("threads", formatThreadDump());
            runlog::flushDiagnostics();
          };
          double lastStallWatch = 0.0;
          std::map<std::string, double> waitingSince;
          std::set<std::string> stallReported;
          // A host task is the script call a player action waits on (click
          // handler, button press). One that stays parked on a monitor is the
          // run's stall: name it once, with every thread's frames.
          const auto watchStalls = [&] {
            const double now = sinceLoopStart();
            if (now - lastStallWatch < 1.0)
              return;
            lastStallWatch = now;
            const auto threads = scheduler.snapshot(10);
            std::set<std::string> live;
            bool newStall = false;
            for (const auto& t : threads) {
              if (t.state != "waiting")
                continue;
              const std::string key = t.name + "|" + t.waitingOn;
              live.insert(key);
              const double since =
                  waitingSince.try_emplace(key, now).first->second;
              if (t.hostTask && now - since >= 5.0 &&
                  stallReported.insert(key).second) {
                runlog::event(runlog::EventKind::Note,
                              "stall: host task \"%s\" parked on %s for %.1fs "
                              "(no notify arrived)",
                              t.name.c_str(), t.waitingOn.c_str(), now - since);
                newStall = true;
              }
            }
            for (auto it = waitingSince.begin(); it != waitingSince.end();)
              it = live.count(it->first) ? std::next(it)
                                         : waitingSince.erase(it);
            for (auto it = stallReported.begin(); it != stallReported.end();)
              it = live.count(*it) ? std::next(it) : stallReported.erase(it);
            if (newStall)
              logThreadDump("stall");
            else
              runlog::setSnapshot("threads", formatThreadDump());
            const auto cars = world.playerCarNote();
            char worldText[256];
            std::snprintf(worldText, sizeof worldText,
                          "nodes=%zu cars=%u pos=%.3f,%.3f,%.3f spd=%.3f "
                          "gear=%d rpm=%.0f system_isLoading=%d",
                          world.nodeCount(), cars.count, cars.x, cars.y, cars.z,
                          cars.speed, cars.gear, cars.rpm,
                          resources.isLoading() ? 1 : 0);
            runlog::setSnapshot("world", worldText);
          };
          const auto recordFailureContext =
              [&](const std::vector<std::string>& trace) {
                logThreadDump("failure");
                if (trace.empty())
                  return;
                std::string cls = trace.back();
                const auto cut = cls.find(" (tree");
                if (cut != std::string::npos)
                  cls.resize(cut);
                if (cls.size() > 72)
                  cls.resize(72);
                for (char& ch : cls)
                  if (ch == '"' || ch == '\\')
                    ch = '?';
                runlog::event(runlog::EventKind::Note,
                              "fail1 {\"v\":1,\"cls\":\"%s\"}", cls.c_str());
              };
          const auto recordWorldSummary = [&] {
            const auto cars = world.playerCarNote();
            char carToken[16] = "unavailable";
            if (cars.count == 1)
              std::snprintf(carToken, sizeof carToken, "0x%08x",
                            cars.instanceId);
            else if (cars.count > 1)
              std::snprintf(carToken, sizeof carToken, "many");
            if (cars.count == 1) {
              runlog::event(
                  runlog::EventKind::Note,
                  "world state: nodes=%zu active_threads=%zu cars=%u car=%s "
                  "pos=%.3f,%.3f,%.3f spd=%.3f gear=%d rpm=%.0f",
                  world.nodeCount(), scheduler.threadCount(), cars.count,
                  carToken, cars.x, cars.y, cars.z, cars.speed, cars.gear,
                  cars.rpm);
              recordCarDelta(cars);
            } else {
              runlog::event(
                  runlog::EventKind::Note,
                  "world state: nodes=%zu active_threads=%zu cars=%u car=%s",
                  world.nodeCount(), scheduler.threadCount(), cars.count,
                  carToken);
            }
            const auto ground = resources.groundSnapshot();
            // Keys stay short so the line fits the 151-character event text.
            runlog::event(
                runlog::EventKind::Note,
                "ground1 "
                "{\"v\":3,\"c\":%u,\"r\":%u,\"t\":%u,\"p\":%u,\"wa\":%u,"
                "\"wl\":%u,\"fg\":%u,\"ds\":%u,\"y\":%.5g,\"z\":%.5g}",
                ground.cities, ground.roads, ground.traffic,
                ground.pedestrianTypes, ground.waterActive, ground.waterLimits,
                ground.fogSettings, ground.densitySet, ground.density,
                ground.densityHi);
            const auto water = resources.groundWaterNote();
            if (water.active) {
              runlog::event(
                  runlog::EventKind::Note,
                  "ground2 {\"v\":1,\"px\":%.5g,\"py\":%.5g,\"pz\":%.5g,"
                  "\"ea\":%.5g,\"eb\":%.5g}",
                  water.posX, water.posY, water.posZ, water.extentA,
                  water.extentB);
            }
          };
          std::unique_ptr<AnimationDispatch> animationDispatch;
          // Whatever ends the loop (failed frame step, scripted quit, error
          // return), leave the thread and world state in run.log and in the
          // dump files before the locals go away.
          struct LoopExitDump {
            std::function<void()> dump;
            ~LoopExitDump() { dump(); }
          } loopExitDump{[&] {
            logThreadDump("loop exit");
            recordWorldSummary();
          }};
          CarWorldStepper carStepper(world, resources, &measurement);
          for (;;) {
            const auto frameStarted = std::chrono::steady_clock::now();
            if (sinceLoopStart() - lastWorldSummary >= 5.0) {
              recordWorldSummary();
              lastWorldSummary = sinceLoopStart();
            }
            watchStalls();
            runlog::frameTick(static_cast<uint32_t>(world.frameCounter()));
            std::string scriptError;
            const double scriptNow =
                std::chrono::duration<double>(frameStarted - loopStarted).count();
            // Observe both routes without changing the script's logical clock.
            const auto milestone = [&](const char* name, bool& recorded) {
              if (!recorded && !scriptReady(name)) {
                recorded = true;
                const double process = std::chrono::duration<double>(
                    frameStarted - processClockOrigin).count();
                std::printf("startup probe: milestone %s process=%.3f "
                            "loop=%.3f frame=%u\n", name, process, scriptNow,
                            static_cast<unsigned>(world.frameCounter()));
                runlog::event(runlog::EventKind::Note,
                              "milestone %s process=%.3f loop=%.3f frame=%u",
                              name, process, scriptNow,
                              static_cast<unsigned>(world.frameCounter()));
              }
            };
            milestone("menu", menuMilestone);
            milestone("driving", drivingMilestone);
            while (const auto* next = script.poll(
                       scriptNow, scriptReady, waitComplete, scriptError)) {
              const auto& action = *next;
              script.consume();
              runlog::event(runlog::EventKind::Input, "script %.1fs %s %s %s",
                            action.at, action.verb.c_str(), action.a.c_str(),
                            action.b.c_str());
              if (action.verb == "fail") {
                if (action.a == "error") {
                  std::fprintf(stderr, "startup probe: injected failure\n");
                  return 1;
                }
                if (action.a == "segv") {
                  *static_cast<volatile int*>(nullptr) = 1;
                }
                if (action.a == "abort")
                  std::abort();
                if (action.a == "throw")
                  throw std::runtime_error("injected failure");
                std::fprintf(stderr,
                             "startup probe: script: unknown fail kind %s\n",
                             action.a.c_str());
                return 2;
              }
              if (action.verb == "quit") {
                // A scripted quit is the planned termination of a successful
                // run, not an error; log it as a note so the run's err list
                // holds only genuine failures.
                runlog::event(runlog::EventKind::Note,
                              "startup probe: script quit at %.1fs", action.at);
                return 0;
              }
              if (action.verb == "move") {
                int width = 0, height = 0;
                SDL_GetWindowSize(diagnosticWindow.get(), &width, &height);
                const auto point = normalizeCursorPosition(
                    std::stof(action.a), std::stof(action.b), width, height);
                if (point)
                  cursorInput =
                      std::array<float, 3>{(*point)[0], (*point)[1], 0.0f};
              } else if ((action.verb == "down" || action.verb == "up") &&
                         platformInput) {
                platformInput->setSyntheticMouseButton(SDL_BUTTON_LEFT,
                                                       action.verb == "down");
              } else if ((action.verb == "keydown" || action.verb == "keyup") &&
                         platformInput) {
                const SDL_Scancode code =
                    SDL_GetScancodeFromName(action.a.c_str());
                if (code == SDL_SCANCODE_UNKNOWN) {
                  std::fprintf(stderr,
                               "startup probe: script: unknown key %s\n",
                               action.a.c_str());
                  return 2;
                } else {
                  if (action.verb == "keydown") {
                    const int fail = crashIfKey(code);
                    if (fail >= 0)
                      return fail;
                  }
                  platformInput->setSyntheticKey(static_cast<uint32_t>(code),
                                                 action.verb == "keydown");
                }
              } else if (action.verb.rfind("sdl_", 0) == 0 && platformInput) {
                // Test hooks: push a genuine SDL event so it goes through the
                // recorded path below. SDL does not change its own key or
                // button state for a pushed event, so the game's view is set
                // the same way as the plain verbs do.
                SDL_Event pushed{};
                const SDL_WindowID windowId =
                    SDL_GetWindowID(diagnosticWindow.get());
                if (action.verb == "sdl_move") {
                  pushed.type = SDL_EVENT_MOUSE_MOTION;
                  pushed.motion.windowID = windowId;
                  pushed.motion.x = std::stof(action.a);
                  pushed.motion.y = std::stof(action.b);
                } else if (action.verb == "sdl_down" ||
                           action.verb == "sdl_up") {
                  const bool down = action.verb == "sdl_down";
                  pushed.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN
                                     : SDL_EVENT_MOUSE_BUTTON_UP;
                  pushed.button.windowID = windowId;
                  pushed.button.button = SDL_BUTTON_LEFT;
                  pushed.button.down = down;
                  platformInput->setSyntheticMouseButton(SDL_BUTTON_LEFT, down);
                } else if (action.verb == "sdl_keydown" ||
                           action.verb == "sdl_keyup") {
                  const SDL_Scancode code =
                      SDL_GetScancodeFromName(action.a.c_str());
                  const bool down = action.verb == "sdl_keydown";
                  pushed.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                  pushed.key.windowID = windowId;
                  pushed.key.scancode = code;
                  pushed.key.down = down;
                  if (code == SDL_SCANCODE_UNKNOWN) {
                    std::fprintf(stderr,
                                 "startup probe: script: unknown key %s\n",
                                 action.a.c_str());
                    return 2;
                  }
                  platformInput->setSyntheticKey(static_cast<uint32_t>(code),
                                                 down);
                } else if (action.verb == "sdl_type") {
                  // Type a literal string into the focused text field by
                  // pushing key down/up pairs with a real keycode, which is
                  // what the game's key->char conversion reads (scancode-only
                  // events carry no character).
                  for (const char c : action.a) {
                    SDL_Keymod modstate = SDL_KMOD_NONE;
                    const SDL_Scancode code = SDL_GetScancodeFromKey(
                        static_cast<SDL_Keycode>(c), &modstate);
                    if (code == SDL_SCANCODE_UNKNOWN)
                      continue;
                    SDL_Event downEvent{};
                    downEvent.type = SDL_EVENT_KEY_DOWN;
                    downEvent.key.windowID = windowId;
                    downEvent.key.scancode = code;
                    downEvent.key.key = static_cast<SDL_Keycode>(c);
                    downEvent.key.down = true;
                    SDL_PushEvent(&downEvent);
                    SDL_Event upEvent = downEvent;
                    upEvent.type = SDL_EVENT_KEY_UP;
                    upEvent.key.down = false;
                    SDL_PushEvent(&upEvent);
                    platformInput->setSyntheticKey(
                        static_cast<uint32_t>(code), false);
                  }
                  pushed.type = 0;
                } else {
                  std::fprintf(stderr,
                               "startup probe: script: unknown action %s\n",
                               action.verb.c_str());
                  return 2;
                }
                if (pushed.type)
                  SDL_PushEvent(&pushed);
              } else if (action.verb == "shot") {
                pendingShot = action.a;
                if (pendingShot.rfind("@run/", 0) == 0) {
                  const auto name = pendingShot.substr(5);
                  if (runDir.empty()) {
                    std::fprintf(
                        stderr,
                        "startup probe: script shot @run requires an active "
                        "run log; use an explicit path with --no-run-log\n");
                    return 2;
                  }
                  if (name.empty() || name == "." || name == ".." ||
                      name.find_first_not_of(
                          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                          "0123456789._-") != std::string::npos) {
                    std::fprintf(stderr, "startup probe: script shot @run "
                                         "requires a simple filename\n");
                    return 2;
                  }
                  pendingShot = (std::filesystem::path(runDir) / name).string();
                  std::error_code ec;
                  const auto status =
                      std::filesystem::symlink_status(pendingShot, ec);
                  if (ec && ec != std::errc::no_such_file_or_directory) {
                    std::fprintf(
                        stderr,
                        "startup probe: script shot cannot inspect %s: %s\n",
                        pendingShot.c_str(), ec.message().c_str());
                    return 2;
                  }
                  if (!ec &&
                      status.type() != std::filesystem::file_type::not_found) {
                    std::fprintf(
                        stderr,
                        "startup probe: script shot already exists: %s\n",
                        pendingShot.c_str());
                    return 2;
                  }
                }
              } else {
                std::fprintf(stderr,
                             "startup probe: script: cannot perform %s\n",
                             action.verb.c_str());
                return 2;
              }
            }
            if (!scriptError.empty()) {
              std::fprintf(stderr, "startup probe: script: %s\n",
                           scriptError.c_str());
              return 2;
            }
            if (scriptPath.empty() &&
                SDL_GetMouseFocus() == diagnosticWindow.get()) {
              float x = 0, y = 0;
              SDL_GetMouseState(&x, &y);
              updateCursorPosition(x, y);
            }
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
              if (event.type == SDL_EVENT_QUIT ||
                  event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                return 0;
              if (event.type == SDL_EVENT_MOUSE_MOTION &&
                  event.motion.windowID ==
                      SDL_GetWindowID(diagnosticWindow.get())) {
                updateCursorPosition(event.motion.x, event.motion.y);
                runlog::inputMotion(sinceLoopStart(), event.motion.x,
                                    event.motion.y);
              }
              if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                   event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                  event.button.windowID ==
                      SDL_GetWindowID(diagnosticWindow.get())) {
                updateCursorPosition(event.button.x, event.button.y);
                if (event.button.button == SDL_BUTTON_LEFT)
                  runlog::inputButton(sinceLoopStart(), event.button.down);
                else
                  runlog::inputUnsupportedButton(sinceLoopStart(),
                                                 event.button.button);
              }
              if ((event.type == SDL_EVENT_KEY_DOWN ||
                   event.type == SDL_EVENT_KEY_UP) &&
                  !event.key.repeat) {
                const char* keyName = SDL_GetScancodeName(event.key.scancode);
                if (runlog::active() && keyName && *keyName)
                  runlog::inputKey(sinceLoopStart(), keyName, event.key.down);
                if (event.key.down) {
                  const int fail = crashIfKey(event.key.scancode);
                  if (fail >= 0)
                    return fail;
                }
              }
            }
            runlog::inputFlush(sinceLoopStart());
            platformInput->beginFrame(animationDispatch == nullptr);
            if (const auto position = platformInput->frameButtonPosition())
              updateCursorPosition((*position)[0], (*position)[1]);
            for (auto* player : world.playerInputs())
              if (player && player->playerInput)
                player->playerInput->beginFrame(previousFrameSeconds);
            if (!timers.advanceFrame(
                    previousFrameSeconds, timers.timeScale(), 0.1f, 0.02f,
                    [&](float seconds, std::string& detail) {
                      return carStepper.step(seconds, detail);
                    },
                    [&](LinkOwner& target, int32_t id, uint32_t event,
                        int32_t param, std::string& detail) {
                      return dispatchTimerEvent(arena, target, id, event, param,
                                                loaded, states, detail);
                    },
                    [&](std::string& detail) {
                      return world.drainDeferredDestroy(detail);
                    },
                    error)) {
              std::fprintf(stderr, "startup probe: simulation step: %s\n",
                           error.c_str());
              for (const auto& frame : arena.failureTrace)
                std::fprintf(stderr, "  in %s\n", frame.c_str());
              return 1;
            }
            if (!runFrameSchedulerSlice(scheduler, error)) {
              std::fprintf(stderr, "startup probe: frame scheduler: %s\n",
                           error.c_str());
              for (const auto& frame : arena.failureTrace)
                std::fprintf(stderr, "  in %s\n", frame.c_str());
              return 1;
            }
            if (!animationDispatch)
              animationDispatch = std::make_unique<AnimationDispatch>(
                  arena, scheduler, std::min(previousFrameSeconds, 0.1f),
                  loaded, states);
            if (!animationDispatch->resume(error)) {
              recordWorldSummary();
              recordFailureContext(arena.failureTrace);
              std::fprintf(stderr, "startup probe: animation callbacks: %s\n",
                           error.c_str());
              for (const auto& frame : arena.failureTrace)
                std::fprintf(stderr, "  in %s\n", frame.c_str());
              if (captureOnError && !captureFrame.empty()) {
                std::string captureError;
                const auto controls = frameVisibility.controls(captureError);
                bool captured = controls && diagnosticRenderer->clear(
                                                0, 0, 0, 255, captureError);
                if (captured && videoPlayer->playing())
                  captured = videoPlayer->draw(
                      arena.performanceMilliseconds() / 1000.0, captureError);
                if (captured)
                  for (auto* viewport : world.activeViewports()) {
                    if (!renderActiveViewport(
                            world, *viewport, controls->worldScale,
                            controls->threshold, controls->frustumChecks,
                            captureError, false, &arena, &states)) {
                      captured = false;
                      break;
                    }
                  }
                if (captured)
                  captured =
                      diagnosticRenderer->saveFrame(captureFrame, captureError);
                if (captured)
                  std::fprintf(stderr,
                               "startup probe: diagnostic scene captured after "
                               "failed animation; no frame presented\n");
                else
                  std::fprintf(stderr,
                               "startup probe: diagnostic scene capture: %s\n",
                               captureError.c_str());
              }
              return 1;
            }
            if (animationDispatch->finished())
              animationDispatch.reset();
            if (!resourceLoadRate || *resourceLoadRate < 0) {
              std::fprintf(stderr,
                           "startup probe: resource load rate unavailable\n");
              return 1;
            }
            if (!resources.completePendingPackLoads(
                    static_cast<uint32_t>(*resourceLoadRate), error)) {
              std::fprintf(stderr, "startup probe: sound precache: %s\n",
                           error.c_str());
              return 1;
            }
            if (!resources.ageRecentUses(error)) {
              std::fprintf(stderr, "startup probe: resource aging: %s\n",
                           error.c_str());
              return 1;
            }
            if (!musicPlayer->update(error)) {
              std::fprintf(stderr, "startup probe: music update: %s\n",
                           error.c_str());
              return 1;
            }
            if (!soundEffects->tick(error)) {
              std::fprintf(stderr, "startup probe: sound update: %s\n",
                           error.c_str());
              return 1;
            }
            const auto controls = frameVisibility.controls(error);
            if (!controls) {
              std::fprintf(stderr, "startup probe: frame controls: %s\n",
                           error.c_str());
              return 1;
            }
            const auto viewports = world.activeViewports();
            std::vector<LinkOwner*> frameCameras;
            for (const auto* viewport : viewports)
              if (viewport->viewport && viewport->viewport->renderAttachments)
                for (auto* camera :
                     viewport->viewport->renderAttachments->active())
                  frameCameras.push_back(camera);
            const size_t cameras = frameCameras.size();
            if (!cameras) {
              // Between scene switches (e.g. garage -> dealer) no camera is
              // attached for a few frames; nothing is drawn but the frame
              // still clears and presents. Logged once per gap, not fatal.
              if (!noCameraFrameLogged) {
                runlog::event(runlog::EventKind::Note,
                              "frame loop has no active camera (frame %u): "
                              "clearing without drawing",
                              static_cast<unsigned>(world.frameCounter()));
                noCameraFrameLogged = true;
              }
            } else {
              noCameraFrameLogged = false;
            }
            if (!diagnosticRenderer->clear(0, 0, 0, 255, error)) {
              std::fprintf(stderr, "startup probe: frame clear: %s\n",
                           error.c_str());
              return 1;
            }
            if (videoPlayer->playing() &&
                !videoPlayer->draw(arena.performanceMilliseconds() / 1000.0,
                                   error)) {
              std::fprintf(stderr, "startup probe: video draw: %s\n",
                           error.c_str());
              return 1;
            }
            for (auto* viewport : viewports) {
              if (!cameras)
                break;
              if (!renderActiveViewport(world, *viewport, controls->worldScale,
                                        controls->threshold,
                                        controls->frustumChecks, error, false,
                                        &arena, &states)) {
                std::fprintf(stderr, "startup probe: frame render: %s\n",
                             error.c_str());
                return 1;
              }
            }
            if (!captureFrame.empty() &&
                !diagnosticRenderer->saveFrame(captureFrame, error)) {
              std::fprintf(stderr, "startup probe: frame capture: %s\n",
                           error.c_str());
              return 1;
            }
            if (!pendingShot.empty()) {
              if (!diagnosticRenderer->saveFrame(pendingShot, error)) {
                std::fprintf(stderr, "startup probe: script shot %s: %s\n",
                             pendingShot.c_str(), error.c_str());
                return 1;
              }
              // Success noise goes to stdout so scripted screenshot saves do
              // not flood the run log's err stream (play.sh captures stderr
              // as diagnostics).
              std::fprintf(
                  stdout,
                  "startup probe: script shot saved %s (frame %u, %.1fs)\n",
                  pendingShot.c_str(),
                  static_cast<unsigned>(world.frameCounter()),
                  std::chrono::duration<double>(
                      std::chrono::steady_clock::now() - loopStarted)
                      .count());
              runlog::event(runlog::EventKind::Input, "shot saved %s",
                            pendingShot.c_str());
              pendingShot.clear();
            }
            if (!diagnosticRenderer->present(error)) {
              std::fprintf(stderr, "startup probe: frame present: %s\n",
                           error.c_str());
              return 1;
            }
            lastFrameCameras = std::move(frameCameras);
            world.setFrameCounter(world.frameCounter() + 1);
            if (!notifyFrontendAfterFrame(scheduler, states, error)) {
              std::fprintf(stderr, "startup probe: frame notification: %s\n",
                           error.c_str());
              return 1;
            }
            SDL_Delay(1);
            previousFrameSeconds =
                std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                             frameStarted)
                    .count();
          }
        }
        if (renderFrame) {
          std::puts(
              "Rendering one diagnostic frame from current startup state");
          const auto controls = frameVisibility.controls(error);
          if (!controls) {
            std::fprintf(stderr, "startup probe: diagnostic frame: %s\n",
                         error.c_str());
          } else {
            const auto viewports = world.activeViewports();
            size_t cameraCount = 0;
            for (const auto* viewport : viewports)
              if (viewport->viewport && viewport->viewport->renderAttachments)
                cameraCount +=
                    viewport->viewport->renderAttachments->active().size();
            std::printf("diagnostic frame: %zu active viewports, %zu attached "
                        "cameras\n",
                        viewports.size(), cameraCount);
            if (!cameraCount) {
              std::fprintf(
                  stderr,
                  "startup probe: diagnostic frame: no active camera\n");
            } else if (!diagnosticRenderer->clear(0, 0, 0, 255, error)) {
              std::fprintf(stderr, "startup probe: diagnostic frame: %s\n",
                           error.c_str());
            } else {
              bool drawn = true;
              for (auto* viewport : viewports)
                if (!renderActiveViewport(
                        world, *viewport, controls->worldScale,
                        controls->threshold, controls->frustumChecks, error,
                        false, &arena, &states)) {
                  drawn = false;
                  break;
                }
              if (drawn && !captureFrame.empty() &&
                  !diagnosticRenderer->saveFrame(captureFrame, error))
                drawn = false;
              if (!drawn || !diagnosticRenderer->present(error))
                std::fprintf(stderr, "startup probe: diagnostic frame: %s\n",
                             error.c_str());
              else {
                world.setFrameCounter(world.frameCounter() + 1);
                std::puts("diagnostic frame presented");
                if (gameResult.ok &&
                    !notifyFrontendAfterFrame(scheduler, states, error))
                  std::fprintf(stderr,
                               "startup probe: frame notification: %s\n",
                               error.c_str());
                if (holdFrame && visibleWindow) {
                  const auto until = std::chrono::steady_clock::now() +
                                     std::chrono::seconds(15);
                  bool quit = false;
                  while (!quit && std::chrono::steady_clock::now() < until) {
                    SDL_Event event;
                    while (SDL_PollEvent(&event))
                      if (event.type == SDL_EVENT_QUIT ||
                          event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                        quit = true;
                    SDL_Delay(10);
                  }
                }
              }
            }
          }
        }
        const auto printProgress = [&] {
          for (const auto& progress : scheduler.threadProgress())
            std::printf("script thread slices: %s = %zu\n",
                        progress.first.c_str(), progress.second);
          for (const auto& wait : scheduler.threadWaits()) {
            const auto* object = wait.second;
            std::printf("script thread waiting: %s on %s\n", wait.first.c_str(),
                        object && object->runtimeClass
                            ? object->runtimeClass->name.c_str()
                            : "unknown object");
          }
        };
        for (size_t tick = 0; tick < 10000; ++tick) {
          const auto result = scheduler.tick(1);
          if (!result.ok) {
            std::fprintf(stderr, "startup probe: scheduler: %s\n",
                         result.error.c_str());
            for (const auto& frame : arena.failureTrace)
              std::fprintf(stderr, "  in %s\n", frame.c_str());
            printProgress();
            return 1;
          }
          if (!scheduler.threadCount())
            break;
        }
        printProgress();
        std::fprintf(stderr, "startup probe: scheduler reached its iteration "
                             "limit without a frontier\n");
        return 1;
      }
  std::fprintf(stderr, "startup probe: Config.init field not found\n");
  return 1;
}

// The recorder must exist before probeMain prints anything, so main pulls out
// the few arguments it needs for meta.txt and leaves real parsing to probeMain.
int main(int argc, char** argv) {
  runlog::RunOptions options;
  options.gameRoot = "ref/raw";
  bool enabled = true, haveRoot = false;
  for (int i = 0; i < argc; ++i)
    options.argv.emplace_back(argv[i]);
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--no-run-log")
      enabled = false;
    else if (arg == "--script" && i + 1 < argc)
      options.scriptPath = argv[++i];
    else if (arg == "--reference-root" && i + 1 < argc)
      options.referenceRoot = argv[++i];
    else if (arg == "--data-root" && i + 1 < argc)
      options.dataRoot = argv[++i];
    else if (arg == "--capture-frame" || arg == "--class" ||
             arg == "--crash-on-key")
      ++i;
    else if (!haveRoot && !arg.empty() && arg[0] != '-') {
      options.gameRoot = arg;
      haveRoot = true;
    }
  }
  // Reject unsafe writable roots before either the recorder or Scripts write.
  if (!options.dataRoot.empty()) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto data = fs::weakly_canonical(options.dataRoot, ec);
    if (ec) {
      std::fprintf(stderr, "startup probe: cannot resolve data root\n");
      return 2;
    }
    const auto contains = [](const fs::path& a, const fs::path& b) {
      auto i = a.begin(), j = b.begin();
      for (; i != a.end() && j != b.end(); ++i, ++j)
        if (*i != *j)
          return false;
      return i == a.end();
    };
    for (const auto& source : {options.referenceRoot, options.gameRoot}) {
      const auto protectedRoot = fs::weakly_canonical(source, ec);
      if (ec || contains(protectedRoot, data) || contains(data, protectedRoot)) {
        std::fprintf(stderr, "startup probe: data root must not overlap reference or installed game\n");
        return 2;
      }
    }
  }
  std::unique_ptr<runlog::RunRecorder> recorder;
  if (enabled) {
    std::string error;
    recorder = runlog::RunRecorder::start(options, error);
    if (!recorder || !error.empty()) {
      std::fprintf(stderr, "startup probe: run log: %s\n", error.c_str());
      return 2;
    }
  }
  const int code =
      probeMain(argc, argv, recorder ? recorder->dir() : std::string{});
  if (recorder)
    recorder->finish(code);
  return code;
}
