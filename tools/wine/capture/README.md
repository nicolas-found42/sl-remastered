# Capture the unchanged stock game under Wine

This folder holds the tools that produced real screenshots of the **unchanged 2005 stock game** (`ref/raw/StreetLegal_Redline.bak.exe`, MD5 `e686099e7233bb7d7c461b1b35c5064c`) running under Wine on this Mac. What the game did is written up in `docs/stock-flow.md`.

Nothing here touches `ref/`. Every run happens in a new folder under `local/wine-runs/capture-<date>-<time>/`, which is not committed.

## Why a "proxy" was needed

Earlier Wine runs showed black pictures. The game was fine; the screen capture was the problem. So these tools take the picture from inside the game instead:

- `d3d9_proxy.c` becomes a stand-in `d3d9.dll` placed next to the game. The game thinks it talks to Direct3D. The stand-in passes every call on to Wine's real Direct3D, but at each "show this frame" call it also copies the finished picture into a BMP file (`dumps/frame_<present number>_t<seconds>.bmp`). Nothing about the game file is changed. It also writes `dumps/d3d9proxy.log` (device settings, frame count, draw-call count, how many pixels were not black).
- `dinput8_proxy.c` does the same for DirectInput (how the game reads keyboard and mouse). It can add a fake mouse click or fake key presses to what the game reads, and logs how the game uses the devices (`dumps/dinput8proxy.log`).
- Plain mouse position and the "move" command are sent from the d3d9 proxy with Windows' `SendInput`/`SetCursorPos` (`dumps/input.cmd`).
- `winlist.swift` and `shot.sh` take a macOS screenshot of the Wine window (`screencapture -l<window id>`). This works even when the window is covered by other windows.

Other pieces reused from earlier work: `tools/wine/synthetic_cd.c` and `tools/wine/synthetic_metadata.py` (the **synthetic** CD table that gets past the disc check; there is no CD audio and the track 11 length and total length are invented, see `docs/wine-comparison.md`).

## What you need

- This Mac (Apple Silicon) with Rosetta, and the Wine 11.0 build that already exists at `local/wine-runs/winehq-source-20261028/build` (macOS windowing). It was built from the official source earlier.
- `/opt/homebrew/bin/i686-w64-mingw32-gcc` (the Windows 32-bit compiler), `python3`, `swift`, `sips` (all already present).
- The extracted 2005 game data in `.scratch/2005-extract/Program Files/Street Legal Racing - Redline` and the disc folder `local/wine-runs/synthetic-run/stock-2005/disc`.

## Exact commands

Run these from the project folder (`/Users/nicolas/Documents/github/sl-remastered`), in Terminal.

1. Start the game. It makes a new run folder, copies the game data into it, puts the stock `.exe` in, checks its MD5, builds the proxies, makes a Wine prefix (a private fake Windows folder) and starts the game in the background. The first time takes about 40 seconds before the window appears.

   ```
   tools/wine/capture/run_capture.sh
   ```

   It prints the run folder, for example `local/wine-runs/capture-20261028-133131`. Use that name in the next commands. To skip the 40-second prefix creation, reuse the prefix of an earlier run:

   ```
   PREFIX_FROM="$PWD/local/wine-runs/capture-20261028-132617/prefix" tools/wine/capture/run_capture.sh
   ```

   (That earlier run must be stopped first.)

2. Wait until a window titled `SL2 build 606 ...` shows the Main Menu (about 15 seconds after the window appears). Then let the script do the whole walk (menu screenshot, hover and click Free Ride, loading screen, then about 65 seconds of keyboard driving, then the in-game menu with Escape):

   ```
   tools/wine/capture/scripted_flow.sh local/wine-runs/capture-20261028-133131
   ```

   Its screenshots land in `<run>/shots/flow/` and its notes in `<run>/flow.log`. The game's own frames land in `<run>/game/dumps/`.

