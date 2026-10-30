"""Render stock Wine captures packaged by wine/capture/finish_run.py."""
import pathlib
import re


def read(path):
    try:
        return path.read_text(errors="replace")
    except OSError:
        return ""


def fields(path, separator):
    result = {}
    for line in read(path).splitlines():
        if separator in line:
            key, value = line.split(separator, 1)
            result.setdefault(key.strip(), []).append(value.strip())
    return result


def first(data, key, default="unknown"):
    return data.get(key, [default])[0]


def summary_line(run):
    result = fields(pathlib.Path(run) / "result.txt", "=")
    return first(result, "status"), first(result, "reason", "")


def render(run):
    run = pathlib.Path(run)
    result = fields(run / "result.txt", "=")
    meta = fields(run / "meta.txt", ":")
    counters = fields(run / "counters.txt", "=")
    status, reason = summary_line(run)
    lines = [f"STOCK WINE RUN: {status.upper()}", "", "CAUSE", "  " + (reason or ("No failure reported." if status == "ok" else "No reason recorded."))]
    for item in result.get("evidence", []):
        lines.append("  " + item)
    lines.extend(["", "TIMELINE (last 20 notable events; Wine lines have no time)"])
    events = [line for line in read(run / "run.log").splitlines()
              if re.match(r"^\[.*\] (error|script|input|frame|shot) ", line)]
    lines.extend("  " + line for line in events[-20:])
    if not events:
        lines.append("  (none recorded)")
    lines.extend(["", "COUNTERS"])
    for key in ("presents", "draws", "clears", "resets", "dumps", "dump_fail", "last_dump_nonblack_ratio", "err_lines", "wine_exceptions", "noise"):
        if key in counters:
            lines.append(f"  {key}: {first(counters, key)}")
    lines.extend(["", "WHAT RAN"])
    for key in ("source_run", "start_utc", "exe_md5", "exe_md5_check", "wine_path", "wine_path_note", "wine_version", "argv", "d3d_device", "data_comparison", "clock_sync", "clock_note"):
        if key in meta:
            lines.append(f"  {key}: {first(meta, key)}")
    lines.extend(["", "FILES"])
    for name in ("run.log", "meta.txt", "result.txt", "counters.txt", "input.txt", "script.txt", "shots.txt"):
        path = run / name
        lines.append(f"  {path}" + ("" if path.exists() else " (not present)"))
    return "\n".join(lines)
