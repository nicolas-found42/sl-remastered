# Port plan

This plan describes how we get from the reference material in `ref/` to a native Apple Silicon build of Street Legal Racing: Redline. It is provisional. The early phases exist to test its guesses, and each section says which parts are guesses.

## Goal

The goal is a macOS arm64 app that loads the original game data and plays like the stock 2005 build. The stock build is `ref/raw/StreetLegal_Redline.bak.exe`, MD5 `e686099e7233bb7d7c461b1b35c5064c`.

The other executable in `ref/raw` is a mod. Its version resource says "Miran Wichur Mod v0.9b" and it differs from the stock file by 5,679 bytes. We keep it only for comparison.

## What 1:1 means

We check three levels, and each one needs the one before it.

1. Data. The port reads every original file format and gets the same contents as the original loader. SLRR_rev reports that 147 of 147 RPK archives, 2,921 of 2,921 class files, and 6,032 non-empty SCX models parse. We rerun that with our own code before we rely on it. On this machine the install is smaller, and `docs/phase2.md` lists the counts we reproduce next to these.
2. Logic. Each native function that the game scripts call behaves as the decompiled stock executable does. A registry tracks every function in one of three states, which are unread, read, and matched.
3. Behavior. The same inputs produce the same game state, frame by frame. This level needs the original running somewhere to compare against. See the first risk below.

## Starting material

The facts in the last column come from each repository's own documents. We have verified none of them yet.

| Repository | What it is | What we take from it | Documented state |
|---|---|---|---|
| `slrr-reverse-main` | C++ host, 77,048 lines, written for MSVC and Direct3D 9 | The native-function registry, the script interpreter, the shape of the game loop | The README says it boots stock scripts. SLRR_rev's status page says it was never built or run there. We have not built it. |
| `SLRR_rev-main` | TypeScript and WebGL2 engine, about 6,100 lines, plus 24 documents | File format parsers, format documentation, a test suite of 110 tests, physics ideas | The authors call it a research prototype. Its suspension and damage values are new approximations. |
| `SLRR-JVM-compiler-main` | Node compiler from the game's Java-like source to its class format | A way to rebuild game scripts | Byte-identical output for 1,919 of 2,819 stock sources that ship with a class file. |
| `blender-slrr-scx-importer-main` | Python parser for SCX models, versions 3 to 6 | A second reading of the SCX format to cross-check against | MIT licensed. |

## Approach

The port is native C++17 for arm64 macOS. A platform layer replaces each Windows service, with one backend per service. The replacements in the last column are guesses that we have not tested.

| Original service | Evidence in the stock executable | Candidate replacement |
|---|---|---|
| Direct3D 9 graphics | Imports `Direct3DCreate9` | SDL3 GPU on Metal, or Metal directly |
| DirectInput 8 | Imports `DirectInput8Create` | SDL3 input |
| DirectSound | Imports `DSOUND.DLL` ordinal 1 | SDL3 audio |
| Video and media playback | Imports `CoCreateInstance` and `mciSendCommandA` | ffmpeg decoding |
| Windows, timers, files, registry | Imports from `USER32`, `WINMM`, `KERNEL32`, `ADVAPI32` | SDL3 and POSIX, with settings in a file |

SDL3, ffmpeg, and shaderc are already installed through Homebrew on this machine. Phase 1 counts the Direct3D calls, and that count decides how large the graphics work is.

Code from `ref/` enters `src/` only after the check for its unit exists. We copy it and never edit it in place.

## Phases

Each phase ends with a check that a script can run.

| Phase | Work | Exit check |
|---|---|---|
| 0 Baseline | The `ref/` fence, the stock executable analyzed in Ghidra, the build check. Done in this setup. | `tools/check.sh` prints `check: ok` |
| 1 Census | Use Ghidra to count the Direct3D 9, DirectInput 8, and DirectSound methods the stock executable calls, and to read its native-function registry. Done. See `phase1.md`. | `docs/phase1.md` holds the counts, and `docs/natives.tsv` has one row per native function, all marked unread |
| 2 Data | Loaders for RPK, SCX, class files, and saves. | The corpus counts in "What 1:1 means", reproduced by our code |
| 3 Scripts | The script interpreter and the native functions. | The registry marks every function the stock scripts call as matched |
| 4 Platform | Window, input, audio, video, rendering. | The main menu draws and responds to input |
| 5 Game | Car assembly, physics, the race loop. | A drive on the test track |
| 6 Parity | The level 3 comparison against the original. | Set when the oracle exists |

## Risks and open decisions

- Level 3 oracle: partly in place (2026-10-28). Screenshots of the unchanged stock executable under Wine now show the Main Menu and a Free Ride scene (`docs/STATUS.md`, "Stock under Wine"); a frame-by-frame or behavior comparison does not exist yet. The history below is how it was reached. The user chose Wine on this Mac and authorized installation when needed. A Linux Wine environment was prepared in Docker and launched a writable stock copy, but it requests the SL2 disc before startup. A synthetic CD metadata compatibility layer passed the disc check, but the stock game still displayed a black window, so no playable comparison is established. The invented metadata does not provide CD audio or prove the original disc layout. Keep writable files outside `ref/`.
- Pointer width. The original is 32-bit. Anything that stores an address in data needs a recheck. That includes scripts, saves, and the registry that `slrr-reverse` builds from addresses in the executable. The build turns on `-Wshorten-64-to-32` to flag silent narrowing. `pointer-width.md` tests three approaches on one structure and recommends 4-byte handles where the original byte layout matters. The user chose typed native runtime structures and native pointers, with four-byte values or handles where the format or demonstrated stock behavior requires them. The wheel experiment and its limits are in `wheel-comparison.md`.
- Quality of `slrr-reverse`. The build attempt in phase 1 fails on 7 of 43 units, mostly because the code pins the original 32-bit memory layout. See `phase1.md`. Linking and running are still untested.
- Unnamed code. Ghidra found 4,394 functions in the stock executable on first analysis. Two scripts that define functions reachable only through vtables, native handlers, and undefined stretches raised that to 7,535, and 3% of `.text` is still outside any function. Ghidra names them by address, like `FUN_0058c700`. Naming them is most of the work in phase 3.
- Undercounting. Every census number is a lower bound, because code outside any function is invisible to the scans. `phase1.md` explains how a `Present` call and 7 natives hid there.
- Statically linked libraries. The executable's strings show Direct3D utility (D3DX), PNG, and zlib code inside it, and no one has located that code. A 1:1 port can replace it with system libraries, as long as the file formats stay the same.
- Ownership. The game and its files belong to Invictus Games and the publisher. This repository holds none of them, and `ref/` is in `.gitignore`.
