"""Check cached display-SD views and both Files sources with UART-off feedback."""
import argparse
import importlib.util
import json
from pathlib import Path
import time
import serial

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',default='COM12')
parser.add_argument('--file',default='/arc-ijk.gcode')
parser.add_argument('--captures',type=Path,default=Path('captures/cached-preview'))
parser.add_argument('--no-captures',action='store_true')
args=parser.parse_args()
spec=importlib.util.spec_from_file_location('remote',Path(__file__).with_name('ui-remote.py'))
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
args.captures.mkdir(parents=True,exist_ok=True)
with serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5) as p, (args.captures/'serial.log').open('wb') as trace:
 original_readline=p.readline
 def traced_readline(*a,**kw):
  line=original_readline(*a,**kw);trace.write(line);trace.flush();return line
 p.readline=traced_readline
 p.dtr=p.rts=False;p.port=args.port;p.open()
 for _ in range(25):
  p.write(b'\nUG SYNC\n');deadline=time.monotonic()+1
  while time.monotonic()<deadline:
   if b'@UGERR command' in p.readline():break
  else:continue
  break
 else:raise TimeoutError('Console sync')
 time.sleep(.8);p.reset_input_buffer()
 def cmd(s):return r.debug(p,s)
 def data(s='STATE'):return json.loads(cmd(s).removeprefix('@UGDATA '))
 def tap(x,y):r.exchange(p,'tap',point=(x,y));time.sleep(.4)
 def location(local):
  tap(896,564);tap(830,266);tap(830,313 if local else 350);tap(384,564)
  assert data('SD STATE')['local']==local
 def choose_view(view):
  tap(885,190)
  cmd('SWIPE 890 260 890 440' if view<6 else 'SWIPE 890 440 890 260')
  time.sleep(.3)
  tap(885,246+38*view if view<6 else 401+38*(view-6))
  assert data('PREFS')['preview_view']==view
 def shot(name):
  if not args.no_captures:r.exchange(p,'shot',args.captures/(name+'.png'))
 try:
  cmd('DEMO ON');cmd('TOUCH OFF');time.sleep(1)
  assert not data()['job'],'Stop the current demo job first'
  if data('PREFS').get('local_preview'):tap(56,112)
  tap(384,564)
  if not data('SD STATE')['local']:location(True)
  sd=data('SD STATE')
  for _ in range(sd['page']):tap(120,478)
  for _ in range(128):
   sd=data('SD STATE')
   found=next((i for i,f in enumerate(sd['files']) if f['path'].lower()==args.file.lower()),None)
   if found is not None:break
   assert sd['page']*4+4<sd['file_count'],args.file
   tap(900,478)
  else:raise AssertionError('File not found')
  shot('panel-display-files')
  tap(778,189+72*found)
  for _ in range(300):
   sd=data('SD STATE')
   if sd['files'][found]['preview']:break
   time.sleep(2)
  assert sd['files'][found]['preview'],sd
  assert data('PREFS')['local_preview'] and not data()['job']
  choose_view(6)
  shot('panel-preview-isometric')
  choose_view(7)
  shot('panel-preview-perspective')
  choose_view(0)
  shot('panel-preview-top')
  for view in range(8):choose_view(view)
  cmd('SWIPE 350 300 400 320');time.sleep(.4)
  assert data('PREFS')['local_preview'] and not data()['job']
  tap(56,112);location(False)
  assert not data('SD STATE')['local']
  time.sleep(.5);assert data()['files']==3
  shot('panel-controller-files')
  tap(896,564);shot('panel-settings');tap(380,96);shot('panel-preview-server')
  tap(940,55);location(True)
  assert data('SD STATE')['local']
  print('PASS all eight cached views, image pan, separate controller listing and preview-server UI',flush=True)
 finally:cmd('TOUCH ON')
