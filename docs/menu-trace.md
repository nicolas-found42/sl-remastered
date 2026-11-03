# Stock menu script leads

These are serialized script references, not a proven execution trace. Dynamic lookup and branch conditions can change which methods execute. Ghidra identified the stock executable as MD5 `e686099e7233bb7d7c461b1b35c5064c`.

The executable's startup code registers `system.rpk`, creates the script resource at local ID `0x47`, and later uses the resulting configuration object's `init` field to create `GameInit`. On this installation those resources name `java.util.Config` and `java.game.Init`. Run `python3 -I tools/startup_trace.py` to check the installed data chain and selected `Init` constructor references. Preloaded classes may run initializer trees earlier, and the path from `GameInit` to the menu is not yet proven.

`python3 -I tools/menu_trace.py ref/raw/sl/Scripts/game/MainMenu.class --class java.game.MainMenu --method enter` shows tree 1 referencing `MainMenu.mmd` and `MainMenuDialog.show()V` at nodes 9 and 10. The executable's route into `MainMenu.enter` remains unread.

`MainMenuDialog.show()V`, tree 57, references `System.buildNumber()I` at node 155 and `GfxEngine.openVideo` at node 194. Its branches and argument values need tracing. The native `openVideo` is registered at handler `0047c330`; its behavior is unread. The same class's `addCustomGroups` tree 59 references `Osd.createBG`, `Osd.createHeader`, and `Menu.addItem`. A constant-pool inspection identifies node 30 as the name `createMenu`.

The current interpreter cannot run these methods. It lacks the object construction, field access, class-path calls, general value types, rendering, and other operations they require. No menu behavior or visual parity has been verified.

## Verified on the cold path (2026-11-01)

The normal cold path now runs with the stock native startup intros enabled, so
`MainMenuDialog.show` is reached the way the stock executable reaches it: after
`Activision.avi` (10.2 s) and `Invictus.avi` (14.9 s), with `StreetLegal.avi`
absent and skipped, then the static splash for 3.06 s. Holding Escape at
launch (script `0 keydown Escape`) collapses both intros to a single skipped
frame, exactly like stock's `GetAsyncKeyState(VK_ESCAPE)` loop.

Menu interactions exercised through the real UI, not the rapid scenario:

- Run 20261101T112616Z-79799 / 20261101T112918Z-83227: `wait menu` succeeds
  (MainMenuDialog `shown`, `actGroup == mainGroup`), the menu background video
  and its stock layout render, `clickn 0.59 0.41` on OPTIONS opens the stock
  OptionsDialog (`shown`, `actGroup == videoGroup`), Escape cancels it
  (`wait menu` succeeds again) and the Main Menu is shown again.
- The same script with intros left running (20261101T112944Z-84149,
  20261101T113537Z-92519) reaches the menu milestone at process 29.2-29.3 s.

Residual differences: the stock menu also renders a 3D car-body scene behind
the aspect-fitted video (visible in `docs/stock-flow.md` because Wine's
DirectShow never drew the video there); the port draws the video over black
letterbox. Hover/selection styling per entry and the CREDITS/EXIT/QUICKRACE
dialogs are not yet exercised.
