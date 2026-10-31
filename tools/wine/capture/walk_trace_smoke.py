#!/usr/bin/env python3
"""Compare hooked and unhooked stock walk executions under native macOS Wine.

Usage: python3 -I tools/wine/capture/walk_trace_smoke.py OUTPUT-DIR
OUTPUT-DIR must not exist and must be outside ref/. An existing idle Wine
prefix may be supplied via SLRR_ORACLE_PREFIX_SOURCE, as in run_original_oracle.sh.
"""
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
STOCK = ROOT / "ref/raw/StreetLegal_Redline.bak.exe"


def winpath(path):
    return "Z:" + str(path).replace("/", "\\")


def float32_from_x87(data):
    significand, exponent = struct.unpack("<QH", data)
    sign, exponent = exponent >> 15, exponent & 0x7fff
    if exponent == 0x7fff:
        value = math.inf if significand == 0x8000000000000000 else math.nan
    else:
        value = math.ldexp(float(significand), (exponent or 1) - 16383 - 63)
    if sign:
        value = -value
    return struct.unpack("<I", struct.pack("<f", value))[0]


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    out = Path(sys.argv[1]).resolve()
    if out == ROOT / "ref" or ROOT / "ref" in out.parents:
        raise SystemExit("output must be outside ref/")
    if os.environ.get("SLRR_ORACLE_BACKEND", "mac") != "mac":
        raise SystemExit("this smoke test uses native macOS Wine")
    out.mkdir(parents=True, exist_ok=False)
    exe = out / "stock.exe"
    subprocess.run(["i686-w64-mingw32-gcc", "-O0", "-DWALK_TRACE_SMOKE", "-Wl,--image-base,0x10000000",
                    str(ROOT / "tools/parity/cached-query/stock.c"), str(ROOT / "tools/wine/capture/walk_trace.c"),
                    "-ladvapi32", "-o", str(exe)], check=True)
    bad = out / "wrong-image.exe"
    image = bytearray(STOCK.read_bytes())
    image[-1] ^= 1
    bad.write_bytes(image)
    outputs = []
    for mode in ("baseline", "hooked"):
        env = os.environ.copy()
        env.pop("SLRR_TEST_HOOK", None)
        if mode == "hooked":
            env.update(SLRR_TEST_HOOK="1", SLRR_TEST_BAD_IMAGE=winpath(bad),
                       SLRR_TEST_TRACE_PATH=winpath(out / "walk-test.jsonl"))
        result = subprocess.run([str(ROOT / "tools/wine/run_original_oracle.sh"), str(out), str(exe)],
                                cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
        (out / (mode + ".stdout")).write_bytes(result.stdout)
        (out / (mode + ".stderr")).write_bytes(result.stderr)
        if result.returncode:
            raise SystemExit("%s failed with %d; see %s.stderr" % (mode, result.returncode, mode))
        outputs.append(result)
    if outputs[0].stdout != outputs[1].stdout:
        raise SystemExit("instrumented stdout differs from baseline")
    def moving(result):
        return [line.split() for line in result.stderr.splitlines() if line.startswith(b"moving ")]
    baseline, hooked = map(moving, outputs)
    if baseline != hooked:
        raise SystemExit("instrumented walk results differ from baseline")
    rows = [json.loads(line) for line in (out / "walk-test.jsonl").read_text().splitlines()]
    status = rows.pop()
    if status.get("event") != "status" or status.get("reserved") != 512 or status.get("pending") != 0 or status.get("dropped", 0) <= 0:
        raise SystemExit("unexpected bounded-buffer status: %r" % status)
    if len(rows) != 512 or any(row["seq"] != i + 1 or row["depth"] != 0 or row["valid_entry"] != 63 or row["valid_return"] != 31
                               for i, row in enumerate(rows)):
        raise SystemExit("incomplete or unpaired walk records")
    count = min(len(hooked), len(rows) // 2)
    if count != 256:
        raise SystemExit("expected 256 paired comparison cases")
    for i in range(count):
        fx = bytes.fromhex(rows[2 * i + 1]["fx_return"])
        if float32_from_x87(fx[32:42]) != int(hooked[i][3], 16):
            raise SystemExit("captured ST(0) differs from stock float result at case %d" % i)
    nested_exe = out / "nested.exe"
    subprocess.run(["i686-w64-mingw32-gcc", "-O0", "-Wl,--image-base,0x10000000",
                    str(ROOT / "tools/wine/capture/walk_trace_nested.c"), str(ROOT / "tools/wine/capture/walk_trace.c"),
                    "-ladvapi32", "-o", str(nested_exe)], check=True)
    result = subprocess.run([str(ROOT / "tools/wine/run_original_oracle.sh"), str(out), str(nested_exe),
                             winpath(out / "nested.jsonl")], cwd=ROOT, env=os.environ.copy(),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
    (out / "nested.stdout").write_bytes(result.stdout)
    (out / "nested.stderr").write_bytes(result.stderr)
    if result.returncode:
        raise SystemExit("nested detour failed with %d; see nested.stderr" % result.returncode)
    nested = [json.loads(line) for line in (out / "nested.jsonl").read_text().splitlines()]
    nested_status = nested.pop()
    if nested_status.get("pending") != 0 or nested_status.get("dropped") != 0 or len(nested) != 6:
        raise SystemExit("nested trace lost records: %r" % nested_status)
    by_thread = {}
    for row in nested:
        by_thread.setdefault(row["thread"], set()).add(row["depth"])
        fx = bytes.fromhex(row["fx_return"])
        expected = struct.unpack("<I", struct.pack("<f", 3.5 - row["depth"]))[0]
        if float32_from_x87(fx[32:42]) != expected:
            raise SystemExit("nested return value does not match recorded depth")
    if len(by_thread) != 2 or any(depths != {0, 1, 2} for depths in by_thread.values()):
        raise SystemExit("nested records did not pair by thread and depth")
    print("walk trace: %d records; %d x87 returns match; %d dropped; hash and prologue refusals passed" %
          (len(rows), count, status["dropped"]))


if __name__ == "__main__":
    main()
