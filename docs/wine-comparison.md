# Stock game comparison preparation

The user chose Wine on this Mac. Homebrew's macOS Wine casks are disabled because they fail Gatekeeper checks. `tools/wine/Dockerfile` builds a separate Linux amd64 Wine environment in Docker Desktop without changing that security setting. This is an experiment, not yet a usable original-game oracle.

The first run used `local/wine-runs/stock-9ljvysz8/game`, a writable copy of `ref/raw`. Its `StreetLegal_Redline.exe` was replaced with a copy of `StreetLegal_Redline.bak.exe` and checked as MD5 `e686099e7233bb7d7c461b1b35c5064c`. The only bind mount was the writable run directory, not `ref/`; networking was disabled. Wine created its prefix outside `ref/`. The game wrote its build-606 header to the copied `diag.log`. The original opened a dialog reading “Please Insert the SL2 disk.” A direct X11 window capture, `local/wine-runs/stock-9ljvysz8/window.png`, recorded that dialog. The user has been asked for the original disc or a legitimate disc image. No playable stock menu or behavior comparison has been established.

The image was built with `docker build --platform linux/amd64 -t slrr-stock-wine-local -f tools/wine/Dockerfile tools/wine`. The experimental run used `docker run --rm --platform linux/amd64 --network none --mount type=bind,src="$PWD/local/wine-runs/stock-9ljvysz8",dst=/work slrr-stock-wine-local timeout 90s xvfb-run -a -s '-screen 0 1024x768x24 -fbdir /work' wine StreetLegal_Redline.exe`. For a reliable capture, use `xwd -root -silent` inside the running container with its X display and authority, then convert the output with ffmpeg. Paths and the run directory are local artifacts and should not be committed.

Next connect the original disc or legitimate image and retry without changing the executable or bypassing its check. Record Wine, image, executable and data identities with any successful comparison. A screenshot alone does not establish matching game behavior.

The framebuffer file from Xvfb did not reliably reflect the live window through the bind mount; the working capture used `xwd -root -silent` inside the container and converted its output with ffmpeg.

