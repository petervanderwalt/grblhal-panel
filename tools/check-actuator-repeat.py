"""Check the Repeat layout on the LCD using isolated controller feedback."""
import argparse
import importlib.util
import json
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='COM17')
parser.add_argument('--configure-demo', action='store_true')
args = parser.parse_args()
if not args.configure_demo:
    parser.error('--configure-demo is required: this selects Repeat for the Z actuator')
spec = importlib.util.spec_from_file_location('remote', Path(__file__).with_name('ui-remote.py'))
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)
with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as p:
    p.dtr = p.rts = False
    p.port = args.port
    p.open()
    for _ in range(20):
        p.write(b'\nUG SYNC\n')
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            if b'@UGERR command' in p.readline():
                break
        else:
            continue
        break
    else:
        raise TimeoutError('Console sync')
    time.sleep(.2)
    p.reset_input_buffer()
    def cmd(s):
        return r.debug(p, s)
    def state():
        return json.loads(cmd('STATE').removeprefix('@UGDATA '))
    def tap(x, y):
        r.exchange(p, 'tap', point=(x, y))
        time.sleep(.6)
    def idle(z=-50):
        cmd(f'RX <Idle|MPos:0,-75,{z},-45|WCO:0,0,0,0|H:0,15|FS:0,0>')
        time.sleep(.5)
    def shot(name):
        time.sleep(.7)
        r.exchange(p, 'shot', Path('captures') / (name + '.png'))
    try:
        cmd('DEMO ON')
        cmd('TOUCH OFF')
        time.sleep(1)
        cmd('RX $376=1')
        idle()
        tap(848, 564)
        cmd('SWIPE 500 460 500 310')
        time.sleep(.6)
        cmd('SWIPE 500 460 500 284')
        time.sleep(.6)
        tap(830, 347)
        for index, text in ((3, '0'), (4, '-100'), (5, '100')):
            cmd(f'ACTUATOR FIELD {index} ' + text.encode().hex())
        tap(707, 280)
        shot('panel-actuator-repeat-config')
        tap(882, 280)
        tap(256, 564)
        idle()
        tap(592, 386)
        assert state()['repeat_axis'] == 2
        assert 'Z-100.000 F50.0' in cmd('LAST')
        idle(-100)
        assert 'Z0.000 F50.0' in cmd('LAST'), 'Endpoint did not reverse direction'
        cmd('RX <Jog|MPos:0,-75,-60,-45|H:0,15|FS:0,0>')
        cmd('SWIPE 702 360 702 290')
        time.sleep(.5)
        assert '0x85' in cmd('LAST'), 'Speed change did not cancel the old leg'
        idle(-60)
        last = cmd('LAST')
        assert 'Z0.000 F' in last and 'F50.0' not in last, last
        shot('panel-actuator-repeat')
        idle(0)
        assert 'Z-100.000 F' in cmd('LAST')
        tap(633, 464)
        assert state()['repeat_axis'] == -1 and '0x85' in cmd('LAST')
        idle(-30)
        assert '0x85' in cmd('LAST'), 'Stop allowed another leg'
        tap(592, 386)
        assert state()['repeat_axis'] == 2
        tap(848, 564)
        assert state()['repeat_axis'] == -1, 'Leaving the page did not disarm Repeat'
        tap(256, 564)
        idle(-30)
        cmd('SWIPE 702 320 702 418')
        time.sleep(.4)
        last = cmd('LAST')
        tap(592, 386)
        assert cmd('LAST') == last and state()['repeat_axis'] == -1, 'Zero speed started motion'
        cmd('SWIPE 702 418 702 334')
        time.sleep(.4)
        print('PASS: repeating endpoints, speed restart, Stop, zero speed and navigation disarm', flush=True)
    finally:
        cmd('TOUCH ON')
