#!/usr/bin/env python3
"""Exercise the stock Config constructor and the Init constructor with a disposable options copy."""
import pathlib
import re
import shutil
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix="sl-config-test-") as root:
    save = pathlib.Path(root) / "save" / "game"
    save.mkdir(parents=True)
    shutil.copyfile("ref/raw/save/game/options", save / "options")
    result = subprocess.run(["build/startup_probe", "--constructor", "--no-run-log", "--data-root", root],
                            text=True, capture_output=True, check=True)
    for expected in ("Config.video_gamma bits = 0x3f806166",
                     "measurement = 1000; sound = 0.72561, 0.3, 0.713415",
                     "Config constructor tree completed in bounded VM"):
        if expected not in result.stdout:
            raise SystemExit(f"missing {expected!r} in probe output:\n{result.stdout}\n{result.stderr}")

    # The constructor reaches the loading dialog. The queued loading thread parks on its receiver.
    init = subprocess.run(["build/startup_probe", "--init", "--no-input", "--no-run-log", "--data-root", root], text=True, capture_output=True)
    if init.returncode != 1:
        raise SystemExit(f"expected the Init constructor to stop at its frontier, got {init.returncode}:\n{init.stdout}\n{init.stderr}")
    for pattern in (r"world node 'nulltype' type 0 instance 0x00000000 flags 0x0",
                    r"Config constructor world ID 0xffff0080",
                    r"Config attached to system\.rpk 0x00000004; global link 0xffff0080",
                    r"Init constructor world ID 0xffff007f",
                    r"world node +'Config' type 1 instance 0xffff0080 flags 0x221",
                    r"world node +'GameInit' type 1 instance 0xffff007f flags 0x221",
                    r"world node +'dummy' type 1 instance 0xffff007e flags 0x231 \(own sector root\)",
                    r"world node +'_viewport' type 18 instance 0xffff[0-9a-f]+ flags 0x0",
                    r"world node +'Ljava\.render\.Osd;' type 1 instance 0xffff0079 flags 0x231 \(own sector root\)",
                    r"world: 10 nodes, 7 game instances created; resource world: 304 pack nodes, 0 missing ids",
                    r"Frontend\.defLoadingPic = resource 0x000100a8, node type 7",
                    r"Frontend\.largeFont = resource 0x00010025, node type 14",
                    r"Frontend\.mediumFont = resource 0x00010139, node type 14",
                    r"Frontend\.smallFont = resource 0x00010138, node type 14",
                    r"Frontend\.pointers = resource 0x0001003c, node type 14",
                    r"script thread: Ljava\.render\.LoadingScreen;<[0-9a-f]+>",
                    r"script thread: Hotkey watcher",
                    r"script thread slices: Ljava\.render\.LoadingScreen;<[0-9a-f]+> = [1-9][0-9]*",
                    r"script thread slices: Hotkey watcher = [1-9][0-9]*",
                    r"script thread waiting: Ljava\.render\.LoadingScreen;<[0-9a-f]+> on java\.render\.GfxEngine",
                    r"Running queued script threads after the constructor failure for diagnostics only"):
        if not re.search(pattern, init.stdout):
            raise SystemExit(f"missing {pattern!r} in --init output:\n{init.stdout}\n{init.stderr}")
    # Without --renderer the loading dialog cannot cache its texture, which is the honest stop for a probe with no renderer.
    for expected in ("startup probe: Init constructor: ResourceRef.cache: texture renderer unavailable",
                     "  in java.render.SimpleLoadingDialog (tree 1 pc 6 line 24)",
                     "  in java.game.Init (tree 0 pc 21 line 19)",
                     "startup probe: scheduler reached its iteration limit without a frontier"):
        if expected not in init.stderr:
            raise SystemExit(f"missing {expected!r} in --init stderr:\n{init.stdout}\n{init.stderr}")
    for stale in ("ResourceRef.set(I)V", "unsupported instance initializer",
                  "unsupported constructor", "Object.toString", "class not registered", "frontier:",
                  "native not registered: java.render.Viewport.create",
                  "java.render.osd.dialog.Dialog: node 130",
                  "unsupported logical not type",
                  "unsupported array type or length",
                  "GameType.createNativeInstance: unsupported script payload"):
        if stale in init.stderr:
            raise SystemExit(f"stale {stale!r} in --init stderr:\n{init.stdout}\n{init.stderr}")
    stock_root = pathlib.Path(".scratch/2005-extract/Program Files/Street Legal Racing - Redline")
    stock_options = stock_root / "save/game/options"
    if stock_options.is_file():
        shutil.copyfile(stock_options, save / "options")
        stock = subprocess.run(["build/startup_probe", "--init", "--no-input", "--no-run-log", "--data-root", root, str(stock_root)],
                               text=True, capture_output=True)
        if stock.returncode != 1 or "ResourceRef.cache: texture renderer unavailable" not in stock.stderr:
            raise SystemExit(f"stock startup did not reach the no-renderer texture frontier:\n{stock.stdout}\n{stock.stderr}")
        if "script thread waiting: Ljava.render.LoadingScreen;" not in stock.stdout:
            raise SystemExit(f"stock loading thread did not park:\n{stock.stdout}\n{stock.stderr}")
        # Stock installer options differ from the mod install's saved values.
        for expected in ("Config.video_gamma bits = 0x3f800000",
                         "measurement = 1000; sound = 1, 0.5, 1"):
            if expected not in stock.stdout:
                raise SystemExit(f"missing {expected!r} in stock-root probe output:\n{stock.stdout}\n{stock.stderr}")
print("Config and Init probes: no-renderer texture frontier, parked loading thread, and stock options values reported")
