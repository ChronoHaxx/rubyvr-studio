"""Source-backed M1 inventory; discovery is not execution or presentation approval.

This reads the local pokeruby adapter's JSON, assembly macros and C declarations.
It never executes source, evaluates a script, or guesses a runtime variable.
Both conditional source branches are retained for review.
"""
from __future__ import annotations

import ast
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re

from coverage_source import digest, file_hash
import native_trace

VERSION = 1


def adapter_hash():
    return digest({name: file_hash(Path(__file__).with_name(name))
                   for name in ('dynamic_inventory.py', 'native_trace.py')})
SOURCE_EXTENSIONS = {".c", ".h", ".inc", ".s", ".json", ".bin", ".png", ".pal"}
EVENT_KINDS = {"connections": "connection", "object_events": "object_event",
               "warp_events": "warp", "coord_events": "coordinate_event", "bg_events": "background_event"}
DEFINITIONS = {
    "event_objects.h": (("OBJ_EVENT_GFX_", "object_graphic"), ("MOVEMENT_TYPE_", "movement_type")),
    "field_effects.h": (("FLDEFF_", "field_effect"),),
    "weather.h": (("WEATHER_", "weather"), ("COORD_EVENT_WEATHER_", "coordinate_weather")),
    "map_types.h": (("MAP_TYPE_", "map_type"),),
    "metatile_behaviors.h": (("MB_", "metatile_behavior"),),
}
NATIVE_BOUNDARIES = re.compile(
    r"\b(MapGridSetMetatile\w*|SetWeather|SetSav1Weather|SetCurrentAndNextWeather|"
    r"SetWarpDestination\w*|SetDynamicWarp\w*|FieldEffectStart|"
    r"RemoveObjectEventByLocalIdAndMap|SpawnSpecialObjectEventParameterized)\s*\(")
FUNCTION = re.compile(r"(?m)^[ \t]*(?:[\w*]+[ \t]+)+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{")
TOKEN = re.compile(r"\b[A-Za-z_]\w*\b")
LABEL = re.compile(r"^\s*([A-Za-z_]\w*)::?\s*$")


def strip_comments(text, strings=False):
    pattern = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    def replace(match):
        token = match.group()
        if token.startswith(("/*", "//")) or strings:
            return re.sub(r"[^\n]", " ", token)
        return token
    return re.sub(pattern, replace, text, flags=re.S)


def balanced(text, start, left="{", right="}"):
    """Return the end of a balanced initializer/call without interpreting it."""
    masked = strip_comments(text[start:], strings=True)
    depth = 0
    for offset, char in enumerate(masked):
        if char == left:
            depth += 1
        elif char == right:
            depth -= 1
            if depth == 0:
                return start + offset + 1
    raise ValueError("Unclosed source initializer or call")


def integer(expression, constants, seen=()):
    """Resolve a small integer/alias expression; everything else stays symbolic."""
    if isinstance(expression, int):
        return expression
    if not isinstance(expression, str) or len(expression) > 512 or len(seen) > 32:
        return None
    try:
        node = ast.parse(expression.strip(), mode="eval").body
        def visit(n):
            if isinstance(n, ast.Constant) and type(n.value) is int:
                return n.value
            if isinstance(n, ast.Name) and n.id in constants and n.id not in seen:
                values = constants[n.id]
                if len(values) != 1:
                    raise ValueError("Conditional or duplicate definition")
                value = integer(values[0]["expression"], constants, (*seen, n.id))
                if value is not None:
                    return value
            if isinstance(n, ast.UnaryOp) and isinstance(n.op, (ast.UAdd, ast.USub, ast.Invert)):
                value = visit(n.operand)
                return value if isinstance(n.op, ast.UAdd) else -value if isinstance(n.op, ast.USub) else ~value
            if isinstance(n, ast.BinOp):
                a, b = visit(n.left), visit(n.right)
                if isinstance(n.op, ast.Add): return a + b
                if isinstance(n.op, ast.Sub): return a - b
                if isinstance(n.op, ast.BitOr): return a | b
                if isinstance(n.op, ast.BitAnd): return a & b
                if isinstance(n.op, ast.LShift) and 0 <= b <= 32: return a << b
                if isinstance(n.op, ast.RShift) and 0 <= b <= 32: return a >> b
            raise ValueError("Runtime or unsupported expression")
        return visit(node)
    except (SyntaxError, ValueError, TypeError, RecursionError):
        return None


