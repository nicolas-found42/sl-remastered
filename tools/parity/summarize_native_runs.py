#!/usr/bin/env python3
"""Summarize existing Run records as JSON. Does not launch or modify a Run.

Checks executable, data, fixture and recorded environment identities. Inspect
native logs, display configuration and Cartrace separately before interpreting
these timings as equivalent outcomes. Missing timings remain explicit.
"""
import argparse
import json
from pathlib import Path
import statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('outputs', nargs='+', type=Path)
    args = parser.parse_args()
    samples, identities, scripts = [], [], {}
    try:
        for out in args.outputs:
            metadata = json.loads((out / 'metadata.json').read_text())
            result = json.loads((out / 'result.json').read_text())
            fixture = json.loads((out / 'initial-save/manifest.json').read_text())
            if metadata['game_data']['status'] != 'complete':
                raise ValueError(f'{out}: incomplete game-data identity')
            identities.append({'probe': metadata['probe_sha256'],
                               'data': metadata['game_data']['sha256'],
                               'fixture': fixture['files'],
                               'trace': metadata['trace'],
                               'presentation': metadata.get('presentation', 'visible'),
                               'environment': metadata['environment']})
            mode = metadata['mode']
            script = metadata.get('script_sha256')
            if mode in scripts and scripts[mode] != script:
                raise ValueError(f'{out}: differing scripts within {mode}')
            scripts[mode] = script
            samples.append({'output': str(out.resolve()), 'mode': mode,
                            'exit_code': result['exit_code'],
                            'timed_out': result['timed_out'],
                            'source_save_unchanged': result['source_save_unchanged'],
                            'timing': result['timing']})
        if any(identity != identities[0] for identity in identities[1:]):
            raise ValueError('Run identities differ; compare only equivalent configurations')
        summary = {}
        for mode in sorted(scripts):
            group = [s for s in samples if s['mode'] == mode]
            summary[mode] = {}
            for key in ('preparation_seconds', 'menu_seconds', 'driving_seconds', 'total_seconds'):
                values = [s['timing'][key] for s in group if s['timing'][key] is not None]
                summary[mode][key] = {'count': len(values),
                                     'median': statistics.median(values) if values else None,
                                     'min': min(values) if values else None,
                                     'max': max(values) if values else None}
        failures = sum(s['exit_code'] != 0 or s['timed_out'] or
                       not s['source_save_unchanged'] for s in samples)
        print(json.dumps({'samples': samples, 'failures': failures,
                          'identity': identities[0], 'summary': summary}, indent=2))
        return 1 if failures else 0
    except (OSError, ValueError, KeyError) as error:
        parser.exit(2, f'summarize_native_runs: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
