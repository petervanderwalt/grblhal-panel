"""Check inputs, accessories and macros on an isolated development display.

Requires CNC mode, no open dialog and an empty macro library. The temporary
macro is deleted on success; no command reaches the controller UART.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='COM17')
parser.add_argument('--captures', type=Path, default=Path('captures/tools'))
parser.add_argument('--only', nargs='+', help='Capture only these screenshot names')
parser.add_argument('--no-captures', action='store_true')
parser.add_argument('--reboot-macro', action='store_true', help='Verify an untouched default-colour macro survives a reboot')
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('remote', Path(__file__).with_name('ui-remote.py'))
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)

with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as p:
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
    sync()

    def cmd(s):
        return r.debug(p, s)

    def data(verb='STATE'):
        return json.loads(cmd(verb).removeprefix('@UGDATA '))

    def tap(x, y):
        r.exchange(p, 'tap', point=(x, y))
        time.sleep(.5)

    def shot(name):
        if not args.no_captures and (not args.only or name in args.only):
            time.sleep(.6)
            r.exchange(p, 'shot', args.captures / (name + '.png'))

    def field(index, text):
        cmd(f'MACRO FIELD {index} ' + text.encode('ascii').hex())

    try:
        cmd('DEMO ON')
        cmd('TOUCH OFF')
        time.sleep(1)
        prefs = data('PREFS')
        assert not prefs['actuator_mode'], 'Select CNC mode first'
        assert prefs['macro_count'] == 0, 'Use a development display with an empty macro library'
        cmd('TEST')
        tap(896, 564)
        tap(592, 96)
        cmd('RX $10=511')
        cmd('RX <Idle|Pn:XDIJ>')
        shot('panel-inputs')
        inputs = data()['inputs']
        assert inputs & (1 << 23) and inputs & (1 << 8)
        cmd('RX <Idle|Pn:j>')
        assert not data()['inputs'] & (1 << 23)
        assert data()['inputs'] & (1 << 8), 'An omitted probe must retain its state'
        cmd('RX <Idle|Pn:i>')
        shot('panel-inputs-inactive')
        tap(916, 62)
        tap(128, 564)
        shot('panel-jog')
        tap(248, 432)
        cmd('RX <Idle|FS:0,0|A:>')
        shot('panel-accessories')
        tap(770, 484)  # Backspace the initial zero using the native numeric keyboard
        for point in ((166, 394), (338, 394), (338, 528), (338, 528), (338, 528)):
            tap(*point)
        tap(422, 166)  # CW requires confirmation
        shot('panel-spindle-confirm')
        tap(630, 409)
        assert data()['spindle_direction'] == 1 and data()['rpm'] == 12000
        assert 'M3 S12000' in cmd('LAST')
        tap(844, 166)  # Off
        assert data()['spindle_direction'] == 0 and 'M5' in cmd('LAST')
        tap(630, 166)  # CCW also requires confirmation
        tap(630, 409)
        assert data()['spindle_direction'] == -1 and 'M4 S12000' in cmd('LAST')
        tap(844, 166)
        tap(530, 274)  # Flood
        assert data()['coolant'] == 2 and 'M8' in cmd('LAST')
        tap(145, 274)  # Off
        assert data()['coolant'] == 0 and 'M9' in cmd('LAST')
        cmd('RX <Hold:0|FS:0,0|A:>')
        before = cmd('LAST')
        tap(530, 274)
        assert cmd('LAST') == before, 'Accessories must lock during Hold'
        cmd('RX <Idle|FS:0,0|A:>')
        tap(916, 56)
        tap(640, 564)
        assert data('PREFS')['page'] == 3
        tap(890, 110)  # Add in header
        field(0, 'Metric / absolute')
        field(1, 'G21\nM8!')
        tap(920, 176)
        assert data('PREFS')['macro_count'] == 0
        field(1, 'G21\nG90')
        shot('panel-macro-editor')
        tap(920, 176)
        assert data('PREFS')['macro_count'] == 1
        if args.reboot_macro:
            p.rts = True
            time.sleep(.15)
            p.rts = False
            sync()
            cmd('DEMO ON')
            cmd('TOUCH OFF')
            time.sleep(1)
            assert data('PREFS')['macro_count'] == 1, 'Default-colour macro did not persist'
            tap(640, 564)
        shot('panel-macros')
        tap(110, 175)  # Run tile
        shot('panel-macro-confirm')
        tap(630, 409)
        assert data()['macro_running'] and '$G' in cmd('LAST')
        tap(128, 564)
        assert data('PREFS')['page'] == 3, 'Keep Stop accessible while a macro runs'
        tap(680, 110)
        shot('panel-macro-stop')
        tap(342, 409)
        cmd('RX <Idle|FS:0,0>')
        time.sleep(.5)
        assert not data()['macro_running']
        tap(110, 175)
        tap(630, 409)
        cmd('RX error:20')
        cmd('RX <Hold:0|FS:0,0>')
        time.sleep(.4)
        shot('panel-macro-error')
        tap(342, 409)  # Dismiss the fault; Stop must remain accessible
        tap(680, 110)
        tap(630, 409)
        assert data('PREFS')['page'] == 0 and not data()['error']
        tap(640, 564)
        tap(232, 175)  # Edit tile
        field(0, 'Metric setup')
        tap(920, 176)
        assert data('PREFS')['macro_count'] == 1
        tap(232, 175)  # Editor contains Delete
        tap(724, 176)
        tap(630, 409)
        assert data('PREFS')['macro_count'] == 0
        tap(128, 564)
        print('PASS: live inputs, spindle keypad/CW/CCW/Off, coolant interlocks, macro validation/save/edit/run/stop/delete and navigation')
    finally:
        cmd('TOUCH ON')
