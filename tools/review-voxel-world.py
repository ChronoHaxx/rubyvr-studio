"""Verify the scenery pack with the production batch mesher and hidden Studio.

This records actual rendered views, source-role agreement, exact save/reopen,
closed geometry and matching across every map. It does not infer visual approval.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw

ROOT=Path(__file__).resolve().parents[1]
GAME=ROOT


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack",type=Path,default=ROOT/"mod-assets/voxel-world-v6.json")
    parser.add_argument("--out",type=Path,default=GAME/"build/voxel-world-review/final")
    parser.add_argument("--gui",type=Path,default=GAME/"build/rubyvr_gui.exe")
    args=parser.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    pack=json.loads(args.pack.read_text())
    immutable=out/"input.json";immutable.write_text(json.dumps(pack,indent=2)+"\n")
    env={k:v for k,v in os.environ.items() if not k.startswith("RUBYVR_")}
    def run(executable,arguments,label,timeout=600):
        with (out/(label+".out.log")).open("w") as so,(out/(label+".err.log")).open("w") as se:
            binary=args.gui if executable=="rubyvr_gui.exe" else GAME/"build"/executable
            result=subprocess.run([str(binary),*map(str,arguments)],cwd=GAME,env=env,
                stdin=subprocess.DEVNULL,stdout=so,stderr=se,timeout=timeout)
        print(f"{label}: exit {result.returncode}",flush=True)
        return result.returncode
    audit_code=run("rubyvr_studio.exe",["--audit-assets",out/"topology.json","--overrides",immutable],"audit")
    if audit_code:
        raise SystemExit(f"Asset audit failed ({audit_code}); see {out/'audit.err.log'}. Rebuild the matching target; see docs/building.md.")
    render_dir=out/"renders"
    render_code=run("rubyvr_gui.exe",["--map","MAP_OLDALE_TOWN","--overrides",immutable,"--asset-review",render_dir],"renders")
    if render_code:
        raise SystemExit(f"Asset rendering failed ({render_code}); see {out/'renders.err.log'}.")
    topology=json.loads((out/"topology.json").read_text())
    rendered=json.loads((render_dir/"renders.json").read_text())
    sources=GAME/"build/voxel-world-source"
    masks=True
    for pattern in pack["patterns"]:
        for role in ("object","shadow"):
            filename=f"{pattern['id']}-{role}.png"
            if (sources/filename).exists():
                expected=Image.open(sources/filename).convert("RGB")
                actual=Image.open(render_dir/filename).convert("RGB")
                masks &= expected.size==actual.size and expected.tobytes()==actual.tobytes()
    # A fixed scale in the isolated captures makes dimensions comparable.
    for page,start in enumerate(range(0,len(pack["patterns"]),16),1):
        sheet=Image.new("RGB",(1280,1408),(16,19,27));draw=ImageDraw.Draw(sheet)
        for i,pattern in enumerate(pack["patterns"][start:start+16]):
            x,y=i%4*320,i//4*352
            draw.text((x+10,y+8),pattern["name"][:44],fill=(235,239,245))
            path=render_dir/(pattern["id"]+"-oblique.png")
            if path.exists():
                shot=Image.open(path).resize((320,320),Image.Resampling.NEAREST)
                sheet.paste(shot,(x,y+28))
        sheet.save(out/f"models-{page:02}.png")
    # The full pack also goes through ordinary scene loading and the real UI.
    town_results=[]
    maps={m["id"]:m for m in json.loads((GAME/"build/sprite-catalog/catalog.json").read_text())["maps"]}
    for room in ("MAP_OLDALE_TOWN","MAP_LITTLEROOT_TOWN","MAP_PETALBURG_CITY","MAP_RUSTBORO_CITY"):
        name=room.removeprefix("MAP_").lower();prefix=out/name
        scenario=json.loads((ROOT/"tools/gui-probe-scene.json").read_text())
        scenario["id"]="voxel-world-"+name
        width,height=maps[room]["width"]+15,maps[room]["height"]+14
        scenario["camera"].update(yaw=.35,pitch=.9,dist=max(width,height)*.65,tx=width/2,ty=0,tz=height/2)
        scenario_path=out/(name+".scenario.json");scenario_path.write_text(json.dumps(scenario))
        events=[dict(frame=5,type="motion",x=629,y=18),dict(frame=6,type="down",x=629,y=18),
                dict(frame=7,type="up",x=629,y=18),dict(frame=13,type="motion",x=1250,y=930),dict(frame=32,type="quit")]
        script=out/(name+".events.json")
        script.write_text(json.dumps(dict(frames=33,record=0,events=events,checkpoints=[dict(frame=25,name="town")],
            chapters=[dict(frame=0,text="VOXEL SCENERY / complete pack in the actual editor")])))
        code=run("rubyvr_gui.exe",["--map",room,"--mode","diorama","--overrides",immutable,"--probe",scenario_path,
            "--showcase",script,"--probe-out",prefix,"--out",out/(name+"-user-copy.json")],name)
        state=json.loads(prefix.with_suffix(".json").read_text())
        checkpoint=state["checkpoints"][0]
        town_results.append(dict(room=room,passed=code==0 and state["ok"] and checkpoint["mode"]==3,
            screenshot=str(prefix)+".png.town.png",geometry=checkpoint.get("room_hash",checkpoint.get("geom"))))
    accepted=[p for a in topology["assets"] for p in a["accepted"]]
    primary=[p for p in accepted if not p["crosses_layout_boundary"]]
    report=dict(status="PASS" if audit_code==render_code==0 and masks and all(t["passed"] for t in town_results) else "FAIL",
        pack_sha256=hashlib.sha256(args.pack.read_bytes()).hexdigest(),
        gui_sha256=hashlib.sha256(args.gui.read_bytes()).hexdigest(),
        batch_sha256=hashlib.sha256((GAME/"build/rubyvr_studio.exe").read_bytes()).hexdigest(),
        patterns=len(pack["patterns"]),maps_checked=topology["maps_checked"],failed_maps=topology["failed_maps"],
        full_map_placements=len(primary),connection_padding_placements=len(accepted)-len(primary),
        maps_with_full_placements=len({p["map"] for p in primary}),source_masks_match_production=masks,
        exact_resave=all(a["exact_resave"] for a in rendered["assets"]),
        closed_connected=all(a["closed_connected"] for a in topology["assets"] if not a.get("ground_only",False)),
        ground_only_empty=all(a["triangles"]==0 for a in topology["assets"] if a.get("ground_only",False)),
        rendered_patterns=len(rendered["assets"]),towns=town_results,visual_review="pending",headset_tested=False)
    report["status"]="PASS" if report["status"]=="PASS" and report["rendered_patterns"]==len(pack["patterns"]) and report["exact_resave"] and report["closed_connected"] and report["ground_only_empty"] else "FAIL"
    (out/"report.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2),flush=True)
    raise SystemExit(0 if report["status"]=="PASS" else 1)


if __name__=="__main__":main()
