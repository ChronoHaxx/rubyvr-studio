"""Check contribution metadata and the source publication boundary, offline."""
import argparse
import ast
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import unquote

import yaml

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {'.git', 'build', 'mod-assets', 'release-stage', '__pycache__', '.idea', '.vscode'}
ROOT_FILES = {'README.md', 'CONTRIBUTING.md', 'AGENTS.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
              'SOURCE_ORIGIN.json', 'CMakeLists.txt', 'requirements.txt', '.gitignore', '.gitattributes'}
TEXT = {'.md', '.cpp', '.h', '.inl', '.py', '.ps1', '.json', '.yml', '.yaml', '.txt'}
FORBIDDEN = {'.gba', '.gb', '.gbc', '.rom', '.sav', '.srm', '.fla', '.flash', '.snap',
             '.bin', '.exe', '.dll', '.obj', '.pdb', '.zip', '.7z', '.mp4', '.apk'}
SECRET = re.compile(r'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b|-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----')


def working_files():
    for base, dirs, names in os.walk(ROOT):
        relative = Path(base).relative_to(ROOT)
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not d.startswith('build-')
                   and not (relative == Path('third_party') and d == 'pokeruby')]
        for name in names:
            yield (Path(base) / name).relative_to(ROOT).as_posix()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--publication', action='store_true', help='Audit staged/tracked paths and bytes, including ignored files staged by force.')
    args = parser.parse_args()
    errors, count = [], 0
    git = ['git', '-c', f'safe.directory={ROOT.as_posix()}', '-C', str(ROOT)]
    if args.publication:
        top = subprocess.check_output(git + ['rev-parse', '--show-toplevel'], text=True).strip()
        if Path(top).resolve() != ROOT:
            raise SystemExit('Initialize and stage the standalone repository first; parent repo is not the publication target.')
        raw = subprocess.check_output(git + ['ls-files', '--stage', '-z']).decode('utf-8')
        staged = {}
        for row in raw.split('\0'):
            if not row:
                continue
            metadata, path = row.split('\t', 1)
            mode, blob, stage = metadata.split()
            if mode not in ('100644', '100755') or stage != '0':
                errors.append(f'{path}: symlink, nested repository or unresolved merge is not publishable')
            staged[path] = blob
        if not staged:
            raise SystemExit('No staged/tracked files to audit.')
        paths = sorted(staged)
        def read(path):
            return subprocess.check_output(git + ['cat-file', 'blob', staged[path]])
    else:
        paths = sorted(working_files())
        def read(path):
            return (ROOT / path).read_bytes()

    present = set(paths)
    for required in ROOT_FILES | {'third_party/imgui/LICENSE.txt', 'LICENSES/DRAMALESS_SHAPE-MIT.txt',
                                '.github/workflows/ci.yml', 'docs/issues/catalog.json', 'docs/verification.md'}:
        if required not in present:
            errors.append(f'missing required file: {required}')
    for path in paths:
        count += 1
        p = Path(path)
        allowed = (path in ROOT_FILES or p.parts[0] in {'.github', 'docs', 'LICENSES', 'recipes', 'src', 'integration', 'tools'}
                   or path.startswith('third_party/imgui/'))
        if not allowed or p.suffix.lower() in FORBIDDEN or any(part in SKIP_DIRS for part in p.parts):
            errors.append(f'{path}: outside the source publication allowlist')
            continue
        if (ROOT / path).is_symlink():
            errors.append(f'{path}: symbolic links require explicit review')
            continue
        data = read(path)
        if p.suffix.lower() in {'.png', '.gif'}:
            if not path.startswith('docs/media/'):
                errors.append(f'{path}: image outside reviewed documentation media')
            continue
        if p.suffix.lower() not in TEXT and path not in ROOT_FILES:
            errors.append(f'{path}: unreviewed file type')
            continue
        try:
            content = data.decode('utf-8-sig')
            if SECRET.search(content):
                errors.append(f'{path}: credential/private-key pattern (value withheld)')
            if re.search(r'[A-Za-z]:[\\/]+Users[\\/]+[^\s]', content):
                errors.append(f'{path}: personal absolute filesystem path')
            if p.suffix == '.py':
                ast.parse(content, filename=path)
            elif p.suffix == '.json':
                json.loads(content)
            elif p.suffix in {'.yml', '.yaml'}:
                yaml.safe_load(content)
            elif p.suffix == '.md':
                for link in re.findall(r'!?\[[^\]\n]*\]\(([^)\n]+)\)', content):
                    target = link.strip().strip('<>').split('#', 1)[0]
                    if not target or re.match(r'^[a-z]+:', target, re.I):
                        continue
                    resolved = (ROOT / p.parent / unquote(target)).resolve()
                    if not resolved.is_relative_to(ROOT):
                        errors.append(f'{path}: local link escapes repository: {target}')
                    elif resolved.relative_to(ROOT).as_posix() not in present:
                        errors.append(f'{path}: missing local link target: {target}')
        except (UnicodeError, ValueError, SyntaxError, yaml.YAMLError) as exc:
            errors.append(f'{path}: {exc}')

    try:
        catalog = json.loads(read('docs/issues/catalog.json'))
        ids = [row['id'] for row in catalog['issues']]
        labels = {row['name'] for row in catalog['labels']}
        milestones = {row['title'] for row in catalog['milestones']}
        assert len(ids) == len(set(ids)), 'duplicate work package IDs'
        for issue in catalog['issues']:
            assert issue['body'] in present, f"missing issue body: {issue['id']}"
            assert set(issue['labels']) <= labels, f"unknown label: {issue['id']}"
            assert issue['milestone'] in milestones, f"unknown milestone: {issue['id']}"
            assert set(issue['depends_on']) <= set(ids), f"unknown dependency: {issue['id']}"
            assert f"<!-- rubyvr-work-package:{issue['id']} -->" in read(issue['body']).decode('utf-8')
        for issue in catalog['issues']:
            assert issue['id'] not in issue['depends_on'], 'self dependency'
    except (KeyError, AssertionError, ValueError) as exc:
        errors.append(f'issue catalog: {exc}')
    for error in errors:
        print('FAIL', error)
    print(f"{'FAIL' if errors else 'PASS'}: {count} source files; {'staged/tracked bytes' if args.publication else 'working files'}; offline metadata and publication checks")
    return int(bool(errors))


if __name__ == '__main__':
    raise SystemExit(main())