class Source:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.data = {}
        for folder in ("data", "src", "include", "graphics"):
            for path in sorted((self.root / folder).rglob("*")):
                if path.is_file() and path.suffix.lower() in SOURCE_EXTENSIONS:
                    if not path.resolve().is_relative_to(self.root):
                        raise ValueError(f"Source path escapes checkout: {path}")
                    self.data[path.relative_to(self.root).as_posix()] = path.read_bytes()
        self.hashes = {p: hashlib.sha256(b).hexdigest() for p, b in self.data.items()}
        self.source_hash = digest(self.hashes)
        self.texts = {}
        self.legacy_encodings = []

    def text(self, path):
        if path not in self.data:
            raise ValueError(f"Required source file missing: {path}")
        if path not in self.texts:
            try:
                self.texts[path] = self.data[path].decode("utf-8")
            except UnicodeDecodeError:
                if path.endswith(".json"):
                    raise ValueError(f"Source JSON is not UTF-8: {path}")
                # Some decomp C comments use legacy single-byte text. Latin-1
                # preserves every byte; identifiers/macros remain ASCII. Record
                # this explicitly, and continue hashing the original bytes.
                self.texts[path] = self.data[path].decode("latin-1")
                self.legacy_encodings.append(path)
        return self.texts[path]

    def json(self, path):
        return json.loads(self.text(path))

    def dependencies(self, paths):
        return digest({p: self.hashes.get(p, "missing") for p in sorted(set(paths))})


def script_category(command):
    if command == "def_special":
        return "native_special_definition"
    if command in ("setmetatile", "setmaplayoutindex", "setstepcallback", "setdooropened", "setdoorclosed"):
        return "script_tile_change"
    if "warp" in command:
        return "script_warp"
    if "weather" in command or command in ("dotimebasedevents", "gettime", "setflashradius", "setflashlevel"):
        return "script_weather_time"
    if "fieldeffect" in command or command.startswith("fieldeffect_"):
        return "script_field_effect"
    if "object" in command or command in ("applymovement", "waitmovement", "setvar", "copyvar", "copyvarifnotzero"):
        return "script_actor_state"
    if command in ("special", "specialvar", "callnative", "gotonative"):
        return "script_native_call"
    if any(word in command for word in ("battle", "contest", "menu", "msgbox", "braille", "pokemart")):
        return "script_presentation"
    return None


def script_blocks(source):
    paths = {p for p in source.data if re.fullmatch(r"data/maps/[^/]+/scripts\.inc", p)
             or (p.startswith("data/scripts/") and p.endswith(".inc"))}
    paths.update(p for p in ("data/event_scripts.s", "data/field_effect_scripts.s") if p in source.data)
    # Follow source includes, retaining unused/conditional scripts as candidates.
    todo = list(paths)
    while todo:
        p = todo.pop()
        for child in re.findall(r'\.include\s+"([^"]+)"', source.text(p)):
            if child.startswith("data/") and child in source.data and child not in paths:
                paths.add(child)
                todo.append(child)
    macros, movements = set(), set()
    for p in source.data:
        if (p.startswith("include/macros") and p.endswith(".inc")) or p in paths:
            macros.update(re.findall(r"^\s*\.macro\s+(\w+)", source.text(p), re.M))
            # The adapter source generates these macros from an explicit list.
            movements.update(re.findall(r"^\s*create_movement_action\s+(\w+)", source.text(p), re.M))
    macros.update(movements)
    blocks, unknown = [], Counter()
    for path in sorted(paths):
        current, repeats, conditions = None, Counter(), []
        for line_number, raw in enumerate(strip_comments(source.text(path)).splitlines(), 1):
            line = raw.split("@", 1)[0].strip()
            conditional = re.match(r"^[#.]\s*(if\w*|elseif|else|elif|endif)\b", line)
            if conditional:
                directive = conditional[1]
                if directive.startswith("if"):
                    conditions.append([line])
                elif directive == "endif":
                    if conditions: conditions.pop()
                elif conditions:
                    conditions[-1].append(line)
                continue
            if match := LABEL.fullmatch(line):
                label = match[1]
                if current and not current["commands"] and not current.get("data_only"):
                    current["references"].add(label)
                    current["alias"] = label
                repeats[label] += 1
                current = dict(path=path, line=line_number, label=label, variant=repeats[label],
                               commands=[], references=set(), conditions=[list(c) for c in conditions])
                blocks.append(current)
                continue
            if not line or line.startswith("#") or current is None:
                continue
            if line.startswith("."):
                if line.startswith((".string", ".byte", ".2byte", ".4byte", ".word", ".incbin")):
                    current["data_only"] = True
                if line.startswith((".4byte ", ".word ")):
                    current["references"].update(TOKEN.findall(line))
                continue
            command, _, arguments = line.partition(" ")
            # Tabs are legal separators too.
            parts = line.split(None, 1)
            command, arguments = parts[0], parts[1] if len(parts) > 1 else ""
            recognized = command in macros
            if not recognized: unknown[command] += 1
            current["commands"].append(dict(command=command, arguments=arguments, line=line_number,
                                             recognized=recognized, movement=command in movements,
                                             conditions=[list(c) for c in conditions]))
            current["references"].update(TOKEN.findall(arguments))
    # Text labels and data-only tables are not executable states. Their files
    # remain fingerprinted; field-effect entry labels have real macro commands.
    return [b for b in blocks if b["commands"] or b.get("alias")], unknown


