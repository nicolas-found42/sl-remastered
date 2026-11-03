# What the unchanged stock game does (under Wine on this Mac)

Written 2026-10-28. Everything below was seen in the run `local/wine-runs/capture-20261028-133131/` unless it names another run. Commands to repeat it are in `tools/wine/capture/README.md`.

## In one paragraph

The unchanged 2005 stock executable runs under Wine on this Mac. It passes the disc check (with an invented CD table), shows a splash picture, then the real Main Menu. A scripted click on Free Ride shows the "PLEASE WAIT / LOADING..." screen for about three seconds and then a live 3D scene: a car on a road with trees, houses, a GPS map and a speedometer. Scripted keyboard input (Up, Left, Right, Down, Escape) controlled the car and opened the in-game menu. The game process stayed alive for 263 seconds in this run, about 3.5 minutes of which was after the Free Ride click. The earlier black pictures were a screen-capture problem, not a broken game.

## What was run (identities)

| Item | Value |
|---|---|
| Executable | `ref/raw/StreetLegal_Redline.bak.exe` copied as `StreetLegal_Redline.exe` into the run copy; MD5 `e686099e7233bb7d7c461b1b35c5064c` (`exe-md5.txt` in the run folder). Window title: `SL2 build 606 (Feb  7 2005 11:40:30)`. |
| Game data | Writable copy of the extracted 2005 installer data, `.scratch/2005-extract/Program Files/Street Legal Racing - Redline` (7,858 files). `data-comparison.txt` in the run folder: only `StreetLegal_Redline.exe` (deliberately the stock one) and `diag.log` (written by the game) differ; the three added files are `d3d9.dll`, `dinput8.dll` (our proxies) and `mcicda.dll` (synthetic CD driver); 368 files in `dumps/` at that time. |
| Wine | WineHQ 11.0, built from source on this Mac (`local/wine-runs/winehq-source-20261028/build`): x86-64 macOS loader (Rosetta) with 32-bit Windows support, macOS windowing driver, OpenGL through the Apple driver (`GL_VENDOR "Apple"` in the log). Run in `explorer /desktop=slrr,800x600`. |
| Computer | Apple M5 Pro, macOS 27.0.1 (26A434). |
| Disc check | **Synthetic**. `texture.dat` gives tracks 1 to 10; track 11 length (32201 ms) and total (4557237 ms) are invented. The disc shim answers the game's MCI questions only; **there is no CD audio**. See `docs/wine-comparison.md`. |
| Direct3D device the game asked for | From `dumps/d3d9proxy.log`: 800x600, back buffer X8R8G8B8 (format 21), 1 back buffer, no multisampling, swap effect 2 (flip), D24S8 depth (format 77), fullscreen requested (inside the virtual desktop). `CreateDevice` returned success. |

## Step by step

Times are seconds on the wall clock of `flow.log` (click at 13:32:22) or the proxy's own clock (marked "proxy"). Screenshots: "macOS window" is a `screencapture` of the Wine window; "back buffer" is the game's own 800x600 frame written by the proxy (exact pixels). The macOS window captures show the game 33 pixels lower than it is rendered and cut off its bottom 33 pixels (see the README), so use the back-buffer images for exact content.

1. **Start.** The game window opens; Direct3D is created at 0.8 s (proxy). Frames 1 and 2 are black (nothing drawn yet). The next frame is 2.5 s later (run `...133131`; 9 s in the earlier run `...131946`). A DirectShow error appears in the log at the same time (`err:quartz:autoplug Failed to create filter for L"AVI Splitter"`, also `Wave Parser`, `MPEG-I Stream Splitter`). I did not see the intro videos; whether the gap is the failed video step is not proven.
2. **Splash picture.** A full-screen "Street Legal Racing Redline" art (white and red car, city at night) is shown for a few seconds. Example: `local/wine-runs/capture-20261028-131946/game/dumps/frame_000060_t000011.png` (one 800x600 frame; the frames at presents 60 to 240 in that run are identical, 476,542 non-black pixels, so the splash is static).
3. **Main Menu.** Header `MAIN MENU` and `v2.2.1 (build 606)`. Left column: NEW CAREER, LOAD CAREER, DELETE CAREER, BACK TO GARAGE (grey), LOAD CAR, SAVE CAR (grey). Right column: QUICKRACE, FREE RIDE, DEMO, OPTIONS, CREDITS, EXIT GAME. The background is an animated 3D scene: smooth dark-blue and black car-body shapes that move. About 120 frames per second, 155 draw calls per frame (stats lines in `d3d9proxy.log`).
   - back buffer: `.scratch/shots/stock-flow-1-main-menu-backbuffer.png`
   - macOS window: `.scratch/shots/stock-flow-1-main-menu-macos-window.png`
   - Earlier runs: `.scratch/shots/stock-main-menu.png`, `.scratch/shots/stock-main-menu-macos-window.png`
