"""Check source hooks needed by the current private native adapter.

Read-only preflight, not a build, licence determination or binary validator.
It reads only the supplied framework's four runtime source files.
"""
import argparse
import json
from pathlib import Path

HOOKS = [
    ('game settings callbacks', 'runtime.h', ['ui_extra_items', 'ui_get_text', 'ui_set_text']),
    ('quiescent frame sink', 'host_window.h', ['set_frame_sink', 'FrameSink']),
    ('camera input ownership', 'runtime.cpp', ['vr::game_input::filter']),
    ('developer checkpoint dispatch', 'runtime.cpp', ['vr::dev::take_request', 'vr::dev::complete']),
    ('developer pacing and frame step', 'runtime.cpp', ['vr::dev::pace', 'vr::dev::next_frame']),
    ('viewer panel event routing', 'host_window.cpp', ['vr::dev::viewer_event']),
]

def inspect(root):
    source = root / 'src/runtime'
    contents = {}
    for _, name, _ in HOOKS:
        path = source / name
        if name not in contents:
            contents[name] = path.read_text(encoding='utf-8', errors='replace') if path.is_file() else ''
    checks = [dict(hook=label, file='src/runtime/'+name,
                   markers_present=all(marker in contents[name] for marker in markers))
              for label, name, markers in HOOKS]
    return dict(checks=checks, source_hooks_present=all(c['markers_present'] for c in checks),
                scope='Source markers only. Build/link, native integration, user inputs and distribution terms still require verification.')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--framework', type=Path, required=True)
    parser.add_argument('--json', action='store_true', help='Print the report as JSON')
    args = parser.parse_args()
    report = inspect(args.framework)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        for c in report['checks']:
            print(('FOUND   ' if c['markers_present'] else 'MISSING ') + c['hook'])
        print(report['scope'])
    return 0 if report['source_hooks_present'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
