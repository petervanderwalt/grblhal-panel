"""Measure actual LCD page construction/rendering; opens forms without saving or moving."""
import argparse,importlib.util,json,statistics,time
from pathlib import Path
import serial

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',default='COM12');p.add_argument('--samples',type=int,default=10)
p.add_argument('--output',type=Path,required=True);p.add_argument('--tabs-only',action='store_true')
p.add_argument('--warm-settings',action='store_true',help='Load controller metadata before the measured runs')
a=p.parse_args();a.output.parent.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('remote',Path(__file__).with_name('ui-remote.py'))
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
port=serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5)
port.dtr=port.rts=False;port.port=a.port
samples=[]
def cmd(t):return r.debug(port,t)
def data(k):return json.loads(cmd(k).removeprefix('@UGDATA '))
def tap(x,y):r.exchange(port,'tap',point=(x,y))
def close():
 kind=data('PREFS')['form_kind']
 if kind!='none':
  tap(264,520) if kind=='confirmation' else tap(822,520) if kind=='notice' else tap(976,32)
  time.sleep(.15)
 assert data('PREFS')['form_kind']=='none'
def page(index):
 close();tap({0:116,1:314,3:512,6:710,2:908}[index],566)
 assert data('PREFS')['page']==index
def settled(previous):
 end=time.monotonic()+15
 while time.monotonic()<end:
  s=data('PERF')
  if s['seq']>previous and s['ready']:return s
  time.sleep(.03)
 raise AssertionError(('No rendered page',s))
def measure(name,action):
 before=data('PERF')['seq'];start=time.monotonic();action();s=settled(before)
 s.update(case=name,host_us=round((time.monotonic()-start)*1e6),builds=s['seq']-before)
 assert s['builds']==1,('Unexpected extra page rebuild',name,s)
 status=data('AUDIT');s['status_age_ms']=status['ms']-status['status_ms']
 samples.append(s);a.output.write_text(json.dumps(samples,indent=2));return s
def scroll(target):
 for _ in range(6):
  delta=target-data('PREFS')['scroll_y']
  if abs(delta)<24:return
  step=max(-260,min(260,delta));start=440 if step>0 else 150
  cmd(f'SWIPE 940 {start} 940 {start-step}');time.sleep(.2)
 raise AssertionError('Settings scroll position')
def row(local):tap(850,round(70+local-data('PREFS')['scroll_y']))
def form(kind):assert data('PREFS')['form_kind']==kind
original=None
context={}
try:
 port.open();r.buffered_lines(port)
 for _ in range(20):
  port.write(b'\nUG SYNC\n');end=time.monotonic()+1
  while time.monotonic()<end:
   if b'@UGERR command' in port.readline():break
  else:continue
  break
 else:raise TimeoutError('Console sync')
 time.sleep(.2);port.reset_input_buffer();original=data('PREFS')
 state=data('STATE');assert not any(state[k] for k in ['job','local_job','probing','macro_running'])
 assert not original['actuator_mode'],'Benchmark starts in CNC mode'
 assert data('AUDIT')['mode']=='Idle'
 cmd('TOUCH OFF');page(0);time.sleep(.5)
 end=time.monotonic()+45
 while time.monotonic()<end:
  wifi=data('WIFI STATE')
  if not wifi['connecting'] and not wifi['scanning'] and data('PREFS')['can_setup']:break
  time.sleep(.25)
 if a.warm_settings:
  page(2);scroll(480);row(658);form('machine-settings');end=time.monotonic()+30
  while data('STATE')['settings_loading'] and time.monotonic()<end:time.sleep(.1)
  assert data('STATE')['settings_result']==1;close();page(0)
 context['before']={'state':data('STATE'),'audit':data('AUDIT'),'wifi':data('WIFI STATE')}
 time.sleep(1)
 for cycle in range(a.samples):
  for index,name in [(1,'Files'),(3,'Macros'),(6,'Diag'),(2,'Settings'),(0,'Jog')]:
   measure(name,lambda index=index:page(index));time.sleep(.1)
 if not a.tabs_only:
  for _ in range(min(a.samples,3)):
   page(0);measure('Probe',lambda:tap(100,480));form('probe');close()
   measure('Tools',lambda:tap(254,482));form('tools');close()
   page(3);measure('Macro editor',lambda:tap(408,42));form('macro-editor');close()
   page(2);scroll(480)
   measure('Preview server',lambda:row(570));form('preview-server');close()
   measure('Machine settings',lambda:row(658));form('machine-settings')
   end=time.monotonic()+30
   while data('STATE')['settings_loading'] and time.monotonic()<end:time.sleep(.1)
   assert data('STATE')['settings_result']==1;close()
   scroll(650);measure('Probe configuration',lambda:row(794));form('probe-configuration');close()
  page(0);measure('Wi-Fi scanner',lambda:tap(900,24));form('wifi-scan');close()
 page(0)
 after=data('PREFS')
 for k in ['x','y','z','diameter','feed','inset','inches','step','continuous','jog_speed','macro_count','brightness','usb_connection','actuator_mode']:
  assert original[k]==after[k],k
 for name in dict.fromkeys(s['case'] for s in samples):
  rows=[s for s in samples if s['case']==name]
  result={k:round(statistics.median(s[k] for s in rows)/1000,2) for k in ['build_us','layout_us','render_us','flush_us','wait_us','transition_us']}
  print('RESULT',name,len(rows),json.dumps(result),flush=True)
 print('PASS benchmark; no motion or saved configuration changes',flush=True)
 context['after']={'state':data('STATE'),'audit':data('AUDIT'),'wifi':data('WIFI STATE')}
 a.output.with_suffix('.context.json').write_text(json.dumps(context,indent=2))
finally:
 if port.is_open:
  try:cmd('TOUCH ON')
  finally:port.close()
