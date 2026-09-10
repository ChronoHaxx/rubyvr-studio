"""Paired actual Studio views of the tested foundation action."""
import hashlib
import importlib.util
import json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/terrain-foundations'


def main():
    report=json.loads((OUT/'verification.json').read_bytes())
    assert report['status']=='PASS'
    assert hashlib.sha256((ROOT/'build/rubyvr_gui.exe').read_bytes()).hexdigest()==report['gui_sha256']
    spec=importlib.util.spec_from_file_location('ui',ROOT/'tools/test-studio-terrain.py')
    ui=importlib.util.module_from_spec(spec);spec.loader.exec_module(ui);ui.BUILD=OUT/'views'
    font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',21)
    small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',17)
    frames=[];captures=[]
    for name,yaw,pitch,dist in [('Front / right',.7,.28,8),('Rear',3.65,.9,7),('Front / left',-.9,.28,8)]:
        sheet=Image.new('RGB',(1124,350),'#131720');draw=ImageDraw.Draw(sheet)
        draw.text((12,5),'Level foundation | controlled slope test | '+name,font=font,fill='#eef2ff')
        for col,(label,path) in enumerate([('Before','slope.json'),('After','wide-saved.json')]):
            slug=name.split()[0].lower()+('-right' if name=='Front / right' else '-left' if name=='Front / left' else '')+'-'+label.lower()
            result=ui.run(slug,[dict(frame=24,type='quit')],[(18,'view')],source=OUT/path,height=1100,frames=28,
                          camera=dict(yaw=yaw,pitch=pitch,dist=dist,tx=13,ty=2,tz=13))
            state=result['checkpoints'][0]
            assert not state['terrain_rejected'] and not state['unresolved_placements']
            frame=Image.open(OUT/f'views/{slug}.png.view.png').convert('RGB').crop((208,516,1332,1028))
            sheet.paste(frame.resize((562,256),Image.Resampling.NEAREST),(col*562,65))
            draw.text((col*562+12,37),('Before: sloping ground intersects the base','After: flat pad; house shape preserved')[col],font=small,fill='#eef2ff')
            captures.append(dict(view=name,state=label,room_hash=state['room_hash']))
        draw.text((12,326),'Actual Studio | Select model > Level foundation | Test slope, not proposed Oldale geography',font=small,fill='#b9c5d5')
        sheet.save(OUT/(slug.rsplit('-',1)[0]+'-comparison.png'));frames.append(sheet)
    target=ROOT/'docs/media/terrain-foundations.gif'
    frames[0].save(target,save_all=True,append_images=frames[1:],duration=4000,loop=0,disposal=2)
    record=dict(duration_seconds=12,frames=3,captures=captures,gif_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                gui_sha256=report['gui_sha256'])
    (OUT/'visual-verification.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print('PASS: six actual renders; 12-second before/after GIF')


if __name__=='__main__':main()
