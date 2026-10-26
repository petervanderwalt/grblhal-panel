"""Check continuous-jog release timing in isolated demo mode; no physical motion."""
import argparse, importlib.util, json, time
from pathlib import Path
import serial
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',default='COM12')
a=parser.parse_args()
spec=importlib.util.spec_from_file_location('remote',Path(__file__).with_name('ui-remote.py'))
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
p=serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5);p.dtr=p.rts=False;p.port=a.port

def cmd(t):return r.debug(p,t)
def data(t='PREFS'):return json.loads(cmd(t).removeprefix('@UGDATA '))
def tap(x,y):r.exchange(p,'tap',point=(x,y));time.sleep(.3)
def idle():cmd('RX <Idle|MPos:100,0,0|WCO:0,0,0|FS:0,0|Pn:i>');time.sleep(.2)
original=None
try:
 p.open();r.buffered_lines(p);p.write(b'\nUG SYNC\n');time.sleep(.5);p.reset_input_buffer()
 original=data();assert not original['actuator_mode'],'CNC layout required'
 cmd('DEMO ON');cmd('TOUCH OFF');idle()
 for _ in range(5):
  kind=data()['form_kind']
  if kind=='none':break
  tap(264,520) if kind=='confirmation' else tap(976,32)
 tap(116,566);idle();tap(878,154)
 assert data()['continuous']
 for hold in (60,100,150):
  idle();cmd(f'HOLD 575 379 {hold}')
  d=data();assert '0x85' in cmd('LAST'),'Release did not cancel jog'
  assert hold<=d['jog_hold_ms']<hold+150,d
  print('PASS held',hold,'ms; cancel sent at',d['jog_hold_ms'],'ms',flush=True)
finally:
 if p.is_open:
  try:
   cmd('DEMO ON');idle()
   if original and data()['page']==0:
    tap(878 if original['continuous'] else 638,154)
   cmd('TOUCH ON')
  finally:p.close()
