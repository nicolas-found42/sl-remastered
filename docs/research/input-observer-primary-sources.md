# Input observer: primary-source facts that fix the design

Research date: 2026-11-01, via Firecrawl, primary sources only (Microsoft documentation, Microsoft
Detours wiki, Wine source). This supplements `docs/research/stock-input-consumption.md` (which proposes the
three-channel observer) and `docs/research/replay-design-primary-sources.md`. Facts only; nothing here is an
implemented or qualified observer.

## Facts and their sources

| Fact | Primary source |
|---|---|
| `IDirectInputDevice8::Acquire` returns `DI_OK`, or **`S_FALSE` if the device was already acquired**; failures include `DIERR_INVALIDPARAM`, `DIERR_NOTINITIALIZED`, `DIERR_OTHERAPPHASPRIO`. | [Microsoft Acquire](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417818(v=vs.85)) |
| The `GetDeviceState` page specifies **nothing** about the output buffer on failure. | [Microsoft GetDeviceState](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417897(v=vs.85)) |
| Wine's `dinput_device_Acquire` returns `DI_NOEFFECT` (== `S_FALSE`, 1) when already acquired. This is the repeated `Acquire -> 0x00000001` in the retained Stock DirectInput log. | Wine [`dlls/dinput/device.c` @ `wine-11.16`](https://gitlab.winehq.org/wine/wine/-/raw/wine-11.16/dlls/dinput/device.c) |
| Wine's `dinput_device_GetDeviceState` returns `DIERR_NOTACQUIRED` (not acquired), `DIERR_INPUTLOST` (unplugged) or `DIERR_INVALIDPARAM` (null data, missing format, wrong `size`) **without touching the caller's buffer**. Only the success branch does `memset(user_state, 0, size)` and rewrites it. | same file and tag |
| On success, for a device without `DIDF_ABSAXIS`, Wine **resets each relative axis in the device state to 0 after copying it**. `GetDeviceState` on a mouse is therefore a *destructive read* of accumulated relative motion. | same file and tag |
| Detours updates only threads enlisted with `DetourUpdateThread` whose instruction pointer lies in rewritten code; "threads not enlisted ... may attempt to execute an illegal combination of old and new code". | [Detours wiki: DetourUpdateThread](https://github.com/microsoft/detours/wiki/DetourUpdateThread) |

The Wine facts were read from the `wine-11.16` tag, matching the Hangover 11.16 runtime image, and from `master`;
both agree on these lines. The Wine facts describe that implementation, not Windows.

## Consequences for the observer and replay

1. **Never re-read the device.** Because a mouse `GetDeviceState` consumes relative motion, an observer that
   calls the real method to "peek" changes the game's input. Record the bytes the game's own call returned.
2. **Failure post-images are meaningful.** Wine leaves the buffer untouched on failure, and Stock's retry logic
   keeps stale bytes when a retry fails. The tape must retain the HRESULT as raw u32 bits and, on failure, the
   buffer as the game left it. Replacing failures with zeros is wrong. (The Microsoft contract is silent, so
   this is Wine-specific behavior; a replay must not assume it holds on another runtime.)
3. **`S_FALSE` versus `S_OK` is a real branch.** Stock's immediate path tests `== 0` while its buffered path
   accepts `>= 0`; record raw HRESULTs.
4. **Replay must substitute bytes, not drive devices.** A replay that injects synthetic device state still
   triggers the reset-on-read; the cleaner design replaces the response of the consumer call itself.
5. **Patching safety.** Detours' documentation shows why unsynchronized code patching is unsafe unless every
   other thread is accounted for. Our early bootstrap relies on no other thread being able to execute Stock's
   spans at attach (see `tools/parity/stock/control-format.md`, independent review item b). That remains an
   assumption; COM vtable-slot patches after device creation are single aligned pointer stores, but a stub
   in flight on another thread must still be handled by the guards.

## Still unknown

* Windows' real behavior for the same failure paths (Stock was authored for Windows; this runtime is Wine).
* Whether Wine's `GetDeviceData` consumes events identically; only `GetDeviceState` was read to the line.
* Exact window-message delivery order relative to DirectInput polls under Wine; needs an owned observer.
