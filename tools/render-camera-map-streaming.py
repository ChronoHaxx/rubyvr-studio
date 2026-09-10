"""Capture and inspectable GIF of actual SDL flight with a moving resident window."""
import hashlib
import importlib.util
import json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/camera-map-streaming'


def main():
    verified=json.loads((OUT/'verification.json').read_bytes())
    assert verified['status']=='PASS'
    assert hashlib.sha256((ROOT/'build/rubyvr_gui.exe').read_bytes()).hexdigest()==verified['gui_sha256']
    spec=importlib.util.spec_from_file_location('streaming',ROOT/'tools/test-camera-map-streaming.py')
    stream=importlib.util.module_from_spec(spec);spec.loader.exec_module(stream)
    test=stream.test;test.OUT=OUT
    events=[];test.ui.click(events,6,470,501);test.ui.click(events,12,460,564)
    stream.flight(events,35,120,4);stream.flight(events,170,255,7)
    test.ui.click(events,280,1470,224)
    for frame in (285,290,295):test.ui.click(events,frame,997,501)
    events.append(dict(frame=320,type='quit'))
    points=[(18,'oldale')]+[(f,'west-'+str(f)) for f in range(40,120,5)]+[(140,'petalburg')]
    points += [(f,'east-'+str(f)) for f in range(175,255,5)]+[(270,'oldale-again'),(310,'overview')]
    states=test.run('visual',events,points,frames=330,connected=True,
        camera=dict(yaw=0,pitch=.85,dist=8,tx=17,ty=8,tz=10.7))
    assert states['petalburg']['stream_anchor']=='MAP_PETALBURG_CITY' and states['petalburg']['region_maps']==3
    assert states['oldale-again']['stream_anchor']=='MAP_OLDALE_TOWN' and states['oldale-again']['region_maps']==6
    assert states['oldale-again']['region_hash']==states['oldale']['region_hash']
    original=(OUT/'visual.png.oldale.working.json').read_bytes()
    frames=[];durations=[]
    font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',22)
    small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',17)
    for s in states.values():
        assert s['exploring'] and not s['stream_errors'] and not s['unsaved'] and not s['draft_dirty']
        assert (OUT/f'visual.png.{s["name"]}.working.json').read_bytes()==original
        image=Image.open(OUT/f'visual.png.{s["name"]}.png').convert('RGB')
        x,y,w,h=map(int,s['view']);crop=image.crop((x,y,x+w,y+h))
        frame=Image.new('RGB',(w,h+96),'#131720');frame.paste(crop,(0,66));draw=ImageDraw.Draw(frame)
        direction='Flying west toward Petalburg' if s['name'].startswith('west-') else 'Flying back to Oldale'
        if s['name']=='oldale':direction='Oldale: fly across the connected world'
        elif s['name']=='petalburg':direction='Petalburg: distant maps released'
        elif s['name']=='oldale-again':direction='Oldale: six maps restored at the same positions'
        elif s['name']=='overview':direction='The connected area | authored terrain and shared scenery'
        draw.text((12,5),direction,font=font,fill='#eef2ff')
        status='Preparing nearby maps in background' if s['stream_pending'] else 'Nearby maps ready'
        draw.text((12,35),f'{s["region_maps"]} maps resident | {s["region_bytes"]/1048576:.1f} MiB mesh + art | {status}',font=small,fill='#bedebd')
        draw.text((12,h+71),'Actual Studio frames, sampled with pauses | M2 desktop preview | Outer borders and M4 foliage unfinished',font=small,fill='#b9c5d5')
        frame.save(OUT/f'film-{s["name"]}.png');frames.append(frame)
        durations.append(170 if s['name'].startswith(('west-','east-')) else 1800 if s['name']=='overview' else 1400)
    target=ROOT/'docs/media/camera-map-streaming.gif'
    frames[0].save(target,save_all=True,append_images=frames[1:],duration=durations,loop=0,disposal=2)
    sheet=Image.new('RGB',(1200,215*((len(frames)+2)//3)),'#10151c')
    for i,frame in enumerate(frames):
        thumb=frame.copy();thumb.thumbnail((395,185));left=(i%3)*400;top=(i//3)*215
        sheet.paste(thumb,(left,top));ImageDraw.Draw(sheet).text((left+4,top+187),f'{i+1}: {points[i][1]}',font=small,fill='white')
    sheet.save(OUT/'visual-contact.png')
    report=dict(status='PASS',gui_sha256=verified['gui_sha256'],input_sha256=verified['input_sha256'],
        gif_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),frames=len(frames),duration_seconds=sum(durations)/1000,
        captures=[dict(name=s['name'],maps=s['region_maps'],bytes=s['region_bytes'],anchor=s['stream_anchor'],pending=s['stream_pending']) for s in states.values()],
        scope='Actual SDL input and production frames sampled with pauses; no interpolated views or real-time FPS claim')
    (OUT/'visual-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
