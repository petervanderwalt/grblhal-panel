"""Check controller SD download/preview/run on the display with UART-off sample feedback."""
import importlib.util
import json
from pathlib import Path
import sys
import time
import serial

spec = importlib.util.spec_from_file_location("remote", Path(__file__).with_name("ui-remote.py"))
remote = importlib.util.module_from_spec(spec)
spec.loader.exec_module(remote)
with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as port:
    port.dtr = port.rts = False
    port.port = "COM12"
    port.open()
    time.sleep(2)
    port.reset_input_buffer()
    for attempt in range(15):
        port.write(b"\nUG SYNC\n")
        if b"@UGERR command" in port.readline():
            break
    else:
        raise TimeoutError("Console sync")
    time.sleep(.2)
    port.reset_input_buffer()
    def command(s):
        return remote.debug(port, s)
    def tap(x, y):
        remote.exchange(port, "tap", point=(x, y))
        time.sleep(.3)
    def state():
        return json.loads(command("STATE").removeprefix("@UGDATA "))
    def shot(name):
        if "--no-captures" not in sys.argv:
            remote.exchange(port, "shot", Path("captures") / name)
    try:
        command("TOUCH OFF")
        command("TEST")
        command("DEMO ON")
        time.sleep(1)
        tap(512, 564)
        tap(888, 112)
        assert state()["files"] == 3
        tap(778, 189)
        assert "$F<=/bracket.nc" in command("LAST")
        time.sleep(1)
        data = state()
        assert data["download_ready"] and data["download_bytes"] > 100 and not data["job"], data
        shot("sd-preview-3d.png")
        tap(836, 198)  # Top
        shot("sd-preview-top.png")
        tap(944, 198)  # 3D; zoom uses physical pinch input
        tap(886, 112)  # Back to files
        tap(922, 189)  # Run selected, separate from preview
        tap(630, 409)
        assert "$F=/bracket.nc" in command("LAST")
        assert state()["job"]
        tap(374, 370)
        assert not state()["job"]
        tap(176, 564)
        print("PASS: SD download, viewer, Top/3D, file round trip and separate confirmed run; physical pinch not injected", flush=True)
    finally:
        command("TOUCH ON")
