"""Live pendant/controller checks. Requires a confirmed isolated controller PCB."""
import argparse,contextlib,datetime,importlib.util,json,re,time,urllib.request,urllib.error
from pathlib import Path
import serial
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',default='COM12');p.add_argument('--host',default='192.168.0.34');p.add_argument('--isolated-controller',action='store_true',required=True)
p.add_argument('--stage',choices=['inspect','streams','controls','jog','fault','homing','soak','stop','metadata','tools','probe','restore','invalidfile','critical'],default='inspect');p.add_argument('--output',type=Path,default=Path('captures/live-serial-2026-10-27'));a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('remote',Path(__file__).with_name('ui-remote.py'));r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
port=serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5);port.dtr=port.rts=False;port.port=a.port
log=(a.output/(a.stage+'-events.jsonl')).open('a',encoding='utf-8')
def record(event,**data):
 log.write(json.dumps(dict(time=datetime.datetime.now(datetime.timezone.utc).isoformat(),event=event,**data))+'\n');log.flush()
def cmd(text):
 result=r.debug(port,text);record('console',command=text,result=result);return result
def data(key='STATE'):
 for attempt in range(3):
  try:return json.loads(cmd(key).removeprefix('@UGDATA '))
  except (json.JSONDecodeError,RuntimeError):
   if attempt==2:raise
   time.sleep(.2)
def tap(x,y):r.exchange(port,'tap',point=(x,y));record('tap',x=x,y=y);time.sleep(.35)
def audit():return data('AUDIT')
def wait(predicate,timeout=20,interval=.25):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  s=audit()
  if predicate(s):return s
  time.sleep(interval)
 raise AssertionError(('Timed out',s))
def form(kind):
 s=data('PREFS');assert s['form_kind']==kind,s;return s
def close():
 s=data('PREFS')
 if s['form_kind']=='confirmation':tap(264,520)
 elif s['form_kind']=='notice':tap(822,520)
 elif s['form_kind']!='none':tap(976,32)
def page(index):
 close();tap({0:116,1:314,2:908,3:512,6:710}[index],566);assert data('PREFS')['page']==index

def shot(name):r.exchange(port,'shot',a.output/(name+'.png'));record('screenshot',path=name+'.png')
def settings():
 result={}
 count=audit()['settings']
 for i in range(count):
  value=data(f'AUDIT {i}');assert not value.get('end');result[str(value['id'])]=value
 return result

def upload(name,code):
 path=f'000-00-serial-{name}.nc';request=urllib.request.Request(f'http://{a.host}:47820/gcam/v1/load',data=json.dumps({'name':path,'gcode':code}).encode(),headers={'Content-Type':'application/json'})
 try:
  with urllib.request.urlopen(request,timeout=15) as res:result=json.load(res);assert res.status==202
 except urllib.error.HTTPError as e:
  assert e.code==409,(e.code,e.read());result={'existing':True}
 record('upload',path=path,result=result)
 assert not data()['job'],'Upload must not auto-run';return '/'+path

def find(path):
 page(1)
 for _ in range(40):
  sd=data('SD STATE')
  if sd['page']==0:break
  tap(100,494)
 for _ in range(128):
  sd=data('SD STATE')
  for i,f in enumerate(sd['files']):
   if f['path']==path:return i
  assert sd['page']*4+4<sd['file_count'],(path,sd)
  tap(916,494)
 raise AssertionError('File not found')
def start(path,cancel_first=False):
 wait(lambda s:s['mode']=='Idle');row=find(path);tap(906,137+88*row);form('confirmation')
 if cancel_first:
  before=audit();tap(264,520);after=audit();assert before['sent']==after['sent'] and before['resets']==after['resets'];tap(906,137+88*row);form('confirmation')
 tap(760,520);record('started',path=path)

def expected(lines):
 h=2166136261
 for c in ('\n'.join(lines)+'\n').encode():h=((h^c)*16777619)&0xffffffff
 return dict(sent=len(lines),acked=len(lines),wire_bytes=len(('\n'.join(lines)+'\n').encode()),hash=h)
def finish(lines,timeout=60,capture=None,interval=.2):
 s=wait(lambda s:s['complete'] or s['failed'] or s['errors']>error_baseline,timeout,interval)
 assert not s['failed'] and s['complete'] and s['mode']=='Idle',s
 for key,value in expected(lines).items():assert s[key]==value,(key,value,s)
 assert s['acked_wire']==s['wire_bytes'] and s['high_water']<128 and s['in_flight']==s['pending']==0,s
 wait(lambda s:s['mode']=='Idle' and s['command']=='$#');formpage=data('PREFS');assert formpage['page']==5,formpage
 if capture:shot(capture)
 tap(512,366);assert data('PREFS')['page']==0
 record('pass',stage=a.stage,result=s);return s

