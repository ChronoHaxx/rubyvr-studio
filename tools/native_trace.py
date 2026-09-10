"""Conservative C-source witnesses for script natives, never runtime reachability.

Function bodies come from the existing pokeruby source adapter. Direct calls and
literal callbacks at inspected registration APIs are followed. Pointer/table
calls, macros and unavailable bodies remain explicit frontiers.
"""
from collections import Counter, defaultdict, deque
import re

IDENTIFIER = re.compile(r'^[A-Za-z_]\w*$')
CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
KEYWORDS = {'if', 'for', 'while', 'switch', 'sizeof', '_Alignof', '__alignof__',
            'typeof', '__typeof__', '__attribute__', 'return'}
# Argument positions verified against the local adapter's declarations and uses.
# These edges mean "registered candidate", not immediate execution.
CALLBACKS = {'CreateTask': 0, 'SetMainCallback2': 0, 'SetVBlankCallback': 0,
             'SetHBlankCallback': 0, 'SetupNativeScript': 1}
INDIRECT = re.compile(r'\b[A-Za-z_]\w*(?:(?:\[[^\]\n]*\])|(?:\s*(?:\.|->)\s*\w+))+\s*\('
                      r'|\(\s*\*\s*[A-Za-z_]\w*(?:\s*(?:\.|->)\s*\w+)*\s*\)\s*\(')


def closing(text, start):
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                return i
    raise ValueError('Unclosed native call')


def arguments(text):
    result, start, depth = [], 0, 0
    for i, c in enumerate(text):
        if c in '([{': depth += 1
        elif c in ')]}': depth -= 1
        elif c == ',' and depth == 0:
            result.append(text[start:i].strip()); start = i + 1
    return result + [text[start:].strip()]


