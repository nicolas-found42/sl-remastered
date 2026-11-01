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
