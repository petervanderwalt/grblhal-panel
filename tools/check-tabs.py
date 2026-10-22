"""Check tab navigation on COM12 with the controller UART disconnected."""
import importlib.util
import json
from pathlib import Path
import time
import serial

spec = importlib.util.spec_from_file_location("remote", Path(__file__).with_name("ui-remote.py"))
remote = importlib.util.module_from_spec(spec)
spec.loader.exec_module(remote)

with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as port:
    port.dtr = port.rts = False
    port.port = "COM12"
    port.open()
    for _ in range(15):
        port.write(b"\nUG SYNC\n")
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            if b"@UGERR command" in port.readline():
                break
        else:
            continue
        break
    else:
        raise TimeoutError("Console sync")
    time.sleep(.2)
    port.reset_input_buffer()
    try:
        remote.debug(port, "TOUCH OFF")
        remote.debug(port, "DEMO ON")
        time.sleep(.4)
        for target in (1, 2, 0, 2, 1, 0):
            remote.exchange(port, "tap", point=((176, 512, 848)[target], 564))
            time.sleep(.3)
            prefs = json.loads(remote.debug(port, "PREFS").removeprefix("@UGDATA "))
            assert prefs["page"] == target, prefs
        # Back-to-back selections must cancel the previous slide cleanly.
        for x in (512, 848, 176):
            remote.exchange(port, "tap", point=(x, 564))
        time.sleep(.3)
        prefs = json.loads(remote.debug(port, "PREFS").removeprefix("@UGDATA "))
        assert prefs["page"] == 0, prefs
        print("PASS: tab navigation in both directions and interrupted transitions")
    finally:
        remote.debug(port, "TOUCH ON")
