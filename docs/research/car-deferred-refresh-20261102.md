# Car updatevariables scheduling, 2026-11-02

Reference decision: the native Car `updatevariables` command requests/coalesces a refresh. It does not immediately invoke the script method during installation. The Car frame routine consumes a pending0x800000 bit; the refresh clears that bit before checking `suspend_update` and invoking the script. Older and newer agree. Runtime parity after the parent-owned fix remains unverified by this research-only slice.

## Actual causal question

The parent observed an ordinary native owned-car reload accepting more parts after subtree propagation, but still rejecting11 parts (first strut slot121, flags16) and showing prestige151 rather than175. The new install implementation directly called script updatevariables after each owner registry transfer. This report tests the reference timing that could alter slot availability while a car is only partly loaded; it does not claim the pending-bit fix has already resolved that actual Journey.

## Immutable source bindings

Older `ref/raw/StreetLegal_Redline.bak.exe`, SHA256 `7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`.
Newer `ref/version 2-3-1/StreetLegal_Redline.exe`, SHA256 `f44fb88a4335c657e20701cdb84afb8d1d85f7c3aceb93d30113e6e6e44fefc2`.

All artifacts are direct children of the unchanged registered8MiB root `local/verify/issue53-texture-rea-20261102`:

- `car-refresh-older-v1`: six exact REA read_bytes slices, automatic query/retain/close; holder90193 released exit0 under180seconds. Query interval23:45:26.188–23:47:02.748UTC; complete open/close timestamps in sequence.json.
- `car-refresh-older-review-v1`: all six postchecks REVIEW, no SKIP. Manual review consumed only fixed hexadecimal/string bytes as data; provider metadata has instruction-like boilerplate. No directives were followed.
- `car-refresh-older-decoder-v1`: all six slices equal exact immutable PE bytes; source SHA checked before/after; bounded LLVM supplements, result0.
- `car-refresh-newer-v1`: seven exact slices, automatic query/retain/close; holder73825 released exit0 under180seconds. Query interval23:47:52.425–23:48:57.384UTC; complete lifecycle in sequence.json.
- `car-refresh-newer-review-v1`: all seven postchecks REVIEW, no SKIP. Same manual data-only review; no threshold changes or rerolls.
- `car-refresh-newer-decoder-v1`: exact PE equality7/7, result0.

No C++ source, tests, executable, or probe links were changed. The root owns implementation and runtime validation. Skipped physical-detach raw/isolated-instruction results from separate attempts are not used in this scheduling conclusion.

## Command branch and frame consumption

Older command branch45af19..45af42 compares the updatevariables token at61078c. On match it ORs instance+0x70 with0x800000, returns1, and exits. No script call occurs in that matching branch. Frame4518c0..4518ed tests this flag then calls448430 at4518e9. Its following ordinary dt test is a historical/independent LLVM lead; the bounded REA frame slice ends at the refresh-call boundary.

Newer command branch4a5185..4a51b7 has the same sequence, with command token59bae4. Evidence id `ev_21d44f97ca5f99286c703c1e140859262b608a0a43be8218b53cda5e882f63fa`. OR [instance+70],0x800000 is at4a519a and returns1 at4a51a1.

Newer frame slice494930..49494f tests bit0x800000 at494938 and calls refresh4aebf0 at49494b when set. Evidence id `ev_dbe63e1f5c8d3ffb3cfc62953cd276714be40c826513a120e586603b795eb881`. Subsequent dt comparison494958 is an independent LLVM lead immediately after this bounded slice. Initial discovery mistakenly called the second refresh caller4b5968 the frame call; that is instead an explicit resource-refresh wrapper, and was corrected before selecting the qualified frame slice.

The single dirty bit naturally coalesces repeated native requests before a frame consumes it. It is not an arbitrary delay/queue inferred from the method name.

## Refresh entry and script guard

Older448430 clears the dirty bit at entry. It obtains the script state, reads suspend_update (literal6100a8), then invokes script updatevariables (literal6100b8) only through its guarded refresh path. Historical all.c56983..57045 includes the intervening wheel-state reset and script callback; the six bounded slices do not claim complete qualification of every intervening reset write.

Newer4aec0d..4aec1c clears instance+70 with0xff7fffff at4aec16. Evidence id `ev_19e1efb64530d5086f23a7335138e634ce36a83e63bae9fd5fc89aa394229c33`.

Newer4aeca2..4aecc2 obtains script-state+50 and reads suspend_update at59b49c via4ece80. Nonzero branches to4af521 rather than the script refresh. Evidence id `ev_098368e019eb7bdd8e7f03e12bb5302715ebfb626a8d95f07734c357ef434e29`. Field-name bytes are separately bound in refresh-field-names.json.

Newer4aed53..4aed64 invokes script updatevariables (59b4ac) through47dd50 at4aed60. Evidence id `ev_5b3772dd2eb6f531073da3771408f691ae6b76e99439a00946eb38e33b0a2b7f`. The local all.c/LLVM lead shows intervening wheel resets, but broad refresh bodies were deliberately not fetched.

## Scope limits

This specifically qualifies the Car native command and Car frame/refresh path. It does not establish that Part owners should receive the same pending flag, nor that every event-mask dispatch is queued. Older426800 is a small4265c0 wrapper; historical4265c0 can invoke native+0x38 synchronously and fall back to script event dispatch if unconsumed. The actual Car command consumes updatevariables by setting its dirty flag. Therefore a generic claim that all commands are asynchronous would be wrong.

The bit is cleared before the suspend_update check. A suspended script refresh is skipped after consumption rather than retaining the old bit indefinitely; later requests can set it again. Parent implementation should preserve that distinction and demonstrate the actual load no longer invokes premature script slot changes. No full Port or release acceptance is asserted here.
