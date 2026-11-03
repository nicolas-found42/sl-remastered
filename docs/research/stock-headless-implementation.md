# Stock headless implementation experiments

Research date: 2026-11-01. Target: the 2005 Stock Executable, MD5 `e686099e7233bb7d7c461b1b35c5064c`, and its unchanged installed data, running inside a Linux container without host display/audio/HID access. This note records partial experiments and implementation leads; it does not claim deterministic Stock/Port Parity.


## Working configuration established

The successful route is **native ARM64 Hangover11.16 / Box64CPU0.4.4**, Xvfb, native ARM64 Mesa llvmpipe, and a private PulseAudio null sink. The final fresh-source CLI run reaches both full intro movies, Main Menu, the actual New Career name dialog, the actual Cancel button, and Main Menu again with native DirectInput press/release evidence and real PCM. `local/verify/stock-arm64-xtest-final-20261101/result.json` is `passed=true`, shell exit0, elapsed66.310s. All7,859 Installed Game source-file hashes are preserved; before/after manifest SHA256 is `925f85b3390144b6b9631d3c4c1d2490735dc14e932df7ea7f6cf9f2a3ed2a51`.

Reusable research artifact: `local/verify/stock-arm64-adapter/README.md`, `run.py`, `state_flow.py`, Dockerfiles, and necessary copied instrumentation/shim source. Qualified XTEST image: `slrr-private-stock-hangover1116-xtest-arm64-20261101`, ARM64 digest `sha256:c3105bb5c0fe829f89f6f782e79d7bfdb31708209ee4456e3ab4be5e7f94d88f`. It uses four container CPUs,3GiB RAM, no network or host display/audio/HID mounts, a private output/prefix, and a read-only disc fixture. Canonical `ref/` was not modified. These are runtime/flow qualifications, **not deterministic Stock/Port Parity**, actual CD-track audio, or normal user-quit certification.

Earlier failures and detector corrections remain below rather than being rewritten as successful runs.

## Reproducible evidence

All experiment scripts, command arrays, exact outputs, copied prefix, screenshots and PCM are under `local/verify/stock-headless-20261101/`. This directory is deliberately untracked because its private game copy contains game data. The only repository change from this investigation is this note. No `ref/` file was modified. All containers had unique names and were forcibly removed in a Python `finally` block, including timeout paths. The temporary image is retained for the parent implementation task, as requested.

Docker reports `aarch64`, 18 CPUs and 8,317,267,968 bytes RAM. `slrr-stock-wine-gl` is an amd64 Debian Bookworm image with Wine8.0, Mesa22.3.6 and no entrypoint. The experiment image `slrr-private-headless-audio-20261101` adds PulseAudio16.1, its CLI tools and `libxcomposite1:i386`; exact Dockerfiles and build logs are in the evidence directory. Installing PulseAudio does not change the parent image.

### Virtual display and GL

`probe.py` starts a fresh container, starts Xvfb explicitly, waits for `xdpyinfo`, then runs bounded `glxinfo -B`. Both runs succeeded:

| Configuration | Elapsed including container lifecycle | Observed renderer | Compatibility GL |
|---|---:|---|---|
| `GALLIUM_DRIVER=softpipe`, `LP_NUM_THREADS=1` | 0.692 s | softpipe, Mesa22.3.6 | 3.3 |
| `GALLIUM_DRIVER=llvmpipe`, `LP_NUM_THREADS=1` | 0.536 s | llvmpipe LLVM15.0.6, 256bits, Mesa22.3.6 | 4.5 |

Both used `--platform linux/amd64 --cpus 4 --memory 2g --network none -e LIBGL_ALWAYS_SOFTWARE=1`, with no host display mounts. Exact records: `softpipe.json`, `llvmpipe.json`. This did not reproduce the earlier 45s timeout recorded at `local/verify/headless-capacity-503sad4q/docker-virtual-gl.json`; it does not identify that earlier timeout's cause. Backend, thread cap, explicit Xvfb readiness and CPU/RAM allocations differed. GL availability alone does not establish successful game rendering.

