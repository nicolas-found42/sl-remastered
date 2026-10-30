#!/usr/bin/env python3
"""Replay the recorded input of a native run and compare the outcome (see docs/run-log.md).

Usage: tools/replay.sh <run-dir | latest> [--same-window]
Rebuilds the original command with recorded input and an isolated writable save copy.
Reports whether the diagnosis has the same cause; this is not deterministic replay.
"""
import hashlib
import math
import os
import pathlib
import re
import shutil
import stat
import tempfile
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import diagnose  # noqa: E402


def sha256(path):
    try:
        digest = hashlib.sha256()
        with open(path, "rb") as handle:
            for chunk in iter(lambda: handle.read(1 << 20), b""):
                digest.update(chunk)
        return digest.hexdigest()
    except OSError:
        return None


REPLAY_VERBS = {'move', 'down', 'up', 'keydown', 'keyup', 'sdl_move', 'sdl_down', 'sdl_up',
                'sdl_keydown', 'sdl_keyup', 'shot', 'quit', 'fail'}


def replayable_count(path):
    if not path.is_file():
        return 0
    count = 0
    for line in path.read_text(errors="replace").splitlines():
        fields = line.split()
        if len(fields) < 2 or fields[0].startswith('#') or fields[1] not in REPLAY_VERBS:
            continue
        try:
            at = float(fields[0])
        except ValueError:
            continue
        if not math.isfinite(at) or at < 0:
            continue
        verb = fields[1]
        if verb in ('move', 'sdl_move'):
            if len(fields) < 4:
                continue
            try:
                coordinates = (float(fields[2]), float(fields[3]))
            except ValueError:
                continue
            if not all(math.isfinite(value) for value in coordinates):
                continue
        if verb in ('keydown', 'keyup', 'sdl_keydown', 'sdl_keyup', 'shot') and len(fields) < 3:
            continue
        if verb == 'fail' and (len(fields) < 3 or fields[2] not in ('error', 'segv', 'abort', 'throw')):
            continue
        count += 1
    return count


def select_input(original):
    physical, scripted = original / 'input.txt', original / 'script.txt'
    physical_count, scripted_count = replayable_count(physical), replayable_count(scripted)
    if physical_count:
        warning = ('both recorded physical input and a scripted input file contain actions; '
                   'replay uses physical input only and omits the script') if scripted_count else None
        return physical.resolve(), physical_count, 'recorded physical input', warning
    if scripted_count:
        return scripted.resolve(), scripted_count, 'recorded script', None
    raise ValueError('no replayable input or script actions were recorded')


def rebuild_command(meta, input_path, data_root):
    numbered = sorted(((int(m[1]), v) for k, v in meta.items() if (m := re.fullmatch(r"arg\[(\d+)\]", k))))
    if not numbered:
        return None
    args = [v for _, v in numbered]
    out, i, replaced = [args[0]], 1, False
    while i < len(args):
        if args[i] in ("--script", "--data-root"):
            if i + 1 >= len(args) or args[i + 1].startswith("--"):
                raise ValueError(f"missing argument for {args[i]}")
            if args[i] == "--script":
                if not replaced:
                    out += ["--script", str(input_path)]
                    replaced = True
            i += 2
        else:
            out.append(args[i])
            i += 1
    if not replaced:
        out += ["--script", str(input_path)]
    out += ["--data-root", str(data_root)]
    return out


MAX_SAVE_BYTES = 16 * 1024 * 1024
MAX_SAVE_ENTRIES = 2048
REPLAY_BASE = pathlib.Path(__file__).resolve().parent.parent / "local" / "replays"
REF = pathlib.Path(__file__).resolve().parent.parent / "ref"


def save_files(source):
    source = pathlib.Path(source)
    if source.is_symlink() or not source.is_dir():
        raise ValueError(f"save directory is missing or a symlink: {source}")
    files, total, entries = {}, 0, 0
    for directory, dirs, names in os.walk(source, followlinks=False, onerror=lambda e: (_ for _ in ()).throw(e)):
        for name in sorted(dirs + names):
            path = pathlib.Path(directory) / name
            entries += 1
            if entries > MAX_SAVE_ENTRIES:
                raise ValueError("save has too many entries")
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                raise ValueError(f"save contains a symlink: {path}")
            if stat.S_ISREG(mode):
                relative = path.relative_to(source).as_posix()
                if any(c in relative for c in "\r\n\t"):
                    raise ValueError("save has an unsupported filename")
                size = path.stat().st_size
                total += size
                if total > MAX_SAVE_BYTES:
                    raise ValueError("save exceeds 16 MiB")
                digest = sha256(path)
                if digest is None:
                    raise ValueError(f"cannot read save file: {path}")
                files[relative] = digest
            elif not stat.S_ISDIR(mode):
                raise ValueError(f"save contains a special file: {path}")
    return files, total


