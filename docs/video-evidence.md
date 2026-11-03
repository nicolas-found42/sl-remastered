# Startup video path and menu video

## Native startup intros (stock FUN_0058c700)

The stock executable's startup `FUN_0058c700` (all.c:302104) runs its own
launch sequence before any script code: disc check, distribution version,
`video_x/y/depth/windowed` display init (`FUN_004fc1b0`), sound init, then a
loop over three paths built at all.c:302193:

    Data\FMV\Activision.avi
    Data\FMV\Invictus.avi
    Data\FMV\StreetLegal.avi

`FUN_0054c170` resolves each path (case-insensitive lookup, `/` normalised to
`\`) and reports whether the file exists; a missing file is skipped silently.
`FUN_0055c470` plays it through a DirectShow graph, rendering into the game
window, and its message loop calls `GetAsyncKeyState(0x1b)` (VK_ESCAPE) on
every iteration: a held Escape ends that video immediately. Playback ends on
the graph's end-of-stream event.

The installed 2005 data contains `Activision.avi` (Cinepak 29.97 fps, 304
frames, PCM audio) and `Invictus.avi` (Cinepak 25 fps, 373 frames, PCM audio);
`StreetLegal.avi` is absent, so stock skips it. Nothing in the installed
Scripts calls `openVideo` for these files: only
`sl/Scripts/game/MainMenu.class` invokes it (for Prime), so the startup intros
are native-side behavior, not a Script transition.

### Host implementation

`tools/startup_probe.cpp` performs the same sequence after the stock display
mode is applied and before the script world starts. It resolves each path
below the game root with the same case-insensitive rules as the menu player,
treats "file not found" as stock's silent skip, and plays non-looping video in
the real window with an aspect-fitted frame and the same
`GetAsyncKeyState`-style Escape check (real SDL key state and the probe's
scripted key state). `--no-intro` is a harness switch for the rapid iteration
route only; the cold path runs with intros enabled.

Audio streams present in `Activision.avi` and `Invictus.avi` are decoded and
played on the default output device like stock DirectShow: `VideoDecoder`
resamples the PCM stream (44.1 kHz stereo s16le) to 48 kHz stereo float with
libswresample and queues timestamped chunks; `VideoPlayer` queues them to its
`AudioOutput` in sync with the video clock and clears the queue on loop wrap
and close. `Prime.avi` carries no audio stream, so the menu video stays
silent. `AudioOutput::describeDevice()` records the opened device in the run
log.

Observed on the cold path with the real device (seed 235, no
`SDL_AUDIODRIVER` override, `tools/scripts/cold-startup-full.txt`, run
20261101T121557Z-39201):

    [+00.234] note video audio device: coreaudio device 'MacBook Pro Speakers'
    [+00.907] note video audio open src=44100Hz dst=48000Hz stereo
    [+00.907] note video open data\fmv\Activision.avi loop=0
    [+11.053] note video audio close
    [+11.058] note startup video data\fmv\Activision.avi finished
    [+11.059] note video audio open src=44100Hz dst=48000Hz stereo
    [+11.059] note video open data\fmv\Invictus.avi loop=0
    [+25.981] note video audio close
    [+25.989] note startup video data\fmv\Invictus.avi finished

Menu milestone at process 29.263 s, matching the video-only cold contract
(29.2-29.3 s). The held-Escape skip run (20261101T121633Z-39497) logs
`video audio open`/`video audio close` around each `skipped` intro and reaches
the menu at process 1.335 s; the rapid `--no-intro` route
(20261101T121655Z-40373) keeps zero startup-video notes and menu at 1.495 s.
An agent cannot attest to audible output; the demonstrated level is a non-dummy
device open plus decoded, queued, device-format samples (video_test asserts
~10.14 s / ~14.92 s of non-silent 48 kHz stereo per intro; video_player_test
asserts queueing and close-clear on the dummy device).

A full device queue is backpressure, not an error: `draw()` skips audio that
round instead of failing (`video_player_test` fills the queue to capacity and
keeps drawing). A naturally ended non-looping video queues its remaining audio
tail and leaves it draining on the device — stock's graph end-of-stream covers
the audio renderer — and the probe waits for that tail (bounded by the 2 s
queue capacity) before the next intro, like stock's blocking playback loop.
Skip (held Escape) still cuts audio immediately, as stock's key check does.
Cold run 20261101T122346Z-42526 (real device, seed 235): tails drain
11.071->11.567 s and 26.489->26.986 s between the intros, menu milestone at
process 30.257 s; skip run 20261101T122434Z-42780 closes audio at once and
reaches the menu at 1.342 s; rapid run keeps zero startup-video notes.

Observed on the cold path (seed 235, `tools/scripts/cold-startup-full.txt`):

    [+00.903] note video open data\fmv\Activision.avi loop=0
    [+11.062] note startup video data\fmv\Activision.avi finished
    [+11.063] note video open data\fmv\Invictus.avi loop=0
    [+25.996] note startup video data\fmv\Invictus.avi finished
    [+25.996] note startup video data\fmv\StreetLegal.avi absent; skipped

10.2 s and 14.9 s are the real data durations (304/29.97 and 373/25). Menu
milestone follows at process 29.3 s, splash auto-advance 3.06 s later. A run
whose script starts with `0 keydown Escape` models a player already holding
Escape at launch; both videos then log `skipped` in the first frame and the
menu milestone arrives at 0.8-4.2 s (runs 20261101T112616Z-79799,
20261101T112918Z-83227).

## Menu video (stock MainMenuDialog.show)

The separately extracted 2005 `MainMenuDialog.show` tree 20 calls
`GfxEngine.openVideo("data\\fmv\\prime.avi", 1, 1)` at node 64. The same
file is present in the installed data; its video is Cinepak, 160 × 120, 24
frames per second, 834 frames, with no audio stream. This identifies the
script call, not proof that full startup has reached it.

The identified stock executable's handler `0047c330` returns -1 for a null
filename and otherwise calls `004f8dd0`. The latter starts a DirectShow graph,
marks video active, and in nonzero texture mode constructs aspect-fitted video
geometry through `004f8b30`. End event handler `004f9670` either closes or
rewinds according to the third argument. `004f9750` reports the active flag;
`004f9320` releases the video. `004fca30` calls renderer slot +0x24, whose
identified target `004baa60` draws the video via `004f9550` before the camera
viewport pass. Per-address logs are saved in `local/census`.

The bounded host path supports texture mode 1, loop values 0 and 1, files with
or without audio streams, and monotonic video timestamps. It resolves
case-insensitive paths below the game root, decodes frames using FFmpeg, keeps
one SDL texture updated, and draws an aspect-fitted background before camera
viewports. Other modes and failures to decode remain explicit errors. Host
frame scheduling, color conversion and SDL presentation have not been compared
pixel-for-pixel with stock DirectShow and Direct3D output.

Run evidence (cold path, seed 235): the live script opens
`data\fmv\prime.avi loop=1` at the menu milestone, wraps at
`video loop 34.750s` (834 frames / 24 fps), and menu frames sampled 10 s apart
differ in 32-49 % of pixels (runs 20261101T110715Z-40524, 20261101T112944Z-84149).

`video_test` reads all 834 real Prime frames, decodes every frame of the two
audio-bearing startup intros and verifies rewind, and drains each intro's
audio to 48 kHz stereo float asserting chunk monotonicity, total duration
(within 0.5 s of the container durations) and a non-silent peak. The separate
`video_player_test` checks the first and five-second Prime frame pixels against
independent FFmpeg output, loops and closes the player, rejects a path escape,
queues intro audio to a dummy-device `AudioOutput` and verifies close clears
the queue, and executes the unchanged 2005 menu video-call slice through the
VM and registered native. The test falls back to the installed script when the
2005 extraction is unavailable.

## Stock comparison limits

The stock executable under Wine (WineHQ 11.0, `docs/stock-flow.md`, 2026-10-28)
never showed an intro video: Wine's DirectShow failed to build the AVI Splitter
(`err:quartz:autoplug`), so neither the three startup intros nor `Prime.avi`
rendered in the only stock runs that reached the menu. What those runs show is
black frames, the static splash art, then the Main Menu whose background in
Wine is the 3D car-body scene. That is a capture/codec limitation of the Wine
environment, not evidence of what stock does on Windows, and it is the
residual Gap for pixel-level startup-media parity.
