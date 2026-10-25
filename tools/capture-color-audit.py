"""Capture final colour roles on hardware; individual scenes can be retried."""
import argparse,importlib.util,json,time,serial
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--port',default='COM12');parser.add_argument('--scene',nargs='+',default=['jog','settings','machine','probe','warning','error','job','paused','complete','files','wifi','macros','actuators','surface','corner','numpad','controller','server']);args=parser.parse_args()
spec=importlib.util.spec_from_file_location('r',Path(__file__).with_name('ui-remote.py'));r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
names=['jog','settings','machine','probe','warning','error','job','paused','complete','files','wifi','macros','actuators','surface','corner','numpad','controller','server']
with serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5) as p:
 p.dtr=p.rts=False;p.port=args.port;p.open()
 def sync():
  for _ in range(30):
   p.write(b'\nUG SYNC\n');end=time.monotonic()+1
   while time.monotonic()<end:
    if b'@UGERR command' in p.readline():time.sleep(.8);p.reset_input_buffer();return
  raise TimeoutError('sync')
 def cmd(s):return r.debug(p,s)
 def data(s='STATE'):return json.loads(cmd(s).removeprefix('@UGDATA '))
 def tap(x,y):r.exchange(p,'tap',point=(x,y));time.sleep(.4)
 def idle():cmd('RX <Idle|MPos:125,84.5,-12|WCO:100,50,-20|FS:0,0|Pn:i>');time.sleep(.4)
 def job():cmd('RX <Run|SD:42.5,/bracket.nc|MPos:125,84.5,-12|FS:800,12000|Ov:100,100,100>');time.sleep(.5)
 def stop():tap(374,370);tap(630,409);time.sleep(.4)
 sync()
 try:
  for scene in args.scene:
   assert scene in names
   # Always clean up the previous demo scene before creating the next one.
   page=data('PREFS')['page']
   if page==5:tap(512,366)
   elif page==4:stop()
   cmd('DEMO ON');cmd('TOUCH OFF');time.sleep(1);idle();tap(128,564)
   for _ in range(60):
    if data('PREFS').get('can_setup'):break
    time.sleep(.2)
   assert data('PREFS')['can_setup']
   if scene=='settings':tap(896,564)
   elif scene=='machine':
    tap(896,564);tap(820,96)
    for _ in range(60):
     if not data()['settings_loading']:break
     time.sleep(.3)
   elif scene in ('probe','warning','surface','corner','numpad'):
    tap(130,432);assert data('PREFS')['probe_open'];tap(280,154);tap(500,218);tap(500,265)
    if scene=='surface':tap(500,218);tap(500,341)
    elif scene=='corner':tap(720,154)
    cmd('RX <Idle|FS:0,0|Pn:I>');time.sleep(.3);cmd('RX <Idle|FS:0,0|Pn:i>');time.sleep(.3)
    assert data()['contact']
    if scene=='warning':tap(720,486)
    elif scene=='numpad':tap(280,295)
   elif scene=='error':cmd('RX error:20');time.sleep(.4)
   elif scene in ('job','paused','complete'):
    job();assert data('PREFS')['page']==4
    if scene=='paused':cmd('RX <Hold:0|SD:42.5,/bracket.nc|FS:0,12000>');time.sleep(.4)
    if scene=='complete':cmd('RX [MSG:Program End]');idle();assert data('PREFS')['page']==5
   elif scene=='files':tap(384,564)
   elif scene=='controller':tap(896,564);tap(830,266);tap(830,350);tap(384,564)
   elif scene=='server':tap(896,564);tap(380,96)
   elif scene=='wifi':
    tap(900,30)
    for _ in range(60):
     if not data('WIFI STATE')['scanning']:break
     time.sleep(.3)
   elif scene=='macros':tap(640,564)
   elif scene=='actuators':tap(896,564);tap(900,160);assert data('PREFS')['actuator_mode'];tap(128,564)
   time.sleep(.5)
   path=Path('captures/color-audit/after')/f'{names.index(scene)+1:02}-{scene}.png'
   r.exchange(p,'shot',path);print('CAPTURED',scene,flush=True)
   if scene=='machine':tap(956,48)
   elif scene in ('probe','surface','corner'):tap(916,82)
   elif scene=='numpad':tap(380,459);tap(916,82)
   elif scene=='warning':tap(342,409);tap(916,82)
   elif scene=='error':tap(630,409)
   elif scene=='wifi':tap(916,92)
   elif scene=='controller':tap(896,564);tap(830,266);tap(830,313)
   elif scene=='server':tap(940,55)
   elif scene=='actuators':tap(896,564);tap(760,160);assert not data('PREFS')['actuator_mode']
  page=data('PREFS')['page']
  if page==4:stop()
  elif page==5:tap(512,366)
  idle();tap(128,564)
  print('PASS selected colour-audit scenes',flush=True)
 finally:cmd('TOUCH ON')