3. Stop Wine and turn the BMP frames into PNG (macOS `sips`):

   ```
   tools/wine/capture/stop.sh local/wine-runs/capture-20261028-133131
   tools/wine/capture/bmp2png.sh local/wine-runs/capture-20261028-133131/game/dumps
   ```

4. Optional check that only the expected files changed in the run's game copy (only `StreetLegal_Redline.exe`, which is deliberately the stock one, and the game's own `diag.log`; plus the three added DLLs and `dumps/`):

   ```
   python3 -I tools/wine/capture/compare_data.py local/wine-runs/capture-20261028-133131
   ```

## Doing it by hand

While a run is going (run folder `RUN`):

```
tools/wine/capture/dump.sh RUN                     # save the next frame as a BMP right now
tools/wine/capture/shot.sh /path/to/out.png        # macOS screenshot of the Wine window
tools/wine/capture/send.sh RUN "move 483 189"      # put the pointer on FREE RIDE
tools/wine/capture/send.sh RUN "di mclick 0 300"   # left click (button 0, held 300 ms) through the DirectInput proxy
tools/wine/capture/send.sh RUN "di key 0x01 200"   # press Escape (DirectInput scan code 1)
tools/wine/capture/send.sh RUN "di kdown 0xC8"     # hold Up arrow (scan code 0xC8); "di kup 0xC8" lets go
```

Useful DirectInput scan codes: Escape 0x01, Enter 0x1C, Up 0xC8, Down 0xD0, Left 0xCB, Right 0xCD, Space 0x39, C 0x2E. Commands for `send.sh` without `di` go to the d3d9 proxy (`move X Y`, `click`, `where`, `focus`, `key NAME`, `sleep MS`, see the top of `d3d9_proxy.c`). Lines starting with `di ` go to the DirectInput proxy (see the top of `dinput8_proxy.c`).

## Things to know (learned the hard way)

- **The mouse needs two parts.** The game reads the mouse position with `GetCursorPos` (a `move` works) but reads buttons through DirectInput. A click sent with `SendInput` (`click`, `mdown`/`mup`) and the Enter and Down keys sent with `SendInput` were ignored (the log shows the commands ran; the game did not react). The same click and keys injected through the DirectInput proxy (`di mclick 0 300`, `di key ...`) worked. The `mevent` and `postclick` commands exist in the proxy but were never tried after the DirectInput route worked.
- **33 pixel offset.** In Wine's 800 by 600 virtual desktop the game window sits at y = 33 (window rectangle 0,33 to 800,633). A point at game position (x, y) must be sent as desktop position (x, y + 33). FREE RIDE is at game position (483, 156), so send `move 483 189`. Because of the same offset, macOS screenshots of the Wine window show the game 33 pixels lower and clip its bottom 33 pixels; the BMP/PNG frames from `dumps/` are the exact 800 by 600 back buffer.
- **`move` moves the real macOS mouse pointer once.** The Wine macOS driver warps the real pointer when a program calls `SetCursorPos`. Do not touch the mouse while the script runs.
- **Escape.** The old notes said holding Escape past the intro video was needed. In the successful runs here it was not needed; the game went on by itself after the splash picture.
- **About 2 of 6 launches that started the game died or hung right after the first frame** (run folders `capture-20261028-131724`: hung at 100 percent CPU, and `capture-20261028-132857`: Wine logged `err:seh:call_seh_handlers invalid frame 000000000022F3FC` and the process disappeared). If `scripted_flow.sh` says "game process is gone" or "no menu", run `stop.sh` and start again with a new run folder. This was not investigated further.
- **No intro videos.** Wine's DirectShow could not create the AVI and MPEG filters (`err:quartz:autoplug Failed to create filter for L"AVI Splitter"`), so the opening videos are not shown. The game continues anyway.
- A run folder is about 2 GB of logical size (a copy of the game data; the copy uses APFS clones so it takes little real disk space). The BMP frames are 1.4 MB each; delete them once the PNGs exist.
- `tools/check.sh` does not run any of this.
