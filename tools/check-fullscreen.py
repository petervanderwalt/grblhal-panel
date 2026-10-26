"""Verify full-screen secondary pages on the UART-isolated display; never run motion."""
import argparse, importlib.util, json, os, time
from pathlib import Path
import serial
spec=importlib.util.spec_from_file_location('remote',Path(__file__).with_name('ui-remote.py'))
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
a=argparse.ArgumentParser(description=__doc__)
a.add_argument('--port',default='COM12')
a.add_argument('--captures',type=Path,default=Path('captures/fullscreen'))
a.add_argument('--only',nargs='*',help='Capture only these names; still check all navigation')
args=a.parse_args();args.captures.mkdir(parents=True,exist_ok=True)
p=serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5);p.dtr=p.rts=False;p.port=args.port

def cmd(t):return r.debug(p,t)
def data(k='PREFS'):return json.loads(cmd(k).removeprefix('@UGDATA '))
def tap(x,y):r.exchange(p,'tap',point=(x,y));time.sleep(.55)
def password(value):
 try:cmd('WIFI PASSWORD '+value.encode().hex())
 except Exception:raise RuntimeError('Wi-Fi password injection failed') from None

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
def find(setting_id):
 form('machine-settings');tap(266,120);form('machine-search')
 cmd('SETTING FIELD '+f'${setting_id} '.encode().hex());tap(892,135);form('machine-settings');tap(859,194);form('machine-editor')

original=None
try:
 p.open();r.buffered_lines(p)
 for _ in range(15):
  p.write(b'\nUG SYNC\n');end=time.monotonic()+1
  while time.monotonic()<end:
   if b'@UGERR command' in p.readline():break
  else:continue
  break
 else:raise TimeoutError('Console sync')
 time.sleep(.3);p.reset_input_buffer();cmd('DEMO ON');cmd('TOUCH OFF');idle();clear_form()
 original=data();assert not original['actuator_mode'],'Start in CNC mode'
 tap(870,566);tap(154,566);page(0);tap(900,4);form('wifi-scan')
 for _ in range(60):
  w=data('WIFI STATE')
  if not w['scanning'] and w['networks']:break
  if not w['connecting'] and not w['scanning']:
   tap(718,128);form('wifi-scan')
  time.sleep(1)
 assert w['networks'];shot('panel-wifi')
 assert 'Braai-Fi' in w['networks'],'Approved test network is not visible'
 index=w['networks'].index('Braai-Fi')
 assert index<5,'Choose a visible test network'
 tap(240,212+index*72);form('wifi-password');shot('panel-wifi-password')
 password('x');tap(892,132);form('wifi-password');shot('panel-wifi-validation')
 secret=os.environ.get('PANEL_TEST_WIFI_PASSWORD')
 if secret and w['networks'][index]=='Braai-Fi':
  password(secret);tap(892,132)
  for _ in range(45):
   if data()['form_kind']=='notice':break
   time.sleep(1)
  form('notice');assert data('WIFI STATE')['connected'];shot('panel-wifi-connected');tap(822,520)
  assert data()['form_kind']=='none','OK must close notification and scanner'
 else:tap(700,132);form('wifi-scan');tap(976,32)
 page(1)
 sd=data('SD STATE')
 if sd['local'] and sd['mounted']:
  tap(379,42);form('preview-update');shot('panel-preview-update');clear_form()
 page(0);tap(100,480);form('probe');shot('panel-probe-single-z')
 tap(740,128);form('probe');shot('panel-probe')
 tap(240,272);form('numeric');shot('panel-numpad');tap(174,528);form('probe');tap(270 if original['single_probe'] else 740,128);tap(976,32)
 page(0);tap(254,482);form('tools');shot('panel-accessories')
 tap(718,32);form('spindles');time.sleep(2);shot('panel-spindles');tap(976,32)
 page(0);tap(64,96);form('confirmation');shot('panel-home-confirm');tap(264,520)
 tap(414,96);form('confirmation');shot('panel-zero-confirm');tap(264,520)
 page(2);scroll(480);setting(570);form('preview-server');shot('panel-preview-server');tap(700,132)
 scroll(480);setting(658);form('machine-settings')
 for _ in range(40):
  if data('STATE')['settings_result']==1:break
  time.sleep(.5)
 assert data('STATE')['settings_result']==1;shot('panel-machine-settings')
 for setting_id,name in ((100,'number'),(13,'switch'),(22,'mask'),(32,'options'),(74,'text'),(23,'axes'),(75,'password'),(302,'ip')):
  find(setting_id);shot('panel-machine-setting-'+name);tap(976,32);form('machine-settings')
 tap(266,120);form('machine-search');shot('panel-machine-setting-search');tap(976,32);form('machine-settings');tap(976,32)
 if not original.get('usb_connection',0):
  page(2);scroll(800);setting(1026);form('numeric');shot('panel-settings-number');tap(174,528)
 page(3)
 if original['macro_count']:
  tap(236,134);form('macro-editor');shot('panel-macro-editor');tap(976,32)
 tap(408,42);form('macro-editor');shot('panel-macro-add');tap(720,204);assert data()['form_kind']=='none'
 page(2);tap(910,154);assert data()['actuator_mode']==1
 scroll(900);setting(1058);form('actuator-editor');shot('panel-actuator-editor');tap(976,32)
 tap(154,566);assert data()['page']==0;shot('panel-actuators')
 tap(870,566);scroll(0);tap(775,154);assert data()['actuator_mode']==0
 page(0);cmd('RX <Run|SD:42.5,/bracket.nc|MPos:125,84.5,-12|FS:800,12000|Ov:100,100,100>');time.sleep(.5)
 assert data()['page']==4;shot('panel-job');tap(360,470);form('confirmation');shot('panel-job-stop-confirm');tap(264,520)
 cmd('RX <Idle|SD:100,/bracket.nc>');cmd('RX [MSG:Program End]');idle();time.sleep(.5)
 assert data()['page']==5;shot('panel-job-complete');tap(512,366)
 cmd('DEMO ON');idle();clear_form();page(0)
 cmd('RX <Alarm|MPos:0,0,0|WCO:0,0,0>');time.sleep(.4);form('confirmation');shot('panel-startup-alarm');tap(264,520)
 cmd('RX ALARM:1');time.sleep(.4);form('confirmation');shot('panel-alarm');tap(264,520)
 cmd('DEMO ON');idle();cmd('RX error:20');time.sleep(.4);form('confirmation');shot('panel-error');tap(264,520)
 cmd('DEMO ON');idle();page(0)
 for key in ('x','y','z','diameter','feed','inset','tx','rx','baud','inches','macro_count','brightness','a_rotary','actuator_mode','usb_connection'):
  assert data()[key]==original[key],key
 cmd('TEST');print('PASS full-screen forms, Cancel/navigation, Wi-Fi, interlocks and unchanged preferences',flush=True)
finally:
 if p.is_open:
  try:
   clear_form()
   if data()['page']==4:
    tap(360,470);form('confirmation');tap(760,520);time.sleep(.5)
   elif data()['page']==5:tap(512,366)
   cmd('DEMO ON');idle();clear_form()
   if original and data()['actuator_mode']!=original['actuator_mode']:
    tap(870,566);scroll(0);tap(775,154)
  finally:
   try:cmd('TOUCH ON')
   finally:p.close()
