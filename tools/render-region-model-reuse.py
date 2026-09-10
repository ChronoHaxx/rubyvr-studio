"""Make a short GIF from verified production captures; no invented world views."""
import hashlib
import importlib.util
import json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/region-model-reuse'


def main():
    report=json.loads((OUT/'verification.json').read_bytes())
    assert report['status']=='PASS'
    assert hashlib.sha256((ROOT/'build/rubyvr_gui.exe').read_bytes()).hexdigest()==report['gui_sha256']
    spec=importlib.util.spec_from_file_location('reuse',ROOT/'tools/test-region-model-reuse.py')
    test=importlib.util.module_from_spec(spec);spec.loader.exec_module(test)
    north=json.loads((OUT/'north-flight.json').read_bytes())['checkpoints']
    west=json.loads((OUT/'west-flight.json').read_bytes())['checkpoints']
    samples=[('north-flight',s) for s in north if s['name']!='overview']
    samples += [('west-flight',s) for s in west if s['name']!='crossed']
    samples.append(('north-flight',north[-1]))
    frames=[];durations=[]
    font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',22)
    small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',17)
    for prefix,s in samples:
        image=Image.open(OUT/f'{prefix}.png.{s["name"]}.png').convert('RGB')
        x,y,w,h=map(int,s['view']);crop=image.crop((x,y,x+w,y+h))
        frame=Image.new('RGB',(w,h+70),'#131720');frame.paste(crop,(0,42));draw=ImageDraw.Draw(frame)
        ex,_,ez=test.eye(s)
        if prefix=='west-flight':title='Route 102 -> Petalburg: the offset connection stays joined'
        elif s['name']=='overview':title='Six full maps in one view | 69.4 MiB of mesh + indexed art'
        else:
            place='Littleroot' if ez>47 else 'Route 101' if ez>27 else 'Oldale' if ez>7 else 'Route 103'
            title=f'Flying north: {place} | One continuous camera, no map switch'
        draw.text((12,6),title,font=font,fill='#eef2ff')
        draw.text((12,h+46),'Actual Studio render | Shared model meshes | Desktop preview; outer edges and M4 foliage still unfinished',font=small,fill='#b9c5d5')
        frame.save(OUT/f'film-{prefix}-{s["name"]}.png');frames.append(frame)
        durations.append(220 if s['name'].startswith('travel-') else 2500 if s['name']=='overview' else 1400)
    target=ROOT/'docs/media/region-model-reuse.gif'
    frames[0].save(target,save_all=True,append_images=frames[1:],duration=durations,loop=0,disposal=2)
    sheet=Image.new('RGB',(1124,240*((len(frames)+2)//3)),'#10151c')
    for i,frame in enumerate(frames):
        thumb=frame.copy();thumb.thumbnail((370,206));left=(i%3)*374;top=(i//3)*240
        sheet.paste(thumb,(left,top));ImageDraw.Draw(sheet).text((left+4,top+210),f'{i+1}: {samples[i][1]["name"]}',font=small,fill='white')
    sheet.save(OUT/'contact.png')
    result=dict(gui_sha256=report['gui_sha256'],input_sha256=report['input_sha256'],
        gif_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),frames=len(frames),duration_seconds=sum(durations)/1000,
        scope='Actual SDL input and sampled production frames, with pause durations; no generated/interpolated world views')
    (OUT/'visual-verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
