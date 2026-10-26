"""Verify USB3 on the isolated RP2350 development controller; never run motion."""
import argparse, importlib.util, json, os, time
from pathlib import Path
import serial
spec=importlib.util.spec_from_file_location('remote',Path('tools/ui-remote.py'))
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
a=argparse.ArgumentParser(description=__doc__)
a.add_argument('--port',default='COM12')
a.add_argument('--captures',type=Path,default=Path('captures/usb'))
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

def scroll(target):
 for _ in range(5):
  delta=target-data()['scroll_y']
  if abs(delta)<32:return
  step=max(-260,min(260,delta));start=440 if step>0 else 150
  cmd(f'SWIPE 940 {start} 940 {start-step}');time.sleep(.4)

def setting(local):tap(850,round(70+local-data()['scroll_y']))
def live():
 for _ in range(60):
  u=data('USB STATE');st=data('STATE')
  if u['connected'] and st['identified'] and st['ready']:
   assert (u['vid'],u['pid'])==(0x2e8a,0x000a),u
   print('LIVE USB',u,st,flush=True);return st
  time.sleep(.5)
 raise AssertionError(('USB identification timed out',data('USB STATE'),data('STATE')))

def settings_bottom():
 page(2);scroll(1300)

def connect_button():
 d=data();y=94+868+216+32-d['scroll_y']
 assert 110<y<510,(y,d)
 tap(264,round(y))

success=False;original=None
try:
 p.open();r.buffered_lines(p)
 raw=(args.captures/'usb-raw.log').open('ab');original_readline=p.readline
 def traced_readline(*a,**kw):
  line=original_readline(*a,**kw)
  if not line.startswith(b'@UGROW '):raw.write(line);raw.flush()
  return line
 p.readline=traced_readline
 for _ in range(15):
  p.write(b'\nUG SYNC\n');end=time.monotonic()+1
  while time.monotonic()<end:
   if b'@UGERR command' in p.readline():break
  else:continue
  break
 time.sleep(.3);p.reset_input_buffer();cmd('DEMO ON');cmd('TOUCH OFF');idle();clear_form();original=data();cmd('TEST')
 page(2);scroll(850)
 if not data()['usb_connection']:
  y=94+938-data()['scroll_y'];assert 110<y<490;tap(912,round(y))
 assert data()['usb_connection']==1;settings_bottom();shot('panel-usb-settings-disconnected')
 # Connecting sends the existing identification/status queries, not a motion command.
 if data('STATE')['online']:connect_button()
 connect_button();st=live();clear_form();settings_bottom();shot('panel-usb-settings')
 before=data('USB STATE');time.sleep(1.2);after=data('USB STATE');assert after['rx_bytes']>before['rx_bytes'],(before,after)
 page(0);clear_form();shot('panel-usb-jog')
 if not st['alarm']:
  page(2);scroll(480);setting(658);form('machine-settings')
  for _ in range(120):
   d=data('STATE')
   if not d['settings_loading'] and d['settings_result']!=0:break
   time.sleep(.5)
  assert d['settings_result']==1 and d['settings_count']>0,d
  print('LIVE SETTINGS',d['settings_count'],d['settings_groups'],flush=True);shot('panel-usb-machine-settings');tap(976,32)
 settings_bottom();gen=data('USB STATE')['generation'];connect_button();time.sleep(.8)
 assert not data('USB STATE')['connected'] and not data('STATE')['online']
 connect_button();live();assert data('USB STATE')['generation']>gen
 cmd('DEMO ON');time.sleep(.3);assert not data('USB STATE')['enabled'] and not data('USB STATE')['connected']
 idle();cmd('DEMO OFF');settings_bottom();connect_button();live()
 for key in ('x','y','z','diameter','feed','inset','tx','rx','baud','inches','macro_count','brightness','a_rotary','actuator_mode'):
  assert data()[key]==original[key],key
 page(0);clear_form();success=True
 print('PASS RP2350 USB identification, real feedback, settings availability, reconnect, demo isolation and unchanged user configuration',flush=True)
finally:
 if p.is_open:
  try:
   if not success:cmd('DEMO ON');idle()
   cmd('TOUCH ON')
  finally:p.close()
