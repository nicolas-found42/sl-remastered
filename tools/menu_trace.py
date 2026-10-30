#!/usr/bin/env python3
"""Print serialized method trees and their known name operands, without executing them."""

import argparse
import importlib.util
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location("tufa_census", Path(__file__).with_name("tufa_census.py"))
tufa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tufa)


def parse_trees(payload):
    cursor = tufa.Cur(payload)
    count = cursor.u32()
    if count > cursor.left() // 4:
        raise tufa.Bad("TREE count exceeds chunk")
    trees = []
    for _ in range(count):
        n = cursor.u32()
        if n > cursor.left() // 3:
            raise tufa.Bad("TREE node count exceeds chunk")
        nodes = []
        for _ in range(n):
            op = cursor.u8()
            source = cursor.u16()
            if op > tufa.MAX_TABLE_OP:
                raise tufa.Bad(f"TREE op 0x{op:02x} is past the executable's table")
            operand = cursor.u32() if op in tufa.WIDE_OPS else None
            nodes.append((op, source, operand))
        trees.append(nodes)
    if cursor.left():
        raise tufa.Bad(f"TREE {cursor.left()} unconsumed bytes")
    return trees


def reference(constants, index):
    if index >= len(constants) or constants[index][0] != 5:
        raise tufa.Bad(f"invalid reference constant {index}")
    owner_index, pair_index = constants[index][1]
    if pair_index >= len(constants) or constants[pair_index][0] != 7:
        raise tufa.Bad(f"invalid name-and-type constant {pair_index}")
    name_index, descriptor_index = constants[pair_index][1]
    owner = tufa.const_string(constants, owner_index)
    name = tufa.const_string(constants, name_index)
    descriptor = tufa.const_string(constants, descriptor_index)
    if owner is None or name is None or descriptor is None:
        raise tufa.Bad(f"invalid reference constant {index}")
    return f"{owner}.{name} {descriptor}"


def trace(path, class_filter, methods):
    # Reuse the independent container reader while retaining each node it parses.
    tufa.parse_tree = parse_trees
    classes = tufa.parse_file(path.read_bytes())
    if not classes:
        raise tufa.Bad("file has no classes")
    found = set()
    for cls in classes:
        if not all(key in cls for key in ("cons", "clss", "mthd", "tree")):
            raise tufa.Bad("class missing CONS, CLSS, MTHD or TREE")
        constants = cls["cons"]
        name = tufa.const_string(constants, cls["clss"][0][2])
        if name is None:
            raise tufa.Bad("invalid class name")
        if class_filter and name != class_filter:
            continue
        for records in cls["mthd"]:
            for flags, name_index, descriptor_index, tree_index, _ in records:
                method = tufa.const_string(constants, name_index)
                descriptor = tufa.const_string(constants, descriptor_index)
                if method is None or descriptor is None:
                    raise tufa.Bad(f"{name}: invalid method name or descriptor")
                if methods and method not in methods:
                    continue
                found.add(method)
                print(f"class {name} method {method}{descriptor} flags=0x{flags:x} tree={tree_index}")
                if flags & 0x40:
                    print("  native method; no script tree")
                    continue
                if tree_index >= len(cls["tree"]):
                    raise tufa.Bad(f"{name}.{method}: tree index {tree_index} out of range")
                for index, (op, source, operand) in enumerate(cls["tree"][tree_index]):
                    line = f"  {index:04d} op=0x{op:02x} source={source}"
                    if operand is not None:
                        line += f" operand={operand}"
                    # These operations resolve their operand as a name in the stock dispatcher.
                    if op in (0x1c, 0x25, 0x26):
                        value = tufa.const_string(constants, operand)
                        if value is None:
                            raise tufa.Bad(f"{name}.{method} node {index}: invalid name constant {operand}")
                        line += f" name={value!r}"
                    if op in (0x1a, 0x1b):
                        line += f" reference={reference(constants, operand)!r}"
                    print(line)
    missing = set(methods) - found
    if missing:
        raise tufa.Bad("method not found: " + ", ".join(sorted(missing)))
    if not found:
        raise tufa.Bad("no methods matched")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--class", dest="class_filter")
    parser.add_argument("--method", action="append", default=[])
    args = parser.parse_args()
    try:
        trace(args.path, args.class_filter, args.method)
    except (OSError, tufa.Bad) as error:
        print(f"menu_trace: {args.path}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
