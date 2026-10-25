"""With preview service stopped, save a small SD sample and check the LCD fallback; never runs the job."""
"""Save a harmless header sample, then check text fallback with preview server offline."""
import importlib.util,json,serial,time,urllib.request
from pathlib import Path
spec=importlib.util.spec_from_file_location('r',Path(__file__).with_name('ui-remote.py'));r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
code='''(BRACKET - HEADER REVIEW)
(Sample G-code)
(Tool: 6.350 mm end mill)
(Stock thickness: 18.000 mm)
G21 (millimetres)
G90 (absolute positions)
G17 (XY plane)
G54 (work coordinate system)
M5 (spindle off)
G0 Z10.000
G0 X0.000 Y0.000
S12000 M3
G1 Z-1.000 F150.000
G1 X40.000 F600.000
M5
M30
'''
name='000-header-review-'+str(int(time.time()))+'.nc'
with serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5) as p:
 p.dtr=p.rts=False;p.port='COM12';p.open()
 for _ in range(30):
  p.write(b'\nUG SYNC\n');end=time.monotonic()+1
  while time.monotonic()<end:
   if b'@UGERR command' in p.readline():break
  else:continue
  break
 time.sleep(.8);p.reset_input_buffer()
 def cmd(s):return r.debug(p,s)
 def data(s='SD STATE'):return json.loads(cmd(s).removeprefix('@UGDATA '))
 def tap(x,y):r.exchange(p,'tap',point=(x,y));time.sleep(.4)
 try:
  cmd('DEMO ON');cmd('TOUCH OFF');assert not data('STATE')['job'];assert data()['local']
  payload=json.dumps({'name':name,'gcode':code}).encode()
  request=urllib.request.Request('http://192.168.0.34:47820/gcam/v1/load',data=payload,headers={'Content-Type':'application/json'})
  with urllib.request.urlopen(request,timeout=15) as response:assert response.status==202
  if data('PREFS')['local_preview']:tap(56,112)
  tap(384,564)
  for _ in range(data()['page']):tap(120,478)
  for _ in range(90):
   sd=data();row=next((i for i,f in enumerate(sd['files']) if f['path']=='/'+name),None)
   if row is not None:break
   time.sleep(1)
  assert row is not None,sd
  tap(778,189+72*row);time.sleep(3)
  assert data('PREFS')['local_preview'] and not data('STATE')['job']
  r.exchange(p,'shot',Path('captures/panel-preview-text.png'))
  tap(56,112);assert not data('PREFS')['local_preview']
  tap(778,189+72*row);assert data('PREFS')['local_preview']
  print('PASS real SD file header fallback with preview server offline:',name,flush=True)
 finally:cmd('TOUCH ON')
