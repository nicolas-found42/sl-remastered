#!/usr/bin/env python3
"""Print the methods and decoded trees of one stock .class file, with constants resolved.

    python3 -I tools/tufa_dump.py ref/raw/system/Scripts/render/Frontend.class [tree-number ...]

With no tree number it prints every method record (flags, name, descriptor, tree index) and the static and instance
fields. With tree numbers it prints each node as `index op source-line [operand]`, including branch targets.
`--all <dir>` reports decode failures for every .class file in a directory tree.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tufa_census as t  # noqa: E402


def trees_of(payload):
    c = t.Cur(payload)
    result = []
    for _ in range(c.u32()):
        nodes = []
        for _ in range(c.u32()):
            op = c.u8()
            if op > t.MAX_TABLE_OP:
                raise t.Bad(f"TREE op 0x{op:02x} is past the executable's table")
            line = c.u16()
            operand = c.u32() if op in t.WIDE_OPS else None
            nodes.append((op, line, operand))
        result.append(nodes)
    if c.left():
        raise t.Bad(f"TREE {c.left()} unconsumed bytes")
    return result


def describe(cons, index):
    if index is None or index >= len(cons):
        return ""
    kind, value = cons[index]
    if kind == 0:
        return repr(value)
    if kind == 4 and value[0] < len(cons) and cons[value[0]][0] == 0:
        return "class " + cons[value[0]][1]
    if kind == 5:
        return "fieldref(" + describe(cons, value[0]) + ", " + describe(cons, value[1]) + ")"
    if kind == 7:
        return "name/type(" + describe(cons, value[0]) + ", " + describe(cons, value[1]) + ")"
    return f"const[{index}] kind {kind}"


def load(path):
    data = path.read_bytes()
    t.parse_file(data)
    c = t.Cur(data)
    cls = None
    classes = []
    while c.left():
        tag = t.tag_text(c.take(4))
        size = c.u32()
        payload = c.take(size)
        if tag == "TUFA":
            cls = {"trees": []}
            classes.append(cls)
        elif tag == "CONS":
            cls["cons"] = t.parse_cons(payload)
        elif tag == "FILD":
            cls["fild"] = t.parse_lists(payload, 4, "FILD")
        elif tag == "MTHD":
            cls["mthd"] = t.parse_lists(payload, 5, "MTHD")
        elif tag == "TREE":
            cls["trees"] = trees_of(payload)
    return classes


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--all":
        root = Path(sys.argv[2])
        if not root.is_dir():
            raise SystemExit(f"not a directory: {root}")
        files = sorted(root.rglob("*.class"))
        failures = 0
        for path in files:
            try:
                load(path)
            except (t.Bad, OSError, ValueError, IndexError, TypeError) as error:
                print(f"{path}: {error}")
                failures += 1
        print(f"files={len(files)} failures={failures}")
        return int(failures != 0)
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    path = Path(sys.argv[1])
    want = sys.argv[2:]
    classes = load(path)
    # A file can hold several classes (a class and its helpers). Tree numbers are per class: `3` is class 0, `1:3` is
    # tree 3 of class 1. With no argument every class is listed.
    if not want:
        for number, cls in enumerate(classes):
            print(f"class {number}")
            show(cls)
        return
    for item in want:
        which, sep, tree = item.partition(":")
        if not sep:
            which, tree = "0", which
        cls = classes[int(which)]
        cons = cls["cons"]
        number = int(tree)
        print(f"class {which} tree {number}")
        for i, (op, line, operand) in enumerate(cls["trees"][number]):
            if op in (0x14, 0x15, 0x16) and operand is not None:
                relative = operand if operand < 0x80000000 else operand - 0x100000000
                text = f" {relative:+d} -> {i + relative}"
            else:
                text = "" if operand is None else f" {operand} (0x{operand:x}) {describe(cons, operand)}"
            print(f"  {i:3d} op 0x{op:02x} line {line}{text}")


def show(cls):
    cons = cls["cons"]
    if True:
        for which, lists in (("field", cls.get("fild", [])), ("method", cls.get("mthd", []))):
            for group, records in enumerate(lists):
                for rec in records:
                    name = describe(cons, rec[1])
                    kind = describe(cons, rec[2])
                    extra = ("native" if which == "method" and rec[0] & 0x40 else
                             f"tree {rec[3]}" if which == "method" else
                             f"init tree {rec[3] if rec[3] < 0xffffffff else '-'}")
                    print(f"  {which} group {group} flags 0x{rec[0]:x} {name} {kind} {extra}")
        return


if __name__ == "__main__":
    sys.exit(main())
