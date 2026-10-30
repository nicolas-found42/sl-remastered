#!/usr/bin/env python3
"""Audit matched native claims against evidence and build/test inventories.

Usage: python3 -I tools/native_audit.py [--registry PATH] [--called PATH]
       [--evidence PATH] [--implementation-program PATH] [--test-program PATH ...]
       [--require-complete]

Each JSON inventory is an array of objects with class, method, descriptor and
handler fields. Evidence objects also contain stock_md5 and a nonempty source.
Programs are executed by this audit and must print their JSON inventory to stdout.
--test-program may be repeated; a matched native needs a VM-dispatch entry in the union.
A nonzero exit status rejects their output. Test entries must identify coverage as
"vm-dispatch". This label is not proof by itself: the build must connect a real
VM test. The audit checks traceability, not the quality of the test or evidence.
"""

import argparse
import csv
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
FIELDS = ("class", "method", "descriptor", "handler")
STOCK_MD5 = "e686099e7233bb7d7c461b1b35c5064c"


def identity(row, where):
    if not isinstance(row, dict) or any(not isinstance(row.get(k), str) or not row[k].strip() for k in FIELDS):
        raise ValueError(f"{where}: missing or empty identity field")
    if not re.fullmatch(r"[0-9a-fA-F]{8}", row["handler"]):
        raise ValueError(f"{where}: invalid handler address")
    return tuple(row[k] if k != "handler" else row[k].lower() for k in FIELDS)


def registry(path):
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if not set(FIELDS + ("status",)).issubset(reader.fieldnames or []):
            raise ValueError(f"{path}: missing registry columns")
        rows = list(reader)
    seen = set()
    for row in rows:
        key = identity(row, str(path))
        if key[:3] in seen:
            raise ValueError(f"{path}: duplicate native identity {key[:3]}")
        seen.add(key[:3])
        if row["status"] not in ("unread", "read", "matched"):
            raise ValueError(f"{path}: invalid status {row['status']!r}")
    return rows


def inventory(rows, where, evidence=False, vm_test=False):
    if not isinstance(rows, list):
        raise ValueError(f"{where}: inventory must be an array")
    keys = set()
    for row in rows:
        key = identity(row, where)
        if key in keys:
            raise ValueError(f"{where}: duplicate inventory identity {key}")
        if evidence and (row.get("stock_md5") != STOCK_MD5 or not isinstance(row.get("source"), str) or not row["source"].strip()):
            raise ValueError(f"{where}: evidence must name the stock MD5 and source")
        if vm_test and row.get("coverage") != "vm-dispatch":
            raise ValueError(f"{where}: test inventory must identify VM dispatch coverage")
        keys.add(key)
    return keys


def run_inventory(program, label, vm_test=False):
    result = subprocess.run([str(program)], capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        raise ValueError(f"{label} program failed with exit status {result.returncode}")
    try:
        rows = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ValueError(f"{label} program returned invalid JSON") from error
    return inventory(rows, label, vm_test=vm_test)


def executed_tests(programs):
    covered = set()
    for program in programs:
        covered |= run_inventory(program, "executed test", vm_test=True)
    return covered


def audit(args):
    rows = registry(args.registry)
    matched = {identity(row, str(args.registry)) for row in rows if row["status"] == "matched"}
    if matched:
        with args.evidence.open(encoding="utf-8") as stream:
            evidence = inventory(json.load(stream), str(args.evidence), evidence=True)
        implementation = run_inventory(args.implementation_program, "implementation")
        stock = {identity(row, str(args.registry)) for row in rows}
        print(f"runtime handler entries: {len(implementation)}; "
              f"stock signatures without a runtime handler: {len(stock - implementation)}")
        sources = (("evidence", evidence),
                   ("implementation", implementation),
                   ("executed test", executed_tests(args.test_program)))
        for label, available in sources:
            missing = matched - available
            if missing:
                raise ValueError(f"{label} inventory missing matched native(s): {sorted(missing)}")
    if args.require_complete:
        with args.called.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream, delimiter="\t")
            if not set(FIELDS + ("bucket",)).issubset(reader.fieldnames or []):
                raise ValueError(f"{args.called}: missing called columns")
            called = {identity(row, str(args.called)) for row in reader if row["bucket"] == "called"}
        if not called:
            raise ValueError(f"{args.called}: no called natives")
        missing = called - matched
        if missing:
            raise ValueError(f"{len(missing)} called native(s) not matched")
    print(f"native audit: {len(matched)} matched claims checked; behavioral quality is not assessed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=Path, default=ROOT / "docs/natives.tsv")
    parser.add_argument("--called", type=Path, default=ROOT / "docs/natives_called.tsv")
    parser.add_argument("--evidence", type=Path, default=ROOT / "docs/native_evidence.json")
    parser.add_argument("--implementation-program", type=Path, default=ROOT / "build/native_registry_inventory")
    parser.add_argument("--test-program", type=Path, action="append",
                        help="repeatable; default is build/native_vm_audit_test and build/native_vm_dispatch_test")
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args()
    if not args.test_program:
        args.test_program = [ROOT / "build/native_vm_audit_test", ROOT / "build/native_vm_dispatch_test"]
    try:
        audit(args)
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"native audit: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
