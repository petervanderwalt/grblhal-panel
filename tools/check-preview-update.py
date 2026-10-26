"""Real LCD check: Files never starts cache work; explicit Update and Cancel do."""
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
 tap(434,566)
 def sd():return json.loads(cmd('SD STATE').removeprefix('@UGDATA '))
 for _ in range(20):
  t=sd()
  if not t['preparing'] and t['file_count']:break
  time.sleep(.5)
 assert t['local'] and t['mounted'], 'Select Onboard SD with the development card inserted'
 print('INITIAL',json.dumps(t),flush=True)
 for _ in range(8):
  assert not sd()['update_active'] and not sd()['preparing'];time.sleep(.5)
 shot('files-manual-update')
 tap(380,52)
 time.sleep(2)
 print('STARTED',json.dumps(sd()),flush=True)
 assert sd()['update_active']
 shot('preview-update-progress')
 tap(752,454)
 for _ in range(40):
  if not sd()['preparing']:break
  time.sleep(.5)
 assert not sd()['update_active'] and not sd()['preparing']
 shot('preview-update-cancelled')
 tap(752,454)
 for _ in range(8):
  assert not sd()['preparing'] and not sd()['update_active'];time.sleep(.5)
 tap(285,566);tap(434,566)
 assert not sd()['preparing'] and not sd()['update_active']
 print('PASS idle browsing starts no work; explicit update starts; Cancel stops; navigation does not restart',flush=True)
finally:
 if p.is_open:
  cmd('DEMO ON');time.sleep(.3);cmd('RX <Idle|FS:0,0>');cmd('TOUCH ON');p.close()
