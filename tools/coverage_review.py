"""Read-only, per-map Studio views of the coverage ledger. No approvals or game art."""
from __future__ import annotations

from collections import defaultdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sqlite3
import uuid
import coverage_disposition

STAGES = ('visual', 'live', 'headset', 'disposition')


def open_readonly(path):
    db = sqlite3.connect(Path(path).resolve().as_uri() + '?mode=ro', uri=True)
    db.row_factory = sqlite3.Row
    if db.execute('PRAGMA user_version').fetchone()[0] != 1:
        db.close()
        raise ValueError('Unsupported coverage ledger version')
    return db


def fingerprint(data):
    """Fast byte identity shared with the editor, not a security signature."""
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f'{value:016x}'


def export_studio(db, pack_path, output):
    """Publish the index last; incomplete exports cannot replace a usable index."""
    output, pack_path = Path(output), Path(pack_path)
    inputs = [pack_path.resolve()] + [Path(r[2]).resolve() for r in db.execute('PRAGMA database_list') if r[2]]
    if output.resolve() in inputs:
        raise ValueError('Review output would overwrite a pack or ledger input')
    pack_bytes = pack_path.read_bytes()
    pack = json.loads(pack_bytes)
    models = {p['id']: p for p in pack['patterns']}
    if len(models) != len(pack['patterns']):
        raise ValueError('Duplicate model ID')
    with db:
        db.execute('BEGIN')
        metadata = {r['key']: json.loads(r['value']) for r in db.execute('SELECT * FROM metadata')}
        pack_hash = hashlib.sha256(pack_bytes).hexdigest()
        if metadata.get('pack_sha256') != pack_hash:
            raise ValueError('Pack differs from the last coverage sync; sync that pack before exporting its review view')
        catalog = Path(metadata['catalog_directory']).resolve()
        if output.resolve().is_relative_to(catalog):
            raise ValueError('Review output must be outside the source catalog')
        if hashlib.sha256((catalog / 'catalog.json').read_bytes()).hexdigest() != metadata.get('catalog_sha256'):
            raise ValueError('Catalog differs from the last coverage sync; sync before exporting')
        reviews = {}
        for r in db.execute('SELECT * FROM reviews ORDER BY id'):
            reviews[(r['entry_id'], r['stage'])] = dict(r)
        policy = coverage_disposition.Policy(db)
        defects = defaultdict(list)
        for r in db.execute("SELECT * FROM defects WHERE status='open' ORDER BY id"):
            defects[r['entry_id']].append(dict(r))
        families = {r['family_id']: json.loads(r['detail']) for r in db.execute(
            "SELECT family_id,detail FROM entries WHERE present=1 AND kind='family'")}
        maps = [dict(r) for r in db.execute("SELECT * FROM entries WHERE present=1 AND kind='map' ORDER BY map_id")]
        geometry = {}

        def shape(id, model=False):
            key = ('model:' if model else 'family:') + id
            if key not in geometry:
                if model:
                    p = models[id]
                else:
                    path = (catalog / families[id]['proposal']).resolve()
                    if not path.is_relative_to(catalog):
                        raise ValueError('Proposal path escapes catalog')
                    p = json.loads(path.read_text(encoding='utf-8'))['patterns'][0]
                w, h = p['w'], p['extent']
                mask = p['mask']
                if w < 1 or h < 1 or len(mask) != w*h or 1 not in mask:
                    raise ValueError('Invalid source footprint')
                anchor = p.get('anchor', mask.index(1))
                if not isinstance(anchor, int) or not 0 <= anchor < len(mask) or mask[anchor] != 1:
                    raise ValueError('Invalid source anchor')
                geometry[key] = (w, h, anchor % w, anchor // w)
            return geometry[key]

        def evidence(id):
            stages, notes, failed = [], [], False
            for stage in STAGES:
                r = reviews.get((id, stage))
                status = ('stale' if r['invalidated'] else r['result']) if r else 'unreviewed'
                stages.append(status)
                if r:
                    notes.append(f"{id} / {stage}: {status}. {r['note']} [{r['evidence']}]" +
                                 (f" Previous result: {r['result']}; {r['invalidated']}" if r['invalidated'] else ''))
                    failed |= r['result'] in ('rejected', 'blocked')
            for d in defects.get(id, []):
                notes.append(f"{d['id']} ({d['milestone']}): {d['title']}")
                failed = True
            return stages, notes, failed

        documents, summaries = {}, []
        # Per-map files keep all exact placements without building a 200k-row
        # JSON DOM or scanning every map on each editor frame.
        for m in maps:
            map_id = m['map_id']
            if not re.fullmatch(r'MAP_[A-Z0-9_]+', map_id):
                raise ValueError('Invalid map identity')
            bounds = json.loads(m['detail'])
            # Catalog dimensions describe the unpadded layout; exact placement
            # coordinates already include the engine's 7-cell backup border.
            width, height = bounds['width'] + 15, bounds['height'] + 14
            rows = []
            map_notes = [f"{d['id']} ({d['milestone']}): {d['title']}" for d in defects.get(m['id'], [])]
            for entry in db.execute("SELECT * FROM entries WHERE present=1 AND map_id=? "
                                    "AND kind IN ('placement','instance','export_gap') ORDER BY id", (map_id,)):
                r = dict(entry)
                detail = json.loads(r['detail'])
                own, notes, failed = evidence(r['id'])
                related = []
                model_id = detail.get('model', '')
                if r['family_id']:
                    related.append('family:' + r['family_id'])
                related += ['model:' + id for id in detail.get('models', [])]
                if model_id:
                    related.append('model:' + model_id)
                for id in sorted(set(related)):
                    _, more, bad = evidence(id)
                    notes += ['Related ' + text for text in more]
                    failed |= bad
                # Related reviews flag attention but never replace an instance's
                # own independent stage or approve an entire source family.
                intent = policy.classify(r, reviews.get((r['id'], 'disposition')))
                disposition = intent['disposition']
                notes.insert(0, f"Intended treatment ({intent['basis']}): {disposition} / {intent['milestone']}. {intent['reason']} Next: {intent['next_action']}")
                if r['kind'] == 'export_gap':
                    w, h, ax, ay = detail['width'], detail['height'], 0, 0
                else:
                    w, h, ax, ay = shape(model_id or r['family_id'], bool(model_id))
                x, y = r['x'], r['y']
                if not all(isinstance(n, int) for n in (x, y, w, h, ax, ay)):
                    raise ValueError('Missing exact placement coordinates')
                if not (0 <= x <= x+ax < width and 0 <= y <= y+ay < height and w > 0 and h > 0):
                    raise ValueError('Source placement anchor is outside the backup map')
                flags = (int(r['implementation'] in ('unmodeled', 'partial')) |
                         (int(disposition == 'unresolved') << 1) |
                         (int(own[0] in ('unreviewed', 'stale')) << 2) |
                         (int(failed or r['implementation'] in ('blocked', 'conflict')) << 3))
                label = r['label'] if model_id else f"{r['category'].capitalize()} at {x},{y}"
                rows.append(dict(id=r['id'], label=label, kind=r['kind'], category=r['category'],
                                 boundary=r['boundary'] or 'map_body', x=x, y=y, w=w, h=h,
                                 anchor_x=x+ax, anchor_y=y+ay, model=model_id,
                                 implementation=r['implementation'], disposition=disposition,
                                 visual=own[0], live=own[1], headset=own[2], flags=flags,
                                 notes='\n'.join(notes)))
            body = dict(version=1, map=map_id, width=width, height=height,
                        notes='\n'.join(map_notes), rows=rows)
            documents[map_id] = json.dumps(body, ensure_ascii=True, separators=(',', ':')) + '\n'
            summaries.append(dict(id=map_id, rows=len(rows),
                                  failed=sum(bool(r['flags'] & 8) for r in rows),
                                  unmodeled=sum(bool(r['flags'] & 1) for r in rows)))

    signature = hashlib.sha256()
    for map_id, body in documents.items():
        signature.update(map_id.encode()); signature.update(body.encode())
    signature.update(pack_hash.encode())
    signature.update(str(metadata.get('source_version', '')).encode())
    generation = 'snapshot-' + signature.hexdigest()[:24]
    directory = output.parent / generation
    directory.mkdir(parents=True, exist_ok=True)
    for map_id, body in documents.items():
        path = directory / (map_id + '.json')
        if not path.exists() or path.read_text(encoding='utf-8') != body:
            temporary = path.with_suffix('.json.tmp-' + uuid.uuid4().hex)
            temporary.write_text(body, encoding='utf-8'); temporary.replace(path)
    index = dict(version=1, directory=generation, created_at=datetime.now(timezone.utc).isoformat(),
                 classification_policy=policy.identity,
                 pack_fingerprint=fingerprint(pack_bytes), pack_sha256=pack_hash,
                 source_version=metadata.get('source_version', ''), maps=summaries,
                 scope='Static placements and their related reviews; source-state reports remain in the ledger CLI.')
    temporary = output.with_suffix('.json.tmp-' + uuid.uuid4().hex)
    temporary.write_text(json.dumps(index, indent=2) + '\n', encoding='utf-8')
    temporary.replace(output)
    return dict(maps=len(maps), rows=sum(m['rows'] for m in summaries), output=str(output), generation=generation)
