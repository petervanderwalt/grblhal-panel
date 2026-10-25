"""Exercise grouped settings on the LCD with isolated controller feedback.

Start on Jog/Actuators/Settings with no dialog open. Changes affect demo values
only; physical touch is restored on exit.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='COM17')
parser.add_argument('--captures', type=Path, default=Path('captures/machine-settings'))
parser.add_argument('--only', nargs='+', help='Capture only these screenshot names')
parser.add_argument('--no-captures', action='store_true')
args = parser.parse_args()
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

    def shot(name):
        if args.no_captures or (args.only and name not in args.only):
            return
        time.sleep(.7)
        r.exchange(p, 'shot', args.captures / (name + '.png'))

    def field(value):
        cmd('SETTING FIELD ' + value.encode('ascii').hex())

    def find(setting):
        tap(258, 120)
        field(f'${setting} ')
        tap(892, 135)
        tap(859, 194)

    def saved():
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            s = state()
            if not s['settings_saving']:
                assert s['settings_result'] == 1, s
                assert s['settings_count'] >= 20, s
                return
            time.sleep(.2)
        raise TimeoutError('Settings save/readback')

    try:
        cmd('DEMO ON')
        cmd('TOUCH OFF')
        time.sleep(1)
        tap(848, 564)
        shot('panel-settings-root')
        tap(831, 96)
        time.sleep(2)
        s = state()
        assert s['settings_result'] == 1 and s['settings_groups'] >= 8, s
        shot('panel-machine-settings')
        tap(148, 282)
        shot('panel-machine-settings-homing')
        tap(148, 186)
        tap(922, 540)
        shot('panel-machine-settings-page2')
        tap(362, 540)

        find(0)
        shot('panel-machine-setting-number')
        field('9999')
        tap(892, 248)
        assert not state()['settings_saving'], 'Out-of-range value was sent'
        field('12.5')
        tap(892, 248)
        saved()

        find(13)
        shot('panel-machine-setting-switch')
        tap(377, 264)
        tap(892, 532)
        saved()

        find(22)
        shot('panel-machine-setting-mask')
        tap(46, 232)
        tap(892, 532)
        saved()

        find(23)
        shot('panel-machine-setting-axes')
        tap(528, 300)  # Metadata exposes A even when the current status has three axes.
        tap(892, 532)
        saved()
        find(32)
        shot('panel-machine-setting-options')
        tap(956, 48)
        find(74)
        field('Workshop pendant')
        shot('panel-machine-setting-text')
        tap(892, 248)
        saved()
        find(75)
        field('demo-only-replacement')
        shot('panel-machine-setting-password')
        tap(892, 248)
        saved()
        find(302)
        field('192.168.0.90')
        shot('panel-machine-setting-ip')
        tap(892, 248)
        saved()
        find(13)
        tap(147, 264)
        tap(892, 532)
        saved()
        tap(956, 48)
        tap(256, 564)
        tap(848, 564)
        print('PASS: groups, pagination, search, typed editors, validation and save/readback')
    finally:
        cmd('TOUCH ON')
