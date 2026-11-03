# Native process isolation: measured limits on macOS 27

Research date: 2026-11-01. Host: macOS 27.0.1, build 26A434; installed SDL 3.4.18. This supplements [native-headless-implementation.md](native-headless-implementation.md). Only this research note is a tracked change. Experiment sources, profiles, binaries, logs, and the final result manifest are in ignored `local/verify/native-process-isolation/`.

## Practical recommendation

Keep the enforced SDL disk destination, real decoders, final mix, and windowless GPU target as the native implementation. Keep passive foreground observation as independent evidence. There is a measured **additional audio defense** available on this machine: launch a fresh child under `sandbox-exec` with Mach lookup denied for `com.apple.audio.coreaudiod` and `com.apple.audio.audiohald`. The tested CoreAudio open then fails, while the tested disk mix, GPU readback, native intro decode, and complete menu MP3 decode still succeed. This is a candidate for an explicit experimental runner mode, not a supported or complete host-output boundary.

Do not silently make this a production prerequisite: the installed `sandbox-exec(1)` manual explicitly marks the command deprecated. The public SDK marks `sandbox_init` "No longer supported" and permits only named-profile flags in its documented API. These are concrete support limits, even though the measured custom profiles currently execute. [Installed SDK `sandbox.h`, lines 24–49](/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/sandbox.h:24)

No supported, independently enforced process policy meeting **both** complete output prevention and no activation while preserving this renderer was established by this investigation. Do not describe the current driver assertions, an observer pass, or this device-open failure as such a guarantee. A stronger host boundary remains unresolved.

## Why ordinary App Sandbox is insufficient

Apple describes App Sandbox as resource access control through entitlements. However, the installed OS's own App Sandbox application profile calls `(coreaudio-services)` and `(audio-output)` unconditionally. Only the nearby audio-input permission is conditional on microphone/audio-input entitlements. `appsandbox-common.sb` explicitly permits the two hardware services above and AudioComponentRegistrar; its audio-output helper permits AudioConverterService and output IOKit clients. Omitting a microphone entitlement therefore does not implement an audio-output prohibition. This conclusion is about the inspected macOS 27 profiles; these system profile internals are not a stable public policy API. [Apple App Sandbox](https://developer.apple.com/documentation/security/app-sandbox), local `/System/Library/Sandbox/Profiles/application.sb:720,826–829` and `appsandbox-common.sb:496–524,565–575`.

The installed `sandbox(7)` manual also says restrictions generally apply when acquiring resources and newly created processes inherit their parent's sandbox. Its example warns that already-open resources can remain usable. Apply any experimental deny before starting the native executable, with no inherited audio connections, and keep the supervisor outside that deny. Do not apply a profile after audio initialization and assume it revokes existing channels. Inheritance is documented by that manual; cross-service launches and handed-off Mach rights were not tested here.

## Safe audio experiments

`paused_audio_probe.cpp` directly looks up four known local audio services, selects SDL's CoreAudio backend, and calls `SDL_OpenAudioDeviceStream` with a null callback. It **never supplies audio samples, installs a callback, or calls resume**. SDL's installed public header documents that this convenience stream starts paused. The successful control independently reports `paused=1` and `queued_bytes=0`. Thus an ineffective deny could not cause this probe to play a nonzero source signal. This statement concerns the probe's source/data, not an assertion that the backend never starts an internal hardware queue. [Matching SDL stream API](https://github.com/libsdl-org/SDL/blob/release-3.4.18/include/SDL3/SDL_audio.h#L1997-L2058)

No volume, mute, default device, system audio preference, aggregate device, or process tap was changed. No application activation was requested and no focus-stealing rejection test was performed.

