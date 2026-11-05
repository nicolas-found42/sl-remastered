#!/usr/bin/env python3
"""Explain a startup_probe run from the files it left in local/runs/<run>/ (see docs/run-log.md).

Usage: tools/diagnose.sh [--raw] [run-dir | latest | list]   |   tools/diagnose.sh --diff-code RUN_A RUN_B
Reads run records, including completed flight checkpoints. Every file is optional,
so an old or half-written run directory still gets an answer. A run whose meta.txt says kind: stock-wine is handed
to tools/diagnose_stock.py.
"""
import datetime
import ctypes
import hashlib
from collections import Counter
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

SIGNAL_WORDS = {
    "SIGSEGV": "the program touched memory it does not own (a crash)",
    "SIGBUS": "the program made a bad memory access (a crash)",
    "SIGILL": "the CPU hit an instruction it cannot run (a crash)",
    "SIGFPE": "the program did an impossible arithmetic operation (a crash)",
    "SIGABRT": "the program stopped itself on purpose after detecting a bug (abort, or an uncaught C++ exception)",
    "SIGTERM": "something asked the program to stop, for example the timeout command or kill",
    "SIGINT": "someone pressed Ctrl+C",
}
HANDLER_FRAMES = ("onSignal", "_sigtramp", "dumpAndRecord", "onTerminate", "backtrace", "run_log.cpp", "_pthread_kill",
                  "pthread_kill", "abort ", "__abort")
LINE = re.compile(r"^\[\+(\d+(?:\.\d+)?) f(\d+)\] (\S+) ?(.*)$")
# Mirrors kKindTable in src/run_log.hpp (tests/run_log_test.cpp checks the C++ side). Above ROUTINE_RANK is "key".
RANK = {"error": 10, "drop": 9, "input": 8, "sound": 7, "resource": 6, "note": 5, "heartbeat": 4, "frame": 4,
        "thread": 3, "scriptenter": 2, "cursor": 2, "nativecall": 1}
ROUTINE_RANK = 3
ROUTINE_NATIVES = ("java.io.Input.lastKey", "java.lang.System.currentTime")
KEY_EVENTS = 15
TIMELINE_EVENTS = 25
REPO = pathlib.Path(__file__).resolve().parent.parent


def read_text(path):
    try:
        return path.read_text(errors="replace")
    except OSError:
        return None


def read_kv(path, repeated=()):
    text = read_text(path)
    if text is None:
        return None
    values = {}
    for line in text.splitlines():
        key, sep, value = line.partition("=")
        if not sep:
            key, sep, value = line.partition(": ")
        if not sep:
            continue
        if key in repeated:
            values.setdefault(key, []).append(value)
        else:
            values[key] = value
    return values


def read_meta(path):
    """meta.txt is 'key: value'. Values (the command line) can contain '=', so split on ': ' only."""
    text = read_text(path)
    if text is None:
        return None
    values = {}
    for line in text.splitlines():
        key, sep, value = line.partition(": ")
        if sep:
            values[key] = value
    return values


def parse_log(text):
    """Returns (lines before the exit dump as (secs, frame, tag, text) or raw strings, dump lines)."""
    main, dump, in_dump = [], [], False
    for raw in (text or "").splitlines():
        if raw.startswith("--- last events ---"):
            in_dump = True
            continue
        match = LINE.match(raw)
        item = (float(match[1]), int(match[2]), match[3], match[4]) if match else raw
        (dump if in_dump else main).append(item)
    return main, dump


def show(item):
    if isinstance(item, str):
        return item
    secs, frame, tag, text = item
    return f"[+{secs:06.3f} f{frame}] {tag} {text}".rstrip()


def demangle(lines):
    tool = shutil.which("c++filt")
    if not tool or not lines:
        return lines
    try:
        out = subprocess.run([tool], input="\n".join(lines), capture_output=True, text=True, timeout=10).stdout
        result = out.splitlines()
        return result if len(result) == len(lines) else lines
    except (OSError, subprocess.SubprocessError):
        return lines


def crash_sections(text):
    header, events, frames, part = {}, [], [], "head"
    for raw in (text or "").splitlines():
        if raw.startswith("--- last events"):
            part = "events"
        elif raw.startswith("--- backtrace"):
            part = "trace"
        elif part == "head":
            key, _, value = raw.partition("=")
            header[key] = value
        elif part == "events":
            events.append(raw)
        elif raw.strip():
            frames.append(raw)
    return header, events, frames


def seconds_text(value):
    try:
        return f"{float(value):.1f} s"
    except (TypeError, ValueError):
        return "unknown time"


class ProcBsdInfo(ctypes.Structure):
    # macOS <sys/proc_info.h>, PROC_PIDTBSDINFO.  MAXCOMLEN is 16.
    _fields_ = [(name, ctypes.c_uint32) for name in (
        "flags", "status", "xstatus", "pid", "ppid", "uid", "gid", "ruid", "rgid", "svuid", "svgid", "reserved")]
    _fields_ += [("comm", ctypes.c_char * 16), ("name", ctypes.c_char * 32)]
    _fields_ += [(name, ctypes.c_uint32) for name in ("nfiles", "pgid", "pjobc", "tdev", "tpgid")]
    _fields_ += [("nice", ctypes.c_int32), ("start_sec", ctypes.c_uint64), ("start_usec", ctypes.c_uint64)]


