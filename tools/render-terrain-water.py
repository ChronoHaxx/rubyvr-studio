"""Actual Studio water-level comparison; generated packs remain local."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

from PIL import Image,ImageDraw,ImageFont

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/terrain-water'
BASE='0a5c2193bbc253a7995e9668e55324ab9f2c31b5'  # merged PR #27


def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    compiler=module('compiler','tools/build-terrain-region-example.py')
    old_recipe=json.loads(subprocess.check_output(['git','-c',f'safe.directory={ROOT.as_posix()}',
        '-C',str(ROOT),'show',BASE+':recipes/terrain-regions.json']))
    recipe=json.loads((ROOT/'recipes/terrain-regions.json').read_bytes())
    starter=json.loads((ROOT/'mod-assets/voxel-world-v6.json').read_bytes())
    hashes={}
    for name,r in [('before',old_recipe),('after',recipe)]:
        pack,_=compiler.build(r,starter)
        data=(json.dumps(pack,separators=(',',':'))+'\n').encode()
        (OUT/(name+'.json')).write_bytes(data);hashes[name]=hashlib.sha256(data).hexdigest()
    ui=module('ui','tools/test-studio-terrain.py');ui.BUILD=OUT/'captures'
    views=[
        ('pond','Route 102 pond','MAP_ROUTE102',dict(yaw=.5,pitch=.3,dist=9,tx=50,ty=1,tz=11)),
        ('pond-above','Route 102 pond: overhead approach','MAP_ROUTE102',dict(yaw=-.8,pitch=.8,dist=8,tx=50,ty=1,tz=11)),
        ('ocean','Route 103 shoreline','MAP_ROUTE103',dict(yaw=.7,pitch=.32,dist=11,tx=32,ty=1,tz=17)),
        ('ocean-reverse','Route 103 shoreline: reverse view','MAP_ROUTE103',dict(yaw=2.8,pitch=.35,dist=12,tx=34,ty=1,tz=17)),
    ]
    font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',21)
    small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',17)
    frames=[];captures=[]
    for name,title,map_id,camera in views:
        sheet=Image.new('RGB',(1124,350),'#131720');draw=ImageDraw.Draw(sheet)
        draw.text((12,5),title,font=font,fill='#eef2ff')
        for col,version in enumerate(('before','after')):
            result=ui.run(name+'-'+version,[dict(frame=22,type='quit')],[(18,'view')],source=OUT/(version+'.json'),
                map_id=map_id,width=1600,height=1100,frames=26,camera=camera)
            state=result['checkpoints'][0]
            assert state['mode']==3 and state['terrain_rejected']==0
            im=Image.open(OUT/f'captures/{name}-{version}.png.view.png').convert('RGB').crop((208,516,1332,1028))
            im.save(OUT/(name+'-'+version+'.png'))
            sheet.paste(im.resize((562,256),Image.Resampling.NEAREST),(col*562,65))
            draw.text((col*562+12,37),('Before: water follows the land grade','After: level water + flat shore contact')[col],font=small,fill='#eef2ff')
            captures.append(dict(view=name,version=version,room_hash=state['room_hash'],terrain_rejected=0))
        draw.text((12,326),'Actual Studio renders | same source art and camera | authored water level: 16 px',font=small,fill='#b9c5d5')
        sheet.save(OUT/(name+'-acceptance.png'));frames.append(sheet)
    target=ROOT/'docs/media/terrain-water.gif'
    frames[0].save(target,save_all=True,append_images=frames[1:],duration=3000,loop=0,disposal=2)
    record=dict(duration_seconds=12,frames=len(frames),baseline_commit=BASE,pack_sha256=hashes,
        gui_sha256=hashlib.sha256((ROOT/'build/rubyvr_gui.exe').read_bytes()).hexdigest(),
        gif_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),captures=captures)
    (OUT/'visual-verification.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print(f'PASS: {len(captures)} actual captures; {target}')


if __name__=='__main__': main()
