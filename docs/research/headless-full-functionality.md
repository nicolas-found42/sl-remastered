# Headless execution with full application functionality

Research date: 2026-11-01. Scope: keep every test and probe invisible and independent of the user's foreground app, while exercising rendering, input, audio, video, display configuration, and game behavior. This is a design note, not an implementation or a claim that the complete pipeline has passed.

## Recommendation

Make presentation an output adapter: run the real game logic, asset loading, decoding, rendering commands, audio generation, and input state machine, but route their outputs to offscreen textures/surfaces and PCM artifacts. Run the stock Windows oracle inside its own Linux virtual X display and private audio server. Keep a fast native deterministic lane and a smaller compatibility lane. Treat parity between these lanes as an explicit measurement.

The word “headless” must not mean “disable the subsystem.” Dummy audio can support useful queue/mixing tests, but success opening it is not evidence of correct PCM, music, effects, stereo positioning, timing, or simultaneous playback. Likewise a software renderer is useful coverage but does not establish Metal backend parity. A virtual display can exercise fullscreen requests within the guest, but it does not establish behavior of the physical macOS desktop.

## Current repository capability and gaps

Directly inspected local code:

- `tools/startup_probe.cpp` sets `SDL_HINT_MAC_BACKGROUND_APP=1` and disables activation when shown/raised, with override priority, before initialization in background mode. It creates a hidden, nonfocusable window. This addresses known activation opportunities; independent observation is still needed.
- Background mode applies display settings with `isWindowed || backgroundWindow`. It preserves the game configuration fields and content dimensions but changes the host presentation to windowed. It explicitly logs that the native intro was bypassed; startup intro playback requires `visibleWindow`. Thus background startup is not yet an identical execution of all startup features.
- `tests/native_background_test.py` exercises flag and environment selection, reaches the menu, checks a BMP signature, checks save preservation, and requires hidden/fullscreen/input-focus flags `1/0/0`. It selects dummy audio. It does not assert the system's frontmost PID, intro decode/audio behavior, screenshot pixels, or final mixed PCM.
- `src/render2d.cpp` already has `Render2D::forSurface`, using `SDL_CreateSoftwareRenderer`, alongside the window renderer. `saveFrame` uses renderer readback. This is an existing seam for truly windowless functional rendering.
- `src/audio.cpp` opens a separate SDL audio device stream per `AudioOutput`. Its meter measures samples when queued, before device consumption; it does not capture the device's final mix. `tests/audio_test.cpp` checks queue capacity, draining, pause, clear, and recreation with dummy audio. These are useful tests with a narrower meaning than audible-output equivalence.
- `tests/video_player_test.cpp` already draws decoded video to an SDL surface and checks selected pixel values. Extend this pattern to startup intro and audio behavior instead of omitting the feature in the headless lane.

These observations describe source inspected during this task; nearby work can change them. No application was launched for this research.

## Native rendering: preserve the work, replace presentation