def read_process_identity(pid):
    """Return (start seconds, microseconds, status), None if uninspectable, or 'absent'."""
    if sys.platform != "darwin":
        return None
    try:
        lib = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
        proc = lib.proc_pidinfo
        proc.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int]
        proc.restype = ctypes.c_int
        info = ProcBsdInfo()
        size = proc(pid, 3, 0, ctypes.byref(info), ctypes.sizeof(info))
        if size == ctypes.sizeof(info) and info.pid == pid and info.start_sec and info.start_usec < 1000000:
            return info.start_sec, info.start_usec, info.status
    except (OSError, AttributeError):
        return None
    # A failed inspection is not proof of death; only ESRCH from a separate
    # existence check establishes that no process has this PID at that instant.
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return 'absent'
    except (PermissionError, OSError):
        return None
    return None


def missing_process_status(meta, lookup=read_process_identity):
    """Classify the recorded process, not merely whichever process has its PID."""
    try:
        pid = int(meta.get('pid', ''))
        match = re.fullmatch(r'(\d+)\.(\d{6})', meta.get('process_start_unix', ''))
        if not match or not 0 < pid <= 2147483647:
            return 'unknown'
        expected = int(match[1]), int(match[2])
        if expected[0] <= 0:
            return 'unknown'
    except (TypeError, ValueError):
        return 'unknown'
    actual = lookup(pid)
    if actual == 'absent':
        return 'not running'
    if actual is None:
        return 'unknown'
    if actual[:2] != expected:
        return 'not running (PID reused)'
    if actual[2] == 5:  # SZOMB: process has exited, awaiting collection.
        return 'not running (exited, awaiting collection)'
    return 'running'


def verdict_line(result, crash_header, last_time, process="unknown"):
    if result is None:
        spent = f"; the log's last entry is at {last_time:.1f} s" if last_time is not None else ""
        state = ('recorded process running' if process == 'running' else
                 'recorded process not running; exit cause unknown' if process.startswith('not running') else
                 'killed or still running')
        return f"VERDICT: no result recorded ({state}){spent}"
    status = result.get("status", "unknown").upper()
    parts = [f"VERDICT: {status}", f"exit code {result.get('exit_code', '?')}"]
    if result.get("signal", "none") != "none":
        parts.append(f"signal {result['signal']}")
    parts.append(seconds_text(result.get("seconds")))
    parts.append(f"{result.get('frames', '?')} frames")
    return ", ".join(parts)


def parse_event(line):
    match = LINE.match(line)
    return (float(match[1]), int(match[2]), match[3], match[4]) if match else None


def flight_checkpoints(run):
    checkpoints = []
    for name in ("flight-0.log", "flight-1.log"):
        text = read_text(run / name)
        if text is None:
            continue
        lines = text.splitlines()
        header = re.fullmatch(r"# checkpoint=(\d+) ring_written=(\d+) retained=(\d+) slots=2 overwrites_older_checkpoints=1", lines[0] if lines else "")
        if not header:
            continue
        events = [event for line in lines[1:] if (event := parse_event(line)) is not None]
        if len(events) != int(header[3]):
            continue
        checkpoints.append((int(header[1]), name, int(header[2]), events))
    return sorted(checkpoints)


def merge_events(persisted, tail):
    """Merge logs at millisecond precision, deduplicating occurrences across sources only."""
    counts = Counter(persisted)
    merged = list(persisted)
    for event in tail:
        if counts[event]:
            counts[event] -= 1
        else:
            merged.append(event)
    return sorted(merged, key=lambda event: event[0])


def trace_lines(events):
    """Show note events and legacy opt-in stderr traces without duplicating a call."""
    lines = ["", "RANDOM SEED AND CAR REQUEST (recorded traces; not proof of a created car)"]
    for label, prefix in (("random seed", "random seed: "), ("car request", "car creation requested: ")):
        entries = [e for e in events if e[2] in ("note", "out", "err") and e[3].startswith(prefix)]
        notes = [e for e in entries if e[2] == "note"]
        unmatched = list(notes)
        selected = list(notes)
        for entry in (e for e in entries if e[2] != "note"):
            match = next((i for i, note in enumerate(unmatched)
                          if note[1] == entry[1] and abs(note[0] - entry[0]) <= 0.1 and
                          (note[3] == entry[3] or (len(note[3]) == 151 and entry[3].startswith(note[3])))), None)
            if match is None:
                selected.append(entry)
            else:
                note = unmatched.pop(match)
                if len(entry[3]) > len(note[3]):
                    selected[selected.index(note)] = entry
        if selected:
            lines.extend("  " + show(e) for e in sorted(selected, key=lambda e: e[0]))
        else:
            lines.append(f"  {label}: not recorded (tracing may have been disabled)")
    return lines


def timeline(events):
    routine = [e for e in events if not (e[2] == "nativecall" and e[3].split("(")[0].strip() in ROUTINE_NATIVES)]
    kept = routine or events
    collapsed = []
    for e in kept:
        if collapsed and collapsed[-1][0][2:] == e[2:] and collapsed[-1][0][3] == e[3]:
            collapsed[-1][1].append(e)
        else:
            collapsed.append((e, [e]))
    lines = []
    for first, group in collapsed[-TIMELINE_EVENTS:]:
        line = show(first)
        if len(group) > 1:
            line += f"  (x {len(group)}, from +{group[0][0]:.3f} to +{group[-1][0]:.3f})"
        lines.append(line)
    return lines or ["(none recorded)"]


def normalize_cause(text):
    """Strips the numbers and paths that differ between two runs of the same failure."""
    text = re.sub(r"0x[0-9a-fA-F]+", "0xN", text)
    text = re.sub(r"(/[\w.@+-]+)+", "<path>", text)
    return re.sub(r"\s+", " ", text).strip()