4. **Hover Free Ride.** FREE RIDE turns white when the pointer is on it. In game coordinates it is at (483, 156); the pointer has to be sent to desktop (483, 189) because of the 33 pixel window offset. A pointer at game position (483, 156) sent without the offset highlighted QUICKRACE instead (run `...131946`).
   - `.scratch/shots/stock-flow-2-free-ride-hover-backbuffer.png`
5. **Click Free Ride** (13:32:22, a 300 ms left-button press through the DirectInput proxy).
6. **Loading screen.** "PLEASE WAIT" top left, "LOADING..." bottom right over a black-and-white comic-style picture of an engine bay. It was on screen in the shots 1 s, 2 s and 3 s after the click and gone at 4 s (about 3 seconds).
   - back buffer: `.scratch/shots/stock-flow-3-loading-backbuffer.png`
   - macOS window: `.scratch/shots/stock-flow-3-loading-macos-window.png`
7. **Free Ride scene (car on the road).** 4 seconds after the click. A pink car ("Player riding a Duhen SunStrip 2.2 DVC" top left) on a tarmac car park near a road with trees, white fences and houses; blue sky with clouds. HUD at the bottom: GPS map (bottom left), a 7-digit counter (`0000000`), NITRO and TURBO bars, speed in digits with a gear letter (`N`, `1st`, `2nd` ...), and a green car damage icon (bottom right). Frame rate in this scene about 28 frames per second at about 450 draw calls per frame (from `stats:` lines between 56 s and 130 s on the proxy clock).
   - back buffer: `.scratch/shots/stock-flow-4-track-first-frame-backbuffer.png` and `.scratch/shots/stock-flow-5-driving-backbuffer.png`
   - macOS window: `.scratch/shots/stock-flow-4-track-macos-window.png`, `.scratch/shots/stock-flow-5-driving-macos-window.png`
   - Earlier runs: `.scratch/shots/stock-free-ride-loading-macos-window.png`, `.scratch/shots/stock-free-ride-track-macos-window.png` ("Player riding a Emer Nonus Street GT").
   - **The car is different from run to run**: Emer Nonus Street GT in runs `...131946`, `...132617`; Duhen SunStrip 2.2 DVC in `...133131`. The native port notes say the same about its car.
8. **About 65 seconds of keyboard input** (`scripted_flow.sh`, times from the start of the key script, 13:32:47): Up held for 6 s, Left 1.2 s, Up still held, Right 1.2 s, C tapped, Up released and Down (brake) 2.5 s, Up 8 s, Left 2.5 s, Up, Right 2.5 s, Up 8 s, then Escape. Observed in `flow/03-drive-*.png`: speed counter went from 000 to 016 (3rd gear) within 4 s, the car reached a house and a white fence, stopped (000, 1st gear) with tyre smoke, the camera view changed from side-on to behind the car. I did not confirm what the C key did; the camera change may be from the car moving or from the key. The key script ended with Escape.
   - macOS windows: `local/wine-runs/capture-20261028-133131/shots/flow/03-drive-00s.png` to `03-drive-65s.png` (17 files).
9. **In-game menu** (after Escape): header `INGAME MENU`, entries CONTINUE, OPTIONS, EXIT TO MAIN MENU over the dimmed scene.
   - `.scratch/shots/stock-flow-6-ingame-menu-macos-window.png`
10. **Still running.** The proxy log shows frames continuing until the run was stopped by hand at 263 s (`presents=12720`, no errors in `game-run.log` apart from the DirectShow and OpenGL notes below).

## Wine messages seen in the clean run (`game-run.log`)

