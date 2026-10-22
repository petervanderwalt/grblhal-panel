"""Check jog input on the display in UART-disconnected preview mode."""
import importlib.util
from pathlib import Path
import time
import serial
import sys
import json

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
        tap(88, 560)
        tap(490, 272)  # Incremental
        idle()
        tap(68, 297)  # X- Y+
        last = command("LAST")
        assert "$J=G91 G21 X-" in last and " Y" in last and "0x85" not in last, last
        command("RX ok")
        idle()
        tap(714, 272)  # Continuous
        command("HOLD 68 297 700")
        assert "0x85" in command("LAST")
        time.sleep(.5)
        assert "0x85" in command("LAST"), "Jog queued after release"
        idle()
        shot("jog-continuous.png")
        tap(490, 272)
        shot("jog-incremental.png")
        tap(550, 560)  # Zero / Home
        tap(189, 456)  # Home X
        shot("home-x-confirm.png")
        tap(630, 409)
        assert "$HX" in command("LAST")
        command("RX ok")
        idle()
        tap(88, 560)  # Return to Jog and its DRO actions
        tap(386, 202)  # Go to XY work zero, beside the DRO
        tap(630, 409)
        assert "$G" in command("LAST")
        idle()
        tap(638, 202)  # Go to Z work zero
        tap(630, 409)
        assert "$G" in command("LAST")
        idle()
        tap(890, 202)  # Set zero all, separate from go-to-zero motion
        tap(630, 409)
        assert "G10 L20 P0 X0 Y0 Z0" in command("LAST")
        command("RX ok")
        idle()
        tap(282, 136)  # Zero X on DRO
        tap(630, 409)
        assert "G10 L20 P0 X0" in command("LAST")
        command("RX ok")
        idle()
        command("RX <Run|SD:42.5,/test.nc|FS:800,12000>")
        time.sleep(.3)
        tap(767, 32)  # Pause stays available above every page
        assert "0x21" in command("LAST")
        tap(933, 32)
        assert "0x7E" in command("LAST")
        tap(242, 560)  # Files
        tap(899, 560)  # Stop immediately, without confirming
        data = json.loads(command("STATE").removeprefix("@UGDATA "))
        assert not data["job"], "Stop opened a dialog or left the job running"
        tap(88, 560)
        # Stop also stays reachable above a confirmation dialog.
        idle()
        tap(134, 202)  # Home all confirmation beside the DRO
        tap(899, 560)  # Stop closes it, without homing
        time.sleep(.5)
        idle()
        tap(68, 297)
        assert "$J=G91 G21 X-" in command("LAST"), "Dialog remained over jog controls"
        command("RX ok")
        idle()
        print("PASS: jog, DRO set/go zero, navigation, homing, pause/resume and Stop", flush=True)
    finally:
        command("TOUCH ON")
