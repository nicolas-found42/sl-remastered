#!/usr/bin/env python3
"""Independent reader for the game's .class files (TUFA containers).

Written from the stock executable's own loader (JVM::addClass, FUN_00411b20, and the
constant pool reader FUN_00407660), not from ref/SLRR_rev. It exists so that the C++ loader
in src/tufa.cpp is compared with a second reader and not only with itself.

Layout as the stock executable reads it (all values little-endian u32 unless noted):

  file     = class*             a class is a run of chunks that starts at a TUFA chunk
  chunk    = tag[4] size payload[size]
  TUFA     = u32 version                              (size 4)
  CONS     = count, entry*                            entry = kind, body
             kind 0: len, bytes[len], NUL    kind 1, 2, 4: 1 word
             kind 3, 5, 7: 2 words           kind 6: the stock exe reports an error
  FILD     = list, list     list = count, record*     record = 4 words
  MTHD     = list, list     list = count, record*     record = 5 words
  CLSS     = 5 words, then ifaceCount (word 4) words
  TREE     = count, tree*   tree = nodeCount, node*   node = op u8, source u16, [operand u32]
             operand present when the stock exe's table at 0x5f0914 marks the op wide,
             which is true for these ops: 01-03 06-0b 0d-0f 11 12 14-17 19 1a 1b 1c 1e 1f 25-27 2e
             (checked against the table bytes in the executable, ops 00 to 2e)

    python3 -I tools/tufa_census.py <dir> [--per-file]
"""

import struct
import sys
from pathlib import Path

WIDE_OPS = frozenset(
    [0x01, 0x02, 0x03, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x11, 0x12,
     0x14, 0x15, 0x16, 0x17, 0x19, 0x1A, 0x1B, 0x1C, 0x1E, 0x1F, 0x25, 0x26, 0x27, 0x2E]
)
MAX_TABLE_OP = 0x2E
BODY_WORDS = {1: 1, 2: 1, 3: 2, 4: 1, 5: 2, 7: 2}


class Bad(Exception):
    pass


class Cur:
    def __init__(self, data: bytes):
        self.d = data
        self.p = 0

    def left(self) -> int:
        return len(self.d) - self.p

    def u32(self) -> int:
        if self.left() < 4:
            raise Bad("truncated u32")
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def u8(self) -> int:
        if self.left() < 1:
            raise Bad("truncated u8")
        v = self.d[self.p]
        self.p += 1
        return v

    def u16(self) -> int:
        if self.left() < 2:
            raise Bad("truncated u16")
        v = struct.unpack_from("<H", self.d, self.p)[0]
        self.p += 2
        return v

    def take(self, n: int) -> bytes:
        if n > self.left():
            raise Bad("truncated block")
        v = self.d[self.p:self.p + n]
        self.p += n
        return v


def tag_text(t: bytes) -> str:
    return t.decode("latin-1")


def parse_cons(payload: bytes):
    c = Cur(payload)
    count = c.u32()
    consts = []
    for i in range(count):
        kind = c.u32()
        if kind == 0:
            n = c.u32()
            raw = c.take(n)
            if c.u8() != 0:
                raise Bad(f"CONS string {i} missing NUL")
            consts.append((0, raw.decode("cp1252", errors="replace")))
        elif kind in BODY_WORDS:
            consts.append((kind, tuple(c.u32() for _ in range(BODY_WORDS[kind]))))
        else:
            raise Bad(f"CONS unknown kind {kind} at entry {i}")
    if c.left():
        raise Bad(f"CONS {c.left()} unconsumed bytes")
    return consts


def parse_lists(payload: bytes, words: int, what: str):
    c = Cur(payload)
    lists = []
    for _ in range(2):
        n = c.u32()
        if n * words * 4 > c.left():
            raise Bad(f"{what} list overruns the chunk")
        lists.append([tuple(c.u32() for _ in range(words)) for _ in range(n)])
    if c.left():
        raise Bad(f"{what} {c.left()} unconsumed bytes")
    return lists


def parse_clss(payload: bytes):
    c = Cur(payload)
    head = [c.u32() for _ in range(5)]
    ifaces = [c.u32() for _ in range(head[4])] if head[4] * 4 <= c.left() else None
    if ifaces is None:
        raise Bad("CLSS interface list overruns the chunk")
    if c.left():
        raise Bad(f"CLSS {c.left()} unconsumed bytes")
    return head, ifaces


