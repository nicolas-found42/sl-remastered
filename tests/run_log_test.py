#!/usr/bin/env python3
"""Run startup_probe with deliberate failures and check that tools/diagnose.sh explains each run directory.

Usage: python3 -I tests/run_log_test.py [path/to/startup_probe]   (default build/startup_probe)
Needs the 2005 extract and a window server. Missing prerequisites fail the
check so a successful project check cannot hide untested crash diagnostics.
"""
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import time

PROBE = sys.argv[1] if len(sys.argv) > 1 else "build/startup_probe"
GAME = pathlib.Path(".scratch/2005-extract/Program Files/Street Legal Racing - Redline")
LOOP_READY = "Init constructor tree completed"


def skip(why):
    print(f"run log integration test NOT RUN: {why}", file=sys.stderr)
    sys.exit(1)


def diagnose(arg):
    done = subprocess.run(["tools/diagnose.sh", str(arg)], capture_output=True, text=True)
    assert done.returncode == 0, f"diagnose failed on {arg}:\n{done.stdout}\n{done.stderr}"
    return done.stdout


def expect(output, *needles, label):
    for needle in needles:
        assert needle in output, f"[{label}] missing {needle!r} in diagnose output:\n{output}"


def start(tmp, name, script_text):
    base = tmp / f"runs-{name}"
    script = tmp / f"{name}.txt"
    script.write_text(script_text)
    env = dict(os.environ, SLRR_RUN_DIR=str(base))
    process = subprocess.Popen(
        [PROBE, "--init", "--renderer", "--loop", "--background", "--no-intro", "--script", str(script), "--data-root", str(tmp / "data"), str(GAME)],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    return process, base


def run_dir(base):
    runs = [p for p in base.iterdir() if p.is_dir() and not p.is_symlink()]
    assert len(runs) == 1, f"expected one run directory in {base}, found {runs}"
    return runs[0]


def wait_for_loop(base, process, label):
    deadline = time.time() + 60
    while time.time() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"[{label}] probe exited early with {process.returncode}:\n{process.stderr.read()}")
        for log in base.glob("*/run.log"):
            if LOOP_READY in log.read_text(errors="replace"):
                return
        time.sleep(0.1)
    process.kill()
    raise AssertionError(f"[{label}] probe never finished Init")


def check_failure(tmp, kind, status_word, needles):
    process, base = start(tmp, kind, f"1 move 100 100\n2 fail {kind}\n")
    try:
        _, stderr = process.communicate(timeout=90)
    except subprocess.TimeoutExpired:
        process.kill()
        raise AssertionError(f"fail {kind} did not end")
    if "physical input unavailable" in stderr:
        skip("no window server or input devices for the probe")
    run = run_dir(base)
    out = diagnose(run)
    expect(out, f"VERDICT: {status_word}", *needles, "WHAT RAN", "FILES", "fail " + kind, label=kind)
    assert (run / "meta.txt").is_file() and (run / "script.txt").is_file(), f"[{kind}] meta.txt or script.txt missing"
    return run, out


def main():
    if not GAME.is_dir() or not pathlib.Path(PROBE).is_file():
        skip(f"missing {GAME} or {PROBE}")
    with tempfile.TemporaryDirectory(prefix="sl-run-log-") as name:
        tmp = pathlib.Path(name)
        (tmp / "data/save/game").mkdir(parents=True)
        shutil.copyfile(GAME / "save/game/options", tmp / "data/save/game/options")

        run, _ = check_failure(tmp, "error", "FAILED", ["exit code 1", "startup probe: injected failure"])
        log = (run / "run.log").read_text()
        assert "--- last events ---" in log and "] err startup probe: injected failure" in log, "run.log lacks failure record"
        assert not (run / "crash.txt").exists(), "a normal failure must not leave crash.txt"
        assert "reason=startup probe: injected failure" in (run / "result.txt").read_text()
        assert (run.parent / "latest").is_symlink(), "latest symlink missing"

        run, out = check_failure(tmp, "segv", "CRASHED", ["SIGSEGV", "top of the call stack"])
        assert (run / "crash.txt").is_file() and "status=crashed" in (run / "result.txt").read_text()
        run, _ = check_failure(tmp, "abort", "CRASHED", ["SIGABRT"])
        run, _ = check_failure(tmp, "throw", "CRASHED", ["SIGABRT", "uncaught exception: injected failure"])

        process, base = start(tmp, "term", "60 quit\n")
        wait_for_loop(base, process, "term")
        time.sleep(1.0)
        process.send_signal(signal.SIGTERM)
        process.communicate(timeout=30)
        assert process.returncode == 128 + signal.SIGTERM, f"SIGTERM run exited {process.returncode}"
        out = diagnose(run_dir(base))
        expect(out, "VERDICT: TERMINATED", "SIGTERM", "Stopped by SIGTERM", "TIMELINE", label="term")

        process, base = start(tmp, "kill", "60 quit\n")
        wait_for_loop(base, process, "kill")
        time.sleep(6.5)
        run = run_dir(base)
        checkpoints = list(run.glob("flight-[01].log"))
        assert checkpoints, "[kill] no completed flight checkpoint after heartbeat"
        checkpoint = checkpoints[0].read_text()
        assert "checkpoint=" in checkpoint and "nativecall " in checkpoint, "[kill] checkpoint lacks recent native events"
        process.send_signal(signal.SIGSTOP)
        time.sleep(0.2)
        assert checkpoints[0].read_text() == checkpoint, "[kill] stopped process changed its checkpoint"
        process.kill()
        process.communicate(timeout=30)
        assert checkpoints[0].read_text() == checkpoint, "[kill] completed checkpoint was not preserved"
        out = diagnose(run)
        expect(out, "no result recorded", "recorded process not running", "exit cause unknown", "last heartbeat: [+", " heartbeat frame=", label="kill")

        partial = tmp / "partial"
        partial.mkdir()
        (partial / "meta.txt").write_text("argv: old format\n")
        expect(diagnose(partial), "no result recorded", "killed", label="partial")
        empty = tmp / "empty"
        empty.mkdir()
        expect(diagnose(empty), "no result recorded", label="empty")

        listing = subprocess.run(["tools/diagnose.sh", "list"], capture_output=True, text=True,
                                 env=dict(os.environ, SLRR_RUN_DIR=str(tmp / "runs-error")))
        assert listing.returncode == 0 and "failed" in listing.stdout and "injected failure" in listing.stdout, listing.stdout
    print("run log integration: fail error, segv, abort, throw, SIGTERM, SIGKILL and partial runs all diagnosed")


main()
