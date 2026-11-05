#!/usr/bin/env python3
"""Read bounded local incident records; never launch targets or infer parity.

Use --record for a known run directory or JSON result, --root to discover runs,
and --events/--operation op1:ID for a native operation timeline. Output deliberately
omits raw messages, argv, environment, save names and contents. Paths are local
retrieval references, not a shareable/redacted evidence export.
"""
import argparse
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat
import sys
import time

MARKERS = {'meta.txt', 'metadata.json', 'config.json', 'result.json', 'result.txt', 'attempt.json'}
EXCLUDED = {'.git', '.venv', 'ref', 'game', 'data', 'prefix', 'source', 'initial-save', 'node_modules'}
EVENT = re.compile(r'^\[\+(\d+(?:\.\d+)?) f(\d+)\] (\w+) (.*)$')
OPERATIONS = {1: 'file_read', 2: 'file_write_open', 3: 'file_close'}
OUTCOMES = {0: 'unknown', 1: 'ok', 2: 'open_unavailable', 3: 'rejected', 4: 'io_error', 5: 'no_handle'}


def timestamp(value):
    if not isinstance(value, str):
        return None
    try:
        result = dt.datetime.fromisoformat(value.replace('Z', '+00:00'))
        return result.timestamp() if result.tzinfo else None
    except (ValueError, OverflowError):
        return None


def strict_json(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate JSON field')
            result[key] = value
        return result
    def constant(_):
        raise ValueError('nonfinite JSON value')
    def number(value):
        result = float(value)
        if not math.isfinite(result):
            raise ValueError('overflowing JSON number')
        return result
    return json.loads(raw, object_pairs_hook=pairs, parse_constant=constant, parse_float=number)


class Reader:
    def __init__(self, *, max_entries=20000, max_bytes=64 * 1024**2, seconds=5):
        self.max_entries, self.max_bytes = max_entries, max_bytes
        self.deadline = time.monotonic() + seconds
        self.entries = self.bytes = 0
        self.problems = []
        self.bindings = []

    def check(self):
        if time.monotonic() > self.deadline:
            raise ValueError('query deadline reached; inventory incomplete')

    def read(self, path, optional=False):
        self.check()
        try:
            # Discovery never follows links; direct reads also refuse parent aliases.
            if path.resolve(strict=True) != path.absolute():
                raise ValueError('aliased/symlink evidence refused')
            fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            try:
                before = os.fstat(fd)
                if not stat.S_ISREG(before.st_mode):
                    raise ValueError('regular evidence file required')
                if before.st_size > 16 * 1024**2 or self.bytes + before.st_size > self.max_bytes:
                    raise ValueError('query byte limit reached; evidence incomplete')
                raw = bytearray()
                while len(raw) <= before.st_size:
                    self.check()
                    chunk = os.read(fd, min(65536, before.st_size + 1 - len(raw)))
                    if not chunk:
                        break
                    raw.extend(chunk)
                after = os.fstat(fd)
                self.bytes += len(raw)
                if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
                        after.st_size, after.st_mtime_ns, after.st_ctime_ns) or len(raw) != before.st_size:
                    raise ValueError('evidence changed during query')
            finally:
                os.close(fd)
        except FileNotFoundError:
            if optional:
                return None
            raise
        self.bindings.append({'path': str(path), 'sha256': hashlib.sha256(raw).hexdigest(),
                              'bytes': len(raw)})
        return bytes(raw)

    def document(self, path):
        raw = self.read(path, optional=True)
        if raw is None:
            return {}
        value = strict_json(raw)
        if not isinstance(value, dict):
            raise ValueError('JSON object required')
        return value

    def fields(self, path, separator):
        raw = self.read(path, optional=True)
        result = {}
        for line in (raw or b'').decode('utf-8', errors='replace').splitlines():
            if separator in line:
                key, value = line.split(separator, 1)
                if key.strip() in result:
                    if key.strip() in {'start_utc', 'pid', 'process_start_unix', 'exe_sha256',
                                       'src_tree_sha256', 'status', 'seconds', 'run_id'}:
                        raise ValueError('duplicate identity/result field')
                    continue  # repeated stack/argv rows are not identity fields
                result[key.strip()] = value.strip()
        return result

    def discover(self, root):
        if root.resolve(strict=True) != root.absolute() or not root.is_dir():
            raise ValueError('canonical root directory required')
        pending, found = [(root, 0)], set()
        while pending:
            self.check()
            directory, depth = pending.pop()
            with os.scandir(directory) as entries:
                for entry in entries:
                    self.entries += 1
                    if self.entries > self.max_entries:
                        self.problems.append('entry limit reached; discovery incomplete')
                        return sorted(found)
                    self.check()
                    if entry.is_symlink():
                        if entry.name in MARKERS:
                            self.problems.append(f'{entry.path}: symlink marker skipped; evidence incomplete')
                        continue
                    if entry.name in MARKERS and not entry.is_file(follow_symlinks=False):
                        self.problems.append(f'{entry.path}: nonregular marker skipped; evidence incomplete')
                    if entry.is_file(follow_symlinks=False) and entry.name in MARKERS:
                        found.add(directory)
                    elif entry.is_dir(follow_symlinks=False) and entry.name not in EXCLUDED:
                        if depth == 24:
                            self.problems.append('depth limit reached; discovery incomplete')
                        else:
                            pending.append((Path(entry.path), depth + 1))
        return sorted(found)


