# Configured display evidence

The Port applies the unchanged stock Config display fields at startup. The saved
fixture requests 800x600x32 fullscreen. The application uses that content size
inside the actual SDL window rather than the former fixed 640x480 diagnostic.

An exact host fullscreen mode uses SDL's exclusive mode. When macOS does not
expose that mode, the application uses desktop fullscreen and letterboxes the
requested content resolution. Run logs report requested, window, backing, and
content dimensions, plus fullscreen and exclusive flags. This adaptation does
not establish stock exclusive-mode Parity. Unsupported 16-bit output is rejected
and is not offered in the mode inventory.

`clickn X Y` and `moven X Y` use fractions of the game content rectangle in
`[0,1]`. Existing `click`, `move`, and raw SDL replay actions retain physical
window coordinates. SDL converts between content and window coordinates. A
letterbox bar ignores input and cancels a press dragged out of game content.
The bundled rapid and cold Free Ride routes use content-relative coordinates.

`wait options` and `wait video` observe the shown stock OptionsDialog ancestry,
its active group, and resource readiness. MainMenuDialog inherits OptionsDialog,
so the predicates inspect its real group fields. `wait menu` requires the main
group to be active. `tools/scripts/display-video-options.txt` follows the stock
Splash Screen, Main Menu, Options, and Video Options path using queued clicks.

## Stock authority

- Startup `0058c700` reads `video_x`, `video_y`, `video_depth`, and
  `video_windowed`. `004fc1b0` selects 640x480x32 windowed when either dimension
  is zero. Stock Config.class defaults to 800x600x32 fullscreen and loads the
  normal options file in its constructor.
- Stock Group.deactivate sets WORLDTREELEAF, bit `0x40`. Cursor query walk
  `0049e710`, reached through `00460140`, `0048d450`, and `0049e3d0`, skips those
  descendants. Both cursor picks and cached material draws now honor the flag.
- Evaluator `00420ff0`, literal case `0x100d`, copies the boolean operand.
  Stock Vector.removeElement uses this operation when a settings selector
  releases its hotkeys. The Port stores its boolean result as an integer.
- Steam OptionsDialog defers display changes until restart. Stock OptionsDialog
  source lines 697 through 704 instead calls changeVideoMode immediately.
  Steam's restart rule is not the authority for the Port.

## Executed evidence

Private evidence is under `local/issue-1/ticket-45-evidence` in the original
checkout. It includes immutable-source identities, scripts, logs, screenshots,
source-save manifests, and failures retained before each fix.

`red-fixed-window` records Config 800x600 with an actual 640x480 window and
renderer. `green-video-clean-layout` reaches Video Options through the real UI
with 800x600 content on a 1512x949 desktop fullscreen window. The source save is
unchanged. `foundation-final-letterbox-input` also ignores a physical bar click,
then reaches Video Options through queued content-relative input. Its capture
records 800x600 content, a 1512x949 desktop fullscreen window, and simulation
scale 1 at frame 52. Earlier Runs show inactive hidden groups intercepting clicks and old
menu text remaining visible. Their corrected path reaches Video Options with
clean group visibility.

Supplementary tests check actual letterboxed pixels, content dimensions under
non-unit scaling, real SDL window resizing, rejected requests preserving window
size, normalized scenario parsing, and boolean literals. The old output-size
behavior and the missing boolean-literal behavior each fail their regression
checks before the fixes.

## Remaining ticket 45 acceptance

This checkpoint proves configured startup presentation and usable settings
navigation. Live mode application, normal Config finalization and persistence,
process restart, cancelled selections, windowed refusal, and simulation timing
across those transitions remain pending. Gamma, shadow, texture-detail, flare,
headlight, and particle rendering controls are not verified by this checkpoint.
The two settings Natives remain unimplemented here rather than accepting no-op
changes. Full integration checks belong to the parent integration checkout.

## Blocker at the apply/cancel step (2026-11-01)

Reaching the settings apply path is blocked by a VM semantics divergence, not by
the display code. Run `local/runs/20261101T113035Z-85061` selects a resolution in
Video Options (`clickn 0.6 0.2`) and then presses Back (`clickn 0.94 0.925`). The
run aborts with `null or invalid qualified receiver` with the stack
`Group.handleEvent` (tree 10 pc 583 line 277) -> `Osd.deactivateText` (tree 72
pc 7 line 296) -> `MultiChoice.unfocus` (tree 2 pc 10 line 38) ->
`Vector.removeElement` (tree 24 pc 7 line 283) -> `Vector.indexOf(Object,I)`
(tree 12 pc 6 line 141) -> `Vector.indexOf` (tree 13 pc 13 line 148).

Stock VM evidence: `FUN_0041fbc0` (stock `Thread::callMethod`) rejects a null
receiver for a non-native method only by logging
`Thread::callMethod: instance method called with no instance`
(string at virtual address `0x0060c2e8` in the authority image, MD5
`e686099e7233bb7d7c461b1b35c5064c`) and returning `-1`
(`0041fbe8` tests method flag `0x8`, `0041fbf6` tests the receiver, and the
`0041fc03`/`0041fc0e` path logs then returns -1). The caller
`FUN_004207c0` keeps running (`local/census/DecompileAt-004207c0.log`). The
Port instead returns a fatal VM error from the qualified-receiver path
(`src/vm.cpp`, "null or invalid qualified receiver"), which is why the stock
script sequence aborts the run.

Stock script evidence: `MultiChoice.unfocus` sets `hkl`/`hkr` to null
(`MultiChoice.class` tree 2 node 20), so a second `unfocus` on the same gadget
passes a null search value to `Vector.removeElement`, which calls
`search.equals(...)` with a null receiver. Rejecting that call is the port's VM
policy; the stock VM evidently tolerates the null receiver, since stock ships
this exact script on the Back/cancel path. Back alone (`.../t45-back`) and
selection alone (`.../t45-sel`) both exit ok, so the failing sequence is the
post-selection redraw plus Back.

Consequence: applying/persisting/cancelling a display change through the stock
Options UI cannot be exercised until the VM matches stock for a null receiver
(and the subsequent `hkl.inactivate()` on null). Live mode application,
persistence, restart and timing across transitions remain unverified for the
same reason. Startup presentation below is verified with a real window capture
(run `local/runs/20261101T113010Z-84628`: `video-options.bmp`, window 1265x949,
render 800x600, fullscreen=1, exclusive=0).
