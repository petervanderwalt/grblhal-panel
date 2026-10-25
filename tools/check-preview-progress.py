"""Check real large-file preview progress and capture the physical LCD."""
import argparse,importlib.util,json,time,serial
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--port',default='COM12');parser.add_argument('--file',default='/mb_83.nc');args=parser.parse_args()
spec=importlib.util.spec_from_file_location('r',Path(__file__).with_name('ui-remote.py'));r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
with serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5) as p:
 p.dtr=p.rts=False;p.port=args.port;p.open();r.buffered_lines(p)
 for _ in range(30):
  p.write(b'\nUG SYNC\n');deadline=time.monotonic()+1
  while time.monotonic()<deadline:
   if b'@UGERR command' in p.readline():break
  else:continue
  break
 else:raise TimeoutError('Console sync')
 time.sleep(.8);p.reset_input_buffer()
 def cmd(s):return r.debug(p,s)
 def data(s='SD STATE'):return json.loads(cmd(s).removeprefix('@UGDATA '))
 def tap(x,y):r.exchange(p,'tap',point=(x,y));time.sleep(.3)
 try:
  cmd('DEMO ON');cmd('TOUCH OFF');assert not data('STATE')['job']
  if data('PREFS')['local_preview']:tap(56,112)
  tap(384,564);assert data()['local']
  for _ in range(data()['page']):tap(120,478)
  for _ in range(128):
   sd=data();row=next((i for i,f in enumerate(sd['files']) if f['path'].lower()==args.file.lower()),None)
   if row is not None:break
   assert sd['page']*4+4<sd['file_count'];tap(900,478)
  else:raise AssertionError('file missing')
  tap(778,189+72*row)
  samples=[]
  for _ in range(90):
   sd=data()
   if sd['preparing'] and sd['percent']>=0:samples.append(sd['percent'])
   if len(set(samples))>=2:break
   time.sleep(1)
  assert len(set(samples))>=2,sd
  r.exchange(p,'shot',Path('captures/panel-preview-progress.png'))
  print('PASS live preview progress changes:',samples,flush=True)
 finally:cmd('TOUCH ON')
