# Music natives: stock evidence and wiring

All readings are from the stock executable `StreetLegal_Redline.bak.exe`. The Ghidra MCP `get_program_info` reported MD5 `e686099e7233bb7d7c461b1b35c5064c`. No natives are marked matched by this work. The registry, VM and `docs/natives.tsv` were not touched.

## What the stock game plays

Music is files on disk, not CD audio. The player scans four folders under `Music\` in the game directory and plays `.mp3` and `.wav` files from them. The MCI calls in the executable belong to the disc check, not to music.

Evidence. The strings `Music\Garage_Shop` (0x00612c78), `Music\Roam_Ride` (0x00612c8c), `Music\Race_Chase` (0x00612c9c) and `Music\Main_Menu` (0x00612cb0) are each passed to one player constructor at start-up (references at 0x0047bcd0, 0x0047bd05 and so on). The play function `0x00559ed0` compares the file extension with `.wav` (0x0061b840) and `.mp3` (0x0061b848). `ref/raw/music` holds 15 mp3 files (Garage_Shop 4, Roam_ride 6, Race_chase 4, Main_menu 1) and no other music.

## Music set

A music set is one of four playlist objects. Set number to object: 0 `0x0063c878` Garage_Shop, 1 `0x00640908` Roam_Ride, 2 `0x006408a8` Race_Chase, 3 `0x00640890` Main_Menu. Any other number selects none. The current set pointer is `0x0064091c`.

Object layout (read from the constructor `0x00550be0` and its users):

| Offset | Meaning |
|---|---|
| +0x00 | volume, float, starts 1.0 |
| +0x04 | current track index, starts at `GetTickCount() % count` |
| +0x08 | track count |
| +0x0c | pointer to NUL-separated full paths |
| +0x10 | end-of-track mode, starts 3 |

The scan `0x00550c60` lists every file in the folder, skips `.` and `..`, and recurses into subdirectories in place. It does not filter by extension, so a file of another type occupies a slot and then fails to play.

## Handlers

| Native | Handler | Behavior |
|---|---|---|
| `Sound.changeMusicSet(I)V` | `0x00487460` | Read the int. Map 0..3 to a set, else none. If it differs from the current set: stop the current one (`0x00550f30`, keeps its index), make the new one current, and if it is not none start it (`0x00550e50`) and apply the music-channel global volume `0x00612c5c` (`0x00550fe0`). Same set is a no-op. No return value, no error. |
| `Sound.nextTrack()V` | `0x00487500` | If a set is current, jump to `0x00550f40`: stop, index+1, start. |
| `Sound.prevTrack()V` | `0x00487510` | Same with `0x00550f60`: index-1. |
| `Sound.setMode(I)I` | `0x00487520` | With a set: store the argument in +0x10 only if it is 0..3, and return the set's mode. With no set: return the argument. |
| `Sound.getMode()I` | `0x00487560` | Return +0x10 of the current set, or -1 with none. |
| `Sound.setVolume(IF)V` | `0x00487580` | Channels 0..2 store the float in `0x00612c58/5c/60`. Channel 1 also calls `0x00550fe0` when a set is current. See `docs/sound-volume-evidence.md`. |
| `Sound.getVolume(I)F` | `0x004875f0` | Returns those globals, or `_DAT_005e73cc` for other channels. |

`0x00487500`, `0x00487510`, `0x00487560` and `0x00487580` are not defined as functions in the Ghidra project, so the decompiler refused them. Their bytes were read with `read_memory` and decoded by hand. `nextTrack` and `prevTrack` are `mov ecx,[0x64091c]; test ecx,ecx; jz ret; jmp 0x00550f40` and `jmp 0x00550f60`. `getMode` is `eax=-1; if ecx then eax=[ecx+0x10]`.

Not read: `Sound.getVolume` for channel numbers over 2 beyond the constant name, `Sound.setMode` callers, `has3DHardware`, `hasMixHardware`, `enableDebugDump`, `ResourceRef.makeSound`, and the sound-effect path (`sound.rpk`).

### Helpers

- `0x00550e50` start: returns without touching anything if there is no list, count is below 1, or volume is below 0.01 (`0x005f0c20` is `0x3c23d70a`). Otherwise wraps the index (below 0 goes to count-1, at or above count goes to 0), walks to that path, and calls `0x00559ed0(path, loop = (mode == 0), context = mode != 0 ? player : 0)`, then sets the volume through `0x0055b160`.
- `0x00550fe0` set volume: stores the value. Below 0.01 it stores 0 and stops. If the old value was below 0.01 it starts the track. Then it applies the volume. There is no upper clamp.
- `0x0055b160`: volume times 64 (`0x005f2d2c` is 64.0), `__ftol` truncation, and if the result is 0..64 it is an index into the table at `0x0061b700` (65 signed 32-bit values from -10000 to 0, DirectSound hundredths of a decibel). Outside that range the attenuation is 0, which is full volume.
- `0x0055af00` stop: tears down the mmio, ACM or DirectShow playback. Safe when nothing plays.
- `0x00551050` end-of-track, called when a non-looping track finishes: mode 0 restart same, 1 next, 2 previous, 3 random index different from the current one (`GetTickCount() % count`, looping until it differs, only if count is above 1). Each stops first and then starts.
- `0x00559ed0` play: wav goes through mmio (mode 1), mp3 through an in-house decoder (mode 2), or DirectShow (mode 3) when flag `0x00784ca1` is set. Any other extension returns -1 and plays nothing. Failure is ignored by the caller.

## What the port implements

`src/music.hpp` and `src/music.cpp`. `MusicPlayer` holds the four playlists and mirrors the handlers above. The sound goes through a `MusicBackend`:

- `createAudioToolboxMusicBackend(AudioOutput&)` decodes mp3 and wav with macOS AudioToolbox and queues 48 kHz stereo float PCM into `src/audio.cpp`'s SDL3 stream. It applies the stock volume table as gain. It is real audio output; the native application plays it through the default CoreAudio device (see the real-device section at the end). ctest binaries still force SDL's dummy device.
- `NullMusicBackend` records calls and makes no sound. It is the labelled "no audio source available" backend and the one the state tests drive.

The original draft is kept as `src/music.draft.orig.txt`. It stored the volume without the mute rule, had no mode or random start, ignored the volume table, scanned only `.mp3` directly in each folder, and keyed on a lowercase `music` folder name.

## Verified and not verified

Verified by test (`build/music_test`, ctest name `music_unit`, built with ASan and UBSan): all state transitions above against the Null backend, including empty sets, muted sets, odd extensions, failing backend, case-insensitive folder lookup on the real `ref/raw/music` data (counts 4, 6, 4, 1), recursive scan on a temporary tree, the attenuation table at its ends, and real decoding of a generated wav and of the installed Main_menu mp3 through the dummy audio device, including end-of-track advance in NextTrack mode.

`native_music_unit` also executes the original nodes around `MainMenuDialog.show`'s `Sound.changeMusicSet` call through the VM, native handler, and `MusicPlayer` with a controlled silent backend. It checks selection of set 3, the requested track, and the music channel's volume. It runs the modified installed script in `ref/raw` and, when available, the separately extracted 2005 script in `.scratch/2005-extract`; these are different files. The test isolates the call and adds a return instruction, so it does not establish that the whole menu runs or that music was heard.

Not verified:

- File order. Stock uses the file system enumeration order of `FindFirstFile`. The port sorts names case-insensitively, which matches NTFS but is unproven for the original install media.
- The initial index depends on `GetTickCount`, so the stock first track is not reproducible. The port takes a tick source.
- Random end-of-track spins forever on a frozen clock in stock. The port gives up after 1000 tries and uses index+1.
- The stock decoders (the in-house mp3 decoder, mmio, DirectShow) and their sample-exact output. The port uses AudioToolbox and adds MP3 decoder priming differences. Volume applies to samples queued after the change, up to about two seconds late.
- When the end-of-track event fires. Stock fires from the audio timer callback near the end of the buffer. The port fires when decoding ends and the queue has drained.
- A synthetic CD metadata shim passed the stock disc check under Wine, but the game window remained black and no playable comparison was obtained (see `docs/wine-comparison.md`).
- Who calls `setMode` and when the menu script calls `changeMusicSet`. Only the first call in `MainMenuDialog.show` is known from the earlier trace.
- The `0x00784ca1` DirectShow flag (what sets it).

## How to wire it in

CMake: link `sl_music` (defined at the end of `CMakeLists.txt`, includes `src/music.cpp`, links `sl_data`, AudioToolbox and CoreFoundation) into the target that holds the handlers. `sl_data` already contains `src/audio.cpp`. Do not add `src/music.cpp` to `sl_data` unless you also link AudioToolbox and CoreFoundation there.

Construction, once, where the VM sound state lives (the data root is the directory that contains `music/`, for example `ref/raw` or the copied game dir):

```cpp
std::string error;
auto output = AudioOutput::create(error);                       // src/audio.hpp
auto backend = createAudioToolboxMusicBackend(*output);         // or NullMusicBackend
auto music = MusicPlayer::create(dataRoot, *backend, MusicPlayer::steadyClockTicks(), error);
```

`output`, `backend` and `music` must be destroyed in the reverse order (music, backend, output). A missing `Music` folder is not an error; it gives empty sets. `create` fails only if a directory cannot be read.

Handlers. VM ints are `int32_t`, floats are `float`.

| Native | Call |
|---|---|
| `Sound.changeMusicSet(I)V` | `music->changeMusicSet(args[0].i32, soundState->volumes[1])` returns nothing |
| `Sound.nextTrack()V` | `music->nextTrack()` |
| `Sound.prevTrack()V` | `music->prevTrack()` |
| `Sound.setMode(I)I` | return `music->setMode(args[0].i32)` as int |
| `Sound.getMode()I` | return `music->getMode()` as int |
| `Sound.setVolume(IF)V` | keep the existing global store for channels 0..2. For channel 1 also call `music->setMusicVolume(f)` when `music->selectedSet() >= 0`. Stock does this only when a set is current, and `setMusicVolume` is itself a no-op without one. This replaces the `musicPlayerActive` "unsupported" refusal in `src/natives/sound.cpp`. |
| `Sound.getVolume(I)F` | unchanged |

Stock `changeMusicSet` and its siblings do not validate argument count or types beyond reading one int through `0x0045d910`; keep the registry's usual argument checking. They never fail and never return an error value.

Main loop: call `music->update(error)` every frame, or at least every 50 ms. It feeds the decoder and handles end-of-track. On a false return `error` holds the reason (a decode or audio stream error). The player has already stopped that track.

Diagnostics: `selectedSet()`, `trackCount(set)`, `trackIndex(set)`, `setVolume(set)`, `currentTrack()`, `isPlaying()`, `lastPlayError()`.

Startup ordering: the stock static constructors build the four players before any script runs, with the first track picked from the clock. Construct `MusicPlayer` before the first `changeMusicSet`. In the stock Config constructor the music volume global is read from the options file; set `soundState->volumes[1]` before the first `changeMusicSet`, since the new set gets that value.

## Type-6 sound cache (bounded host adapter)

The stock `ResourceRef.cache` handler at `0047e8d0` calls `0048a920`; for a
non-material resource that applies a load value and requests loading through
`005447d0`. The type-6 loader `0053bff0` calls `0053bcc0`, which parses
`sourcefile`, `volume`, `mindist`, `maxdist`, `instances`, and `flags`, creates
a sound object via `00505960`, and updates the resource's loaded list. The
unload function `0053c060` destroys that object. These addresses were checked
against the identified stock executable and saved in `local/census`.

The host supports mounted type-6 WAV resources by decoding the sample into an
owned PCM buffer at cache time. Its buffer is reused on repeated cache calls and
released with the resource node. This is synchronous; stock can queue file
loading, so timing is not matched. Sound-effect playback is supported only within the bounds described below;
the remaining resource types are still unsupported. The real `sound.rpk` `0x1d`
entry loads `count3.wav`; tests also check malformed payloads, path escape,
repeated caching and buffer teardown.

## Non-spatial sound-effect playback

Saved identified-stock decompiles `00480d40`, `0048cfb0`, `005505e0`,
`00550980`, `0055b9a0`, `0055bb20`, `00480e60`, `0048d180`, and
`00550870` cover the request, voice allocation, backend parameters and stop.
The host plays the cached WAV on separate SDL streams, which the SDL device can
mix with music. It applies resource volume and sound channel 0 volume, the
stock quantized DirectSound attenuation table, and positive pitch scaling.
Named voices can be stopped by resource and sound ID. The host starts streams
immediately instead of stock's next sound update and SDL/device output has not
been compared with DirectSound. Spatial playback, flags, random pitch,
replacing an already playing named voice, and exact priority arbitration are
unsupported. Stream completion uses a conservative grace period before cleanup;
this is a host lifecycle approximation.

The startup host now constructs an `AudioOutput`, AudioToolbox backend,
`MusicPlayer`, and VM music control before running scripts, and pumps the
player during each supported host frame. Their destruction order keeps the
output alive until its backend is gone. `native_music_test` executes the real
`MainMenuDialog.show` music-selection slice against both available data roots,
decodes the selected soundtrack, and observes queued samples with SDL's dummy
audio device. This tests the script slice and decoder. Full startup now does
reach the stock main menu and Free Ride (runs cited in the real-device section),
and the application plays that soundtrack on the real CoreAudio device; the
ctest path deliberately stays silent.

## Real-device playback (2026-11-01, #47)

The native application itself reaches the stock main menu and plays the stock
soundtrack through the real CoreAudio playback device. Runs were launched from
worktree `../slrr-wt-47` with the real 2005 extract as the game root, an
absolute `--reference-root`, and the writable data root inside the worktree
(`local/stock-data`, `save/game/options` copied from the extract as `play.sh`
does). No `SDL_AUDIODRIVER` override was set; only the ctest binaries force
`dummy`.

Instrumentation added for this evidence (not a behavior change):
`AudioOutput::deviceName` (SDL device name) and two startup-probe log lines per
run: `music output: driver '<drv>' device '<name>'` at construction, and a
once-per-second runlog note
`music: set=<n> mode=<m> track=<i>/<count> playing=<0|1> queued=<samples> volume=<f>`.

Observed, seed 235:

- `local/runs/20261101T110120Z-20261` (`free-ride-drive.txt`, 69.7 s, 3445
  frames): `driver 'coreaudio' device 'MacBook Pro Speakers'`; set 3
  (Main_menu, 1 track) `playing=1` from loop 4.4 s through the end, queued
  samples oscillating 184320–190464, i.e. the device consumed decoded audio
  continuously. The scripted click opened the "Enter player name" dialog
  instead of Free Ride in this run, so no scene transition happened.
- `local/runs/20261101T110350Z-24971` (`free-ride-fast.txt`, 13.5 s, 108
  frames, `status=ok`): set 3 at 2.8 s, then on Free Ride entry set 1
  (Roam_Ride, 6 tracks) at 4.0 s with `playing=1` and random start index 5
  (stock `GetTickCount() % count`); queued dropped to 110592 at the
  transition (stock stop-then-start) and refilled. `wait driving` reached at
  8.4 s.
- `local/runs/20261101T110852Z-50169` (Options → Sound Options, 4.9 s):
  default `volume=0.500` loaded from the stock options file, menu music
  playing; a click on the MUSIC VOLUME slider track at x≈0.71 of the content
  rect set `volume=0.976` live while playback continued.
- `local/runs/20261101T110905Z-51057` (restart, 3.4 s): `volume=0.976`
  restored from the options save written at the previous run's stock Config
  shutdown; menu music playing.
- `local/runs/20261101T110711Z-40312` (two slider clicks, 6.4 s): the second
  click drove volume below stock's 0.01 threshold; the set stopped
  (`playing=0`, `queued=0`) exactly as the stock mute rule requires, and the
  next run (`20261101T110741Z-42443`) reloaded `volume=0.000` and refused to
  start the set without an error, matching `0x00550fe0`/`0x00550e50`.

RESOLVED (2026-11-01): the defect was the VM's failure to tolerate a null
receiver on a non-static qualified call, which stock `FUN_0041fbc0` skips
with a log note. The fix from the display-settings work (worktree 45,
uncommitted there) was ported into this tree (`src/vm.cpp`, plus the
null-receiver expectations in `tests/vm_test.cpp`,
`tests/vm_native_call_test.cpp`, `testNullReceiverSkipsBody` in
`tests/vm_eval_name_test.cpp`). With the fix, the previously failing
deterministic repros now pass:
- `local/runs/20261101T124855Z-53206` and `20261101T124958Z-53503` (music
  volume slider, stock state 0.000): status=ok; the stock note
  `vm: instance method called with no instance: java.io.Hotkey.inactivate()V`
  fires on the sound-page path and the run continues; slider 0.976 applies
  live while `playing=1`, slider near zero stops the set per the 0.01 rule.
- `local/runs/20261101T124937Z-53428` (effects slider, same state):
  status=ok.
While music was playing during the slider interaction, the new PCM meter in
`AudioOutput` logged non-silent signal at the exact point samples are handed
to the device (`music pcm: rms=0.109 peak=0.959 frames=135168` on
'MacBook Pro Speakers'), which is stronger than device-name plus queue
drain but still not a room-loudness recording.

Historical note: before the fix, in `20261101T110741Z-42443` (volume
already 0.000) a slider click failed with `thread host:handleEvent: null
qualified receiver: node 12 reference 13` at `java.render.osd.Slider` tree 11
pc 12, exiting the run with status=failed. The same interaction succeeded in
`20261101T110852Z-50169` (volume 0.500). The differentiator was the persisted
channel volume: with music volume stored as 0.000 the failure reproduced
deterministically (`20261101T112743Z-81130`) and it also reproduced with the
EFFECTS slider at that state (`20261101T112831Z-81781`).

Still not verified: that the speakers were audibly loud in the room (no
recording was made; the evidence is a named real playback device consuming
non-silent decoded stock audio), file enumeration order against original
install media, the stock decoders' sample-exact output, and end-of-track
timing during a run.
