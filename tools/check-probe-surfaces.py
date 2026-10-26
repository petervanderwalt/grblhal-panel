"""Check single-surface probe UI using real LCD and UART-disconnected feedback."""
import argparse,importlib.util,json,time,serial
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--port',default='COM12');parser.add_argument('--no-captures',action='store_true');args=parser.parse_args()
spec=importlib.util.spec_from_file_location('r',Path(__file__).with_name('ui-remote.py'));r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
with serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5) as p:
 p.dtr=p.rts=False;p.port=args.port;p.open()
 for _ in range(30):
  p.write(b'\nUG SYNC\n');end=time.monotonic()+1
  while time.monotonic()<end:
   if b'@UGERR command' in p.readline():break
  else:continue
  break
 else:raise TimeoutError('sync')
 time.sleep(.8);p.reset_input_buffer()
 def cmd(s):return r.debug(p,s)
 def data(s='STATE'):return json.loads(cmd(s).removeprefix('@UGDATA '))
 def tap(x,y):r.exchange(p,'tap',point=(x,y));time.sleep(.3)
 def shot(name):
  if not args.no_captures:r.exchange(p,'shot',Path('captures/probe-surfaces')/(name+'.png'))
 def touch_release():
  cmd('RX <Idle|FS:0,0|Pn:I>');time.sleep(.2)
  assert not data()['contact']
  cmd('RX <Idle|FS:0,0|Pn:i>');time.sleep(.2)
  assert data()['contact'] and not data()['contact_testing']
 def surface(index):
  tap(500,218);tap(500,265+38*index)
  assert data('PREFS')['probe_surface']==index
 def run(axis):
  tap(720,486);tap(630,409);time.sleep(.8)
  s=data();assert s['probing'] and s['probe_axis']==axis,s
  assert 'G38.2 '+ 'XYZ'[axis] in cmd('LAST')
  cmd('RX [PRB:125,84.5,-12:1]')
  for _ in range(30):
   if not data()['probing']:break
   time.sleep(.2)
  assert not data()['probing'] and not data()['contact']
 try:
  cmd('DEMO ON');cmd('TOUCH OFF');assert not data()['job']
  cmd('RX <Idle|MPos:125,84.5,-12|WCO:100,50,-20|FS:0,0|Pn:i>')
  tap(128,564);tap(130,432)
  prefs=data('PREFS');assert prefs['single_probe'] and prefs['probe_surface']==0,prefs
  assert data()['contact_testing'] and not data()['contact']
  tap(720,486);assert not data()['probing'] # Disabled until release.
  touch_release();shot('panel-probe-z')
  tap(720,486);shot('panel-probe-z-confirm');tap(342,409)
  run(2)
  tap(916,82);tap(130,432)
  for index in range(1,5):surface(index)
  surface(2);touch_release();shot('panel-probe-surface')
  if data('PREFS')['diameter']>0:run(0)
  tap(916,82);tap(130,432);tap(720,154)
  assert not data('PREFS')['single_probe'];touch_release();shot('panel-probe')
  tap(280,295);shot('panel-numpad');tap(174,528)
  tap(280,154);surface(0)
  assert data('PREFS')['single_probe'] and data('PREFS')['probe_surface']==0
  print('PASS Z default, auto-armed touch/release, disabled start, all five surfaces, Z/X probe completion, Corner and numeric editor',flush=True)
 finally:cmd('TOUCH ON')
