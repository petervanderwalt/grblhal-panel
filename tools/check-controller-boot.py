"""Check automatic USB controller connection after pendant boots; no motion or unlock."""
import argparse
import importlib.util
import json
import time
from pathlib import Path
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='COM12')
parser.add_argument('--cycles', type=int, default=2)
parser.add_argument('--watch-controller-reset', action='store_true')
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('remote', Path(__file__).with_name('ui-remote.py'))
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)
p = serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5)
p.dtr = p.rts = False
p.port = args.port

def cmd(text):
    return r.debug(p, text)

def data(text):
    for attempt in range(3):
        try:
            return json.loads(cmd(text).removeprefix('@UGDATA '))
        except json.JSONDecodeError:
            if attempt == 2:
                raise
            print('Retrying read-only snapshot after interleaved console log', flush=True)

def sync():
    p.write(b'\nUG SYNC\n')
    time.sleep(.4)
    p.reset_input_buffer()

def connected(timeout=35, generation=None):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        u, s = data('USB STATE'), data('STATE')
        if u['connected'] and s['online'] and s['identified'] and s['ready']:
            if generation is None or u['generation'] > generation:
                assert not s['job'] and not s['local_job'] and not s['probing'], s
                return u, s
        time.sleep(.4)
    raise AssertionError(('Automatic connection timed out', u, s))

try:
    p.open()
    r.buffered_lines(p)
    sync()
    assert data('PREFS')['usb_connection'], 'Select USB3 before this live development-board check'
    initial = data('STATE')
    assert not any(initial[k] for k in ('job','local_job','probing','macro_running')) and initial['repeat_axis'] < 0, 'Stop active work before this reboot check'
    for cycle in range(args.cycles):
        p.rts = True
        time.sleep(.15)
        p.rts = False
        time.sleep(5)
        sync()
        cmd('TOUCH OFF')
        u, s = connected()
        if s['alarm']:
            assert data('PREFS')['form_kind'] == 'confirmation', 'Startup alarm did not open its confirmation'
        print('PASS automatic connection after pendant boot', cycle + 1, u['generation'], flush=True)
    if args.watch_controller_reset:
        baseline = data('USB STATE')['generation']
        print('WAITING: reset only the controller now; pendant must remain powered', flush=True)
        u, s = connected(timeout=300, generation=baseline)
        print('PASS controller USB re-enumeration recovered automatically', u['generation'], flush=True)
finally:
    if p.is_open:
        try:
            cmd('TOUCH ON')
        finally:
            p.close()