def find_crash_report(run, meta, result):
    """Returns (path or None, body dict or None, copied). Never touches the original report."""
    copy = run / "macos-crash.ips"
    if copy.is_file():
        return copy, load_ips(copy), False
    directory = pathlib.Path(os.environ.get("SLRR_CRASH_REPORT_DIR") or pathlib.Path.home() / "Library/Logs/DiagnosticReports")
    try:
        pid = int(meta.get("pid", ""))
        started = datetime.datetime.strptime(meta["start_utc"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)
    except (ValueError, KeyError):
        return None, None, False
    try:
        spent = float((result or {}).get("seconds", 0))
    except ValueError:
        spent = 0.0
    crashed_at = started + datetime.timedelta(seconds=spent)
    try:
        candidates = sorted(directory.glob("startup_probe-*.ips"), reverse=True)
    except OSError:
        return None, None, False
    for path in candidates:
        body = load_ips(path)
        if not body or body.get("pid") != pid:
            continue
        try:
            when = datetime.datetime.strptime(body["captureTime"], "%Y-%m-%d %H:%M:%S.%f %z")
        except (KeyError, ValueError):
            when = datetime.datetime.fromtimestamp(path.stat().st_mtime, datetime.timezone.utc)
        if abs((when - crashed_at).total_seconds()) > 600:
            continue
        try:
            shutil.copyfile(path, copy)
        except OSError:
            return path, body, False
        return path, body, True
    return None, None, False


def load_ips(path):
    """An .ips file is one JSON header line, then one JSON report. Returns the report dict or None."""
    text = read_text(path)
    if not text:
        return None
    _, _, body = text.partition("\n")
    for candidate in (body, text):
        try:
            value = json.loads(candidate)
        except ValueError:
            continue
        if isinstance(value, dict) and "pid" in value:
            return value
    return None


def crash_report_lines(run, meta, result):
    path, body, copied = find_crash_report(run, meta, result)
    if body is None:
        return ["  not found yet. macOS writes its crash report a few seconds after a crash, so run this again in a moment.",
                "  (looked in ~/Library/Logs/DiagnosticReports for startup_probe-*.ips with this run's process id)"]
    lines = []
    exc = body.get("exception") or {}
    term = body.get("termination") or {}
    lines.append(f"  exception: {exc.get('type', '?')} signal {exc.get('signal', '?')} codes {exc.get('codes', '?')}")
    if term.get("indicator"):
        lines.append(f"  termination: {term['indicator']}")
    threads = body.get("threads") or []
    index = body.get("faultingThread")
    images = body.get("usedImages") or []
    if isinstance(index, int) and 0 <= index < len(threads):
        frames = threads[index].get("frames") or []
        names = [f.get("symbol") or "?" for f in frames]
        # the probe's own handler re-raises the signal, so the report starts inside it; the real site follows _sigtramp
        for i, name in enumerate(names):
            if name == "_sigtramp":
                frames = frames[i + 1:]
                break
        lines.append("  faulting thread, top frames (best effort):")
        for f in frames[:8]:
            image = images[f["imageIndex"]].get("name", "?") if isinstance(f.get("imageIndex"), int) and f["imageIndex"] < len(images) else "?"
            symbol = f.get("symbol") or f"{image}+{f.get('imageOffset', '?')}"
            lines.append(f"    {symbol}  ({image})")
    lines.append(f"  report: {path}" + (f"  (copied to {run / 'macos-crash.ips'})" if copied else ""))
    return lines


def native_summary(run):
    result = read_kv(run / "result.txt") or {}
    status = result.get("status", "no result")
    cause = result.get("reason", "")
    if not cause and status in ("crashed", "terminated"):
        crash = crash_sections(read_text(run / "crash.txt"))[0]
        cause = crash.get("note") or crash.get("signal", "")
    if not result:
        cause = "killed or still running"
    return status, cause


def diagnose(run, raw_mode=False):
    result = read_kv(run / "result.txt", repeated=("stack",))
    meta = read_meta(run / "meta.txt") or {}
    crash_text = read_text(run / "crash.txt")
    log_text = read_text(run / "run.log")
    main, dump = parse_log(log_text)
    structured = [i for i in main if not isinstance(i, str)]
    last_time = structured[-1][0] if structured else None
    crash_header, crash_events, crash_frames = crash_sections(crash_text)
    out = []

    process = missing_process_status(meta) if result is None else "unknown"
    out.append(verdict_line(result, crash_header, last_time, process))
    try:
        health = json.loads(read_text(run / 'telemetry.json') or '{}')
        for key in ('dropped_lines', 'dropped_context_lines', 'io_failures', 'capture_unavailable'):
            value = health.get(key)
            if type(value) is int and value > 0:
                out.append(f'  EVIDENCE INCOMPLETE: recorder {key}={value}; inspect telemetry.json')
    except (ValueError, AttributeError):
        out.append('  EVIDENCE UNKNOWN: malformed recorder telemetry.json')
    counters_early = read_kv(run / "counters.txt") or {}
    if counters_early.get("sound_dropped", "0") not in ("0", ""):
        out.append(f"  sound dropped: {counters_early['sound_dropped']}  (the 64-voice limit discarded sound requests)")
    out.append("")
    out.append("CAUSE")
    status = (result or {}).get("status")
    if result is None:
        if process == 'running':
            out.append("  no result.txt: the recorded process is currently running (PID and start time match).")
        elif process.startswith('not running'):
            out.append(f"  no result.txt: the recorded process is {process}; its exit cause is unknown.")
        else:
            out.append("  no result.txt: the process was killed (kill -9) or is still running; identity could not be verified.")
        beats = [i for i in structured if i[2] == "heartbeat"]
        if beats:
            out.append(f"  last heartbeat: {show(beats[-1])}")
        else:
            out.append("  no heartbeat was logged (it runs only after 5 seconds in the frame loop).")
        out.append("  last run.log lines:")
        tail = main[-8:] or ["(run.log is empty or missing)"]
        out.extend("    " + show(i) for i in tail)
    elif status in ("crashed", "terminated"):
        name = crash_header.get("signal") or result.get("signal", "unknown")
        meaning = SIGNAL_WORDS.get(name, "see the signal name")
        verb = "Stopped by" if status == "terminated" else "Crashed with"
        out.append(f"  {verb} {name}: {meaning}.")
        if crash_header.get("note"):
            out.append(f"  detail: {crash_header['note']}")
        elif crash_header.get("fault_address") and name in ("SIGSEGV", "SIGBUS"):
            out.append(f"  fault address: {crash_header['fault_address']}")
        if crash_header.get("frame"):
            out.append(f"  it was at frame {crash_header['frame']}, {seconds_text(crash_header.get('seconds'))} after the process started")
        frames = [f for f in demangle(crash_frames) if not any(word in f for word in HANDLER_FRAMES)]
        if status == "crashed" and frames:
            out.append("  top of the call stack (best effort, may be incomplete):")
            out.extend("    " + " ".join(f.split()) for f in frames[:8])
        elif status == "crashed":
            out.append("  no call stack was captured.")
    else:
        reason = result.get("reason", "")
        if reason:
            out.append("  " + reason)
            stack = result.get("stack", [])
            out.extend("  " + s.rstrip() if s.startswith("  ") else "  " + s for s in stack[:12])
            if len(stack) > 12:
                out.append(f"    ... {len(stack) - 12} more in result.txt")
        elif status == "ok":
            out.append("  none: the run finished normally.")
        else:
            out.append("  the probe failed without printing a 'startup probe:' line; read the end of run.log.")

    events = [event for event in structured if event[2] in RANK]
    sources = ["run.log"]
    checkpoints = flight_checkpoints(run)
    for sequence, name, written, checkpoint_events in checkpoints:
        events = merge_events(events, checkpoint_events)
        sources.append(f"{name} checkpoint {sequence} (ring wrote {written})")
    if dump:
        events = merge_events(events, [e for e in dump if not isinstance(e, str)])
        sources.append("end-of-run dump")
    if crash_events:
        events = merge_events(events, [e for line in crash_events if (e := parse_event(line)) is not None])
        sources.append("crash dump")
    source = " and ".join(sources)
    if len(sources) > 1:
        source += "; equal timestamps and thread order are approximate"
    if checkpoints:
        out.extend(["", "FLIGHT CHECKPOINTS",
                    "  Two alternating snapshots retain only recent sampled events; older checkpoints are overwritten.",
                    "  A stalled frame loop cannot write a new checkpoint; incomplete temporary files are ignored."])
    if raw_mode:
        out.append("")
        out.append(f"LAST EVENTS (raw, last {TIMELINE_EVENTS}, from {source})")
        out.extend("  " + show(e) for e in (events[-TIMELINE_EVENTS:] or []))
        if not events:
            out.append("  (none recorded)")
    else:
        key = [e for e in events if RANK.get(e[2], 0) > ROUTINE_RANK][-KEY_EVENTS:]
        out.append("")
        out.append(f"KEY EVENTS (newest last, up to {KEY_EVENTS} non-routine events, from {source})")
        out.extend("  " + show(e) for e in key)
        if not key:
            out.append("  (none: only routine events were recorded)")
        out.append("")
        out.append(f"TIMELINE (last {TIMELINE_EVENTS} events, repeats collapsed, routine native calls hidden, from {source})")
        out.extend("  " + line for line in timeline(events))

    world_states = [event for event in events if event[2] == "note" and event[3].startswith("world state:")]
    if world_states:
        out.extend(["", "WORLD STATE", "  " + show(world_states[-1])])

    camera_follow = []
    camera_follow_bad = 0
    for event in events:
        if event[2] != "note" or not event[3].startswith("camfollow1 "):
            continue
        try:
            record = json.loads(event[3][len("camfollow1 "):])
            if not isinstance(record, dict) or type(record.get("v")) is not int or record["v"] != 1:
                raise ValueError("unsupported camera follow version")
            ids = ("p", "c", "car", "body", "look", "move")
            if any(type(record.get(key)) is not int or not 0 <= record[key] <= 0xffffffff
                   for key in ids):
                raise ValueError("invalid camera follow ID")
            if type(record.get("r")) is not int or record["r"] not in (0, 1):
                raise ValueError("invalid registration state")
            if record.get("reason") != "sim":
                raise ValueError("unsupported camera follow reason")
            camera_follow.append((event, record))
        except (ValueError, TypeError, json.JSONDecodeError):
            camera_follow_bad += 1
    if camera_follow or camera_follow_bad:
        out.extend(["", "CAMERA FOLLOW"])
        if camera_follow:
            event, record = camera_follow[-1]
            out.append("  " + show(event).split(" note ", 1)[0] + " " +
                       " ".join(f"{key}=0x{record[key]:08x}" for key in
                                ("p", "c", "car", "body", "look", "move")) +
                       f" registered={'yes' if record['r'] else 'no'} reason=simulation unsupported")
            if len(camera_follow) > 1:
                out.append(f"  {len(camera_follow) - 1} earlier camera follow records omitted")
        if camera_follow_bad:
            out.append(f"  {camera_follow_bad} malformed or unsupported camera follow records; state may be incomplete")

    def last_json_notes(prefix, version=1):
        records, malformed = [], 0
        for event in events:
            if event[2] != "note" or not event[3].startswith(prefix + " "):
                continue
            try:
                record = json.loads(event[3][len(prefix) + 1:])
                if record.get("v") != version:
                    raise ValueError("bad version")
                records.append(record)
            except (ValueError, TypeError, json.JSONDecodeError):
                malformed += 1
        return records, malformed

    grlast, grlast_bad = last_json_notes("grlast")
    if grlast or grlast_bad:
        out.extend(["", "LAST GROUND NATIVE"])
        if grlast:
            record = grlast[-1]
            out.append(f"  method={record.get('m', '?')} ok={record.get('ok', '?')}")
        if grlast_bad:
            out.append(f"  {grlast_bad} malformed grlast records")

    fail1, fail1_bad = last_json_notes("fail1")
    if fail1 or fail1_bad:
        out.extend(["", "FAILURE SCRIPT"])
        if fail1:
            out.append(f"  innermost={fail1[-1].get('cls', '?')}")
        if fail1_bad:
            out.append(f"  {fail1_bad} malformed fail1 records")

    card1, card1_bad = last_json_notes("card1")
    if card1 or card1_bad:
        out.extend(["", "CAR DELTA (since prior snapshot)"])
        if card1:
            record = card1[-1]
            out.append(
                f"  dx={record.get('dx', '?')} dy={record.get('dy', '?')} dz={record.get('dz', '?')}")
        if card1_bad:
            out.append(f"  {card1_bad} malformed card1 records")

    ground_states = []
    ground_malformed = 0
    for event in events:
        if event[2] != "note" or not event[3].startswith("ground1 "):
            continue
        try:
            record = json.loads(event[3][8:])
            version = record.get("v")
            if version not in (1, 2, 3):
                raise ValueError("invalid ground record")
            ints = ("c", "r", "t", "ds")
            if any(type(record.get(key)) is not int or record[key] < 0 for key in ints):
                raise ValueError("invalid ground record")
            if version >= 2:
                for key in ("p", "wa"):
                    if type(record.get(key)) is not int or record[key] < 0:
                        raise ValueError("invalid ground record")
            if version >= 3:
                for key in ("wl", "fg"):
                    if type(record.get(key)) is not int or record[key] < 0:
                        raise ValueError("invalid ground record")
            if (type(record.get("y")) not in (int, float) or
                type(record.get("z")) not in (int, float)):
                raise ValueError("invalid ground record")
            ground_states.append(record)
        except (ValueError, TypeError, json.JSONDecodeError):
            ground_malformed += 1
    water_states = []
    water_malformed = 0
    for event in events:
        if event[2] != "note" or not event[3].startswith("ground2 "):
            continue
        try:
            record = json.loads(event[3][8:])
            if record.get("v") != 1:
                raise ValueError("invalid water record")
            for key in ("px", "py", "pz", "ea", "eb"):
                if type(record.get(key)) not in (int, float):
                    raise ValueError("invalid water record")
            water_states.append(record)
        except (ValueError, TypeError, json.JSONDecodeError):
            water_malformed += 1
    if ground_states or ground_malformed or water_states or water_malformed:
        out.extend(["", "GROUND STATE"])
        if ground_states:
            record = ground_states[-1]
            ped = record.get("p", 0)
            water_active = record.get("wa", 0)
            water_limits = record.get("wl", 0)
            fog_settings = record.get("fg", 0)
            out.append(
                f"  cities={record['c']} roads={record['r']} traffic={record['t']} "
                f"pedestrian_types={ped} water_active={water_active} "
                f"water_limits={water_limits} fog_settings={fog_settings} "
                f"density_set={record['ds']} density={record['y']} density_hi={record['z']}")
            if len(ground_states) > 1:
                out.append(f"  {len(ground_states) - 1} earlier ground snapshots omitted")
        if water_states:
            record = water_states[-1]
            out.append(
                f"  water pos=({record['px']},{record['py']},{record['pz']}) "
                f"extents=({record['ea']},{record['eb']})")
            if len(water_states) > 1:
                out.append(f"  {len(water_states) - 1} earlier water snapshots omitted")
        if ground_malformed:
            out.append(f"  {ground_malformed} malformed ground records; state may be incomplete")
        if water_malformed:
            out.append(f"  {water_malformed} malformed water records; state may be incomplete")

    loads = {}
    load_malformed = 0
    for event in structured:
        if event[2] != "note" or not event[3].startswith("load1 "):
            continue
        try:
            record = json.loads(event[3][6:])
            if (record.get("v") != 1 or type(record.get("id")) is not int or
                record["id"] <= 0 or type(record.get("resource")) is not int or
                not 0 <= record["resource"] <= 0xffffffff or
                record.get("event") not in ("enter", "end") or
                not isinstance(record.get("stage"), str) or
                not re.fullmatch(r"[a-z_]{1,40}", record["stage"])):
                raise ValueError("invalid load record")
            loads.setdefault(record["id"], []).append(record)
        except (ValueError, TypeError, json.JSONDecodeError):
            load_malformed += 1
    if loads or load_malformed:
        out.extend(["", "TYPE-1 LOAD STAGES (persisted; end does not imply success)"])
        for attempt, entries in list(loads.items())[-20:]:
            stages = [entry["stage"] for entry in entries if entry["event"] == "enter"]
            resources = {entry["resource"] for entry in entries}
            ends = [entry for entry in entries if entry["event"] == "end"]
            consistent = (len(resources) == 1 and stages and stages[0] == "payload" and
                          len(ends) <= 1 and (not ends or
                          (entries[-1] is ends[0] and ends[0]["stage"] == stages[-1])))
            if not consistent:
                out.append(f"  attempt {attempt}: incomplete or inconsistent records; state unknown")
                continue
            resource = next(iter(resources))
            outcome = "returned or unwound; see cause" if ends else "no end record; outcome unknown"
            out.append(f"  attempt {attempt}: resource=0x{resource:08x} stages=" +
                       " -> ".join(stages) + f"; {outcome}")
        if len(loads) > 20:
            out.append(f"  {len(loads) - 20} earlier attempts omitted")
        if load_malformed:
            out.append(f"  {load_malformed} malformed or unsupported load records; state may be incomplete")

    assembly = [event for event in structured if event[2] == "note" and
                event[3].startswith(("car creation requested:", "part creation requested:",
                                     "ground creation requested:", "part install:", "part remove:"))]
    if assembly:
        out.extend(["", "ASSEMBLY EVENTS (persisted, oldest first)"])
        out.extend("  " + show(event) for event in assembly)

    records = {}
    removals = {}
    wheelrefs = []
    malformed = 0
    for event in structured:
        if event[2] != "note" or not event[3].startswith(("asm1 ", "asmr ", "wheelref ")):
            continue
        payload = event[3]
        kind = payload.split(" ", 1)[0]
        try:
            record = json.loads(payload[len(kind) + 1:])
            if kind == "asm1":
                if record.get("v") != 1 or record.get("k") not in ("req", "cand", "state", "result", "summary", "slot", "inventory") or not isinstance(record.get("id"), int):
                    raise ValueError("invalid assembly record")
                required = {"req": ("part", "target", "slot"), "cand": ("n", "candidate", "reasons"),
                            "state": ("parent0", "parent1", "pp0", "pp1", "tp0", "tp1", "omitted"),
                            "result": ("why", "resolved", "ps", "ts"), "summary": ("examined", "reasons"),
                            "slot": ("c", "node", "slot", "flags", "attached"),
                            "inventory": ("c", "node", "type", "slots", "shown", "links")}
                if any(field not in record for field in required[record["k"]]):
                    raise ValueError("missing assembly fields")
                for field in required[record["k"]]:
                    if not isinstance(record[field], str if field == "why" else int):
                        raise ValueError("invalid assembly field")
                records.setdefault(record["id"], []).append(record)
            elif kind == "asmr":
                if record.get("v") != 1 or record.get("k") not in ("req", "done") or not isinstance(record.get("id"), int):
                    raise ValueError("invalid removal record")
                required = {"req": ("part", "slot", "found", "tgt", "tsl"),
                            "done": ("det", "rb", "ra", "ub", "ua")}
                if any(field not in record for field in required[record["k"]]):
                    raise ValueError("missing removal fields")
                for field in required[record["k"]]:
                    if not isinstance(record[field], int):
                        raise ValueError("invalid removal field")
                removals.setdefault(record["id"], []).append(record)
            else:
                if record.get("v") != 1 or not isinstance(record.get("slotType"), int) \
                        or not isinstance(record.get("index"), int) \
                        or not isinstance(record.get("wheels"), int) \
                        or record.get("reason") not in ("no_indexed_car", "no_wheel_record", "record_without_proto"):
                    raise ValueError("invalid wheel pose record")
                wheelrefs.append(record)
        except (ValueError, TypeError, AttributeError):
            malformed += 1
    if records or removals or wheelrefs or malformed:
        out.extend(["", "STRUCTURED ASSEMBLY (schema 1, persisted run.log)",
                    "  IDs correlate records within this run. Log order across threads is not guaranteed."])
        reasons = {1: "self or missing instance", 2: "prototype unavailable", 4: "target full",
                   8: "part already attached", 16: "target slot unavailable", 32: "no matching pair found (including unavailable slots)",
                   64: "part slot unavailable"}
        for ident, entries in records.items():
            req = next((r for r in entries if r["k"] == "req"), None)
            result_record = next((r for r in entries if r["k"] == "result"), None)
            state = next((r for r in entries if r["k"] == "state"), None)
            candidates = [r for r in entries if r["k"] == "cand"]
            summary = next((r for r in entries if r["k"] == "summary"), None)
            if not req or not result_record or not state or any(sum(r["k"] == kind for r in entries) != 1 for kind in ("req", "state", "result")):
                out.append(f"  attempt {ident}: incomplete records; cause unknown")
                continue
            out.append(f"  attempt {ident}: part={req.get('part')} target={req.get('target')} requested_slot={req.get('slot')} outcome={result_record.get('why')} resolved_target={result_record.get('resolved')} part_slot={result_record.get('ps')} target_slot={result_record.get('ts')}")
            if not summary:
                out.append("    aggregate candidate summary unavailable; search evidence incomplete")
            if result_record.get("why") != "ok":
                out.append(f"    install-parent link before={state.get('parent0')} after={state.get('parent1')}; part physics before={state.get('pp0')} after={state.get('pp1')}; target physics before={state.get('tp0')} after={state.get('tp1')}")
                for candidate in candidates:
                    mask = candidate.get("reasons", 0)
                    labels = [name for bit, name in reasons.items() if isinstance(mask, int) and mask & bit]
                    out.append(f"    candidate {candidate.get('n')}: target={candidate.get('candidate')} reasons={', '.join(labels) if labels else 'none recorded'}")
                for record in entries:
                    if record["k"] == "inventory":
                        out.append(f"    inventory candidate={record['c']} node={record['node']} type={record['type']} slots={record['slots']} shown={record['shown']} attached_links={record['links']}")
                    elif record["k"] == "slot":
                        out.append(f"    slot candidate={record['c']} node={record['node']} id={record['slot']} flags={record['flags']} attached={record['attached']}")
                if summary:
                    mask = summary.get("reasons", 0)
                    labels = [name for bit, name in reasons.items() if isinstance(mask, int) and mask & bit]
                    out.append(f"    examined={summary['examined']} aggregate reasons={', '.join(labels) if labels else 'none recorded'}")
                if state.get("omitted"):
                    out.append(f"    {state['omitted']} candidate records omitted; cause may be incomplete")
        for ident, entries in removals.items():
            req = next((r for r in entries if r["k"] == "req"), None)
            done = next((r for r in entries if r["k"] == "done"), None)
            if not req or not done or any(sum(r["k"] == kind for r in entries) != 1 for kind in ("req", "done")):
                out.append(f"  removal {ident}: incomplete records; registry state after the removal is unknown")
                continue
            out.append(f"  removal {ident}: part={req['part']} requested_slot={req['slot']} found_slot={req['found']}"
                       f" host_target={req['tgt']} host_slot={req['tsl']} links_detached={done['det']}"
                       f" registry {done['rb']}->{done['ra']} used_slots {done['ub']}->{done['ua']}")
        declines = {}
        for record in wheelrefs:
            key = (record["slotType"], record["reason"])
            declines[key] = declines.get(key, 0) + 1
        for (slot_type, reason), count in sorted(declines.items()):
            out.append(f"  wheel pose override declined {count}x: slot_type={slot_type} reason={reason};"
                       " search and attachment used the geometry pose instead")
        if malformed:
            out.append(f"  {malformed} malformed, truncated, or unsupported records; evidence incomplete")
        if result is None:
            out.append("  Run did not record a final result; the last transition and later events may be missing.")
    elif assembly:
        out.extend(["", "STRUCTURED ASSEMBLY", "  unavailable: this run did not record version 1 assembly traces; rejection causes cannot be reconstructed"])

    out.extend(trace_lines(structured))

    out.append("")
    out.append("COUNTERS (whole run, from counters.txt)")
    counters = read_kv(run / "counters.txt")
    if counters is None:
        out.append("  (no counters.txt: an older run, or killed before it could be written)")
    else:
        shown = False
        for name, value in counters.items():
            if name.startswith("event_") and value != "0":
                out.append(f"  {name[len('event_'):]} events: {value}")
                shown = True
        for name in ("sound_started", "sound_updated", "sound_dropped", "input_recorded", "frames"):
            if name in counters:
                out.append(f"  {name.replace('_', ' ')}: {counters[name]}")
                shown = True
        if not shown:
            out.append("  (nothing counted)")

    if status == "crashed":
        out.append("")
        out.append("MACOS CRASH REPORT")
        out.extend(crash_report_lines(run, meta, result))

    out.append("")
    out.append("WHAT RAN")
    for key in ("start_utc", "process_start_unix", "argv", "cwd", "git_head", "git_changed_files", "exe_sha256", "src_tree_sha256", "source_identity", "initial_save_snapshot", "initial_save_limits", "game_data_status", "game_data_manifest", "game_data_manifest_sha256", "game_data_scope", "game_root",
                "data_root", "options_md5", "executable", "executable_mtime_utc", "build_type", "sdl_version", "macos"):
        if key in meta:
            out.append(f"  {key}: {meta[key]}")
    if not meta:
        out.append("  (no meta.txt)")
    if 'source_identity' not in meta:
        out.append("  source identity: unavailable (not recorded)")
    if 'initial_save_snapshot' not in meta:
        out.append("  initial save snapshot: unavailable (not recorded)")
    if 'game_data_status' not in meta:
        out.append("  game data identity: unavailable (not recorded)")
    else:
        path = run / 'game-data-manifest.json'
        expected = meta.get('game_data_manifest_sha256', '')
        try:
            if path.is_symlink() or not re.fullmatch(r'[0-9a-f]{64}', expected) or path.stat().st_size > 32 * 1024 * 1024:
                raise ValueError('manifest unavailable or unsafe')
            actual = hashlib.sha256(path.read_bytes()).hexdigest()
            out.append('  game data manifest integrity: ' + ('matches recorded hash' if actual == expected else 'MISMATCH'))
        except (OSError, ValueError):
            out.append('  game data manifest integrity: unavailable')
    if (run / 'source-manifest.txt').is_file():
        out.append(f"  source manifest: {run / 'source-manifest.txt'}")
    if (run / 'initial-save-manifest.txt').is_file():
        out.append(f"  initial save manifest: {run / 'initial-save-manifest.txt'}")
    elif meta.get('initial_save_snapshot', '').startswith('captured'):
        out.append("  initial save manifest: unavailable (snapshot metadata says captured)")
    script = read_text(run / "script.txt")
    if script is not None:
        out.append("  script used (script.txt):")
        out.extend("    " + line for line in script.splitlines()[:30])
    elif meta.get("script", "none") != "none":
        out.append(f"  script: {meta['script']} (no copy in this run)")

    inputs = read_text(run / "input.txt")
    if inputs is not None:
        recorded = sum(1 for line in inputs.splitlines() if line and not line.startswith("#"))
        out.append(f"  real input recorded: {recorded} events (input.txt); replay with tools/replay.sh {run}")

    out.append("")
    out.append("FILES")
    for name in ("run.log", "meta.txt", "result.txt", "crash.txt", "counters.txt", "input.txt", "script.txt", "flight-0.log", "flight-1.log", "macos-crash.ips"):
        path = run / name
        out.append(f"  {path}" + ("" if path.exists() else "   (not present)"))
    return "\n".join(out)


def run_base():
    return pathlib.Path(os.environ.get("SLRR_RUN_DIR") or "local/runs")


def is_stock(run):
    return (read_meta(run / "meta.txt") or {}).get("kind") == "stock-wine"


def load_stock():
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    try:
        import diagnose_stock
    except ImportError:
        return None
    return diagnose_stock


def summary(run):
    """(status, cause) for any run kind."""
    if is_stock(run):
        stock = load_stock()
        return stock.summary_line(run) if stock else ("stock-wine", "stock run support not installed")
    return native_summary(run)


def one_line(run):
    status, cause = summary(run)
    return f"{run.name}  {status:<10} {cause or '-'}"


def list_runs():
    base = run_base()
    runs = sorted((p for p in base.glob("*") if p.is_dir() and not p.is_symlink()), key=lambda p: p.name, reverse=True)
    if not runs:
        return f"no runs found in {base}"
    return "\n".join(one_line(r) for r in runs[:10])


def manifest(run, hash_value):
    """Load a hash-verified run-local manifest, falling back to the historical store."""
    if not re.fullmatch(r'[0-9a-f]{64}', hash_value or ''):
        return None, []
    notes = []
    for path in (run / 'source-manifest.txt', REPO / 'local/src-manifests' / f'{hash_value}.txt'):
        try:
            data = path.read_bytes()
        except FileNotFoundError:
            continue
        except OSError:
            notes.append(f"cannot read source manifest: {path}")
            continue
        if hashlib.sha256(data).hexdigest() != hash_value:
            notes.append(f"source manifest hash mismatch: {path}")
            continue
        try:
            lines = data.decode('utf-8').splitlines()
            entries = {}
            for line in lines:
                name, digest = line.split('\t')
                if not name or name in entries or not re.fullmatch(r'[0-9a-f]{64}', digest):
                    raise ValueError('invalid manifest entry')
                entries[name] = digest
        except (UnicodeDecodeError, ValueError):
            notes.append(f"invalid source manifest: {path}")
            continue
        return entries, notes
    return None, notes


def diff_code(a, b):
    metas = [read_meta(r / "meta.txt") or {} for r in (a, b)]
    out = []
    for label, field in (("source tree", "src_tree_sha256"), ("executable", "exe_sha256")):
        x, y = (m.get(field, "unknown") for m in metas)
        out.append(f"{label}: {x}  vs  {y}  ->  " + ("same" if x == y and x != "unknown" else "unknown" if "unknown" in (x, y) else "DIFFERENT"))
    out.append("Source identity describes files observed at launch, not necessarily the executable's build.")
    hashes = [m.get("src_tree_sha256", "unknown") for m in metas]
    manifests = []
    for run, hash_value in zip((a, b), hashes):
        entries, notes = manifest(run, hash_value)
        out.extend(notes)
        if entries is None:
            out.append(f"no verified manifest for source tree {hash_value}; cannot list changed files")
        manifests.append(entries)
    if all(m is not None for m in manifests):
        old, new = manifests
        changed = sorted(p for p in set(old) | set(new) if old.get(p) != new.get(p))
        if not changed:
            out.append("source files: identical")
        else:
            out.append(f"changed files ({len(changed)}):")
            for path in changed:
                out.append(f"  {'added' if path not in old else 'removed' if path not in new else 'modified'}  {path}")
    return "\n".join(out)


def resolve(arg):
    base = run_base()
    if arg == "latest":
        return (base / "latest").resolve()
    path = pathlib.Path(arg)
    if path.is_dir():
        return path
    return base / arg


def main():
    args = sys.argv[1:]
    if args and args[0] in ("-h", "--help"):
        print(__doc__.strip().splitlines()[2])
        return 0
    if args and args[0] == "--diff-code":
        if len(args) != 3:
            print("usage: tools/diagnose.sh --diff-code RUN_A RUN_B", file=sys.stderr)
            return 2
        runs = [resolve(x) for x in args[1:]]
        for r in runs:
            if not r.is_dir():
                print(f"diagnose: no run directory at {r}.", file=sys.stderr)
                return 1
        print(diff_code(*runs))
        return 0
    raw_mode = "--raw" in args
    args = [x for x in args if x != "--raw"]
    if len(args) > 1:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    arg = args[0] if args else "latest"
    if arg == "list":
        print(list_runs())
        return 0
    run = resolve(arg)
    if not run.is_dir():
        print(f"diagnose: no run directory at {run}. Try 'tools/diagnose.sh list'.", file=sys.stderr)
        return 1
    if is_stock(run):
        stock = load_stock()
        print(stock.render(run) if stock else "stock run support not installed")
        return 0
    print(diagnose(run, raw_mode))
    return 0


if __name__ == "__main__":
    sys.exit(main())
