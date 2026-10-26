"""Check Settings rows, large Preview Server editor and numeric-dialog cleanup without changing preferences."""
import importlib.util,json,time,sys
from pathlib import Path
import serial
s=importlib.util.spec_from_file_location('r','tools/ui-remote.py');r=importlib.util.module_from_spec(s);s.loader.exec_module(r)
p=serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5);p.dtr=p.rts=False;p.port=__import__('os').environ.get('PANEL_PORT','COM12')
def cmd(t):return r.debug(p,t)
def data():return json.loads(cmd('PREFS').removeprefix('@UGDATA '))
def tap(x,y):r.exchange(p,'tap',point=(x,y));time.sleep(.7)
def shot(name):
 time.sleep(.5)
 for i in range(3):
  try:r.exchange(p,'shot',Path('captures/'+name+'.png'));return
  except ValueError:
   if i==2:raise
try:
 p.open();r.buffered_lines(p)
 for _ in range(15):
  p.write(b'\nUG SYNC\n');end=time.monotonic()+1
  while time.monotonic()<end:
   if b'@UGERR command' in p.readline():break
  else:continue
  break
 time.sleep(.3);p.reset_input_buffer();cmd('DEMO ON');cmd('TOUCH OFF');time.sleep(.5)
 cmd('RX <Idle|MPos:125.375,84.5,-12.25|WCO:100,50,-20|FS:0,0|Pn:i>')
 before=data()['macro_count']
 original=data();assert original['dialog']=='none' and not original['actuator_mode'] and not original['inches'], 'Start in CNC mode with metric display and no dialog';original_server=json.loads(cmd('SD STATE').removeprefix('@UGDATA '))['server'];tap(870, 566);assert data()['page']==2
 def scroll_to(target):
  for _ in range(4):
   current=data()['scroll_y'];delta=target-current
   if abs(delta)<24:return
   step=max(-260,min(260,delta));start=440 if step>0 else 150
   cmd(f'SWIPE 940 {start} 940 {start-step}');time.sleep(.5)
 scroll_to(480);shot('settings-service-rows')
 y=lambda local:round(70+local-data()['scroll_y'])
 tap(850,y(570));assert data()['dialog']=='preview-server';shot('settings-preview-server-large');tap(943,220);tap(700,132);assert data()['dialog']=='none'
 assert json.loads(cmd('SD STATE').removeprefix('@UGDATA '))['server']==original_server
 tap(691,566);assert data()['page']==6 and data()['dialog']=='none';tap(870,566)
 scroll_to(480);tap(850,y(658));time.sleep(2)
 settings=json.loads(cmd('STATE').removeprefix('@UGDATA '));assert settings['settings_result']==1
 tap(976,32);assert data()['dialog']=='none'
 scroll_to(650);row_y=y(794);assert 110<row_y<475
 keys=('x','y','z','feed','inset')
 points=((428,132),(428,208),(428,284),(428,360),(428,436))
 def field(axis,text):
  tap(*points[axis]);cmd('SETTING FIELD '+text.encode('ascii').hex())
 tap(850,row_y);assert data()['dialog']=='probe-offsets'
 for axis,key in enumerate(keys):field(axis,'0' if axis==1 else f"{original[key]+.1:.1f}")
 tap(458,528);assert data()['dialog']=='probe-offsets'
 for key in keys:assert data()[key]==original[key]
 for axis,key in enumerate(keys):field(axis,f"{original[key]:.1f}")
 tap(458,528);assert data()['dialog']=='none'
 scroll_to(650);row_y=y(794);tap(850,row_y);assert data()['dialog']=='probe-offsets'
 delta=(.1,.2,.3,1,.5)
 for axis,key in enumerate(keys):field(axis,f"{original[key]+delta[axis]:.1f}")
 for axis,key in enumerate(keys):assert abs(data()['probe_edit_'+key]-(original[key]+delta[axis]))<.001
 tap(710,155);assert abs(data()['probe_edit_inset']-float(f"{original['inset']+.5:.1f}"+'1'))<.001
 field(4,f"{original['inset']+.5:.1f}")
 shot('settings-probe-offsets-large');tap(174,528);assert data()['dialog']=='none'
 for key in keys:assert data()[key]==original[key]
 if not original.get('usb_connection',0):
  scroll_to(800);tap(850,y(1026));assert data()['dialog']=='number-keypad'
  tap(712,132);assert data()['dialog']=='none'
 shot('settings-popup-closed')
 for key in ('x','y','z','diameter','feed','inset','tx','rx','baud','inches','step','actuator_mode','a_rotary','macro_count','brightness','usb_connection'):
  assert data()[key]==original[key],key
 tap(512, 566);assert data()['page']==3
 if before:
  tap(236,134);assert data()['dialog']=='other';shot('macro-editor-consistent')
  tap(720,204);tap(264,520);assert data()['dialog']=='none';assert data()['macro_count']==before
 tap(408,42);assert data()['dialog']=='other';shot('macro-add-consistent')
 tap(720,204);assert data()['dialog']=='none';assert data()['macro_count']==before
 tap(870, 566);shot('settings-root-rows')
 assert data()['macro_count']==before
 print('PASS settings actions and Troubleshooting, editor input and navigation; preferences unchanged',flush=True)
finally:
 if p.is_open:
  cmd('DEMO ON');time.sleep(.3);cmd('RX <Idle|FS:0,0>');cmd('TOUCH ON');p.close()