def safe_identity(value):
    return value if isinstance(value, str) and re.fullmatch(r'[A-Za-z0-9_.:+-]{1,128}', value) else None


def operations(reader, directory, start, since, until, operation):
    raw = reader.read(directory / 'run.log', optional=True)
    if raw is None:
        return [], ['run.log unavailable; operation history unknown']
    observations, issues = {}, []
    for number, line in enumerate(raw.decode('utf-8', errors='replace').splitlines(), 1):
        reader.check()
        match = EVENT.match(line)
        if not match or not match[4].startswith('op1 '):
            continue
        try:
            item = strict_json(match[4][4:])
            if (set(item) != {'v', 'i', 'p', 's', 'k', 'e', 'h', 'r', 'n'} or
                    any(type(value) is not int or not 0 <= value <= 2**64-1 for value in item.values()) or
                    item['v'] != 1 or item['i'] == 0 or item['k'] not in OPERATIONS or
                    item['r'] not in OUTCOMES or item['e'] not in (0, 1) or item['h'] > 2**32-1):
                raise ValueError('invalid operation schema')
        except (ValueError, TypeError, RecursionError):
            issues.append(f'run.log:{number}: malformed op1; history incomplete')
            continue
        key = f"op1:{item['i']}"
        if operation and operation != key:
            continue
        elapsed = float(match[1])
        if not math.isfinite(elapsed):
            issues.append(f'run.log:{number}: invalid event time; history incomplete')
            continue
        wall = start + elapsed if start is not None else None
        row = {'operation': key, 'parent': f"op1:{item['p']}" if item['p'] else None,
               'scope': item['s'], 'kind': OPERATIONS[item['k']],
               'event': 'end' if item['e'] else 'begin', 'outcome': OUTCOMES[item['r']],
               'handle': item['h'], 'bytes_written': item['n'], 'elapsed_seconds': elapsed,
               'frame': int(match[2]), 'utc': dt.datetime.fromtimestamp(wall, dt.timezone.utc).isoformat() if wall is not None else None,
               'source_line': number}
        identity = (key, item['e'])
        if identity in observations:
            previous = observations[identity]
            if any(previous[k] != row[k] for k in row if k != 'source_line'):
                issues.append(f'{key}: conflicting duplicate event; ordering unknown')
            continue  # normal failure tails repeat already persisted events
        observations[identity] = row
    for key in {key for key, _ in observations}:
        begin, end = observations.get((key, 0)), observations.get((key, 1))
        if begin is None or end is None:
            issues.append(f'{key}: missing begin/end; outcome incomplete')
        elif end['elapsed_seconds'] < begin['elapsed_seconds']:
            issues.append(f'{key}: reversed time; ordering unknown')
        elif any(begin[field] != end[field] for field in ('parent', 'scope', 'kind')):
            issues.append(f'{key}: begin/end identity differs; correlation unknown')
        if end and end['outcome'] == 'unknown':
            issues.append(f'{key}: returned/unwound without a known outcome')
    result = list(observations.values())
    if since is not None or until is not None:
        result = [row for row in result if row['utc'] is None or
                  ((since is None or timestamp(row['utc']) >= since) and
                   (until is None or timestamp(row['utc']) <= until))]
    return sorted(result, key=lambda row: (row['elapsed_seconds'], row['source_line'])), issues