| Scenario | Measured result |
| --- | --- |
| Unsandboxed paused CoreAudio control | Lookup succeeds for coreaudiod, audiohald, AudioComponentRegistrar, and AudioSession; `driver=coreaudio stream=open paused=1 queued_bytes=0`; exit 0 |
| Broad audio-service deny (`audio-mach-deny.sb`) | All four lookups return 1100 with invalid ports; SDL reports `No default audio device available`; no stream opens |
| Narrow hardware-service deny (`hardware-mach-deny.sb`) | coreaudiod/audiohald lookups return 1100; registrar/session lookups still succeed; same SDL open failure |
| Menu MP3 decode under broad deny | Fails at setting AudioToolbox client data format: `format_status=1718449215`; exit 5 |
| Complete menu MP3 decode with no deny and narrow deny | Both exit 0 at EOF with 11,012,806 frames, peak 1.125990, FNV-1a-64 sample checksum `8518448cc66fc569`, 48 kHz stereo |
| Existing `build/private_audio_test` under narrow deny | Exit 0; disk F32LE stereo 48 kHz; 32,640 captured samples, including 17,280 samples at the expected combined signal and 13,440 with only the independently playing stream |
| Existing `build/offscreen_render_test` under narrow deny | Exit 0; repeated resized GPU frames, preserved texture pixels, screenshot readback, and no SDL windows pass |
| Existing `build/video_test` under narrow deny | Exit 0; no stdout; inspected assertions cover all three stock intros, frame counts, audio presence, resampling/nonzero PCM, and rewind |

Exact scheduled sample counts are observations, not required future test constants. Earlier broad-deny disk/render/video experiments also exited 0; they did not establish MP3 compatibility, which was checked separately and failed.

The broad profile's failure is a useful counterexample: preventing an audio service connection can remove decoder work even when no hardware output is intended. The installed `ExtendedAudioFile.h` describes this API as combining AudioFile and AudioConverter. The installed `AudioFile.h:148` identifies 1718449215 as `kAudioFileUnsupportedDataFormatError` (`'fmt?'`). The observed failure establishes an incompatibility of this broad profile with this conversion; it does not isolate which of its several denied services causes that failure. [Repository decoder use](../../src/music.cpp), SDK `AudioToolbox.framework/Headers/ExtendedAudioFile.h:9–12,65–89` and `AudioFile.h:148`.

The pure `decode_probe.cpp` follows the music backend's file-open/client-format/read approach, hashes every produced float through EOF, and never opens an output device. It decodes `ref/raw/music/Main_menu/C.D.N. and Dramaties - Redline mastered - Activision.mp3`. Matching checksums are measured evidence for this complete file and conversion, not coverage of every codec, track, device, or future OS release. Intro coverage is provided separately by the existing `video_test` assertions and the repository's FFmpeg decode/resample path in `src/video.cpp`.

The narrow experimental profile is:

```scheme
(version 1)
(allow default)
(deny mach-lookup
  (global-name "com.apple.audio.coreaudiod")
  (global-name "com.apple.audio.audiohald"))
```

It is deliberately an allow-by-default **service deny list**. It does not deny every audio API, direct IOKit path, other/new service, inherited/sendable capability, external helper, or external LaunchServices process. The installed matching SDL CoreAudio source uses hardware-device enumeration and AudioQueue playback; the probe exercises this backend, not every possible client. A failed default-device open is insufficient evidence for complete output enforcement. [SDL CoreAudio implementation](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/audio/coreaudio/SDL_coreaudio.m)

## Activation policy and WindowServer dependency

