"""Caption actual SDL captures from test-studio-terrain.py; no mock renderer."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'build/terrain-review'
OUTPUT=ROOT/'docs/media/terrain-acceptance.gif'
font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',21)
small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',17)


def frame(name,title,detail):
    # The production DIORAMA viewport, cropped for a readable short review.
    capture=Image.open(SOURCE/name).convert('RGB').crop((208,516,1332,1028))
    canvas=Image.new('RGB',(1124,596),'#131720')
    canvas.paste(capture,(0,48))
    draw=ImageDraw.Draw(canvas)
    draw.text((16,10),title,font=font,fill='#f1f5ff')
    draw.text((16,570),detail,font=small,fill='#c7d5e9')
    return canvas.quantize(colors=256)


def main():
    pictures,durations=[],[]
    def add(name,title,detail,duration):
        pictures.append(frame(name,title,detail));durations.append(duration)
    add('seam-before.png.front.png','Before: the ledge drawing lies on a flat floor',
        'Actual Studio capture | Route 101, looking toward Oldale',3000)
    add('seam-after.png.front.png','Authored example: two ledges, with continuous land around their ends',
        'Two 8 px drops connect the 16 px northern surface to the 0 px southern boundary.',4500)
    for f in range(55,131,5):
        add(f'seam-after.png.orbit-{f}.png','Inspect the connected upper surface from another angle',
            '746 non-cliff edges share heights. Complete geography and scenery art still need review.',250)
    add('seam-after.png.reverse.png','Terrain foundation verified; complete map reconstruction remains in M2',
        '394 source maps checked | 40 matching seam edges in both views | desktop only',4500)
    OUTPUT.parent.mkdir(parents=True,exist_ok=True)
    pictures[0].save(OUTPUT,save_all=True,append_images=pictures[1:],duration=durations,loop=0,optimize=False,disposal=2)
    print(f'{OUTPUT}: {sum(durations)/1000:g} seconds, {OUTPUT.stat().st_size:,} bytes')


if __name__=='__main__':
    main()
