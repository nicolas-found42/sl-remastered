#!/usr/bin/env python3
"""Replay passive foreground evidence. No UI, signals, or OS mutation."""
import json
import math
import sys
import argparse
from pathlib import Path


def read_events(path):
    def pairs(items):
        result = {}
        for name, value in items:
            if name in result:
                raise ValueError('duplicate observer key: ' + name)
            result[name] = value
        return result
    def constant(value):
        raise ValueError('nonfinite observer constant: ' + value)
    return [json.loads(line, object_pairs_hook=pairs, parse_constant=constant)
            for line in Path(path).read_text().splitlines() if line.strip()]


def key(identity):
    if not isinstance(identity, dict) or identity.get('read_ok') is not True:
        return None
    values = tuple(identity.get(k) for k in ('pid', 'start_sec', 'start_usec'))
    if (any(type(v) is not int for v in values) or values[0] <= 0 or
            values[1] < 0 or not 0 <= values[2] < 1000000):
        return None
    return values


def classify(events, max_gap=0.25):
    owned = set()
    violations, unknown, legitimate_switches = [], [], []
    finishes = [e for e in events if e['kind'] == 'observer_finished']
    roots = [e for e in events if e['kind'] == 'child_started']
    starts = [e for e in events if e['kind'] == 'observer_ready']
    kinds = {'observer_ready', 'child_started', 'observer_finished', 'foreground',
             'owned_registered', 'launch_failed', 'orphaned_group'}
    previous = -math.inf
    for n, event in enumerate(events):
        if (type(event.get('schema')) is not int or event['schema'] != 1 or
                type(event.get('sequence')) is not int or event['sequence'] != n or
                event.get('kind') not in kinds):
            unknown.append({'reason': 'missing_reordered_or_unknown_record', 'index': n})
        when = event.get('monotonic')
        if type(when) not in (int, float) or not math.isfinite(when) or when < previous:
            unknown.append({'reason': 'invalid_record_time', 'index': n})
        else:
            previous = when
    if len(roots) != 1 or len(starts) != 1 or len(finishes) != 1:
        unknown.append({'reason': 'missing_or_duplicate_lifecycle_record'})
    if not events or events[0].get('kind') != 'observer_ready' or events[-1].get('kind') != 'observer_finished':
        unknown.append({'reason': 'incomplete_observer_lifecycle'})
    requested = starts[0]['requested_executable'] if starts else None
    for e in events:
        if e['kind'] == 'observer_ready':
            k = key(e.get('observer'))
            if k:
                owned.add(k)
            else:
                unknown.append({'reason': 'unreadable_observer_identity'})
        elif e['kind'] == 'child_started':
            root = e.get('root', {})
            k = key(root)
            if (k and type(e.get('spawned_pid')) is int and e['spawned_pid'] == k[0] and
                    root.get('pgid') == k[0] and isinstance(root.get('exe'), str) and root['exe'] and
                    isinstance(requested, str) and Path(requested).is_absolute()):
                owned.add(k)
            else:
                unknown.append({'reason': 'unreadable_or_unexpected_child_identity', 'record': e})
        elif e['kind'] == 'owned_registered':
            parent, child = e.get('parent', {}), e.get('identity', {})
            p, c = key(parent), key(child)
            if p in owned and c and p[1:] <= c[1:] and child.get('ppid') == p[0]:
                owned.add(c)
            else:
                unknown.append({'reason': 'unverified_ownership_registration', 'record': e})
        elif e['kind'] == 'foreground':
            chain = e.get('chain', [])
            keys = [key(p) for p in chain]
            broken = e.get('end_reason') not in ('terminal',) or not chain or any(k is None for k in keys)
            # Check every captured parent edge: never infer through a reused/ambiguous PID.
            for child, parent in zip(chain, chain[1:]):
                c, p = key(child), key(parent)
                if not c or not p or child.get('ppid') != p[0] or p[1:] > c[1:]:
                    broken = True
            if any(k in owned for k in keys if k):
                # An unreadable later ancestor does not erase a directly known owned identity.
                first_owned = next(i for i, k in enumerate(keys) if k in owned)
                prefix = chain[:first_owned+1]
                prefix_ok = all(key(p) for p in prefix)
                for child, parent in zip(prefix, prefix[1:]):
                    prefix_ok &= child.get('ppid') == parent['pid'] and key(parent)[1:] <= key(child)[1:]
                if prefix_ok:
                    violations.append(e)
                    owned.update(key(p) for p in prefix)
                    continue
            if broken:
                unknown.append({'reason': 'unknown_foreground_ancestry', 'record': e})
            else:
                legitimate_switches.append(e)
    for e in finishes:
        if (e.get('timed_out') is not False or e.get('interrupted_signal', 0) or e.get('orphaned_group') or
                type(e.get('polls')) is not int or e['polls'] < 2 or
                type(e.get('max_poll_gap_seconds')) not in (float, int) or
                not math.isfinite(e['max_poll_gap_seconds']) or
                e['max_poll_gap_seconds'] < 0 or e['max_poll_gap_seconds'] > max_gap):
            unknown.append({'reason': 'observer_coverage_gap', 'record': e})
    if any(e['kind'] in ('launch_failed', 'orphaned_group') for e in events):
        unknown.append({'reason': 'failed_or_orphaned_child'})
    return {
        'status': 'owned_activation_observed' if violations else 'inconclusive' if unknown else 'no_owned_activation_observed',
        'owned_activation_count': len(violations), 'unknown_count': len(unknown),
        'allowed_external_records': len(legitimate_switches),
        'violations': violations, 'unknown': unknown,
        'child_exit': finishes[0].get('child_exit') if finishes else None,
        'limitations': 'Notifications plus sampled foreground state are observational evidence, not a guarantee that every activation or short-lived descendant was observed.'
    }