def parse_tree(payload: bytes):
    c = Cur(payload)
    trees = c.u32()
    nodes = 0
    max_op = 0
    for _ in range(trees):
        n = c.u32()
        for _ in range(n):
            op = c.u8()
            c.u16()
            if op > MAX_TABLE_OP:
                raise Bad(f"TREE op 0x{op:02x} is past the executable's table")
            if op in WIDE_OPS:
                c.u32()
            nodes += 1
            max_op = max(max_op, op)
    if c.left():
        raise Bad(f"TREE {c.left()} unconsumed bytes")
    return trees, nodes, max_op


def const_string(consts, idx):
    if idx < len(consts):
        k, v = consts[idx]
        if k == 0:
            return v
        if k == 4 and v[0] < len(consts) and consts[v[0]][0] == 0:
            return consts[v[0]][1]
    return None


def parse_file(data: bytes):
    c = Cur(data)
    classes = []
    cur = None
    while c.left():
        if c.left() < 8:
            raise Bad("truncated chunk header")
        tag = tag_text(c.take(4))
        size = c.u32()
        payload = c.take(size)
        if tag == "TUFA":
            if size != 4:
                raise Bad("TUFA chunk size is not 4")
            cur = {"order": ["TUFA"], "version": struct.unpack("<I", payload)[0]}
            classes.append(cur)
            continue
        if cur is None:
            raise Bad(f"{tag} before the first TUFA chunk")
        cur["order"].append(tag)
        if tag == "CONS":
            cur["cons"] = parse_cons(payload)
        elif tag == "FILD":
            cur["fild"] = parse_lists(payload, 4, "FILD")
        elif tag == "MTHD":
            cur["mthd"] = parse_lists(payload, 5, "MTHD")
        elif tag == "CLSS":
            cur["clss"] = parse_clss(payload)
        elif tag == "TREE":
            cur["tree"] = parse_tree(payload)
        else:
            raise Bad(f"unknown chunk {tag}")
    return classes


def summarize(classes):
    out = []
    for k in classes:
        cons = k.get("cons")
        name = const_string(cons, k["clss"][0][2]) if cons and "clss" in k else None
        out.append({
            "name": name,
            "order": "".join(t[0] for t in k["order"]),
            "version": k["version"],
            "cons": len(cons) if cons else 0,
            "fild": sum(len(x) for x in k["fild"]) if "fild" in k else 0,
            "mthd": sum(len(x) for x in k["mthd"]) if "mthd" in k else 0,
            "iface": len(k["clss"][1]) if "clss" in k else 0,
            "trees": k["tree"][0] if "tree" in k else 0,
            "nodes": k["tree"][1] if "tree" in k else 0,
        })
    return out


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    root = Path(argv[0])
    per_file = "--per-file" in argv[1:]
    files = sorted(p for p in root.rglob("*") if p.is_file() and p.suffix.lower() == ".class")
    opened = 0
    failures = []
    totals = dict(classes=0, cons=0, fild=0, mthd=0, trees=0, nodes=0, iface=0)
    versions = {}
    orders = {}
    max_op = 0
    for p in files:
        rel = str(p.relative_to(root))
        try:
            classes = parse_file(p.read_bytes())
            sums = summarize(classes)
            for k in classes:
                if "tree" in k:
                    max_op = max(max_op, k["tree"][2])
        except Bad as e:
            failures.append((rel, str(e)))
            print(f"{rel} FAIL {e}")
            continue
        opened += 1
        for s in sums:
            totals["classes"] += 1
            for key in ("cons", "fild", "mthd", "trees", "nodes", "iface"):
                totals[key] += max(s[key], 0)
            versions[s["version"]] = versions.get(s["version"], 0) + 1
            orders[s["order"]] = orders.get(s["order"], 0) + 1
        if per_file:
            body = " | ".join(
                f"{s['name']} v=0x{s['version']:x} order={s['order']} cons={s['cons']} fild={s['fild']} "
                f"mthd={s['mthd']} iface={s['iface']} trees={s['trees']} nodes={s['nodes']}"
                for s in sums
            )
            print(f"{rel} OK {body}")
    print(f"files found: {len(files)}")
    print(f"files opened: {opened}")
    print(f"files failed: {len(failures)}")
    print(f"classes: {totals['classes']}")
    print(f"constants: {totals['cons']}")
    print(f"field records: {totals['fild']}")
    print(f"method records: {totals['mthd']}")
    print(f"interfaces: {totals['iface']}")
    print(f"trees: {totals['trees']}")
    print(f"tree nodes: {totals['nodes']}")
    print(f"highest tree op: 0x{max_op:02x}")
    print("versions: " + " ".join(f"0x{v:x}={n}" for v, n in sorted(versions.items())))
    print("chunk orders: " + " ".join(f"{o}={n}" for o, n in sorted(orders.items())))
    print("failed files:")
    if not failures:
        print("none")
    for rel, err in failures:
        print(f"{rel}: {err}")
    return 1 if failures or not files else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
