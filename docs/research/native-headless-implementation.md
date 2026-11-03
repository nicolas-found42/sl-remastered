# Native invisible execution: implementation seams

Research date: 2026-11-01. This note proposes changes; it does not implement them or certify complete game parity. Two standalone probes ran with no game launch, window creation, host volume change, or host audio-device selection.

## Recommendation

Keep the real startup, video decoder, music decoder, sound voices, queues, gains, and render commands. Select SDL's **disk audio driver** before initialization to capture the process's final audio output without opening its CoreAudio backend. Preserve the existing separate logical streams: the disk backend receives the combined physical-device mix. Restore native intro execution in background Runs. For a stronger accelerated render destination, the installed SDL supports **a windowless GPU renderer using Metal**; it does not require a hidden drawable. These are engineering recommendations supported by the bounded probes below, not complete application results.

The shortest first implementation is disk capture plus background intro execution using the existing hidden window. A windowless GPU destination can then replace the hidden backbuffer behind `Render2D`, while a hidden nonfocusable window remains available if existing display/input code needs one. Avoid combining audio-device ownership changes with this work: that would complicate independent pause behavior unnecessarily.

## Repository seams, inspected directly

Source references describe the checkout inspected during research; concurrent implementation can move them.

| Existing behavior | Source | Implementation implication |
| --- | --- | --- |
| `AudioOutput::create` opens `SDL_OpenAudioDeviceStream(DEFAULT_PLAYBACK, F32 stereo 48000)` | `src/audio.cpp:7–23` | Selecting the driver upstream preserves queue, pause, resume, and meter behavior. The meter measures queued samples, not final output. |
| Music and video have separate `AudioOutput` instances | `tools/startup_probe.cpp:393–413` | A callback on one logical output cannot represent every output. |
| Sound voices open additional SDL device streams, including spatial and loop callbacks | `src/sfx.cpp:511–526`, `628–655` | Capture must include voices created after startup, without per-voice registration omissions. |
| Video decodes and queues audio using the video clock; natural completion leaves an audio tail to drain | `src/video_player.cpp:145–201`, `tools/startup_probe.cpp:1061–1074` | Preserve the real decoder and tail-drain path; do not substitute a null backend. |
| Native intros require `!noIntro && visibleWindow && runLoop && videoPlayer` | `tools/startup_probe.cpp:988` | Background execution currently omits this work. Allow either selected presentation mode, keeping explicit `--no-intro` behavior. |
| `Render2D` already contains both window and surface constructors | `src/render2d.cpp:9–24` | An offscreen constructor belongs here, rather than in a parallel renderer abstraction. |
| `Render2D::outputSize` uses `SDL_GetRenderOutputSize` when logical presentation is disabled | `src/render2d.cpp:150–157` | A windowless destination must use current target dimensions or set logical presentation before queries; main-output size can be zero. |
| Background initialization suppresses Cocoa activation before SDL initialization | `tools/startup_probe.cpp:142–151` | Retain these hints even if no renderer window is created. Independently observe application activation. |

Both bounded `jg` searches used the explicit `src`/`tools` roots. They reported incomplete discovery due to request limits and ended with `End context.`; the exact files above were then read directly. Missing discovery results were not treated as negative evidence.

## Audio: process-private output without dropping the mix

