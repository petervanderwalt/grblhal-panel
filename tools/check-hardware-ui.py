"""Check actual backlight slider persistence and RGB driver status with UART off."""
import importlib.util,json,time,serial,argparse
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--port',default='COM12');parser.add_argument('--captures',type=Path,default=Path('captures/color-audit/before'));parser.add_argument('--no-captures',action='store_true');parser.add_argument('--restore-brightness',type=int);args=parser.parse_args()
spec=importlib.util.spec_from_file_location('r',Path(__file__).with_name('ui-remote.py'));r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
with serial.Serial(port=None,baudrate=115200,timeout=1,write_timeout=5) as p:
 p.dtr=p.rts=False;p.port=args.port;p.open()
 def sync(delay=.8):
  time.sleep(delay);p.reset_input_buffer()
  for _ in range(30):
   p.write(b'\nUG SYNC\n');end=time.monotonic()+1
   while time.monotonic()<end:
    if b'@UGERR command' in p.readline():time.sleep(.8);p.reset_input_buffer();return
  raise TimeoutError('sync')
 def cmd(s):return r.debug(p,s)
 def data(s='PREFS'):return json.loads(cmd(s).removeprefix('@UGDATA '))
 def tap(x,y):r.exchange(p,'tap',point=(x,y));time.sleep(.3)
 def color(expected):
  time.sleep(.4);s=data();assert s['rgb_ready'] and s['rgb_color']==expected,s
 sync()
 try:
  cmd('DEMO ON');cmd('TOUCH OFF');original=args.restore_brightness if args.restore_brightness is not None else data()['brightness']
  tap(916,82);tap(896,564)
  cmd('SWIPE 950 352 810 352');time.sleep(.3);changed=data()['brightness'];assert 10<=changed<=90,changed
  if not args.no_captures:r.exchange(p,'shot',args.captures/'01-settings.png')
  p.rts=True;time.sleep(.15);p.rts=False;sync(8)
  assert data()['brightness']==changed
  cmd('DEMO ON');cmd('TOUCH OFF');cmd('RX <Idle|FS:0,0>');color(0x00FF00)
  cmd('RX <Run|FS:100,0>');color(0xFF8000)
  cmd('RX <Hold:0|FS:0,0>');color(0xFF8000)
  cmd('RX ALARM:1');color(0xFF0000);tap(630,409);cmd('RX <Idle|FS:0,0>');color(0x00FF00)
  cmd('RX error:20');color(0xFF0000);tap(630,409);cmd('RX <Idle|FS:0,0>');color(0x00FF00)
  tap(896,564)
  end=round(706+(original-10)*250/90);cmd(f'SWIPE 810 352 {end} 352');time.sleep(.3)
  assert data()['brightness']==original
  tap(128,564);tap(130,432)
  if not args.no_captures:r.exchange(p,'shot',args.captures/'02-probe.png')
  print('PASS live brightness, saved value after reset, original brightness restored, RGB Idle/Run/Hold/alarm/error output',flush=True)
 finally:cmd('TOUCH ON')