class Audit:
    def __init__(self, source, bodies, sinks, strip_comments):
        self.nodes, self.by_name, self.calls, self.sinks = {}, defaultdict(list), {}, defaultdict(list)
        self.cache = {}
        texts = {p: strip_comments(source.text(p), strings=True) for p in {b['path'] for b in bodies}}
        macros = set()
        for path in source.data:
            if path.endswith(('.c', '.h')) and path.startswith(('src/', 'include/')):
                macros.update(re.findall(r'^\s*#\s*define\s+(\w+)\(', strip_comments(source.text(path)), re.M))
        repeats = Counter()
        for b in bodies:
            b = dict(b)
            repeats[b['path'], b['symbol']] += 1
            id = f"{b['path']}:{b['symbol']}:{repeats[b['path'], b['symbol']]}"
            b['id'] = id
            self.nodes[id] = b
            self.by_name[b['symbol']].append(id)
        for sink in sinks:
            d = sink['detail']; src = d['source']
            for id in self.by_name[d['function']]:
                b = self.nodes[id]
                if b['path'] == src['path'] and b['line'] <= src['line'] <= b['end_line']:
                    self.sinks[id].append(sink)
        for id, b in self.nodes.items():
            text = texts[b['path']]
            body = text[b['start']+1:b['end']-1]
            signature = text[b['declaration_start']:b['start']]
            # Parameters and local pointer variables can shadow global functions.
            local = set(re.findall(r'\(\s*\*\s*(\w+)\s*\)', signature + body))
            params = signature[signature.find('(')+1:signature.rfind(')')]
            for param in arguments(params):
                name = re.search(r'\b(\w+)\s*(?:\[[^\]]*\])?\s*$', param)
                if name and name[1] not in ('void', 'int', 'char', 'short', 'long'):
                    local.add(name[1])
            local.update(re.findall(r'\b(?:[A-Za-z_]\w*[ \t*]+)+([A-Za-z_]\w*)\s*(?:=|;)', body))
            # A preprocessor definition is not a call statement. Its use remains
            # a macro frontier; neither conditional C branch is evaluated here.
            lines, continued = [], False
            for line in body.splitlines(keepends=True):
                directive = continued or line.lstrip().startswith('#')
                continued = directive and line.rstrip().endswith('\\')
                lines.append(re.sub(r'[^\n]', ' ', line) if directive else line)
            body = ''.join(lines)
            edges = []

            def edge(offset, target, kind, targets=()):
                edges.append(dict(target=target, kind=kind, candidates=list(targets),
                                  source=dict(path=b['path'], line=text.count('\n', 0, b['start']+1+offset)+1)))

            indirect = [(m.start(), m.end()) for m in INDIRECT.finditer(body)]
            for start, end in indirect:
                edge(start, body[start:end-1].strip(), 'indirect_call')
            # These inspected callback stores are visible boundaries, but this
            # slice does not infer when their later dispatcher will run them.
            assignments = re.compile(r'\b(?:\w+(?:\[[^\]\n]+\])?\s*(?:\.|->)\s*(?:func|callback\d*)|gFieldCallback\d*)\s*=(?!=)\s*([^;\n]+)')
            for match in assignments.finditer(body):
                edge(match.start(), match[1].strip(), 'callback_assignment')
            for match in CALL.finditer(body):
                name = match[1]
                if name in KEYWORDS or any(start <= match.start() < end and match.end()==end for start, end in indirect):
                    continue
                # Ignore local prototypes instead of mistaking declarations for
                # a route. A statement call has no type words in front of it.
                prefix = body[max(body.rfind('\n', 0, match.start()), body.rfind(';', 0, match.start()),
                                  body.rfind('{', 0, match.start()), body.rfind('}', 0, match.start()))+1:match.start()]
                if (re.fullmatch(r'\s*(?:[A-Za-z_]\w*\s+)+\**\s*', prefix)
                        and not (set(prefix.split()) & (KEYWORDS | {'else', 'do', 'case', 'goto'}))):
                    continue
                targets = self.resolve(name, b['path'])
                kind = ('local_or_pointer_call' if name in local else 'unexpanded_macro' if name in macros else
                        'direct_call' if len(targets) == 1 else 'ambiguous_call' if targets else 'missing_body')
                if kind not in ('direct_call', 'ambiguous_call'):
                    targets = []
                edge(match.start(), name, kind, targets)
                if name not in CALLBACKS or kind not in ('direct_call', 'ambiguous_call', 'missing_body'):
                    continue
                start = body.index('(', match.start(), match.end())
                args = arguments(body[start+1:closing(body, start)])
                position = CALLBACKS[name]
                target = args[position] if position < len(args) else '<missing argument>'
                if target in ('NULL', '0'):
                    continue
                symbol = target.lstrip('&').strip()
                candidates = self.resolve(symbol, b['path']) if IDENTIFIER.fullmatch(symbol) and symbol not in local else []
                edge(match.start(), target, 'registered_callback_candidate' if candidates else 'unresolved_callback', candidates)
            self.calls[id] = edges
            # The older boundary inventory is lexical. A locally shadowed API
            # name or a prototype must not become a resolved mutation witness.
            sites = {(e['source']['line'], e['target']) for e in edges
                     if e['kind'] in ('direct_call', 'ambiguous_call', 'missing_body')}
            self.sinks[id] = [s for s in self.sinks[id]
                              if (s['detail']['source']['line'], s['detail']['callee']) in sites]

    def resolve(self, name, path=None):
        candidates = self.by_name.get(name, ())
        local = [id for id in candidates if self.nodes[id]['static'] and self.nodes[id]['path'] == path]
        return local or [id for id in candidates if not self.nodes[id]['static']]

    def descriptor(self, id):
        b = self.nodes[id]
        return dict(id=id, function=b['symbol'], source=dict(path=b['path'], line=b['line']))

    def trace(self, symbol):
        if symbol in self.cache:
            return self.cache[symbol]
        roots = self.resolve(symbol)
        parents = {id: None for id in roots}
        queue, found, gaps = deque(roots), [], []

        def witness(id):
            path = []
            while parents[id] is not None:
                previous, edge = parents[id]
                path.append(dict(function=self.nodes[id]['symbol'], definition=self.descriptor(id)['source'],
                                 via=edge['kind'], call=edge['source'], ambiguous=len(edge['candidates'])>1))
                id = previous
            path.append(dict(function=self.nodes[id]['symbol'], definition=self.descriptor(id)['source'], via='script_target'))
            return list(reversed(path))

        while queue:
            id = queue.popleft()
            for sink in self.sinks[id]:
                d = sink['detail']
                found.append(dict(reference=sink['id'], callee=d['callee'], source=d['source'],
                                  arguments=d['arguments'], witness=witness(id)))
            terminal = {(s['detail']['source']['line'], s['detail']['callee']) for s in self.sinks[id]}
            for edge in self.calls[id]:
                # The selected mutation API is the endpoint being audited.
                if (edge['source']['line'], edge['target']) in terminal:
                    continue
                if not edge['candidates']:
                    gaps.append(dict(function=self.nodes[id]['symbol'], **edge))
                else:
                    for target in edge['candidates']:
                        if target not in parents:
                            parents[target] = (id, edge); queue.append(target)
        result = dict(target=symbol, roots=[self.descriptor(id) for id in roots],
                      resolution='source_candidate' if len(roots)==1 else 'ambiguous_root' if roots else 'missing_body',
                      functions_visited=len(parents), mutations=sorted(found, key=lambda r:r['reference']),
                      frontiers=gaps, frontier_counts=dict(Counter(e['kind'] for e in gaps)),
                      evaluation='Conservative source paths, including registered callback candidates; branches, schedules and variables are not evaluated. No path found is not proof of no mutation.')
        self.cache[symbol] = result
        return result

    def summary(self):
        return dict(function_definitions=len(self.nodes), call_sites=sum(map(len,self.calls.values())),
                    call_kinds=dict(Counter(e['kind'] for edges in self.calls.values() for e in edges)),
                    traced_targets=len(self.cache), targets_with_mutations=sum(bool(t['mutations']) for t in self.cache.values()),
                    targets_with_frontiers=sum(bool(t['frontiers']) for t in self.cache.values()))


