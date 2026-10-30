#!/usr/bin/env python3
"""Check the observed startup resource chain and script references, without executing it."""
import argparse
import importlib.util
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("menu_trace", ROOT / "tools/menu_trace.py")
menu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(menu)
tufa = menu.tufa


def script_resource(path, local_id):
    data = path.read_bytes()
    if len(data) < 32 or data[:4] != b"RPAK":
        raise ValueError(f"{path}: invalid RPK")
    version, a, b = struct.unpack_from("<III", data, 4)
    header = 16 + (a + b) * 64
    if version != 0x200 or header + 16 > len(data):
        raise ValueError(f"{path}: invalid RPK header")
    size, count, _, _ = struct.unpack_from("<IIII", data, header)
    start = header + 16
    end = start + size
    if end > len(data) or count > size // 23:
        raise ValueError(f"{path}: invalid RPK table")
    found = []
    offset = start
    for _ in range(count):
        if offset + 23 > end:
            raise ValueError(f"{path}: truncated RPK record")
        _, identity = struct.unpack_from("<II", data, offset)
        kind, flags = data[offset + 8:offset + 10]
        pos, length = struct.unpack_from("<II", data, offset + 14)
        next_offset = offset + 23 + data[offset + 22] + (48 if flags & 1 else 0)
        if next_offset > end:
            raise ValueError(f"{path}: invalid RPK record")
        if identity == local_id:
            if kind != 8 or pos < end or pos + length > len(data):
                raise ValueError(f"{path}: invalid script resource")
            found.append(data[pos:pos + length])
        offset = next_offset
    if offset != end or len(found) != 1:
        raise ValueError(f"{path}: expected exactly one resource {local_id:#x}")
    try:
        payload = found[0].decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError(f"{path}: invalid script payload") from error
    parts = payload.split()
    if len(parts) != 2 or parts[0] != "script":
        raise ValueError(f"{path}: invalid script directive")
    return parts[1]


def read_class(path, name):
    tufa.parse_tree = menu.parse_trees
    classes = tufa.parse_file(path.read_bytes())
    matches = []
    for cls in classes:
        if not all(key in cls for key in ("cons", "clss", "fild", "mthd", "tree")):
            raise ValueError(f"{path}: incomplete class")
        cls_name = tufa.const_string(cls["cons"], cls["clss"][0][2])
        if cls_name is None:
            raise ValueError(f"{path}: invalid class name")
        if cls_name == name:
            matches.append(cls)
    if len(matches) != 1:
        raise ValueError(f"{path}: expected exactly one {name}")
    return matches[0]


def startup(root):
    config_class = script_resource(root / "system.rpk", 0x47)
    if config_class != "java.util.Config":
        raise ValueError("unexpected configuration class")
    config = read_class(root / "system/Scripts/util/Config.class", config_class)
    constants = config["cons"]
    fields = [record for group in config["fild"] for record in group
              if tufa.const_string(constants, record[1]) == "init"]
    if len(fields) != 1 or not (fields[0][0] & 8) or tufa.const_string(constants, fields[0][2]) != "I":
        raise ValueError("missing or invalid Config.init field")
    tree_index = fields[0][3]
    if tree_index >= len(config["tree"]):
        raise ValueError("invalid Config.init initializer")
    nodes = config["tree"][tree_index]
    if len(nodes) != 3 or [(x[0], x[2]) for x in nodes[:1]] != [(0x07, 28)] or nodes[1][0] != 0x0e or nodes[2][0] != 0x10:
        raise ValueError("unsupported Config.init initializer")
    idx = nodes[1][2]
    if idx >= len(constants) or constants[idx][0] != 3:
        raise ValueError("invalid Config.init resource constant")
    path_idx, local_id = constants[idx][1]
    pack = tufa.const_string(constants, path_idx)
    if pack != "sl.rpk" or local_id != 0x40:
        raise ValueError("unexpected Config.init resource")
    init_class = script_resource(root / "sl.rpk", local_id)
    if init_class != "java.game.Init":
        raise ValueError("unexpected startup class")
    init = read_class(root / "sl/Scripts/game/Init.class", init_class)
    const = init["cons"]
    methods = [m for group in init["mthd"] for m in group
               if tufa.const_string(const, m[1]) == "<init>" and tufa.const_string(const, m[2]) == "(I)"]
    if len(methods) != 1 or methods[0][3] >= len(init["tree"]):
        raise ValueError("missing Init constructor")
    tree = init["tree"][methods[0][3]]
    expected = [(7, "java.sound.Sound.init ()V"), (13, "java.render.Frontend.init ()V"),
                (20, "java.render.LoadingScreen.show ()V"),
                (29, "java.io.Input.initControllers (Ljava.util.resource.GameRef;Ljava.lang.String;)V"),
                (35, "java.io.Input.enablePointingDevice ()V")]
    for index, ref in expected:
        if index >= len(tree) or tree[index][0] != 0x1a or menu.reference(const, tree[index][2]) != ref:
            raise ValueError(f"unexpected Init constructor reference at node {index}")
    if len(tree) <= 41 or tree[37][0] != 0x07 or tree[37][2] != 30 or tree[38][0] != 0x06 or tufa.const_string(const, tree[38][2]) != "Ljava.game.GameLogic;" or tree[40][0] != 0x21:
        raise ValueError("unexpected GameLogic construction")
    print("system.rpk 0x47 -> java.util.Config")
    print("Config.init -> sl.rpk 0x40 -> java.game.Init")
    print("Init.<init>(I) contains Sound.init, Frontend.init, LoadingScreen.show, Input.initControllers, Input.enablePointingDevice, and GameLogic construction")
    print("static references only; first script execution and menu transition are not established")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=ROOT / "ref/raw")
    args = parser.parse_args()
    try:
        startup(args.root)
    except (OSError, ValueError, tufa.Bad, IndexError, struct.error) as error:
        print(f"startup_trace: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
