"""Capture Jog states, SD listings and cached previews for the current README theme."""
import argparse, importlib.util, json, time
from pathlib import Path
import serial
spec=importlib.util.spec_from_file_location('remote',Path(__file__).with_name('ui-remote.py'))
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
a=argparse.ArgumentParser(description=__doc__)
a.add_argument('--port',default='COM12')
a.add_argument('--captures',type=Path,default=Path('captures/readme-refresh'))
a.add_argument('--only',nargs='*',help='Capture only these names; still check all navigation')
args=a.parse_args();args.captures.mkdir(parents=True,exist_ok=True)
p=serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5);p.dtr=p.rts=False;p.port=args.port

def cmd(t):return r.debug(p,t)
def data(k='PREFS'):return json.loads(cmd(k).removeprefix('@UGDATA '))
def tap(x,y):r.exchange(p,'tap',point=(x,y));time.sleep(.55)
def idle():cmd('RX <Idle|MPos:125,84.5,-12|WCO:100,50,-20|FS:0,0|Pn:i>');time.sleep(.2)
def form(kind):
 d=data();assert d['form_kind']==kind,d
 assert (d['form_w'],d['form_h'])==(1024,600),d
 return d

def shot(name):
 print('CHECK',name,flush=True)
 if args.only is not None and name not in args.only:return
 time.sleep(1.5);r.exchange(p,'shot',args.captures/(name+'.png'))

def clear_form():
 for _ in range(20):
  kind=data()['form_kind']
  if kind=='none':return
  tap(264,520) if kind=='confirmation' else tap(822,520) if kind in ('notice','preview-update') else tap(976,32)
 assert data()['form_kind']=='none'

def page(index):
 clear_form()
 if index==2 and data()['page']==2:tap(154,566)
 tap({0:154,1:333,2:870,3:512,6:691}[index],566)
 assert data()['page']==index
 idle()

def scroll(target):
 for _ in range(5):
  delta=target-data()['scroll_y']
  if abs(delta)<32:return
  step=max(-260,min(260,delta));start=440 if step>0 else 150
  cmd(f'SWIPE 940 {start} 940 {start-step}');time.sleep(.4)

def setting(local):tap(850,round(70+local-data()['scroll_y']))
def location(local):
 page(2);tap(830,266);tap(830,313 if local else 350);page(1)
 assert bool(data('SD STATE')['local'])==local

def find_file(predicate):
 sd=data('SD STATE')
 for _ in range(sd['page']):tap(104,494)
 for _ in range(40):
  sd=data('SD STATE')
  found=next((i for i,f in enumerate(sd['files']) if predicate(f)),None)
  if found is not None:return found,sd['files'][found]
  assert (sd['page']+1)*4<sd['file_count'],'Requested file not found'
  tap(916,494)
 raise AssertionError('File scan exhausted')

def preview(slot):
 tap(766,137+88*slot);assert data()['local_preview']
 for _ in range(30):
  if data('SD STATE')['preview_viewing']:return
  time.sleep(.3)

def view(which):
 tap(885,190)
 cmd('SWIPE 890 260 890 440' if which<6 else 'SWIPE 890 440 890 260');time.sleep(.3)
 tap(885,246+38*which if which<6 else 401+38*(which-6))
 assert data()['preview_view']==which

original=None;source=None
try:
 p.open();r.buffered_lines(p)
 for _ in range(15):
  p.write(b'\nUG SYNC\n');end=time.monotonic()+1
  while time.monotonic()<end:
   if b'@UGERR command' in p.readline():break
  else:continue
  break
 time.sleep(.3);p.reset_input_buffer();cmd('DEMO ON');cmd('TOUCH OFF');idle();clear_form();original=data();source=bool(data('SD STATE')['local'])
 assert not original['actuator_mode'] and not original['inches'] and not original['continuous'],'Start in metric incremental CNC mode'
 page(0);tap(638,154);assert not data()['continuous'];shot('panel-jog')
 tap(870,154);assert data()['continuous'];shot('panel-jog-continuous');tap(638,154)
 cmd('RX <Hold:0|MPos:125,84.5,-12|WCO:100,50,-20|FS:0,0>');shot('panel-jog-disabled')
 cmd('DEMO OFF');time.sleep(3);shot('panel-jog-disconnected');cmd('DEMO ON');idle()
 page(2);tap(912,442);assert data()['inches'];page(0);shot('panel-jog-inch');page(2);tap(775,442);assert not data()['inches'];shot('panel-settings')
 page(3);shot('panel-macros')
 location(False);time.sleep(1);assert data('STATE')['files']==3;shot('panel-controller-files')
 location(True);time.sleep(.5);shot('panel-display-files')
 idx,f=find_file(lambda f: f['path'].lower().endswith('/mb_83.nc'))
 print('PREVIEW FILE',f,flush=True);preview(idx);assert data('SD STATE')['preview_viewing'],'Run Update previews before capturing cached images'
 view(6);shot('panel-preview-isometric');view(7);shot('panel-preview-perspective');view(0);shot('panel-preview-top');cmd('SWIPE 350 300 430 340');shot('panel-preview-pan');view(original['preview_view']);tap(56,112);assert not data()['local_preview']
 idx,f=find_file(lambda f: f['path'].lower()=='/rotary/3d_finish_1_1.gcode')
 print('FALLBACK FILE',f,flush=True);tap(766,137+88*idx);assert data()['local_preview'];time.sleep(1)
 assert not data('SD STATE')['preview_viewing'];shot('panel-preview-text');tap(56,112)
 location(source);page(0)
 for key in ('x','y','z','diameter','feed','inset','tx','rx','baud','inches','macro_count','brightness','a_rotary','actuator_mode'):
  assert data()[key]==original[key],key
 print('PASS current Jog states, both SD sources, cached dark views, text fallback and unchanged preferences',flush=True)
finally:
 if p.is_open:
  try:
   clear_form()
   if data()['local_preview']:tap(56,112)
   cmd('DEMO ON');idle()
   if original and data()['inches']!=original['inches']:page(2);tap(912 if original['inches'] else 775,442)
   if source is not None and bool(data('SD STATE')['local'])!=source:location(source)
   page(0)
  finally:
   try:cmd('TOUCH ON')
   finally:p.close()
