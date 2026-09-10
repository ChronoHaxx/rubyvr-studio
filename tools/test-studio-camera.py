"""Hidden SDL proof of viewport camera controls and authoring input isolation.

Uses the real event loop and toolbar, without displaying a window, capturing
the desktop cursor, or touching an open studio process. Pass --exe when testing
a separately linked binary while the user's editor is still running.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
from studio_paths import executable as native_executable, native_environment
import subprocess
from datetime import datetime, timezone

parser = argparse.ArgumentParser()
parser.add_argument("--exe", type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent
game = repo
build = game / "build"
exe = (args.exe or native_executable('rubyvr_gui')).resolve()
prefix = build / "studio-camera"
script = build / "studio-camera-events.json"
report = build / "studio-camera-pass.json"
events, checkpoints = [], []


def event(frame, kind, **values):
    events.append(dict(frame=frame, type=kind, **values))


def click(frame, x, y):
    event(frame, "motion", x=x, y=y)
    event(frame + 1, "down", x=x, y=y)
    event(frame + 2, "up", x=x, y=y)


def key(frame, scan, mod=0):
    event(frame, "key-down", scan=scan, mod=mod)
    event(frame + 1, "key-up", scan=scan)


def point(frame, name):
    checkpoints.append(dict(frame=frame, name=name))


click(8, 552, 116)  # Select the frozen inferred house.
point(15, "selected")
click(20, 1050, 438)  # Focus Selected in the existing view title strip.
point(27, "focused")
click(30, 951, 438)  # Zoom -.
point(37, "zoom-out")
click(40, 997, 438)  # Zoom +.
point(47, "zoom-in")
event(50, "motion", x=800, y=620)
event(52, "wheel", amount=1)
point(58, "wheel")
key(60, 9)  # F focuses the selected object from the viewport.
point(66, "focus-key")
click(68, 1180, 438)  # Fly.
event(74, "motion", x=800, y=620)
event(76, "down", x=800, y=620, button=3)
point(79, "look-start")
event(81, "motion", x=820, y=606, rx=20, ry=-14)
point(85, "looked")
event(88, "key-down", scan=26)  # W.
event(98, "key-up", scan=26)
point(102, "walked")
event(105, "key-down", scan=225, mod=1)  # Left Shift.
event(108, "key-down", scan=26, mod=1)
event(118, "key-up", scan=26, mod=1)
event(120, "key-up", scan=225)
point(124, "fast")
event(127, "key-down", scan=8)  # E up.
event(137, "key-up", scan=8)
point(141, "up")
event(144, "key-down", scan=20)  # Q down.
event(154, "key-up", scan=20)
point(158, "down")
event(161, "up", x=820, y=606, button=3)
event(164, "key-down", scan=26)
event(171, "key-up", scan=26)
point(175, "released")
event(178, "down", x=820, y=606, button=3)
key(182, 41)  # Escape releases fly without deselecting the house.
event(185, "key-down", scan=26)
event(190, "key-up", scan=26)
event(192, "up", x=820, y=606, button=3)
point(197, "escape")
event(200, "down", x=820, y=606, button=3)
event(204, "focus-lost")
event(206, "key-down", scan=26)
event(211, "key-up", scan=26)
event(213, "up", x=820, y=606, button=3)
point(218, "focus-lost")
click(221, 1400, 119)  # The inspected group name field; typing owns camera keys.
point(227, "typing")
event(230, "motion", x=800, y=620)
event(232, "wheel", amount=2)
key(234, 9)  # F must not focus through a text editor.
event(237, "down", x=800, y=620, button=3)
event(240, "key-down", scan=26)
event(247, "key-up", scan=26)
event(249, "up", x=800, y=620, button=3)
point(253, "typing-blocked")
# Return leaves the name unchanged. Modal/handle guards are also exercised by
# the camera selfchecks and the existing authoring UI probe.
key(255, 40)
point(263, "final")
event(264, "quit")

# Exercise the other three movement keys independently, with equal-duration
# inverse pairs. Keep the rest of the inspected sequence unchanged.
for entry in events + checkpoints:
    if entry["frame"] >= 127:
        entry["frame"] += 80
event(127, "key-down", scan=22)  # S back.
event(137, "key-up", scan=22)
point(141, "backward")
event(144, "key-down", scan=26)  # W returns.
event(154, "key-up", scan=26)
point(158, "forward-return")
event(161, "key-down", scan=4)  # A left.
event(171, "key-up", scan=4)
point(175, "left")
event(178, "key-down", scan=7)  # D right returns.
event(188, "key-up", scan=7)
point(192, "right-return")

script.write_text(json.dumps(dict(frames=345, record=0, events=events,
    checkpoints=checkpoints, chapters=[]), indent=2), encoding="utf-8")
provenance = dict(status="RUNNING", executable=str(exe),
    executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
    script=str(script), script_sha256=hashlib.sha256(script.read_bytes()).hexdigest(),
    started_utc=datetime.now(timezone.utc).isoformat())
report.write_text(json.dumps(provenance, indent=2), encoding="utf-8")
environment = {k: v for k, v in os.environ.items() if not k.startswith("RUBYVR_")}
command = [str(exe), "--probe", str(repo / "tools/gui-probe-scene.json"),
    "--showcase", str(script), "--probe-out", str(prefix),
    "--out", str(build / "studio-camera-output.json")]
with prefix.with_suffix(".out.log").open("w") as stdout, prefix.with_suffix(".err.log").open("w") as stderr:
    subprocess.run(command, cwd=game, env=environment, stdin=subprocess.DEVNULL,
        stdout=stdout, stderr=stderr, check=True, timeout=180)
result = json.loads(prefix.with_suffix(".json").read_text())
points = {p["name"]: p for p in result["checkpoints"]}
checks = []


def require(value, label):
    if not value:
        raise SystemExit(f"[camera] FAIL: {label}; inspect {prefix}.json")
    checks.append(label)


def distance(a, b):
    return math.dist(points[a]["camera_target"], points[b]["camera_target"])


def same_pose(a, b):
    return (math.dist(points[a]["camera"], points[b]["camera"]) < 1e-5
            and distance(a, b) < 1e-5)


def eye(name):
    p = points[name]
    yaw, pitch, dist = p["camera"]
    tx, ty, tz = p["camera_target"]
    return (tx + math.sin(yaw)*math.cos(pitch)*dist, ty + math.sin(pitch)*dist,
            tz + math.cos(yaw)*math.cos(pitch)*dist)


require(result["ok"] and len(points) == len(checkpoints), "all SDL checkpoints completed")
require(points["focused"]["camera"][2] < points["selected"]["camera"][2]
    and points["focused"]["camera_target"][0] == 23, "visible Focus Selected frames the selected house")
require(points["zoom-out"]["camera"][2] > points["focused"]["camera"][2]
    and same_pose("zoom-in", "focused"), "visible zoom buttons move out and restore distance")
require(points["wheel"]["camera"][2] < points["zoom-in"]["camera"][2]
    and same_pose("focus-key", "focused"), "viewport wheel zooms and F restores focus")
require(points["look-start"]["fly_mode"] and points["look-start"]["fly_looking"], "Fly toggle and RMB capture are active")
require(points["look-start"]["camera"][:2] != points["looked"]["camera"][:2]
    and math.dist(eye("look-start"), eye("looked")) < 1e-4, "RMB relative look changes direction with a stationary eye")
require(distance("looked", "walked") > 1, "W moves forward while RMB is held")
require(abs(distance("walked", "fast") / distance("looked", "walked") - 4) < .05,
    "Shift moves four times farther over the same input duration")
require(distance("fast", "backward") > 1 and same_pose("fast", "forward-return"),
    "S moves backward and equal-duration W restores the position")
require(distance("forward-return", "left") > 1 and same_pose("fast", "right-return"),
    "A moves sideways and equal-duration D restores the position")
require(points["up"]["camera_target"][1] > points["fast"]["camera_target"][1]
    and same_pose("fast", "down"), "E and Q move vertically and return to the original altitude")
require(not points["released"]["fly_looking"] and same_pose("down", "released"),
    "releasing RMB stops movement even with W held")
require(not points["escape"]["fly_looking"] and same_pose("released", "escape")
    and points["escape"]["has_draft"], "Escape releases fly without clearing selection")
require(not points["focus-lost"]["fly_looking"] and same_pose("escape", "focus-lost"),
    "window focus loss releases movement")
require(points["typing"]["typing"] and same_pose("typing", "typing-blocked")
    and not points["typing-blocked"]["fly_looking"], "typing blocks wheel, F, RMB and movement keys")
baseline = points["selected"]
draft_path = lambda name: Path(f"{prefix}.png.{name}.draft.json")
baseline_document = json.loads(draft_path("selected").read_text())
for name, p in points.items():
    require(p["room_hash"] == baseline["room_hash"] and p["undo"] == baseline["undo"]
        and p["members"] == baseline["members"] and p["parts"] == baseline["parts"]
        and json.loads(draft_path(name).read_text()) == baseline_document,
        f"{name}: camera actions preserve geometry and the full authored draft/history")
report.write_text(json.dumps(dict(provenance, status="PASS", checks=checks,
    checkpoint_file=str(prefix.with_suffix(".json")), completed_utc=datetime.now(timezone.utc).isoformat()),
    indent=2), encoding="utf-8")
print(f"[camera] PASS checks={len(checks)}; hidden SDL zoom/focus/fly/input isolation")
print(f"[camera] report={report}")