Mesa documents that `LIBGL_ALWAYS_SOFTWARE=true` forces software rendering, `GALLIUM_DRIVER` selects softpipe/llvmpipe, and **`LP_NUM_THREADS=0` disables threading**; an unset value defaults to CPU-core count. Thus the existing `run_stock_synthetic.sh` default of zero is not an uncapped thread pool. [Mesa environment variables](https://docs.mesa3d.org/envvars.html)

### Retain audio processing without speakers

The base image contains the i386 Wine Pulse driver and libpulse, but no PulseAudio server, `pactl`, or `parecord`. The experiment starts a private server with only an explicit native UNIX protocol socket and null sink:

```sh
export PULSE_SERVER=unix:/tmp/pulse/native
mkdir -p /tmp/pulse
pulseaudio -n --daemonize=no --exit-idle-time=-1 \
  --log-target=file:/tmp/pulse.log \
  -L 'module-native-protocol-unix socket=/tmp/pulse/native auth-anonymous=1' \
  -L 'module-null-sink sink_name=stock rate=48000 channels=2' &
# Wait for pactl info, then capture before starting Wine.
parecord --device=stock.monitor --file-format=wav --rate=48000 --channels=2 /work/stock.wav &
```

PulseAudio documents that sinks expose monitor sources; the null sink therefore permits output recording. This configuration has no host audio socket or device and loads no automatic hardware modules. All audio stays inside the container. Successful server startup and a PCM file alone are insufficient: require nonzero expected signal, correct timing/channel format, and explicit silence assertions. [PulseAudio modules](https://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/User/Modules/)

The first actual Stock attempt cloned `local/wine-runs/blackwindow-lp1/wineprefix`, which explicitly selected `HKCU\\Software\\Wine\\Drivers` `Audio=alsa`. Its 32-bit WinMM tone helper failed `waveOutOpen=2`; ALSA reported a missing i386 Pulse plugin. Stock created D3D devices but produced two all-black screenshots and no input polls through the 90s deadline. Both WAVs were silent. This failed attempt is preserved under `attempt-alsa/`, including complete launcher output and game exit124. The outer container command exited0 because its diagnostic script intentionally continued after the game timeout; consumers must check the inner game exit and actual artifacts.

The next attempt changes only the private prefix's driver selection to `Audio=pulse`. The WinMM helper then reports `waveOutOpen=0`, `done=1`, while Stock renders the first intro movie: `attempt-pulse/dumps/frame_000030_t000013.bmp` has actual decoded movie content, and the proxy records 307,200 nonblack pixels. `attempt-pulse/sink-inputs-during.txt` identifies the active game stream as `Invictus Street Legal 2`, float32 stereo48000Hz, uncorked/unmuted, volume100%, connected to the null sink. A short helper WAV was nevertheless silent at first inspection; opening an audio device is not sufficient evidence of monitor capture. The finalized Stock capture proves actual nonzero audio: `attempt-pulse/pcm-metrics.json` reports stereo s16 48,000Hz, 4,250,638 frames (88.555s), 2,271,732 nonzero channel samples, absolute peak32,768 and RMS2,959.58. First/last nonzero frames are481,163/1,733,089. This establishes captured movie audio for this run; it does not establish effects/music/CD or waveform parity. The short WinMM tone remains a failing acceptance probe despite device success.

Both intro movies continued rendering after that observation, reaching Present150 at32.304s; then no further Presents or DirectInput reads were recorded before the90s deadline (game exit124). No menu or gameplay was established. The pulse attempt reports `XComposite is not available, using GLXPixmap hack` while creating the second movie's 640x400 device. The base image lacks `libxcomposite1:i386`; the next experiment layer adds it. This is an observed missing capability and a candidate remedy, not proof that it caused the stall.

The existing synthetic CD shim is retained and disclosed. Its own README says there is no CD audio and some metadata lengths are invented. This route can establish intro, file music and effects only when their real PCM is captured; it cannot certify actual CD-track audio or unmodified CD environment behavior. Full audio coverage needs an actual disc/audio source and observable MCI playback, or an explicit unresolved CD-audio gap.

### Final bounded results and remaining blocker

The `libxcomposite1:i386` retry is preserved in `attempt-composite/`. It removed the XComposite fallback warning and again rendered both movie devices, with Present 150 captured at 34.140 s. It still recorded no DirectInput state/data reads and timed out at the 90 s game deadline. Its finalized Stock WAV contains 4,278,560 stereo frames at 48,000 Hz (89.137 s), 2,270,746 nonzero channel samples, peak 32,768 and RMS 2,944.64. Thus the tested configuration retains real intro decode, render and audio processing invisibly, but **has not reached the menu or driving**. The log reports DirectShow/Quartz audio underruns and a dropped sample block; these captures cannot certify clean audio timing or waveform Parity. There was also a short pre-game diagnostic attempt (`attempt-composite-bootstrap/`) that stopped because `wineserver -k` returned nonzero after Wine had already exited; the diagnostic cleanup was corrected, and that attempt is not counted as a Stock launch.

A revised independent tone helper explicitly sets output volume to 100% and waits two seconds before reset/close. With its own cloned prefix and separate invisible container it captures a real signal: `tone-revised-metrics.json` reports 345,682 stereo 48,000 Hz frames, peak 10,000 and 82,422 nonzero channel samples. The requested tone was one second at 440 Hz, so complete sample duration/phase still needs stricter acceptance checks; the measurement establishes signal transport, not exact offline mixing. Both the helper source and exact container invocation are preserved. The original immediate-reset helper consistently captured silence despite `WHDR_DONE`, demonstrating why queue completion alone is inadequate.

Retained image ID: `sha256:10fe7d636e28fa8f902a8f7f8872314435fb6223a348521bd36ccb1bc6c6a6aa`, tag `slrr-private-headless-audio-20261101`. Rebuild with:

```sh
docker build --platform linux/amd64 -t slrr-private-headless-audio-20261101 \
  -f local/verify/stock-headless-20261101/Dockerfile.audio \
  local/verify/stock-headless-20261101
```

No experiment containers remain. Before repeating `run_game.py`, preserve/move existing artifacts into a unique attempt directory; it intentionally operates on this private experimental work directory and is not yet a safe public runner. A production runner must create an atomic unique directory per invocation, reject reused state, fail when the game deadline/input/milestone/audio criteria fail, and retain process/container cleanup in all paths.

The immediate next real-target task is to diagnose the **post-intro startup wait** with a verified process/thread stack and startup transition telemetry, retaining both movies/audio. The test has not shown a rendering capability failure after the intro. The initial debugger attachment used a log thread ID rather than a proven process ID and failed with error 87 (`attempt-composite/stock-winedbg.txt`), so it supplies no stack evidence. Enumerate Wine processes first; use the actual Stock process, then inspect its blocked main thread and DirectShow notification/graph completion state. If native debugging is needed, start the next isolated container with the explicitly scoped ptrace capability and a diagnostics image. Do not work around this by skipping the intro or disabling audio and then declare full functionality.

## Direct implementation seams for actual Stock/Port traces

The detailed primary static mapping already exists in `reference-parity-audit.md`, section “Candidate Stock Executable control seams.” This investigation independently inspected preferred-address disassembly of the immutable Stock Executable with `i686-w64-mingw32-objdump`; saved evidence: `pe-imports.txt`, `clock-disassembly.txt`, `rng-disassembly.txt`, `input-disassembly.txt`. These are instrumentation candidates, not validated live hooks.

1. Start a **record-only** DLL loaded before Stock entry using the existing proxy-loading route. Verify executable identity, each displaced byte sequence and expected image base; fail closed on mismatch. Keep a bounded in-memory event buffer with overflow counters. Do not perform file I/O in the observed step/input/RNG routines; flush at a separately identified safe boundary. Existing `walk_trace.c` demonstrates preserving general registers, flags and FXSAVE state, but each new hook still needs its own ABI/register-return proof.
2. First build a ledger around `00428450` and `004a5190`: frame/global-dt bits from `0063c534`, `this`, actual supplied substep bits, return value, selected verified state and phase/thread/caller identity. Retain outer Stock splitting, control accumulation, timers and cleanup. Hooking Present alone misses these semantics; the alternate step path `00428790` also needs observation.
3. Capture QPC/QPF, `GetTickCount` and `timeGetTime` using the executable's import table, preserving original calls and reporting caller/thread/read index. The inspected engine-clock routine `005516c0` calls QPF at `005e7050` then QPC at `005e7048`, subtracts the stored origin and scales. Record first, then experiment with replay of the consumed sequence. A frozen clock can change clock-budgeted Script dispatch; advancing on every read lets instrumentation itself perturb time. Clock values must be matched at actual consumption, with scheduler/animation clocks retained.
4. Extend `dinput8_proxy.c` at `GetDeviceState`/`GetDeviceData` to log full returned state/data and query order, not just injected down bits. Apply scheduled state at verified Stock input sample boundaries (`0054dda0` → `00556bc0`, sample counter `007686ec`) and retain the input helper's independent QPC delta. Deterministic playback must replace both polled state and buffered event streams coherently and record overflow/peek/capacity behavior. Current command-file sleeps and asynchronous OR-injected held state do not provide this.
5. Record seed and RNG state at `005d73fb`, `005d7408`, and native TLS ownership via `005dd8d3`. Disassembly confirms state at TLS+0x14, recurrence `state*0x343fd+0x269ec3`, result bits16–30. Both function entries begin with an x86 relative `CALL`; a trampoline must relocate that instruction rather than copy its five bytes unchanged. Start with an isolated helper that executes the real instructions and checks hook-off/hook-on outputs and FPU/register state, then validate in the actual game. Include every CRT consumer and reseed; Math.random alone misses other consumers. Playlist selection separately consumes `GetTickCount()%count` and filesystem enumeration order.
6. Only after repeated **Stock A versus Stock B** ledgers agree, map the same logical boundary/units/inputs/state into the Port and compare the earliest divergence. A helper passing or Port repeatability never establishes scenario parity. Retain intro/video/audio/resource work throughout; rejected/unsupported fields must remain explicit.

No production seam or clock/RNG override was implemented by this research. The full `tools/check.sh` gate was not run because this note-only investigation changes no executable behavior and the parent is coordinating implementation/validation. The experiments above themselves remain bounded diagnostics with their own exact outputs; no suite-wide passing claim is made.

## Research handling

The required `jg` query used the explicit `tools` root, bounded requests/output and concurrency4. It reported four locations-only leads and an incomplete request-limit notice; direct reads of focused Wine tools and existing research supplied the subsequent findings, without treating missing search results as absence. Firecrawl retrieved primary Mesa and PulseAudio material; Jev screened both as `pass`. Jev verified both API facts automatically. A broad combined GL/coverage claim was flagged review; the narrower exact measured GL-version claim was accepted with support0.99/confidence0.98. The narrow PCM-metrics claim was accepted with support 0.99/confidence 0.98; a combined media/coverage interpretation was flagged review, so the report keeps exact measured output separate from coverage claims. Remaining recommendations above are explicitly unverified implementation work.


## Post-intro diagnosis: actual failing loop and captured stacks

Follow-up on 2026-11-01 uses the diagnosing-bugs workflow. One repeatable command is now available:

```sh
python3 -I local/verify/stock-post-intro/repro.py
```

It atomically creates a new attempt directory, clones private game/prefix state, starts a uniquely named container, retains both intro movies and PCM, and after 60 seconds captures the virtual display. It requires OCR to find `MAIN MENU`, `FREE RIDE` and `OPTIONS`, and requires a positive recorded DirectInput poll count. The known-good native Wine Stock menu image at `local/wine-runs/capture-20261028-133131/shots/flow/00-main-menu.png` produces these OCR labels; it is an acceptance reference, not proof that the Linux run reached the menu. The first actual invocation returned shell exit 1 after 62.437 s: all labels absent, poll count 0 (`local/verify/stock-post-intro/run-2b5a1c1834/result.json`). The inner diagnostic container exited 0 after its planned capture/cleanup; the outer Python command correctly rejected missing game progress. The loop removes the unrelated tone helper and redundant warmup from the earlier experiment. Full required intro playback makes a subsecond reproduction inappropriate; no movie/audio bypass is used to shorten it.

`diagnose.py` repeats that actual path with bounded Quartz tracing and a 32-bit process/thread observer. It identifies the actual Stock process by name using Toolhelp, rather than confusing log thread IDs with process IDs. Run `run-4d433c1f32` found process 208 / main thread 212. Main contexts at 43 and 50 seconds both had EIP `02d4b5b2`, with changing stack pointers. External module/stack memory reads yielded no data, so that helper alone did not establish a stack.

`observe.py` copies a locally compiled diagnostic version of the existing D3D proxy into its private game copy. It adds an observer thread which briefly suspends other game threads, copies readable stack memory in-process, immediately resumes them, then writes artifacts. The Stock Executable/data files are unchanged; the additional diagnostic DLL/thread is disclosed. This observation can alter scheduling and must not be treated as an untouched control run. PEB module mappings and raw stack/EBP chains are preserved under `run-0b040809e7/game/dumps/self42.txt` and `self49.txt`.

At 49 seconds the main thread 216 resolves as follows, using the shipped Wine DLLs' own debug information and their observed load bases (`run-0b040809e7/resolved-stacks.json`):

| Captured address | Resolved location |
|---|---|
| `02d4b5b2` | `wined3d_cs_mt_finish`, `dlls/wined3d/cs.c:3221` |
| `02d50276` | `wined3d_device_context_emit_map`, `cs.c:2412` |
| `02d63a9f` | `wined3d_device_context_unlock`, `wined3d_private.h:5111` |
| `02d99e19` | `wined3d_resource_map`, `resource.c:322` |
| `02b9417d` | `d3d9_surface_LockRect`, `dlls/d3d9/surface.c:248` |
| `005a0f30`, then `00591350` | Stock return addresses; immutable-binary disassembly confirms `005a0f2d` makes the surface vtable call with offset `0x34` before `005a0f30` |

Worker thread 280 was at `opengl32!glDrawArrays`, called by `wined3d_context_gl_draw_primitive_arrays` and `wined3d_cs_exec_draw`. The main thread's 42-second snapshot was still in a file/module-loading path, so these observations do **not** establish a permanent deadlock from identical main-thread EIP alone. The Wine source's `wined3d_cs_mt_finish` spins until queue head equals tail; it explains the observed main-thread wait, not why the worker has not completed the relevant drawing.

Quartz trace shows a complete first graph lifecycle and a second lifecycle that reuses graph address `01896810`; there are four code-`0x1` notifications across the complete trace, two per lifecycle. After the second movie's notifications, the graph/filter release sequence progresses through GStreamer parser destruction and final reader release. This weakens the original missing-completion idea and moves the next experiment to the graphics command-stream dependency. Do not count graph-pointer equality as graph identity across free/reallocation.

The ranked, falsifiable graphics hypotheses are saved in `local/verify/stock-post-intro/graphics-hypotheses.md`: CSMT queue dependency, LLVM software-renderer/JIT behavior, a shared D3D/GL issue, or simply expensive but advancing startup. The first one-variable experiment uses `WINE_D3D_CONFIG=csmt=0x0`. Installed-version primary source `local/wine-runs/winehq8-source-20261028/wine-8.0/dlls/wined3d/wined3d_main.c:325-336` documents this environment syntax, its precedence over registry keys, and a log of the selected value. This changes a guest graphics setting while retaining rendering, intros and audio; it is not a game-rule change. Results follow below.

Primary Microsoft documentation was retrieved using Firecrawl and screened `pass` by Jev. `IMediaEvent::GetEvent` removes events from the graph queue; a second observer must not drain them to inspect completion. [Microsoft GetEvent](https://learn.microsoft.com/en-us/windows/win32/api/control/nf-control-imediaevent-getevent). The actual Stock loop's virtual call at offset `0x24` matches the separately observed Wine `MediaEvent_WaitForCompletion` trace. Jev verified the narrow main/worker stack mappings automatically (support 0.99 / 0.98). A combined earlier trace-count claim was flagged; inspection corrected the graph-address reuse and total count above. No root-cause fix or passing Stock menu claim has yet been made.


### Guest driver experiments (all remain red)

Exact comparison records are in `local/verify/stock-post-intro/driver-results.tsv` and each attempt's `result.json`. `csmt0.py`, `softpipe.py`, `gl21.py`, and `softpipe_gl21.py` each change one driver parameter from its stated comparison. Diagnostic variants wait 43 + 7 + 5 seconds around two bounded helper calls, rather than the base loop's exact 60-second sleep; their metadata was corrected to `observation_sleep_seconds=55`. All complete in about 58–60 seconds and use the same menu/input assertions.

- **CSMT disabled, llvmpipe:** `run-df9dc953f5` logs selected command-stream setting 0. The main thread now directly executes `opengl32!glDrawArrays`, with the same wined3d draw-family callers previously on the worker. Only two black Presents occur before the deadline; no intro or menu is reached. This falsifies CSMT disabling as a sufficient remedy and identifies the earlier queue wait as an intermediate dependency.
- **Softpipe, CSMT disabled:** `run-af3964d32c` exits the game during the first DirectShow video setup with fatal `GLXBadMatch / X_GLXMakeCurrent` at `VMR9FilterConfig_SetRenderingMode`. This is a distinct failure, not startup progress.
- **llvmpipe, CSMT disabled, GL 2.1 cap:** `run-04b82b9177` logs both selected settings but again produces only the two initial black frames and fails menu/input criteria.
- **Softpipe with that GL 2.1 cap:** `run-0f34dddd4a` repeats the fatal VMR9 GLXBadMatch. Neither feature cap is adopted.

A process listing during `run-04b82b9177` showed the actual Stock Linux process as `/usr/bin/qemu-i386 /usr/lib/wine/wine /usr/lib/wine/wine Z:\work\game\StreetLegal_Redline.exe`; the Wine server and services also used qemu-i386. This establishes the specific 32-bit Unix translation path being exercised. Image architecture alone did not establish it. It makes a newer 64-bit Unix WoW64 route worth testing, without asserting that translation is the proven root cause.

### Native macOS availability and bounded server probe

No native Wine/game was launched in this follow-up. Both local Wine11 `build/wine` and `build-x11/wine` are Mach-O x86_64. The latter still contains both `winemac` and `winex11` drivers; its name is not a guarantee of X11-only presentation. Wine11 `programs/explorer/desktop.c:1004-1028` reads `HKCU\Software\Wine\Drivers` `Graphics`; a future X11 lane must pin `x11` and explicitly disable `winemac.drv` fallback. Default macdrv is unsuitable for the no-activation contract: `cocoa_app.m:223-236` sets regular activation policy and invokes foreground activation, and `:1993-2006` can call `activateIgnoringOtherApps`. Existing MacDriver options inspected do not provide a documented blanket no-activation flag.

`/opt/X11/bin/Xvfb` is installed (universal arm64/x86_64, X.Org 21.1.23). Its installed signature has hardened runtime. The first safe, no-Wine probe could not start because `/tmp/.X11-unix` was absent and Xtrans requires root ownership; a temporary user-owned empty directory was removed. No sudo, ownership change or system setting was used.

A subsequent probe copied Xvfb to the local diagnostic directory and ad-hoc signed that private copy so it could load a tiny process-local `bind` interposer. The interposer forces IPv4/IPv6 listeners to loopback; a constructor sentinel and `lsof` verification gate client launch. The server uses a private mode-0600 Xauthority file, an unprinted random authentication cookie, TCP-only transport and no global Unix socket directory. Measured listeners were only `127.0.0.1` and `::1`. Both server processes were stopped in `finally`. No other applications' windows or audio were touched.

The native server's `xdpyinfo` succeeds at 800x600 and lists GLX, but **`glxinfo -B` prints “couldn't find RGB GLX visual or fbconfig” while returning 0**. Enabling documented `+iglx` produces the same failure. Exact evidence is in `native-xvfb-5f6abbd7/result.json` and `native-xvfb-826bb903/result.json`; sources/scripts are `native_xvfb_tcp.py`, `native_xvfb_iglx.py`, and `loopback_bind.c`. This installed native Xvfb is therefore not yet a demonstrated Stock D3D renderer. No game/audio investment or native launch follows from extension presence or exit0.

Current native Wine11 builds also lack `winegstreamer.so` and `winepulse.so`. Their configure log explicitly reports missing 64-bit GStreamer base development files and Pulse development files (`build/config.log:17233-17235`). Native historical captures that skipped/faulted intro decoding cannot count as complete intro coverage.

For future native audio isolation, Wine11 `winecoreaudio.drv/coreaudio.c:726-786` creates one HAL AudioUnit per WASAPI stream and installs `ca_render_cb` with `AudioUnitSetProperty(SetRenderCallback, Input, 0)`. Its callback consumes queued data and supplies silence when needed (`:392-443`). A process-local wrapper could copy generated PCM then zero only Wine's callback output, but those are per-stream samples before the final hardware combination, not proof of the combined mix. The local Apple SDK documents `CATapMuted` as capturing process audio while withholding it from hardware, and `CATapMutedWhenTapped` as withholding only while a client reads the tap. No process tap, audio permission request, device change or host mute was performed; permission/creation timing and complete Wine-process coverage remain unverified prerequisites to using that route.

### Supported 64-bit Unix WoW64 runtime experiment

Wine11's primary release text (`wine-11.0/ANNOUNCE.md:27-46`) says new WoW64 is supported and allows `WINEARCH=wow64` on a 64-bit prefix; pure win32 prefixes are unsupported in that mode. Its OpenGL section also documents an updated X11 EGL backend. The official WineHQ Bookworm package index confirms version `11.0.0.0~bookworm-1` (`winehq11-packages.json`, fetched over HTTPS and screened with Jev). A private reproducible layer `Dockerfile.wow64` installs that pinned official package from the signed WineHQ repository plus complete amd64 GStreamer base/good/libav, GL and Pulse dependencies. It built successfully in 60.365 seconds as `slrr-private-stock-wine11-wow64-20261101`; the earlier Wine8 environment remains separately named.

`wow64.py` creates a fresh 64-bit prefix, explicitly selects Pulse, sets the read-only disc mapping, verifies the Stock MD5, retains the same private Installed Game and media pipeline, and applies the original menu/input assertions after 60 game seconds. It removes earlier CSMT/GL-version experiments and records actual runtime/process provenance. Outcome is recorded below when complete; package presence alone is not execution success.


#### WineHQ11 new-WoW64 results and translator preflight

The first WineHQ11 attempt `run-ddf3ca982f` is red after 75.491 s. `wine-version.txt` records Wine11.0; `processes20.txt` records the x86_64 Unix loader and services rather than qemu-i386. No Stock process/menu/input was observed. Prefix bootstrap logged a Rosetta `rt_sigreturn` assertion about XSAVE feature bits, so this runtime is not qualified merely because its services started.

`wow64_direct.py` removes Explorer indirection and first invokes a tiny actual 32-bit Windows console executable that reports pointer size and WoW64 status. The helper is a runtime entry preflight, not a substitute Parity target; the same unchanged Stock invocation follows. In `run-ec727c5138`, this helper exits 139 before printing its main output, direct Stock does not establish progress, and the final menu/input gate is red after 64.140 s. This identifies a 32-bit runtime entry failure before the graphics scenario, without proving that the earlier bootstrap assertion caused every failed launch.

A separately named auxiliary image adds native ARM64 QEMU7.2 `qemu-x86_64-static` so the complete 64-bit Unix Wine path can be explicitly translated without Rosetta. Source inspection found Wine11 no longer reads `WINELOADER` and prioritizes canonical loader/server locations; environment variables alone would not prove translator control. The private image therefore preserves original binaries as `wine-real` / `wineserver-real`, wraps their canonical paths, and removes only the private Wine preloader so the source's documented exec fallback reaches the wrapper. No host binfmt registration or runtime setting is changed. `run-92f712ee09/processes20.txt` establishes that the Wine server/services/Stock actually use that native ARM64 QEMU64 executable. It still fails the 32-bit helper deadline with repeated `unexpected trap -1`; Stock produces the same error family, with no D3D startup or menu. The actual menu/input loop is red after 74.985 s. This QEMU7.2 variant is not a usable WoW64 route.

Primary Box64 documentation describes a further distinction: x86_64 Wine WoW64 can execute x86 Windows programs without Box32, but its 32-bit WineD3D performance has known limitations. Primary Hangover documentation instead describes native ARM64 Wine, with only Windows application code emulated and Unix graphics/audio outside emulation. Its default 32-bit emulator is `wowbox64.dll`; FEX is an available alternative. The official release API supplies a Debian12 ARM64 Hangover11.16 bundle dated 2026-08-30, SHA256 `dd7373202a168b7200cc36627a25b588511f3783756c2661bf4c022be8380aa8`. These documents were retrieved with Firecrawl and screened pass. The native ARM64 layer `Dockerfile.hangover` installs the verified official package bundle and full native GStreamer/GL/Pulse dependencies; its actual entry/game result is pending, not implied by upstream compatibility claims. [Box64 Wine guide](https://github.com/ptitSeb/box64/blob/main/docs/WINE.md), [Hangover design and runtime selection](https://github.com/AndreRH/hangover), [official Hangover11.16 release](https://github.com/AndreRH/hangover/releases/tag/hangover-11.16).


### Final input qualification, reusable artifact and exact limits

The native ARM64 route first reached an actual menu in `run-a1fd1292ac`:1,483 input polls and visible menu pixels, although default full-image OCR initially failed on small italic labels over moving video. A fixed-region classifier was validated against the actual new menu, a known native Stock menu, and the prior stalled capture. `run-ad9a4d4647` then passed the repaired menu/input gate with both actual movies captured at timed checkpoints and real audio (2,904,833 stereo frames at48kHz,4,462,244 nonzero channel samples). Movie3s/18s/24s frames were independently inspected; no reference bitmap was inserted into output.

`run-dde5eadd93` visibly entered the New Career name dialog and returned through its actual Cancel control. Proxy logs show polled mouse-down observations for both clicks. A newly packaged fresh-source timed run likewise displayed the dialog, but another repeat did not open it despite a returned polled press. The latter was a real input-handoff/readiness failure, not merely OCR. Source comparison found only the deliberately replaced executable and a diagnostic log changed from the prepared source; it did not establish changed game controls as the cause. Both failures and successes remain in the evidence tree.

The recommended local adapter now observes UI state before acting and sends coherent XTEST events only inside its private X server. It waits for menu labels plus input polling, reads Stock's actual client rectangle and origin, clicks New Career, observes title and Cancel control, clicks Cancel, and observes menu return with the dialog absent. The DInput observer records returned **underlying native** button states without replacing them. A bounded diagnostic retry is recorded if used, but qualification requires exactly two actions: one New Career click and one Cancel click.

The final fresh-source CLI run at `local/verify/stock-arm64-xtest-final-20261101` passes in66.310s. Actual client rectangle is `(0,0,800,600)`, origin `(0,0)`; requested client/screen points are `(300,120)` and `(480,470)`. Native mouse state observations are exactly `UP, DOWN, UP, DOWN, UP`, with transitions in the proxy log at34.483/41.032/41.312/45.192/45.473 seconds from that proxy's recorded origin. There are651 sampled input-poll calls in the last statistics row, zero queue drops, and no extra click/retry. The final image is `state-034-await-menu-return.png`; the name-dialog checkpoint is `state-033-await-name-dialog.png`.

Its mixed PCM is stereo s16,48,000Hz,2,263,227 frames,3,197,890 nonzero channel samples, peak32,768 and RMS4,176.94. `source-before.json` and `source-after.json` are byte-identical7,859-file manifests. This certifies preservation of the supplied source tree through this case; runtime variability, random choices, audio sample alignment and full scenario determinism are still separate work.

An earlier state-driven run recorded an already verified terminal menu checkpoint but then failed its outer checker on a redundant later frame whose moving background confused OCR. Its original `result.json` is retained. `qualification.json` records reassessment against the actual terminal checkpoint and image hashes; the final runner makes that boundary authoritative and keeps the later root image debug-only. This correction does not create or copy expected images, and does not accept an unobserved menu transition.

Jev verified source preservation and PCM claims automatically. Its full local-artifact code gate escalated at composite0.730 / safe-to-apply0.28 for generic low-confidence/test-gap/blast-radius rubrics; it reported no actionable defect or contradicted/unsupported claim. The broad combined UI completion claim also remained low-confidence despite the explicit fields. The artifact is research code for independent review before promotion, with that full result in `local/verify/stock-arm64-adapter/jev-gate.json`; no production approval or exhaustive test-coverage claim is made. The repository-depth reference-root resolver is explicitly documented for adjustment if the files move.

### RNG CALL-relocation proof

`local/verify/stock-rng-relocation/proof.c` checks actual Stock `_srand`/`_rand` bytes, executes the original recurrence, relocates the entry `CALL` into an executable trampoline, patches the helper's mapped entry to an integer-only observer, and compares48 unhooked versus48 hooked draws and resulting states. All512 FXSAVE bytes match before/after each tested call. The original relative target and relocated target are identical; deliberately omitting relocation is rejected before execution with expected exit9. Positive proof exits0 in0.609s; negative guard control exits9 in0.729s. Both helpers run under the qualified native ARM64 runtime with the Stock file mounted read-only and private prefix; no game overlaps these console probes.

The helper **replaces only its mapped TLS getter with an explicit synthetic state block** to isolate the relocation/ABI mechanism. It does not prove live-game TLS ownership, seed selection, RNG call order, all FPU states or whole-game behavior. Its mapped image landed at `00770000` while the file's preferred base remained `00400000`, so the proof uses checked RVAs instead of assuming the mapped base. The actual game is still a separate target. Evidence and exact launcher: `proof-positive.log`, `result-positive.json`, `proof-no-relocation.log`, `result-negative.json`, `run.py` in that directory. The neighboring control-tape subtask owns actual record-only IAT clock/input capture; this note does not relabel that work as deterministic playback.

The RNG negative-control claim was accepted automatically by Jev (support0.99/confidence0.98); the combined48-draw/FXSAVE summary remained review-level, so the complete log and source are retained for independent inspection rather than a blanket ABI-safety claim.

### Boundary for original-code physics helpers

Focused inspection of `tools/wine/core_pair_oracle.c` and `contact_geometry_oracle.c` found a reusable but deliberately partial setup: verify original mapped code against file bytes, apply PE `IMAGE_REL_BASED_HIGHLOW` relocations, substitute the three allocator routines at RVAs `14f560/14f5b0/14f580`, and call original RVA `aa560` before original core constructors. These sources do not implement complete import resolution or game startup. Their hand-built body/prototype buffers and chosen x87 control word are experimental inputs, not a complete live Stock snapshot.

In the retained original decompilation, `FUN_004aa560` (`local/census/all.c:124577`) only fills the box face-index tables at `00644580..00644630`. Ground discovery uses a separate candidate-list initializer, `FUN_0049eb10` (`117189`), which also calls `FUN_005d707d(FUN_0049eb60)` for exit registration. `FUN_0049ec00` immediately uses that list and calls `00497280` per candidate. Consequently, successful geometry constructors and the narrow RNG relocation proof do not establish the prerequisites for the full ground-selection/admission path. Any bypass of exit registration, cache lookup, allocation or body initialization must be disclosed and cannot establish retained-state or full-scenario Parity. The contact investigation received this boundary and the qualified ARM64 console launcher; this inspection launched no additional runtime. Jev verified these source-scope claims at confidence0.88 and1.0. A final `docker ps` check found no live containers.
