"""Check WebDAV settings, LCD transfer status and job exclusion on the real pendant."""
import argparse,hashlib,importlib.util,json,threading,time
from pathlib import Path
import http.client as http_client
import serial
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',default='COM12');p.add_argument('--host',default='192.168.0.34')
p.add_argument('--phase',choices=['enable','activity','busy','toggle','final'],required=True)
p.add_argument('--isolated-controller',action='store_true',help='Required for the non-motion busy-job check')
a=p.parse_args();assert a.phase!='busy' or a.isolated_controller
out=Path('captures/webdav');out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('remote',Path(__file__).with_name('ui-remote.py'));r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
port=serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5);port.dtr=port.rts=False;port.port=a.port

def cmd(text):return r.debug(port,text)
def data(text):return json.loads(cmd(text).removeprefix('@UGDATA '))
def tap(x,y):r.exchange(port,'tap',point=(x,y));time.sleep(.3)
def page(index):tap({0:116,1:314,2:908}[index],566);assert data('PREFS')['page']==index

def http(method,path,body=None,status=200):
 c=http_client.HTTPConnection(a.host,47820,timeout=30)
 try:
  c.request(method,path,body=body);res=c.getresponse();result=res.read();assert res.status==status,(res.status,result);return result
 finally:c.close()

def wait(predicate,timeout=20):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  s=data('STATE')
  if predicate(s):return s
  time.sleep(.2)
 raise AssertionError(('Timeout',s))

def shot(name):r.exchange(port,'shot',out/(name+'.png'))

try:
 port.open();r.buffered_lines(port)
 raw_readline=port.readline
 raw=(out/'device-raw.log').open('a',encoding='utf-8')
 def logged_readline():
  line=raw_readline()
  if line and not line.startswith(b'@UG'):
   raw.write(line.decode('utf-8',errors='replace'));raw.flush()
  return line
 port.readline=logged_readline
 for _ in range(20):
  port.write(b'\nUG SYNC\n');end=time.monotonic()+1
  while time.monotonic()<end:
   if b'@UGERR command' in port.readline():break
  else:continue
  break
 else:raise TimeoutError('Console sync')
 time.sleep(.2);port.reset_input_buffer()
 s=data('STATE');assert not any(s[k] for k in ('job','local_job','probing','macro_running')),s
 assert data('AUDIT')['mode']=='Idle';cmd('TOUCH OFF')
 if a.phase=='enable':
  cmd('DAV TEST')
  baseline={'state':s,'audit':data('AUDIT'),'prefs':data('PREFS'),'sd':data('SD STATE')}
  (out/'before.json').write_text(json.dumps(baseline,indent=2))
  page(2)
  for _ in range(8):
   before=data('PREFS')['scroll_y'];cmd('SWIPE 500 440 500 140');time.sleep(.2)
   if abs(before-data('PREFS')['scroll_y'])<5:break
  y=1286-data('PREFS')['scroll_y'];assert 100<y<510,y
  if not data('SD STATE')['webdav']:tap(850,round(y))
  assert data('SD STATE')['webdav']
  end=time.monotonic()+45
  while time.monotonic()<end:
   if data('WIFI STATE')['connected']:break
   time.sleep(.3)
  http('OPTIONS','/sd/');shot('panel-settings-sharing');page(0)
 elif a.phase=='activity':
  page(0);folder='/sd/000-00-serial-webdav-ui'
  c=http_client.HTTPConnection(a.host,47820,timeout=30);c.request('DELETE',folder);response=c.getresponse();response.read();c.close()
  http('MKCOL',folder,status=201)
  payload=(b'G4P0.01\n'*131072)[:1048576];result={}
  def upload():
   try:
    c=http_client.HTTPConnection(a.host,47820,timeout=40);c.putrequest('PUT',folder+'/progress.nc');c.putheader('Content-Length',str(len(payload)));c.endheaders()
    for at in range(0,len(payload),16384):c.send(payload[at:at+16384]);time.sleep(.5)
    response=c.getresponse();result['status']=response.status;response.read();c.close()
   except Exception as e:result['error']=repr(e)
  worker=threading.Thread(target=upload);worker.start();time.sleep(3)
  assert data('SD STATE')['webdav_active'],result;assert data('PREFS')['form_kind']=='sd-transfer',result
  before=data('AUDIT');tap(792,379);after=data('AUDIT');assert before['tx']==after['tx'] and not data('STATE')['job']
  shot('panel-sd-transfer');worker.join(timeout=45);assert not worker.is_alive() and result=={'status':201},result
  assert hashlib.sha256(http('GET',folder+'/progress.nc')).digest()==hashlib.sha256(payload).digest()
  http('DELETE',folder,status=204);time.sleep(2);assert data('PREFS')['form_kind']=='none';page(0)
 elif a.phase=='busy':
  assert data('SD STATE')['local'],'The busy-job check uses display SD'
  name='/000-00-serial-webdav-busy.nc';http('PUT','/sd'+name,b'G21\nG90\n'+b'G4P1\n'*20+b'M2\n',201)
  page(1);time.sleep(1)
  while data('SD STATE')['page']>0:tap(100,494)
  files=data('SD STATE')['files'];row=next(i for i,f in enumerate(files) if f['path']==name)
  tap(906,137+88*row);assert data('PREFS')['form_kind']=='confirmation';tap(760,520)
  wait(lambda s:s['local_job']);http('PUT','/sd/000-00-serial-must-not-exist.nc',b'M2\n',423)
  ping=json.loads(http('GET','/gcam/v1/ping'));assert not ping['accepting']
  tap(365,472);assert data('PREFS')['form_kind']=='confirmation';tap(760,520)
  wait(lambda s:s['online'] and s['identified'] and s['ready'] and not s['job'],30)
  assert data('AUDIT')['mode']=='Idle';time.sleep(.5)
  http('DELETE','/sd'+name,status=204);time.sleep(2);page(0)
 elif a.phase=='toggle':
  page(2)
  for _ in range(8):
   before=data('PREFS')['scroll_y'];cmd('SWIPE 500 440 500 140');time.sleep(.2)
   if abs(before-data('PREFS')['scroll_y'])<5:break
  y=1286-data('PREFS')['scroll_y'];assert 100<y<510,y
  assert data('SD STATE')['webdav'];tap(850,round(y))
  assert not data('SD STATE')['webdav'];http('OPTIONS','/sd/',status=403)
  tap(850,round(y));assert data('SD STATE')['webdav'];http('OPTIONS','/sd/')
  cmd('DAV TEST');shot('panel-settings-sharing');page(0)
 else:
  baseline=json.loads((out/'before.json').read_text());actual={'state':s,'audit':data('AUDIT'),'prefs':data('PREFS'),'sd':data('SD STATE')}
  assert actual['sd']['file_count']==baseline['sd']['file_count'] and actual['sd']['webdav']
  for key in ('x','y','z','diameter','feed','inset','inches','step','continuous','jog_speed','brightness','macro_count','usb_connection','actuator_mode'):
   assert actual['prefs'][key]==baseline['prefs'][key],key
  for key in ('machine','wco'):assert actual['audit'][key]==baseline['audit'][key],key
  assert actual['audit']['writes']==actual['audit']['overflow']==0
  (out/'after.json').write_text(json.dumps(actual,indent=2));page(0)
 print('PASS WebDAV LCD',a.phase,flush=True)
finally:
 if port.is_open:
  try:cmd('TOUCH ON')
  finally:port.close()