The user identified `.scratch/221-files/ex2/Activision-StreetLegalRacing-Win98DX9-Eng.bin`. Its ISO 9660 filesystem is labeled `SLRACING` and its readme identifies Redline; the disc files carry 2003 dates. A read-only ISO data copy was mounted outside `ref/` and supplied to Wine as drive D. After removing an incorrect raw-device mapping, `wine cmd /c dir d:\` listed the disc and its volume, but the 2005 build-606 executable still showed `Please Insert the SL2 disk`. This does not establish whether the mismatch is edition, Wine's CD emulation, or the image itself. The 2005 installer files in `.scratch/221-files/ex1` are a separate candidate; their disc provenance and whether they can satisfy the stock executable have not been established.

The stock check reads `texture.dat` beside the executable and asks Windows for CD track information through MCI (`FUN_00401260` and `FUN_00401480`). A diagnostic program, `tools/wine/cd_probe.cpp`, established that the initial Wine mapping reported drive type 3 (fixed disk). Setting `HKLM\Software\Wine\Drives` value `d:` to the string `cdrom`, then waiting for the Wine server to exit and starting it again, corrected the reported type to 5 (CD-ROM). MCI could then open the drive, but both track-count and length queries returned error 272, a device-driver error. The Wine image maps a read-only directory, not a CD device that provides a table of contents. The supplied BIN contains 341,642 raw sectors, whereas the ISO filesystem declares 183,857 data sectors; no CUE or track table was found. The data-only conversion cannot provide the missing track layout.

The supplied 2005 installer cabinet contains the same `Dummy.class` hash as the reference baseline, but its executable MD5 differs from the specified stock executable. Exposing the extracted installer directory as the Wine drive also resulted in the disc request. It does not establish that its original optical media was present.

An exhaustive scan of the BIN found the declared data sectors followed by 157,785 sectors of which only three contain nonzero bytes; the remaining 157,782 are all zero. No accompanying CUE, TOC, CCD, SUB, MDS, or MDF file was found under `.scratch`. Those bytes do not establish the original track boundaries. An optical-device backend capable of reporting the authentic table of contents, plus suitable source media or verified track metadata, remains needed for the unchanged stock executable's check.

To rerun the drive diagnostic, build it with `i686-w64-mingw32-g++ -std=c++17 -static -o local/wine-runs/stock-9ljvysz8/cd_probe.exe tools/wine/cd_probe.cpp -lwinmm`. Run it in the isolated Wine image with the writable run directory bound at `/work` and the read-only mounted disc data at `/disc`. Wine 8 reads the `d:` drive type from the `REG_SZ` value `cdrom` under `HKLM\Software\Wine\Drives` when its mount manager starts. After setting that value, allow the Wine server to exit and restart it before probing. The supplied data-only mount is not a substitute for a track-capable device.

## What the disc check requires (from the stock executable)

Evidence is the decompilation of the stock executable (MD5 `e686099e7233bb7d7c461b1b35c5064c`, confirmed with Ghidra `get_program_info`).

- `FUN_00401000` calls `FUN_004013f0`, which runs the check. Failure produces the "Please Insert the SL2 disk" dialog.
- `FUN_00401260` reads `texture.dat` next to the executable (76 bytes). It XORs every byte after the first 4 with the 400-byte table at `0x607240`. Decoded, the file is the text `V01.000 Overburn` followed by a list of numbers.
- `FUN_004013f0` hands `FUN_00401480` the list that starts at the 9th 32-bit word of the decoded file. That list is: track count (11), then track lengths in milliseconds.
- `FUN_00401480` first closes any window of class `SJE_CdPlayerClass`. It then tries drive letters A to Z. For each drive where `GetDriveTypeA` returns 5 (CD-ROM), it opens an MCI `cdaudio` device on that letter, sets the time format to milliseconds, and asks for the track count and each track's length. It then asks for the total length.
- The check passes when all of these hold: the track count equals 11; tracks 2 to 10 have exactly the lengths in `texture.dat` (1270401, 213401 x3, 32201 x5); the total length is at least 4470000 ms and below 4830001 ms.
- The check never reads a file from the disc. It does not use the volume label, a drive search by file, or a hash. Track 1 and track 11 lengths are not compared.

So `texture.dat` contains the expected CD layout. The earlier "unknown track table" blocker was wrong. Only three numbers are not in `texture.dat`.

## SYNTHETIC CD table (what is invented)

`tools/wine/synthetic_metadata.py` decodes `texture.dat` using the key read from the stock executable and writes two files: a machine file for the Wine driver and `SYNTHETIC-metadata.txt`. Values and their origin:

- Track count 11, tracks 1 to 10 lengths: read from `texture.dat`. Track 1 (2453427 ms) is in the file but the game does not compare it.
- Track 11 length (32201 ms): SYNTHETIC. It is copied from track 10. The game does not compare it.
- Total length (4557237 ms): SYNTHETIC. It is the sum of the eleven values. The game only needs 4470000 to 4830000.
- This is not the real disc's layout and not CD audio. The real disc's track 11 length and total are unknown. Do not cite these values as measurements of an authentic disc.

## The fix, and how fake it is

`tools/wine/synthetic_cd.c` builds a small Windows DLL named `mcicda.dll`. It stands in for Wine's CD audio driver for MCI inside the container only. It answers exactly the MCI requests the check makes (open on `D:`, set milliseconds, count, track length, total length) from the table above. Playing returns an error, so there is no CD audio. The stock executable and every game file are unchanged. The DLL sits in the writable run copy of the game folder, and Wine is told to prefer it with `WINEDLLOVERRIDES=mcicda=n`. The data-only ISO from `prepare_disc.py` is attached read-only as `D:` and Wine is told `D:` is a CD-ROM (registry `HKLM\Software\Wine\Drives`, value `d:` = `cdrom`). The ISO contents are not read by the check.

Two bugs in the earlier untested experiment were fixed. The DLL exported `DriverProc@20` instead of `DriverProc` (fixed with `-Wl,--kill-at`). Wine 8 gives the driver no parameters when opening it, so the open handler must not require them.

## Rerun

```
tools/wine/run_stock_synthetic.sh local/wine-runs/my-new-run
```

It needs Docker, mingw (`i686-w64-mingw32-gcc`), and a disc data directory. By default it uses `local/wine-runs/synthetic-run/stock-2005/disc`; set `DISC` to use another directory. The run directory must not exist. The runner creates it and refuses to reuse an earlier run. It uses a separate container name for each run and stops that container on exit. It is not part of `tools/check.sh`. It rebuilds the image `slrr-stock-wine-gl` from `tools/wine/Dockerfile` if missing, generates the metadata and DLL, and copies `ref/raw` to the new run directory. It replaces the run copy of `StreetLegal_Redline.exe` with the stock executable and checks its MD5. It runs the probe and game in an isolated container with no network and only the run folder and disc mounted. It saves screenshots as `frame<seconds>-root.png` and `frame<seconds>-game.png` when those captures succeed. `FRAMES` sets capture times relative to the container start, including prefix setup. `LIMIT` sets the game command's timeout. The runner stops the container after the last capture, even when the timeout is longer. A capture can fail before Xvfb starts. The offline test `python3 -I tests/wine_disc_test.py` covers the table decoder.

## Result

The synthetic disc check passed in the isolated Wine run. The stock startup function `FUN_0058c700` returns after the disc dialog if the check fails, before initializing graphics. The run reached Direct3D initialization, as recorded in `local/wine-runs/synthetic-run/unobstructed/game-run.log`. The separate probe in `local/wine-runs/synthetic-run/unobstructed/probe.log` reports drive type 5 and error 0 for open, millisecond format, track count, all track lengths, and total length. Its play request returns error 274, unsupported function.

The captures at 45, 75, 105, 135, and 165 seconds after container start show a black window, not a disc dialog or playable menu. See `local/wine-runs/synthetic-run/unobstructed/frame45-root.png` and `local/wine-runs/synthetic-run/unobstructed/frame165-game.png`. The game wrote `local/wine-runs/synthetic-run/unobstructed/game/diag.log`, which identifies build 606. A separate run's `local/wine-runs/synthetic-run/repeat/window-tree-live.txt` identifies the 800 by 600 game window, and `capture-proof-root.png` shows that XWD captured a test message on the same live display. That test message contaminated the scheduled frames in the separate run, so the unobstructed frames are the result evidence. The black captures do not establish a playable menu or the cause of the black display.

The image was `slrr-stock-wine-gl`, ID `sha256:1187b0af94bc6a84dc56593f916d314ea73feb77a5a671cf0e190ba96f665192`, running Wine `8.0 (Debian 8.0~repack-4)` for Linux amd64. The executable MD5 was `e686099e7233bb7d7c461b1b35c5064c`. The disc data came from `.scratch/221-files/ex2/Activision-StreetLegalRacing-Win98DX9-Eng.bin`, MD5 `b7112f2bca9befddb544fa736e940486`, converted to data-only ISO MD5 `2075368d1c2efc61a9d55a9f54efc145`. The synthetic track 11 length of 32201 ms and synthetic total of 4557237 ms are invented, not measurements of an authentic disc. The shim does not supply CD audio.

The run used copied `ref/raw` game data with the stock executable, not a clean stock 2005 installation. For example, `sl/Scripts/game/MainMenu.class` in that data contains `Miran Wichur Mod BETA`. A post-run SHA-256 comparison found `diag.log` was the only content difference among the 8410 copied source files compared, excluding the deliberately replaced executable. The record is `local/wine-runs/synthetic-run/unobstructed/data-comparison.txt`. The runner's reference guard reported all 8739 protected reference files unchanged.

A second run used the extracted 2005 installer data from `.scratch/2005-extract/Program Files/Street Legal Racing - Redline/`, rather than `ref/raw`. The source installer was `.scratch/221-files/ex1/Street Legal Racing - Redline.msi`, MD5 `b5607ea675400419a25339392c0c50f9`, and `Data1.cab`, MD5 `c5f3f1b347d763c5b48fa2b8b564c728`. The extracted backup executable has the same stock MD5 as above. The extracted `texture.dat` and the file in `ref/raw` have the same MD5, `eb94dce3891c772014327c9fef2a3a07`. The runner replaced the extracted installer executable with the verified stock backup in the writable copy.

The clean-data run also reached Direct3D after the disc check. Its probe returned error 0 for the required MCI operations and error 274 for play. See `local/wine-runs/synthetic-run/stock-2005/game-run.log` and `local/wine-runs/synthetic-run/stock-2005/probe.log`. All seven captures from 45 through 225 seconds after container start show black. The game diagnostic header and first and last images are `local/wine-runs/synthetic-run/stock-2005/game/diag.log`, `frame45-root.png`, and `frame225-game.png` in that run directory. No playable menu was observed. A post-run SHA-256 comparison found only `diag.log` differed among 7857 source files compared, excluding the deliberately replaced executable. See `local/wine-runs/synthetic-run/stock-2005/data-comparison.txt`.

### Black window investigation, 2026-10-27

The experiments below use the clean 2005 data copy and the same stock executable and invented CD table. The track 11 length and total remain synthetic, and the shim supplies no CD audio. The game was not playable in these runs.

- **Missing audio.** An ALSA null PCM and Wine's ALSA driver let DirectSound create and play a primary buffer. The log also shows DirectShow sound buffers and reports a missing MIDI synthesizer. The 13-second and 25-second process snapshots still show the game alive, and their root captures are identical black images. See `local/wine-runs/blackwindow-audio/game-run.log`, `process-first.txt`, `process-later.txt`, and `frame-later.png`. This rules out failure to open the primary DirectSound playback device as a sufficient explanation for those frames. It does not test audible playback or MIDI.
- **No rendering or early crash.** The `+d3d` run recorded 155 `wined3d_swapchain_present`, 38 `wined3d_texture_create`, 576 texture-map, and 668 context-draw trace lines. These are Wine internal trace events, not necessarily individual Direct3D API calls. The game process remained alive at the later 45-second snapshot. The game diagnostic remained 111 bytes in the two audio-run snapshots. See `local/wine-runs/blackwindow-d3d/trace-counts.txt` and `process-final.txt`. These observations reject a crash before device creation and show rendering activity, but do not establish that a visible menu was presented.
- **Black textures, input, or capture.** Holding Escape for five seconds addressed the stock video loop's `GetAsyncKeyState` check. The file trace subsequently recorded reads of `Init.class`, `LoadingScreen.class`, and the 1024 by 1024 DXT1 `frontend/textures/Loading.dds`. The combined trace shows Wine creating and mapping that texture and later reading a font. In that run, two present operations were queued after the loading texture read, but no corresponding command-stream present execution or `swapchain_gl_present` trace appeared before the run stopped. See `local/wine-runs/blackwindow-loading/game-run.log` and `present-after-loading.txt`. The capture remained black at the 71-second game-process snapshot. This does not prove that the texture was drawn correctly or that Escape was delivered to the game.
- **Capture controls.** XWD captured colored `glxgears` on Xvfb. A separate synthetic, windowed Direct3D 9 control cleared its target red, returned success from `CreateDevice`, and produced a red XWD capture. See `local/wine-runs/blackwindow-glx/root-active.png`, `local/wine-runs/blackwindow-d3d-control.c`, and `local/wine-runs/blackwindow-d3d-control/root-live.png`. These controls do not reproduce the game's fullscreen rendering or prove its texture formats work.
- **Other compatibility settings.** Installing `libxcomposite1:i386` removed Wine's `XComposite is not available` warning, but the game remained black. Removing Wine's virtual desktop with a matching 800 by 600 Xvfb screen made the GL child viewable, but the child and root captures were black. Disabling Wine's `csmt` was confirmed in the trace and also left a black capture. Setting `VideoMemorySize` to 256 MiB changed Wine's reported device memory from `0xefb00000` to `0x10000000` bytes, but did not produce a visible frame. `softpipe` reported a GLib allocation failure and repeated traps, so that run is not a successful rasterizer comparison. Evidence is under `local/wine-runs/blackwindow-composite/`, `blackwindow-direct800/`, `blackwindow-csmt0/`, `blackwindow-vram256/`, and `blackwindow-softpipe/`.

The leading explanation is that the game reaches the loading stage but Wine's rendering work does not reach a visible presentation in this environment. The queued presents and the black capture support that explanation, but they do not identify the cause or prove a permanent llvmpipe limitation. A Wine debugger attach returned `error 5` even with container ptrace permissions. No menu, playable comparison, or successful fix has been established.

On 2026-10-28 a separate clean-data run omitted the file and Direct3D traces, used the null audio setup, and sent Escape for five seconds. Its root capture remained black with the game process alive after 1 minute 37 seconds. The evidence is in `local/wine-runs/blackwindow-long-20261028/root-later.png` and `process-later.txt`. Wine also logged DirectShow audio underruns. This shorter run does not rule out a later display or identify the cause.

A second 2026-10-28 run tested whether startup video playback held the black window. It used the same clean installer extract, null audio, synthetic disc table, and stock executable, but moved `Activision.avi`, `Invictus.avi`, and `Prime.avi` out of the writable game copy before launch. Its root capture remained black with the game process alive after 1 minute 34 seconds. See `local/wine-runs/blackwindow-no-video-20261028/frame-later.png`, `process-later.txt`, and `window-tree-50.txt`. This result shows that the three videos were not necessary for that black frame. It does not rule out another video or audio problem, a later visible frame, or a capture problem. `identities.txt` records the executable and installer MD5s, Docker image ID, and omitted-video SHA-256 hashes. `data-comparison.txt` records the three intentionally missing videos and the changed diagnostic log among 7,857 installer files compared, excluding the replaced executable. The synthetic disc shim still supplies no CD audio.

A third run used the same setup with the three videos absent and Wine's Direct3D trace enabled. Between snapshots at game-process elapsed times of 32 and 91 seconds, the trace recorded 35 completed draw commands and two queued presents, but no present execution or `swapchain_gl_present` trace. Both root captures were black. The command stream advanced between snapshots, so the trace does not support a permanent stall at the first snapshot's draw. It does not prove that the queued presents will execute, explain the black window, or show how the game behaves without tracing. Trace logging itself can change timing. The snapshots, captures, process records, and prefix comparison are in `local/wine-runs/blackwindow-d3d-progress-20261028/`. The executable MD5 was `e686099e7233bb7d7c461b1b35c5064c`; the image was `sha256:bbfad8093850e1f06429e2afecb8ef96a25d4dc2bba9ab1623f9eebf6485826b`.

A longer run disabled Wine tracing and again omitted the three startup videos from the clean game copy. Root captures at game-process elapsed times 2:21, 5:21, and 9:21 were identical black images; the game process was alive at each capture. The experiment did not reach a visible menu and does not show whether graphics commands continued or whether a menu would appear later. See `local/wine-runs/blackwindow-long-untraced-20261028/` for the captures, window trees, process records, executable and image identities, and source-file comparison. The same synthetic disc table and null audio limitations apply. After the last capture the run was stopped and its container removed. The reference guard passed.

A separate run selected Wine's `HKCU\Software\Wine\Direct3D\OffscreenRenderingMode` value `backbuffer` instead of the default framebuffer mode. Wine's diagnostic log confirms that it used backbuffer offscreen rendering and warns that this mode is unsupported. With the same clean-data, no-video, synthetic-disc, and null-audio setup, the captures at game-process elapsed times 0:56 and 2:01 were identical black images and the game remained alive. This shows the default offscreen mode was not necessary for those black captures; it does not identify the cause or rule out a later visible frame. Evidence is in `local/wine-runs/blackwindow-backbuffer-20261028/`. The stock executable MD5 was `e686099e7233bb7d7c461b1b35c5064c`, and the experiment's container was removed after capture. The reference guard passed.

Another clean-data, no-video run set `MESA_GL_VERSION_OVERRIDE=2.1`. Wine's Direct3D trace reported OpenGL 2.1 after warnings that WGL context creation failed. Root captures at process elapsed times 0:51 and 1:51 were identical black images and the game remained alive. The trace can affect timing. The result does not establish whether a later frame would appear or identify the cause. The synthetic-disc and null-audio limitations still apply. Logs, captures, and executable, installer, and image identities are in `local/wine-runs/blackwindow-gl21-20261028/`. The container was removed, and the reference guard passed.

To try a different graphics backend, a separate image installed Debian's `mesa-vulkan-drivers:i386` package and requested Mesa Zink over the software Vulkan driver. The 32-bit Direct3D control used for the earlier red-window capture exited with code 3 before a capture. Wine logged failure to initialize OpenGL, find a suitable pixel format, and create its Direct3D adapter. The requested backend therefore did not pass the control in this environment, and no game run used it. The log does not establish the underlying cause. See `local/wine-runs/zink-control-20261028/` for the image build, image ID, control log, and exit status. Its container was removed, and the reference guard passed.

An independent macOS WineHQ 11.0 build was compiled from the official source in `local/wine-runs/winehq-source-20261028/`. It uses an x86-64 macOS loader with 32-bit Windows support. FreeType, GStreamer, and Vulkan were unavailable to the build, so fonts, video, and Vulkan are not verified. Homebrew bison was installed for the build. A 32-bit Direct3D control returned success from device creation and exited 0 after its clear and present loop, but its pixels were not observed. A clean 2005 game copy with the verified stock executable and synthetic disc shim passed the disc queries and remained alive at 49 seconds. Wine reported two display-mode change failures. The supported computer-use inventory and direct app lookup did not expose the Wine process or window, so no live capture or visible menu was established. The run was stopped; logs and the source-file comparison are in `game-run/`, and only `diag.log` changed among 7,857 compared source files, excluding the replaced executable. The reference guard passed. The separate CrossOver trial was not launched.

Wine's source bypasses the macOS display-mode switch in virtual-desktop mode. In separate 800 by 600 virtual-desktop runs, the game and desktop processes remained alive, the previous display-change errors were absent, and a `+system` trace recorded 800 by 600 modes being enumerated. The trace did not show the game's earlier failed request being retried, so it does not prove that request succeeded. The supported computer-use inventory still did not expose those windows. See `desktop-run/` and `desktop-trace/` under the WineHQ run directory.

An X11-enabled copy of that Wine build exposed the live stock window through XQuartz. `x11-game/tree-live.txt` identifies the 800 by 600 `SL2 build 606` window, and `game-live.png` records a black capture. That capture cannot establish what the game displayed: the same XQuartz capture method recorded the red Direct3D control as black, while Wine logged an incomplete framebuffer and `GL_INVALID_FRAMEBUFFER_OPERATION`. Wine 11 did not activate the old backbuffer setting. A forced OpenGL 2.1 control exited with an exception before capture. See `x11-control/` and `x11-game/` under the WineHQ run directory. No menu is established.

For each game run, the executable MD5 was checked before launch. Post-run SHA-256 comparisons against the extracted 2005 source found only `diag.log` changed among the 7857 source files, excluding the deliberately replaced executable. Additional diagnostic files were not compared. The comparison records are in each `local/wine-runs/blackwindow-*/data-comparison.txt` for the audio, graphics, composite, direct, direct-800, softpipe, single-thread, loading-trace, synchronous, and video-memory runs.

To repeat the combined file and Direct3D trace, build the base and diagnostic images as shown below. The commands need the 2005 installer extract and the read-only disc directory from `local/wine-runs/synthetic-run/stock-2005/disc`. They create a new writable run copy. The delay includes Wine prefix setup and is not a game-start timestamp.

```sh
RUN="local/wine-runs/blackwindow-repeat-$(date +%s)"
NAME="slrr-blackwindow-repeat-$(date +%s)"
SOURCE='.scratch/2005-extract/Program Files/Street Legal Racing - Redline'
DISC="$PWD/local/wine-runs/synthetic-run/stock-2005/disc"
mkdir -p "$RUN/game"
cp -R "$SOURCE/." "$RUN/game/"
chmod -R u+w "$RUN/game"
cp ref/raw/StreetLegal_Redline.bak.exe "$RUN/game/StreetLegal_Redline.exe"
test "$(md5 -q "$RUN/game/StreetLegal_Redline.exe")" = e686099e7233bb7d7c461b1b35c5064c
python3 -I tools/wine/synthetic_metadata.py ref/raw/StreetLegal_Redline.bak.exe ref/raw/texture.dat "$RUN/metadata.txt" "$RUN/SYNTHETIC-metadata.txt"
i686-w64-mingw32-gcc -shared -O2 -Wl,--kill-at -o "$RUN/game/mcicda.dll" tools/wine/synthetic_cd.c -lwinmm
i686-w64-mingw32-gcc -O2 -static -o "$RUN/game/synthetic_cd_probe.exe" tools/wine/synthetic_cd_probe.c -lwinmm
printf 'pcm.!default { type null }\nctl.!default { type hw card 0 }\n' > "$RUN/asound.conf"
docker build --platform linux/amd64 -t slrr-stock-wine-gl -f tools/wine/Dockerfile tools/wine
docker build --platform linux/amd64 -t slrr-stock-wine-composite -f tools/wine/Dockerfile.diagnostics tools/wine
docker run -d --name "$NAME" --platform linux/amd64 --network none \
  -e GALLIUM_DRIVER=llvmpipe -e LP_NUM_THREADS=1 -e WINEDEBUG=err+all,+file,+d3d \
  -e WINEDLLOVERRIDES=mcicda=n -e SLRR_SYNTHETIC_CD_METADATA=/work/metadata.txt \
  -e ALSA_CONFIG_PATH=/work/asound.conf \
  --mount type=bind,src="$PWD/$RUN",dst=/work --mount type=bind,src="$DISC",dst=/disc,readonly \
  slrr-stock-wine-composite bash -c '
    set -e
    export WINEPREFIX=/work/wineprefix
    wineboot -i >/work/wineboot.log 2>&1; wineserver -w
    ln -sfn /disc "$WINEPREFIX/dosdevices/d:"
    wine reg add "HKLM\Software\Wine\Drives" /v "d:" /t REG_SZ /d cdrom /f >/dev/null
    wine reg add "HKCU\Software\Wine\Drivers" /v Audio /t REG_SZ /d alsa /f >/dev/null
    wineserver -k; sleep 2; wineserver -w
    cd /work/game
    wine synthetic_cd_probe.exe >/work/probe.log 2>&1
    wineserver -k; sleep 2; wineserver -w
    timeout 420s xvfb-run -a -s "-screen 0 1024x768x24" wine explorer /desktop=slrr,800x600 StreetLegal_Redline.exe >/work/game-run.log 2>&1'
sleep 60
docker exec "$NAME" bash -c '
    set -e; export DISPLAY=:99; set -- /tmp/xvfb-run.*/Xauthority; export XAUTHORITY="$1"
    window=$(xdotool search --name "SL2 build")
    xdotool windowfocus "$window"; xdotool keydown Escape; sleep 5; xdotool keyup Escape'
sleep 60
docker exec "$NAME" bash -c '
    set -e; export DISPLAY=:99; set -- /tmp/xvfb-run.*/Xauthority; export XAUTHORITY="$1"
    ps -eo pid,etime,pcpu,stat,args > /work/process-later.txt
    wc -c /work/game/diag.log /work/game-run.log > /work/sizes-later.txt
    xwd -root -silent -out /work/root-later.xwd'
ffmpeg -loglevel error -y -i "$RUN/root-later.xwd" -pix_fmt rgb24 "$RUN/root-later.png"
docker stop "$NAME"
```


