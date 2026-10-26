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
 time.sleep(.5)
 baseline=data()['jog_control_updates']
 time.sleep(.4)
 assert data()['jog_control_updates']==baseline,'Idle repeatedly restyled controls'
 for hold in (60,100,150):
  idle();cmd(f'HOLD 575 379 {hold}')
  d=data();assert '0x85' in cmd('LAST'),'Release did not cancel jog'
  assert hold-20<=d['jog_hold_ms']<hold+100,d  # Input timers have tick granularity.
  assert d['jog_control_updates']==baseline and d['jog_enabled'], 'Jog toggled control styling'
  print('PASS held',hold,'ms; cancel sent at',d['jog_hold_ms'],'ms',flush=True)
 cmd('RX <Jog|MPos:101,0,0|WCO:0,0,0|FS:1000,0|Pn:i>');time.sleep(.4)
 d=data();assert d['jog_control_updates']==baseline and d['jog_enabled'] and not d['can_setup'],d
 previous=cmd('LAST');cmd('HOLD 575 379 60')
 assert cmd('LAST')==previous and data()['form_kind']=='none','Busy jog tap queued work or opened a popup'
 idle();tap(638,154);time.sleep(.3);baseline=data()['jog_control_updates']
 cmd('HOLD 575 379 100');assert '$J=' in cmd('LAST'),'Incremental jog was cancelled on release'
 cmd('RX <Jog|MPos:102,0,0|WCO:0,0,0|FS:1000,0|Pn:i>');time.sleep(.4)
 d=data();assert d['jog_control_updates']==baseline and d['jog_enabled'] and not d['can_setup'],d
 idle();assert data()['jog_control_updates']==baseline
 cmd('DEMO OFF');time.sleep(.4);assert not data()['jog_enabled'],'Disconnect did not disable jog controls'
 cmd('DEMO ON');idle();assert data()['jog_enabled'],'Reconnect did not enable jog controls'
 cmd('RX ALARM:1');time.sleep(.4);assert data()['form_kind']=='confirmation'
 tap(264,520);time.sleep(.3);assert not data()['jog_enabled'],'Alarm did not disable jog controls'
 cmd('DEMO ON');idle();assert data()['jog_enabled'],'Controls stayed disabled after clearing alarm'
 print('PASS stable Idle/Jog controls; model setup guard, incremental completion, disconnect and fault disabling',flush=True)
finally:
 if p.is_open:
  try:
   cmd('DEMO ON');idle()
   if original and data()['page']==0:
    tap(878 if original['continuous'] else 638,154)
   cmd('TOUCH ON')
  finally:p.close()
