#!/usr/bin/env python3
"""Exercise the native audit through its command-line interface."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "tools/native_audit.py"
KEY = {"class": "java.Example", "method": "run", "descriptor": "()I", "handler": "00401234"}
MD5 = "e686099e7233bb7d7c461b1b35c5064c"


class AuditTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.paths = {name: self.root / name for name in ("registry", "called", "evidence", "implementation-program", "test-program")}
        self.write_tsv("registry", [dict(KEY, status="matched")])
        self.write_tsv("called", [dict(KEY, bucket="called")])
        self.write_json("evidence", [dict(KEY, stock_md5=MD5, source="Ghidra function 00401234")])
        self.write_program("implementation-program", [KEY])
        self.write_program("test-program", [dict(KEY, coverage="vm-dispatch")])

    def write_tsv(self, name, rows):
        with self.paths[name].open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]), delimiter="\t")
            writer.writeheader()
            writer.writerows(rows)

    def write_json(self, name, rows):
        self.paths[name].write_text(json.dumps(rows))

    def write_program(self, name, rows, exit_code=0):
        program = self.paths[name]
        program.write_text(f"#!{sys.executable}\nimport sys\nprint({json.dumps(json.dumps(rows))})\nsys.exit({exit_code})\n")
        program.chmod(0o700)

    def run_audit(self, *extra):
        args = [sys.executable, "-I", str(SCRIPT)]
        for name, path in self.paths.items():
            args.extend(["--" + name, str(path)])
        return subprocess.run(args + list(extra), capture_output=True, text=True)

    def run_audit_with_tests(self, *programs):
        args = [sys.executable, "-I", str(SCRIPT)]
        for name in ("registry", "called", "evidence", "implementation-program"):
            args.extend(["--" + name, str(self.paths[name])])
        for program in programs:
            args.extend(["--test-program", str(program)])
        return subprocess.run(args, capture_output=True, text=True)

    def test_successful_programs_authorize_a_complete_fixture(self):
        result = self.run_audit("--require-complete")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_no_matched_claim_needs_no_inventories(self):
        self.write_tsv("registry", [dict(KEY, status="unread")])
        for name in ("evidence", "implementation-program", "test-program"):
            self.paths[name].unlink()
        self.assertEqual(self.run_audit().returncode, 0)
        self.assertNotEqual(self.run_audit("--require-complete").returncode, 0)

    def test_missing_programs_and_evidence_fail(self):
        for name in ("evidence", "implementation-program", "test-program"):
            with self.subTest(name=name):
                path = self.paths[name]
                original = path.read_bytes()
                path.unlink()
                self.assertNotEqual(self.run_audit().returncode, 0)
                path.write_bytes(original)
                path.chmod(0o700)

    def test_failed_test_cannot_report_success(self):
        self.write_program("test-program", [dict(KEY, coverage="vm-dispatch")], exit_code=1)
        result = self.run_audit()
        self.assertIn("executed test program failed", result.stderr)

    def test_inventory_identity_must_match(self):
        for name in ("implementation-program", "test-program"):
            for field, wrong in (("class", "java.Other"), ("method", "other"), ("descriptor", "()V"), ("handler", "00401235")):
                with self.subTest(name=name, field=field):
                    self.write_program(name, [dict(KEY, **{field: wrong}, **({"coverage": "vm-dispatch"} if name == "test-program" else {}))])
                    result = self.run_audit()
                    self.assertIn("inventory missing matched", result.stderr)
                    self.write_program(name, [dict(KEY, **({"coverage": "vm-dispatch"} if name == "test-program" else {}))])

    def test_test_inventories_are_unioned(self):
        other = dict(KEY, method="other", handler="00401240")
        self.write_tsv("registry", [dict(KEY, status="matched"), dict(other, status="matched")])
        self.write_json("evidence", [dict(KEY, stock_md5=MD5, source="a"), dict(other, stock_md5=MD5, source="b")])
        self.write_program("implementation-program", [KEY, other])
        self.write_program("test-program", [dict(KEY, coverage="vm-dispatch")])
        second = self.root / "second-program"
        second.write_text(f"#!{sys.executable}\nimport json\nprint(json.dumps([{json.dumps(dict(other, coverage='vm-dispatch'))}]))\n")
        second.chmod(0o700)
        self.assertIn("inventory missing matched", self.run_audit_with_tests(self.paths["test-program"]).stderr)
        self.assertIn("inventory missing matched", self.run_audit_with_tests(second).stderr)
        self.assertEqual(self.run_audit_with_tests(self.paths["test-program"], second).returncode, 0)

    def test_every_given_test_program_must_succeed(self):
        failing = self.root / "failing-program"
        failing.write_text(f"#!{sys.executable}\nimport sys\nprint('[]')\nsys.exit(1)\n")
        failing.chmod(0o700)
        result = self.run_audit_with_tests(self.paths["test-program"], failing)
        self.assertIn("executed test program failed", result.stderr)

    def test_direct_registry_test_is_not_vm_dispatch_evidence(self):
        self.write_program("test-program", [KEY])
        self.assertIn("must identify VM dispatch coverage", self.run_audit().stderr)

    def test_stale_json_file_is_not_an_executable_inventory(self):
        self.write_json("test-program", [KEY])
        self.assertNotEqual(self.run_audit().returncode, 0)
        result = self.run_audit("--tests", str(self.paths["test-program"]))
        self.assertNotEqual(result.returncode, 0)

    def test_wrong_stock_and_empty_source_fail(self):
        for change in ({"stock_md5": "0" * 32}, {"source": ""}):
            self.write_json("evidence", [dict(dict(KEY, stock_md5=MD5, source="read"), **change)])
            self.assertIn("evidence must", self.run_audit().stderr)

    def test_duplicate_registry_and_invalid_status_fail(self):
        self.write_tsv("registry", [dict(KEY, status="matched"), dict(KEY, status="matched")])
        self.assertNotEqual(self.run_audit().returncode, 0)
        self.write_tsv("registry", [dict(KEY, status="matchd")])
        self.assertNotEqual(self.run_audit().returncode, 0)


if __name__ == "__main__":
    unittest.main()