def markdown(records, limit=30):
    """Compact witnesses for the existing ledger report; full detail stays JSON."""
    traces = [r['detail'] for r in records if r['category']=='native_mutation_trace']
    if not traces:
        return []
    lines = ['', '## Native mutation witnesses', '',
             f"{len(traces)} native targets; {sum(bool(t['mutations']) for t in traces)} with known mutation paths; "
             f"{sum(bool(t['frontiers']) for t in traces)} with unresolved calls or unexpanded macros.",
             'These are source candidates, including deferred registrations. Conditions, timing and runtime values remain unverified.', '']
    for trace in traces[:limit]:
        lines += [f"### {trace['target']}", '',
                  f"Binding: {trace['resolution']}. {len(trace['mutations'])} mutation sites; {len(trace['frontiers'])} unresolved references.", '']
        for mutation in trace['mutations'][:8]:
            steps = [p['function'] + (' [deferred candidate]' if p['via']=='registered_callback_candidate' else '') +
                     (' [ambiguous]' if p.get('ambiguous') else '') for p in mutation['witness']]
            where = mutation['source']
            lines.append('- `' + ' -> '.join(steps + [mutation['callee']]) + '`' + f" — `{where['path']}:{where['line']}`")
        if not trace['mutations']:
            lines.append('No listed mutation API was found along the supported paths. This does not establish that the target cannot change state.')
        if len(trace['mutations'])>8:
            lines.append('')
            lines.append(f"Showing 8 of {len(trace['mutations'])} mutation sites; JSON/show retains every witness.")
        if trace['frontier_counts']:
            lines.append('')
            lines.append('Unresolved: ' + ', '.join(f'{kind} {count}' for kind,count in sorted(trace['frontier_counts'].items())) + '.')
        lines.append('')
    lines.append(f'Showing {min(limit,len(traces))} of {len(traces)} targets; JSON export retains all paths and unresolved references.')
    return lines
