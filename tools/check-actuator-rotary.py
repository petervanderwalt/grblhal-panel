"""Configure a development rotary A axis and check it on the physical LCD."""
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
    parser.error('--configure-demo is required: this writes a Pan example to A settings')
spec = importlib.util.spec_from_file_location('remote', Path(__file__).with_name('ui-remote.py'))
remote = importlib.util.module_from_spec(spec)
spec.loader.exec_module(remote)
with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as port:
    port.dtr = port.rts = False
    port.port = args.port
    port.open()
    for _ in range(20):
        port.write(b'\nUG SYNC\n')
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            if b'@UGERR command' in port.readline():
                break
        else:
            continue
        break
    else:
        raise TimeoutError('Console sync')
    time.sleep(.2)
    port.reset_input_buffer()
    def command(s):
        return remote.debug(port, s)
    def tap(x, y):
        remote.exchange(port, 'tap', point=(x, y))
        time.sleep(.6)
    def shot(name):
        time.sleep(.6)
        remote.exchange(port, 'shot', Path('captures') / (name + '.png'))
    def idle():
        command('RX <Idle|MPos:0,-75,-50,-45|WCO:0,0,0,0|H:0,15|FS:0,0>')
        time.sleep(.4)
    try:
        command('TOUCH OFF')
        command('DEMO ON')
        time.sleep(1)
        command('TEST')
        command('RX $376=2')  # Simulated B remains rotary when A is changed.
        idle()
        tap(848, 564)
        tap(761, 256)  # Display mm, then configure degree values.
        tap(910, 160)
        command('SWIPE 500 460 500 310')
        time.sleep(.6)
        command('SWIPE 500 460 500 284')
        time.sleep(.6)
        tap(830, 435)
        tap(887, 48)  # A-only Rotary selector.
        controller = json.loads(command('STATE').removeprefix('@UGDATA '))
        assert controller['rotary_mask'] == 2, 'Selecting a type wrote it before Save'
        for index, value in enumerate(('Pan', 'Left', 'Right', '-90', '90', '60')):
            command(f'ACTUATOR FIELD {index} ' + value.encode().hex())
        tap(412, 280)  # Position: angular dial when A is rotary.
        shot('panel-actuator-rotary-config')
        tap(882, 280)
        prefs = json.loads(command('PREFS').removeprefix('@UGDATA '))
        assert prefs['a_rotary'] == 1
        controller = json.loads(command('STATE').removeprefix('@UGDATA '))
        assert controller['rotary_known'] and controller['rotary_mask'] == 3, controller
        assert not (controller['homed'] & 8), 'Type change kept old A homing'
        tap(256, 564)
        idle()
        shot('panel-actuator-rotary')
        tap(936, 408)  # Second endpoint on A's position layout.
        assert 'A90.000 F60.0' in command('LAST')
        idle()
        previous = command('LAST')
        command('SWIPE 850 350 930 340')
        time.sleep(.4)
        last = command('LAST')
        assert last != previous and '$J=G53 G90 G21 A' in last, 'Dial release did not send A jog'
        idle()
        command('RX $376=2')
        idle()
        last = command('LAST')
        tap(936, 408)
        assert command('LAST') == last, 'Mismatched A type accepted a move'
        command('RX $376=3')
        idle()
        tap(848, 564)
        tap(901, 256)
        tap(256, 564)
        shot('panel-actuator-rotary-inch')
        tap(848, 564)
        tap(761, 256)
        command('SWIPE 500 460 500 310')
        time.sleep(.6)
        command('SWIPE 500 460 500 284')
        time.sleep(.6)
        tap(830, 435)
        tap(717, 48)
        tap(882, 280)
        controller = json.loads(command('STATE').removeprefix('@UGDATA '))
        assert controller['rotary_known'] and controller['rotary_mask'] == 2, controller
        idle()
        tap(256, 564)
        tap(848, 564)
        command('SWIPE 500 460 500 310')
        time.sleep(.6)
        command('SWIPE 500 460 500 284')
        time.sleep(.6)
        tap(830, 435)
        tap(887, 48)
        tap(882, 280)
        controller = json.loads(command('STATE').removeprefix('@UGDATA '))
        assert controller['rotary_known'] and controller['rotary_mask'] == 3, controller
        tap(256, 564)
        idle()
        print('PASS: A type writes and readback, preserved B bit, endpoint, dial, mismatch and inch display', flush=True)
    finally:
        command('TOUCH ON')
