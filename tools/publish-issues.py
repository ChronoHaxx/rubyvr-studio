"""Preview work-package publication; --apply explicitly writes to GitHub.

Requires authenticated GitHub CLI only for --apply. Creates missing records,
preserves existing conversations and never sends messages to upstream projects.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def api(endpoint, payload=None, pages=False):
    command = ['gh', 'api', endpoint]
    if pages:
        command += ['--paginate', '--slurp']
    if payload is not None:
        command += ['--method', 'POST', '--input', '-']
    result = subprocess.run(command, input=None if payload is None else json.dumps(payload),
                            capture_output=True, text=True, encoding='utf-8', check=True)
    data = json.loads(result.stdout)
    return [row for page in data for row in page] if pages else data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', required=True, help='Exact OWNER/REPO destination; no inferred default.')
    parser.add_argument('--apply', action='store_true', help='Create missing GitHub labels, milestones and issues.')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9_.-]+', args.repo):
        raise SystemExit('Expected an exact OWNER/REPO GitHub destination.')
    catalog = json.loads((ROOT / 'docs/issues/catalog.json').read_text(encoding='utf-8'))
    bodies = {}
    for item in catalog['issues']:
        path = (ROOT / item['body']).resolve()
        if not path.is_relative_to(ROOT / 'docs/issues'):
            raise SystemExit('Issue body escapes the reviewed issue directory.')
        bodies[item['id']] = path.read_text(encoding='utf-8')
    print(f"Destination: {args.repo}; {len(catalog['labels'])} labels, {len(catalog['milestones'])} milestones, {len(catalog['issues'])} issues")
    if not args.apply:
        for item in catalog['issues']:
            print(f"{item['id']}: {item['title']} ({item['body']})")
        print('DRY RUN: no network access or changes. Review the files before using --apply.')
        return
    base = f'repos/{args.repo}'
    repository = api(base)
    if repository['full_name'].lower() != args.repo.lower() or repository.get('archived') or not repository.get('has_issues'):
        raise SystemExit('Destination identity, archive state or issue availability does not match the request.')
    existing_labels = {row['name'] for row in api(base + '/labels?per_page=100', pages=True)}
    milestones = {row['title']: row['number'] for row in api(base + '/milestones?state=all&per_page=100', pages=True)}
    issues = [row for row in api(base + '/issues?state=all&per_page=100', pages=True) if 'pull_request' not in row]
    for label in catalog['labels']:
        if label['name'] not in existing_labels:
            api(base + '/labels', label)
            print('Created label:', label['name'], flush=True)
    for milestone in catalog['milestones']:
        if milestone['title'] not in milestones:
            result = api(base + '/milestones', milestone)
            milestones[milestone['title']] = result['number']
            print('Created milestone:', milestone['title'], flush=True)
    for item in catalog['issues']:
        marker = f"<!-- rubyvr-work-package:{item['id']} -->"
        existing = [row for row in issues if marker in (row.get('body') or '')]
        if existing:
            print('Preserved existing:', item['id'], existing[0]['html_url'], flush=True)
            continue
        result = api(base + '/issues', dict(title=item['title'], body=bodies[item['id']],
                     labels=item['labels'], milestone=milestones[item['milestone']]))
        print('Created:', item['id'], result['html_url'], flush=True)


if __name__ == '__main__':
    main()
