"""Check jog input on the display in UART-disconnected preview mode."""
import importlib.util
from pathlib import Path
import time
import serial
import sys

spec = importlib.util.spec_from_file_location("remote", Path(__file__).with_name("ui-remote.py"))
remote = importlib.util.module_from_spec(spec)
spec.loader.exec_module(remote)

with serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5) as port:
    port.dtr = port.rts = False
    port.port = "COM12"
    port.open()
    time.sleep(2)
    port.reset_input_buffer()
    port.write(b"\nUG SYNC\n")
    deadline = time.monotonic() + 15
    while b"@UGERR command" not in port.readline():
        if time.monotonic() > deadline:
            raise TimeoutError("Console sync")
    time.sleep(.2)
    port.reset_input_buffer()
    def command(s):
        return remote.debug(port, s)
    def tap(x, y):
        remote.exchange(port, "tap", point=(x, y))
        time.sleep(.3)
    def idle():
        command("RX <Idle|MPos:125,84.5,-12|WCO:100,50,-20|FS:0,0|Pn:i>")
        time.sleep(.3)
    def shot(name):
        if "--no-captures" not in sys.argv:
            remote.exchange(port, "shot", Path("captures") / name)
    try:
        command("TOUCH OFF")
        command("TEST")
        command("DEMO ON")
        time.sleep(1)
        tap(70, 106)
        tap(225, 215)  # Incremental
        idle()
        tap(205, 290)  # X- Y+
        last = command("LAST")
        assert "$J=G91 G21 X-" in last and " Y" in last and "0x85" not in last, last
        command("RX ok")
        idle()
        tap(390, 215)  # Continuous
        command("HOLD 205 290 700")
        assert "0x85" in command("LAST")
        time.sleep(.5)
        assert "0x85" in command("LAST"), "Jog queued after release"
        idle()
        shot("jog-continuous.png")
        tap(225, 215)
        shot("jog-incremental.png")
        tap(680, 342)  # Home X
        shot("home-x-confirm.png")
        tap(630, 409)
        assert "$HX" in command("LAST")
        command("RX ok")
        idle()
        tap(680, 460)  # Zero X
        tap(630, 409)
        assert "G10 L20 P0 X0" in command("LAST")
        command("RX ok")
        idle()
        print("PASS: diagonal incremental, continuous release, Home X, Zero X", flush=True)
    finally:
        command("TOUCH ON")