Besides the DirectShow errors above and a COM class "not registered" for three class ids (once each), Wine logged 108 times `fixme:d3d_shader:print_glsl_info_log WARNING: Output of vertex shader 'ffp_varying_texcoord' not read by fragment shader`, and four times each that the GL extensions `GL_ARB_draw_instanced`, `GL_EXT_texture_array`, `GL_ARB_uniform_buffer_object` are not supported (Apple's OpenGL 2.1-style driver). None of these stopped the game. No audio errors were logged; whether any sound was produced was not tested.

## Differences between the stock game (Wine) and the native port

The native side is taken from the screenshots in `.scratch/shots/` (`PROOF-native-main-menu.png`, `fr-hover.png`, `fr-loading-screen.png`, `script-after-click-30s.png`) and `docs/STATUS.md`. I did not run the native port in this task.

| | Stock under Wine (this run) | Native port |
|---|---|---|
| Window | 800x600 game picture inside a Wine virtual-desktop window; the game's window title is `SL2 build 606 ...` | 640x480 picture; the window title in `PROOF-native-main-menu.png` is "Startup diagnostic" |
| Start sequence | Black frames, then splash art, then Main Menu | No splash picture appears in the native shots; whether it has one was not checked |
| Main Menu text and layout | Same header, version text, two columns, grey disabled items, white highlight on hover; same proportions (FREE RIDE at 60 percent across, 26 percent down in both) | Same |
| Main Menu background | Smooth, animated dark-blue and black 3D shapes (car bodywork close-up) | A coarse, blocky photo-like picture of a car in red, grey and black; it changes between shots but looks low resolution and stretched |
| Highlight | White text under the pointer | Same |
| Free Ride click | Loading screen about 3 s, then the 3D scene | Loading screen (identical comic engine-bay picture, "PLEASE WAIT" and "LOADING..."), then the port stops with exit code 1 (`unsupported script native device 'car'`, `docs/STATUS.md`) |
| 3D scene | Car, road, trees, houses, sky, tyre smoke, damage, HUD (GPS map, counter, nitro/turbo, speed, gear, damage icon) | None yet |
| Car choice | Random each run | Random each run (per `docs/STATUS.md`) |
| Frame rate | About 120 frames per second in the menu, about 28 in Free Ride (Wine, Rosetta, Apple OpenGL) | Not measured here |
| Input | Keyboard and mouse buttons through DirectInput 8; mouse position through `GetCursorPos`; Escape opens the in-game menu | Mouse hover and click through a scripted mouse; no keyboard driving |
| Sound | Not tested; no CD audio at all (synthetic CD table) | Dummy devices only (`docs/STATUS.md`) |
| Intro videos | Not shown: Wine's DirectShow could not create the AVI/MPEG filters | Not applicable (not compared) |

## Honest limits

- **Synthetic CD table, no CD audio.** The disc check passes only because a stand-in driver answers with `texture.dat`'s track lengths plus an invented track 11 length and total. The game's CD music (if it plays CD tracks at all) was never heard.
- **Not a clean stock installation proof.** Game data comes from the 2005 installer extract; its `texture.dat` matches `ref/raw`'s (`docs/wine-comparison.md`). The executable is the verified stock backup.
- **The pictures come from a modified environment.** The game ran with two added stand-in DLLs (`d3d9.dll`, `dinput8.dll`) that pass everything through and add fake input. They do not alter drawing. The in-process pictures and the macOS window captures agree on content, which supports that they do not change what is drawn, but the proxies could in principle change timing.
- **Not run on Windows.** All statements are about the game under this Wine build; Wine's behavior (Apple OpenGL, no GStreamer, no FreeType) may differ from a real Windows run.
- **Window offset and clipped bottom.** In macOS window captures the game is shifted down by 33 pixels and its bottom is cut; use the back-buffer PNGs for exact images.
- **A scripted pointer move shifts the real macOS pointer once** (Wine warps it).
- **Startup is unreliable.** 2 of the 6 launches that started the game died or hung right after the first frame (`capture-20261028-131724`, `capture-20261028-132857`); 4 succeeded. Not investigated.
- **Intro videos and audio output were not checked.**
- **One run was timed for 60+ seconds after the click; behavior over much longer play was not tested.** The proxy log shows the game still presenting frames at 263 s.
- Per-run frame dumps are under `local/wine-runs/capture-*/game/dumps/` (PNG; the BMP originals were deleted to save space) with `d3d9proxy.log` and `dinput8proxy.log` beside them.