def inspect(reader, path, args):
    if not path.exists():
        raise FileNotFoundError(path)
    first_binding = len(reader.bindings)
    direct = path.is_file()
    directory = path.parent if direct else path
    meta = {} if direct else reader.fields(directory / 'meta.txt', ':')
    metadata = {} if direct else reader.document(directory / 'metadata.json')
    config = {} if direct else reader.document(directory / 'config.json')
    attempt = {} if direct else reader.document(directory / 'attempt.json')
    document = reader.document(path if direct else directory / 'result.json')
    result = document
    if isinstance(result.get('result'), dict):
        result = result['result']
    text_result = {} if direct else reader.fields(directory / 'result.txt', '=')
    health = {} if direct else reader.document(directory / 'telemetry.json')
    if attempt and type(result.get('telemetry_failures')) is int:
        health = {'schema': 1, 'phase': 'experiment', 'io_failures': result['telemetry_failures']}
    start_text = meta.get('start_utc') or metadata.get('started_utc') or config.get('started_utc') or attempt.get('started_utc') or document.get('started_utc')
    start = timestamp(start_text)
    status = safe_identity(result.get('status') or text_result.get('status'))
    if status is None and type(result.get('execution_passed')) is bool:
        status = 'execution_passed' if result['execution_passed'] else 'execution_failed'
    problems = []
    if not result and not text_result:
        problems.append('terminal result unavailable; liveness/outcome unknown')
    if start is None:
        problems.append('recorded UTC start unavailable; time-range membership unknown')
    duration = text_result.get('seconds', result.get('seconds'))
    try:
        if type(duration) is bool:
            raise ValueError('boolean duration')
        duration = float(duration)
        if not math.isfinite(duration) or duration < 0:
            duration = None
    except (TypeError, ValueError, OverflowError):
        duration = None
    if start is not None:
        if args.until_value is not None and start > args.until_value:
            return None
        if args.since_value is not None and duration is not None and start + duration < args.since_value:
            return None
    events = []
    if (args.events or args.operation) and not attempt:
        events, event_issues = operations(reader, directory, start, args.since_value, args.until_value, args.operation)
        problems.extend(event_issues)
        if not events:
            problems.append('no matching retained op1 events; absence is not proof of no operation')
    elif args.events or args.operation:
        raw = reader.read(directory / 'stages.jsonl', optional=True)
        states = {}
        for number, line in enumerate((raw or b'').splitlines(), 1):
            reader.check()
            try:
                row = strict_json(line)
                if (row.get('attempt_id') != attempt.get('attempt_id') or
                        type(row.get('schema')) is not int or row.get('schema') != 1):
                    raise ValueError('attempt binding differs')
                if row.get('event') == 'attempt_started':
                    continue
                if (type(row.get('stage')) is not int or not 0 <= row['stage'] < 32 or
                        row.get('event') not in ('begin', 'end', 'interrupted') or
                        timestamp(row.get('utc')) is None or
                        (row.get('event') == 'end' and type(row.get('exit_code')) is not int) or
                        (row.get('event') != 'end' and row.get('exit_code') is not None)):
                    raise ValueError('invalid stage record')
                identity = f"stage:{row['stage']}"
                if args.operation and args.operation != identity:
                    continue
                states.setdefault(identity, []).append(row['event'])
                wall = timestamp(row['utc'])
                if ((args.since_value is None or wall >= args.since_value) and
                        (args.until_value is None or wall <= args.until_value)):
                    events.append({'operation': identity, 'event': row['event'], 'utc': row['utc'],
                                   'exit_code': row.get('exit_code'), 'source_line': number})
            except (ValueError, TypeError, AttributeError, RecursionError):
                problems.append(f'stages.jsonl:{number}: malformed/binding mismatch; history incomplete')
        for identity, sequence in states.items():
            if sequence != ['begin', 'end']:
                problems.append(f'{identity}: incomplete/interrupted/reordered stage history')
        if not raw or not events:
            problems.append('no matching retained stage events; outcome unknown')
    requests = []
    if args.request_id:
        for row in document.get('reviews', []):
            if not isinstance(row, dict) or row.get('request_id') != args.request_id:
                continue
            requests.append({k: row[k] for k in ('request_id', 'created_monotonic',
                'expires_monotonic', 'expired', 'response_consumed', 'response_file_exists',
                'response_mtime_ns', 'image_sha256') if k in row and
                (row[k] is None or type(row[k]) in (bool, int, float) or
                 (k in ('request_id', 'image_sha256') and safe_identity(row[k])))})
        if not requests:
            problems.append('request ID not present in this retained review document')
    if not health:
        problems.append('recorder health unavailable; historical loss/IO status unknown')
    elif (type(health.get('schema')) is not int or health.get('schema') != 1 or
          any(type(health[key]) is not int or health[key] < 0 for key in
              ('dropped_lines', 'dropped_context_lines', 'io_failures') if key in health)):
        problems.append('malformed recorder health; loss/IO status unknown')
    elif type(health.get('dropped_lines')) is int and health['dropped_lines'] > 0:
        problems.append('recorder log budget dropped lines; history incomplete')
    if type(health.get('io_failures')) is int and health['io_failures'] > 0:
        problems.append('recorder observed IO failures; history incomplete')
    if type(health.get('dropped_context_lines')) is int and health['dropped_context_lines'] > 0:
        problems.append('recorder bounded failure context was truncated')
    if health.get('capture_unavailable') == 1:
        problems.append('stdout/stderr capture unavailable; terminal-only output was not retained')
    context = {}
    for key, value in {**attempt, **metadata, **config, **meta}.items():
        if key in {'attempt_id', 'runner_sha256', 'git_head', 'exe_sha256', 'src_tree_sha256', 'build_type', 'probe_sha256', 'image_id', 'presentation'}:
            context[key] = safe_identity(value)
    stage_summary = []
    required, reported = result.get('required_stages'), result.get('stages')
    if isinstance(required, list) and isinstance(reported, dict):
        if len(required) > 32 or any(not isinstance(name, str) for name in required):
            problems.append('invalid declared stage inventory')
        else:
            for index, name in enumerate(required):
                stage = reported.get(name)
                item = {'operation': f'stage:{index}', 'state': 'not_started'}
                if isinstance(stage, dict):
                    item['state'] = 'completed' if stage.get('completed') is True else 'incomplete'
                    for field in ('actual_exit', 'expected_exit'):
                        item[field] = stage.get(field) if type(stage.get(field)) is int else None
                stage_summary.append(item)
    return {'record': str(path), 'run_id': directory.name, 'status': status or 'unknown',
            'start_utc': start_text if start is not None else None,
            'time_match': 'unknown' if start is None else 'possible_overlap',
            'context': context, 'health': {k: health[k] for k in (
                'schema', 'phase', 'elapsed_seconds', 'log_limit_bytes', 'log_bytes_attempted',
                'dropped_lines', 'dropped_context_lines', 'io_failures', 'first_errno', 'checkpoint_sequence',
                'capture_requested', 'capture_unavailable')
                if k in health and (type(health[k]) in (int, float) or
                                    (k == 'phase' and safe_identity(health[k])))},
            'events': events[:args.limit], 'events_omitted': max(0, len(events)-args.limit),
            'requests': requests[:args.limit],
            'stages': stage_summary,
            'problems': problems, 'sources': reader.bindings[first_binding:]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--root', action='append', type=Path)
    selection.add_argument('--record', action='append', type=Path)
    parser.add_argument('--since', help='inclusive ISO8601 time with UTC offset')
    parser.add_argument('--until', help='inclusive ISO8601 time with UTC offset')
    parser.add_argument('--run-id')
    parser.add_argument('--operation', help='process-local identity, e.g. op1:7; pair with --record or --run-id')
    parser.add_argument('--request-id', help='exact 64-hex UI request ID in a retained review JSON document')
    parser.add_argument('--events', action='store_true')
    parser.add_argument('--limit', type=int, default=100)
    parser.add_argument('--max-entries', type=int, default=20000)
    parser.add_argument('--max-bytes', type=int, default=64 * 1024**2)
    parser.add_argument('--seconds', type=float, default=5)
    args = parser.parse_args(argv)
    if (not 1 <= args.limit <= 10000 or args.max_entries <= 0 or args.max_bytes <= 0 or
            not math.isfinite(args.seconds) or not 0 < args.seconds <= 60):
        parser.error('positive bounded query limits required')
    args.since_value, args.until_value = timestamp(args.since), timestamp(args.until)
    if ((args.since and args.since_value is None) or (args.until and args.until_value is None) or
            (args.since_value is not None and args.until_value is not None and args.since_value > args.until_value)):
        parser.error('ordered ISO8601 timestamps with offsets required')
    if args.operation and (not re.fullmatch(r'(op1:[1-9][0-9]*|stage:[0-9]+)', args.operation) or not (args.record or args.run_id)):
        parser.error('--operation needs op1:ID or stage:INDEX and a record or run ID scope')
    if args.request_id and (not re.fullmatch(r'[0-9a-f]{64}', args.request_id) or not args.record):
        parser.error('--request-id needs a 64-hex ID and --record JSON')
    reader = Reader(max_entries=args.max_entries, max_bytes=args.max_bytes, seconds=args.seconds)
    records, paths = [], set()
    for root in args.root or ([] if args.record else [Path('local/runs')]):
        try:
            paths.update(reader.discover(root.absolute()))
        except (OSError, ValueError) as error:
            reader.problems.append(f'{root}: {type(error).__name__}: {error}')
    paths.update(path.absolute() for path in args.record or [])
    matched = 0
    for path in sorted(paths):
        if args.run_id and (path.parent.name if path.is_file() else path.name) != args.run_id:
            continue
        matched += 1
        if len(records) >= args.limit:
            reader.problems.append('record limit reached; selection incomplete')
            break
        try:
            record = inspect(reader, path, args)
            if record:
                records.append(record)
        except (OSError, ValueError, TypeError, RecursionError) as error:
            reader.problems.append(f'{path}: {type(error).__name__}: evidence unavailable/malformed')
    report = {'schema': 1, 'read_only': True, 'inventory_complete': not reader.problems,
              'entries_scanned': reader.entries, 'bytes_read': reader.bytes,
              'matching_paths': matched, 'records': records, 'problems': reader.problems,
              'limits': ['declared records, not causal diagnosis or parity proof',
                         'unknown times retained as unknown; filesystem mtime is not event time',
                         'native UTC is an approximation from second-resolution start plus monotonic offset; clocks may adjust',
                         'raw messages, argv, environment and save contents omitted; paths remain local',
                         'deadline checked between filesystem calls; blocked filesystem IO can exceed it',
                         'discovery excludes game/data/prefix/source/reference trees; no symlink traversal']}
    print(json.dumps(report, indent=2, allow_nan=False))
    return 0 if report['inventory_complete'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