Installed headers identify SDL 3.4.18. Matching official release source was fetched and screened before reading. Its playback loop processes logical devices, mixes their streams, applies each logical postmix callback, and adds each result to the physical final mix before invoking the driver's `PlayDevice`. The disk driver's `PlayDevice` writes the received bytes to an `SDL_IOStream`; opening its playback destination uses `wb`. [SDL 3.4.18 audio implementation](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/audio/SDL_audio.c#L1232-L1297), [disk driver](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/audio/disk/SDL_diskaudio.c#L29-L145)

The distinction matters: `SDL_SetAudioPostmixCallback` is a **logical-device** facility. Its float buffer is final for streams bound to that logical device, after stream and logical gains; it is not a callback on the complete sum of every independently opened logical device. Capturing one music callback would miss video and SFX. Zeroing such callbacks also requires covering every later voice before resume. A selected disk backend avoids this interception requirement. [Installed matching audio API header](https://github.com/libsdl-org/SDL/blob/release-3.4.18/include/SDL3/SDL_audio.h#L2058-L2149), [callback registration implementation](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/audio/SDL_audio.c#L2005-L2029)

Configure the isolation policy once, before any SDL initialization:

```cpp
SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "disk", SDL_HINT_OVERRIDE);
SDL_SetHintWithPriority(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, outputPath, SDL_HINT_OVERRIDE);
SDL_SetHintWithPriority(SDL_HINT_AUDIO_DISK_TIMESCALE, "1.0", SDL_HINT_OVERRIDE);
SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "960", SDL_HINT_OVERRIDE);
```

Check every return value. After audio initialization require `SDL_GetCurrentAudioDriver()` to equal `disk`; any initialization/open/capture-path failure must terminate the isolated Run. Never retry using the host's default backend. Override priority also prevents an inherited `coreaudio` setting from selecting audible output. The filename hint must precede the first audio-device open. [Matching hint definitions and timing requirements](https://github.com/libsdl-org/SDL/blob/release-3.4.18/include/SDL3/SDL_hints.h#L467-L513)

Keep the final capture file path unique per Run and keep a playback owner alive through the Run: closing the last logical device and reopening the disk device can truncate the file because its open mode is `wb`. Obtain the actual device format with `SDL_GetAudioDeviceFormat`; emit a sidecar containing format, channel count, rate, frame size, sample-frame count, byte count, driver, file path, and run-clock anchors. Close the device before finalizing or analyzing its file. `F32LE` raw capture is enough for inspectable PCM; a WAV wrapper can be made after shutdown without changing playback.

Disk timing is paced, not sample-exact against wall time. The implementation computes `(sample_frames * 1000) / frequency` in integer milliseconds, then applies the timescale. At 48 kHz, default 1024-frame buffers nominally sleep 21 ms, rounding down from 21⅓ ms. The probe confirmed a 960-frame hint is accepted, giving an integral 20 ms period. Scheduler delays and file I/O still affect pacing; measure long-run audio/video clock drift. Keep timescale `1.0`; zero runs as quickly as the loop can consume and changes queue timing. [Disk timing implementation](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/audio/disk/SDL_diskaudio.c#L114-L133), [official timescale documentation](https://wiki.libsdl.org/SDL3/SDL_HINT_AUDIO_DISK_TIMESCALE)

Expected SDL startup logging uses `SDL_LogCritical` to announce this driver and its file/format. That text alone is not an audio failure. Preserve real error detection while distinguishing this known backend announcement in Run evidence. File write failure is consequential: the driver returns failure rather than guaranteeing capture continues. [Disk logging and write behavior](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/audio/disk/SDL_diskaudio.c)

## Rendering: windowless accelerated backing is available now

The installed public header documents `SDL_CreateGPURenderer(device, nullptr)` as an offscreen renderer. Create a `SDL_TEXTUREACCESS_TARGET` texture, select it with `SDL_SetRenderTarget`, and use `SDL_RenderPresent` to finish the frame. The SDL core explicitly permits this target-present combination for the GPU renderer without a window. This is SDL's `gpu` renderer backed by Metal on this machine, a different renderer implementation from SDL's older `metal` renderer. [Matching public API](https://github.com/libsdl-org/SDL/blob/release-3.4.18/include/SDL3/SDL_render.h#L354-L387), [target-present implementation](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/render/SDL_render.c#L5517-L5557)

Prefer a single `Render2D` factory that owns the renderer and backing texture. Resize by creating/selecting a replacement target and applying logical presentation to it; destroy textures before destroying the renderer. Existing texture uploads, geometry, blend modes, video textures, screenshot readback, and logical content sizing should continue through the same interface. Target dimensions and logical presentation state require explicit handling. `windowToRender`/`renderToWindow` cannot assume a renderer-owned window: preserve the existing synthetic coordinate semantics and verify letterboxing after a resize.

This removes a real hidden-window hazard. The legacy Metal renderer requests `CAMetalLayer.nextDrawable` when drawing to its main backbuffer, and its readback path can fail when no drawable is available. With a selected target texture it uses the texture directly. `SDL_FlushRenderer` alone is not a complete replacement for a frame submission contract: it drains SDL commands, while Metal command-buffer commit occurs through separate backend operations. Use the supported offscreen GPU contract. [Legacy Metal render destination and readback](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/render/metal/SDL_render_metal.m#L443-L479), [Metal readback synchronization](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/render/metal/SDL_render_metal.m#L1932-L1973)

## Bounded experiments and retained evidence

Artifacts are in ignored `local/verify/native-headless-implementation/`: `disk_probe.cpp`, `disk.log`, `mix.f32`, `offscreen_probe.cpp`, `offscreen.log`. Both probes compiled against `/opt/homebrew/Cellar/sdl3/3.4.18` with C++20 and the repository warning/contraction flags. They are research artifacts, not production tests or changes.

The disk probe opened two different logical devices, queued constant source levels 0.125 and 0.25, resumed both, then paused only the first. It read the closed disk file and failed unless both a combined 0.375 region and a 0.25-only region existed. The retained log records:

```text
SDL=3004018 driver=disk logical_a=21 logical_b=25
format=SDL_AUDIO_F32LE channels=2 freq=48000 sample_frames=960
callbacks_a=12 nonzero_a=12 peak_a=0.125000
callbacks_b=18 nonzero_b=18 peak_b=0.250000
remaining_a=291840 remaining_b=245760
disk_samples=36480 mixed_samples=23040 b_only_samples=11520 disk_peak=0.375000
disk physical final-mix plus independent pause probe: ok
```

The retained successful execution deliberately inherited both `SDL_AUDIO_DRIVER=coreaudio` and `SDL_AUDIODRIVER=coreaudio`; the code's override still selected `disk`. An additional attempt using a nonexistent output directory returned exit 6 at device creation and did not run the playback phase. It was not an end-to-end fail-closed application test.

The offscreen probe initialized SDL video with background/no-activation hints, created no window, cleared a 64×48 target, presented, and read an interior pixel on each of three frames. It required a null renderer window and exact pixels:

```text
renderer=gpu driver=metal window=0x0 frame=0 rgba=32,64,128,255 size=64x48
renderer=gpu driver=metal window=0x0 frame=1 rgba=64,64,128,255 size=64x48
renderer=gpu driver=metal window=0x0 frame=2 rgba=96,64,128,255 size=64x48
windowless accelerated GPU readback: ok
```

Neither probe launches the game or creates a frontmost-application observer. They establish their explicit mix, pause, and readback checks only. Exact result counts depend on scheduling and are evidence from one execution, not values a regression test should hardcode.

## Acceptance tests for implementation

1. An audio integration test uses the real output/music/voice paths with disk capture and known simultaneous signals. Assert mixed PCM regions, independent clear/pause, stereo panning, gain/frequency changes, looping, queue drain, tail preservation, and teardown. Include one real decoded intro and one music file. Injecting a dropped voice or replacing decoded PCM with silence must fail the meaningful checks.
2. A short normal background Run plays all three present native intros to completion, reaches the menu, captures changing intro images and nonzero intro PCM, then captures menu output. No implicit Escape or `--no-intro` shortcut. Correlate stage timestamps with audio sample offsets and video frames; missing files retain stock's explicit absence behavior.
3. A separate input test proves the user's actual held keys cannot accidentally drive the private scripted Run. In particular, review intro Escape polling and synthetic state. No global input injection or cursor movement.
4. A windowless renderer test exercises texture upload/update/release, geometry, alpha/additive blending, letterboxing, resize, consecutive frames, and readback. Compare selected scene outputs to the existing renderer with defined pixel tolerances; a clear-color probe alone does not establish renderer parity.
5. Observe `NSWorkspace.frontmostApplication` or activation notifications independently throughout launch, startup video, menu, display changes, failures, and teardown; reject activation of any test-owned process or descendant while allowing the user to switch among unrelated apps. Existing SDL window flags are useful internal evidence, not an independent system observation.
6. Test capture-path failure and hostile driver environment at the real application boundary; no host fallback is allowed. Isolate mutable game state, save/config paths, audio output, screenshots, and logs per worker before testing concurrency.

Physical speaker response, physical fullscreen scanout, and host audio-driver quality remain outside this isolated route. The application logic must still run, with those hardware effects replaced by private output destinations.

## Verification status

Primary remote source excerpts were screened with Jev and treated as task data. Jev automatically verified the disk file/timing API, windowless GPU API, three-frame readback result, and visible-only intro gate. It returned review-level confidence for the combined logical-to-physical mix interpretation and parts of the experiment narrative despite the explicit source and successful signal test; these were escalated to the parent implementation agent for independent inspection. No claim of complete game parity, complete no-focus certification, or a passing repository-wide gate is made here. Production files and commits were not changed by this research task.


## Passive foreground observer prototype

A later research pass built `local/verify/native-focus-observer/observer.m` and a pure replay classifier `classify.py`. The observer is a standalone Objective-C command-line program linked against AppKit/Foundation. It does not create `NSApplication`, create a window, request application activation, or inject input. It subscribes to `NSWorkspaceDidActivateApplicationNotification` on `NSWorkspace.sharedWorkspace.notificationCenter` before launching its explicitly selected child with `NSTask`. It also records `frontmostApplication` before launch, on polling transitions, and after exit, while servicing the main RunLoop. Notification records and polling records are retained separately rather than replacing one another.

These APIs are documented directly in the installed SDK's primary headers: `NSWorkspace.h:30–34` requires the workspace's own notification center; `:143–144` identifies the frontmost app as the key-event recipient; `:294` declares the activation notification. `NSRunningApplication.h:50–58` explains that this class covers user applications rather than every process, and that time-varying state can remain unchanged until the main RunLoop advances. `:109–112` notes that `launchDate` is unavailable for some apps launched without LaunchServices. Therefore the prototype records `launchDate` for audit but uses `proc_pidinfo(PROC_PIDTBSDINFO)` birth timestamps for process identity. The SDK `sys/proc_info.h:59–82` defines parent PID and start seconds/microseconds. These trusted local SDK headers were read directly; Apple documentation web fetches yielded no substantive text and were not used as evidence.

Ownership is an exact tuple `(pid, start_sec, start_usec)`, with executable paths retained as evidence and checked against the launched root's requested canonical path. Matching a bundle name, process name, path alone, or PID alone does not establish ownership. The observer's own identity is included, so self-activation is also a violation. Each foreground observation records the process ancestry available at that moment. Parent edges must agree with the captured parent PID, and a parent born after its child is ambiguous rather than valid ancestry. The prototype rereads the observed process identity to detect exit/reuse during sampling.

A process census every approximately 100 ms registers descendants whose parent identity is already owned, rechecking both identities before retaining the edge. Registered identities remain owned if they later reparent to PID 1. This was exercised by a CLI shell wrapper launching the two vetted binaries concurrently: `descendants.jsonl` registered both exact child identities and executable paths. This census does not discover a process that forks, reparents, and exits between samples. LaunchServices may create a process parented to an external service; an executable-path match is not sufficient to attribute that process to the test. Integrating explicit child registrations at the runner's spawn boundary can strengthen coverage.

The replay classifier reports one of `owned_activation_observed`, `inconclusive`, or `no_owned_activation_observed`. Arbitrary switches among unrelated user apps are allowed. A known test root, observed descendant, registered reparented descendant, or observer becoming foreground is a violation. Unreadable/ambiguous foreground ancestry, missing lifecycle records, or excessive observer sampling gaps produce an inconclusive observation rather than a false assertion that isolation passed. A later user-app poll does not erase an earlier owned activation notification. An unrelated process reusing a former root PID or running the same executable is not automatically test-owned.

Twelve pure replay scenarios passed without causing any desktop activation: unrelated user switches; owned root; owned descendant; registered reparented descendant; PID reuse; same executable in another process; unreadable identity; observer self-activation; notification-only transient owned activation; ancestor birth inconsistency; missing final record; excessive polling gap. The synthetic records exercise the failure classifier without deliberately stealing focus on the user's desktop.

The final actual passive executions were:

| Observed child | Child exit | Polls | Largest polling interval | Classification |
| --- | --- | --- | --- | --- |
| `build/offscreen_render_test` | 0 | 16 | 37.033 ms | `no_owned_activation_observed` |
| `build/private_audio_test` | 0 | 24 | 39.264 ms | `no_owned_activation_observed` |
| `/bin/sh` launching both vetted binaries | 0 | 25 | 39.111 ms | `no_owned_activation_observed` |

The observed foreground application was an external Codex process in each retained record. No real application-activation notification occurred during these short executions, so actual notification delivery during an activation remains untested. There was no instruction or attempt to make the user keep one app foreground. The classifier's 250 ms maximum-gap threshold only detects substantial observer stalls; it does not turn 20 ms target polling into continuous observation. Notifications reduce polling blind spots, but this prototype cannot claim zero missed events or prove that no subinterval ever activated an unknown short-lived process. Root identity capture and final termination are also races; failures must remain explicit.

Artifacts: `observer.m`, compiled `observer`, `classify.py`, `classifier-selftest.json`, and per-scenario `.jsonl`, `.jsonl.stdout`, `.jsonl.stderr`, `-summary.json` files under `local/verify/native-focus-observer/`. The recordings include only process/activation metadata and test stdout/stderr; they contain no screenshots or keyboard input. This is a research prototype, not a production gate integration. The parent implementation agent owns that integration and the repository-wide checks.

Reproduce without changing desktop focus:

```sh
clang -fobjc-arc -fblocks -Wall -Wextra local/verify/native-focus-observer/observer.m -framework AppKit -framework Foundation -o local/verify/native-focus-observer/observer
local/verify/native-focus-observer/observer /absolute/output.jsonl /absolute/build/offscreen_render_test 10
python3 -I local/verify/native-focus-observer/classify.py /absolute/output.jsonl
python3 -I local/verify/native-focus-observer/classify.py --selftest
```


The integrated native menu smoke was subsequently observed without additional production edits: `observer .../native-background.jsonl /opt/homebrew/bin/python3 100 -I .../tests/native_background_test.py`. Both explicit-flag and environment-selected background paths passed with the test's explicit `--skip-intro` diagnostic bypass. The observer registered the Python runner descendants and both `startup_probe` process identities. The outer child exited 0 after 4.153 seconds; 160 polls had a largest interval of 33.413 ms. Replay classified `no_owned_activation_observed`, with zero unknown or violation records. Artifacts are `native-background.jsonl`, its stdout/stderr, and `native-background-summary.json` in the observer directory. This menu smoke does not certify native intro coverage.

A full-intro script must avoid `0 keydown Escape`: the existing native startup parser treats that as held from process launch even if a `0 wait splash` line precedes it, so it skips native movies. A positive-time Escape action after a splash wait avoids that launch-held shortcut. The normal intro path and decoded PCM must be inspected separately before claiming full startup coverage.

Jev automatically verified the two vetted probe observations, the twelve-case replay result, the passive API/run-loop/birth-identity implementation, and measured polling gaps. The descendant-wrapper narrative was verified with review-level confidence (supports 0.85, confidence 0.77), so its raw ancestry registrations were also sent to the parent for independent inspection. No real focus-stealing action was used to test the rejection path.
