"""Verify Settings file location persists across physical display resets; UART off."""
import importlib.util, serial, time, json
from pathlib import Path
spec=importlib.util.spec_from_file_location('r',Path(__file__).with_name('ui-remote.py'));r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
with serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5) as p:
 p.dtr=p.rts=False;p.port='COM12';p.open();p.rts=True;time.sleep(.15);p.rts=False
 def sync():
  time.sleep(8);p.reset_input_buffer()
  for _ in range(20):
   p.write(b'\nUG SYNC\n');end=time.monotonic()+1
   while time.monotonic()<end:
    if b'@UGERR command' in p.readline():
     time.sleep(.8);p.reset_input_buffer();return
  raise TimeoutError('sync')
 def tap(x,y):r.exchange(p,'tap',point=(x,y));time.sleep(.4)
 sync()
 try:
  for local in (False,True):
   r.debug(p,'DEMO ON');r.debug(p,'TOUCH OFF');tap(896,564);tap(830,266);tap(830,313 if local else 350)
   assert json.loads(r.debug(p,'SD STATE').removeprefix('@UGDATA '))['local']==local
   p.rts=True;time.sleep(.15);p.rts=False;sync()
   assert json.loads(r.debug(p,'SD STATE').removeprefix('@UGDATA '))['local']==local
   print('PASS persisted across reboot:', 'onboard' if local else 'controller',flush=True)
 finally:
  r.debug(p,'DEMO ON');r.debug(p,'TOUCH ON')