def recorded_manifest(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError("recorded save manifest is missing or a symlink")
    lines = path.read_text().splitlines()
    if len(lines) < 3 or lines[0] != "format=1":
        raise ValueError("invalid recorded save manifest")
    try:
        count = int(lines[1].removeprefix("files=")) if lines[1].startswith("files=") else -1
        size = int(lines[2].removeprefix("bytes=")) if lines[2].startswith("bytes=") else -1
    except ValueError:
        raise ValueError("invalid recorded save manifest counts") from None
    files = {}
    for line in lines[3:]:
        digest, sep, name = line.partition("  ")
        path_parts = pathlib.PurePosixPath(name)
        if (not sep or not re.fullmatch(r"[0-9a-f]{64}", digest) or not name or
                path_parts.is_absolute() or any(p in (".", "..") for p in path_parts.parts) or
                name in files):
            raise ValueError("invalid recorded save manifest entry")
        files[name] = digest
    if count != len(files) or not 0 <= size <= MAX_SAVE_BYTES:
        raise ValueError("recorded save manifest counts disagree")
    return files, size


def prepare_replay_save(original, meta, cwd, replay_base=None):
    snapshot = original / "initial-save"
    manifest = original / "initial-save-manifest.txt"
    captured = meta.get("initial_save_snapshot") == "captured"
    has_snapshot = snapshot.exists() or snapshot.is_symlink() or manifest.exists() or manifest.is_symlink()
    if captured or has_snapshot:
        expected, expected_size = recorded_manifest(manifest)
        source = snapshot
        before, size = save_files(source)
        if before != expected or size != expected_size:
            raise ValueError("recorded initial save does not match its manifest")
        matched = True
    else:
        data = meta.get("data_root", "")
        if not data:
            raise ValueError("historical run has no data root or initial save")
        source = cwd / data / "save"
        before, size = save_files(source)
        matched = False
    replay_base = pathlib.Path(replay_base if replay_base is not None else REPLAY_BASE)
    resolved_base = replay_base.resolve()
    ref = REF.resolve()
    if resolved_base == ref or ref in resolved_base.parents:
        raise ValueError("replay data root must be outside ref")
    replay_base.mkdir(parents=True, exist_ok=True)
    data_root = pathlib.Path(tempfile.mkdtemp(prefix="replay-", dir=replay_base))
    try:
        if source.resolve() in data_root.resolve().parents:
            raise ValueError("replay data root must be outside the source save")
        shutil.copytree(source, data_root / "save", symlinks=False)
        copied, copied_size = save_files(data_root / "save")
        after, after_size = save_files(source)
        if copied != before or copied_size != size or after != before or after_size != size:
            raise ValueError("save changed during replay copy")
        (data_root / "replay-source.txt").write_text(
            f"source={source.resolve()}\ninitial_state={'recorded' if matched else 'current-unmatched'}\n")
    except (OSError, ValueError):
        shutil.rmtree(data_root)
        raise
    return data_root, matched


def warnings(meta, cwd, matched_save=False):
    problems = []
    exe = meta.get("executable", "")
    recorded = meta.get("exe_sha256", "unknown")
    now = sha256(exe) if exe else None
    if recorded == "unknown" or now is None:
        problems.append(f"cannot compare the program with the original (recorded {recorded}); the code may differ")
    elif now != recorded:
        problems.append(f"the program changed since the original run (exe_sha256 was {recorded[:12]}, is {now[:12]}); "
                        "a different result may be the new code, not the same bug")
    for key in (("game_root",) if matched_save else ("game_root", "data_root")):
        value = meta.get(key, "")
        if value and not (cwd / value).is_dir():
            problems.append(f"{key} {value} is missing now")
    data = meta.get("data_root", "")
    recorded_md5 = meta.get("options_md5", "none")
    if not matched_save and data and recorded_md5 not in ("none", "unknown"):
        options = cwd / data / "save" / "game" / "options"
        try:
            now_md5 = hashlib.md5(options.read_bytes()).hexdigest()
        except OSError:
            now_md5 = "missing"
        if now_md5 != recorded_md5:
            problems.append(f"the options file differs from the original run (md5 was {recorded_md5}, is {now_md5}); "
                            "the game may start differently")
    return problems


def cause_key(run):
    status, cause = diagnose.native_summary(run)
    return f"{status}: {diagnose.normalize_cause(cause)}"


def run_dirs(base):
    return {p.name for p in base.glob("*") if p.is_dir() and not p.is_symlink()} if base.is_dir() else set()


def find_replay_run(base, before, pid, identity):
    if (not isinstance(identity, tuple) or len(identity) < 2 or
            not isinstance(identity[0], int) or not isinstance(identity[1], int) or
            identity[0] <= 0 or not 0 <= identity[1] < 1000000):
        raise ValueError("cannot verify replay process start time")
    expected = f"{identity[0]}.{identity[1]:06d}"
    candidates = []
    for name in sorted(run_dirs(base) - before):
        candidate = base / name
        meta = diagnose.read_meta(candidate / "meta.txt") or {}
        if meta.get("pid") == str(pid) and meta.get("process_start_unix") == expected:
            candidates.append(candidate)
    if len(candidates) != 1:
        raise ValueError(f"cannot identify replay run uniquely (found {len(candidates)} matching process identities)")
    return candidates[0]


def main():
    args = [a for a in sys.argv[1:] if a != "--same-window"]
    if len(args) != 1 or args[0] in ("-h", "--help"):
        print(__doc__.strip().splitlines()[2])
        return 0 if args and args[0] in ("-h", "--help") else 2
    original = diagnose.resolve(args[0])
    if not original.is_dir():
        print(f"replay: no run directory at {original}. Try 'tools/diagnose.sh list'.", file=sys.stderr)
        return 1
    meta = diagnose.read_meta(original / "meta.txt") or {}
    if meta.get("kind") == "stock-wine":
        print("replay: this is a stock Wine run; replay supports native runs only.", file=sys.stderr)
        return 2
    try:
        input_path, recorded_lines, input_kind, input_warning = select_input(original)
    except (OSError, ValueError) as error:
        print(f"replay: {error}", file=sys.stderr)
        return 1
    if not any(re.fullmatch(r"arg\[\d+\]", key) for key in meta):
        print(f"replay: {original}/meta.txt has no arg[N] lines (it ran before replay support existed).", file=sys.stderr)
        return 1
    cwd = pathlib.Path(meta.get("cwd", "")) if meta.get("cwd") and pathlib.Path(meta["cwd"]).is_dir() else pathlib.Path.cwd()
    try:
        data_root, matched_save = prepare_replay_save(original, meta, cwd)
        command = rebuild_command(meta, input_path, data_root)
    except (OSError, ValueError) as error:
        print(f"replay: cannot prepare isolated save: {error}", file=sys.stderr)
        return 1
    command[0] = meta.get("executable") or command[0]
    print(f"replay data root: {data_root}")
    if not matched_save:
        print("WARNING: this historical run has no initial save snapshot; the copied current save may not match")

    for problem in warnings(meta, cwd, matched_save):
        print(f"WARNING: {problem}")
    if input_warning:
        print(f"WARNING: {input_warning}")
    print(f"replaying {recorded_lines} {input_kind} actions from {input_path}")
    print("note: replay is time-based, not frame-exact; a failure that depends on exact frame timing may not repeat")
    print("command: " + " ".join(repr(c) if " " in c else c for c in command))
    sys.stdout.flush()

    base = diagnose.run_base()
    if not base.is_absolute():
        base = cwd / base
    before = run_dirs(base)
    result = diagnose.read_kv(original / "result.txt") or {}
    try:
        limit = min(600.0, max(120.0, float(result.get("seconds", 0)) * 1.5 + 30))
    except ValueError:
        limit = 120.0
    env = dict(os.environ, SLRR_RUN_DIR=str(base))
    # Use the seed that actually reached Math.randomize, not the caller's environment.
    seeds = re.findall(r"random seed: (?:elapsed_ms=[^ ]+ |forced )seed=(\d+)",
                       (original / "run.log").read_text(errors="replace") if (original / "run.log").exists() else "")
    env.pop("SLRR_FORCE_SEED", None)
    if seeds and len(set(seeds)) == 1 and int(seeds[0]) <= 0xffffffff:
        env["SLRR_FORCE_SEED"] = seeds[0]
        print(f"replay seed: {seeds[0]}", flush=True)
    else:
        print("WARNING: no single recorded seed is available; random choices may differ", flush=True)
    try:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
    except OSError as error:
        print(f"replay: cannot start {command[0]}: {error}", file=sys.stderr)
        return 1
    identity = diagnose.read_process_identity(process.pid)
    try:
        process.wait(timeout=limit)
    except subprocess.TimeoutExpired:
        try:
            process.kill()
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            print("replay: the process did not exit after termination was requested", file=sys.stderr)
            return 1
        print(f"replay: the replayed run did not finish within {limit:.0f} s and was stopped")
    try:
        replay = find_replay_run(base, before, process.pid, identity)
    except ValueError as error:
        print(f"replay: {error}", file=sys.stderr)
        return 1
    status = diagnose.native_summary(replay)[0]
    if status == "crashed" and not os.environ.get("SLRR_CRASH_REPORT_DIR"):
        meta_new, res_new = diagnose.read_meta(replay / "meta.txt") or {}, diagnose.read_kv(replay / "result.txt")
        for _ in range(15):
            if diagnose.find_crash_report(replay, meta_new, res_new)[1]:
                break
            time.sleep(1)
    print()
    print(f"=== diagnosis of the replay run {replay} ===")
    print(diagnose.diagnose(replay))
    print()
    first, second = cause_key(original), cause_key(replay)
    if first == second and second.split(":", 1)[0] in ("failed", "crashed", "terminated"):
        print(f"SAME REPORTED CAUSE: {second} (this does not establish deterministic replay)")
        return 0
    print(f"NOT REPRODUCED: original {first} / replay {second}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
