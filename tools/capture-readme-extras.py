"""Capture remaining README states on the real LCD with controller UART isolated."""
import argparse
import importlib.util
import json
import time
from pathlib import Path
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='COM12')
parser.add_argument('--reset', action='store_true', help='Reset before the capture sequence')
parser.add_argument('--scene', nargs='+', required=True,
                    choices=['actuators', 'actuator-configs', 'top', 'isometric', 'perspective', 'fallback', 'progress', 'disconnected', 'machine', 'continuous', 'inch', 'run-confirm', 'alarm', 'wifi-entry'])
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('remote', Path(__file__).with_name('ui-remote.py'))
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)
out = Path('captures/gallery-refresh/extras')
out.mkdir(parents=True, exist_ok=True)

with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as p:
    p.dtr = p.rts = False
    p.port = args.port
    p.open()
    r.buffered_lines(p)
    if args.reset:
        p.rts = True
        time.sleep(.15)
        p.rts = False
        time.sleep(8)
    for _ in range(30):
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
    time.sleep(.8)
    p.reset_input_buffer()

    def cmd(s):
        return r.debug(p, s)
    def data(s='PREFS'):
        return json.loads(cmd(s).removeprefix('@UGDATA '))
    def tap(x, y):
        r.exchange(p, 'tap', point=(x, y))
        time.sleep(.4)
    def shot(name):
        r.exchange(p, 'shot', out / (name + '.png'))
    def idle(four=False):
        cmd('RX <Idle|MPos:0,-75,-50' + (',-45' if four else '') +
            '|WCO:0,0,0' + (',0' if four else '') + '|H:0,15|FS:0,0|Pn:i>')
        time.sleep(.4)
    def settings():
        tap(128, 564)
        tap(896, 564)
    def scroll(target):
        for _ in range(8):
            delta = target-data()['scroll_y']
            if abs(delta) <= 2:
                return
            delta = max(-280, min(280, delta))
            start = 450 if delta > 0 else 200
            cmd(f'SWIPE 500 {start} 500 {start-delta}')
            time.sleep(.6)
        assert abs(target-data()['scroll_y']) <= 8, data()
    def server(value):
        settings()
        tap(380, 96)
        cmd('PREVIEW SERVER ' + value)
        tap(818, 521)
        assert data('SD STATE')['server'] == value
    def find(path):
        tap(384, 564)
        assert data('SD STATE')['local']
        for _ in range(data('SD STATE')['page']):
            tap(120, 478)
        for _ in range(128):
            sd = data('SD STATE')
            row = next((i for i, f in enumerate(sd['files']) if f['path'].lower() == path.lower()), None)
            if row is not None:
                return row, sd['files'][row]
            assert sd['page'] * 4 + 4 < sd['file_count'], path
            tap(900, 478)
        raise AssertionError(path)

    original_server = None
    original_inches = None
    try:
        cmd('DEMO ON')
        cmd('TOUCH OFF')
        time.sleep(1)
        original_inches = data()['inches']
        if data()['actuator_mode']:
            settings()
            tap(760, 160)
        assert not data()['actuator_mode']
        if data()['local_preview']:
            tap(56, 112)
        if not data('SD STATE')['local']:
            settings()
            tap(830, 266)
            tap(830, 313)
        idle()
        for scene in args.scene:
            if scene in ('actuators', 'actuator-configs'):
                settings()
                tap(761, 448)
                tap(900, 160)
                assert data()['actuator_mode']
                if scene == 'actuators':
                    shot('panel-settings-actuator-mode')
                    tap(128, 564)
                    cmd('RX $376=1')
                    idle(True)
                    shot('panel-actuators')
                    cmd('RX <Idle|MPos:0,-100,-25,45|WCO:0,0,0,0|H:0,15|FS:0,0>')
                    shot('panel-actuator-rotary')
                    cmd('SWIPE 702 350 702 300')
                    shot('panel-actuator-repeat')
                settings()
                scroll(500)
                shot('panel-actuator-settings')
                tap(830, 76+580+38-data()['scroll_y'])
                shot('panel-actuator-config')
                tap(120, 280)
                settings()
                scroll(500)
                tap(830, 76+580+3*88+38-data()['scroll_y'])
                shot('panel-actuator-rotary-config')
                tap(120, 280)
                settings()
                tap(760, 160)
                assert not data()['actuator_mode']
            elif scene == 'wifi-entry':
                tap(900, 30)
                for _ in range(90):
                    wifi = data('WIFI STATE')
                    if not wifi['scanning'] and 'Braai-Fi' in wifi['networks']:
                        break
                    if not wifi['scanning']:
                        tap(682, 166)
                    time.sleep(.5)
                index = wifi['networks'].index('Braai-Fi')
                assert index < 4
                tap(300, 233+66*index)
                shot('wifi-password')
                tap(844, 259)
                shot('wifi-password-validation')
                tap(177, 259)
                tap(916, 92)
            elif scene == 'inch':
                settings()
                tap(901, 448)
                tap(128, 564)
                idle()
                shot('panel-jog-inch')
            elif scene == 'run-confirm':
                settings()
                tap(830, 266)
                tap(830, 350)
                tap(384, 564)
                tap(922, 189)
                assert not data('STATE')['job']
                shot('panel-run-confirm')
                tap(342, 409)
                settings()
                tap(830, 266)
                tap(830, 313)
            elif scene == 'alarm':
                cmd('RX ALARM:1')
                shot('panel-alarm')
                tap(630, 409)
                idle()
            elif scene == 'continuous':
                tap(128, 564)
                for _ in range(60):
                    if data()['can_setup']:
                        break
                    time.sleep(.2)
                tap(885, 184)
                assert data()['continuous'] == 1 and data()['mode_checked'] == 1
                cmd('HOLD 578 268 700')
                assert '0x85' in cmd('LAST'), 'Continuous jog did not stop on release'
                cmd('RX ok')
                idle()
                shot('panel-jog-continuous')
                tap(631, 184)
                assert data()['continuous'] == 0 and data()['mode_checked'] == 0
            elif scene == 'machine':
                settings()
                tap(820, 96)
                for _ in range(60):
                    if not data('STATE')['settings_loading']:
                        break
                    time.sleep(.3)
                for number, name in ((75, 'password'), (302, 'ip')):
                    tap(258, 120)
                    cmd('SETTING FIELD ' + f'${number} '.encode().hex())
                    tap(892, 135)
                    tap(859, 194)
                    shot('panel-machine-setting-' + name)
                    tap(956, 48)
                tap(956, 48)
            elif scene in ('top', 'isometric', 'perspective', 'fallback', 'progress'):
                if scene == 'fallback':
                    original_server = data('SD STATE')['server']
                    server('http://127.0.0.1:9')
                    path = '/rotary/3D_Finish_1_1.gcode'
                else:
                    path = '/mb_83.nc' if scene in ('top', 'isometric', 'perspective') else '/MB-6 test.nc'
                row, entry = find(path)
                if scene == 'fallback':
                    assert not entry['preview'], 'Fallback needs a file without cached images'
                tap(778, 189 + 72 * row)
                assert data()['local_preview'] and not data('STATE')['job']
                if scene in ('top', 'isometric', 'perspective'):
                    for _ in range(300):
                        if data('SD STATE')['files'][row]['preview']:
                            break
                        time.sleep(1)
                    assert data('SD STATE')['files'][row]['preview']
                    view = {'top':0, 'isometric':6, 'perspective':7}[scene]
                    time.sleep(.5)
                    tap(885, 190)
                    cmd('SWIPE 890 260 890 440' if view == 0 else 'SWIPE 890 440 890 260')
                    time.sleep(.3)
                    tap(885, 246 if view == 0 else 401+38*(view-6))
                    assert data()['preview_view'] == view
                elif scene == 'progress':
                    sd = data('SD STATE')
                    assert sd['preparing'] and sd['percent'] >= 0, sd
                shot('panel-preview-' + ('text' if scene == 'fallback' else scene))
                tap(56, 112)
                if original_server is not None:
                    server(original_server)
                    original_server = None
            else:
                settings()
                for _ in range(5):
                    cmd('SWIPE 500 460 500 160')
                    time.sleep(.3)
                tap(250, 450)
                tap(128, 564)
                assert not data('STATE')['online']
                shot('panel-jog-disconnected')
                cmd('DEMO ON')
                idle()
        print('PASS extra gallery scenes', flush=True)
    finally:
        if original_server is not None:
            server(original_server)
        if data()['actuator_mode']:
            settings()
            tap(760, 160)
        if original_inches is not None:
            settings()
            tap(901 if original_inches else 761, 448)
        tap(128, 564)
        cmd('TOUCH ON')