def unlock():
 s=data()
 if s['alarm']:
  assert s['alarm'] in [3,9,11],s
  if data('PREFS')['form_kind']!='confirmation':tap(550,30)
  form('confirmation');tap(760,520)
 wait(lambda s:s['mode']=='Idle')

def base():
 return {'state':data(),'audit':audit(),'prefs':data('PREFS'),'reports':[data(f'AUDIT {-i-2}')['line'] for i in range(14)],'settings':settings()}

error_baseline=0
try:
 port.open();r.buffered_lines(port)
 raw_readline=port.readline
 def logged_readline():
  line=raw_readline()
  if line and not line.startswith(b'@UG'):record('device-log',line=line.decode('utf-8',errors='replace').rstrip())
  return line
 port.readline=logged_readline
 for attempt in range(20):
  port.write(b'\nUG SYNC\n');end=time.monotonic()+1
  while time.monotonic()<end:
   if b'@UGERR command' in port.readline():break
  else:continue
  break
 else:raise TimeoutError('Remote console did not start')
 time.sleep(.2);port.reset_input_buffer();cmd('TOUCH OFF')
 initial=data();assert initial['online'] and initial['identified'] and not any(initial[k] for k in ['job','local_job','probing','macro_running']),initial
 assert data('PREFS')['usb_connection'] and data('SD STATE')['local']
 unlock();close()
 if data()['error']:
  tap(550,30);form('confirmation');tap(760,520);assert not data()['error']
 error_baseline=audit()['errors']
 if a.stage=='inspect':
  assert not (a.output/'controller-before.json').exists(),'Baseline already exists; use --output for a new run directory'
  snapshot=base();(a.output/'controller-before.json').write_text(json.dumps(snapshot,indent=2));record('baseline',data=snapshot)
  for key in ['20','21','22','23','24','25','27','30','31','130','131','132']:print(key,snapshot['settings'].get(key))
  before=audit();time.sleep(2);after=audit();assert after['rt80']>before['rt80'] and after['rt3f']==before['rt3f'] and after['writes']==before['writes'];record('pass',case='realtime-0x80',before=before,after=after)
 elif a.stage in ['streams','soak']:
  cases=[('tiny',['G21','G90','G4P0.3','G4P0.3','M2'],None),('crlf',['G21','G90','G4P0.3','M2'],'(header)\r\n g21\r\n\r\ng90 ;absolute\r\n g4 p0.3 (dwell)\r\nm2\r\n'),('long-block',['G21','G90','G4P'+('0'*118)+'.1','G4P0.3','M2'],None),('short-lines',['G21','G90']+['G4P0.001']*1000+['G4P0.3','M2'],None)] if a.stage=='streams' else [('soak',['G21','G90']+['G4P0.001']*10000+['G4P0.3','M2'],None)]
  for name,lines,raw in cases:
   path=upload(name,raw or '\n'.join(lines)+'\n');start(path,cancel_first=name=='tiny');finish(lines,650 if a.stage=='soak' else 60,capture='stream-complete' if name=='tiny' else None,interval=1 if a.stage=='soak' else .2)
 elif a.stage=='controls':
  lines=['G21','G91','G1X0.5F15','G1X-0.5','G90','M2'];path=upload('moving-controls','\n'.join(lines)+'\n');start(path);wait(lambda s:s['mode']=='Run');tap(140,472);hold=wait(lambda s:s['mode'].startswith('Hold'),8);time.sleep(.6);assert audit()['mode'].startswith('Hold');shot('stream-paused')
  before=audit();tap(365,472);form('confirmation');tap(264,520);assert audit()['resets']==before['resets'] and not audit()['complete'];tap(116,566);assert data('PREFS')['page']==4
  cmd('SWIPE 748 334 770 334');wait(lambda s:108<=s['feed_override']<=112)
  cmd('SWIPE 748 444 770 444');wait(lambda s:108<=s['speed_override']<=112)
  cmd('SWIPE 770 334 748 334');wait(lambda s:s['feed_override']==100)
  cmd('SWIPE 770 444 748 444');wait(lambda s:s['speed_override']==100)
  tap(140,472);wait(lambda s:s['mode']=='Run');finish(lines,40)
 elif a.stage=='fault':
  lines=['G21','G90','G4P0.3','G999','G4P1','M2'];path=upload('invalid-command','\n'.join(lines)+'\n');start(path);s=wait(lambda s:s['failed'] or s['errors']>error_baseline,10);assert not s['complete'];record('expected-controller-error',state=data(),audit=s);shot('stream-error')
 elif a.stage=='homing':
  page(0);tap(87,410);form('confirmation');tap(760,520);s=wait(lambda s:s['mode']=='Alarm' or s['errors']>error_baseline,120,.5);record('expected-homing-failure',state=data(),audit=s);assert not s['complete'];shot('homing-error')
 elif a.stage=='jog':
  page(0);prefs=data('PREFS');record('jog-original',prefs=prefs)
  def step(index):
   tap(500,530);tap(638,154);tap(890,214);tap(860,270+38*index);assert data('PREFS')['step']==index
  def jog(x,y,axis,delta):
   config=json.loads((a.output/'controller-before.json').read_text())['settings'];resolution=float(config[str(100+axis)]['value']);actual=round(delta*resolution)/resolution
   before=audit();updates=data('PREFS')['jog_control_updates'];tap(x,y)
   after=wait(lambda s:abs(s['machine'][axis]-before['machine'][axis]-actual)<.002 and s['mode']=='Idle',8)
   assert after['errors']==before['errors'] and data('PREFS')['jog_control_updates']==updates,(before,after)
   record('pass',case='incremental-jog',command=after['command'],delta=delta,position=after['machine'])
  try:
   step(0)
   for x,y in [(64,96),(416,96),(87,410),(195,410),(304,410),(415,410)]:
    before=audit();tap(x,y);form('confirmation');tap(264,520);after=audit();assert after['tx']==before['tx'],(before,after)
   jog(792,379,0,.1);jog(574,379,0,-.1)
   jog(682,467,1,-.1);jog(682,291,1,.1)
   jog(943,445,2,-.1);jog(943,313,2,.1)
   before=audit();tap(792,467);after=wait(lambda s:s['mode']=='Idle' and abs(s['machine'][0]-before['machine'][0]-.1)<.002 and abs(s['machine'][1]-before['machine'][1]+.1)<.002,8)
   before=audit();tap(574,291);wait(lambda s:s['mode']=='Idle' and abs(s['machine'][0]-before['machine'][0]+.1)<.002 and abs(s['machine'][1]-before['machine'][1]-.1)<.002,8)
   tap(878,154);before=audit();updates=data('PREFS')['jog_control_updates'];cmd('HOLD 792 379 80');after=wait(lambda s:s['mode']=='Idle' and s['machine'][0]>before['machine'][0],8)
   assert '0x85' in cmd('LAST');assert data('PREFS')['jog_control_updates']==updates
   stop_position=after['machine'][0];time.sleep(.8);assert abs(audit()['machine'][0]-stop_position)<.002
   record('pass',case='continuous-release',travel=stop_position-before['machine'][0]);tap(638,154)
   page(2);tap(912,442);assert data('PREFS')['inches'];page(0);step(0);jog(792,379,0,.254);assert 'G21' in audit()['command'];jog(574,379,0,-.254)
  finally:
   if audit()['mode']=='Idle':
    page(2);tap(912 if prefs['inches'] else 772,442);page(0);step(prefs['step']);tap(878 if prefs['continuous'] else 638,154)
 elif a.stage=='stop':
  lines=['G21','G90']+['G4P1']*20+['M2'];path=upload('stop','\n'.join(lines)+'\n');start(path);wait(lambda s:s['sent']>0);before=audit();tap(365,472);form('confirmation');tap(760,520)
  wait(lambda s:s['resets']>before['resets']);time.sleep(1);state=data();assert not state['job'] and not state['local_job'];assert data('PREFS')['page']!=5;unlock();close();record('pass',case='confirmed-stop',state=state)
 elif a.stage=='invalidfile':
  path=upload('overlong','G4P0.2\nG4P'+('0'*200)+'.1\nM2\n');start(path);s=wait(lambda s:s['failed'],10);assert s['sent']==0 and not s['complete'],s;shot('stream-validation-error');record('pass',case='preflight-no-transmission',audit=s)
 elif a.stage=='metadata':
  page(2)
  for _ in range(2):cmd('SWIPE 940 440 940 200');time.sleep(.3)
  scroll=data('PREFS')['scroll_y'];tap(850,round(70+658-scroll));form('machine-settings')
  end=time.monotonic()+45
  while data()['settings_loading'] and time.monotonic()<end:time.sleep(.3)
  state=data();assert state['settings_result']==1 and state['settings_groups']>0,state
  observed=settings();baseline=json.loads((a.output/'controller-before.json').read_text())['settings']
  for k,v in baseline.items():
   if v['known'] and observed[k]['value']!='<hidden>':assert observed[k]['value']==v['value'],(k,v,observed[k])
  (a.output/'controller-metadata.json').write_text(json.dumps(observed,indent=2));shot('live-settings');close();page(6);shot('live-inputs');page(0);record('pass',case='metadata-and-diagnostics',count=state['settings_count'],groups=state['settings_groups'])
 elif a.stage=='probe':
  page(0);tap(100,480);form('probe');before=audit();tap(760,500);after=audit();assert after['tx']==before['tx'] and not data()['probing'];shot('live-probe-unarmed');close();record('pass',case='probe-contact-required',audit=after)
 elif a.stage=='tools':
  page(0);tap(254,482);form('tools');before=audit();tap(390,176);form('confirmation');tap(264,520);assert audit()['tx']==before['tx'];tap(254,482);form('tools');tap(390,176);form('confirmation');tap(760,520)
  end=time.monotonic()+8
  while not data()['spindle_direction'] and time.monotonic()<end:time.sleep(.3)
  assert data()['spindle_direction']==1
  wait(lambda s:s['ok']==s['tx'],30)
  tap(254,482);form('tools');tap(860,176);wait(lambda s:s['mode']=='Idle' and s['ok']==s['tx']);time.sleep(.4);assert data()['spindle_direction']==0
  for x,mask in [(390,1),(630,2),(870,3),(150,0)]:
   tap(x,300);wait(lambda s:s['ok']==s['tx'],30)
   end=time.monotonic()+15
   while data()['coolant']!=mask and time.monotonic()<end:time.sleep(.3)
   assert data()['coolant']==mask,data()
  shot('live-tools');close();record('pass',case='spindle-confirmation-off-and-coolant')
 elif a.stage=='critical':
  assert data()['homed'] & 7 == 7,'Soft-limit fault requires successful XYZ homing; $20=1 alone is insufficient'
  lines=['G21','G53G0X100000','M2'];path=upload('soft-limit','\n'.join(lines)+'\n');start(path);s=wait(lambda s:s['failed'] or s['mode']=='Alarm',10);record('critical-alarm',state=data(),audit=s);assert not s['complete'];shot('critical-alarm')
 elif a.stage=='restore':
  close();wait(lambda s:s['mode']=='Idle');page(0);baseline=json.loads((a.output/'controller-before.json').read_text())
  modal=baseline['reports'][2]
  assert modal.startswith('[GC:') and 'M5' in modal and 'M9' in modal
  lines=[modal[4:-1].replace(' ',''),'M2']
  path=upload('restore-modal','\n'.join(lines)+'\n');start(path);finish(lines,60)
  observed=base()
  for k,v in baseline['settings'].items():
   if v['known'] and observed['settings'][k]['value']!='<hidden>':assert observed['settings'][k]['value']==v['value'],(k,v,observed['settings'].get(k))
  assert observed['audit']['writes']==0
  assert observed['reports'][2]==baseline['reports'][2],(baseline['reports'][2],observed['reports'][2])
  for i in range(3,13):assert observed['reports'][i]==baseline['reports'][i],(i,baseline['reports'][i],observed['reports'][i])
  (a.output/'controller-after.json').write_text(json.dumps(observed,indent=2));shot('live-final-jog');record('pass',case='restored-settings-and-offsets')

 if a.stage in ['fault','invalidfile','critical']:
  close();tap(550,30);form('confirmation');tap(760,520);time.sleep(1);unlock();close()
  assert not data()['job'] and data('PREFS')['page']!=5
 if a.stage=='homing':
  close();tap(550,30);form('confirmation');tap(760,520);wait(lambda s:s['mode']=='Idle');close()
 print('PASS',a.stage,flush=True)
except Exception as e:
 record('failure',error=repr(e))
 try:
  if data().get('job') and data('PREFS')['page']==4:
   close();tap(365,472);form('confirmation');tap(760,520);record('failure-stop')
 except Exception as cleanup:record('cleanup-error',error=repr(cleanup))
 raise
finally:
 if port.is_open:
  if a.stage=='tools':
   try:
    close();wait(lambda s:s['ok']==s['tx'],30);page(0);tap(254,482);form('tools');tap(860,176);wait(lambda s:s['ok']==s['tx']);tap(150,300);time.sleep(.5);close()
   except Exception as cleanup:record('tools-cleanup-error',error=repr(cleanup))
  try:cmd('TOUCH ON')
  finally:port.close()
 log.close()
