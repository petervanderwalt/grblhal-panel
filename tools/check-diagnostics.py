"""Check the read-only Diagnostics page, navigation and real-LCD input LEDs."""
import argparse
import importlib.util
import json
from pathlib import Path
import time
import serial

spec = importlib.util.spec_from_file_location('remote', Path(__file__).with_name('ui-remote.py'))
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)
a = argparse.ArgumentParser()
a.add_argument('--port', default='COM12')
a.add_argument('--captures', type=Path, default=Path('captures/diagnostics'))
args = a.parse_args()
args.captures.mkdir(parents=True, exist_ok=True)
p = serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5)
p.dtr = p.rts = False
p.port = args.port

def cmd(text):
    return r.debug(p, text)

def data(kind='PREFS'):
    return json.loads(cmd(kind).removeprefix('@UGDATA '))

def tap(x, y):
    r.exchange(p, 'tap', point=(x, y))
    time.sleep(.6)

def idle(pins='i'):
    cmd('RX <Idle|MPos:125.375,84.5,-12.25|WCO:100,50,-20|FS:0,0|Pn:' + pins + '>')
    time.sleep(.2)

def shot(name):
    time.sleep(.4)
    r.exchange(p, 'shot', args.captures / (name + '.png'))

original = None
try:
    p.open()
    r.buffered_lines(p)
    for _ in range(15):
        p.write(b'\nUG SYNC\n')
        end = time.monotonic() + 1
        while time.monotonic() < end:
            if b'@UGERR command' in p.readline():
                break
        else:
            continue
        break
    else:
        raise TimeoutError('Console sync')
    time.sleep(.3)
    p.reset_input_buffer()
    cmd('DEMO ON')
    cmd('TOUCH OFF')
    idle()
    original = data()
    assert not original['actuator_mode'] and original['dialog'] == 'none', 'Start in CNC mode without a dialog'
    tap(900, 4)
    assert data()['dialog'] == 'other', 'The header must remain tappable at the bezel'
    tap(916, 92)
    assert data()['dialog'] == 'none'
    for x, page, name in ((154, 0, 'panel-jog'), (333, 1, 'panel-files'),
                          (512, 3, 'panel-macros'), (870, 2, 'panel-settings')):
        tap(x, 566)
        d = data()
        assert d['page'] == page and d['dialog'] == 'none'
        assert tuple(d[k] for k in ('header_x', 'header_y', 'header_w', 'header_h')) == (502, -28, 506, 96)
        idle()
        shot(name)
    tap(691, 566)
    assert data()['page'] == 6 and data()['dialog'] == 'none'
    cmd('RX $10=511')
    idle('XDIJ')
    pins = data('STATE')['inputs']
    assert pins & (1 << 23) and pins & (1 << 8)
    shot('panel-inputs')
    idle('j')
    pins = data('STATE')['inputs']
    assert not pins & (1 << 23) and pins & (1 << 8), 'Omitted probe state must be retained'
    idle('i')
    shot('panel-inputs-inactive')
    cmd('RX $10=0')
    idle()
    shot('panel-inputs-unknown')
    cmd('RX $10=511')
    idle()
    tap(870, 566)
    cmd('SWIPE 940 450 940 150')
    cmd('SWIPE 940 450 940 250')
    time.sleep(.4)
    shot('panel-settings-services')
    # Probe configuration follows Machine settings directly after removing Inputs.
    y = 70 + 794 - data()['scroll_y']
    assert 110 < y < 480
    tap(850, round(y))
    assert data()['dialog'] == 'probe-offsets'
    shot('panel-probe-configuration')
    tap(690, 108)
    tap(154, 566)
    tap(691, 566)
    assert data()['page'] == 6 and data()['dialog'] == 'none'
    for key in ('x', 'y', 'z', 'feed', 'inset', 'diameter', 'tx', 'rx', 'baud', 'inches', 'macro_count', 'brightness'):
        assert data()[key] == original[key], key
    cmd('TEST')
    print('PASS five tabs, live/retained/released/unknown inputs, Settings removal, Probe Cancel and unchanged preferences')
finally:
    if p.is_open:
        cmd('DEMO ON')
        idle()
        cmd('TOUCH ON')
        p.close()