The public AppKit enum documents `Prohibited` as disallowing windows and activation, and identifies it as the default for unbundled executables without an Info.plist. It also distinguishes `Accessory`, which can activate. However, `setActivationPolicy:` is a public, mutable API; the installed header says any policy can be set on macOS 10.9 and later and the setter returns whether it succeeded. This is useful cooperative process configuration, not an external rule that arbitrary child code cannot change. No attempt to change policy or activate a process was made. [Apple activation policies](https://developer.apple.com/documentation/appkit/nsapplication/activationpolicy-swift.enum), installed SDK `AppKit.framework/Headers/NSRunningApplication.h:30–43` and `NSApplication.h:299–306`.

Matching SDL source supports the existing hint choice: `Cocoa_RegisterApp` skips its promotion to `NSApplicationActivationPolicyRegular` when `SDL_HINT_MAC_BACKGROUND_APP` is true; a separate activation sequence is also skipped under the background hint. This does not independently inspect the eventual application policy or constrain future code. [SDL background registration/activation](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/video/cocoa/SDL_cocoaevents.m#L311-L339), [policy promotion](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/video/cocoa/SDL_cocoaevents.m#L511-L524)

A stronger proposed deny was tested without creating windows or attempting activation: `hardware-windowserver-deny.sb` additionally denies lookup of `com.apple.windowserver.active`. The existing offscreen render test then exits 1. A diagnostic build of the previously vetted offscreen prototype exits 3 during SDL video initialization with `The video driver did not add any displays`. Forcing the process-local `SDL_VIDEO_DRIVER=offscreen` instead fails GPU renderer creation with `No supported SDL_GPU backend found!` (exit 4), both with and without the deny. Therefore disconnecting this WindowServer service is not a drop-in replacement for the current Cocoa plus windowless Metal GPU path. These results do not prove that this one lookup denies every activation mechanism.

## Other public audio seams

The installed CoreAudio SDK exposes process PID, device list, and `kAudioProcessPropertyIsRunningOutput` properties. These can support a passive monitor for unexpected output streams by owned process identity. The documented output property describes audio IO with an active output stream; it does not report speaker pressure or prove absence between observations. No new CoreAudio monitor was implemented or run here. SDK `CoreAudio.framework/Headers/AudioHardware.h:1945–1979`.

`CATapDescription` supports explicit process selection and `CATapMuted`, documented as capturing audio while sending none from the selected process to hardware. `CATapMutedWhenTapped` allows hardware output until a client reads the tap, so its documented semantics do not satisfy an unconditional no-output requirement. A process tap would need separate validation of creation failure, initial output, newly created descendants, teardown, and permission behavior before claiming a whole-Run boundary. No tap, capture permission flow, or host audio experiment was attempted. SDK `CoreAudio.framework/Headers/CATapDescription.h:19–56,169–173`.

## Evidence and remaining work

`final-results.json` retains exact commands, exit statuses, and log paths for the final narrow-profile mix/render/intro/full-MP3 checks. Other `paused-*.log`, `mp3-deny.log`, and `offscreen-*-windowserver-deny.log` files retain the controls and rejected profiles. The research sources and profile text are retained alongside the binaries. These are standalone observations, not a complete application Run or performance/clock-parity check.

Before promoting the narrow deny beyond research, validate the complete isolated application with actual intro and menu captures, every production audio path, child lifecycle, and existing foreground observer; retain evidence that profile application failed closed when unavailable. Review OS support separately. Do not broaden the service deny in response to an output concern without rechecking all native decoding. A requirement for an immutable, comprehensive host boundary remains open and must not be hidden by a passing observer or this bounded sandbox experiment.

Remote official SDL excerpts and Apple documentation excerpts were screened before use. Jev automatically verified the deprecation/support limits, safe paused-probe behavior, final narrow-profile test exits, failed renderer alternatives, and mutable activation-policy interpretation. An initial combined-log verification gave a low-confidence contradiction for the MP3 comparison; after evidence was separated by filename, the exact complete-file comparison was automatically verified (supports 0.99, confidence 0.98), with no contradiction in the targeted check. Broad-profile failure attribution, the installed App Sandbox profile interpretation, and CoreAudio tap/property semantics remained review-level (supports 0.72–0.86, confidence 0.58–0.79); exact source/log locations and these judgments were escalated to the parent agent for independent inspection. The note retains the bounded observations and explicitly avoids attributing the broad-profile failure to one particular service.