def inventory(root, expected_maps=None):
    adapter_before = adapter_hash()
    source = Source(root)
    records, ids = [], set()
    constants = defaultdict(list)
    constant_paths = [p for p in source.data if p.startswith("include/constants/") and p.endswith(".h")]
    macro_paths = [p for p in source.data if p.startswith("include/macros") and p.endswith(".inc")]
    for path in constant_paths:
        for match in re.finditer(r"(?m)^#define[ \t]+(\w+)[ \t]+([^\r\n]+)", strip_comments(source.text(path))):
            constants[match[1]].append(dict(expression=match[2].strip(), path=path,
                                            line=source.text(path).count("\n", 0, match.start()) + 1))
    if not constants:
        raise ValueError("Source constant definitions missing")
    constant_hash = source.dependencies(constant_paths + macro_paths + ["data/specials.inc", "data/script_cmd_table.inc"])
    native_paths = [p for p in source.data if p.startswith(("src/", "include/")) and p.endswith((".c", ".h"))]
    native_hash = source.dependencies(native_paths)
    object_paths = [p for p in source.data if p.startswith(("src/data/object_events/", "graphics/object_events/"))]
    effect_paths = [p for p in source.data if "field_effect" in p]
    animation_paths = [p for p in source.data if "/anim/" in p and p.startswith("data/tilesets/")]
    animation_hash = source.dependencies(animation_paths + ["src/tileset_anim.c", "data/tilesets/headers.inc"])

    def add(key, category, label, paths, detail, map_id=None, related_maps=(), x=None, y=None,
            status="discovered", extra=()):
        id = "state:" + key
        if id in ids: raise ValueError(f"Duplicate dynamic source identity: {id}")
        ids.add(id)
        detail = dict(detail, source_status=status, related_maps=sorted(set(related_maps)))
        records.append(dict(id=id, category=category, label=label, map_id=map_id, x=x, y=y,
                            source_hash=digest([source.dependencies(paths), constant_hash, extra]), detail=detail))

    map_paths = sorted(p for p in source.data if re.fullmatch(r"data/maps/[^/]+/map\.json", p))
    maps, map_files, folder_maps = {}, {}, {}
    for path in map_paths:
        row = source.json(path)
        id = row["id"]
        if id in maps: raise ValueError(f"Duplicate map ID: {id}")
        for collection in EVENT_KINDS:
            events = row.get(collection)
            if events is None:
                row[collection] = []
                continue
            if not isinstance(events, list) or any(not isinstance(e, dict) for e in events):
                raise ValueError(f"Invalid {collection}: {id}")
            for event in events:
                if any(k in event and type(event[k]) is not int for k in ("x", "y", "elevation")):
                    raise ValueError(f"Invalid event coordinates/layer: {id} {collection}")
        maps[id], map_files[id], folder_maps[path.rsplit("/", 1)[0]] = row, path, id
    if not maps or (expected_maps is not None and set(maps) != set(expected_maps)):
        raise ValueError("Dynamic inventory map set does not match the static catalog")
    layout_path = "data/layouts/layouts.json"
    layouts = {}
    for layout in source.json(layout_path)["layouts"]:
        if layout["id"] in layouts: raise ValueError(f"Duplicate layout: {layout['id']}")
        if any(type(layout[k]) is not int or layout[k] < 1 for k in ("width", "height")):
            raise ValueError(f"Invalid layout dimensions: {layout['id']}")
        layouts[layout["id"]] = layout

    blocks, unknown = script_blocks(source)
    labels, file_links, file_maps = defaultdict(list), defaultdict(set), defaultdict(set)
    for block in blocks:
        labels[block["label"]].append(block)
        if owner := folder_maps.get(block["path"].rsplit("/", 1)[0]):
            file_maps[block["path"]].add(owner)
    for block in blocks:
        for symbol in block["references"]:
            for target in labels.get(symbol, []): file_links[block["path"]].add(target["path"])
    script_dependencies = {}
    for path in {b["path"] for b in blocks}:
        seen, todo = set(), [path]
        while todo:
            item = todo.pop()
            if item not in seen:
                seen.add(item)
                todo.extend(file_links[item] - seen)
        script_dependencies[path] = seen
    # Associations mean potential source references, not proof of execution.
    for map_id, row in maps.items():
        for kind in EVENT_KINDS:
            for event in row.get(kind) or []:
                for target in labels.get(event.get("script"), []): file_maps[target["path"]].add(map_id)
    for path, associated in list(file_maps.items()):
        for target in script_dependencies.get(path, ()):
            file_maps[target].update(associated)
    symbol_maps = defaultdict(set)
    for id, row in maps.items():
        for token in TOKEN.findall(json.dumps(row)): symbol_maps[token].add(id)
    for block in blocks:
        for token in block["references"]: symbol_maps[token].update(file_maps[block["path"]])

    def script_detail(symbol):
        found = labels.get(symbol, [])
        return dict(symbol=symbol, candidates=[dict(path=b["path"], line=b["line"], label=b["label"],
                                                   variant=b["variant"]) for b in found],
                    resolution="source_candidates" if found else "none" if symbol in (None, "NULL") or integer(symbol, constants) == 0 else "unresolved_reference")

    def script_deps(symbol):
        return set().union(*(script_dependencies[b["path"]] for b in labels.get(symbol, [])))

    for map_id, row in sorted(maps.items()):
        path = map_files[map_id]
        if row["layout"] not in layouts: raise ValueError(f"Unknown map layout: {map_id}")
        layout = layouts[row["layout"]]
        map_scripts = path.replace("map.json", "scripts.inc")
        dims = dict(width=layout["width"], height=layout["height"])
        environment = {k: v for k, v in row.items() if k not in EVENT_KINDS}
        add(f"map_environment:{map_id}", "map_environment", map_id + " source environment",
            [path, layout_path, map_scripts, *script_dependencies.get(map_scripts, ())],
            dict(source=dict(path=path, pointer="/"), metadata=environment,
              dimensions=dims, coordinate_space="layout_cells", elevation_is_gameplay_layer=True), map_id,
            extra=[native_hash])
        for collection, category in EVENT_KINDS.items():
            events = row.get(collection) or []
            if not isinstance(events, list): raise ValueError(f"Invalid {collection}: {map_id}")
            for index, event in enumerate(events):
                detail = dict(source=dict(path=path, pointer=f"/{collection}/{index}"), data=event,
                              source_index=index, coordinate_space="layout_cells", elevation_is_gameplay_layer=True)
                status, deps = "discovered", {path}
                if "script" in event:
                    detail["script"] = script_detail(event["script"])
                    deps.update(script_deps(event["script"]))
                    if detail["script"]["resolution"] == "unresolved_reference": status = "unresolved_reference"
                if category == "object_event":
                    detail["local_object_id"] = index + 1
                    detail["graphics_resolution"] = "runtime_variable" if "_VAR_" in event["graphics_id"] else "source_constant"
                    detail["graphics_integer"] = integer(event["graphics_id"], constants)
                    if event["graphics_id"] not in constants:
                        detail["graphics_resolution"] = "unresolved_reference"
                        status = "unresolved_reference"
                    if detail["graphics_resolution"] == "runtime_variable" and status != "unresolved_reference": status = "runtime_resolution"
                    deps.update(object_paths)
                if category in ("connection", "warp"):
                    target = event.get("map", event.get("dest_map"))
                    detail["target_map"] = target
                    if target == "MAP_DYNAMIC":
                        status = "runtime_resolution"
                        detail["target_resolution"] = "saved_dynamic_warp; source warp index is ignored"
                    elif target not in maps:
                        status = "unresolved_reference"
                        detail["target_resolution"] = "unknown_map"
                    else:
                        deps.add(map_files[target])
                        detail["target_resolution"] = "source_map"
                        if category == "warp":
                            warp = integer(event["dest_warp_id"], constants)
                            if warp is not None and 0 <= warp < len(maps[target].get("warp_events") or []):
                                detail["target_event"] = f"state:warp:{target}:{warp}"
                            else:
                                status = "unresolved_reference"
                                detail["target_resolution"] = "unresolved_warp_index"
                if category == "coordinate_event" and event.get("type") == "weather":
                    detail["weather_namespace"] = "COORD_EVENT_WEATHER; not interchangeable with WEATHER"
                    deps.add("src/coord_event_weather.c")
                add(f"{category}:{map_id}:{index}", category, f"{map_id} {category} {index}", deps, detail,
                    map_id, x=event.get("x"), y=event.get("y"), status=status,
                    extra=[native_hash] if category in ("object_event", "warp", "coordinate_event") else [])

    for filename, prefixes in DEFINITIONS.items():
        path = "include/constants/" + filename
        source.text(path)  # Missing definition inventories must fail explicitly.
        for name, definitions in sorted(constants.items()):
            for variant, definition in enumerate(definitions):
                if definition["path"] != path: continue
                for prefix, category in prefixes:
                    if not name.startswith(prefix): continue
                    value = integer(name, constants)
                    related = symbol_maps[name]
                    deps = [path]
                    if category == "object_graphic": deps += object_paths
                    if category == "field_effect": deps += effect_paths
                    variable = "_VAR_" in name
                    add(f"definition:{name}:{variant}", category, name, deps,
                        dict(source=dict(path=path, line=definition["line"]), expression=definition["expression"],
                             integer=value, alias=bool(re.fullmatch(r"[A-Za-z_]\w*", definition["expression"])),
                             variable=variable, count_is_not_unique_images=True), related_maps=related,
                        status="runtime_resolution" if variable else "symbolic" if value is None else "discovered")

    for block in blocks:
        path, label = block["path"], block["label"]
        key = f"{path}:{label}:{block['variant']}"
        related = file_maps[path]
        deps = script_dependencies[path]
        own_map = folder_maps.get(path.rsplit("/", 1)[0])
        detail = dict(source=dict(path=path, line=block["line"]), label=label,
                      conditions=block["conditions"], commands=block["commands"],
                      alias=block.get("alias"),
                      execution="not_evaluated; source includes both conditional branches",
                      association="source file and transitive symbol references; not runtime reachability")
        add("script_block:" + key, "script_block", label, deps, detail, own_map, related, extra=[native_hash])
        counts = Counter()
        for command in block["commands"]:
            op = command["command"]
            category = script_category(op) if command["recognized"] else "unclassified_script_command"
            if command["movement"]: category = "movement_command"
            if category is None: continue
            counts[op] += 1
            arguments = [a.strip() for a in command["arguments"].split(",")]
            detail = dict(source=dict(path=path, line=command["line"]), label=label, **command,
                          argument_values=[integer(a, constants) for a in arguments],
                          execution="not_evaluated; conditions, variables and call order need runtime evidence")
            add(f"script_command:{key}:{op}:{counts[op]}", category, op + " " + command["arguments"],
                deps, detail, own_map, related, status="source_candidate" if command["recognized"] else "unresolved_reference",
                extra=[native_hash])

    # Explicit declaration records preserve timing expressions, animation loops,
    # dimensions and source pointers without pretending to render any frames.
    declaration = re.compile(r"\b(?:union\s+(?:AnimCmd|AffineAnimCmd)|struct\s+(?:ObjectEventGraphicsInfo|SpriteTemplate))"
                             r"\s+(?:\*\s*(?:const\s+)?)?(\w+)\s*(?:\[[^\]]*\])?\s*=\s*\{")
    native_functions = defaultdict(list)
    native_bodies = []
    for path in sorted(p for p in source.data if p.startswith("src/") and p.endswith((".h", ".c"))):
        text = strip_comments(source.text(path))
        repeats = Counter()
        for match in declaration.finditer(text):
            start = text.index("{", match.start(), match.end())
            end = balanced(text, start)
            name = match[1]
            repeats[name] += 1
            category = ("animation_table" if "*" in match[0] else "actor_animation") if "AnimCmd" in match[0] else "sprite_definition"
            deps = [path] + (object_paths if "/object_events/" in path else effect_paths)
            add(f"declaration:{path}:{name}:{repeats[name]}", category, name, deps,
                dict(source=dict(path=path, line=text.count("\n", 0, match.start()) + 1),
                     initializer=text[start:end], evaluation="source declaration; frames and expressions not executed"),
                extra=[native_hash])
        if not path.endswith(".c"): continue
        masked = strip_comments(text, strings=True)
        functions = []
        for match in FUNCTION.finditer(masked):
            if match[1] in native_trace.KEYWORDS:
                continue
            start = masked.index("{", match.start(), match.end())
            end = balanced(masked, start)
            functions.append((start, end, match[1]))
            native_bodies.append(dict(path=path, symbol=match[1], start=start, end=end,
                                      declaration_start=match.start(), line=text.count('\n',0,match.start())+1,
                                      end_line=text.count('\n',0,end)+1, static=bool(re.search(r'\bstatic\b',match[0]))))
            native_functions[match[1]].append(dict(path=path, line=text.count("\n", 0, match.start()) + 1))
        native_counts = Counter()
        for match in NATIVE_BOUNDARIES.finditer(masked):
            function = next((name for start, end, name in functions if start < match.start() < end), None)
            start = masked.index("(", match.start(), match.end())
            end = balanced(masked, start, "(", ")")
            if function is None:
                prefix = masked[masked.rfind("\n", 0, match.start()) + 1:match.start()]
                if masked[end:].lstrip().startswith("{") or re.fullmatch(r"\s*(?:\w+\s+)+\**\s*", prefix):
                    continue  # Function definition or prototype.
            name = match[1]
            native_counts[(function, name)] += 1
            add(f"native:{path}:{function or 'unscoped'}:{name}:{native_counts[function, name]}",
                "native_state_reference" if function else "native_unscoped_reference",
                (function or "unscoped source") + " references " + name, [path],
                dict(source=dict(path=path, line=text.count("\n", 0, match.start()) + 1),
                     function=function, callee=name, arguments=text[start + 1:end - 1],
                     evaluation="lexical call site; C expressions, preprocessor branches and map context unresolved"),
                status="source_candidate", extra=[native_hash])

    audit = native_trace.Audit(source, native_bodies,
                              [r for r in records if r['category']=='native_state_reference'], strip_comments)
    roots = defaultdict(list)
    registered = {r['detail']['arguments'].strip() for r in records if r['category']=='native_special_definition'}
    for record in records:
        if record["category"] not in ("script_native_call", "native_special_definition"): continue
        detail = record["detail"]
        symbol = detail["arguments"].split(",")[-1].strip()
        detail["native_target"] = symbol
        detail["native_candidates"] = native_functions.get(symbol, [])
        detail["native_resolution"] = "source_candidates" if detail["native_candidates"] else "unresolved_reference"
        special = detail['command'] in ('special', 'specialvar')
        detail['dispatch_resolution'] = 'registered_special' if special and symbol in registered else 'unregistered_special' if special else 'direct_symbol_reference'
        if not native_trace.IDENTIFIER.fullmatch(symbol) or (special and symbol not in registered):
            detail['native_resolution'] = 'unresolved_reference'
            continue
        detail['native_trace_id'] = 'state:native_trace:' + symbol
        roots[symbol].append(record)

    for symbol, callers in sorted(roots.items()):
        detail = audit.trace(symbol)
        related = {m for r in callers for m in r['detail']['related_maps']}
        related.update(r['map_id'] for r in callers if r['map_id'])
        detail = dict(detail, source=detail['roots'][0]['source'] if detail['roots'] else callers[0]['detail']['source'],
                      script_references=[r['id'] for r in callers], association='Conservative script-file map association; not runtime reachability')
        add('native_trace:' + symbol, 'native_mutation_trace', symbol + ' native mutation audit', [], detail,
            related_maps=related, status='source_candidate' if detail['resolution']=='source_candidate' else 'unresolved_reference',
            extra=[native_hash, sorted(r['source_hash'] for r in callers), sorted(related)])

    for path in sorted(animation_paths):
        if Path(path).suffix.lower() not in (".png", ".pal", ".bin"): continue
        add("tileset_frame:" + path, "tileset_animation_frame", path, [path],
            dict(source=dict(path=path), bytes=len(source.data[path]),
                 evaluation="source frame; playback order/rate belongs to tileset callback"), extra=[animation_hash])
    callback_path = "src/tileset_anim.c"
    text = strip_comments(source.text(callback_path))
    header_path = "data/tilesets/headers.inc"
    header_blocks = re.split(r"(?m)^\s*(gTileset_\w+)::?[^\n]*\n", source.text(header_path))
    callbacks = {}
    for index in range(1, len(header_blocks), 2):
        pointers = re.findall(r"\.4byte\s+(\w+)", header_blocks[index + 1])
        if len(pointers) >= 5: callbacks[header_blocks[index]] = pointers[4]
    used_layouts = [layouts[m["layout"]] for m in maps.values()]
    for tileset in sorted({layout[k] for layout in used_layouts for k in ("primary_tileset", "secondary_tileset")}):
        related = [id for id, m in maps.items() if tileset in (layouts[m["layout"]]["primary_tileset"], layouts[m["layout"]]["secondary_tileset"])]
        callback = callbacks.get(tileset)
        no_callback = callback in ("NULL", "0")
        found = bool(callback and re.search(r"\b" + re.escape(callback) + r"\s*\(", text))
        add("tileset_callback:" + tileset, "tileset_animation_callback", tileset, [callback_path, header_path, layout_path],
            dict(source=dict(path=header_path), callback=callback,
                 callback_found=found, no_callback=no_callback,
                 evaluation="callback reference; playback and affected tiles need runtime verification"),
            related_maps=related, status="no_callback" if no_callback else "discovered" if found else "unresolved_reference", extra=[animation_hash])

    if adapter_hash()!=adapter_before:
        raise ValueError('Dynamic adapter changed during inventory; prior ledger retained')
    counts = Counter(r["category"] for r in records)
    return dict(version=VERSION, source_hash=source.source_hash, source_files=source.hashes,
                adapter_sha256=adapter_before, map_ids=sorted(maps), counts=dict(sorted(counts.items())), native_audit=audit.summary(),
                legacy_text_encodings=sorted(source.legacy_encodings),
                unknown_commands=dict(sorted(unknown.items())), records=sorted(records, key=lambda r: r["id"]),
                limits=["Source references are candidates, not an enumerated set of reachable gameplay states.",
                        "Conditional source branches, script variables and native call expressions are not executed.",
                        "Shared script associations conservatively follow source-file symbol references.",
                        "Native paths follow direct helpers and literal registered callback candidates; pointer/table calls and macros remain frontiers. Arbitrary memory writes require further audit.",
                        "Runtime animation, UI/battle routing, time combinations and headset presentation remain untested."])


def verify_source(root, manifest):
    if (Source(root).source_hash != manifest["source_hash"]
            or adapter_hash() != manifest["adapter_sha256"]):
        raise ValueError("Dynamic source changed during inventory; prior ledger retained")
