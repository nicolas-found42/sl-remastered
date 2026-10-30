#!/usr/bin/env python3
"""Which registered natives do the stock scripts call?

Joins three things, all read from the stock install and the stock executable:

  1. docs/natives.tsv, the 360 natives the executable registers (tools/natives_extract.py).
  2. The method records of every class file under ref/raw. A method with flag 0x40 is native.
  3. The method references in every constant pool. A constant of kind 5 holds a class reference
     and a name-and-type reference, and a name-and-type (kind 7) holds a name and a descriptor.

A reference names the class it was compiled against, which can be a subclass of the class that
declares the method. So each reference is resolved up the superclass chain to the first class
that declares that name and descriptor, and it counts as a call of that declaration.

This is a static count of method references, not executed call sites. It cannot see a native that the
executable calls on its own, or one reached through reflection.

    python3 -I tools/natives_called.py [out.tsv]       default docs/natives_called.tsv

Buckets:
  called             registered, declared native in a script, referenced in a constant pool
  uncalled           registered and declared native, no such reference found
  registry-only      registered, but no script declares that exact class, name and descriptor
  script-only        declared native in a script, but not registered under that exact key
"""

import csv
import hashlib
import importlib.util
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NATIVE_FLAG = 0x40
STALE_PATH = Path("system/Scripts/render/osd/Dialog.class")
STALE_SHA256 = "0439e5de8b7b17ae7d3e8aba5cb413f3e78f8cbeb1e616c7889089284679a851"


def read_class(reader, root, path):
    data = path.read_bytes()
    try:
        return reader.parse_file(data)
    except reader.Bad as error:
        if (path.relative_to(root) == STALE_PATH
                and hashlib.sha256(data).hexdigest() == STALE_SHA256
                and str(error) == "CONS unknown kind 10 at entry 5"):
            return None
        raise reader.Bad(f"{path}: {error}") from error


def load_reader():
    spec = importlib.util.spec_from_file_location("tufa_census", ROOT / "tools" / "tufa_census.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main(out_path: Path) -> int:
    t = load_reader()
    registry = {}
    with open(ROOT / "docs" / "natives.tsv", newline="") as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            registry[(row["class"], row["method"], row["descriptor"])] = row

    supers = {}
    declared = defaultdict(dict)
    refs = Counter()
    ref_callers = defaultdict(set)
    classes_seen = 0
    raw = ROOT / "ref" / "raw"
    stale = 0
    for path in sorted(raw.rglob("*.class")):
        classes = read_class(t, raw, path)
        if classes is None:
            stale += 1
            continue
        for k in classes:
            classes_seen += 1
            cons = k["cons"]
            cls = t.const_string(cons, k["clss"][0][2])
            supers[cls] = t.const_string(cons, k["clss"][0][3])
            for part in k.get("mthd", [[], []]):
                for flags, name, desc, _tree, _locals in part:
                    declared[cls][(t.const_string(cons, name), t.const_string(cons, desc))] = flags
            for kind, body in cons:
                if kind != 5:
                    continue
                cls_ref, nat = body
                if nat >= len(cons) or cons[nat][0] != 7:
                    continue
                name_i, desc_i = cons[nat][1]
                key = (t.const_string(cons, cls_ref), t.const_string(cons, name_i), t.const_string(cons, desc_i))
                refs[key] += 1
                ref_callers[key].add(cls)

    def resolve(cls, name, desc):
        for _ in range(64):
            if cls is None:
                return None
            if (name, desc) in declared.get(cls, {}):
                return cls, declared[cls][(name, desc)]
            cls = supers.get(cls)
        return None

    if stale != 1:
        raise t.Bad(f"expected one known stale class, found {stale}")

    sites = Counter()
    callers = defaultdict(set)
    for (cls, name, desc), n in refs.items():
        hit = resolve(cls, name, desc)
        if hit and hit[1] & NATIVE_FLAG:
            sites[(hit[0], name, desc)] += n
            callers[(hit[0], name, desc)] |= ref_callers[(cls, name, desc)]

    script_native = {
        (cls, m, d) for cls, ms in declared.items() for (m, d), fl in ms.items() if fl & NATIVE_FLAG
    }
    rows = []
    for key, reg in registry.items():
        if key in sites:
            bucket = "called"
        elif key in script_native:
            bucket = "uncalled"
        else:
            bucket = "registry-only"
        rows.append((bucket, key, reg["handler"], sites.get(key, 0), len(callers.get(key, ()))))
    for key in sorted(script_native - set(registry)):
        rows.append(("script-only", key, "", sites.get(key, 0), len(callers.get(key, ()))))

    order = {"called": 0, "uncalled": 1, "registry-only": 2, "script-only": 3}
    rows.sort(key=lambda r: (order[r[0]], r[1]))
    with open(out_path, "w", newline="") as fh:
        w = csv.writer(fh, delimiter="\t", lineterminator="\n")
        w.writerow(["bucket", "class", "method", "descriptor", "handler", "call_sites", "calling_classes"])
        for bucket, (cls, m, d), handler, n, c in rows:
            w.writerow([bucket, cls, m, d, handler, n, c])

    counts = Counter(r[0] for r in rows)
    print(f"classes read: {classes_seen}")
    print(f"stock executable registry natives: {len(registry)}")
    for b in order:
        print(f"{b}: {counts.get(b, 0)}")
    print(f"wrote {out_path.relative_to(ROOT) if out_path.is_relative_to(ROOT) else out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs" / "natives_called.tsv"))
