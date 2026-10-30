#!/usr/bin/env python3
"""Try to build ref/slrr-reverse-main/engine on this Mac without touching ref/.

Copies the engine into local/slrr-reverse/, runs the project's own CMake build
there, then syntax-checks every .cpp on its own with clang so failures can be
sorted by cause. Writes local/slrr-reverse/units.tsv and cmake.log.

    python3 -I tools/probe_slrr_reverse.py
"""

import re
import shutil
import subprocess
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "ref" / "slrr-reverse-main" / "engine"
WORK = ROOT / "local" / "slrr-reverse"
ENGINE = WORK / "engine"
BUILD = WORK / "build"

ERROR = re.compile(r"error: (.*)")
MISSING = re.compile(r"'([^']+)' file not found")


def run(cmd: list[str], log: Path) -> int:
    with open(log, "w", encoding="utf-8") as f:
        return subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT).returncode


def cmake_build() -> None:
    log = WORK / "cmake.log"
    rc = run(["cmake", "-S", str(ENGINE), "-B", str(BUILD), "-G", "Ninja"], log)
    print(f"cmake configure: exit {rc}")
    if rc:
        return
    rc = run(["cmake", "--build", str(BUILD), "--", "-k", "0"], log)
    text = log.read_text(encoding="utf-8", errors="replace")
    failed = len(re.findall(r"^FAILED:", text, re.M))
    errors = len(re.findall(r"error:", text))
    print(f"cmake build: exit {rc}, {failed} units failed, {errors} errors, log {log.relative_to(ROOT)}")


LAYOUT = "static assertion failed"
MISSING_NAME = re.compile(
    r"(?:use of undeclared identifier|unknown type name) '([^']+)'"
    r"|no member named '([^']+)' in the global namespace")


def categorize(error: str) -> str:
    if error.startswith(LAYOUT):
        return "layout assertion"
    if MISSING_NAME.search(error):
        return "missing platform name"
    return "other"


def check_unit(cpp: Path) -> tuple[str, str, int, str, list[str]]:
    cmd = ["clang++", "-std=c++17", "-fsyntax-only", "-w", "-ferror-limit=0",
           "-I", str(ENGINE / "include"), str(cpp)]
    out = subprocess.run(cmd, capture_output=True, text=True).stderr
    errors = ERROR.findall(out)
    rel = cpp.relative_to(ENGINE).as_posix()
    if not errors:
        return rel, "ok", 0, "", []
    first = errors[0]
    missing = MISSING.search(out)
    return rel, "fail", len(errors), missing.group(1) if missing else first[:90], errors


def main() -> int:
    if WORK.exists():
        shutil.rmtree(WORK)
    shutil.copytree(SRC, ENGINE)
    cmake_build()
    cpps = sorted(ENGINE.rglob("*.cpp"))
    with ThreadPoolExecutor(8) as pool:
        rows = list(pool.map(check_unit, cpps))
    with open(WORK / "units.tsv", "w", encoding="utf-8") as f:
        f.write("unit\tstatus\terrors\tfirst_cause\n")
        for r in rows:
            f.write("\t".join(map(str, r[:4])) + "\n")
    ok = [r for r in rows if r[1] == "ok"]
    failed = [r for r in rows if r[1] == "fail"]
    print(f"syntax check: {len(ok)} of {len(rows)} units compile as-is on arm64 macOS")
    print("failing units:", ", ".join(f"{r[0]} ({r[2]})" for r in failed))
    kinds: Counter[str] = Counter()
    names: Counter[str] = Counter()
    for r in failed:
        for error in r[4]:
            kinds[categorize(error)] += 1
            m = MISSING_NAME.search(error)
            if m:
                names[m.group(1) or m.group(2)] += 1
    for kind, n in kinds.most_common():
        print(f"  {n:3d} errors, {kind}")
    print("missing platform names:", ", ".join(sorted(names)) or "none")
    print("units that compile:", ", ".join(r[0] for r in ok) or "none")
    return 0


if __name__ == "__main__":
    sys.exit(main())