def selftest():
    def recorded(events):
        return [dict(e, schema=1, sequence=n, monotonic=n * 0.02) for n, e in enumerate(events)]
    def judge(events):
        return classify(recorded(events))
    def identity(pid, start, parent=1, exe='/app'):
        return dict(pid=pid, start_sec=start, start_usec=0, ppid=parent, pgid=pid, exe=exe, read_ok=True)
    observer = identity(90, 10, exe='/observer')
    root = identity(100, 20, parent=90, exe='/test')
    external = identity(200, 5, exe='/user-app')
    base = [dict(kind='observer_ready', observer=observer, requested_executable='/test'), dict(kind='child_started', spawned_pid=100, root=root)]
    finish = dict(kind='observer_finished', child_exit=0, timed_out=False, polls=10, max_poll_gap_seconds=.03)
    def sample(chain, source='didActivate', reason='terminal'):
        return dict(kind='foreground', source=source, chain=chain, end_reason=reason)
    def check(label, middle, expected):
        got = judge(base + middle + [finish])
        assert got['status'] == expected, (label, got)
        return label
    checks = []
    checks.append(check('user switches among unrelated apps', [sample([external]), sample([identity(201, 25)])], 'no_owned_activation_observed'))
    checks.append(check('owned root activates', [sample([root, observer])], 'owned_activation_observed'))
    descendant = identity(101, 21, parent=100, exe='/helper')
    checks.append(check('owned descendant activates', [sample([descendant, root, observer])], 'owned_activation_observed'))
    registered = dict(kind='owned_registered', identity=descendant, parent=root)
    orphan = dict(descendant, ppid=1)
    checks.append(check('registered descendant later reparented', [registered, sample([orphan])], 'owned_activation_observed'))
    checks.append(check('root PID reused by unrelated process', [sample([identity(100, 30)])], 'no_owned_activation_observed'))
    checks.append(check('same executable different process', [sample([identity(202, 30, exe='/test')])], 'no_owned_activation_observed'))
    checks.append(check('unreadable identity is unknown', [sample([dict(pid=203, read_ok=False)], reason='unreadable')], 'inconclusive'))
    checks.append(check('observer self activation is a violation', [sample([observer])], 'owned_activation_observed'))
    checks.append(check('notification catches activation absent from later poll', [sample([root]), sample([external], source='poll_transition')], 'owned_activation_observed'))
    checks.append(check('ancestor birth inconsistent with child', [sample([identity(204, 5, parent=100), root], reason='ancestor_born_after_child')], 'inconclusive'))
    assert judge(base)['status'] == 'inconclusive'
    assert judge(base+[sample([external]), dict(finish, max_poll_gap_seconds=1)])['status'] == 'inconclusive'
    valid = recorded(base + [sample([external]), finish])
    malformed = [dict(e) for e in valid]
    malformed[1]['sequence'] = 9
    assert classify(malformed)['status'] == 'inconclusive'
    malformed = [dict(e) for e in valid]
    malformed[-1]['max_poll_gap_seconds'] = float('nan')
    assert classify(malformed)['status'] == 'inconclusive'
    assert judge(base + [dict(finish, orphaned_group=True)])['status'] == 'inconclusive'
    assert judge(base + [dict(kind='unknown_record'), finish])['status'] == 'inconclusive'
    print(json.dumps(dict(passed=len(checks)+6, cases=checks+[
        'missing final record', 'excessive polling gap', 'missing sequence',
        'nonfinite gap', 'orphaned child', 'unknown record']), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('events', type=Path, nargs='?')
    parser.add_argument('--selftest', action='store_true')
    args = parser.parse_args()
    if args.selftest:
        selftest()
    else:
        if args.events is None:
            parser.error('provide observer events or --selftest')
        try:
            rows = read_events(args.events)
            result = classify(rows)
        except (OSError, ValueError, TypeError, KeyError, AttributeError) as error:
            result = {'status': 'inconclusive', 'reason': str(error)}
        print(json.dumps(result, indent=2))
        raise SystemExit(0 if result['status'] == 'no_owned_activation_observed' else 1)