SDL officially supports a renderer whose destination is a surface. This can exercise the same `Render2D` operations without a desktop window. Keep this as the deterministic, inexpensive lane, including video textures, geometry, blending, logical resolution, screenshot output, and resize/configuration scenarios. It establishes software-path results, not GPU-driver equivalence. [SDL_CreateSoftwareRenderer](https://wiki.libsdl.org/SDL3/SDL_CreateSoftwareRenderer)

For the accelerated lane, use an explicit render-target texture rather than depending on a hidden window's backbuffer lifetime. SDL target textures use `SDL_TEXTUREACCESS_TARGET`; target-specific viewport, clipping, scale, and logical presentation must be configured because that state persists separately per target. Read pixels after drawing and before presenting when using a main render target. SDL warns that readback is slow: capture milestones and failures rather than every frame. [SDL_SetRenderTarget](https://wiki.libsdl.org/SDL3/SDL_SetRenderTarget), [SDL_RenderReadPixels](https://wiki.libsdl.org/SDL3/SDL_RenderReadPixels)

If SDL's chosen accelerated renderer still depends on a window/drawable, a windowless Metal adapter is feasible engineering, not an environment-variable promise. Apple's sample creates an offscreen texture and a custom render pass independently of the view-provided pass. Onscreen presentation is the additional step of acquiring and presenting a Core Animation drawable. [Customizing render pass setup](https://developer.apple.com/documentation/metal/customizing-render-pass-setup), [Onscreen presentation](https://developer.apple.com/documentation/metal/onscreen-presentation)

For Metal artifacts, blit private textures to CPU-accessible storage and wait for the producing GPU work to complete before reading. Prefer asynchronous completion plus a small ring of capture buffers for throughput; blocking `waitUntilCompleted` is simpler for a bounded diagnostic. Apple's example notes that unnecessary synchronization reduces CPU/GPU parallelism. [MTLTexture.getBytes](https://developer.apple.com/documentation/metal/mtltexture/getbytes(_:bytesperrow:from:mipmaplevel:)), [Reading pixel data from a drawable texture](https://developer.apple.com/documentation/metal/reading-pixel-data-from-a-drawable-texture)

Prototype acceptance: the same short scene on surface and accelerated offscreen paths produces nonempty image artifacts with known viewport, alpha/blending, geometry, and letterbox checks; successive frames change when expected; long hidden runs never stall on drawable acquisition; captures are taken at the same simulation tick. Record actual SDL version and renderer rather than assuming Metal was selected.

## Input: inject into the application, not the host

SDL queue injection alone is insufficient for all input. SDL explicitly says pushed device events do not update internal device state. A test can therefore observe a key-down event while polling still reports the key up. Inject through the existing application input seam so both events and held state advance consistently, with explicit timestamps and ticks. Keep a separate adapter test for SDL-to-application translation. Do not use global macOS key/mouse events, cursor warps, or host focus changes. [SDL_PushEvent](https://wiki.libsdl.org/SDL3/SDL_PushEvent)

Prototype acceptance: press/hold/release, simultaneous keys, repeat, modifier changes, relative mouse motion, buttons, wheel, text entry, controller axes, cancellation, and synthetic focus-loss transitions behave consistently. Check both event consumers and state polling. Replaying identical input must reproduce the same application checkpoints without receiving the user's actual keyboard or pointer activity.

## Audio: capture the final pipeline

SDL's postmix callback exposes the final mix of all streams bound to an opened, unpaused device; it also fires on silence. Its float32 channel count and sample rate can change, and it runs on a background thread. Copy samples into a bounded buffer and do encoding/file I/O elsewhere. Capture every relevant logical device or, preferably, bind application streams to one test-owned logical device so combined music/effects/video output is observable. [SDL_SetAudioPostmixCallback](https://wiki.libsdl.org/SDL3/SDL_SetAudioPostmixCallback), [SDL_AudioPostmixCallback](https://wiki.libsdl.org/SDL3/SDL_AudioPostmixCallback)

First validate that the installed SDL dummy backend drives this callback with the real streams. If it does, it can retain conversion, queueing, gain, and mixing work while keeping speakers silent. This is a prototype question, not a result established here. For deterministic offline conversion, unbound SDL audio streams can be created with explicit input/output specs; this supplements rather than replaces the device-mix test. [SDL_CreateAudioStream](https://wiki.libsdl.org/SDL3/SDL_CreateAudioStream)

Prototype acceptance: known tones from multiple simultaneous sources appear at expected sample positions, channel levels, and gain; clipping, silence, pause/resume, clear, looping, teardown, and queue saturation are checked on captured samples. Add decoded intro/video PCM, music transitions, sound effects, and spatial audio. A queue meter is insufficient because queued-but-never-consumed samples can still score as nonzero.

## Stock Windows oracle: isolate display, input, audio, and mutable state

Xvfb is explicitly designed to run without display hardware or physical input devices, using a virtual-memory framebuffer. Run Wine against a private X server inside the Linux environment, with no host X socket or visible viewer. XTEST can synthesize key, button, and motion events within that server, so guest focus can change without moving macOS focus. Give each worker its own display, Wine prefix, writable save/config copy, artifact directory, and timeout cleanup. [Xvfb manual](https://www.x.org/docs/man/man.pdf), [XTEST protocol](https://www.x.org/releases/X11R7.6/doc/xextproto/xtest.pdf)

Use a private PulseAudio server with `module-null-sink` and record the sink's monitor source. The null sink drops output but provides a monitor, preserving an observable audio route without host speakers. Start recording before the scenario; assert samples, duration, channels, silence boundaries, and peak levels. This is broader than muting or selecting an unobserved null device. [PulseAudio modules](https://wiki.freedesktop.org/www/Software/PulseAudio/Documentation/User/Modules/)

Xvfb alone does not prove the game's Direct3D/OpenGL path works. Check the actual Wine graphics backend and renderer inside the display; verify representative game frames, intro decode, menu interaction, driving, audio, and virtual display-mode changes. Software GL, Zink, and any GPU-backed route are separate configurations whose parity and speed require measurement. No retrieved primary evidence establishes that this exact stock game already passes all of those configurations.

Current machine availability reported by the parent investigation: Docker is running an aarch64 Linux environment with 18 CPUs but only about 7.75 GiB VM RAM; the named stock-Wine and Zink images exist. A 2-CPU/1-GiB Xvfb plus GL smoke timed out after 45 seconds and was cleaned up. That bounded result is not evidence that every allocation or backend fails. See `local/verify/headless-capacity-503sad4q/docker-virtual-gl.json` for the parent-produced run record.

## Throughput and transfer limits

Use native ARM64 builds and surface/offscreen tests for the inner loop. Keep warmed, isolated Wine environments for selected end-to-end comparisons; avoid recompiling unrelated tooling under x86 emulation. Docker documents potentially substantial QEMU slowdown for CPU-heavy work, and its Docker VMM currently lacks Rosetta support. The actual VMM, image architecture, translator, and graphics backend must be recorded; a generic fixed emulation multiplier would be invented. [Docker multi-platform builds](https://docs.docker.com/build/building/multi-platform/), [Docker virtual machine managers](https://docs.docker.com/desktop/features/vmm/)

The 48-GiB host memory is not the container budget. Start concurrency conservatively, measure per-worker peak RSS, VM pressure, elapsed time, and graphics/audio stalls, then increase workers while aggregate throughput improves. Bound CPU-intensive oracle jobs separately from native tests and capture jobs. Allocate unique mutable state, reuse immutable assets read-only, and retain a bounded artifact set with complete failures. More processes can reduce useful throughput when they contend for translation, software rasterization, GPU readback, or the current 7.75-GiB VM.

## No-focus verification and unavoidable physical limits

The SDL background hint is documented as permitting background launch and must be set before initialization. Interpret the wording literally; hidden/nonfocusable flags and a successful menu test are not independent observation of the foreground application. Observe `NSWorkspace.frontmostApplication` or activation notifications throughout startup, interaction, error, and teardown. Apple's API identifies the recipient of key events. Keep its observer run loop progressing, since dynamic application properties can otherwise remain stale. [SDL_HINT_MAC_BACKGROUND_APP](https://wiki.libsdl.org/SDL3/SDL_HINT_MAC_BACKGROUND_APP), [NSWorkspace.frontmostApplication](https://developer.apple.com/documentation/appkit/nsworkspace/frontmostapplication), [NSRunningApplication](https://developer.apple.com/documentation/appkit/nsrunningapplication)

A strict invisible-only suite can retain and inspect application rendering, input handling, decode, audio mixing, and virtual display changes. It cannot truthfully certify physical display scanout/HDR/color management, macOS Space/fullscreen transitions, actual monitor-mode switching, human key-to-photon latency, physical HID/hotplug, speaker/headphone acoustics, or real device-driver dropouts by substituting virtual devices. Record those as outside headless certification; do not silently delete their application logic or claim dummy substitutes established hardware equivalence.

Next design frontier: define the test-owned clock, input-state source, render destination, display-mode model, and audio capture destination as explicit adapters; then build one short end-to-end scenario that retains intro video/audio, menu, driving, screenshot, save, and clean shutdown in both native and Wine virtual lanes. Prove observability and isolation before increasing concurrency.

## Evidence quality

Primary SDL, Apple, X.Org, PulseAudio, and Docker material was retrieved and screened with Jev before use. Firecrawl page retrieval stalled/failed; web search supplied primary documentation. Jev verified the central API facts against the retrieved text. An initial event-injection claim lacked evidence in its supplied batch because that fetch failed; a fresh primary SDL excerpt verified it. The broader claim that SDL's background hint guarantees system focus was deliberately not made. Recommendations and acceptance probes above are engineering proposals, not passing test reports. No production files, commits, pushes, GUI launches, or system settings were changed by this research task.
