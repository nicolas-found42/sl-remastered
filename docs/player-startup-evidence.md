# Player device startup frontier

The stock executable was confirmed through Ghidra's `get_program_info` as
`StreetLegal_Redline.bak.exe`, MD5 `e686099e7233bb7d7c461b1b35c5064c`.
The headless decompilation logs named below also record this identity.

The installed controller script resource, id 50, contains a `native player`
directive. The stock loader handles that directive in `FUN_005461a0`.
`FUN_00477bc0` constructs
the player factory and assigns its vtable at `005f12f0` (the vtable and its
references were read in the confirmed Ghidra program). Its create method,
`FUN_004797d0`, constructs the player instance and links it to its world node.
It initializes input state, reads `player_display`, `player_damage_multiplier`,
`player_change_distance`, and `player_change_velocity` from config, links
`frontend.rpk` resource `0x23`, and sets event mask `0x10` through
`FUN_0048d3d0`. See `local/census/DecompileAt-004797d0.log`.
Factory setup `FUN_00477cb0` links `system.rpk` resource 9 twice (type 6)
and registers the Controller native handlers; see
`local/census/DecompileAt-00477cb0.log`. The resource calls use
`FUN_00546070`, a lookup and link operation; they do not by themselves load
the resource payload.

The player vtable's `getInfo` method `FUN_0047b220` returns 1 for kinds 8 and
`0x1c`, and 0 for kind 9. `FUN_00427210` uses those results to register hooks
in two global lists. See `local/census/DecompileAt-0047b220.log` and the
`local/census/DecompileAt-00427210.log`. The previous port
treated the controller as a script-only node and obtained three zero values
from its script `getInfo`; it therefore omitted both registrations and the
native initialization. The bounded player path now records its configuration,
input block, resource links, event mask and hooks. It requires the Config
receiver and resource world; other devices and a nonzero constructor resource
link remain unsupported. Hook consumers and frame-by-frame input are not yet
implemented.

`Input.initControllers` tree 168 calls `queueEvent` on the new Controller with
a null sender, event `EVENT_COMMAND` (`0x10`), and text `setcontrol`. The
handler at `0047da30` calls `FUN_00426800`, which calls `FUN_004265c0` directly;
the latter checks the event mask, calls a native handler if present, and walks
event subscriptions even when the target's own mask rejects the event. The
player's `FUN_0047a920` handles command `setcontrol` by resolving its linked
node and calling its script's `setcontrol()` when that node and script exist;
it returns 1. The stock decompiles are saved at
`local/census/DecompileAt-0047da30.log`, `DecompileAt-004265c0.log`, and
`DecompileAt-0047a920.log`. The script's zero-argument `setcontrol`, tree 6 of
the installed `java.io.Controller`, calls `ControlSet.load(controlFile)` and
then `setcontrol(ControlSet)`. The latter, tree 5, resets the native input
mappings and adds each control entry. Thus dropping this command would skip
observable file and input state.

`PlayerInputState` now represents the embedded input block's initial axis
defaults and mapping slots. `FUN_0054d580` initializes 76 axes and 152 slots;
`FUN_0054d650` inserts at the first slot whose axis is zero, records the
current physical sample and device type, and returns a one-based slot or zero
when full. It returns -1 for negative physical device or axis. The port takes
the sample and device type as inputs because the platform functions producing
them are not yet implemented. `FUN_0054d700` removes the first matching slot;
`FUN_0054d750` clears slot axis words and resets axis defaults, retaining other
slot words. See `local/census/DecompileAt-0054d580.log`,
`local/census/DecompileAt-0054d650.log`,
`local/census/DecompileAt-0054d700.log`, and
`local/census/DecompileAt-0054d750.log`. Virtual axes outside 0–75 are explicitly unsupported by the host;
stock indexes without checking. `GameWorld` also has storage for the three
getInfo hook lists, with append order and linked node lifetime. Fresh stock
decompiles `DecompileAt-00427210.log` and `DecompileAt-004274e0.log` show
list insertion and explicit link detachment. The node destructor path
`FUN_0053e720` -> `FUN_00544e20` -> `FUN_00545f70` clears links that still
point to the destroyed node without deleting the list records; each has a
fresh per-address log. Hook consumers and complete player lifecycle are not
yet connected. `FUN_0054d5e0` adds the
input block to a separate global list; its removal is `FUN_0054d600`.

The controller native handlers for adding and removing mappings, resetting the
input state, and setting axis smoothing now use that block. The handler logs
are `DecompileAt-00477740.log`, `DecompileAt-00477810.log`,
`DecompileAt-00477a00.log`, and `DecompileAt-004778b0.log`; the smoothing
field assignments are in `DecompileAt-0054d7b0.log`. Adding a mapping needs
physical sample and device-type readers; without them it stops explicitly.
The running script stops explicitly if those platform readers are unavailable.

To continue, provide physical input enumeration and sampling, implement the
hook consumers and ControlSet file and mapping operations, and check the other
player commands and event kinds against stock. `GameRef.queueEvent` supports
an unlinked receiver, a masked-out event on a script-only node with an empty
subscription list, and the player `setcontrol` command when a player and its
receiver exist and the list is known empty. The last path calls the script
synchronously and propagates failures; a synthetic player test exercises the
callback through the VM. `FUN_0053dd80` constructs the subscription list empty
(`DecompileAt-0053dd80.log`); no supported native currently adds
subscriptions. Other linked paths fail explicitly. No native registry row is
marked matched by this work.
