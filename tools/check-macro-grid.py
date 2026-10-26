"""Fill all 20 macro slots, reboot, verify and remove the temporary library.

Requires CNC mode, no open dialog and an empty development macro library.
Samples are 768 characters by default; --short-samples uses compact photo examples.
Only G21/G90 execute, in UART-off demo mode.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='COM12')
parser.add_argument('--short-samples', action='store_true', help='Use compact G21/G90 examples for documentation photos')
parser.add_argument('--no-captures', action='store_true')
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('remote', Path(__file__).with_name('ui-remote.py'))
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)

with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as p:
    r.buffered_lines(p)
    p.dtr = p.rts = False
    p.port = args.port
    p.open()

    def sync():
        for _ in range(25):
            p.write(b'\nUG SYNC\n')
            deadline = time.monotonic() + 1
            while time.monotonic() < deadline:
                if b'@UGERR command' in p.readline():
                    time.sleep(.2)
                    p.reset_input_buffer()
                    return
        raise TimeoutError('Console sync')

    def cmd(s):
        return r.debug(p, s)

    def data(verb='PREFS'):
        return json.loads(cmd(verb).removeprefix('@UGDATA '))

    def tap(x, y):
        r.exchange(p, 'tap', point=(x, y))
        time.sleep(.35)

    def field(index, value):
        cmd(f'MACRO FIELD {index} ' + value.encode('ascii').hex())

    sync()
    try:
        cmd('DEMO ON')
        cmd('TOUCH OFF')
        assert not data()['actuator_mode'] and data()['macro_count'] == 0
        tap(512, 566)
        base = 'G21\nG90\n('
        code = 'G21\nG90' if args.short_samples else base + 'x' * (768 - len(base) - 1) + ')'
        for i in range(20):
            tap(408, 52)
            field(0, f'Setup {i + 1:02}')
            field(1, code)
            if i % 4:  # Untouched Blue must persist just like an explicitly clicked colour.
                tap(640 + (i % 4) * 105, 104)
            tap(902, 204)
            assert data()['macro_count'] == i + 1, f'Save failed at slot {i + 1}'
        if not args.no_captures:
            r.exchange(p, 'shot', Path('captures/tools/panel-macros-full.png'))
        p.rts = True
        time.sleep(.15)
        p.rts = False
        sync()
        cmd('DEMO ON')
        cmd('TOUCH OFF')
        assert data()['macro_count'] == 20, 'Macro library did not persist'
        tap(512, 566)
        tap(850, 462)  # Last tile (row five, column four)
        if not args.no_captures:
            r.exchange(p, 'shot', Path('captures/tools/panel-macro-confirm.png'))
        tap(760, 520)
        assert data('STATE')['macro_running'] and '$G' in cmd('LAST')
        cmd('RX <Idle|FS:0,0>')
        time.sleep(.4)
        for i in range(20):
            tap(236 + (i % 4) * 244, 134 + (i // 4) * 82)
            tap(720, 204)
            tap(760, 520)
            assert data()['macro_count'] == 19 - i
        tap(154, 566)
        print('PASS: all 20 temporary macros saved, survived reboot, ran and were removed')
    finally:
        cmd('TOUCH ON')
