#!/usr/bin/env python3
"""Package a stock-under-Wine capture run into the common run layout (see docs/run-log.md, "Stock-under-Wine runs").

Usage: python3 -I tools/wine/capture/finish_run.py CAPTURE-DIR [--out DIR] [--base DIR] [--stopped-at EPOCH]

CAPTURE-DIR is local/wine-runs/capture-<date>-<time>. It is only read, never changed or moved.
The packaged folder (default local/runs/<UTC start>-stock-<id>, or exactly --out) gets
meta.txt, run.log, script.txt, input.txt (when clicks or keys can be rebuilt), counters.txt,
result.txt, shots.txt and shots/ (at most three frames). Each packaging run uses a new folder.
Prints the packaged folder. Python standard library only.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import statistics
import sys
import time

STOCK_MD5 = "e686099e7233bb7d7c461b1b35c5064c"
HUNG_MIN_SECONDS = 20.0
STALL_SECONDS = 30.0
FEW_PRESENTS = 2

PROXY_LINE = re.compile(r"^\[\s*(\d+) ms\] (.*)$")
ORIGIN = re.compile(r"clock_origin_tick=(\d+)")
FLOW_LINE = re.compile(r"^(\d\d):(\d\d):(\d\d) (.*)$")
WINE_LINE = re.compile(r"^[0-9a-f]{4}:")
DUMP_OK = re.compile(r"dump ok: present=(\d+) (\d+)x(\d+) fmt=\d+ nonblack_pixels=(\d+) of (\d+) draws_since_last_present=(\d+) \((\w+)\)")
STATS = re.compile(r"stats: presents=(\d+) draws=(\d+) \(\+\d+\) clears=(\d+) resets=(\d+) dumps=(\d+) dump_fail=(\d+)")
PRESENTS_IN_FLOW = re.compile(r"presents=(\d+)")

NOISE_RULES = (
    ("trace", lambda s: ":trace:" in s),
    ("vulkan", lambda s: "built without Vulkan" in s),
    ("winebth", lambda s: "winebth" in s or "ZwLoadDriver" in s),
    ("sysperf", lambda s: "SYSTEM_PERFORMANCE_INFORMATION" in s),
    ("gl_vendor", lambda s: "wined3d_guess_card_vendor" in s),
    ("glsl_warning", lambda s: "print_glsl_info_log" in s and "ERROR" not in s),
    ("line_pattern", lambda s: "state_linepattern_w" in s),
    ("mapped_buffer_copy", lambda s: "wow64_map_buffer" in s),
)
ERROR_RULES = (
    re.compile(r"err:seh"),
    re.compile(r"Unhandled exception", re.I),
    re.compile(r"unhandled page fault", re.I),
    re.compile(r"page fault on"),
)
FLOW_ERRORS = ("game process is gone", "no menu after", "intermittent startup crash", "stuck at startup")


def read_text(path):
    try:
        return pathlib.Path(path).read_text(errors="replace")
    except OSError:
        return None


def mtime(path):
    try:
        return pathlib.Path(path).stat().st_mtime
    except OSError:
        return None


def digest(path, algo):
    try:
        h = hashlib.new(algo)
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest()
    except OSError:
        return None


def clean(text, limit=220):
    text = " ".join(text.split())
    return text if len(text) <= limit else text[: limit - 3] + "..."


def stamp(t, frame):
    return f"[+{max(t, 0.0):06.3f} f{frame}]"


class Event:
    __slots__ = ("t", "frame", "kind", "text")

    def __init__(self, t, frame, kind, text):
        self.t, self.frame, self.kind, self.text = t, frame, kind, text


def parse_proxy(text):
    """d3d9proxy.log -> (events, facts). Frame numbers are Direct3D Present calls."""
    events, facts = [], {"presents": 0, "t_last_present": None, "dumps": 0, "dump_fail": 0, "draws": 0, "clears": 0,
                         "resets": 0, "last_nonblack": None, "last_ms": None, "stats": [], "focus_lines": 0,
                         "device_params": None, "first_input_move": None}
    frame, last_ratio, last_focus, prev_stats = 0, None, None, None
    pending_last_dump = None
    for raw in (text or "").splitlines():
        m = PROXY_LINE.match(raw)
        if not m:
            continue
        t, body = int(m[1]) / 1000.0, m[2]
        facts["last_ms"] = t
        d = DUMP_OK.search(body)
        if d:
            present, nonblack, total, draws, label = int(d[1]), int(d[4]), int(d[5]), int(d[6]), d[7]
            frame = max(frame, present)
            facts["presents"] = max(facts["presents"], present)
            facts["t_last_present"] = t
            facts["dumps"] += 1
            ratio = nonblack / total if total else 0.0
            facts["last_nonblack"] = (present, nonblack, total)
            line = f"dump present={present} nonblack_pixels={nonblack} of {total} draws_since_last_present={draws} ({label})"
            if label == "first" or last_ratio is None or abs(ratio - last_ratio) > 0.02:
                events.append(Event(t, frame, "frame", line))
                last_ratio, pending_last_dump = ratio, None
            else:
                pending_last_dump = Event(t, frame, "frame", line)
            continue
        s = STATS.search(body)
        if s:
            presents = int(s[1])
            frame = max(frame, presents)
            facts["presents"] = max(facts["presents"], presents)
            if presents > (prev_stats[0] if prev_stats else 0):
                facts["t_last_present"] = t
            facts.update(draws=int(s[2]), clears=int(s[3]), resets=int(s[4]), dumps=max(facts["dumps"], int(s[5])), dump_fail=int(s[6]))
            fps = ""
            if prev_stats and t > prev_stats[1]:
                fps = f" fps={(presents - prev_stats[0]) / (t - prev_stats[1]):.0f}"
            prev_stats = (presents, t)
            facts["stats"].append((t, presents))
            events.append(Event(t, frame, "heartbeat", f"presents={presents} draws={s[2]} clears={s[3]}{fps}"))
            continue
        if body.startswith("focus:"):
            facts["focus_lines"] += 1
            if body != last_focus:
                events.append(Event(t, frame, "note", clean(body)))
            last_focus = body
            continue
        if body.startswith("input:"):
            events.append(Event(t, frame, "input", clean(body[6:].strip())))
            mv = re.match(r"input: move (-?\d+) (-?\d+)", body)
            if mv and facts["first_input_move"] is None:
                facts["first_input_move"] = t
            continue
        if "params:" in body and facts["device_params"] is None:
            facts["device_params"] = body.split("params:", 1)[1].strip()
        events.append(Event(t, frame, "note", clean(body)))
    if pending_last_dump:
        events.append(pending_last_dump)
    events.sort(key=lambda e: e.t)
    return events, facts


def parse_dinput(text):
    events, cmds, last_ms, first_cmd = [], [], None, {}
    for raw in (text or "").splitlines():
        m = PROXY_LINE.match(raw)
        if not m:
            continue
        t, body = int(m[1]) / 1000.0, m[2]
        last_ms = t
        if body.startswith("cmd:"):
            cmd = body[4:].strip()
            cmds.append((t, cmd))
            events.append(Event(t, 0, "input", "dinput " + cmd))
            first_cmd.setdefault(cmd.split()[0] if cmd.split() else "", t)
        elif body.startswith(("input_queue: dropped ", "input_buffered: delivered ", "input_buffered: source_overflow ", "input_state: observed_down ")):
            events.append(Event(t, 0, "input", "dinput " + clean(body)))
        elif body.startswith("proxy loaded") or body.startswith("DirectInput8Create"):
            events.append(Event(t, 0, "note", "dinput8 proxy: " + clean(body)))
    return events, cmds, last_ms, first_cmd


def input_delivery(text, commands):
    """Counts observed proxy records; a queued event does not prove game behavior."""
    result = {'available': False, 'commands': len(commands), 'queued': 0,
              'dropped': 0, 'buffered_delivered': 0, 'state_down_observed': 0, 'source_overflow': 0}
    for raw in (text or '').splitlines():
        match = PROXY_LINE.match(raw)
        if not match:
            continue
        body = match[2]
        if body == 'input_telemetry=v1':
            result['available'] = True
        elif body.startswith('input_queue: queued '):
            result['queued'] += 1
        elif body.startswith('input_queue: dropped '):
            result['dropped'] += 1
        elif body.startswith('input_buffered: delivered '):
            result['buffered_delivered'] += 1
        elif body.startswith('input_buffered: source_overflow '):
            result['source_overflow'] += 1
        elif body.startswith('input_state: observed_down '):
            result['state_down_observed'] += 1
    return result


def parse_flow(text):
    """flow.log -> list of (seconds of day, text). The proxy tail printed at the end has no clock and is skipped."""
    rows, day_offset, prev = [], 0, None
    for raw in (text or "").splitlines():
        m = FLOW_LINE.match(raw)
        if not m:
            continue
        secs = int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3])
        if prev is not None and secs + day_offset < prev - 60:
            day_offset += 86400
        secs += day_offset
        prev = secs
        rows.append((secs, m[4]))
    return rows


def day_seconds(epoch):
    dt = datetime.datetime.fromtimestamp(epoch)
    return dt.hour * 3600 + dt.minute * 60 + dt.second + dt.microsecond / 1e6


def clock_offset(flow_rows, first_cmd, facts, proxy_log_mtime):
    """Wall clock seconds-of-day of proxy time 0, with the method used. Accuracy about one second at best."""
    anchors = []
    wanted = (("click free ride", first_cmd.get("mclick")), ("start keyboard script", first_cmd.get("kdown")),
              ("move pointer", facts.get("first_input_move")))
    for secs, text in flow_rows:
        for prefix, t in wanted:
            if t is not None and text.lower().startswith(prefix):
                anchors.append(secs - t)
    if anchors:
        return statistics.median(anchors), "flow.log steps matched to the proxies' command lines (about 1 s)"
    by_stats = []
    stat_time = {p: t for t, p in facts["stats"]}
    for secs, text in flow_rows:
        m = PRESENTS_IN_FLOW.search(text)
        if m and int(m[1]) in stat_time:
            by_stats.append(secs - stat_time[int(m[1])])
    if by_stats:
        return min(by_stats), "flow.log 'presents so far' matched to proxy stats lines (a few seconds)"
    if proxy_log_mtime is not None and facts["last_ms"] is not None:
        return day_seconds(proxy_log_mtime) - facts["last_ms"], "file time of d3d9proxy.log (inference, about 1 s)"
    return None, "no clock link between flow.log and the proxy log"


def normalise(line):
    line = re.sub(r"^[0-9a-f]{4}:", "", line)
    return re.sub(r"[0-9a-fA-F]{8,}|#\d+|\b\d+\b", "N", line)


def parse_wine(text):
    """game-run.log -> (errors, notes (text, count), noise counts, err_lines, total lines). Wine lines have no clock."""
    errors, notes, noise, err_lines, total = [], {}, {}, 0, 0
    for raw in (text or "").splitlines():
        if not raw.strip():
            continue
        total += 1
        if ":err:" in raw:
            err_lines += 1
        if any(rule.search(raw) for rule in ERROR_RULES):
            errors.append(clean(raw))
            continue
        bucket = next((name for name, test in NOISE_RULES if test(raw)), None)
        if bucket:
            noise[bucket] = noise.get(bucket, 0) + 1
            continue
        key = normalise(raw)
        if key in notes:
            notes[key][1] += 1
        else:
            notes[key] = [clean(raw), 1]
    return errors, list(notes.values()), noise, err_lines, total


def local_start(capture):
    m = re.search(r"(\d{8})-(\d{6})", capture.name)
    if m:
        try:
            tm = time.strptime(m[1] + m[2], "%Y%m%d%H%M%S")
            return time.mktime(tm)
        except ValueError:
            pass
    return mtime(capture / "metadata.txt") or mtime(capture) or time.time()


def sample_time(capture):
    text = read_text(capture / "sample-hang.txt")
    if not text:
        return None
    m = re.search(r"^Date/Time:\s+(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)", text, re.M)
    if not m:
        return None
    try:
        tm = time.strptime(m[1], "%Y-%m-%d %H:%M:%S")
    except ValueError:
        return None
    return day_seconds(time.mktime(tm))


def sample_spinner(capture):
    text = read_text(capture / "sample-hang.txt")
    if not text or "segv_handler" not in text:
        return None
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if "segv_handler" in line:
            for j in range(i, max(i - 8, 0), -1):
                m = re.match(r"\s+(\d+) (Thread_\d+\S*)", lines[j])
                if m:
                    total = re.search(r"Analysis of sampling .*", text)
                    return f"{m[2]} spent {m[1]} samples inside Wine's segv_handler / setup_raise_exception"
    return None


def wine_info(capture, wine_text):
    try:
        launch = json.loads((capture / "launch.json").read_text())
    except (OSError, ValueError):
        launch = {}
    path = launch.get("wine_path") or "not recorded"
    version = launch.get("wine_version")
    note = "recorded at launch" if launch.get("wine_path") else "not recorded at launch"
    cmd = None
    for line in (wine_text or "").splitlines():
        if "explorer.exe" in line and "cmdline" in line and "desktop=" in line and "StreetLegal_Redline" in line:
            m = re.search(r'cmdline L"(.*?)" parent', line) or re.search(r'cmdline L"(.*)', line)
            if m:
                cmd = m[1].replace('\\"', '"').replace("\\\\", "\\")
                break
    if isinstance(launch.get("argv"), list):
        cmd = json.dumps(launch["argv"])
    return str(path), version, note, cmd


def proxy_clock_delta(d3d_text, di_text):
    """DInput origin minus D3D origin in seconds, with 32-bit tick rollover."""
    d3d = ORIGIN.search(d3d_text or "")
    di = ORIGIN.search(di_text or "")
    if not d3d or not di:
        return None
    delta = (int(di[1]) - int(d3d[1])) & 0xffffffff
    if delta >= 0x80000000:
        delta -= 0x100000000
    return delta / 1000.0


def file_listing(root, patterns):
    out = []
    for p in sorted(root.glob(patterns)):
        out.append(p)
    return out


def package(capture, out_dir, stopped_at=None):
    capture = capture.resolve()
    dumps = capture / "game" / "dumps"
    d3d_text = read_text(dumps / "d3d9proxy.log")
    di_text = read_text(dumps / "dinput8proxy.log")
    flow_text = read_text(capture / "flow.log")
    wine_text = read_text(capture / "game-run.log")
    notes_out = []

    proxy_events, facts = parse_proxy(d3d_text)
    di_events, di_cmds, di_last, first_cmd = parse_dinput(di_text)
    delivery = input_delivery(di_text, di_cmds)
    delta = proxy_clock_delta(d3d_text, di_text)
    unaligned_di = []
    if delta is None:
        unaligned_di = di_events
        di_events = []
        di_last = None
        first_cmd = {}
    else:
        for event in di_events:
            event.t += delta
        if di_last is not None:
            di_last += delta
        first_cmd = {key: value + delta for key, value in first_cmd.items()}
    flow_rows = parse_flow(flow_text)
    offset, offset_method = clock_offset(flow_rows, first_cmd, facts, mtime(dumps / "d3d9proxy.log"))

    def flow_t(secs):
        return secs - offset if offset is not None else None

    stat_frames = sorted((t, p) for t, p in facts["stats"])

    def frame_at(t):
        best = 0
        for pt, p in ((e.t, e.frame) for e in proxy_events):
            if pt <= t:
                best = max(best, p)
        return best

    flow_events, flow_error, flow_finished = [], None, False
    for secs, text in flow_rows:
        t = flow_t(secs)
        low = text.lower()
        kind = "script"
        if any(w in low for w in FLOW_ERRORS):
            kind, flow_error = "error", flow_error or text
        elif low.startswith("shot "):
            kind = "shot"
        if low.startswith("script finished"):
            flow_finished = True
        m = PRESENTS_IN_FLOW.search(text)
        if t is None:
            t = 0.0
        frame = int(m[1]) if m else frame_at(t)
        flow_events.append(Event(t, frame, kind, clean(text)))

    timed = sorted(proxy_events + di_events + flow_events, key=lambda e: (e.t, 0))
    for e in di_events:
        e.frame = frame_at(e.t)
    errors, wine_notes, noise, err_lines, wine_total = parse_wine(wine_text)

    t_candidates = [x for x in (facts["last_ms"], di_last) if x is not None] + [e.t for e in flow_events if offset is not None]
    st = sample_time(capture)
    if st is not None and offset is not None:
        t_candidates.append(st - offset)
    if stopped_at is not None and offset is not None:
        t_candidates.append(day_seconds(stopped_at) - offset)
    t_end = max(t_candidates) if t_candidates else 0.0
    presents, t_lp = facts["presents"], facts["t_last_present"]
    gap = (t_end - t_lp) if t_lp is not None else t_end

    evidence = []
    if errors:
        status = "crashed"
        reason = f"Wine reported an exception: {errors[0]}"
        evidence.append(f"game-run.log has {len(errors)} exception line(s); the first is quoted in the reason")
        evidence.append(f"proxy log: {presents} present(s), last at +{(t_lp or 0):.3f} s; Wine log lines carry no clock")
        wl, pl = mtime(capture / "game-run.log"), mtime(dumps / "d3d9proxy.log")
        if wl is not None and pl is not None:
            evidence.append(f"inference from file times: game-run.log was last written {wl - pl:+.1f} s from the last proxy log write")
    elif flow_error:
        status = "failed"
        reason = f"the flow script reported: {flow_error}"
        evidence.append("flow.log message; no Wine exception line in game-run.log")
    elif not d3d_text:
        status, reason = "unknown", "no d3d9proxy.log: the game never loaded the frame proxy, or the run folder is incomplete"
    elif presents <= FEW_PRESENTS and gap >= HUNG_MIN_SECONDS:
        status = "hung"
        reason = f"only {presents} frame(s) were presented, then none for at least {gap:.0f} s (observed until +{t_end:.0f} s)"
        evidence.append(f"d3d9proxy.log: last Present at +{(t_lp or 0):.3f} s, frame {presents}")
        if di_last is not None or any(e.kind == "input" and e.t > (t_lp or 0) + 5 for e in proxy_events):
            evidence.append("the proxy's input thread kept running after the last frame (the process was alive, not exited)")
        spin = sample_spinner(capture)
        if spin:
            evidence.append(f"sample-hang.txt: {spin} (inference: the game thread was faulting repeatedly)")
    elif presents <= FEW_PRESENTS:
        status = "unknown"
        reason = f"only {presents} frame(s) in the {t_end:.0f} s observed; too short to tell hung from slow (needs {HUNG_MIN_SECONDS:.0f} s)"
    elif gap >= STALL_SECONDS:
        status = "hung"
        reason = f"frames stopped at {presents} (+{t_lp:.0f} s) and none came for at least {gap:.0f} s"
        evidence.append("no Present for the rest of the observed run")
    elif flow_finished:
        status = "unknown"
        reason = "the flow script finished and frames continued, but scene success and process exit were not recorded"
        evidence.append(f"frames kept coming until +{t_lp:.0f} s and the flow script reported 'script finished'; this does not establish input effects")
    else:
        status = "terminated"
        reason = f"frames kept coming until +{t_lp:.0f} s; no flow script finish was recorded, so it was stopped by hand or still running"
        evidence.append("no exception or stall was observed; the end of the process was not recorded")
    if offset is None and flow_rows:
        evidence.append("flow.log times could not be placed on the proxy clock")
    if delivery['available']:
        evidence.append(f"DirectInput proxy observed {delivery['commands']} command(s), {delivery['queued']} buffered event(s) queued, {delivery['dropped']} buffered event(s) dropped, {delivery['buffered_delivered']} buffered event(s) returned and {delivery['state_down_observed']} held-down state observation(s), {delivery['source_overflow']} underlying DirectInput overflow response(s); returned input does not establish game response")
    else:
        evidence.append(f"DirectInput proxy recorded {delivery['commands']} command(s); buffered delivery, queue drops and state observations were not recorded")

    ratio = None
    if facts["last_nonblack"]:
        _, nb, tot = facts["last_nonblack"]
        ratio = nb / tot if tot else 0.0

    return {
        "capture": capture, "dumps": dumps, "delivery": delivery, "timed": timed, "wine_notes": wine_notes, "errors": errors, "noise": noise,
        "err_lines": err_lines, "wine_total": wine_total, "facts": facts, "di_cmds": di_cmds, "flow_rows": flow_rows,
        "offset": offset, "offset_method": offset_method, "status": status, "reason": reason, "evidence": evidence,
        "unaligned_di": unaligned_di, "proxy_delta": delta,
        "t_end": t_end, "ratio": ratio, "wine_text": wine_text, "flow_finished": flow_finished, "flow_error": flow_error,
    }


def build_input_txt(info):
    lines, facts = [], info["facts"]
    for e in info["timed"]:
        if e.kind != "input":
            continue
        text = e.text
        if text.startswith("dinput "):
            parts = text[7:].split()
            if parts and parts[0] == "mclick" and len(parts) >= 3:
                lines.append((e.t, f"{e.t:.3f} down"))
                lines.append((e.t + int(parts[2]) / 1000.0, f"{e.t + int(parts[2]) / 1000.0:.3f} up"))
            elif parts and parts[0] == "kdown" and len(parts) >= 2:
                lines.append((e.t, f"{e.t:.3f} keydown {parts[1]}"))
            elif parts and parts[0] == "kup" and len(parts) >= 2:
                lines.append((e.t, f"{e.t:.3f} keyup {parts[1]}"))
            elif parts and parts[0] == "key" and len(parts) >= 2:
                hold = int(parts[2]) / 1000.0 if len(parts) > 2 else 0.1
                lines.append((e.t, f"{e.t:.3f} keydown {parts[1]}"))
                lines.append((e.t + hold, f"{e.t + hold:.3f} keyup {parts[1]}"))
        else:
            mv = re.match(r"move (-?\d+) (-?\d+)", text)
            if mv:
                lines.append((e.t, f"{e.t:.3f} move {mv[1]} {int(mv[2]) - 33}"))
            elif text.startswith("keydown "):
                lines.append((e.t, f"{e.t:.3f} keydown {text.split()[1]}"))
            elif text.startswith("keyup "):
                lines.append((e.t, f"{e.t:.3f} keyup {text.split()[1]}"))
    if not lines:
        return None
    lines.sort(key=lambda x: x[0])
    head = [
        "# Rebuilt from commands seen by the proxies; these are issued inputs, not proof the game received or acted on them.",
        "# Rebuilt from the proxies' logs, best effort. Seconds are on the d3d9 proxy clock (time since the game process loaded the proxy),",
        "# not seconds since a frame loop started. move y is the desktop y minus 33 (the game window sits 33 px lower in Wine's desktop).",
        "# down/up and move match the native --script words. keydown/keyup (DirectInput scan codes or names) have no native script word yet.",
    ]
    if info["unaligned_di"]:
        head.append("# DirectInput events are omitted because their clock origin was not recorded.")
    return "\n".join(head + [l for _, l in lines]) + "\n"


def build_script_txt(info, capture):
    out = ["# Input commands recorded by the proxies; command issue alone does not prove delivery or game response.",
           "# The walk itself is tools/wine/capture/scripted_flow.sh (the version in the repo now, which may differ from the one used)."]
    for name, label in (("input.cmd", "d3d9 proxy input.cmd"), ("dinput.cmd", "dinput8 proxy dinput.cmd")):
        text = read_text(capture / "game" / "dumps" / name)
        if text and text.strip():
            out.append(f"# --- {label} ---")
            out.extend(text.rstrip("\n").splitlines())
    if len(out) == 2:
        out.append("# (no input commands were sent in this run)")
    return "\n".join(out) + "\n"


def build_run_log(info):
    lines = []
    for e in info["timed"]:
        lines.append(f"{stamp(e.t, e.frame)} {e.kind} {e.text}")
    if info["unaligned_di"]:
        lines.append("--- DirectInput proxy clock is unaligned; times below are relative to its own origin ---")
        for e in info["unaligned_di"]:
            lines.append(f"[di+{e.t:06.3f} f--] {e.kind} {e.text}")
    lines.append("--- wine log (game-run.log: no clock in these lines, order within this group only) ---")
    for text in [t for t in info["errors"]]:
        lines.append(f"[+--.--- f--] error {text}")
    for text, count in info["wine_notes"]:
        lines.append(f"[+--.--- f--] note {text}" + (f" (x{count})" if count > 1 else ""))
    if info["noise"]:
        total = sum(info["noise"].values())
        parts = ", ".join(f"{k} {v}" for k, v in sorted(info["noise"].items()))
        lines.append(f"[+--.--- f--] note {total} known harmless Wine lines left out ({parts})")
    return "\n".join(lines) + "\n"


def build_counters(info):
    f, noise = info["facts"], info["noise"]
    pairs = [("presents", f["presents"]), ("draws", f["draws"]), ("clears", f["clears"]), ("resets", f["resets"]),
             ("dumps", f["dumps"]), ("dump_fail", f["dump_fail"])]
    if info["ratio"] is not None:
        pairs.append(("last_dump_nonblack_ratio", f"{info['ratio']:.4f}"))
    pairs += [("err_lines", info["err_lines"]), ("wine_exceptions", len(info["errors"])), ("noise", sum(noise.values())),
              ("wine_log_lines", info["wine_total"]), ("focus_lines", f["focus_lines"])]
    delivery = info['delivery']
    pairs.append(('dinput_commands_issued', delivery['commands']))
    if delivery['available']:
        pairs += [(f'dinput_{key}', delivery[key]) for key in ('queued', 'dropped', 'buffered_delivered', 'state_down_observed', 'source_overflow')]
    pairs += [(f"noise_{k}", v) for k, v in sorted(noise.items())]
    return "".join(f"{k}={v}\n" for k, v in pairs)


def build_meta(info, capture, run_id, start_utc):
    dumps = capture / "game" / "dumps"
    md5_text = read_text(capture / "exe-md5.txt") or ""
    m = re.search(r"=\s*([0-9a-f]{32})", md5_text) or re.search(r"\b([0-9a-f]{32})\b", md5_text)
    exe_md5, source = (m[1], "exe-md5.txt") if m else (digest(capture / "game" / "StreetLegal_Redline.exe", "md5"), "hashed from the run copy")
    if exe_md5 is None:
        verdict, source = "UNKNOWN (no exe-md5.txt and no run copy)", "none"
    else:
        verdict = "MATCH" if exe_md5 == STOCK_MD5 else "MISMATCH"
    wine_path, wine_version, wine_note, wine_cmd = wine_info(capture, info["wine_text"])
    comp = read_text(capture / "data-comparison.txt")
    if comp:
        comp_summary = clean(" | ".join(l.strip() for l in comp.splitlines() if l.strip()), 400)
    else:
        comp_summary = "not run (no data-comparison.txt; run tools/wine/capture/compare_data.py)"
    meta = [
        ("kind", "stock-wine"), ("run_id", run_id), ("source_run", str(capture)), ("start_utc", start_utc),
        ("exe_md5", exe_md5 or "unknown"), ("exe_md5_source", source), ("expected_stock_md5", STOCK_MD5),
        ("exe_md5_check", verdict),
        ("wine_path", wine_path), ("wine_path_note", wine_note), ("wine_version", wine_version or "not recorded at launch"),
    ]
    try:
        launch = json.loads((capture / "launch.json").read_text())
        identity = launch.get("game_data", {})
    except (OSError, ValueError, AttributeError):
        identity = {}
    if isinstance(identity, dict) and identity.get("status") in ("complete", "incomplete"):
        expected = identity.get("sha256")
        actual = digest(capture / "game-data-manifest.json", "sha256")
        check = "MATCH" if expected and actual == expected else "MISSING" if actual is None else "MISMATCH"
        meta.extend([("game_data_manifest", str(capture / "game-data-manifest.json")),
                     ("game_data_status", identity["status"]), ("game_data_manifest_sha256", expected or "unknown"),
                     ("game_data_manifest_check", check),
                     ("game_data_scope", "prelaunch data tree; not a file-access trace or atomic snapshot")])
    else:
        meta.append(("game_data_status", "unavailable or not recorded at launch"))
        if isinstance(identity, dict) and identity.get("reason"):
            meta.append(("game_data_reason", clean(str(identity["reason"]))))
    if re.search(r":trace:", info["wine_text"] or ""):
        meta.append(("winedebug", "trace output was on in this run (game-run.log holds trace lines); the usual setting is err+all"))
    meta.append(("argv", wine_cmd or "not recorded; run_capture.sh default is: wine explorer /desktop slrr,800x600 <run>/game/StreetLegal_Redline.exe"))
    if info["facts"]["device_params"]:
        meta.append(("d3d_device", info["facts"]["device_params"]))
    meta.append(("data_comparison", comp_summary))
    for name in ("d3d9.dll", "dinput8.dll", "mcicda.dll"):
        h = digest(capture / "game" / name, "sha256")
        meta.append((name.replace(".", "_") + "_sha256", h or "not present"))
    meta.append(("clock_sync", info["offset_method"]))
    meta.append(("proxy_clock_sync", f"DirectInput origin is {info['proxy_delta']:+.3f} s from D3D origin" if info["proxy_delta"] is not None else "unknown; DirectInput events are not merged into the D3D timeline"))
    meta.append(("clock_note", "times are the d3d9 proxy clock (seconds since the proxy loaded); frames are Direct3D Present calls, not simulation frames; Wine log lines have no clock"))
    meta.append(("packaged_by", "tools/wine/capture/finish_run.py"))
    meta.append(("packaged_utc", datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")))
    return "".join(f"{k}: {v}\n" for k, v in meta)


def build_result(info):
    f = info["facts"]
    lines = [("status", info["status"]), ("exit_code", "unknown"), ("signal", "unknown"),
             ("seconds", f"{info['t_end']:.1f}"), ("frames", f["presents"]), ("reason", info["reason"])]
    out = "".join(f"{k}={v}\n" for k, v in lines)
    out += "".join(f"evidence={e}\n" for e in info["evidence"])
    return out


def copy_frames(info, out_dir):
    dumps = info["dumps"]
    frames = sorted(dumps.glob("frame_*.png")) or sorted(dumps.glob("frame_*.bmp"))
    chosen = []
    if frames:
        for idx in (0, len(frames) // 2, len(frames) - 1):
            if frames[idx] not in chosen:
                chosen.append(frames[idx])
    (out_dir / "shots").mkdir(parents=True, exist_ok=True)
    copied = []
    for p in chosen:
        shutil.copyfile(p, out_dir / "shots" / p.name)
        copied.append(p.name)
    index = [f"# Shots of {info['capture']} (not copied except the frames listed under 'copied')"]
    index.append(f"dump frames in the original run: {len(frames)} ({dumps})")
    index.append("copied into shots/ (first, middle, last):")
    index.extend("  " + n for n in copied) if copied else index.append("  (none)")
    flow = sorted((info["capture"] / "shots").rglob("*.png"))
    index.append(f"macOS window screenshots in the original run: {len(flow)}")
    for p in flow:
        index.append("  " + str(p))
    return "\n".join(index) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description="Package a capture run into the common run layout.")
    ap.add_argument("capture_dir")
    ap.add_argument("--out", help="write exactly here instead of <base>/<UTC>-stock-<id>")
    ap.add_argument("--base", default=None, help="parent folder (default local/runs)")
    ap.add_argument("--stopped-at", type=float, default=None, help="epoch seconds when the run was stopped (stop.sh passes it)")
    args = ap.parse_args(argv)
    capture = pathlib.Path(args.capture_dir)
    if not capture.is_dir():
        print(f"finish_run: no capture folder at {capture}", file=sys.stderr)
        return 1
    capture = capture.resolve()
    root = pathlib.Path(__file__).resolve().parents[3]
    start = local_start(capture)
    start_utc = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(start))
    run_id = f"{time.strftime('%Y%m%dT%H%M%SZ', time.gmtime(start))}-stock-{hashlib.sha1(capture.name.encode()).hexdigest()[:6]}"
    out_dir = pathlib.Path(args.out) if args.out else pathlib.Path(args.base or root / "local/runs") / run_id
    out_dir = out_dir.resolve()
    if out_dir == capture or capture in out_dir.parents or out_dir in capture.parents:
        print("finish_run: the output folder must be outside the capture folder", file=sys.stderr)
        return 1
    if out_dir == root / "ref" or (root / "ref") in out_dir.parents:
        print("finish_run: refusing to write under ref/", file=sys.stderr)
        return 1
    if args.out:
        try:
            out_dir.mkdir(parents=True, exist_ok=False)
        except FileExistsError:
            print(f"finish_run: output already exists: {out_dir}", file=sys.stderr)
            return 1
    else:
        base_dir = out_dir
        for suffix in range(1, 10001):
            candidate = base_dir if suffix == 1 else base_dir.with_name(f"{base_dir.name}-{suffix}")
            try:
                candidate.mkdir(parents=True, exist_ok=False)
                out_dir = candidate
                break
            except FileExistsError:
                continue
        else:
            print("finish_run: no unused output name available", file=sys.stderr)
            return 1
    info = package(capture, out_dir, args.stopped_at)
    (out_dir / "meta.txt").write_text(build_meta(info, capture, run_id, start_utc))
    (out_dir / "run.log").write_text(build_run_log(info))
    (out_dir / "script.txt").write_text(build_script_txt(info, capture))
    input_txt = build_input_txt(info)
    if input_txt:
        (out_dir / "input.txt").write_text(input_txt)
    (out_dir / "counters.txt").write_text(build_counters(info))
    (out_dir / "result.txt").write_text(build_result(info))
    (out_dir / "shots.txt").write_text(copy_frames(info, out_dir))
    print(out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
