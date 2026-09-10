"""Capture an actual 12-second desktop comparison, with local game assets.

Only crops/captions the production SDL output. This is not a traversal demo.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

from PIL import Image,ImageDraw,ImageFont

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/terrain-regions'


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    for command in ([sys.executable,'tools/terrain-seam-fixture.py','--out',str(OUT/'before.json')],
                    [sys.executable,'tools/build-terrain-region-example.py']):
        subprocess.run(command,cwd=ROOT,check=True)
    spec=importlib.util.spec_from_file_location('ui',ROOT/'tools/test-studio-terrain.py')
    ui=importlib.util.module_from_spec(spec);spec.loader.exec_module(ui);ui.BUILD=OUT/'acceptance'
    views=[
        ('north','Oldale north entrance','MAP_OLDALE_TOWN',dict(yaw=3.14,pitch=.55,dist=11,tx=17,ty=.8,tz=7.3)),
        ('west','Oldale west entrance','MAP_OLDALE_TOWN',dict(yaw=-1.57,pitch=.5,dist=11,tx=7,ty=.8,tz=17)),
        ('east-ledge','Route 102: east-facing ledge','MAP_ROUTE102',dict(yaw=.7,pitch=.75,dist=14,tx=39,ty=1.4,tz=13)),
        ('terraces','Route 103: western terraces','MAP_ROUTE103',dict(yaw=.35,pitch=.75,dist=17,tx=19,ty=1.5,tz=15)),
    ]
    font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',21)
    small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',17)
    frames=[];reports=[]
    for name,title,map_id,camera in views:
        sheet=Image.new('RGB',(1124,350),'#131720');draw=ImageDraw.Draw(sheet)
        draw.text((12,5),title,font=font,fill='#eef2ff')
        for col,version in enumerate(('before','regions')):
            result=ui.run(name+'-'+version,[dict(frame=22,type='quit')],[(18,'view')],source=OUT/(version+'.json'),
                map_id=map_id,width=1600,height=1100,frames=26,camera=camera)
            state=result['checkpoints'][0]
            assert state['mode']==3 and state['terrain_rejected']==0
            im=Image.open(OUT/f'acceptance/{name}-{version}.png.view.png').convert('RGB').crop((208,516,1332,1028))
            sheet.paste(im.resize((562,256),Image.Resampling.NEAREST),(col*562,65))
            draw.text((col*562+12,37),('Before: PR #26 example','After: connected region example')[col],font=small,fill='#eef2ff')
            reports.append(dict(view=name,version=version,room_hash=state['room_hash'],terrain_rejected=0))
        draw.text((12,326),'Actual Studio renders | authored heights | foliage art and coastal geography remain unfinished',font=small,fill='#b9c5d5')
        sheet.save(OUT/(name+'-acceptance.png'));frames.append(sheet)
    target=ROOT/'docs/media/terrain-regions.gif'
    frames[0].save(target,save_all=True,append_images=frames[1:],duration=3000,loop=0,disposal=2)
    report=dict(duration_seconds=12,frames=len(frames),capture='actual SDL/production mesher; crops, captions and paused comparison views',
        gui_sha256=hashlib.sha256((ROOT/'build/rubyvr_gui.exe').read_bytes()).hexdigest(),
        pack_sha256=hashlib.sha256((OUT/'regions.json').read_bytes()).hexdigest(),
        gif_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),checkpoints=reports)
    (OUT/'visual-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'PASS: {len(reports)} actual application captures; {target}')


if __name__=='__main__': main()
