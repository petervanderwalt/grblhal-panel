"""Verify spindle discovery and confirmed activation on the UART-isolated display."""
import argparse
import importlib.util
import json
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='COM12')
parser.add_argument('--only', nargs='+', help='Capture only these screenshot names')
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('remote', Path(__file__).with_name('ui-remote.py'))
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)
with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as p:
    p.dtr = p.rts = False
    p.port = args.port
    p.open()
    for _ in range(25):
        p.write(b'\nUG SYNC\n')
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            if b'@UGERR command' in p.readline():
                time.sleep(.2)
                p.reset_input_buffer()
                break
        else:
            continue
        break
    else:
        raise TimeoutError('Console sync')
    def cmd(s):
        return r.debug(p, s)
    def data():
        return json.loads(cmd('STATE').removeprefix('@UGDATA '))
    def tap(x, y):
        r.exchange(p, 'tap', point=(x, y))
        time.sleep(.6)
    def shot(name):
        if args.only and name not in args.only:
            return
        r.exchange(p, 'shot', Path('captures/tools') / (name + '.png'))
    try:
        cmd('DEMO ON')
        cmd('TOUCH OFF')
        time.sleep(1)
        cmd('TEST')
        tap(128, 564)
        tap(248, 432)
        cmd('RX <Idle|FS:0,0|A:>')
        shot('panel-accessories')
        tap(770, 57)
        assert data()['spindle_count'] == 3
        shot('panel-spindles')
        tap(720, 188)
        shot('panel-spindle-activate')
        tap(342, 409)  # Cancel must not activate.
        assert data()['active_spindle'] == 0
        tap(248, 432)
        tap(770, 57)
        tap(720, 188)
        tap(630, 409)
        assert data()['active_spindle'] == 1 and not data()['error']
        shot('panel-accessories-vfd')
        tap(770, 57)
        shot('panel-spindles-active')
        tap(916, 57)
        tap(248, 432)
        cmd('RX <Idle|FS:0,1000|A:S>')
        tap(770, 57)
        tap(280, 188)  # Running spindle must block switching.
        assert data()['active_spindle'] == 1
        cmd('RX <Idle|FS:0,0|A:>')
        tap(280, 188)
        tap(630, 409)
        assert data()['active_spindle'] == 0
        tap(916, 57)
        tap(384, 564)
        tap(128, 564)
        print('PASS discovery, cancel, confirmed M104 selection/readback, running interlock and tab round trip')
    finally:
        cmd('TOUCH ON')
