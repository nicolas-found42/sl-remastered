# Sound.setVolume evidence

The stock registry maps `java.sound.Sound.setVolume(IF)V` to `00487580`. The Ghidra MCP identified the stock executable by MD5 `e686099e7233bb7d7c461b1b35c5064c`. The handler reads the channel and float through `FUN_0045d910`. For channels 0, 1 and 2, it stores the float in globals `00612c58`, `00612c5c` and `00612c60` respectively, without clamping. Other channels do nothing. The installed Sound class defines these as effects, music and engine channels; their program-image initial values are 1.0, though full runtime initialization order remains unverified.

When a music player exists, channel 1 also calls `FUN_00550fe0`. It stores the player volume, stops playback below the binary32 threshold `0x3c23d70a`, and otherwise may resume playback and applies volume. The global keeps the original unclamped input. Actual playback and its platform state are not yet implemented.

The bounded host handler stores the three values when no music player is active and reports unsupported for a music-channel change with an active player. It does not claim to play sound or reproduce all exceptional behavior. The native remains unread until its complete side effects and stock comparison are verified.

## Real-device playback evidence (2026-11-01, ticket #46)

`SoundEffects::create` now logs the SDL backend it actually opened, and the
probe logs the live voice count at loop exit (`tools/startup_probe.cpp`). Runs
from this macOS session open `driver 'coreaudio' device 'MacBook Pro Speakers'`
(48 kHz stereo), i.e. a real output device, not the dummy driver that the ctest
suites force.

- `local/sfx46/menu2-vol050-20261101T113233Z` (options effects volume 0.5):
  `sound = 0.5, 0.5, 1`; both stock UI cues start at `volume 0.50`
  (`sound Started resource 0x0001007a/0x00010079`), counters `sound_started=2`,
  `sound effects at exit: active_voices=0`, status ok.
- `local/sfx46/menu2-vol000-20261101T113241Z` (effects volume 0): same path,
  `sound = 0, 0.5, 1`, no voice starts and no dropped counter — the effects
  channel gate suppresses the requests before the device, as stock's
  `005505e0` volume threshold does. Both runs share the seed/fixture identity.
- `local/sfx46/20261101T111840Z` (seed 235, `free-ride-drive.txt`): full
  gameplay run, exit 0, `sound Started ... volume 1.00`,
  `sound effects at exit: active_voices=0`.
- `tests/sfx_device_test.cpp` (`sfx_device_unit`, no `SDL_AUDIODRIVER`): on the
  real device it asserts the driver is not dummy, then exercises four
  overlapping mounted `sound.rpk` voices, the volume rounding-to-silence
  refusal, the per-resource instance arbitration (`min(instances, 8)` starts,
  excess `Dropped`, matching `00550980`'s sorted admission and state 1->0
  zeroing), targeted `stop`, loop release after one tick without a refresh
  request, a loop that survives while re-requested, and that every one-shot
  voice drains to zero voices. It skips itself when only the dummy backend
  exists.

Stock corroboration for arbitration: `local/census/all.c:251737` (`00550980`)
sorts the 64 request slots by `DAT_00768a7c` (the `distance^2/volume^2` score
computed in `005505e0`), admits while the device slot count from `00559dd0`,
the per-sound capacity from `0055b900` (at most 8 buffers) and `0055b920`
allow, zeroes the rejected request (`state 1 -> 0`) instead of stealing a
playing voice, stops a state-2 loop record that has no refresh, and releases
finished buffers. The host mirrors that admission/limit/refresh contract; the
device slot count is modelled as the 64-entry table because the DirectSound
hardware-buffer count is not observable on this path.
