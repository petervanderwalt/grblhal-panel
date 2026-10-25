"""Check palette text contrast and reject the retired purple accents."""
import re
from pathlib import Path
root=Path(__file__).resolve().parent.parent/'firmware/viewesmart-p4/main'
source=(root/'panel_ui.cpp').read_text(encoding='utf8')
colors={k:int(v,16) for k,v in re.findall(r'\b(BG|CARD|EDGE|TEXT|MUTED|PRIMARY|RED|GREEN|AMBER|WHITE|LINE|DISABLED|TRACK)\s*=\s*0x([A-Fa-f0-9]{6})',source)}
def luminance(c):
 values=[(c>>s&255)/255 for s in (16,8,0)]
 values=[x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in values]
 return sum(a*b for a,b in zip(values,[.2126,.7152,.0722]))
def ratio(a,b):
 x,y=sorted((luminance(a),luminance(b)));return (y+.05)/(x+.05)
for front,back in [('WHITE','PRIMARY'),('WHITE','GREEN'),('WHITE','RED'),('TEXT','AMBER'),('MUTED','CARD'),('MUTED','BG'),('MUTED','DISABLED'),('PRIMARY','CARD'),('RED','CARD'),('WHITE','EDGE')]:
 value=ratio(colors[front],colors[back]);assert value>=4.5,(front,back,value)
 print(f'{front}/{back}: {value:.2f}:1')
assert ratio(0x8A5500,colors['CARD'])>=4.5
assert ratio(colors['TRACK'],colors['CARD'])>=3
# Blue button's lightest gradient endpoint also carries small white labels.
blue=colors['PRIMARY'];light=[round((blue>>s&255)*(243/255)+255*(12/255)) for s in (16,8,0)]
light=light[0]<<16|light[1]<<8|light[2];assert ratio(colors['WHITE'],light)>=4.5
for p in [root/'panel_ui.cpp',*root.glob('*_ui.inc'),root/'panel_actuator_web.cpp']:
 assert not re.search(r'5356c9|e8e9fa|e6eafe|8b5195|"Purple"',p.read_text(encoding='utf8'),re.I),p
print('PASS semantic text pairs, blue gradient and retired-purple audit; screenshots still require visual inspection')
