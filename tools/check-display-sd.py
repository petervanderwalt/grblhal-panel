"""Check actual display-SD files and isolated streaming on the development pendant."""
import argparse
import importlib.util
import json
from pathlib import Path
import time
import serial

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',default='COM12')
parser.add_argument('--captures',type=Path,default=Path('captures/display-sd'))
parser.add_argument('--no-captures',action='store_true')
args=parser.parse_args()
spec=importlib.util.spec_from_file_location('remote',Path(__file__).with_name('ui-remote.py'))
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
with serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5) as p:
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
 def shot(name):
  if not args.no_captures:r.exchange(p,'shot',args.captures/(name+'.png'))
 def find(path):
  sd=data('SD STATE')
  for _ in range(sd['page']):tap(120,478)
  for _ in range(128):
   sd=data('SD STATE')
   for i,file in enumerate(sd['files']):
    if file['path']==path:return i
   assert sd['page']*4+4<sd['file_count'],path
   tap(900,478)
  raise AssertionError('Catalogue page limit')
 try:
  cmd('DEMO ON');cmd('TOUCH OFF');time.sleep(1)
  prefs=data('PREFS')
  if prefs.get('local_preview'):tap(56,112)
  current=prefs['page']
  if current==5:tap(512,366)
  elif current==4:tap(374,370);tap(630,409)
  assert data('SD STATE')['mounted']
  tap(384,564)
  sd=data('SD STATE');assert sd['local']
  for _ in range(sd['page']):tap(120,478)
  paths=[]
  while True:
   sd=data('SD STATE');paths.extend(file['path'] for file in sd['files'])
   if sd['page']*4+4>=sd['file_count']:break
   tap(900,478)
  assert len(paths)==len(set(paths))==sd['file_count']
  print('PASS every SD file is reachable:',len(paths),flush=True)
  small=next(path for path in paths if Path(path).name.lower()=='arc-ijk.gcode')
  row=find(small);shot('panel-display-files')
  tap(922,189+72*row);tap(630,409);time.sleep(1)
  assert data('PREFS')['page']==5 and not data()['job']
  shot('panel-display-complete');tap(512,366)
  assert data('PREFS')['page']==0
  large=next(path for path in paths if Path(path).name.lower()=='mb-14.5 test.nc')
  tap(384,564);row=find(large);tap(922,189+72*row);tap(630,409)
  for _ in range(120):
   status=data()
   if not status['checking']:break
   time.sleep(.5)
  assert not status['checking'], 'File validation timed out'
  assert status['local_job'] and status['job_total']>10*1024*1024
  tap(130,370);assert '0x82' in cmd('LAST')
  time.sleep(.5);before=data()['job_bytes'];time.sleep(.5);assert data()['job_bytes']==before
  shot('panel-display-paused')
  tap(130,370);time.sleep(.6);assert data()['job_bytes']>before
  tap(374,370);assert data()['local_job'];tap(342,409);assert data()['local_job']
  tap(374,370);tap(630,409);time.sleep(.6)
  assert not data()['local_job'] and data('PREFS')['page']==0
  # Error recovery uses the same real file, with synthetic controller feedback.
  tap(384,564);row=find(large);tap(922,189+72*row);tap(630,409)
  cmd('RX error:20');time.sleep(.5);assert not data()['local_job'] and data()['error']==20
  shot('panel-display-error');tap(630,409);time.sleep(.6)
  assert data('PREFS')['page']==0
  print('PASS actual-file validation, multi-line streaming, completion, pause/resume, confirmed Stop and error recovery',flush=True)
 finally:cmd('TOUCH ON')
